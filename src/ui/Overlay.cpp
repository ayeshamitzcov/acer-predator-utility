#include "ui/Overlay.h"

#include <windows.h>

#include <string>

namespace predator::ui {
namespace {

HWND g_hwnd = nullptr;
std::wstring g_text = L"Predator Utility";
bool g_visible = false;

void DestroyStale() {
    HWND stale = FindWindowW(L"PredatorLiteOsd", nullptr);
    while (stale) {
        DestroyWindow(stale);
        stale = FindWindowW(L"PredatorLiteOsd", nullptr);
    }
}

LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HBRUSH br = CreateSolidBrush(RGB(12, 12, 12));
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 140, 40));
            HFONT font = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
            auto old = SelectObject(hdc, font);
            DrawTextW(hdc, g_text.c_str(), -1, &rc, DT_LEFT | DT_TOP | DT_NOPREFIX);
            SelectObject(hdc, old);
            DeleteObject(font);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(h, m, w, l);
    }
}

bool CreateOverlayWindow() {
    if (g_hwnd && IsWindow(g_hwnd)) {
        return true;
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PredatorLiteOsd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    DestroyStale();
    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"PredatorLiteOsd", L"", WS_POPUP, 16, 16, 420, 88, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) {
        return false;
    }
    SetLayeredWindowAttributes(g_hwnd, 0, 210, LWA_ALPHA);
    return true;
}

}  // namespace

void OverlayCreate(HWND) {
    g_visible = false;
    g_hwnd = nullptr;
}

void OverlayDestroy() {
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    g_visible = false;
    DestroyStale();
}

void OverlaySetVisible(bool visible) {
    g_visible = visible;
    if (!visible) {
        if (g_hwnd) {
            ShowWindow(g_hwnd, SW_HIDE);
            DestroyWindow(g_hwnd);
            g_hwnd = nullptr;
        }
        DestroyStale();
        return;
    }
    if (!CreateOverlayWindow()) {
        return;
    }
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayUpdate(float cpu_t, float gpu_t, float cpu_w, float gpu_w, float batt_w, float cpu_fan,
                   float gpu_fan, bool on_ac) {
    if (!g_visible) {
        OverlaySetVisible(false);
        return;
    }
    wchar_t buf[256];
    if (gpu_t > 0) {
        swprintf_s(buf,
                   L"CPU %4.0fC  %4.0fW     GPU %4.0fC\n"
                   L"Pack %+5.0fW  Fans %4.0f / %4.0f  %s",
                   cpu_t, cpu_w < 0 ? 0 : cpu_w, gpu_t, batt_w, cpu_fan, gpu_fan,
                   on_ac ? L"AC" : L"BAT");
    } else {
        swprintf_s(buf,
                   L"CPU %4.0fC  %4.0fW\n"
                   L"Pack %+5.0fW  Fans %4.0f / %4.0f  %s",
                   cpu_t, cpu_w < 0 ? 0 : cpu_w, batt_w, cpu_fan, gpu_fan, on_ac ? L"AC" : L"BAT");
    }
    (void)gpu_w;
    g_text = buf;
    if (g_hwnd) {
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

}  // namespace predator::ui
