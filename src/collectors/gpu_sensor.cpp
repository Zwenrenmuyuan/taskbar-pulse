#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>

#include "collectors/gpu_sensor.hpp"
#include <algorithm>
#include <cstdint>

namespace {

constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

struct nvmlMemory_v2_t {
    unsigned int version;
    unsigned long long total;
    unsigned long long reserved;
    unsigned long long free;
    unsigned long long used;
};

constexpr unsigned int kNvmlMemoryV2Version = (2u << 24) | sizeof(nvmlMemory_v2_t);

struct NvThermalSettings {
    uint32_t version;
    uint32_t count;
    struct {
        int controller;
        int defaultMinTemp;
        int defaultMaxTemp;
        int currentTemp;
        int target;
    } sensor[3];
};

struct NvDynamicPstatesInfoEx {
    uint32_t version;
    uint32_t flags;
    struct {
        uint32_t bIsPresent : 1;
        uint32_t percentage;
    } utilization[8];
};

struct NvGpuMemoryInfoEx {
    uint32_t version;
    uint64_t dedicatedVideoMemory;
    uint64_t availableDedicatedVideoMemory;
    uint64_t systemVideoMemory;
    uint64_t sharedSystemMemory;
    uint64_t curAvailableDedicatedVideoMemory;
    uint64_t dedicatedVideoMemoryEvictionsSize;
    uint64_t dedicatedVideoMemoryEvictionCount;
    uint64_t dedicatedVideoMemoryPromotionsSize;
    uint64_t dedicatedVideoMemoryPromotionCount;
};

using PFN_NvAPI_QueryInterface = void* (*)(uint32_t);
using PFN_NvAPI_Init = int (*)(void);
using PFN_NvAPI_Unload = int (*)(void);
using PFN_NvAPI_EnumPhysicalGPUs = int (*)(void**, uint32_t*);
using PFN_NvAPI_GPU_GetFullName = int (*)(void*, char*);
using PFN_NvAPI_GPU_GetThermalSettings = int (*)(void*, uint32_t, NvThermalSettings*);
using PFN_NvAPI_GPU_GetDynamicPstatesInfoEx = int (*)(void*, NvDynamicPstatesInfoEx*);
using PFN_NvAPI_GPU_GetMemoryInfoEx = int (*)(void*, NvGpuMemoryInfoEx*);

using PFN_nvmlInit_v2 = int (*)(void);
using PFN_nvmlShutdown = int (*)(void);
using PFN_nvmlDeviceGetCount_v2 = int (*)(unsigned int*);
using PFN_nvmlDeviceGetHandleByIndex_v2 = int (*)(unsigned int, void**);
using PFN_nvmlDeviceGetName = int (*)(void*, char*, unsigned int);
using PFN_nvmlDeviceGetUtilizationRates = int (*)(void*, nvmlUtilization_t*);
using PFN_nvmlDeviceGetTemperature = int (*)(void*, int, unsigned int*);
using PFN_nvmlDeviceGetMemoryInfo = int (*)(void*, nvmlMemory_t*);
using PFN_nvmlDeviceGetMemoryInfo_v2 = int (*)(void*, nvmlMemory_v2_t*);

std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

} // namespace

GpuSensor::~GpuSensor() {
    if (m_nvapiReady && m_pNvapiUnload) {
        reinterpret_cast<PFN_NvAPI_Unload>(m_pNvapiUnload)();
        m_nvapiReady = false;
    }
    if (m_hNvapi) {
        FreeLibrary(m_hNvapi);
        m_hNvapi = nullptr;
    }
    if (m_nvmlReady && m_pNvmlShutdown) {
        reinterpret_cast<PFN_nvmlShutdown>(m_pNvmlShutdown)();
        m_nvmlReady = false;
    }
    if (m_hNvml) {
        FreeLibrary(m_hNvml);
        m_hNvml = nullptr;
    }
}

