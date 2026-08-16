#include "common/Log.h"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>

namespace predator {
namespace {

std::mutex g_logMu;
std::wofstream g_log;
bool g_inited = false;

std::wstring NowStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    return buf;
}

}  // namespace

std::wstring AppDataDir() {
    wchar_t path[MAX_PATH]{};
    ExpandEnvironmentStringsW(L"%APPDATA%\\PredatorLite", path, MAX_PATH);
    CreateDirectoryW(path, nullptr);
    return path;
}

void LogInit() {
    std::lock_guard lock(g_logMu);
    if (g_inited) {
        return;
    }
    const auto file = AppDataDir() + L"\\predatorlite.log";
    g_log.open(file, std::ios::app);
    g_inited = true;
}

void Log(std::string_view message) {
    LogInit();
    std::lock_guard lock(g_logMu);
    if (!g_log) {
        return;
    }
    g_log << NowStamp() << L"  ";
    for (unsigned char c : message) {
        g_log << static_cast<wchar_t>(c);
    }
    g_log << L"\n";
    g_log.flush();
}

}  // namespace predator
