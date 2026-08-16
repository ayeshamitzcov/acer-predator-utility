#include "ui/Hotkeys.h"

#include "acer/WmiSession.h"
#include "common/Log.h"

#include <windows.h>
#include <wbemidl.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace predator::ui {
namespace {

HHOOK g_hook = nullptr;
HWND g_owner = nullptr;
std::atomic<bool> g_run{false};
std::thread g_wmi_thread;
bool g_predator_down = false;
bool g_predator_combo = false;
DWORD g_last_logged_scan = 0xFFFFFFFFu;

bool IsPredatorScan(DWORD scan) {
    return scan == 0x75;
}

void LogOemKey(DWORD vk, DWORD scan, DWORD flags) {
    if (scan == g_last_logged_scan) {
        return;
    }
    const bool oem = scan >= 0x59 || vk == 0xFF || (flags & LLKHF_EXTENDED);
    if (!oem) {
        return;
    }
    g_last_logged_scan = scan;
    Log("key vk=0x" + std::to_string(vk) + " scan=0x" + std::to_string(scan) +
        " flags=0x" + std::to_string(flags));
}

LRESULT CALLBACK LowLevelKbd(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && g_owner) {
        const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
        if (down) {
            LogOemKey(k->vkCode, k->scanCode, k->flags);
        }
        if (IsPredatorScan(k->scanCode)) {
            if (down) {
                g_predator_down = true;
                g_predator_combo = false;
            } else if (up) {
                if (!g_predator_combo) {
                    PostMessageW(g_owner, kMsgPredatorKey, 0, 0);
                }
                g_predator_down = false;
                g_predator_combo = false;
            }
            return 1;
        }
        if (g_predator_down && down) {
            int n = 0;
            if (k->vkCode >= 0x31 && k->vkCode <= 0x35) {
                n = static_cast<int>(k->vkCode - 0x30);
            } else if (k->vkCode >= 0x61 && k->vkCode <= 0x65) {
                n = static_cast<int>(k->vkCode - 0x60);
            }
            if (n >= 1 && n <= 5) {
                g_predator_combo = true;
                PostMessageW(g_owner, kMsgPredatorMode, static_cast<WPARAM>(n), 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

int VariantToInt(const VARIANT& v) {
    if (v.vt == VT_I4) {
        return v.lVal;
    }
    if (v.vt == VT_UI4) {
        return static_cast<int>(v.ulVal);
    }
    if (v.vt == VT_UI1) {
        return v.bVal;
    }
    if (v.vt == VT_I2) {
        return v.iVal;
    }
    if (v.vt == VT_UI2) {
        return v.uiVal;
    }
    return -1;
}

std::vector<uint8_t> VariantToBytes(const VARIANT& v) {
    std::vector<uint8_t> out;
    if ((v.vt & VT_ARRAY) && v.parray) {
        void* data = nullptr;
        LONG lo = 0, hi = 0;
        SafeArrayGetLBound(v.parray, 1, &lo);
        SafeArrayGetUBound(v.parray, 1, &hi);
        if (SUCCEEDED(SafeArrayAccessData(v.parray, &data)) && data && hi >= lo) {
            const auto n = static_cast<size_t>(hi - lo + 1);
            if ((v.vt & VT_TYPEMASK) == VT_UI1) {
                auto* p = static_cast<uint8_t*>(data);
                out.assign(p, p + n);
            } else if ((v.vt & VT_TYPEMASK) == VT_I4) {
                auto* p = static_cast<int*>(data);
                for (size_t i = 0; i < n && i < 8; ++i) {
                    out.push_back(static_cast<uint8_t>(p[i]));
                }
            }
            SafeArrayUnaccessData(v.parray);
        }
    }
    return out;
}

void DispatchAcerEvent(int fn, int key) {
    Log("Acer key event fn=" + std::to_string(fn) + " key=" + std::to_string(key));
    if (!g_owner) {
        return;
    }
    // Turbo is WMI fn 7 / key 4 only. FN+arrows also send fn=4 key=0 and must not change modes.
    if (fn == 7 && key == 4) {
        PostMessageW(g_owner, kMsgTurboKey, 0, 0);
        return;
    }
    if (fn == 7 && key == 1) {
        PostMessageW(g_owner, kMsgPredatorKey, 0, 0);
    }
}

void HandleEventObject(IWbemClassObject* obj) {
    VARIANT v;
    VariantInit(&v);
    std::vector<uint8_t> bytes;
    int detail = -1;
    const wchar_t* names[] = {L"EventDetail", L"uEventDetail", L"key_num", L"Active",
                              L"WmiEventData"};
    for (auto* n : names) {
        if (FAILED(obj->Get(n, 0, &v, nullptr, nullptr))) {
            continue;
        }
        auto b = VariantToBytes(v);
        if (!b.empty()) {
            bytes = std::move(b);
        } else {
            const int i = VariantToInt(v);
            if (i >= 0) {
                detail = i;
            }
        }
        VariantClear(&v);
        VariantInit(&v);
    }
    VariantClear(&v);

    if (bytes.size() >= 2) {
        DispatchAcerEvent(bytes[0], bytes[1]);
        return;
    }
    if (bytes.size() == 1) {
        DispatchAcerEvent(bytes[0], 0);
        return;
    }
    if (detail >= 0) {
        if (detail > 255) {
            DispatchAcerEvent(detail & 0xFF, (detail >> 8) & 0xFF);
        } else {
            DispatchAcerEvent(detail, detail);
        }
    }
}

void DrainEvents(IEnumWbemClassObject* en) {
    if (!en) {
        return;
    }
    IWbemClassObject* obj = nullptr;
    ULONG got = 0;
    if (en->Next(0, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
        HandleEventObject(obj);
        obj->Release();
    }
}

void WmiThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    WmiSession session;
    if (!session.Connect()) {
        Log("hotkey WMI connect failed");
        CoUninitialize();
        return;
    }
    std::vector<IEnumWbemClassObject*> ens;
    auto add = [&](const wchar_t* wql) {
        IEnumWbemClassObject* en = nullptr;
        if (session.ExecNotificationQuery(wql, &en) && en) {
            ens.push_back(en);
        }
    };
    add(L"SELECT * FROM WMIEvent");
    add(L"SELECT * FROM APGeEvent");
    add(L"SELECT * FROM AcerGenericEvent");
    add(L"SELECT * FROM AcerGamingFunctionEvent");
    for (const auto& c : session.ListClasses()) {
        if (c.find(L"Event") == std::wstring::npos) {
            continue;
        }
        if (c.find(L"Acer") == std::wstring::npos && c.find(L"APGe") == std::wstring::npos &&
            c.find(L"Gaming") == std::wstring::npos && c.find(L"WMIEvent") == std::wstring::npos) {
            continue;
        }
        std::wstring q = L"SELECT * FROM ";
        q += c;
        add(q.c_str());
    }
    Log("WMI hotkey watchers=" + std::to_string(ens.size()));
    while (g_run) {
        for (auto* en : ens) {
            DrainEvents(en);
        }
        Sleep(40);
    }
    for (auto* en : ens) {
        en->Release();
    }
    CoUninitialize();
}

}  // namespace

void HotkeysStart(HWND owner) {
    g_owner = owner;
    RAWINPUTDEVICE rids[2]{};
    rids[0].usUsagePage = 0x01;
    rids[0].usUsage = 0x06;
    rids[0].dwFlags = RIDEV_INPUTSINK;
    rids[0].hwndTarget = owner;
    rids[1].usUsagePage = 0x0C;
    rids[1].usUsage = 0x01;
    rids[1].dwFlags = RIDEV_INPUTSINK;
    rids[1].hwndTarget = owner;
    if (!RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE))) {
        Log("raw input register failed");
    }
    if (!g_hook) {
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbd, GetModuleHandleW(nullptr), 0);
        Log(g_hook ? "keyboard hook active" : "keyboard hook failed");
    }
    if (!g_run) {
        g_run = true;
        g_wmi_thread = std::thread(WmiThread);
    }
}

void HotkeysStop() {
    g_run = false;
    if (g_wmi_thread.joinable()) {
        g_wmi_thread.join();
    }
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    g_owner = nullptr;
}

void HotkeysHandleRawInput(LPARAM lParam) {
    UINT sz = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
    if (sz == 0) {
        return;
    }
    std::vector<uint8_t> buf(sz);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buf.data(), &sz,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }
    auto* raw = reinterpret_cast<RAWINPUT*>(buf.data());
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& kb = raw->data.keyboard;
        if (kb.Flags & RI_KEY_BREAK) {
            if (IsPredatorScan(kb.MakeCode) && !g_predator_combo) {
                PostMessageW(g_owner, kMsgPredatorKey, 0, 0);
            }
        } else if (IsPredatorScan(kb.MakeCode)) {
            g_predator_down = true;
            g_predator_combo = false;
        }
    }
}

}  // namespace predator::ui
