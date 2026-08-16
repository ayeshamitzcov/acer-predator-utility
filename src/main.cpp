#include "probe/Probe.h"
#include "ui/App.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

namespace {

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elev{};
    DWORD n = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &n);
    CloseHandle(token);
    return ok && elev.TokenIsElevated != 0;
}

bool RelaunchElevated(PWSTR cmdline) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = cmdline;
        sei.nShow = (cmdline && wcsstr(cmdline, L"--minimized")) ? SW_HIDE : SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) == TRUE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdline, int) {
    if (!IsElevated()) {
        RelaunchElevated(cmdline);
        return 0;
    }
    if (cmdline && wcsstr(cmdline, L"--probe")) {
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        const std::string report = predator::RunHardwareProbe();
        fwrite(report.data(), 1, report.size(), stdout);
        printf("\nPress Enter to close...\n");
        getchar();
        return 0;
    }
    const bool start_min = cmdline && wcsstr(cmdline, L"--minimized");
    return predator::ui::RunApp(start_min);
}
