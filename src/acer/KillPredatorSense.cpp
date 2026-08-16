#include "acer/KillPredatorSense.h"

#include "common/Log.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cwctype>
#include <string>

namespace predator {
namespace {

void DisableService(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return;
    }
    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    char buf[128]{};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, sizeof(buf) - 1, nullptr, nullptr);
    Log(std::string("disabled service ") + buf);
}

void DisableTask(const wchar_t* name, void (*pump)()) {
    std::wstring cmd = L"schtasks.exe /Change /TN \"";
    cmd += name;
    cmd += L"\" /DISABLE";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pi)) {
        const DWORD start = GetTickCount();
        while (WaitForSingleObject(pi.hProcess, 80) == WAIT_TIMEOUT) {
            if (pump) {
                pump();
            }
            if (GetTickCount() - start > 4000) {
                break;
            }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void KillMatchingProcesses() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const DWORD self = GetCurrentProcessId();
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == self) {
                continue;
            }
            std::wstring name = pe.szExeFile;
            for (auto& c : name) {
                c = static_cast<wchar_t>(towlower(c));
            }
            const bool hit = name.find(L"predatorsense") != std::wstring::npos ||
                             name == L"pssvc.exe" || name == L"pslauncher.exe" ||
                             name == L"psagent.exe" || name == L"psadminagent.exe" ||
                             name == L"psmobile.exe";
            if (!hit) {
                continue;
            }
            HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (proc) {
                TerminateProcess(proc, 1);
                CloseHandle(proc);
                Log("killed PredatorSense process");
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

}  // namespace

void KillPredatorSense(void (*pump)()) {
    const wchar_t* services[] = {L"PSSvc", L"PredatorService", L"AcerCCAgentSvis", L"AcerDIAgentSvis",
                                 L"AcerDeviceEnablingServiceV2"};
    for (const wchar_t* s : services) {
        DisableService(s);
        if (pump) {
            pump();
        }
    }
    DisableTask(L"\\PredatorSense", pump);
    DisableTask(L"\\PredatorSense UI", pump);
    KillMatchingProcesses();
}

}  // namespace predator
