#include "power/DiscreteGpu.h"

#include "common/Log.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <string>
#include <vector>

namespace predator {
namespace {

bool IsNvidiaHwId(const wchar_t* ids) {
    if (!ids) {
        return false;
    }
    std::wstring up(ids);
    for (auto& c : up) {
        if (c >= L'a' && c <= L'z') {
            c = static_cast<wchar_t>(c - L'a' + L'A');
        }
    }
    return up.find(L"VEN_10DE") != std::wstring::npos;
}

std::wstring HardwareIds(HDEVINFO set, SP_DEVINFO_DATA& info) {
    DWORD needed = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr, nullptr, 0, &needed);
    if (needed < 4) {
        return {};
    }
    std::vector<wchar_t> buf(needed / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr,
                                           reinterpret_cast<PBYTE>(buf.data()), needed, nullptr)) {
        return {};
    }
    return buf.data();
}

template <typename Fn>
void EachNvidiaGpu(Fn fn) {
    GUID display = GUID_DEVCLASS_DISPLAY;
    HDEVINFO set = SetupDiGetClassDevsW(&display, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return;
    }
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        const std::wstring hw = HardwareIds(set, info);
        if (IsNvidiaHwId(hw.c_str())) {
            fn(set, info);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
}

}  // namespace

bool DiscreteGpu::NvidiaPresent() {
    bool present = false;
    EachNvidiaGpu([&](HDEVINFO, SP_DEVINFO_DATA&) { present = true; });
    return present;
}

bool DiscreteGpu::NvidiaEnabled() {
    bool enabled = false;
    EachNvidiaGpu([&](HDEVINFO, SP_DEVINFO_DATA& info) {
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS &&
            (status & DN_STARTED)) {
            enabled = true;
        }
    });
    return enabled;
}

bool DiscreteGpu::SetNvidiaEnabled(bool enable) {
    bool ok = false;
    EachNvidiaGpu([&](HDEVINFO set, SP_DEVINFO_DATA& info) {
        SP_PROPCHANGE_PARAMS pcp{};
        pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        pcp.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
        pcp.Scope = DICS_FLAG_GLOBAL;
        pcp.HwProfile = 0;
        SetupDiSetClassInstallParamsW(set, &info, &pcp.ClassInstallHeader, sizeof(pcp));
        SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, set, &info);
        const CONFIGRET cr =
            enable ? CM_Enable_DevNode(info.DevInst, 0) : CM_Disable_DevNode(info.DevInst, 0);
        Log(std::string(enable ? "enable" : "disable") + " NVIDIA GPU cfg=" + std::to_string(cr));
        if (cr == CR_SUCCESS) {
            ok = true;
        }
    });
    return ok;
}

}  // namespace predator
