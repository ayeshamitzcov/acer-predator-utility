#pragma once

#include <windows.h>

namespace predator::ui {

constexpr UINT kMsgPredatorKey = WM_APP + 20;
constexpr UINT kMsgTurboKey = WM_APP + 21;
constexpr UINT kMsgPredatorMode = WM_APP + 22;  // wParam 1-5

void HotkeysStart(HWND owner);
void HotkeysStop();
void HotkeysHandleRawInput(LPARAM lParam);

}  // namespace predator::ui
