#include "common/Settings.h"

#include "common/Log.h"

#include <fstream>
#include <map>
#include <sstream>

namespace predator {
namespace {

std::wstring SettingsPath() {
    return AppDataDir() + L"\\settings.ini";
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) {
        s.push_back(static_cast<char>(c < 128 ? c : '?'));
    }
    return s;
}

int ParseInt(const std::map<std::string, std::string>& kv, const char* key, int fallback) {
    auto it = kv.find(key);
    if (it == kv.end()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

Settings Settings::Load() {
    Settings s;
    std::ifstream in(Narrow(SettingsPath()));
    if (!in) {
        return s;
    }
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '[') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    s.osd = ParseInt(kv, "osd", 0) != 0;
    s.eco_on_battery = ParseInt(kv, "eco_on_battery", 1) != 0;
    s.min_fan_percent = ParseInt(kv, "min_fan_percent", 20);
    s.last_ac_mode = ParseInt(kv, "last_ac_mode", 1);
    s.gpu_core_offset_mhz = ParseInt(kv, "gpu_core_offset_mhz", 0);
    s.gpu_mem_offset_mhz = ParseInt(kv, "gpu_mem_offset_mhz", 0);
    s.couple_fans = ParseInt(kv, "couple_fans", 1) != 0;
    s.monitor_gpu = ParseInt(kv, "monitor_gpu", 0) != 0;
    s.disable_dgpu_on_eco = ParseInt(kv, "disable_dgpu_on_eco", 1) != 0;
    s.rgb_mode = ParseInt(kv, "rgb_mode", 0);
    s.rgb_speed = ParseInt(kv, "rgb_speed", 3);
    s.rgb_brightness = ParseInt(kv, "rgb_brightness", 80);
    s.rgb_r = ParseInt(kv, "rgb_r", 255);
    s.rgb_g = ParseInt(kv, "rgb_g", 80);
    s.rgb_b = ParseInt(kv, "rgb_b", 0);
    s.rgb_dir = ParseInt(kv, "rgb_dir", 1);
    if (s.min_fan_percent < 10) {
        s.min_fan_percent = 10;
    }
    if (s.min_fan_percent > 50) {
        s.min_fan_percent = 50;
    }
    return s;
}

void Settings::Save() const {
    std::ofstream out(Narrow(SettingsPath()), std::ios::trunc);
    if (!out) {
        Log("failed to save settings.ini");
        return;
    }
    out << "osd=" << (osd ? 1 : 0) << "\n";
    out << "eco_on_battery=" << (eco_on_battery ? 1 : 0) << "\n";
    out << "min_fan_percent=" << min_fan_percent << "\n";
    out << "last_ac_mode=" << last_ac_mode << "\n";
    out << "gpu_core_offset_mhz=" << gpu_core_offset_mhz << "\n";
    out << "gpu_mem_offset_mhz=" << gpu_mem_offset_mhz << "\n";
    out << "couple_fans=" << (couple_fans ? 1 : 0) << "\n";
    out << "monitor_gpu=" << (monitor_gpu ? 1 : 0) << "\n";
    out << "disable_dgpu_on_eco=" << (disable_dgpu_on_eco ? 1 : 0) << "\n";
    out << "rgb_mode=" << rgb_mode << "\n";
    out << "rgb_speed=" << rgb_speed << "\n";
    out << "rgb_brightness=" << rgb_brightness << "\n";
    out << "rgb_r=" << rgb_r << "\n";
    out << "rgb_g=" << rgb_g << "\n";
    out << "rgb_b=" << rgb_b << "\n";
    out << "rgb_dir=" << rgb_dir << "\n";
}

}  // namespace predator
