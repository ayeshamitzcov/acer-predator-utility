#pragma once

#include "acer/GamingWmi.h"

#include <string>
#include <vector>

namespace predator {

struct PowerScheme {
    std::string guid;
    std::string name;
    bool active = false;
};

class PowerPlans {
public:
    static std::vector<PowerScheme> List();
    static bool ActivateGuid(const std::string& guid);
    static bool SetOverlay(int kind);  // 0 efficiency, 1 balanced, 2 performance
    static bool ApplyForThermal(ThermalMode mode);
    static bool SetCpuPolicy(int min_percent, int max_percent, int boost_mode);
    static bool SetUsbPowerSaving(bool enable);
};

}  // namespace predator
