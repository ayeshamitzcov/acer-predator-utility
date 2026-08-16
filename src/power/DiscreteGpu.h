#pragma once

#include <string>

namespace predator {

class DiscreteGpu {
public:
    static bool NvidiaPresent();
    static bool NvidiaEnabled();
    static bool SetNvidiaEnabled(bool enable);
};

}  // namespace predator
