#include "acer/WmiSession.h"

#include "common/Log.h"

#include <windows.h>
#include <wbemidl.h>

#include <cstring>
#include <map>
#include <sstream>

#ifndef CIM_UINT32
#define CIM_UINT32 19
#endif
#ifndef CIM_UINT64
#define CIM_UINT64 21
#endif

namespace predator {
namespace {

void VariantClearSafe(VARIANT& v) {
    VariantClear(&v);
}

bool PutU32(IWbemClassObject* pIn, uint32_t in) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = static_cast<LONG>(in);
    HRESULT hr = pIn->Put(L"gmInput", 0, &v, CIM_UINT32);
    if (FAILED(hr)) {
        VariantClearSafe(v);
        VariantInit(&v);
        v.vt = VT_UI4;
        v.ulVal = in;
        hr = pIn->Put(L"gmInput", 0, &v, 0);
    }
    VariantClearSafe(v);
    return SUCCEEDED(hr);
}

bool PutU64(IWbemClassObject* pIn, uint64_t in) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI8;
    v.ullVal = in;
    HRESULT hr = pIn->Put(L"gmInput", 0, &v, CIM_UINT64);
    if (SUCCEEDED(hr)) {
        VariantClearSafe(v);
        return true;
    }
    VariantClearSafe(v);
    VariantInit(&v);
    v.vt = VT_I8;
    v.llVal = static_cast<LONGLONG>(in);
    hr = pIn->Put(L"gmInput", 0, &v, CIM_UINT64);
    if (SUCCEEDED(hr)) {
        VariantClearSafe(v);
        return true;
    }
    VariantClearSafe(v);
    VariantInit(&v);
    wchar_t buf[32];
    swprintf_s(buf, L"%llu", static_cast<unsigned long long>(in));
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(buf);
    hr = pIn->Put(L"gmInput", 0, &v, CIM_UINT64);
    VariantClearSafe(v);
    return SUCCEEDED(hr);
}

}  // namespace

WmiSession::WmiSession() = default;

WmiSession::~WmiSession() {
    if (svc_) {
        svc_->Release();
        svc_ = nullptr;
    }
    if (com_inited_) {
        CoUninitialize();
        com_inited_ = false;
    }
}

void WmiSession::SetError(const std::string& e) {
    last_error_ = e;
    Log(e);
}

bool WmiSession::Connect(const wchar_t* ns) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE) {
        com_inited_ = (hr == S_OK);
    } else if (hr == RPC_E_CHANGED_MODE) {
        com_inited_ = false;
    } else {
        SetError("CoInitializeEx failed");
        return false;
    }

    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    IWbemLocator* loc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                          reinterpret_cast<void**>(&loc));
    if (FAILED(hr) || !loc) {
        SetError("CoCreateInstance WbemLocator failed");
        return false;
    }

    BSTR bns = SysAllocString(ns);
    hr = loc->ConnectServer(bns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc_);
    SysFreeString(bns);
    loc->Release();
    if (FAILED(hr) || !svc_) {
        SetError("ConnectServer ROOT\\WMI failed (admin required for AcerGamingFunction)");
        svc_ = nullptr;
        return false;
    }

    hr = CoSetProxyBlanket(svc_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        SetError("CoSetProxyBlanket failed");
        return false;
    }
    return true;
}

