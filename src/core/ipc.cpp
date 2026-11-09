#include "ipc.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <sys/time.h>

#include <cerrno>
#include <cstring>
#include <sstream>

std::string defaultSocketPath() { return "/var/run/fanctl.sock"; }

bool ipcRequest(const std::string &socketPath, const std::string &request,
                std::string &reply, std::string &error) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::strerror(errno);
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof addr.sun_path, "%s", socketPath.c_str());

    timeval tv{1, 0};   // never let a wedged daemon freeze the caller
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        error = "cannot connect to fanctld: " + std::string(std::strerror(errno));
        ::close(fd);
        return false;
    }

    std::string line = request + "\n";
    if (::write(fd, line.data(), line.size()) < 0) {
        error = std::strerror(errno);
        ::close(fd);
        return false;
    }

    reply.clear();
    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            error = std::strerror(errno);
            ::close(fd);
            return false;
        }
        if (n == 0) break;
        reply.append(buf, size_t(n));
        if (reply.find("\nEND\n") != std::string::npos) break;
    }
    ::close(fd);
    return true;
}

Status parseStatus(const std::string &reply) {
    Status st;
    std::istringstream in(reply);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "ERR") {
            st.ok = false;
            std::getline(ls, st.error);
            if (!st.error.empty() && st.error[0] == ' ') st.error.erase(0, 1);
            return st;
        }
        if (tag == "OK") {
            st.ok = true;
            ls >> st.mode;
        } else if (tag == "PROTO") {
            int v = 0;
            ls >> v;
            st.hasProto = (v >= 2);
        } else if (tag == "SIM") {
            int v = 0;
            ls >> v;
            st.simulated = v != 0;
        } else if (tag == "FAN") {
            FanStatus f;
            ls >> f.index >> f.actual >> f.min >> f.max >> f.target >> f.mode >> f.flags;
            st.fans.push_back(f);
        } else if (tag == "CURVE") {
            st.hasCurve = true;
            int count = 0;
            ls >> st.curveSensor >> count;
            st.curve.clear();
            for (int i = 0; i < count; ++i) {
                double t = 0, r = 0;
                if (!(ls >> t >> r)) break;
                st.curve.emplace_back(t, r);
            }
        } else if (tag == "SENSOR") {
            std::string key;
            double value = 0;
            ls >> key >> value;
            st.sensors.emplace_back(key, value);
        } else if (tag == "END") {
            break;
        }
    }
    return st;
}
