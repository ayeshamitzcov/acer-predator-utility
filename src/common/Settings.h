#pragma once

#include <string>

namespace predator {

struct Settings {
    bool osd = false;
    bool eco_on_battery = true;
    int min_fan_percent = 20;
    int last_ac_mode = 1;  // Balanced
    int gpu_core_offset_mhz = 0;
    int gpu_mem_offset_mhz = 0;
    bool couple_fans = true;
    bool monitor_gpu = false;
    bool disable_dgpu_on_eco = true;
    bool start_with_windows = true;
    bool start_minimized = false;
    int rgb_mode = 0;
    int rgb_speed = 3;
    int rgb_brightness = 80;
    int rgb_r = 255;
    int rgb_g = 80;
    int rgb_b = 0;
    int rgb_dir = 1;

    static Settings Load();
    void Save() const;
};

}  // namespace predator