bool WmiSession::GetInstancePath(const wchar_t* class_name, std::wstring& path_out) {
    if (!svc_) {
        return false;
    }
    auto cached = path_cache_.find(class_name);
    if (cached != path_cache_.end()) {
        path_out = cached->second;
        return true;
    }

    auto grab_path = [&](IWbemClassObject* obj) -> bool {
        VARIANT v;
        VariantInit(&v);
        const HRESULT hr = obj->Get(L"__PATH", 0, &v, nullptr, nullptr);
        if (FAILED(hr) || v.vt != VT_BSTR || !v.bstrVal) {
            VariantClearSafe(v);
            return false;
        }
        path_out.assign(v.bstrVal, SysStringLen(v.bstrVal));
        VariantClearSafe(v);
        path_cache_[class_name] = path_out;
        return true;
    };

    IEnumWbemClassObject* en = nullptr;
    std::wstring query = L"SELECT * FROM ";
    query += class_name;
    BSTR wql = SysAllocString(L"WQL");
    BSTR q = SysAllocString(query.c_str());
    HRESULT hr = svc_->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY, nullptr, &en);
    SysFreeString(wql);
    SysFreeString(q);
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* obj = nullptr;
        ULONG got = 0;
        hr = en->Next(8000, 1, &obj, &got);
        en->Release();
        if (SUCCEEDED(hr) && got && obj) {
            const bool ok = grab_path(obj);
            obj->Release();
            if (ok) {
                return true;
            }
        }
    }

    BSTR bclass = SysAllocString(class_name);
    hr = svc_->CreateInstanceEnum(bclass, 0, nullptr, &en);
    SysFreeString(bclass);
    if (FAILED(hr) || !en) {
        SetError("CreateInstanceEnum failed");
        return false;
    }
    IWbemClassObject* obj = nullptr;
    ULONG got = 0;
    hr = en->Next(8000, 1, &obj, &got);
    en->Release();
    if (FAILED(hr) || got == 0 || !obj) {
        SetError("no WMI instance");
        return false;
    }
    const bool ok = grab_path(obj);
    obj->Release();
    if (!ok) {
        SetError("missing __PATH");
    }
    return ok;
}

bool WmiSession::ExecU32InU64Out(const wchar_t* class_name, const wchar_t* method, uint32_t in,
                                 uint64_t& out) {
    out = 0;
    if (!svc_) {
        return false;
    }
    std::wstring path;
    if (!GetInstancePath(class_name, path)) {
        return false;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(class_name);
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        SetError("GetObject class failed");
        return false;
    }
    IWbemClassObject* pInSig = nullptr;
    BSTR bmethod = SysAllocString(method);
    hr = pClass->GetMethod(bmethod, 0, &pInSig, nullptr);
    pClass->Release();
    if (FAILED(hr)) {
        SysFreeString(bmethod);
        SetError("GetMethod failed");
        return false;
    }
    IWbemClassObject* pIn = nullptr;
    if (pInSig) {
        pInSig->SpawnInstance(0, &pIn);
        pInSig->Release();
    }
    if (pIn) {
        PutU32(pIn, in);
    }
    IWbemClassObject* pOut = nullptr;
    BSTR bpath = SysAllocString(path.c_str());
    hr = svc_->ExecMethod(bpath, bmethod, 0, nullptr, pIn, &pOut, nullptr);
    SysFreeString(bpath);
    SysFreeString(bmethod);
    if (pIn) {
        pIn->Release();
    }
    if (FAILED(hr) || !pOut) {
        std::ostringstream oss;
        oss << "ExecMethod failed hr=0x" << std::hex << static_cast<unsigned>(hr);
        SetError(oss.str());
        return false;
    }
    VARIANT vo;
    VariantInit(&vo);
    hr = pOut->Get(L"gmOutput", 0, &vo, nullptr, nullptr);
    pOut->Release();
    if (FAILED(hr)) {
        VariantClearSafe(vo);
        SetError("gmOutput missing");
        return false;
    }
    if (vo.vt == VT_UI8) {
        out = vo.ullVal;
    } else if (vo.vt == VT_I8) {
        out = static_cast<uint64_t>(vo.llVal);
    } else if (vo.vt == VT_UI4) {
        out = vo.ulVal;
    } else if (vo.vt == VT_I4) {
        out = static_cast<uint32_t>(vo.lVal);
    } else {
        VariantChangeType(&vo, &vo, 0, VT_UI8);
        out = vo.ullVal;
    }
    VariantClearSafe(vo);
    return true;
}

