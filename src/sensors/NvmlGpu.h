#pragma once

#include <string>

namespace predator {

struct GpuSample {
    bool present = false;
    std::string name;
    float temp_c = 0;
    float util_percent = 0;
    float power_w = 0;
    float core_clock_mhz = 0;
    float mem_clock_mhz = 0;
    float mem_used_mb = 0;
    float mem_total_mb = 0;
};

class NvmlGpu {
public:
    NvmlGpu();
    ~NvmlGpu();
    bool Ok() const { return ok_; }
    GpuSample Sample();

private:
    bool ok_ = false;
};

}  // namespace predator
