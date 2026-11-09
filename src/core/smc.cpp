// smc.cpp - see smc.h for the wire format.
#include "smc.h"

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <unistd.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

constexpr uint32_t kSelector = 2;
constexpr uint8_t kCmdReadBytes = 5;
constexpr uint8_t kCmdWriteBytes = 6;
constexpr uint8_t kCmdReadIndex = 8;
constexpr uint8_t kCmdReadKeyInfo = 9;

constexpr size_t kStructSize = 80;
constexpr size_t kOffKey = 0;
constexpr size_t kOffDataSize = 28;
constexpr size_t kOffDataType = 32;
constexpr size_t kOffAttr = 36;
constexpr size_t kOffResult = 40;
constexpr size_t kOffData8 = 42;
constexpr size_t kOffData32 = 44;
constexpr size_t kOffBytes = 48;

uint32_t fourcc(const std::string &s) {
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
           (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
}

std::string unfourcc(uint32_t v) {
    std::string s(4, '\0');
    s[0] = char((v >> 24) & 0xff);
    s[1] = char((v >> 16) & 0xff);
    s[2] = char((v >> 8) & 0xff);
    s[3] = char(v & 0xff);
    return s;
}

uint32_t readLE32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

void writeLE32(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

std::string reversed4(const std::string &k) {
    if (k.size() != 4) return k;
    return std::string{k[3], k[2], k[1], k[0]};
}

bool looksLikeType(const std::string &t) {
    static const char *known[] = {"flt ", "fpe2", "sp78", "sp87", "sp5a", "ui8 ",
                                  "ui16", "ui32", "si8 ", "si16", "fp88", "hex_",
                                  "ch8*", "flag", "ioft", "{fds"};
    for (const char *k : known)
        if (t == k) return true;
    return false;
}

std::string normaliseType(const std::string &raw) {
    if (looksLikeType(raw)) return raw;
    std::string rev = reversed4(raw);
    if (looksLikeType(rev)) return rev;
    return raw;
}

const char *resultName(uint8_t r) {
    switch (r) {
    case 0x00: return "ok";
    case 0x81: return "bad argument";
    case 0x82: return "bad message";
    case 0x83: return "not privileged";
    case 0x84: return "key not found";
    case 0x85: return "key not readable";
    case 0x86: return "key not writable";
    case 0x87: return "key size mismatch";
    case 0x88: return "key type mismatch";
    default:   return "unknown SMC error";
    }
}

}  // namespace

// ---------------------------------------------------------------- simulation

struct SMC::Sim {
    std::vector<double> rpm{1800, 1750};
    std::vector<double> target{1800, 1750};
    std::vector<int> mode{0, 0};
    double t_cpu = 52.0;
    double t_gpu = 45.0;
    int tick = 0;
    std::mt19937 rng{1234};

    double jitter(double amp) {
        std::uniform_real_distribution<double> d(-amp, amp);
        return d(rng);
    }

    void advance() {
        ++tick;
        for (size_t i = 0; i < rpm.size(); ++i) {
            if (mode[i] == 1) {
                // spin towards the requested target
                rpm[i] += (target[i] - rpm[i]) * 0.35;
            } else {
                rpm[i] += ((1820 + 40 * i) - rpm[i]) * 0.2;
            }
        }
        // temperature reacts to fan speed a little, so curves are visible
        double cooling = (rpm.empty() ? 0 : (rpm[0] - 1800) / 4000.0);
        t_cpu += ((58.0 - 25.0 * cooling) - t_cpu) * 0.1 + jitter(0.3);
        t_gpu += ((50.0 - 20.0 * cooling) - t_gpu) * 0.1 + jitter(0.3);
    }
};

// ---------------------------------------------------------------------- SMC

SMC::SMC(bool simulate) : simulate_(simulate) {
    if (simulate_) {
        sim_ = new Sim();
        open_ = true;
    } else {
        open_ = openDevice();
        if (open_) {
            buildAliases();
            detectEndianness();
        }
    }
}

SMC::~SMC() {
    if (fd_ >= 0) {
        IOServiceClose(io_connect_t(fd_));
        fd_ = -1;
    }
    delete sim_;
}

bool SMC::openDevice() {
    CFMutableDictionaryRef match = IOServiceMatching("AppleSMC");
    if (!match) {
        error_ = "IOServiceMatching(AppleSMC) failed";
        return false;
    }
    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS) {
        error_ = "AppleSMC service not found";
        return false;
    }
    io_service_t service = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!service) {
        error_ = "AppleSMC service not found";
        return false;
    }
    io_connect_t conn = 0;
    kr = IOServiceOpen(service, mach_task_self(), 0, &conn);
    IOObjectRelease(service);
    if (kr != KERN_SUCCESS) {
        error_ = "IOServiceOpen failed";
        return false;
    }
    fd_ = int(conn);
    return true;
}

