#pragma once

#include <windows.h>

namespace predator::ui {

void OverlayCreate(HWND parent);
void OverlayDestroy();
void OverlaySetVisible(bool visible);
void OverlayUpdate(float cpu_t, float gpu_t, float cpu_w, float gpu_w, float batt_w, float cpu_fan,
                   float gpu_fan, bool on_ac);

}  // namespace predator::ui