bool WmiSession::ExecU64InU32Out(const wchar_t* class_name, const wchar_t* method, uint64_t in,
                                 uint32_t& out) {
    out = 0;
    if (!svc_) {
        return false;
    }
    std::wstring path;
    if (!GetInstancePath(class_name, path)) {
        return false;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(class_name);
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        SetError("GetObject class failed");
        return false;
    }
    IWbemClassObject* pInSig = nullptr;
    BSTR bmethod = SysAllocString(method);
    hr = pClass->GetMethod(bmethod, 0, &pInSig, nullptr);
    pClass->Release();
    if (FAILED(hr) || !pInSig) {
        SysFreeString(bmethod);
        SetError("GetMethod failed");
        return false;
    }
    IWbemClassObject* pIn = nullptr;
    pInSig->SpawnInstance(0, &pIn);
    pInSig->Release();
    if (!pIn || !PutU64(pIn, in)) {
        if (pIn) {
            pIn->Release();
        }
        SysFreeString(bmethod);
        SetError("Put gmInput uint64 failed");
        return false;
    }
    IWbemClassObject* pOut = nullptr;
    BSTR bpath = SysAllocString(path.c_str());
    hr = svc_->ExecMethod(bpath, bmethod, 0, nullptr, pIn, &pOut, nullptr);
    SysFreeString(bpath);
    SysFreeString(bmethod);
    pIn->Release();
    if (FAILED(hr) || !pOut) {
        std::ostringstream oss;
        oss << "ExecMethod set failed hr=0x" << std::hex << static_cast<unsigned>(hr);
        SetError(oss.str());
        return false;
    }
    VARIANT vo;
    VariantInit(&vo);
    pOut->Get(L"gmOutput", 0, &vo, nullptr, nullptr);
    pOut->Release();
    if (vo.vt == VT_UI4) {
        out = vo.ulVal;
    } else if (vo.vt == VT_I4) {
        out = static_cast<uint32_t>(vo.lVal);
    } else {
        VariantChangeType(&vo, &vo, 0, VT_UI4);
        out = vo.ulVal;
    }
    VariantClearSafe(vo);
    return true;
}

bool WmiSession::ExecBytesInU32Out(const wchar_t* class_name, const wchar_t* method,
                                   const std::vector<uint8_t>& in, uint32_t& out) {
    out = 0;
    if (!svc_) {
        return false;
    }
    std::wstring path;
    if (!GetInstancePath(class_name, path)) {
        return false;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(class_name);
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        return false;
    }
    IWbemClassObject* pInSig = nullptr;
    BSTR bmethod = SysAllocString(method);
    hr = pClass->GetMethod(bmethod, 0, &pInSig, nullptr);
    pClass->Release();
    if (FAILED(hr) || !pInSig) {
        SysFreeString(bmethod);
        return false;
    }
    IWbemClassObject* pIn = nullptr;
    pInSig->SpawnInstance(0, &pIn);
    pInSig->Release();

    SAFEARRAYBOUND bound{static_cast<ULONG>(in.size()), 0};
    SAFEARRAY* sa = SafeArrayCreate(VT_UI1, 1, &bound);
    void* data = nullptr;
    SafeArrayAccessData(sa, &data);
    memcpy(data, in.data(), in.size());
    SafeArrayUnaccessData(sa);
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_ARRAY | VT_UI1;
    v.parray = sa;
    pIn->Put(L"gmInput", 0, &v, 0);
    // Do not VariantClear: Put may take ownership depending on WMI; clone to be safe.
    // SafeArrayDestroy will happen via VariantClear after Put copies.
    VariantClear(&v);

    IWbemClassObject* pOut = nullptr;
    BSTR bpath = SysAllocString(path.c_str());
    hr = svc_->ExecMethod(bpath, bmethod, 0, nullptr, pIn, &pOut, nullptr);
    SysFreeString(bpath);
    SysFreeString(bmethod);
    pIn->Release();
    if (FAILED(hr) || !pOut) {
        return false;
    }
    VARIANT vo;
    VariantInit(&vo);
    pOut->Get(L"gmOutput", 0, &vo, nullptr, nullptr);
    pOut->Release();
    if (vo.vt == VT_UI4) {
        out = vo.ulVal;
    } else if (vo.vt == VT_I4) {
        out = static_cast<uint32_t>(vo.lVal);
    }
    VariantClearSafe(vo);
    return true;
}

