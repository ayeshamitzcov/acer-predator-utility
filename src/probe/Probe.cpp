#include "probe/Probe.h"

#include "acer/AcerService.h"
#include "acer/BatteryHealth.h"
#include "acer/GamingWmi.h"
#include "acer/WmiHub.h"
#include "sensors/NvapiOc.h"
#include "sensors/NvmlGpu.h"
#include "sensors/PawnIo.h"

#include <windows.h>
#include <winsvc.h>
#include <wbemidl.h>

#include <sstream>

namespace predator {
namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n ? n - 1 : 0), 0);
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    }
    return s;
}

void DumpService(std::ostringstream& oss, const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
    oss << "  " << WideToUtf8(name) << ": ";
    if (!svc) {
        oss << "not installed\n";
        CloseServiceHandle(scm);
        return;
    }
    SERVICE_STATUS_PROCESS ssp{};
    DWORD needed = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp),
                             &needed)) {
        oss << (ssp.dwCurrentState == SERVICE_RUNNING ? "RUNNING" : "stopped/other");
    } else {
        oss << "query failed";
    }
    oss << "\n";
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

}  // namespace

std::string RunHardwareProbe() {
    std::ostringstream oss;
    oss << "Predator Utility hardware probe\n";
    oss << "================================\n\n";

    IWbemLocator* loc = nullptr;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                   reinterpret_cast<void**>(&loc))) &&
        loc) {
        IWbemServices* cim = nullptr;
        BSTR ns = SysAllocString(L"ROOT\\CIMV2");
        if (SUCCEEDED(loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &cim)) &&
            cim) {
            CoSetProxyBlanket(cim, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            IEnumWbemClassObject* en = nullptr;
            BSTR wql = SysAllocString(L"WQL");
            BSTR q = SysAllocString(L"SELECT Name, Vendor, Version FROM Win32_ComputerSystemProduct");
            if (SUCCEEDED(cim->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY, nullptr, &en)) && en) {
                IWbemClassObject* obj = nullptr;
                ULONG got = 0;
                if (en->Next(3000, 1, &obj, &got) == WBEM_S_NO_ERROR && obj) {
                    VARIANT v;
                    VariantInit(&v);
                    auto grab = [&](const wchar_t* n) {
                        VariantClear(&v);
                        if (SUCCEEDED(obj->Get(n, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR) {
                            oss << WideToUtf8(n) << ": " << WideToUtf8(v.bstrVal) << "\n";
                        }
                    };
                    grab(L"Vendor");
                    grab(L"Name");
                    grab(L"Version");
                    VariantClear(&v);
                    obj->Release();
                }
                en->Release();
            }
            SysFreeString(wql);
            SysFreeString(q);
            cim->Release();
        }
        SysFreeString(ns);
        loc->Release();
    }
    oss << "\nAcer services\n";
    const wchar_t* svcs[] = {L"PSSvc",
                             L"PredatorService",
                             L"AcerServiceSvc",
                             L"AcerLightingService",
                             L"AcerQAAgentSvis",
                             L"AcerCCAgentSvis",
                             L"AcerDIAgentSvis",
                             L"ASMSvc",
                             L"AcerDeviceEnablingServiceV2"};
    for (auto* n : svcs) {
        DumpService(oss, n);
    }

    oss << "\nAcerService TCP: " << (AcerService::Available() ? "reachable" : "not reachable") << "\n";
    oss << "PawnIO: " << PawnIo::StatusText() << "\n";

    NvmlGpu nvml;
    auto gpu = nvml.Sample();
    oss << "NVML: " << (gpu.present ? gpu.name : "not available") << "\n";

    auto& wmi = GetWmi();
    oss << "\nWMI ROOT\\WMI: " << (wmi.Ok() ? "connected" : "FAILED - " + wmi.LastError()) << "\n";
    if (wmi.Ok()) {
        oss << "\nInteresting classes:\n";
        for (const auto& c : wmi.ListClasses()) {
            const auto s = WideToUtf8(c);
            if (s.find("Acer") != std::string::npos || s.find("APGe") != std::string::npos ||
                s.find("Battery") != std::string::npos) {
                oss << "  " << s << "\n";
            }
        }
        oss << "\nAcerGamingFunction methods:\n";
        for (const auto& m : wmi.ListMethods(L"AcerGamingFunction")) {
            oss << "  " << WideToUtf8(m) << "\n";
        }

        GamingWmi g;
        oss << "\nSample reads:\n";
        const uint32_t cmds[] = {0x01, 0x02, 0x06, 0x0A, 0x0201, 0x0601};
        const char* labels[] = {"CPU temp/id 0x01", "CPU fan 0x02", "GPU fan 0x06",
                                "GPU temp 0x0A",    "v4 CPU RPM",   "v4 GPU RPM"};
        for (size_t i = 0; i < 6; ++i) {
            auto raw = g.GetSysInfo(cmds[i]);
            oss << "  GetGamingSysInfo " << labels[i] << ": ";
            if (raw) {
                oss << "raw=0x" << std::hex << *raw << std::dec
                    << " decoded=" << GamingWmi::DecodeSysInfo(*raw) << "\n";
            } else {
                oss << "fail (" << wmi.LastError() << ")\n";
            }
        }
        if (auto p = g.GetFanPercent(GamingWmi::kCpuFanId)) {
            oss << "  CPU fan %: " << *p << "\n";
        }
        if (auto p = g.GetFanPercent(GamingWmi::kGpuFanId)) {
            oss << "  GPU fan %: " << *p << "\n";
        }
        if (auto m = g.GetFanMode()) {
            oss << "  Fan mode: " << static_cast<int>(*m) << "\n";
        }
        if (auto t = g.GetThermalMode()) {
            oss << "  Thermal: " << ThermalModeName(*t) << " (" << static_cast<int>(*t) << ")\n";
        }
        uint64_t misc = 0;
        if (GetWmi().ExecU32InU64Out(L"AcerGamingFunction", L"GetGamingMiscSetting", 0x05, misc)) {
            oss << "  BIOS CPU OC (misc 0x05): " << ((misc >> 8) & 0xFF) << "\n";
        }
        if (GetWmi().ExecU32InU64Out(L"AcerGamingFunction", L"GetGamingMiscSetting", 0x07, misc)) {
            oss << "  BIOS GPU OC (misc 0x07): " << ((misc >> 8) & 0xFF) << "\n";
        }
        auto bat = BatteryHealth::Query();
        oss << "  Battery health WMI: " << (bat.available ? "yes" : "no");
        if (bat.available) {
            oss << " limit=" << (bat.health_mode ? "80%" : "100%");
        }
        oss << "\n";
    }

    NvapiOc nv;
    int core = 0;
    int mem = 0;
    oss << "NVAPI GPU offsets: ";
    if (nv.GetOffsetsMhz(core, mem)) {
        oss << "core " << core << " MHz, memory " << mem << " MHz\n";
    } else {
        oss << nv.LastError() << "\n";
    }

    oss << "\nWrites are not performed by the probe.\n";
    return oss.str();
}

}  // namespace predator
