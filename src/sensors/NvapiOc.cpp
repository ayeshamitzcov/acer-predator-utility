#include "sensors/NvapiOc.h"

#include "common/Log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace predator {
namespace {

using NvStatus = int;
using NvU32 = unsigned int;
using NvS32 = int;
using NvPhysicalGpuHandle = void*;

constexpr NvStatus NVAPI_OK = 0;
constexpr NvU32 NVAPI_MAX_PHYSICAL_GPUS = 64;
constexpr NvU32 NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS = 0;
constexpr NvU32 NVAPI_GPU_PUBLIC_CLOCK_MEMORY = 4;

#define NV_MAKE_VERSION(s, v) (static_cast<NvU32>(sizeof(s)) | (static_cast<NvU32>(v) << 16))

struct NvDelta {
    NvS32 value;
    struct {
        NvS32 mindelta;
        NvS32 maxdelta;
    } valueRange;
};

struct NvClockEntry {
    NvU32 domainId;
    NvU32 typeId;
    NvU32 bIsEditable : 1;
    NvU32 reserved : 31;
    NvDelta freqDelta_kHz;
    union {
        struct {
            NvU32 freq_kHz;
        } single;
        struct {
            NvU32 minFreq_kHz;
            NvU32 maxFreq_kHz;
            NvU32 domainId;
            NvU32 minVoltage_uV;
            NvU32 maxVoltage_uV;
        } range;
    } data;
};

struct NvBaseVoltage {
    NvU32 domainId;
    NvU32 bIsEditable : 1;
    NvU32 reserved : 31;
    NvU32 volt_uV;
    NvDelta voltDelta_uV;
};

struct NvPstates20 {
    NvU32 version;
    NvU32 bIsEditable : 1;
    NvU32 reserved : 31;
    NvU32 numPstates;
    NvU32 numClocks;
    NvU32 numBaseVoltages;
    struct {
        NvU32 pstateId;
        NvU32 bIsEditable : 1;
        NvU32 reserved : 31;
        NvClockEntry clocks[8];
        NvBaseVoltage baseVoltages[4];
    } pstates[16];
};

using QueryInterface_t = void* (*)(unsigned int);
using Initialize_t = NvStatus (*)();
using EnumGpus_t = NvStatus (*)(NvPhysicalGpuHandle*, NvU32*);
using SetPstates_t = NvStatus (*)(NvPhysicalGpuHandle, NvPstates20*);
using GetPstates_t = NvStatus (*)(NvPhysicalGpuHandle, NvPstates20*);

QueryInterface_t pQuery = nullptr;
Initialize_t pInit = nullptr;
EnumGpus_t pEnum = nullptr;
SetPstates_t pSet = nullptr;
GetPstates_t pGet = nullptr;
HMODULE g_nvapi = nullptr;

}  // namespace

bool NvapiOc::Init() {
    if (ok_) {
        return true;
    }
    g_nvapi = LoadLibraryW(L"nvapi64.dll");
    if (!g_nvapi) {
        err_ = "nvapi64.dll not found";
        return false;
    }
    pQuery = reinterpret_cast<QueryInterface_t>(GetProcAddress(g_nvapi, "nvapi_QueryInterface"));
    if (!pQuery) {
        err_ = "nvapi_QueryInterface missing";
        return false;
    }
    pInit = reinterpret_cast<Initialize_t>(pQuery(0x0150E828));
    pEnum = reinterpret_cast<EnumGpus_t>(pQuery(0xE5AC921F));
    pSet = reinterpret_cast<SetPstates_t>(pQuery(0x0F4DAE6B));
    pGet = reinterpret_cast<GetPstates_t>(pQuery(0x6FF81213));
    if (!pInit || !pEnum || !pSet || !pGet) {
        err_ = "NVAPI function IDs not resolved";
        return false;
    }
    if (pInit() != NVAPI_OK) {
        err_ = "NvAPI_Initialize failed";
        return false;
    }
    ok_ = true;
    Log("NVAPI initialized");
    return true;
}

bool NvapiOc::SetOffsetsMhz(int core, int memory) {
    if (!Init()) {
        return false;
    }
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32 count = 0;
    if (pEnum(gpus, &count) != NVAPI_OK || count == 0) {
        err_ = "NvAPI_EnumPhysicalGPUs failed";
        return false;
    }

    auto apply = [&](int domain, int mhz) -> bool {
        NvPstates20 info{};
        info.version = NV_MAKE_VERSION(NvPstates20, 1);
        info.numPstates = 1;
        info.numClocks = 1;
        info.pstates[0].pstateId = 0;
        info.pstates[0].clocks[0].domainId = static_cast<NvU32>(domain);
        info.pstates[0].clocks[0].freqDelta_kHz.value = mhz * 1000;
        const NvStatus st = pSet(gpus[0], &info);
        return st == NVAPI_OK;
    };

    const bool c = apply(static_cast<int>(NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS), core);
    const bool m = apply(static_cast<int>(NVAPI_GPU_PUBLIC_CLOCK_MEMORY), memory);
    if (!c || !m) {
        err_ = "NvAPI_GPU_SetPstates20 rejected offset (laptop GPU may lock this)";
        Log(err_);
        return false;
    }
    Log("NVAPI GPU offsets applied");
    return true;
}

bool NvapiOc::GetOffsetsMhz(int& core, int& memory) {
    core = 0;
    memory = 0;
    if (!Init()) {
        return false;
    }
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32 count = 0;
    if (pEnum(gpus, &count) != NVAPI_OK || count == 0) {
        err_ = "NvAPI_EnumPhysicalGPUs failed";
        return false;
    }
    NvPstates20 info{};
    info.version = NV_MAKE_VERSION(NvPstates20, 2);
    NvStatus st = pGet(gpus[0], &info);
    if (st != NVAPI_OK) {
        info = {};
        info.version = NV_MAKE_VERSION(NvPstates20, 1);
        st = pGet(gpus[0], &info);
    }
    if (st != NVAPI_OK) {
        err_ = "NvAPI_GPU_GetPstates20 failed";
        return false;
    }
    const NvU32 nclocks = info.numClocks > 8 ? 8 : info.numClocks;
    for (NvU32 i = 0; i < nclocks; ++i) {
        const auto& c = info.pstates[0].clocks[i];
        const int mhz = c.freqDelta_kHz.value / 1000;
        if (c.domainId == NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS) {
            core = mhz;
        } else if (c.domainId == NVAPI_GPU_PUBLIC_CLOCK_MEMORY) {
            memory = mhz;
        }
    }
    return true;
}

}  // namespace predator
