// fanctld - privileged fan control daemon.
//
// Runs as root (installed as a LaunchDaemon), owns the SMC connection and the
// control loop, and exposes a tiny line protocol on a unix socket so the
// unprivileged GUI/CLI never needs root itself.
#include "smc.h"
#include "ipc.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

enum class Mode { Auto, Manual, Curve };

// 折线插值：低于首点取首点，高于末点取末点
double evalCurve(const std::vector<std::pair<double, double>> &pts, double temp) {
    if (pts.empty()) return 0;
    if (temp <= pts.front().first) return pts.front().second;
    if (temp >= pts.back().first) return pts.back().second;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (temp <= pts[i].first) {
            const double t0 = pts[i - 1].first, t1 = pts[i].first;
            const double r0 = pts[i - 1].second, r1 = pts[i].second;
            const double k = (t1 > t0) ? (temp - t0) / (t1 - t0) : 0.0;
            return r0 + k * (r1 - r0);
        }
    }
    return pts.back().second;
}

const char *modeName(Mode m) {
    switch (m) {
    case Mode::Manual: return "manual";
    case Mode::Curve:  return "curve";
    default:           return "auto";
    }
}

struct State {
    Mode mode = Mode::Auto;
    std::vector<double> targets;
    std::string curveSensor = "Tp01";
    std::vector<std::pair<double, double>> curve = {
        {40, 1200}, {55, 2000}, {70, 3200}, {85, 4500}};
};

class Daemon {
public:
    Daemon(std::string socketPath, bool simulate, std::string statePath)
        : socketPath_(std::move(socketPath)), statePath_(std::move(statePath)),
          smc_(simulate) {
        loadState();
    }

    ~Daemon() {
        if (listenFd_ >= 0) ::close(listenFd_);
        if (!socketPath_.empty()) ::unlink(socketPath_.c_str());
    }

