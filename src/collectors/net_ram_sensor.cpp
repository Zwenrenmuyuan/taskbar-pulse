#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "collectors/net_ram_sensor.hpp"
#include <algorithm>

namespace {
constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
} // namespace

NetRamSensor::NetRamSensor() {
    QueryPerformanceFrequency(&m_qpcFreq);
}

void NetRamSensor::Initialize() {
    SystemSnapshot dummy{};
    PollNet(dummy);
}

void NetRamSensor::PollRam(SystemSnapshot& out) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        uint64_t used = ms.ullTotalPhys - ms.ullAvailPhys;
        out.ramTotalGb = static_cast<double>(ms.ullTotalPhys) / kBytesPerGiB;
        out.ramUsedGb = static_cast<double>(used) / kBytesPerGiB;
        out.ramPercent = ms.ullTotalPhys > 0
            ? std::clamp((static_cast<double>(used) * 100.0) / static_cast<double>(ms.ullTotalPhys), 0.0, 100.0)
            : static_cast<double>(ms.dwMemoryLoad);
    }
}

void NetRamSensor::PollNet(SystemSnapshot& out) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    PMIB_IF_TABLE2 ifTable = nullptr;
    if (GetIfTable2(&ifTable) != NO_ERROR || !ifTable) {
        out.nicName = m_lastNicName;
        out.netSessionDownBytes = m_sessionInBytes;
        out.netSessionUpBytes = m_sessionOutBytes;
        return;
    }

    uint64_t totalIn = 0;
    uint64_t totalOut = 0;
    uint64_t bestTraffic = 0;
    std::wstring bestNic;

    for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
        const MIB_IF_ROW2& row = ifTable->Table[i];
        if (row.OperStatus == IfOperStatusUp &&
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            row.Type != IF_TYPE_TUNNEL &&
            row.InterfaceAndOperStatusFlags.HardwareInterface &&
            !row.InterfaceAndOperStatusFlags.FilterInterface) {
            totalIn += row.InOctets;
            totalOut += row.OutOctets;
            uint64_t sum = row.InOctets + row.OutOctets;
            if (bestNic.empty() || sum > bestTraffic) {
                bestTraffic = sum;
                bestNic = row.Description;
            }
        }
    }
    FreeMibTable(ifTable);

    if (!bestNic.empty()) m_lastNicName = bestNic;
    out.nicName = m_lastNicName;

    if (m_hasPrevNet && m_qpcFreq.QuadPart > 0) {
        double dt = static_cast<double>(now.QuadPart - m_prevNetQpc.QuadPart) / static_cast<double>(m_qpcFreq.QuadPart);
        if (dt > 0.05 && totalIn >= m_prevInOctets && totalOut >= m_prevOutOctets) {
            uint64_t dIn = totalIn - m_prevInOctets;
            uint64_t dOut = totalOut - m_prevOutOctets;
            out.netDownBps = static_cast<double>(dIn) / dt;
            out.netUpBps = static_cast<double>(dOut) / dt;
            m_sessionInBytes += dIn;
            m_sessionOutBytes += dOut;
        }
    }
    m_prevInOctets = totalIn;
    m_prevOutOctets = totalOut;
    m_prevNetQpc = now;
    m_hasPrevNet = true;

    out.netSessionDownBytes = m_sessionInBytes;
    out.netSessionUpBytes = m_sessionOutBytes;
}
