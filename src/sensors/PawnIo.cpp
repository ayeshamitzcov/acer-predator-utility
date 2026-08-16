#include "sensors/PawnIo.h"

#include "common/Log.h"

#include <windows.h>
#include <winsvc.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace predator {
namespace {

using pawnio_open_fn = HRESULT(__stdcall*)(HANDLE*);
using pawnio_load_fn = HRESULT(__stdcall*)(HANDLE, const unsigned char*, SIZE_T);
using pawnio_execute_fn = HRESULT(__stdcall*)(HANDLE, const char*, const unsigned long long*, SIZE_T,
                                              unsigned long long*, SIZE_T, SIZE_T*);
using pawnio_close_fn = HRESULT(__stdcall*)(HANDLE);

constexpr unsigned long long kMsrRaplUnit = 0x606;
constexpr unsigned long long kMsrPkgEnergy = 0x611;
constexpr unsigned long long kMsrPerfStatus = 0x198;
constexpr unsigned long long kMsrTempTarget = 0x1A2;
constexpr unsigned long long kMsrPkgTherm = 0x1B1;
constexpr unsigned long long kMsrCoreTherm = 0x19C;

HMODULE g_dll = nullptr;
pawnio_open_fn g_open = nullptr;
pawnio_load_fn g_load = nullptr;
pawnio_execute_fn g_exec = nullptr;
pawnio_close_fn g_close = nullptr;
HANDLE g_handle = nullptr;
bool g_tried = false;
bool g_ready = false;
std::string g_status = "PawnIO not initialized";

double g_energy_unit_j = 1.0 / (1 << 14);  // typical Intel default until we read 0x606
bool g_have_prev_energy = false;
uint32_t g_prev_energy = 0;
std::chrono::steady_clock::time_point g_prev_energy_time{};

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    const auto slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        p.resize(slash);
    }
    return p;
}

std::wstring FindFile(const wchar_t* name) {
    const std::wstring candidates[] = {
        ExeDir() + L"\\" + name,
        std::wstring(L"C:\\Program Files\\PawnIO\\") + name,
        AppDataDir() + L"\\modules\\" + name,
    };
    for (const auto& c : candidates) {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return c;
        }
    }
    return {};
}

bool LoadDll() {
    if (g_dll) {
        return true;
    }
    const auto next_to_exe = ExeDir() + L"\\PawnIOLib.dll";
    g_dll = LoadLibraryW(next_to_exe.c_str());
    if (!g_dll) {
        g_dll = LoadLibraryW(L"C:\\Program Files\\PawnIO\\PawnIOLib.dll");
    }
    if (!g_dll) {
        g_status = "PawnIOLib.dll not found (copy from C:\\Program Files\\PawnIO)";
        return false;
    }
    g_open = reinterpret_cast<pawnio_open_fn>(GetProcAddress(g_dll, "pawnio_open"));
    g_load = reinterpret_cast<pawnio_load_fn>(GetProcAddress(g_dll, "pawnio_load"));
    g_exec = reinterpret_cast<pawnio_execute_fn>(GetProcAddress(g_dll, "pawnio_execute"));
    g_close = reinterpret_cast<pawnio_close_fn>(GetProcAddress(g_dll, "pawnio_close"));
    if (!g_open || !g_load || !g_exec || !g_close) {
        g_status = "PawnIOLib.dll is missing required exports";
        return false;
    }
    return true;
}

std::vector<unsigned char> ReadBlob(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(f);
        return {};
    }
    std::vector<unsigned char> buf(static_cast<size_t>(sz.QuadPart));
    DWORD wr = 0;
    const BOOL ok = ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &wr, nullptr);
    CloseHandle(f);
    if (!ok || wr != buf.size()) {
        return {};
    }
    return buf;
}

