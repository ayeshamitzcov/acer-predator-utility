#include "power/PowerPlans.h"

#include "common/Log.h"

#include <objbase.h>
#include <windows.h>
#include <powrprof.h>

#include <algorithm>
#include <array>

namespace predator {
namespace {

using PowerSetActiveOverlayScheme_t = DWORD(WINAPI*)(const GUID*);
using PowerGetActualOverlayScheme_t = DWORD(WINAPI*)(GUID*);

GUID kOverlayEfficiency{0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
GUID kOverlayBalanced{0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
GUID kOverlayPerformance{0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};

std::string GuidToString(const GUID& g) {
    wchar_t w[64]{};
    StringFromGUID2(g, w, 64);
    char a[64]{};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), nullptr, nullptr);
    return a;
}

std::wstring Utf8ToWide(const std::string& s) {
    std::wstring w(s.size() + 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), static_cast<int>(w.size()));
    return w;
}

}  // namespace

std::vector<PowerScheme> PowerPlans::List() {
    std::vector<PowerScheme> out;
    GUID* active = nullptr;
    PowerGetActiveScheme(nullptr, &active);
    DWORD index = 0;
    GUID scheme{};
    DWORD sz = sizeof(scheme);
    while (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index, reinterpret_cast<UCHAR*>(&scheme),
                          &sz) == ERROR_SUCCESS) {
        PowerScheme ps;
        ps.guid = GuidToString(scheme);
        DWORD nsz = 0;
        PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &nsz);
        std::wstring name(nsz / sizeof(wchar_t) + 1, 0);
        if (nsz) {
            PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr,
                                  reinterpret_cast<UCHAR*>(name.data()), &nsz);
        }
        char a[256]{};
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, a, sizeof(a), nullptr, nullptr);
        ps.name = a;
        if (active && IsEqualGUID(scheme, *active)) {
            ps.active = true;
        }
        out.push_back(std::move(ps));
        ++index;
        sz = sizeof(scheme);
    }
    if (active) {
        LocalFree(active);
    }
    return out;
}

bool PowerPlans::ActivateGuid(const std::string& guid) {
    GUID g{};
    auto w = Utf8ToWide(guid);
    if (CLSIDFromString(w.c_str(), &g) != NOERROR) {
        return false;
    }
    Log(std::string("powercfg setactive ") + guid);
    return PowerSetActiveScheme(nullptr, &g) == ERROR_SUCCESS;
}

bool PowerPlans::SetOverlay(int kind) {
    HMODULE mod = LoadLibraryW(L"powrprof.dll");
    if (!mod) {
        return false;
    }
    auto fn = reinterpret_cast<PowerSetActiveOverlayScheme_t>(
        GetProcAddress(mod, "PowerSetActiveOverlayScheme"));
    if (!fn) {
        return false;
    }
    const GUID* g = &kOverlayBalanced;
    if (kind == 0) {
        g = &kOverlayEfficiency;
    } else if (kind == 2) {
        g = &kOverlayPerformance;
    }
    const DWORD st = fn(g);
    Log(std::string("PowerSetActiveOverlayScheme ") + std::to_string(kind) + " st=" + std::to_string(st));
    return st == ERROR_SUCCESS;
}

bool PowerPlans::ApplyForThermal(ThermalMode mode) {
    int overlay = 1;
    const char* scheme_hint = nullptr;
    switch (mode) {
        case ThermalMode::Silent:
        case ThermalMode::Eco:
            overlay = 0;
            scheme_hint = "{a1841308-3541-4fab-bc81-f71556f20b4a}";  // Power saver
            break;
        case ThermalMode::Balanced:
            overlay = 1;
            scheme_hint = "{381b4222-f694-41f0-9685-ff5bb260df2e}";
            break;
        case ThermalMode::Performance:
        case ThermalMode::Turbo:
            overlay = 2;
            scheme_hint = "{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}";  // High performance
            break;
    }
    SetOverlay(overlay);
    if (scheme_hint) {
        ActivateGuid(scheme_hint);
    }
    return true;
}

bool WriteBoth(GUID* scheme, const GUID* sub, const GUID* setting, DWORD value) {
    PowerWriteACValueIndex(nullptr, scheme, sub, setting, value);
    PowerWriteDCValueIndex(nullptr, scheme, sub, setting, value);
    return true;
}

bool PowerPlans::SetCpuPolicy(int min_percent, int max_percent, int boost_mode) {
    min_percent = std::clamp(min_percent, 0, 100);
    max_percent = std::clamp(max_percent, 1, 100);
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) {
        return false;
    }
    GUID sub{0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
    GUID throttle_min{0x893dee8e, 0x2bef, 0x41e0, {0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c}};
    GUID throttle_max{0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
    GUID boost{0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
    WriteBoth(scheme, &sub, &throttle_min, static_cast<DWORD>(min_percent));
    WriteBoth(scheme, &sub, &throttle_max, static_cast<DWORD>(max_percent));
    WriteBoth(scheme, &sub, &boost, static_cast<DWORD>(boost_mode));
    const bool ok = PowerSetActiveScheme(nullptr, scheme) == ERROR_SUCCESS;
    LocalFree(scheme);
    Log(std::string("CPU policy min=") + std::to_string(min_percent) +
        " max=" + std::to_string(max_percent) + " boost=" + std::to_string(boost_mode));
    return ok;
}

bool PowerPlans::SetUsbPowerSaving(bool enable) {
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme) {
        return false;
    }
    GUID usb{0x2a737441, 0x1930, 0x4402, {0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3}};
    GUID sel{0x48e6b7a6, 0x50f5, 0x4782, {0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26}};
    WriteBoth(scheme, &usb, &sel, enable ? 1u : 0u);
    const bool ok = PowerSetActiveScheme(nullptr, scheme) == ERROR_SUCCESS;
    LocalFree(scheme);
    return ok;
}

}  // namespace predator