bool WmiSession::ExecNotificationQuery(const wchar_t* wql, IEnumWbemClassObject** enumerator) {
    if (enumerator) {
        *enumerator = nullptr;
    }
    if (!svc_ || !enumerator) {
        return false;
    }
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(wql);
    HRESULT hr = svc_->ExecNotificationQuery(
        lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator);
    SysFreeString(lang);
    SysFreeString(q);
    if (FAILED(hr) || !*enumerator) {
        SetError("ExecNotificationQuery failed");
        return false;
    }
    return true;
}

bool WmiSession::ExecBatteryGetHealth(uint8_t battery_no, uint8_t query, uint8_t& function_list,
                                      std::vector<uint8_t>& function_status) {
    function_list = 0;
    function_status.clear();
    if (!svc_) {
        return false;
    }
    std::wstring path;
    if (!GetInstancePath(L"BatteryControl", path)) {
        return false;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(L"BatteryControl");
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        return false;
    }
    IWbemClassObject* pInSig = nullptr;
    BSTR bmethod = SysAllocString(L"GetBatteryHealthControlStatus");
    hr = pClass->GetMethod(bmethod, 0, &pInSig, nullptr);
    pClass->Release();
    if (FAILED(hr) || !pInSig) {
        SysFreeString(bmethod);
        return false;
    }
    IWbemClassObject* pIn = nullptr;
    pInSig->SpawnInstance(0, &pIn);
    pInSig->Release();
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI1;
    v.bVal = battery_no;
    pIn->Put(L"uBatteryNo", 0, &v, 0);
    v.bVal = query;
    pIn->Put(L"uFunctionQuery", 0, &v, 0);
    VariantClearSafe(v);

    SAFEARRAYBOUND bound{2, 0};
    SAFEARRAY* sa = SafeArrayCreate(VT_UI1, 1, &bound);
    VARIANT vr;
    VariantInit(&vr);
    vr.vt = VT_ARRAY | VT_UI1;
    vr.parray = sa;
    pIn->Put(L"uReserved", 0, &vr, 0);
    VariantClear(&vr);

    IWbemClassObject* pOut = nullptr;
    BSTR bpath = SysAllocString(path.c_str());
    hr = svc_->ExecMethod(bpath, bmethod, 0, nullptr, pIn, &pOut, nullptr);
    SysFreeString(bpath);
    SysFreeString(bmethod);
    pIn->Release();
    if (FAILED(hr) || !pOut) {
        return false;
    }
    VARIANT fl;
    VariantInit(&fl);
    if (SUCCEEDED(pOut->Get(L"uFunctionList", 0, &fl, nullptr, nullptr))) {
        if (fl.vt == VT_UI1) {
            function_list = fl.bVal;
        } else if (fl.vt == VT_I4) {
            function_list = static_cast<uint8_t>(fl.lVal);
        }
    }
    VariantClearSafe(fl);
    VARIANT st;
    VariantInit(&st);
    if (SUCCEEDED(pOut->Get(L"uFunctionStatus", 0, &st, nullptr, nullptr)) &&
        (st.vt & VT_ARRAY) && st.parray) {
        void* data = nullptr;
        LONG lbound = 0, ubound = 0;
        SafeArrayGetLBound(st.parray, 1, &lbound);
        SafeArrayGetUBound(st.parray, 1, &ubound);
        SafeArrayAccessData(st.parray, &data);
        const auto n = static_cast<size_t>(ubound - lbound + 1);
        function_status.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + n);
        SafeArrayUnaccessData(st.parray);
    }
    VariantClearSafe(st);
    pOut->Release();
    return true;
}

