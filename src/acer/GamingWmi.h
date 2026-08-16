#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace predator {

enum class FanMode : uint8_t { Auto = 0x01, Max = 0x02, Custom = 0x03 };

enum class ThermalMode : uint8_t {
    Silent = 0x00,
    Balanced = 0x01,
    Performance = 0x04,
    Turbo = 0x05,
    Eco = 0x06
};

inline const char* ThermalModeName(ThermalMode m) {
    switch (m) {
        case ThermalMode::Silent:
            return "Silent";
        case ThermalMode::Balanced:
            return "Balanced";
        case ThermalMode::Performance:
            return "Performance";
        case ThermalMode::Turbo:
            return "Turbo";
        case ThermalMode::Eco:
            return "Eco";
        default:
            return "Unknown";
    }
}

class GamingWmi {
public:
    static constexpr uint8_t kCpuFanId = 0x01;
    static constexpr uint8_t kGpuFanId = 0x04;
    static constexpr uint16_t kBothFans = 0x0009;  // BIT(0)|BIT(3)

    bool Connect();
    bool Ok() const;
    std::string LastError() const;

    std::optional<uint64_t> GetSysInfo(uint32_t command);
    std::optional<int> GetSensorValue(uint32_t sensor_id);  // temp C or RPM
    std::optional<int> GetFanPercent(uint8_t fan_id);
    std::optional<FanMode> GetFanMode();
    bool SetFanMode(FanMode mode);
    bool SetFanPercent(uint8_t fan_id, int percent);
    bool RestoreAutoFans();

    std::optional<ThermalMode> GetThermalMode();
    bool SetThermalMode(ThermalMode mode);
    bool SetMiscSetting(uint8_t index, uint8_t value);
    bool SetGamingLed(uint64_t packed);
    bool SetGamingProfile(uint64_t packed);
    bool SetLcdOverdrive(bool enable);
    bool ApplyTurboHardware(bool on);

    bool SetKeyboardBacklight(const std::vector<uint8_t>& payload16);
    bool SetKeyboardStaticZone(uint8_t zone_mask, uint8_t r, uint8_t g, uint8_t b);
    std::vector<std::wstring> Methods() const;

    static int DecodeSysInfo(uint64_t raw);

private:
    // Pimpl-ish: session lives in cpp via static/shared? Better hold unique ptr.
};

}  // namespace predator
