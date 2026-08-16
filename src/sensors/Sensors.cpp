#include "sensors/Sensors.h"

#include "acer/GamingWmi.h"
#include "sensors/PawnIo.h"

#include <pdh.h>
#include <windows.h>
#include <wbemidl.h>

#include <cmath>

namespace predator {
namespace {

float ReadThermalZoneC() {
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void**>(&loc))) ||
        !loc) {
        return -1;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) {
        return -1;
    }
    IEnumWbemClassObject* en = nullptr;
    BSTR wql = SysAllocString(L"WQL");
    BSTR q = SysAllocString(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
    hr = svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(wql);
    SysFreeString(q);
    float best = -1;
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        while (en->Next(1000, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &v, nullptr, nullptr))) {
                int tenths_k = 0;
                if (v.vt == VT_I4) {
                    tenths_k = v.lVal;
                } else if (v.vt == VT_UI4) {
                    tenths_k = static_cast<int>(v.ulVal);
                }
                const float c = tenths_k / 10.0f - 273.15f;
                if (c > best && c < 150) {
                    best = c;
                }
            }
            VariantClear(&v);
            obj->Release();
        }
        en->Release();
    }
    svc->Release();
    return best;
}

void ReadBattery(Snapshot& s) {
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        s.on_ac = ps.ACLineStatus == 1;
        if (ps.BatteryLifePercent != 255) {
            s.battery_percent = static_cast<float>(ps.BatteryLifePercent);
        }
    }

    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void**>(&loc))) ||
        !loc) {
        return;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) {
        return;
    }
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IEnumWbemClassObject* en = nullptr;
    BSTR wql = SysAllocString(L"WQL");
    BSTR q = SysAllocString(L"SELECT Voltage, ChargeRate, DischargeRate, RemainingCapacity FROM BatteryStatus");
    hr = svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(wql);
    SysFreeString(q);
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        if (en->Next(1000, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
            auto get_i = [&](const wchar_t* name) -> int {
                VARIANT v;
                VariantInit(&v);
                int r = 0;
                if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr))) {
                    if (v.vt == VT_I4) {
                        r = v.lVal;
                    } else if (v.vt == VT_UI4) {
                        r = static_cast<int>(v.ulVal);
                    }
                }
                VariantClear(&v);
                return r;
            };
            const int mv = get_i(L"Voltage");
            const int charge = get_i(L"ChargeRate");
            const int discharge = get_i(L"DischargeRate");
            if (mv > 0) {
                s.battery_voltage_v = mv / 1000.0f;
            }
            // ACPI rates are milliwatts on most Acer firmware.
            int mw = 0;
            if (charge > 0) {
                mw = charge;
            } else if (discharge > 0) {
                mw = -discharge;
            }
            s.battery_rate_mw = static_cast<float>(mw);
            s.battery_watts = mw / 1000.0f;
            obj->Release();
        }
        en->Release();
    }
    svc->Release();
}

std::string ReadModel() {
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void**>(&loc))) ||
        !loc) {
        return {};
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) {
        return {};
    }
    IEnumWbemClassObject* en = nullptr;
    BSTR wql = SysAllocString(L"WQL");
    BSTR q = SysAllocString(L"SELECT Name, Vendor FROM Win32_ComputerSystemProduct");
    hr = svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY, nullptr, &en);
    SysFreeString(wql);
    SysFreeString(q);
    std::string out;
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        if (en->Next(2000, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"Name", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) {
                char buf[128]{};
                WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, buf, sizeof(buf), nullptr, nullptr);
                out = buf;
            }
            VariantClear(&v);
            obj->Release();
        }
        en->Release();
    }
    svc->Release();
    return out;
}

}  // namespace

Sensors::Sensors() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
        PDH_HCOUNTER c = nullptr;
        if (PdhAddEnglishCounterW(q, L"\\Processor(_Total)\\% Processor Time", 0, &c) == ERROR_SUCCESS) {
            pdh_query_ = q;
            pdh_cpu_ = c;
            PdhCollectQueryData(q);
            pdh_ready_ = true;
        } else {
            PdhCloseQuery(q);
        }
    }
}

Sensors::~Sensors() {
    if (pdh_query_) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(pdh_query_));
    }
}

Snapshot Sensors::Poll(bool include_gpu) {
    Snapshot s;
    s.model = ReadModel();
    if (include_gpu) {
        s.gpu = nvml_.Sample();
        last_gpu_ = s.gpu;
    } else {
        s.gpu = last_gpu_;
    }
    ReadBattery(s);

    const auto cpu = PawnIo::Sample();
    if (cpu.package_watts) {
        s.cpu_power_w = *cpu.package_watts;
    }
    if (cpu.voltage_v) {
        s.cpu_voltage_v = *cpu.voltage_v;
    }
    if (cpu.package_temp_c) {
        s.cpu_temp_c = *cpu.package_temp_c;
    }

    if (pdh_ready_) {
        PdhCollectQueryData(static_cast<PDH_HQUERY>(pdh_query_));
        PDH_FMT_COUNTERVALUE val{};
        if (PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(pdh_cpu_), PDH_FMT_DOUBLE, nullptr,
                                        &val) == ERROR_SUCCESS) {
            s.cpu_util = static_cast<float>(val.doubleValue);
        }
    }

    GamingWmi wmi;
    if (wmi.Ok() || wmi.Connect()) {
        if (s.cpu_temp_c < 0) {
            if (auto t = wmi.GetSensorValue(0x01)) {
                s.cpu_temp_c = static_cast<float>(*t);
            }
        }
        if (auto t = wmi.GetSensorValue(0x0A)) {
            if (s.gpu.temp_c <= 0) {
                s.gpu.temp_c = static_cast<float>(*t);
            }
        }
        if (auto rpm = wmi.GetSensorValue(0x02)) {
            s.cpu_fan_rpm = static_cast<float>(*rpm);
        }
        if (s.cpu_fan_rpm <= 0) {
            if (auto rpm = wmi.GetSensorValue(0x0201)) {
                s.cpu_fan_rpm = static_cast<float>(*rpm);
            }
        }
        if (auto rpm = wmi.GetSensorValue(0x06)) {
            s.gpu_fan_rpm = static_cast<float>(*rpm);
        }
        if (s.gpu_fan_rpm <= 0) {
            if (auto rpm = wmi.GetSensorValue(0x0601)) {
                s.gpu_fan_rpm = static_cast<float>(*rpm);
            }
        }
        if (auto p = wmi.GetFanPercent(GamingWmi::kCpuFanId)) {
            s.cpu_fan_pct = static_cast<float>(*p);
        }
        if (auto p = wmi.GetFanPercent(GamingWmi::kGpuFanId)) {
            s.gpu_fan_pct = static_cast<float>(*p);
        }
    }
    if (s.cpu_temp_c < 0) {
        s.cpu_temp_c = ReadThermalZoneC();
    }
    return s;
}

}  // namespace predator
