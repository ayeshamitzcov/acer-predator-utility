#include "display/DisplayMode.h"

#include "common/Log.h"

#include <windows.h>
#include <wbemidl.h>

#include <algorithm>
#include <string>

namespace predator {
namespace {

constexpr DWORD kDmDisplayFrequency = 0x00400000;

}  // namespace

DisplayStatus DisplayMode::Query() {
    DisplayStatus st;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        st.current_hz = static_cast<int>(dm.dmDisplayFrequency);
    }
    DWORD mode = 0;
    while (EnumDisplaySettingsW(nullptr, mode, &dm)) {
        const int hz = static_cast<int>(dm.dmDisplayFrequency);
        if (hz >= 30 && std::find(st.rates.begin(), st.rates.end(), hz) == st.rates.end()) {
            st.rates.push_back(hz);
        }
        ++mode;
    }
    std::sort(st.rates.begin(), st.rates.end());
    if (!st.rates.empty()) {
        st.min_hz = st.rates.front();
        st.max_hz = st.rates.back();
    }
    return st;
}

bool DisplayMode::SetRefreshHz(int hz) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        return false;
    }
    dm.dmDisplayFrequency = static_cast<DWORD>(hz);
    dm.dmFields = kDmDisplayFrequency;
    const LONG rc = ChangeDisplaySettingsW(&dm, CDS_UPDATEREGISTRY);
    Log(std::string("Set refresh ") + std::to_string(hz) + "Hz rc=" + std::to_string(rc));
    return rc == DISP_CHANGE_SUCCESSFUL;
}

int DisplayMode::GetBrightness() {
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
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IEnumWbemClassObject* en = nullptr;
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
    hr = svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(lang);
    SysFreeString(q);
    int out = -1;
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        if (en->Next(1000, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) {
                if (v.vt == VT_UI1) {
                    out = v.bVal;
                } else if (v.vt == VT_I4) {
                    out = v.lVal;
                }
            }
            VariantClear(&v);
            obj->Release();
        }
        en->Release();
    }
    svc->Release();
    return out;
}

bool DisplayMode::SetBrightness(int percent) {
    percent = std::clamp(percent, 0, 100);
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void**>(&loc))) ||
        !loc) {
        return false;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) {
        return false;
    }
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IEnumWbemClassObject* en = nullptr;
    BSTR cls = SysAllocString(L"WmiMonitorBrightnessMethods");
    hr = svc->CreateInstanceEnum(cls, WBEM_FLAG_FORWARD_ONLY, nullptr, &en);
    SysFreeString(cls);
    bool ok = false;
    if (FAILED(hr) || !en) {
        svc->Release();
        return false;
    }
    IWbemClassObject* inst = nullptr;
    ULONG got = 0;
    if (en->Next(1000, 1, &inst, &got) != WBEM_S_NO_ERROR || !got || !inst) {
        en->Release();
        svc->Release();
        return false;
    }
    VARIANT path;
    VariantInit(&path);
    inst->Get(L"__PATH", 0, &path, nullptr, nullptr);
    inst->Release();
    en->Release();
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(L"WmiMonitorBrightnessMethods");
    hr = svc->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    IWbemClassObject* pIn = nullptr;
    BSTR bmethod = SysAllocString(L"WmiSetBrightness");
    if (SUCCEEDED(hr) && pClass) {
        IWbemClassObject* sig = nullptr;
        pClass->GetMethod(bmethod, 0, &sig, nullptr);
        pClass->Release();
        if (sig) {
            sig->SpawnInstance(0, &pIn);
            sig->Release();
        }
    }
    SysFreeString(bclass);
    if (pIn) {
        VARIANT t, b;
        VariantInit(&t);
        VariantInit(&b);
        t.vt = VT_UI4;
        t.ulVal = 1;
        b.vt = VT_UI1;
        b.bVal = static_cast<BYTE>(percent);
        pIn->Put(L"Timeout", 0, &t, 0);
        pIn->Put(L"Brightness", 0, &b, 0);
        VariantClear(&t);
        VariantClear(&b);
        IWbemClassObject* pOut = nullptr;
        hr = svc->ExecMethod(path.bstrVal, bmethod, 0, nullptr, pIn, &pOut, nullptr);
        ok = SUCCEEDED(hr);
        if (pOut) {
            pOut->Release();
        }
        pIn->Release();
    }
    SysFreeString(bmethod);
    VariantClear(&path);
    svc->Release();
    Log(std::string("brightness ") + std::to_string(percent) + (ok ? " ok" : " fail"));
    return ok;
}

}  // namespace predator
