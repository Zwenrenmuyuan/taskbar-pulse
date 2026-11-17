#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>
#include <pdh.h>

#include "collectors/cpu_sensor.hpp"
#include "resource.h"
#include <algorithm>
#include <vector>

namespace {

constexpr DWORD kPawnLoadModule = (41394u << 16) | (0x821u << 2);
constexpr DWORD kPawnExecute = (41394u << 16) | (0x841u << 2);

struct PROCESSOR_POWER_INFORMATION_LOCAL {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

CpuSensor::~CpuSensor() {
    if (m_hPawnIo != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPawnIo);
        m_hPawnIo = INVALID_HANDLE_VALUE;
    }
    if (m_hPciMutex) {
        CloseHandle(m_hPciMutex);
        m_hPciMutex = nullptr;
    }
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
        m_pdhPerfCounter = nullptr;
    }
}

void CpuSensor::Initialize() {
    InitFreq();
    InitTemp();
    SystemSnapshot dummy{};
    PollUsageAndFreq(dummy);
}

void CpuSensor::InitFreq() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors > 0) {
        std::vector<PROCESSOR_POWER_INFORMATION_LOCAL> ppi(si.dwNumberOfProcessors);
        if (CallNtPowerInformation(ProcessorInformation, nullptr, 0,
                ppi.data(), static_cast<ULONG>(ppi.size() * sizeof(PROCESSOR_POWER_INFORMATION_LOCAL))) == 0) {
            m_baseMhz = static_cast<double>(ppi[0].MaxMhz);
        }
    }
    if (PdhOpenQueryW(nullptr, 0, &m_pdhQuery) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(m_pdhQuery, L"\\Processor Information(_Total)\\% Processor Performance", 0, &m_pdhPerfCounter) == ERROR_SUCCESS) {
            PdhCollectQueryData(m_pdhQuery);
            m_pdhPrimed = true;
        } else {
            PdhCloseQuery(m_pdhQuery);
            m_pdhQuery = nullptr;
            m_pdhPerfCounter = nullptr;
        }
    }
}

void CpuSensor::InitTemp() {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAWNIO_AMD), MAKEINTRESOURCEW(10));
    if (!resource) return;
    HGLOBAL module = LoadResource(nullptr, resource);
    void* data = module ? LockResource(module) : nullptr;
    if (!data) return;

    HANDLE device = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (device == INVALID_HANDLE_VALUE) return;

    // The signed module checks that the processor is a supported AMD Zen CPU.
    DWORD bytes = 0;
    if (!DeviceIoControl(device, kPawnLoadModule, data, SizeofResource(nullptr, resource),
            nullptr, 0, &bytes, nullptr)) {
        CloseHandle(device);
        return;
    }
    m_hPciMutex = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
    if (!m_hPciMutex) {
        CloseHandle(device);
        return;
    }
    m_hPawnIo = device;
}

void CpuSensor::PollUsageAndFreq(SystemSnapshot& out) {
    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        uint64_t idle = FileTimeToU64(idleFt);
        uint64_t kernel = FileTimeToU64(kernelFt);
        uint64_t user = FileTimeToU64(userFt);
        if (m_hasPrevCpu) {
            uint64_t dIdle = idle - m_prevIdle;
            uint64_t dKernel = kernel - m_prevKernel;
            uint64_t dUser = user - m_prevUser;
            uint64_t total = dKernel + dUser;
            if (total > 0 && total >= dIdle) {
                m_lastCpuPct = std::clamp((static_cast<double>(total - dIdle) * 100.0) / static_cast<double>(total), 0.0, 100.0);
            }
        }
        m_prevIdle = idle;
        m_prevKernel = kernel;
        m_prevUser = user;
        m_hasPrevCpu = true;
    }
    out.cpuPercent = m_lastCpuPct;

    if (m_pdhQuery && m_pdhPerfCounter && m_baseMhz > 0.0) {
        if (PdhCollectQueryData(m_pdhQuery) == ERROR_SUCCESS && m_pdhPrimed) {
            PDH_FMT_COUNTERVALUE val{};
            if (PdhGetFormattedCounterValue(m_pdhPerfCounter, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
                out.cpuFreqGhz = (m_baseMhz * (val.doubleValue / 100.0)) / 1000.0;
                out.hasCpuFreq = (out.cpuFreqGhz > 0.1);
            }
        }
        m_pdhPrimed = true;
    } else if (m_baseMhz > 0.0) {
        out.cpuFreqGhz = m_baseMhz / 1000.0;
        out.hasCpuFreq = true;
    }
}

void CpuSensor::PollTemp(SystemSnapshot& out) {
    if (m_hPawnIo == INVALID_HANDLE_VALUE) return;

    // SMN uses a shared PCI index/data pair; coordinate with other monitors.
    DWORD wait = WaitForSingleObject(m_hPciMutex, 10);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return;

    struct {
        char name[32] = "ioctl_read_smn";
        uint64_t offset = 0x59800; // THM_TCON_CUR_TMP
    } request;
    uint64_t raw = 0;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(m_hPawnIo, kPawnExecute, &request, sizeof(request),
        &raw, sizeof(raw), &bytes, nullptr);
    ReleaseMutex(m_hPciMutex);

    if (!ok || bytes != sizeof(raw)) return;
    uint32_t value = static_cast<uint32_t>(raw);
    out.cpuTempC = (value >> 21) * 0.125;
    if ((value & (1u << 19)) || (value & (3u << 16)) == (3u << 16)) {
        out.cpuTempC -= 49.0;
    }
    out.hasCpuTemp = true;
}
