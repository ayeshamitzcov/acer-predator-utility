#pragma once

namespace predator {

// Stop PredatorSense UI/service so it cannot steal keys or override fans.
// Keeps AcerService / lighting. Optional pump keeps the UI painting.
void KillPredatorSense(void (*pump)() = nullptr);

}  // namespace predator
