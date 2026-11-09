// fanctl - command line client.
#include "ipc.h"
#include "smc.h"

#include <chrono>
#include <thread>
#include <vector>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage() {
    std::printf(
        "usage: fanctl <command>\n\n"
        "  status                       show fans, mode and temperatures\n"
        "  sensors                      show temperature sensors\n"
        "  auto                         hand fans back to the system\n"
        "  set <fan|all> <rpm>          fix a fan at a constant speed\n"
        "  curve <sensor> <t1> <r1> <t2> <r2> ...\n"
        "                               speed follows a temperature sensor\n"
        "  ping                         check whether fanctld is alive\n"
        "  doctor                       read-only SMC health check (no root)\n"
        "  selftest                     prove fan control works, then restore auto\n");
}

std::string socketPath() {
    if (const char *env = std::getenv("FANCTL_SOCKET")) return env;
    return defaultSocketPath();
}

// ---------------------------------------------------------------- doctor
int cmdDoctor() {
    SMC smc(false);   // read-only, no root needed
    std::printf("SMC device       : %s\n",
                smc.isOpen() ? "open (AppleSMC)" : smc.lastError().c_str());
    if (!smc.isOpen()) return 1;

    auto keys = smc.allKeys();
    std::printf("keys enumerated  : %zu\n", keys.size());
    if (keys.empty()) {
        std::printf("\nresult: the SMC user client answered but exposed no keys.\n");
        return 1;
    }

    auto fanCount = smc.readValue("FNum");
    if (fanCount)
        std::printf("FNum (fans)      : %.0f\n", *fanCount);
    else
        std::printf("FNum (fans)      : MISSING (key not found)\n");

    static const char *kFanKeys[] = {"FNum",  "F0Ac", "F0Mn", "F0Mx",
                                     "F0Tg",  "F0Md", "F0Sf", "F1Ac",
                                     "F1Mn",  "F1Mx", "F1Tg", "F1Md", "F1Sf"};
    std::printf("fan keys         : ");
    int found = 0;
    for (const char *k : kFanKeys) {
        auto in = smc.info(k);
        if (!in) continue;
        std::printf("%s%s[%s]", found ? ", " : "", k, in->type.c_str());
        ++found;
    }
    if (!found) std::printf("(none)");
    std::printf("\n");

    if (fanCount && int(*fanCount) > 0) {
        for (const auto &f : smc.fans()) {
            std::printf("fan %-12d: %.0f rpm (min %.0f, max %.0f, %s)\n", f.index,
                        f.actual, f.min, f.max, f.mode ? "forced" : "auto");
        }
        std::printf("value byte order : %s\n",
                    smc.littleEndian() ? "host (little-endian)" : "network (big-endian)");
    }

    auto sensors = smc.sensors();
    int live = 0;
    for (const auto &t : sensors)
        if (std::abs(t.value) > 0.01) ++live;
    std::printf("temperature keys : %zu (%d reporting a non-zero value)\n", sensors.size(),
                live);
    for (size_t i = 0; i < sensors.size() && i < 5; ++i)
        std::printf("  %s  %.1f C  [%s]\n", sensors[i].key.c_str(),
                    sensors[i].value, sensors[i].type.c_str());

    std::printf("\nresult           : ");
    if (!fanCount || int(*fanCount) == 0) {
        std::printf("INFEASIBLE on this machine\n"
                    "  The SMC does not expose fan keys (FNum/F0Ac/F0Tg), so no\n"
                    "  software can control the fans here by writing SMC.\n");
        if (live == 0)
            std::printf("  All temperature sensors also read 0.0 - this is a stubbed or\n"
                        "  virtualised SMC, not real hardware.\n");
        return 2;
    }
    if (!smc.info("F0Tg")) {
        std::printf("PARTIAL - fans exist but F0Tg (target) is missing\n");
        return 1;
    }
    std::printf("FEASIBLE - run `fanctl selftest` to prove it end to end\n");
    return 0;
}

// -------------------------------------------------------------- selftest
int sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 0;
}

