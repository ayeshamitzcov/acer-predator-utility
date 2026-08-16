#pragma once

#include <vector>

namespace predator {

struct DisplayStatus {
    int current_hz = 0;
    int min_hz = 60;
    int max_hz = 60;
    std::vector<int> rates;
};

class DisplayMode {
public:
    static DisplayStatus Query();
    static bool SetRefreshHz(int hz);
    static int GetBrightness();
    static bool SetBrightness(int percent);
};

}  // namespace predator