bool GpuSensor::InitNvapi() {
    m_hNvapi = LoadLibraryW(L"nvapi64.dll");
    if (!m_hNvapi) return false;

    auto q = reinterpret_cast<PFN_NvAPI_QueryInterface>(GetProcAddress(m_hNvapi, "nvapi_QueryInterface"));
    if (!q) {
        FreeLibrary(m_hNvapi);
        m_hNvapi = nullptr;
        return false;
    }

    auto pInit = reinterpret_cast<PFN_NvAPI_Init>(q(0x0150E828));
    m_pNvapiUnload = q(0xD22BDD7E);
    auto pEnum = reinterpret_cast<PFN_NvAPI_EnumPhysicalGPUs>(q(0xE5AC921F));
    auto pName = reinterpret_cast<PFN_NvAPI_GPU_GetFullName>(q(0xCEEE8E9F));
    m_pNvapiTherm = q(0xE3640A56);
    m_pNvapiPstates = q(0x60DED2ED);
    m_pNvapiMemEx = q(0xC0599498);

    auto pMemEx = reinterpret_cast<PFN_NvAPI_GPU_GetMemoryInfoEx>(m_pNvapiMemEx);
    if (!pInit || !pEnum || !pMemEx || pInit() != 0) {
        FreeLibrary(m_hNvapi);
        m_hNvapi = nullptr;
        return false;
    }

    void* gpus[64]{};
    uint32_t cnt = 0;
    if (pEnum(gpus, &cnt) == 0 && cnt > 0) {
        uint64_t bestMem = 0;
        for (uint32_t i = 0; i < cnt; ++i) {
            NvGpuMemoryInfoEx mx{};
            mx.version = (1u << 16) | sizeof(mx);
            uint64_t ded = 0;
            if (pMemEx(gpus[i], &mx) == 0) ded = mx.dedicatedVideoMemory;
            if (i == 0 || ded > bestMem) {
                bestMem = ded;
                m_nvapiGpu = gpus[i];
            }
        }
        if (m_nvapiGpu) {
            m_nvapiReady = true;
            if (pName) {
                char name[64]{};
                if (pName(m_nvapiGpu, name) == 0) m_gpuName = Utf8ToWide(name);
            }
            return true;
        }
    }

    if (m_pNvapiUnload) reinterpret_cast<PFN_NvAPI_Unload>(m_pNvapiUnload)();
    FreeLibrary(m_hNvapi);
    m_hNvapi = nullptr;
    return false;
}

void GpuSensor::Initialize() {
    if (InitNvapi()) return;

    m_hNvml = LoadLibraryW(L"nvml.dll");
    if (!m_hNvml) {
        wchar_t progFiles[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"ProgramFiles", progFiles, MAX_PATH) > 0) {
            std::wstring path = std::wstring(progFiles) + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
            m_hNvml = LoadLibraryW(path.c_str());
        }
    }

    if (m_hNvml) {
        auto pInit = reinterpret_cast<PFN_nvmlInit_v2>(GetProcAddress(m_hNvml, "nvmlInit_v2"));
        m_pNvmlShutdown = reinterpret_cast<void*>(GetProcAddress(m_hNvml, "nvmlShutdown"));
        auto pCount = reinterpret_cast<PFN_nvmlDeviceGetCount_v2>(GetProcAddress(m_hNvml, "nvmlDeviceGetCount_v2"));
        auto pHandle = reinterpret_cast<PFN_nvmlDeviceGetHandleByIndex_v2>(GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex_v2"));
        auto pName = reinterpret_cast<PFN_nvmlDeviceGetName>(GetProcAddress(m_hNvml, "nvmlDeviceGetName"));
        m_pNvmlUtil = reinterpret_cast<void*>(GetProcAddress(m_hNvml, "nvmlDeviceGetUtilizationRates"));
        m_pNvmlTemp = reinterpret_cast<void*>(GetProcAddress(m_hNvml, "nvmlDeviceGetTemperature"));
        m_pNvmlMem = reinterpret_cast<void*>(GetProcAddress(m_hNvml, "nvmlDeviceGetMemoryInfo"));
        m_pNvmlMemV2 = reinterpret_cast<void*>(GetProcAddress(m_hNvml, "nvmlDeviceGetMemoryInfo_v2"));

        auto pMem = reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(m_pNvmlMem);
        if (pInit && pCount && pHandle && pInit() == 0) {
            unsigned int count = 0;
            if (pCount(&count) == 0 && count > 0) {
                unsigned long long bestMem = 0;
                for (unsigned int i = 0; i < count; ++i) {
                    void* cand = nullptr;
                    if (pHandle(i, &cand) == 0 && cand) {
                        unsigned long long totalMem = 0;
                        if (pMem) {
                            nvmlMemory_t m{};
                            if (pMem(cand, &m) == 0) totalMem = m.total;
                        }
                        if (i == 0 || totalMem > bestMem) {
                            bestMem = totalMem;
                            m_nvmlDev = cand;
                        }
                    }
                }
                if (m_nvmlDev) {
                    m_nvmlReady = true;
                    if (pName) {
                        char buf[128]{};
                        if (pName(m_nvmlDev, buf, sizeof(buf)) == 0) {
                            m_gpuName = Utf8ToWide(buf);
                        }
                    }
                }
            }
        }
    }

    if (m_gpuName.empty()) {
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
            IDXGIAdapter1* ad = nullptr;
            SIZE_T bestDed = 0;
            for (UINT i = 0; factory->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(ad->GetDesc1(&desc))) {
                    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && desc.DedicatedVideoMemory > bestDed) {
                        bestDed = desc.DedicatedVideoMemory;
                        m_gpuName = desc.Description;
                        m_fallbackVramTotalGb = static_cast<double>(desc.DedicatedVideoMemory) / kBytesPerGiB;
                    }
                }
                ad->Release();
            }
            factory->Release();
        }
    }
}

