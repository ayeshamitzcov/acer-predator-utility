#pragma once

#include <windows.h>

namespace predator::ui {

constexpr UINT kTrayCallback = WM_APP + 1;

enum TrayCommand : UINT {
    IDM_TRAY_OPEN = 41001,
    IDM_TRAY_OVERLAY = 41002,
    IDM_TRAY_FANS_AUTO = 41003,
    IDM_TRAY_SILENT = 41004,
    IDM_TRAY_BALANCED = 41005,
    IDM_TRAY_ESPORTS = 41008,
    IDM_TRAY_TURBO = 41006,
    IDM_TRAY_ECO = 41009,
    IDM_TRAY_EXIT = 41007,
};

void TrayCreate(HWND owner);
void TrayDestroy();
void TrayBalloon(const wchar_t* text);
void TrayPopupMenu(HWND owner, bool overlay_on, int thermal_mode);
bool TrayIsClick(LPARAM lParam);

}  // namespace predator::ui
