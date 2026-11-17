#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include "collectors/sensors.hpp"

class NetRamSensor {
public:
    NetRamSensor();
    ~NetRamSensor() = default;

    NetRamSensor(const NetRamSensor&) = delete;
    NetRamSensor& operator=(const NetRamSensor&) = delete;

    void Initialize();
    void PollRam(SystemSnapshot& out);
    void PollNet(SystemSnapshot& out);

private:
    bool m_hasPrevNet = false;
    uint64_t m_prevInOctets = 0;
    uint64_t m_prevOutOctets = 0;
    uint64_t m_sessionInBytes = 0;
    uint64_t m_sessionOutBytes = 0;
    LARGE_INTEGER m_prevNetQpc{};
    LARGE_INTEGER m_qpcFreq{};
    std::wstring m_lastNicName;
};