bool EnsureReady() {
    if (g_tried) {
        return g_ready;
    }
    g_tried = true;
    if (!LoadDll()) {
        Log(g_status);
        return false;
    }
    HRESULT hr = g_open(&g_handle);
    if (FAILED(hr) || !g_handle) {
        g_status = "pawnio_open failed — run as Administrator and confirm the PawnIO driver is running";
        Log(g_status);
        return false;
    }
    auto blob_path = FindFile(L"IntelMSR.bin");
    if (blob_path.empty()) {
        g_status = "IntelMSR.bin missing next to the exe";
        Log(g_status);
        return false;
    }
    auto blob = ReadBlob(blob_path);
    if (blob.empty()) {
        g_status = "failed to read IntelMSR.bin";
        return false;
    }
    hr = g_load(g_handle, blob.data(), blob.size());
    if (FAILED(hr)) {
        g_status = "pawnio_load(IntelMSR.bin) failed (HRESULT " + std::to_string(static_cast<long>(hr)) + ")";
        Log(g_status);
        return false;
    }
    g_ready = true;
    g_status = "PawnIO IntelMSR ready (RAPL + DTS)";
    Log(g_status);
    return true;
}

std::optional<unsigned long long> ReadMsr(unsigned long long index) {
    if (!EnsureReady()) {
        return std::nullopt;
    }
    unsigned long long in = index;
    unsigned long long out = 0;
    SIZE_T ret = 0;
    const HRESULT hr = g_exec(g_handle, "ioctl_read_msr", &in, 1, &out, 1, &ret);
    if (FAILED(hr) || ret < 1) {
        return std::nullopt;
    }
    return out;
}

}  // namespace

bool PawnIo::DriverPresent() {
    if (GetFileAttributesW(L"C:\\Program Files\\PawnIO\\PawnIOLib.dll") != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return false;
    }
    SC_HANDLE svc = OpenServiceW(scm, L"PawnIO", SERVICE_QUERY_STATUS);
    const bool found = svc != nullptr;
    if (svc) {
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return found;
}

bool PawnIo::Ready() {
    return EnsureReady();
}

CpuTelemetry PawnIo::Sample() {
    CpuTelemetry t;
    if (!EnsureReady()) {
        return t;
    }

    if (auto unit = ReadMsr(kMsrRaplUnit)) {
        const unsigned esu = static_cast<unsigned>(((*unit) >> 8) & 0x1F);
        if (esu > 0 && esu < 31) {
            g_energy_unit_j = 1.0 / static_cast<double>(1u << esu);
        }
    }

    if (auto energy = ReadMsr(kMsrPkgEnergy)) {
        const uint32_t now_e = static_cast<uint32_t>(*energy);
        const auto now_t = std::chrono::steady_clock::now();
        if (g_have_prev_energy) {
            uint32_t delta_e = now_e - g_prev_energy;  // wraps mod 2^32
            const double dt = std::chrono::duration<double>(now_t - g_prev_energy_time).count();
            if (dt > 0.05 && dt < 10.0) {
                t.package_watts = static_cast<float>((delta_e * g_energy_unit_j) / dt);
            }
        }
        g_prev_energy = now_e;
        g_prev_energy_time = now_t;
        g_have_prev_energy = true;
    }

    if (auto perf = ReadMsr(kMsrPerfStatus)) {
        const unsigned vid = static_cast<unsigned>(((*perf) >> 32) & 0xFFFF);
        if (vid > 0) {
            t.voltage_v = vid / 8192.0f;
        }
    }

    auto target = ReadMsr(kMsrTempTarget);
    auto therm = ReadMsr(kMsrPkgTherm);
    if (!therm) {
        therm = ReadMsr(kMsrCoreTherm);
    }
    if (target && therm) {
        const int tjmax = static_cast<int>(((*target) >> 16) & 0xFF);
        const int readout = static_cast<int>(((*therm) >> 16) & 0x7F);
        if (tjmax >= 60 && tjmax <= 120) {
            t.package_temp_c = static_cast<float>(tjmax - readout);
        }
    }
    return t;
}

std::optional<float> PawnIo::TryCpuPackageWatts() {
    return Sample().package_watts;
}

std::string PawnIo::StatusText() {
    EnsureReady();
    return g_status;
}

}  // namespace predator