int cmdSelfTest() {
    std::string reply, error;
    if (!ipcRequest(socketPath(), "STATUS", reply, error)) {
        std::fprintf(stderr, "fanctl: %s\n", error.c_str());
        return 1;
    }
    Status st = parseStatus(reply);
    if (!st.ok) {
        std::fprintf(stderr, "fanctl: %s\n", st.error.c_str());
        return 1;
    }
    if (st.fans.empty()) {
        std::printf("selftest: no fans reported by the daemon%s.\n",
                    st.simulated ? " (simulated)" : "");
        std::printf("Run `fanctl doctor` to see what this SMC actually exposes.\n");
        return 2;
    }

    std::printf("selftest: %zu fan(s), mode=%s%s\n", st.fans.size(), st.mode.c_str(),
                st.simulated ? " [simulated]" : "");
    for (const auto &f : st.fans)
        std::printf("  fan %d: %.0f rpm (min %.0f max %.0f)\n", f.index, f.actual, f.min,
                    f.max);

    struct Reading { int index; double before; double target; };
    std::vector<Reading> tests;
    std::string cmd = "MANUAL";
    for (const auto &f : st.fans) {
        double target = f.actual + 300;
        if (target > f.max - 50) target = f.actual - 300;
        if (target < f.min + 50) target = (f.min + f.max) / 2;
        tests.push_back({f.index, f.actual, target});
        cmd += " " + std::to_string(int(target));
    }

    std::printf("selftest: writing %s\n", cmd.c_str());
    if (!ipcRequest(socketPath(), cmd, reply, error)) {
        std::fprintf(stderr, "fanctl: %s\n", error.c_str());
        return 1;
    }
    Status after = parseStatus(reply);
    if (!after.ok) {
        std::fprintf(stderr, "fanctl: write rejected: %s\n", after.error.c_str());
        ipcRequest(socketPath(), "AUTO", reply, error);
        return 1;
    }

    double bestDelta = 0;
    for (int i = 0; i < 20; ++i) {          // up to 10 s
        sleepMs(500);
        if (!ipcRequest(socketPath(), "STATUS", reply, error)) break;
        Status now = parseStatus(reply);
        bestDelta = 0;
        for (size_t k = 0; k < now.fans.size() && k < tests.size(); ++k) {
            double d = now.fans[k].actual - tests[k].before;
            if (std::abs(d) > std::abs(bestDelta)) bestDelta = d;
        }
        if (std::abs(bestDelta) >= 60) break;
    }

    std::printf("selftest: restoring automatic control\n");
    ipcRequest(socketPath(), "AUTO", reply, error);

    bool moved = std::abs(bestDelta) >= 60;
    std::printf("\nresult: %s (fan moved %.0f rpm after the write)\n",
                moved ? "PASS - fan control works" : "FAIL - fans did not respond", bestDelta);
    if (!moved)
        std::printf("  Possible causes: fans already at min/max, the SMC ignores F*Tg on\n"
                    "  this model, or macOS took the mode back within the test window.\n"
                    "  Check: log show --last 2m --predicate 'process == fanctld'\n");
    return moved ? 0 : 3;
}

int request(const std::string &req) {
    std::string reply, error;
    if (!ipcRequest(socketPath(), req, reply, error)) {
        std::fprintf(stderr, "fanctl: %s\n", error.c_str());
        std::fprintf(stderr, "fanctl: is the daemon installed? try install.command\n");
        return 1;
    }
    Status st = parseStatus(reply);
    if (!st.ok) {
        std::fprintf(stderr, "fanctl: %s\n", st.error.c_str());
        return 1;
    }

    if (req == "STATUS") {
        std::printf("mode: %s%s\n", st.mode.c_str(), st.simulated ? "  [simulated]" : "");
        for (const auto &f : st.fans) {
            std::printf("fan %d: %.0f rpm  (min %.0f, max %.0f, target %.0f, %s)\n",
                        f.index, f.actual, f.min, f.max, f.target,
                        f.mode ? "forced" : "automatic");
        }
        std::printf("temperatures:\n");
        for (const auto &s : st.sensors)
            std::printf("  %s  %.1f°C\n", s.first.c_str(), s.second);
    } else if (req == "SENSORS") {
        for (const auto &s : st.sensors) std::printf("%s %.1f\n", s.first.c_str(), s.second);
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "status") return request("STATUS");
    if (cmd == "sensors") return request("SENSORS");
    if (cmd == "ping") return request("PING");
    if (cmd == "doctor") return cmdDoctor();
    if (cmd == "selftest") return cmdSelfTest();
    if (cmd == "auto") return request("AUTO");
    if (cmd == "set") {
        if (argc != 4) {
            usage();
            return 2;
        }
        std::string which = argv[2];
        int rpm = std::atoi(argv[3]);
        if (which == "all") return request("MANUAL " + std::to_string(rpm));
        return request("MANUALONE " + which + " " + std::to_string(rpm));
    }
    if (cmd == "curve") {
        // fanctl curve <sensor> <t1> <r1> <t2> <r2> ...
        if (argc < 7 || (argc - 3) % 2 != 0) {
            usage();
            return 2;
        }
        const int count = (argc - 3) / 2;
        std::string req = std::string("CURVE ") + argv[2] + " " + std::to_string(count);
        for (int i = 3; i + 1 < argc; i += 2)
            req += std::string(" ") + argv[i] + " " + argv[i + 1];
        return request(req);
    }
    usage();
    return 2;
}