bool SMC::call(uint8_t command, const std::string *key, uint32_t data32,
               size_t size, const std::vector<uint8_t> &payload,
               std::vector<uint8_t> &out) {
    if (!open_) {
        error_ = "SMC connection is not open";
        return false;
    }
    uint8_t in[kStructSize] = {0};
    uint8_t resp[kStructSize] = {0};
    if (key) {
        if (key->size() != 4) {
            error_ = "invalid SMC key";
            return false;
        }
        uint32_t k = fourcc(resolve(*key));
        in[kOffKey + 0] = uint8_t(k >> 24);
        in[kOffKey + 1] = uint8_t(k >> 16);
        in[kOffKey + 2] = uint8_t(k >> 8);
        in[kOffKey + 3] = uint8_t(k);
    }
    in[kOffData8] = command;
    writeLE32(in + kOffData32, data32);
    if (size) writeLE32(in + kOffDataSize, uint32_t(size));
    if (!payload.empty()) {
        size_t n = payload.size() < 32 ? payload.size() : 32;
        std::memcpy(in + kOffBytes, payload.data(), n);
    }
    size_t outSize = kStructSize;
    kern_return_t kr = IOConnectCallStructMethod(io_connect_t(fd_), kSelector, in,
                                                 kStructSize, resp, &outSize);
    if (kr != KERN_SUCCESS) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "IOConnectCallStructMethod failed (%#x)", kr);
        error_ = buf;
        return false;
    }
    if (resp[kOffResult] != 0) {
        error_ = std::string("SMC: ") + resultName(resp[kOffResult]);
        return false;
    }
    out.assign(resp, resp + kStructSize);
    return true;
}

std::string SMC::resolve(const std::string &key) const {
    if (alias_.empty()) return key;
    auto it = alias_.find(key);
    return it == alias_.end() ? key : it->second;
}

// Some SMCs keep fan keys byte-reversed ("FNum" is stored as "muNF").  Enumerate
// once and register both spellings so callers can always use the readable name.
void SMC::buildAliases() {
    std::vector<std::string> raw;
    for (uint32_t i = 0; i < 20000; ++i) {
        std::vector<uint8_t> out;
        if (!call(kCmdReadIndex, nullptr, i, 0, {}, out)) break;
        std::string k(reinterpret_cast<const char *>(&out[kOffKey]), 4);
        if (k == std::string(4, '\0')) break;
        raw.push_back(k);
    }
    keys_ = raw;
    std::set<std::string> rawSet(raw.begin(), raw.end());
    for (const auto &k : raw) alias_[k] = k;
    for (const auto &k : raw) {
        std::string r = reversed4(k);
        if (!rawSet.count(r)) alias_[r] = k;
    }
}

// Values may be stored host-order (little-endian) instead of the classic
// network order.  Fan min/max are a good probe: pick whichever reading lands
// in a physically plausible rpm range.
void SMC::detectEndianness() {
    little_ = false;
    for (const char *name : {"F0Mn", "F0Mx", "F0Ac", "F1Mn", "F1Mx"}) {
        auto info = keyInfo(name);
        if (!info || info->size < 2) continue;
        auto raw = readRaw(name);
        if (!raw) continue;
        std::vector<uint8_t> le = *raw;
        std::reverse(le.begin(), le.end());
        auto beV = decodeBE(*raw, info->type);
        auto leV = decodeBE(le, info->type);
        if (!beV || !leV) continue;
        bool beSane = *beV > 100 && *beV < 20000;
        bool leSane = *leV > 100 && *leV < 20000;
        if (leSane && !beSane) { little_ = true; return; }
        if (beSane && !leSane) { little_ = false; return; }
    }
}

std::optional<KeyInfo> SMC::keyInfo(const std::string &key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    std::vector<uint8_t> out;
    if (!call(kCmdReadKeyInfo, &key, 0, 0, {}, out)) return std::nullopt;
    KeyInfo info;
    info.size = readLE32(&out[kOffDataSize]);
    {
        std::string raw(reinterpret_cast<const char *>(&out[kOffDataType]), 4);
        info.type = normaliseType(raw);
    }
    info.attributes = out[kOffAttr];
    cache_[key] = info;
    return info;
}

