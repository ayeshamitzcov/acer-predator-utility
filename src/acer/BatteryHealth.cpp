#include "acer/BatteryHealth.h"

#include "acer/WmiHub.h"
#include "common/Log.h"

#include <vector>

namespace predator {

BatteryHealth::Status BatteryHealth::Query() {
    Status s;
    auto& wmi = GetWmi();
    if (!wmi.Ok()) {
        return s;
    }
    uint8_t list = 0;
    std::vector<uint8_t> st;
    if (!wmi.ExecBatteryGetHealth(1, 1, list, st)) {
        Log("BatteryControl GetBatteryHealthControlStatus failed");
        return s;
    }
    s.available = true;
    if (!st.empty()) {
        s.health_mode = st[0] != 0;
    }
    if (st.size() > 1) {
        s.calibration = st[1] != 0;
    }
    if ((list & 1) == 0 && st.empty()) {
        s.available = false;
    }
    return s;
}

bool BatteryHealth::SetHealthMode(bool enabled) {
    Log(enabled ? "enabling 80% charge limit" : "disabling 80% charge limit");
    return GetWmi().ExecBatterySetHealth(1, 1, enabled ? 1 : 0);
}

}  // namespace predator
