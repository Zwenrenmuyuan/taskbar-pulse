#include "collectors/sensors.hpp"
#include "collectors/cpu_sensor.hpp"
#include "collectors/gpu_sensor.hpp"
#include "collectors/net_ram_sensor.hpp"
#include <algorithm>

void HistoryBuffer::Push(const SystemSnapshot& s) {
    cpu[head] = static_cast<float>(std::clamp(s.cpuPercent, 0.0, 100.0));
    gpu[head] = static_cast<float>(std::clamp(s.gpuPercent, 0.0, 100.0));
    ram[head] = static_cast<float>(std::clamp(s.ramPercent, 0.0, 100.0));
    netDown[head] = static_cast<float>(std::max(0.0, s.netDownBps));
    netUp[head] = static_cast<float>(std::max(0.0, s.netUpBps));
    head = (head + 1) % kCapacity;
    if (count < kCapacity) ++count;
}

struct SystemSensors::Impl {
    CpuSensor cpu;
    GpuSensor gpu;
    NetRamSensor netRam;
};

SystemSensors::SystemSensors() : m_impl(new Impl()) {}

SystemSensors::~SystemSensors() {
    delete m_impl;
    m_impl = nullptr;
}

void SystemSensors::Initialize() {
    m_impl->cpu.Initialize();
    m_impl->gpu.Initialize();
    m_impl->netRam.Initialize();
}

SystemSnapshot SystemSensors::Poll() {
    SystemSnapshot snap{};
    m_impl->cpu.PollUsageAndFreq(snap);
    m_impl->cpu.PollTemp(snap);
    m_impl->netRam.PollRam(snap);
    m_impl->gpu.Poll(snap);
    m_impl->netRam.PollNet(snap);
    m_history.Push(snap);
    return snap;
}
