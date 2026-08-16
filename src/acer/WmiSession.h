#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct IWbemServices;
struct IEnumWbemClassObject;

namespace predator {

class WmiSession {
public:
    WmiSession();
    ~WmiSession();

    WmiSession(const WmiSession&) = delete;
    WmiSession& operator=(const WmiSession&) = delete;

    bool Connect(const wchar_t* ns = L"ROOT\\WMI");
    bool Ok() const { return svc_ != nullptr; }
    std::string LastError() const { return last_error_; }

    bool GetInstancePath(const wchar_t* class_name, std::wstring& path_out);
    bool ExecU32InU64Out(const wchar_t* class_name, const wchar_t* method, uint32_t in,
                         uint64_t& out);
    bool ExecU64InU32Out(const wchar_t* class_name, const wchar_t* method, uint64_t in,
                         uint32_t& out);
    bool ExecBytesInU32Out(const wchar_t* class_name, const wchar_t* method,
                           const std::vector<uint8_t>& in, uint32_t& out);
    bool ExecNotificationQuery(const wchar_t* wql, IEnumWbemClassObject** enumerator);
    bool ExecBatteryGetHealth(uint8_t battery_no, uint8_t query, uint8_t& function_list,
                              std::vector<uint8_t>& function_status);
    bool ExecBatterySetHealth(uint8_t battery_no, uint8_t mask, uint8_t status);

    std::vector<std::wstring> ListMethods(const wchar_t* class_name);
    std::vector<std::wstring> ListClasses();

private:
    void SetError(const std::string& e);
    IWbemServices* svc_ = nullptr;
    bool com_inited_ = false;
    std::string last_error_;
    std::map<std::wstring, std::wstring> path_cache_;
};

}  // namespace predator
