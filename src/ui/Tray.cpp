#include "ui/Tray.h"

#include <shellapi.h>
#include <windows.h>

namespace predator::ui {
namespace {

NOTIFYICONDATAW g_nid{};
bool g_added = false;

}  // namespace

void TrayCreate(HWND owner) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = owner;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kTrayCallback;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Predator Utility");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    g_added = true;
}

void TrayDestroy() {
    if (g_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_added = false;
    }
}

void TrayBalloon(const wchar_t* text) {
    if (!g_added) {
        return;
    }
    g_nid.uFlags = NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, L"Predator Utility");
    wcsncpy_s(g_nid.szInfo, text, _TRUNCATE);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
}

bool TrayIsClick(LPARAM lParam) {
    const UINT ev = LOWORD(lParam);
    return ev == WM_LBUTTONUP || ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU || ev == NIN_SELECT ||
           ev == NIN_KEYSELECT || ev == WM_LBUTTONDBLCLK;
}

void TrayPopupMenu(HWND owner, bool overlay_on, int thermal_mode) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    const auto mode_flag = [thermal_mode](int id) -> UINT {
        return MF_STRING | (thermal_mode == id ? MF_CHECKED : 0);
    };
    AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Open Predator Utility");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (overlay_on ? MF_CHECKED : 0), IDM_TRAY_OVERLAY, L"On-screen overlay");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_FANS_AUTO, L"Fans: Auto");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, mode_flag(0), IDM_TRAY_SILENT, L"Quiet");
    AppendMenuW(menu, mode_flag(1), IDM_TRAY_BALANCED, L"Normal");
    AppendMenuW(menu, mode_flag(4), IDM_TRAY_ESPORTS, L"ESports");
    AppendMenuW(menu, mode_flag(5), IDM_TRAY_TURBO, L"Turbo");
    AppendMenuW(menu, mode_flag(6), IDM_TRAY_ECO, L"Battery saver");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    SetForegroundWindow(owner);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

}  // namespace predator::ui
