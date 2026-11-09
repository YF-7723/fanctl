// smc.h - AppleSMC protocol client.
//
// The SMC is an IOKit service ("AppleSMC"). Requests are a fixed 80-byte
// struct sent via IOConnectCallStructMethod with selector 2:
//
//   +0   u32  key           4-char code, big-endian ("F0Tg" -> 0x46305467)
//   +4   ..   vers(6) + 2 bytes padding
//   +12  ..   pLimitData(16)
//   +28  u32  keyInfo.dataSize
//   +32  u32  keyInfo.dataType   ('flt ', 'fpe2', 'sp78', 'ui8 ', ...)
//   +36  u8   keyInfo.dataAttributes
//   +40  u8   result
//   +41  u8   status
//   +42  u8   data8          5=read bytes, 6=write bytes, 8=key by index,
//                            9=read key info
//   +44  u32  data32         (index, for command 8)
//   +48  u8[32] bytes        payload
//
// Reads work as a normal user. Writes require root.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct KeyInfo {
    uint32_t size = 0;
    std::string type;
    uint8_t attributes = 0;
};

struct FanInfo {
    int index = 0;
    double actual = 0;
    double min = 0;
    double max = 0;
    double target = 0;
    int mode = 0;          // 0 = system automatic, 1 = forced
    bool has_actual = false;
    bool has_min = false;
    bool has_max = false;
    bool has_target = false;
    bool has_mode = false;
};

struct SensorInfo {
    std::string key;
    double value = 0;
    std::string type;
};

class SMC {
public:
    explicit SMC(bool simulate = false);
    ~SMC();

    SMC(const SMC &) = delete;
    SMC &operator=(const SMC &) = delete;

    bool isOpen() const { return open_; }
    bool littleEndian() const { return little_; }
    bool isSimulated() const { return simulate_; }
    const std::string &lastError() const { return error_; }

    int fanCount();
    std::vector<FanInfo> fans();
    std::vector<SensorInfo> sensors();
    std::vector<std::string> allKeys();

    std::optional<double> readValue(const std::string &key);
    std::optional<KeyInfo> info(const std::string &key) { return keyInfo(key); }
    bool setFanForced(int index, bool forced);
    bool setFanRpm(int index, double rpm);

    static std::optional<double> decodeBE(const std::vector<uint8_t> &data,
                                          const std::string &type);
    std::optional<double> decode(const std::vector<uint8_t> &data,
                                 const std::string &type);
    std::vector<uint8_t> encodeRpm(double rpm, const std::string &type,
                                   size_t size);

private:
    bool openDevice();
    bool call(uint8_t command, const std::string *key, uint32_t data32,
              size_t size, const std::vector<uint8_t> &payload,
              std::vector<uint8_t> &out);
    std::optional<KeyInfo> keyInfo(const std::string &key);
    std::optional<std::vector<uint8_t>> readRaw(const std::string &key);
    bool writeRaw(const std::string &key, const std::vector<uint8_t> &data);

    struct Sim;
    Sim *sim_ = nullptr;

    int fd_ = -1;   // actually an io_connect_t
    bool open_ = false;
    bool simulate_ = false;
    std::string error_;
    std::string resolve(const std::string &key) const;
    void buildAliases();
    void detectEndianness();

    std::map<std::string, KeyInfo> cache_;
    std::map<std::string, std::string> alias_;  // "FNum" -> "muNF" on some SMCs
    bool little_ = false;                       // values stored host-order?
    std::vector<std::string> keys_;      // enumerated once
    std::vector<std::string> tempKeys_;  // temperature keys, resolved once
};
