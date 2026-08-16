#include "acer/GamingWmi.h"

#include "acer/AcerService.h"
#include "acer/WmiHub.h"
#include "common/Log.h"

#include <algorithm>
#include <sstream>

namespace predator {
namespace {

constexpr wchar_t kClass[] = L"AcerGamingFunction";

uint64_t PackFanBehavior(uint16_t fan_bitmap, FanMode mode) {
    uint64_t input = fan_bitmap;
    const uint64_t m = static_cast<uint64_t>(mode);
    if (fan_bitmap & 0x0001) {
        input |= (m & 0x3) << 16;
    }
    if (fan_bitmap & 0x0008) {
        input |= (m & 0x3) << 22;
    }
    return input;
}

// Helios 300 / PT315-52 PredatorSense v3 packing (linux acer-wmi WMID_gaming_set_fan_mode).
uint64_t PackHeliosFanBehavior(uint8_t fan_mode, int cpu_fans = 1, int gpu_fans = 1) {
    uint64_t config2 = 0;
    uint64_t config1 = 0;
    if (cpu_fans > 0) {
        config2 |= 1;
    }
    for (int i = 0; i < (cpu_fans + gpu_fans); ++i) {
        config2 |= 1ull << (i + 1);
    }
    for (int i = 0; i < gpu_fans; ++i) {
        config2 |= 1ull << (i + 3);
    }
    if (cpu_fans > 0) {
        config1 |= fan_mode;
    }
    for (int i = 0; i < (cpu_fans + gpu_fans); ++i) {
        config1 |= static_cast<uint64_t>(fan_mode) << (2 * i + 2);
    }
    for (int i = 0; i < gpu_fans; ++i) {
        config1 |= static_cast<uint64_t>(fan_mode) << (2 * i + 6);
    }
    return config2 | (config1 << 16);
}

uint64_t PackFanSpeed(uint8_t fan_id, uint8_t percent) {
    return static_cast<uint64_t>(fan_id) | (static_cast<uint64_t>(percent) << 8);
}

uint64_t PackMisc(uint8_t index, uint8_t value) {
    return static_cast<uint64_t>(index) | (static_cast<uint64_t>(value) << 8);
}

}  // namespace

int GamingWmi::DecodeSysInfo(uint64_t raw) {
    return static_cast<int>((raw >> 8) & 0x1FFF);
}

bool GamingWmi::Connect() {
    return GetWmi().Ok();
}

bool GamingWmi::Ok() const {
    return GetWmi().Ok();
}

std::string GamingWmi::LastError() const {
    return GetWmi().LastError();
}

std::optional<uint64_t> GamingWmi::GetSysInfo(uint32_t command) {
    uint64_t out = 0;
    if (!GetWmi().ExecU32InU64Out(kClass, L"GetGamingSysInfo", command, out)) {
        return std::nullopt;
    }
    return out;
}

std::optional<int> GamingWmi::GetSensorValue(uint32_t sensor_id) {
    auto raw = GetSysInfo(sensor_id);
    if (!raw) {
        return std::nullopt;
    }
    return DecodeSysInfo(*raw);
}

std::optional<int> GamingWmi::GetFanPercent(uint8_t fan_id) {
    uint64_t out = 0;
    if (!GetWmi().ExecU32InU64Out(kClass, L"GetGamingFanSpeed", fan_id, out)) {
        return std::nullopt;
    }
    const int pct = static_cast<int>((out >> 8) & 0xFF);
    if (pct > 0 && pct <= 100) {
        return pct;
    }
    const int low = static_cast<int>(out & 0xFF);
    if (low > 0 && low <= 100) {
        return low;
    }
    return std::nullopt;
}

std::optional<FanMode> GamingWmi::GetFanMode() {
    uint64_t out = 0;
    if (!GetWmi().ExecU32InU64Out(kClass, L"GetGamingFanBehavior", kBothFans, out)) {
        return std::nullopt;
    }
    const uint8_t cpu_mode = static_cast<uint8_t>((out >> 8) & 0x3);
    if (cpu_mode == 0) {
        return std::nullopt;
    }
    return static_cast<FanMode>(cpu_mode);
}

bool GamingWmi::SetFanMode(FanMode mode) {
    uint32_t out = 0;
    const uint8_t helios_mode = static_cast<uint8_t>(mode);  // 1 auto, 2 max, 3 custom
    const uint64_t helios = PackHeliosFanBehavior(helios_mode);
    {
        std::ostringstream oss;
        oss << "SetGamingFanBehavior Helios payload=0x" << std::hex << helios;
        Log(oss.str());
    }
    if (GetWmi().ExecU64InU32Out(kClass, L"SetGamingFanBehavior", helios, out)) {
        return true;
    }
    const uint64_t v4 = PackFanBehavior(kBothFans, mode);
    std::ostringstream oss;
    oss << "Helios fan mode failed (" << GetWmi().LastError() << "), trying v4 payload=0x" << std::hex
        << v4;
    Log(oss.str());
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingFanBehavior", v4, out);
}

bool GamingWmi::SetFanPercent(uint8_t fan_id, int percent) {
    percent = std::clamp(percent, 0, 100);
    uint32_t out = 0;
    const uint64_t packed = PackFanSpeed(fan_id, static_cast<uint8_t>(percent));
    std::ostringstream oss;
    oss << "SetGamingFanSpeed fan=" << static_cast<int>(fan_id) << " pct=" << percent
        << " payload=0x" << std::hex << packed;
    Log(oss.str());
    if (GetWmi().ExecU64InU32Out(kClass, L"SetGamingFanSpeed", packed, out)) {
        return true;
    }
    // Some BIOS uses fan index 0/1 instead of 1/4.
    const uint8_t alt = (fan_id == kCpuFanId) ? 0 : 1;
    const uint64_t packed_alt = PackFanSpeed(alt, static_cast<uint8_t>(percent));
    Log("retry SetGamingFanSpeed with alt fan id");
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingFanSpeed", packed_alt, out);
}

bool GamingWmi::RestoreAutoFans() {
    Log("restoring Auto fan mode");
    return SetFanMode(FanMode::Auto);
}

std::optional<ThermalMode> GamingWmi::GetThermalMode() {
    uint64_t out = 0;
    if (!GetWmi().ExecU32InU64Out(kClass, L"GetGamingMiscSetting", 0x0B, out)) {
        return std::nullopt;
    }
    const uint8_t status = static_cast<uint8_t>(out & 0xFF);
    if (status != 0) {
        return std::nullopt;
    }
    return static_cast<ThermalMode>(static_cast<uint8_t>((out >> 8) & 0xFF));
}

bool GamingWmi::SetThermalMode(ThermalMode mode) {
    uint32_t out = 0;
    const uint64_t packed = PackMisc(0x0B, static_cast<uint8_t>(mode));
    std::ostringstream oss;
    oss << "SetGamingMiscSetting profile payload=0x" << std::hex << packed;
    Log(oss.str());
    if (!GetWmi().ExecU64InU32Out(kClass, L"SetGamingMiscSetting", packed, out)) {
        return false;
    }
    return true;
}

bool GamingWmi::SetMiscSetting(uint8_t index, uint8_t value) {
    uint32_t out = 0;
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingMiscSetting", PackMisc(index, value), out);
}

bool GamingWmi::SetGamingLed(uint64_t packed) {
    uint32_t out = 0;
    if (GetWmi().ExecU64InU32Out(kClass, L"SetGamingLED", packed, out)) {
        return true;
    }
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingLed", packed, out);
}

bool GamingWmi::SetGamingProfile(uint64_t packed) {
    uint32_t out = 0;
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingProfile", packed, out);
}

bool GamingWmi::SetLcdOverdrive(bool enable) {
    if (SetGamingProfile(enable ? 0x1000000000010ull : 0x10ull)) {
        return true;
    }
    return AcerService::SetLcdOverdrive(enable);
}

bool GamingWmi::ApplyTurboHardware(bool on) {
    bool ok = true;
    if (on) {
        ok = SetGamingLed(0x10001) && ok;
        ok = SetMiscSetting(0x05, 0x02) && ok;
        ok = SetMiscSetting(0x07, 0x02) && ok;
        ok = SetFanMode(FanMode::Max) && ok;
    } else {
        SetGamingLed(0x1);
        SetMiscSetting(0x05, 0x00);
        SetMiscSetting(0x07, 0x00);
        SetFanMode(FanMode::Auto);
    }
    return ok;
}

bool GamingWmi::SetKeyboardBacklight(const std::vector<uint8_t>& payload16) {
    if (payload16.size() != 16) {
        return false;
    }
    uint32_t out = 0;
    return GetWmi().ExecBytesInU32Out(kClass, L"SetGamingKBBacklight", payload16, out);
}

bool GamingWmi::SetKeyboardStaticZone(uint8_t zone_mask, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> payload{zone_mask, r, g, b};
    uint32_t out = 0;
    const wchar_t* names[] = {L"SetGamingKBBacklightStatic", L"SetGamingStaticLED",
                              L"SetGamingLEDColor", L"SetGamingKBBacklight"};
    for (auto* name : names) {
        if (GetWmi().ExecBytesInU32Out(kClass, name, payload, out)) {
            return true;
        }
    }
    const uint64_t packed = static_cast<uint64_t>(zone_mask) | (static_cast<uint64_t>(r) << 8) |
                            (static_cast<uint64_t>(g) << 16) | (static_cast<uint64_t>(b) << 24);
    return GetWmi().ExecU64InU32Out(kClass, L"SetGamingKBBacklightStatic", packed, out) ||
           GetWmi().ExecU64InU32Out(kClass, L"SetGamingStaticLED", packed, out);
}

std::vector<std::wstring> GamingWmi::Methods() const {
    return GetWmi().ListMethods(kClass);
}

}  // namespace predator