    bool init() {
        if (!smc_.isOpen()) {
            std::fprintf(stderr, "fanctld: cannot open SMC: %s\n", smc_.lastError().c_str());
            return false;
        }
        listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            std::perror("socket");
            return false;
        }
        ::unlink(socketPath_.c_str());
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", socketPath_.c_str());
        if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
            std::perror("bind");
            return false;
        }
        ::chmod(socketPath_.c_str(), 0660);   // root:admin only
        gid_t adminGid = 80;
        if (struct group *g = ::getgrnam("admin")) adminGid = g->gr_gid;
        ::chown(socketPath_.c_str(), 0, adminGid);
        if (::listen(listenFd_, 8) != 0) {
            std::perror("listen");
            return false;
        }
        return true;
    }

    void run() {
        std::thread ticker([this] {
            while (g_running) {
                apply();
                refreshSensors();
                for (int i = 0; i < 20 && g_running; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        while (g_running) {
            // accept() with a timeout: a blocking accept() would swallow SIGTERM
            // and the daemon would never hand the fans back to macOS.
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(listenFd_, &fds);
            timeval tv{0, 300000};   // 300 ms
            int ready = ::select(listenFd_ + 1, &fds, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ready == 0) continue;
            int client = ::accept(listenFd_, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                break;
            }
            handleClient(client);
            ::close(client);
        }
        g_running = false;
        ticker.join();
        restoreAuto();
    }

private:
    // ------------------------------------------------------------- control
    void apply() {
        std::lock_guard<std::mutex> smcLock(smcMu_);
        if (!smc_.isOpen()) return;
        std::vector<FanInfo> fans = smc_.fans();
        if (fans.empty()) return;

        State st;
        {
            std::lock_guard<std::mutex> lk(mu_);
            st = state_;
        }

        if (st.mode == Mode::Auto) {
            for (const auto &f : fans) {
                if (f.has_mode && f.mode != 0) smc_.setFanForced(f.index, false);
            }
            return;
        }

        std::vector<double> want(fans.size(), 0);
        if (st.mode == Mode::Manual) {
            for (size_t i = 0; i < fans.size(); ++i) {
                if (st.targets.empty()) want[i] = fans[i].actual;
                else if (st.targets.size() == 1) want[i] = st.targets[0];
                else want[i] = i < st.targets.size() ? st.targets[i] : fans[i].actual;
            }
        } else {
            double temp = 0;
            if (auto v = smc_.readValue(st.curveSensor)) temp = *v;
            const double rpm = evalCurve(st.curve, temp);
            for (size_t i = 0; i < fans.size(); ++i) want[i] = rpm;
        }

        for (size_t i = 0; i < fans.size(); ++i) {
            const FanInfo &f = fans[i];
            if (f.has_min && want[i] < f.min) want[i] = f.min;
            if (f.has_max && want[i] > f.max) want[i] = f.max;
            if (!(f.has_mode && f.mode == 1)) smc_.setFanForced(f.index, true);
            bool changed = !f.has_target || std::fabs(want[i] - f.target) > 0.5;
            if (changed || ++tickCount_ % 5 == 0) smc_.setFanRpm(f.index, want[i]);
        }
    }

    void restoreAuto() {
        std::lock_guard<std::mutex> smcLock(smcMu_);
        if (!smc_.isOpen()) return;
        for (const auto &f : smc_.fans()) smc_.setFanForced(f.index, false);
        std::fprintf(stderr, "fanctld: restored automatic fan control\n");
    }

    // 传感器只在后台线程读一次，STATUS 直接返回快照，避免拖窗口时卡顿
    void refreshSensors() {
        std::lock_guard<std::mutex> smcLock(smcMu_);
        if (!smc_.isOpen()) return;
        auto list = smc_.sensors();
        std::lock_guard<std::mutex> lk(sensorsMu_);
        sensorsCache_.clear();
        sensorsCache_.reserve(list.size());
        for (const auto &s : list) sensorsCache_.emplace_back(s.key, s.value);
    }

    // ---------------------------------------------------------------- ipc
    void handleClient(int fd) {
        std::string req;
        char buf[1024];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0) {
            req.append(buf, size_t(n));
            if (req.find('\n') != std::string::npos) break;
        }
        while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) req.pop_back();

        std::istringstream in(req);
        std::string cmd;
        in >> cmd;

        std::string reply;
        bool shutdown = false;

        if (cmd == "AUTO") {
            {
                std::lock_guard<std::mutex> lk(mu_);
                state_.mode = Mode::Auto;
            }
            saveState();
            apply();
        } else if (cmd == "MANUAL") {
            std::vector<double> values;
            double v;
            while (in >> v) values.push_back(v);
            if (!values.empty()) {
                std::vector<FanInfo> fans;
                {
                    std::lock_guard<std::mutex> smcLock(smcMu_);
                    fans = smc_.fans();
                }
                std::lock_guard<std::mutex> lk(mu_);
                state_.mode = Mode::Manual;
                if (values.size() == 1) {
                    state_.targets.assign(fans.size(), values[0]);   // 广播
                } else {
                    state_.targets = values;
                    if (state_.targets.size() < fans.size()) {
                        const size_t old = state_.targets.size();
                        state_.targets.resize(fans.size(), 0);
                        for (size_t k = old; k < fans.size(); ++k)
                            state_.targets[k] = (fans[k].has_target && fans[k].target > 0)
                                                    ? fans[k].target
                                                    : fans[k].actual;
                    }
                }
            }
            saveState();
            apply();
        } else if (cmd == "MANUALONE") {
            int i = 0;
            double v = 0;
            in >> i >> v;
            std::vector<FanInfo> fans;
            {
                std::lock_guard<std::mutex> smcLock(smcMu_);
                fans = smc_.fans();
            }
            {
                std::lock_guard<std::mutex> lk(mu_);
                state_.mode = Mode::Manual;
                // 补齐到风扇数量：其它风扇保持它们当前的目标，而不是掉到 0
                if (state_.targets.size() < fans.size()) {
                    const size_t old = state_.targets.size();
                    state_.targets.resize(fans.size(), 0);
                    for (size_t k = old; k < fans.size(); ++k)
                        state_.targets[k] = (fans[k].has_target && fans[k].target > 0)
                                                ? fans[k].target
                                                : fans[k].actual;
                }
                if (i >= 0 && i < int(state_.targets.size())) state_.targets[i] = v;
            }
            saveState();
            apply();
        } else if (cmd == "CURVE") {
            // CURVE <sensor> <count> <t0> <r0> <t1> <r1> ...
            std::string sensor;
            int count = 0;
            if (in >> sensor >> count && count >= 2 && count <= 10) {
                std::vector<std::pair<double, double>> pts;
                bool ok = true;
                for (int i = 0; i < count; ++i) {
                    double t = 0, r = 0;
                    if (!(in >> t >> r)) { ok = false; break; }
                    pts.emplace_back(t, r);
                }
                if (ok) {
                    std::sort(pts.begin(), pts.end());
                    {
                        std::lock_guard<std::mutex> lk(mu_);
                        state_.mode = Mode::Curve;
                        state_.curveSensor = sensor;
                        state_.curve = pts;
                    }
                    saveState();
                }
            }
            apply();
        } else if (cmd == "SHUTDOWN") {
            shutdown = true;
        } else if (cmd != "STATUS" && cmd != "SENSORS" && cmd != "PING" && !cmd.empty()) {
            reply = "ERR unknown command " + cmd + "\nEND\n";
            (void)::write(fd, reply.data(), reply.size());
            return;
        }

        if (reply.empty()) {
            if (cmd == "PING") {
                reply = "OK pong\nEND\n";
            } else {
                reply = buildStatus(cmd == "SENSORS");
            }
        }
        (void)::write(fd, reply.data(), reply.size());
        if (shutdown) g_running = false;
    }

    std::string buildStatus(bool sensorsOnly) {
        std::ostringstream os;
        State st;
        {
            std::lock_guard<std::mutex> lk(mu_);
            st = state_;
        }
        os << (sensorsOnly ? "OK sensors\n" : "OK " + std::string(modeName(st.mode)) + "\n");
        os << "PROTO 2\n";
        os << "SIM " << (smc_.isSimulated() ? 1 : 0) << "\n";
        if (!sensorsOnly) {
            std::vector<FanInfo> fans;
            {
                std::lock_guard<std::mutex> smcLock(smcMu_);
                fans = smc_.fans();
            }
            for (const auto &f : fans) {
                unsigned flags = 0;
                if (f.has_actual) flags |= 1;
                if (f.has_min) flags |= 2;
                if (f.has_max) flags |= 4;
                if (f.has_target) flags |= 8;
                if (f.has_mode) flags |= 16;
                os << "FAN " << f.index << ' ' << f.actual << ' ' << f.min << ' '
                   << f.max << ' ' << f.target << ' ' << f.mode << ' ' << flags << "\n";
            }
        }
        os << "CURVE " << st.curveSensor << ' ' << st.curve.size();
        for (const auto &p : st.curve) os << ' ' << p.first << ' ' << p.second;
        os << "\n";
        std::vector<std::pair<std::string, double>> sensors;
        {
            std::lock_guard<std::mutex> lk(sensorsMu_);
            sensors = sensorsCache_;
        }
        for (const auto &s : sensors)
            os << "SENSOR " << s.first << ' ' << s.second << "\n";
        os << "END\n";
        return os.str();
    }

    // ------------------------------------------------------------- state
    void saveState() {
        State st;
        {
            std::lock_guard<std::mutex> lk(mu_);
            st = state_;
        }
        std::ofstream f(statePath_, std::ios::trunc);
        if (!f) return;
        f << "mode=" << modeName(st.mode) << "\n";
        f << "sensor=" << st.curveSensor << "\n";
        f << "curve=";
        for (size_t i = 0; i < st.curve.size(); ++i)
            f << (i ? "," : "") << st.curve[i].first << ':' << st.curve[i].second;
        f << "\n";
        f << "targets=";
        for (size_t i = 0; i < st.targets.size(); ++i)
            f << (i ? "," : "") << st.targets[i];
        f << "\n";
    }

    void loadState() {
        std::ifstream f(statePath_);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "mode") {
                if (v == "manual") state_.mode = Mode::Manual;
                else if (v == "curve") state_.mode = Mode::Curve;
                else state_.mode = Mode::Auto;
            } else if (k == "sensor") {
                state_.curveSensor = v;
            } else if (k == "curve") {
                state_.curve.clear();
                std::istringstream cs(v);
                std::string item;
                while (std::getline(cs, item, ',')) {
                    auto colon = item.find(':');
                    if (colon == std::string::npos) continue;
                    state_.curve.emplace_back(std::atof(item.substr(0, colon).c_str()),
                                              std::atof(item.substr(colon + 1).c_str()));
                }
                if (state_.curve.size() < 2)
                    state_.curve = {{40, 1200}, {55, 2000}, {70, 3200}, {85, 4500}};
            } else if (k == "targets") {
                state_.targets.clear();
                std::istringstream vs(v);
                std::string item;
                while (std::getline(vs, item, ','))
                    if (!item.empty()) state_.targets.push_back(std::atof(item.c_str()));
            }
        }
    }

    std::string socketPath_;
    std::string statePath_;
    int listenFd_ = -1;
    SMC smc_;
    std::mutex smcMu_;        // SMC 连接不是线程安全的
    std::mutex sensorsMu_;
    std::vector<std::pair<std::string, double>> sensorsCache_;
    std::mutex mu_;
    State state_;
    unsigned tickCount_ = 0;
};

}  // namespace

int main(int argc, char **argv) {
    std::string socketPath = defaultSocketPath();
    std::string statePath = "/Library/Application Support/FanCtl/state.conf";
    bool simulate = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--simulate") simulate = true;
        else if (a == "--socket" && i + 1 < argc) socketPath = argv[++i];
        else if (a == "--state" && i + 1 < argc) statePath = argv[++i];
        else if (a == "--help") {
            std::printf("usage: fanctld [--simulate] [--socket PATH] [--state PATH]\n");
            return 0;
        }
    }

    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);

    Daemon daemon(socketPath, simulate, statePath);
    if (!daemon.init()) return 1;
    std::fprintf(stderr, "fanctld: listening on %s%s\n", socketPath.c_str(),
                 simulate ? " (simulated)" : "");
    daemon.run();
    return 0;
}
