// ipc.h - line protocol between the GUI/CLI (unprivileged) and fanctld (root).
//
// Request  : one line, e.g. "STATUS", "MANUAL 2000 2000", "AUTO",
//            "CURVE Tp01 4 40 1200 55 2000 70 3200 85 4500", "SHUTDOWN"
// Response : "OK <mode>" or "ERR <message>", then body lines, then "END"
//
//   FAN <index> <actual> <min> <max> <target> <mode> <flags>
//   SENSOR <key> <value>
//   SIM <0|1>
#pragma once

#include <string>
#include <vector>

struct FanStatus {
    int index = 0;
    double actual = 0, min = 0, max = 0, target = 0;
    int mode = 0;
    unsigned flags = 0;   // 1 actual, 2 min, 4 max, 8 target, 16 mode
};

struct Status {
    bool ok = false;
    std::string error;
    std::string mode = "auto";   // auto | manual | curve
    bool simulated = false;
    std::vector<FanStatus> fans;
    std::vector<std::pair<std::string, double>> sensors;
    bool hasProto = false;
    bool hasCurve = false;
    std::string curveSensor;
    std::vector<std::pair<double, double>> curve;
};

// Sends one request and waits for "END". Returns the raw reply.
bool ipcRequest(const std::string &socketPath, const std::string &request,
                std::string &reply, std::string &error);

// Parses a raw reply into a Status.
Status parseStatus(const std::string &reply);

std::string defaultSocketPath();
