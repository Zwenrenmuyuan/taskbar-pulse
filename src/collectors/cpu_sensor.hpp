#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <cstdint>
#include "collectors/sensors.hpp"

class CpuSensor {
public:
    CpuSensor() = default;
    ~CpuSensor();

    CpuSensor(const CpuSensor&) = delete;
    CpuSensor& operator=(const CpuSensor&) = delete;

    void Initialize();
    void PollUsageAndFreq(SystemSnapshot& out);
    void PollTemp(SystemSnapshot& out);

private:
    void InitFreq();
    void InitTemp();

    bool m_hasPrevCpu = false;
    uint64_t m_prevIdle = 0;
    uint64_t m_prevKernel = 0;
    uint64_t m_prevUser = 0;
    double m_lastCpuPct = 0.0;

    double m_baseMhz = 0.0;
    PDH_HQUERY m_pdhQuery = nullptr;
    PDH_HCOUNTER m_pdhPerfCounter = nullptr;
    bool m_pdhPrimed = false;

    HANDLE m_hPawnIo = INVALID_HANDLE_VALUE;
    HANDLE m_hPciMutex = nullptr;
};
