#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "collectors/sensors.hpp"

class GpuSensor {
public:
    GpuSensor() = default;
    ~GpuSensor();

    GpuSensor(const GpuSensor&) = delete;
    GpuSensor& operator=(const GpuSensor&) = delete;

    void Initialize();
    void Poll(SystemSnapshot& out);

private:
    bool InitNvapi();

    HMODULE m_hNvapi = nullptr;
    bool m_nvapiReady = false;
    void* m_nvapiGpu = nullptr;
    void* m_pNvapiUnload = nullptr;
    void* m_pNvapiTherm = nullptr;
    void* m_pNvapiPstates = nullptr;
    void* m_pNvapiMemEx = nullptr;

    HMODULE m_hNvml = nullptr;
    bool m_nvmlReady = false;
    void* m_nvmlDev = nullptr;
    std::wstring m_gpuName;
    double m_fallbackVramTotalGb = 0.0;
    void* m_pNvmlShutdown = nullptr;
    void* m_pNvmlUtil = nullptr;
    void* m_pNvmlTemp = nullptr;
    void* m_pNvmlMem = nullptr;
    void* m_pNvmlMemV2 = nullptr;
};
