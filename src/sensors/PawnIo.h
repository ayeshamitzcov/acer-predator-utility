#pragma once

#include <optional>
#include <string>

namespace predator {

struct CpuTelemetry {
    std::optional<float> package_watts;
    std::optional<float> voltage_v;
    std::optional<float> package_temp_c;
};

class PawnIo {
public:
    static bool DriverPresent();
    static bool Ready();
    static CpuTelemetry Sample();
    static std::optional<float> TryCpuPackageWatts();
    static std::string StatusText();
};

}  // namespace predator