bool WmiSession::ExecBatterySetHealth(uint8_t battery_no, uint8_t mask, uint8_t status) {
    if (!svc_) {
        return false;
    }
    std::wstring path;
    if (!GetInstancePath(L"BatteryControl", path)) {
        return false;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(L"BatteryControl");
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        return false;
    }
    IWbemClassObject* pInSig = nullptr;
    BSTR bmethod = SysAllocString(L"SetBatteryHealthControl");
    hr = pClass->GetMethod(bmethod, 0, &pInSig, nullptr);
    pClass->Release();
    if (FAILED(hr) || !pInSig) {
        SysFreeString(bmethod);
        return false;
    }
    IWbemClassObject* pIn = nullptr;
    pInSig->SpawnInstance(0, &pIn);
    pInSig->Release();
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI1;
    v.bVal = battery_no;
    pIn->Put(L"uBatteryNo", 0, &v, 0);
    v.bVal = mask;
    pIn->Put(L"uFunctionMask", 0, &v, 0);
    v.bVal = status;
    pIn->Put(L"uFunctionStatus", 0, &v, 0);
    VariantClearSafe(v);

    SAFEARRAYBOUND bound{5, 0};
    SAFEARRAY* sa = SafeArrayCreate(VT_UI1, 1, &bound);
    VARIANT vr;
    VariantInit(&vr);
    vr.vt = VT_ARRAY | VT_UI1;
    vr.parray = sa;
    pIn->Put(L"uReservedIn", 0, &vr, 0);
    VariantClear(&vr);

    IWbemClassObject* pOut = nullptr;
    BSTR bpath = SysAllocString(path.c_str());
    hr = svc_->ExecMethod(bpath, bmethod, 0, nullptr, pIn, &pOut, nullptr);
    SysFreeString(bpath);
    SysFreeString(bmethod);
    pIn->Release();
    if (pOut) {
        pOut->Release();
    }
    return SUCCEEDED(hr);
}

std::vector<std::wstring> WmiSession::ListMethods(const wchar_t* class_name) {
    std::vector<std::wstring> names;
    if (!svc_) {
        return names;
    }
    IWbemClassObject* pClass = nullptr;
    BSTR bclass = SysAllocString(class_name);
    HRESULT hr = svc_->GetObject(bclass, 0, nullptr, &pClass, nullptr);
    SysFreeString(bclass);
    if (FAILED(hr) || !pClass) {
        return names;
    }
    IWbemClassObject* pQual = nullptr;
    // Enum methods via GetNames on class? Use BeginMethodEnumeration
    hr = pClass->BeginMethodEnumeration(0);
    if (SUCCEEDED(hr)) {
        BSTR name = nullptr;
        while (pClass->NextMethod(0, &name, nullptr, nullptr) == WBEM_S_NO_ERROR && name) {
            names.emplace_back(name, SysStringLen(name));
            SysFreeString(name);
            name = nullptr;
        }
        pClass->EndMethodEnumeration();
    }
    pClass->Release();
    return names;
}

std::vector<std::wstring> WmiSession::ListClasses() {
    std::vector<std::wstring> names;
    if (!svc_) {
        return names;
    }
    IEnumWbemClassObject* en = nullptr;
    HRESULT hr = svc_->CreateClassEnum(nullptr, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                       nullptr, &en);
    if (FAILED(hr) || !en) {
        return names;
    }
    IWbemClassObject* obj = nullptr;
    ULONG got = 0;
    while (en->Next(2000, 1, &obj, &got) == WBEM_S_NO_ERROR && got && obj) {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(obj->Get(L"__CLASS", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR &&
            v.bstrVal) {
            names.emplace_back(v.bstrVal);
        }
        VariantClearSafe(v);
        obj->Release();
        obj = nullptr;
    }
    en->Release();
    return names;
}

}  // namespace predator
