#pragma once

#include <optional>

namespace predator {

class BatteryHealth {
public:
    struct Status {
        bool available = false;
        bool health_mode = false;  // ~80% charge limit
        bool calibration = false;
    };

    static Status Query();
    static bool SetHealthMode(bool enabled);
};

}  // namespace predator
