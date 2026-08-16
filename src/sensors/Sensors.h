#pragma once

#include "sensors/NvmlGpu.h"

#include <string>

namespace predator {

struct Snapshot {
    std::string model;
    std::string manufacturer;

    float cpu_util = 0;
    float cpu_temp_c = -1;
    float cpu_power_w = -1;
    float cpu_voltage_v = -1;

    GpuSample gpu;

    float cpu_fan_rpm = -1;
    float gpu_fan_rpm = -1;
    float cpu_fan_pct = -1;
    float gpu_fan_pct = -1;

    bool on_ac = true;
    float battery_percent = -1;
    float battery_voltage_v = -1;
    float battery_watts = 0;  // negative = discharge
    float battery_rate_mw = 0;

    std::string note;
};

class Sensors {
public:
    Sensors();
    ~Sensors();
    Snapshot Poll(bool include_gpu = true);

private:
    void* pdh_query_ = nullptr;
    void* pdh_cpu_ = nullptr;
    NvmlGpu nvml_;
    GpuSample last_gpu_{};
    bool pdh_ready_ = false;
};

}  // namespace predator