std::optional<std::vector<uint8_t>> SMC::readRaw(const std::string &key) {
    auto info = keyInfo(key);
    if (!info) return std::nullopt;
    std::vector<uint8_t> out;
    if (!call(kCmdReadBytes, &key, 0, info->size, {}, out)) return std::nullopt;
    size_t n = info->size;
    if (n > 32) n = 32;
    return std::vector<uint8_t>(out.begin() + kOffBytes, out.begin() + kOffBytes + n);
}

bool SMC::writeRaw(const std::string &key, const std::vector<uint8_t> &data) {
    if (geteuid() != 0) {
        error_ = "writing SMC keys requires root";
        return false;
    }
    auto info = keyInfo(key);
    if (!info) return false;
    if (data.size() != info->size) {
        error_ = key + ": size mismatch";
        return false;
    }
    std::vector<uint8_t> out;
    return call(kCmdWriteBytes, &key, 0, info->size, data, out);
}

std::optional<double> SMC::decode(const std::vector<uint8_t> &in,
                                 const std::string &type) {
    std::vector<uint8_t> d = in;
    if (little_) std::reverse(d.begin(), d.end());
    return decodeBE(d, type);
}

std::optional<double> SMC::decodeBE(const std::vector<uint8_t> &d,
                                   const std::string &type) {
    auto need = [&](size_t n) { return d.size() >= n; };
    if (type == "flt " && need(4)) {
        uint32_t bits = (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) |
                        (uint32_t(d[2]) << 8) | d[3];
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return double(f);
    }
    if (type == "fpe2" && need(2)) return double((d[0] << 8) | d[1]) / 4.0;
    if (type == "sp78" && need(2)) return double(int16_t((d[0] << 8) | d[1])) / 256.0;
    if (type == "sp87" && need(2)) return double(int16_t((d[0] << 8) | d[1])) / 128.0;
    if (type == "sp5a" && need(2)) return double(int16_t((d[0] << 8) | d[1])) / 1024.0;
    if (type == "ui8 " && need(1)) return double(d[0]);
    if (type == "ui16" && need(2)) return double((d[0] << 8) | d[1]);
    if (type == "ui32" && need(4)) return double((uint32_t(d[0]) << 24) |
                                                (uint32_t(d[1]) << 16) |
                                                (uint32_t(d[2]) << 8) | d[3]);
    if (type == "si8 " && need(1)) return double(int8_t(d[0]));
    if (type == "si16" && need(2)) return double(int16_t((d[0] << 8) | d[1]));
    return std::nullopt;
}

std::vector<uint8_t> SMC::encodeRpm(double rpm, const std::string &type,
                                   size_t size) {
    std::vector<uint8_t> out(size, 0);
    if (type == "fpe2" && size >= 2) {
        uint16_t v = uint16_t(std::lround(std::max(0.0, std::min(65535.0, rpm * 4))));
        out[0] = uint8_t(v >> 8);
        out[1] = uint8_t(v);
    } else if (type == "flt " && size >= 4) {
        float f = float(rpm);
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        out[0] = uint8_t(bits >> 24);
        out[1] = uint8_t(bits >> 16);
        out[2] = uint8_t(bits >> 8);
        out[3] = uint8_t(bits);
    } else if (type == "ui16" && size >= 2) {
        uint16_t v = uint16_t(std::lround(std::max(0.0, std::min(65535.0, rpm))));
        out[0] = uint8_t(v >> 8);
        out[1] = uint8_t(v);
    } else if (type == "ui8 " && size >= 1) {
        out[0] = uint8_t(std::lround(std::max(0.0, std::min(255.0, rpm))));
    }
    if (little_) std::reverse(out.begin(), out.end());
    return out;
}

std::vector<std::string> SMC::allKeys() {
    if (!keys_.empty()) return keys_;
    std::vector<std::string> keys;
    if (simulate_) {
        for (const char *k : {"FNum", "F0Ac", "F0Mn", "F0Mx", "F0Tg", "F0Md",
                              "F1Ac", "F1Mn", "F1Mx", "F1Tg", "F1Md",
                              "Tp01", "Tp05", "Tg0f", "TB0T"})
            keys.emplace_back(k);
        return keys;
    }
    for (uint32_t i = 0; i < 20000; ++i) {
        std::vector<uint8_t> out;
        if (!call(kCmdReadIndex, nullptr, i, 0, {}, out)) break;
        std::string k(reinterpret_cast<const char *>(&out[kOffKey]), 4);
        if (k == std::string(4, '\0')) break;
        keys.push_back(k);
    }
    keys_ = keys;
    return keys;
}

