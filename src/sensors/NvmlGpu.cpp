#include "sensors/NvmlGpu.h"

#include <windows.h>

namespace predator {
namespace {

using nvmlReturn_t = int;
struct nvmlDevice_st;
using nvmlDevice_t = nvmlDevice_st*;

struct nvmlUtilization_t {
    unsigned gpu;
    unsigned memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

using nvmlInit_t = nvmlReturn_t (*)();
using nvmlShutdown_t = nvmlReturn_t (*)();
using nvmlDeviceGetHandleByIndex_t = nvmlReturn_t (*)(unsigned, nvmlDevice_t*);
using nvmlDeviceGetName_t = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned);
using nvmlDeviceGetTemperature_t = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned*);
using nvmlDeviceGetUtilizationRates_t = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using nvmlDeviceGetPowerUsage_t = nvmlReturn_t (*)(nvmlDevice_t, unsigned*);
using nvmlDeviceGetClockInfo_t = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned*);
using nvmlDeviceGetMemoryInfo_t = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);

HMODULE g_dll = nullptr;
nvmlInit_t pInit = nullptr;
nvmlShutdown_t pShutdown = nullptr;
nvmlDeviceGetHandleByIndex_t pHandle = nullptr;
nvmlDeviceGetName_t pName = nullptr;
nvmlDeviceGetTemperature_t pTemp = nullptr;
nvmlDeviceGetUtilizationRates_t pUtil = nullptr;
nvmlDeviceGetPowerUsage_t pPower = nullptr;
nvmlDeviceGetClockInfo_t pClock = nullptr;
nvmlDeviceGetMemoryInfo_t pMem = nullptr;
bool g_inited = false;

bool LoadNvml() {
    if (g_dll) {
        return true;
    }
    g_dll = LoadLibraryW(L"nvml.dll");
    if (!g_dll) {
        g_dll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }
    if (!g_dll) {
        return false;
    }
    pInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(g_dll, "nvmlInit_v2"));
    if (!pInit) {
        pInit = reinterpret_cast<nvmlInit_t>(GetProcAddress(g_dll, "nvmlInit"));
    }
    pShutdown = reinterpret_cast<nvmlShutdown_t>(GetProcAddress(g_dll, "nvmlShutdown"));
    pHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(
        GetProcAddress(g_dll, "nvmlDeviceGetHandleByIndex_v2"));
    if (!pHandle) {
        pHandle = reinterpret_cast<nvmlDeviceGetHandleByIndex_t>(
            GetProcAddress(g_dll, "nvmlDeviceGetHandleByIndex"));
    }
    pName = reinterpret_cast<nvmlDeviceGetName_t>(GetProcAddress(g_dll, "nvmlDeviceGetName"));
    pTemp = reinterpret_cast<nvmlDeviceGetTemperature_t>(
        GetProcAddress(g_dll, "nvmlDeviceGetTemperature"));
    pUtil = reinterpret_cast<nvmlDeviceGetUtilizationRates_t>(
        GetProcAddress(g_dll, "nvmlDeviceGetUtilizationRates"));
    pPower = reinterpret_cast<nvmlDeviceGetPowerUsage_t>(GetProcAddress(g_dll, "nvmlDeviceGetPowerUsage"));
    pClock = reinterpret_cast<nvmlDeviceGetClockInfo_t>(GetProcAddress(g_dll, "nvmlDeviceGetClockInfo"));
    pMem = reinterpret_cast<nvmlDeviceGetMemoryInfo_t>(GetProcAddress(g_dll, "nvmlDeviceGetMemoryInfo"));
    return pInit && pHandle;
}

}  // namespace

NvmlGpu::NvmlGpu() {
    if (!LoadNvml()) {
        return;
    }
    if (!g_inited && pInit && pInit() == 0) {
        g_inited = true;
    }
    ok_ = g_inited;
}

NvmlGpu::~NvmlGpu() = default;

GpuSample NvmlGpu::Sample() {
    GpuSample s;
    if (!ok_ || !pHandle) {
        return s;
    }
    nvmlDevice_t dev = nullptr;
    if (pHandle(0, &dev) != 0 || !dev) {
        return s;
    }
    s.present = true;
    char name[96]{};
    if (pName) {
        pName(dev, name, sizeof(name));
        s.name = name;
    }
    unsigned t = 0;
    if (pTemp && pTemp(dev, 0 /*NVML_TEMPERATURE_GPU*/, &t) == 0) {
        s.temp_c = static_cast<float>(t);
    }
    nvmlUtilization_t u{};
    if (pUtil && pUtil(dev, &u) == 0) {
        s.util_percent = static_cast<float>(u.gpu);
    }
    unsigned mw = 0;
    if (pPower && pPower(dev, &mw) == 0) {
        s.power_w = mw / 1000.0f;
    }
    unsigned clk = 0;
    if (pClock && pClock(dev, 0 /*graphics*/, &clk) == 0) {
        s.core_clock_mhz = static_cast<float>(clk);
    }
    if (pClock && pClock(dev, 2 /*mem*/, &clk) == 0) {
        s.mem_clock_mhz = static_cast<float>(clk);
    }
    nvmlMemory_t mem{};
    if (pMem && pMem(dev, &mem) == 0) {
        s.mem_used_mb = static_cast<float>(mem.used / (1024ull * 1024ull));
        s.mem_total_mb = static_cast<float>(mem.total / (1024ull * 1024ull));
    }
    return s;
}

}  // namespace predator
