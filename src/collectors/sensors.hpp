#pragma once

#include <array>
#include <cstdint>
#include <string>

struct SystemSnapshot {
    double cpuPercent = 0.0;
    double cpuFreqGhz = 0.0;
    bool hasCpuFreq = false;
    double cpuTempC = 0.0;
    bool hasCpuTemp = false;

    double gpuPercent = 0.0;
    double gpuTempC = 0.0;
    bool hasGpuUsage = false;
    bool hasGpuTemp = false;

    double vramUsedGb = 0.0;
    double vramTotalGb = 0.0;
    double vramPercent = 0.0;
    bool hasVram = false;
    std::wstring gpuName;

    double ramUsedGb = 0.0;
    double ramTotalGb = 0.0;
    double ramPercent = 0.0;

    double netUpBps = 0.0;
    double netDownBps = 0.0;
    uint64_t netSessionUpBytes = 0;
    uint64_t netSessionDownBytes = 0;
    std::wstring nicName;
};

struct HistoryBuffer {
    static constexpr size_t kCapacity = 48;
    std::array<float, kCapacity> cpu{};
    std::array<float, kCapacity> gpu{};
    std::array<float, kCapacity> ram{};
    std::array<float, kCapacity> netDown{};
    std::array<float, kCapacity> netUp{};
    size_t head = 0;
    size_t count = 0;

    void Push(const SystemSnapshot& s);
    template <typename Arr>
    float GetOrdered(const Arr& arr, size_t idx) const {
        if (count == 0 || idx >= count) return 0.0f;
        size_t start = (count == kCapacity) ? head : 0;
        return arr[(start + idx) % kCapacity];
    }
};

class SystemSensors {
public:
    SystemSensors();
    ~SystemSensors();

    SystemSensors(const SystemSensors&) = delete;
    SystemSensors& operator=(const SystemSensors&) = delete;

    void Initialize();
    SystemSnapshot Poll();
    const HistoryBuffer& History() const { return m_history; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    HistoryBuffer m_history{};
};