void GpuSensor::Poll(SystemSnapshot& out) {
    out.gpuName = m_gpuName;
    if (m_nvapiReady && m_nvapiGpu) {
        if (m_pNvapiPstates) {
            NvDynamicPstatesInfoEx ps{};
            ps.version = (1u << 16) | sizeof(ps);
            if (reinterpret_cast<PFN_NvAPI_GPU_GetDynamicPstatesInfoEx>(m_pNvapiPstates)(m_nvapiGpu, &ps) == 0 &&
                ps.utilization[0].bIsPresent) {
                out.gpuPercent = std::clamp(static_cast<double>(ps.utilization[0].percentage), 0.0, 100.0);
                out.hasGpuUsage = true;
            }
        }
        if (m_pNvapiTherm) {
            NvThermalSettings ts{};
            ts.version = (2u << 16) | sizeof(ts);
            if (reinterpret_cast<PFN_NvAPI_GPU_GetThermalSettings>(m_pNvapiTherm)(m_nvapiGpu, 0, &ts) == 0 &&
                ts.count > 0) {
                out.gpuTempC = static_cast<double>(ts.sensor[0].currentTemp);
                out.hasGpuTemp = true;
            }
        }
        if (m_pNvapiMemEx) {
            NvGpuMemoryInfoEx mx{};
            mx.version = (1u << 16) | sizeof(mx);
            if (reinterpret_cast<PFN_NvAPI_GPU_GetMemoryInfoEx>(m_pNvapiMemEx)(m_nvapiGpu, &mx) == 0 &&
                mx.dedicatedVideoMemory > 0) {
                uint64_t avail = std::min(mx.curAvailableDedicatedVideoMemory, mx.dedicatedVideoMemory);
                uint64_t used = mx.dedicatedVideoMemory - avail;
                out.vramUsedGb = static_cast<double>(used) / kBytesPerGiB;
                out.vramTotalGb = static_cast<double>(mx.dedicatedVideoMemory) / kBytesPerGiB;
                out.vramPercent = std::clamp((static_cast<double>(used) * 100.0) / static_cast<double>(mx.dedicatedVideoMemory), 0.0, 100.0);
                out.hasVram = true;
            }
        }
        return;
    }

    if (!m_nvmlReady || !m_nvmlDev) {
        out.vramTotalGb = m_fallbackVramTotalGb;
        return;
    }

    if (m_pNvmlUtil) {
        nvmlUtilization_t util{};
        if (reinterpret_cast<PFN_nvmlDeviceGetUtilizationRates>(m_pNvmlUtil)(m_nvmlDev, &util) == 0) {
            out.gpuPercent = std::clamp(static_cast<double>(util.gpu), 0.0, 100.0);
            out.hasGpuUsage = true;
        }
    }
    if (m_pNvmlTemp) {
        unsigned int temp = 0;
        if (reinterpret_cast<PFN_nvmlDeviceGetTemperature>(m_pNvmlTemp)(m_nvmlDev, 0, &temp) == 0) {
            out.gpuTempC = static_cast<double>(temp);
            out.hasGpuTemp = true;
        }
    }

    bool memOk = false;
    if (m_pNvmlMemV2) {
        nvmlMemory_v2_t mem{};
        mem.version = kNvmlMemoryV2Version;
        if (reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo_v2>(m_pNvmlMemV2)(m_nvmlDev, &mem) == 0 && mem.total > 0) {
            out.vramUsedGb = static_cast<double>(mem.used) / kBytesPerGiB;
            out.vramTotalGb = static_cast<double>(mem.total) / kBytesPerGiB;
            out.vramPercent = std::clamp((static_cast<double>(mem.used) * 100.0) / static_cast<double>(mem.total), 0.0, 100.0);
            out.hasVram = true;
            memOk = true;
        }
    }
    if (!memOk && m_pNvmlMem) {
        nvmlMemory_t mem{};
        if (reinterpret_cast<PFN_nvmlDeviceGetMemoryInfo>(m_pNvmlMem)(m_nvmlDev, &mem) == 0 && mem.total > 0) {
            out.vramUsedGb = static_cast<double>(mem.used) / kBytesPerGiB;
            out.vramTotalGb = static_cast<double>(mem.total) / kBytesPerGiB;
            out.vramPercent = std::clamp((static_cast<double>(mem.used) * 100.0) / static_cast<double>(mem.total), 0.0, 100.0);
            out.hasVram = true;
        }
    }
}
