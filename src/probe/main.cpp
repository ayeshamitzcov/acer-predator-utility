#include "common/Log.h"
#include "probe/Probe.h"

#include <windows.h>

#include <iostream>
#include <string>

int main() {
    predator::LogInit();
    const std::string report = predator::RunHardwareProbe();
    std::cout << report;
    const auto path = predator::AppDataDir() + L"\\probe-report.txt";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(f, report.data(), static_cast<DWORD>(report.size()), &wr, nullptr);
        CloseHandle(f);
        std::wcout << L"\nWrote " << path << L"\n";
    }
    return 0;
}