int SMC::fanCount() {
    if (simulate_) {
        sim_->advance();
        return int(sim_->rpm.size());
    }
    auto v = readValue("FNum");
    return v ? int(*v) : 0;
}

std::vector<FanInfo> SMC::fans() {
    std::vector<FanInfo> result;
    int n = fanCount();
    if (simulate_) {
        for (int i = 0; i < n; ++i) {
            FanInfo f;
            f.index = i;
            f.actual = sim_->rpm[i];
            f.min = 1200;
            f.max = 6200;
            f.target = sim_->target[i];
            f.mode = sim_->mode[i];
            f.has_actual = f.has_min = f.has_max = f.has_target = f.has_mode = true;
            result.push_back(f);
        }
        return result;
    }
    for (int i = 0; i < n; ++i) {
        FanInfo f;
        f.index = i;
        struct Item { const char *suffix; double *dst; bool *has; } items[] = {
            {"Ac", &f.actual, &f.has_actual}, {"Mn", &f.min, &f.has_min},
            {"Mx", &f.max, &f.has_max},       {"Tg", &f.target, &f.has_target}};
        for (auto &it : items) {
            std::string key = "F" + std::to_string(i) + it.suffix;
            auto info = keyInfo(key);
            if (!info) continue;
            auto raw = readRaw(key);
            if (!raw) continue;
            auto v = decode(*raw, info->type);
            if (v) {
                *it.dst = *v;
                *it.has = true;
            }
        }
        std::string mk = "F" + std::to_string(i) + "Md";
        if (auto info = keyInfo(mk)) {
            if (auto raw = readRaw(mk)) {
                if (auto v = decode(*raw, info->type)) {
                    f.mode = int(*v);
                    f.has_mode = true;
                }
            }
        }
        result.push_back(f);
    }
    return result;
}

std::optional<double> SMC::readValue(const std::string &key) {
    if (simulate_) {
        if (key == "FNum") return double(sim_->rpm.size());
        if (key == "Tp01") return sim_->t_cpu;
        if (key == "Tp05") return sim_->t_cpu - 3.0;
        if (key == "Tg0f") return sim_->t_gpu;
        if (key == "TB0T") return 31.0;
        return std::nullopt;
    }
    auto info = keyInfo(key);
    if (!info) return std::nullopt;
    auto raw = readRaw(key);
    if (!raw) return std::nullopt;
    return decode(*raw, info->type);
}

std::vector<SensorInfo> SMC::sensors() {
    if (simulate_) {
        sim_->advance();
        return {{"Tp01", sim_->t_cpu, "sp78"},
                {"Tp05", sim_->t_cpu - 3.0, "sp78"},
                {"Tg0f", sim_->t_gpu, "sp78"},
                {"TB0T", 31.0, "sp78"}};
    }
    if (tempKeys_.empty()) {
        for (const auto &rawKey : allKeys()) {
            if (rawKey.size() != 4) continue;
            std::string key = rawKey;
            if (key[0] != 'T') {
                std::string rev = reversed4(key);
                if (rev[0] != 'T') continue;
                key = rev;   // e.g. "T0pT" -> "Tp0T"
            }
            auto info = keyInfo(key);
            if (!info) continue;
            if (info->type == "sp78" || info->type == "flt " ||
                info->type == "sp87" || info->type == "sp5a")
                tempKeys_.push_back(key);
        }
    }
    std::vector<SensorInfo> out;
    for (const auto &key : tempKeys_) {
        auto info = keyInfo(key);
        if (!info) continue;
        auto v = readValue(key);
        if (!v || *v < -40 || *v > 150) continue;
        out.push_back({key, *v, info->type});
    }
    std::sort(out.begin(), out.end(),
              [](const SensorInfo &a, const SensorInfo &b) { return a.key < b.key; });
    return out;
}

bool SMC::setFanForced(int index, bool forced) {
    if (simulate_) {
        if (index < 0 || index >= int(sim_->mode.size())) return false;
        sim_->mode[index] = forced ? 1 : 0;
        return true;
    }
    std::string key = "F" + std::to_string(index) + "Md";
    if (!keyInfo(key)) return false;
    return writeRaw(key, {uint8_t(forced ? 1 : 0)});
}

bool SMC::setFanRpm(int index, double rpm) {
    if (simulate_) {
        if (index < 0 || index >= int(sim_->target.size())) return false;
        sim_->target[index] = rpm;
        return true;
    }
    std::string key = "F" + std::to_string(index) + "Tg";
    auto info = keyInfo(key);
    if (!info) {
        error_ = key + " not present";
        return false;
    }
    return writeRaw(key, encodeRpm(rpm, info->type, info->size));
}
