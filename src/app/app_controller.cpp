#include "app/app_controller.hpp"
#include "app/tray_icon.hpp"
#include "app/widget_window.hpp"
#include "app/flyout_window.hpp"
#include "app/menu_window.hpp"
#include "render/palette.hpp"
#include "resource.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>

// ---- VsyncPacer ----

void VsyncPacer::Start(HWND hwndTarget, UINT msgTick) {
    Stop();
    {
        std::lock_guard lock(m_mutex);
        m_hwnd = hwndTarget;
        m_msg = msgTick;
        m_stop = false;
        m_active = false;
        m_tickPending.store(false);
    }
    m_thread = std::thread(&VsyncPacer::Run, this);
}

void VsyncPacer::Stop() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
        m_active = false;
    }
    m_cv.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_tickPending.store(false);
}

bool VsyncPacer::Wake() {
    {
        std::lock_guard lock(m_mutex);
        if (m_stop || m_active) return false;
        m_active = true;
    }
    m_cv.notify_one();
    return true;
}

void VsyncPacer::SleepUntilWoken() {
    std::lock_guard lock(m_mutex);
    m_active = false;
}

void VsyncPacer::OnTickReceived() {
    m_tickPending.store(false);
}

void VsyncPacer::Run() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    for (;;) {
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || m_active; });
            if (m_stop) break;
        }
        LARGE_INTEGER t0{}, t1{};
        QueryPerformanceCounter(&t0);
        HRESULT hr = DwmFlush();
        QueryPerformanceCounter(&t1);
        double elapsedMs = (freq.QuadPart > 0)
            ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart)
            : 0.0;
        // Guard against DwmFlush returning immediately (e.g. RDP/disconnected display),
        // which would otherwise spin and starve lower-priority WM_TIMER messages.
        if (FAILED(hr) || elapsedMs < 2.0) {
            Sleep(4);
        }
        HWND target = nullptr;
        UINT msg = 0;
        bool shouldPost = false;
        {
            std::lock_guard lock(m_mutex);
            if (m_stop) break;
            shouldPost = m_active;
            target = m_hwnd;
            msg = m_msg;
        }
        if (shouldPost && target && !m_tickPending.exchange(true)) {
            if (!PostMessageW(target, msg, 0, 0)) {
                m_tickPending.store(false);
            }
        }
    }
}

// ---- SensorWorker ----

void SensorWorker::Start(HWND hwndTarget, UINT msgData, int intervalMs) {
    Stop();
    {
        std::lock_guard lock(m_mutex);
        m_hwnd = hwndTarget;
        m_msg = msgData;
        m_intervalMs = std::clamp(intervalMs, 250, 10000);
        m_stop = false;
        m_intervalChanged = false;
    }
    m_thread = std::thread(&SensorWorker::Run, this);
}

void SensorWorker::Stop() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void SensorWorker::SetInterval(int intervalMs) {
    {
        std::lock_guard lock(m_mutex);
        m_intervalMs = std::clamp(intervalMs, 250, 10000);
        m_intervalChanged = true;
    }
    m_cv.notify_one();
}

bool SensorWorker::FetchLatest(SystemSnapshot& outSnap, HistoryBuffer& outHistory, uint64_t& outSeq) {
    std::lock_guard lock(m_mutex);
    if (m_seq == 0 || m_seq == outSeq) return false;
    outSnap = m_latestSnap;
    outHistory = m_latestHistory;
    outSeq = m_seq;
    return true;
}

void SensorWorker::Run() {
    SystemSensors sensors;
    sensors.Initialize();

    for (;;) {
        SystemSnapshot snap = sensors.Poll();
        HWND target = nullptr;
        UINT msg = 0;
        int intervalMs = 1000;
        {
            std::lock_guard lock(m_mutex);
            if (m_stop) break;
            m_latestSnap = std::move(snap);
            m_latestHistory = sensors.History();
            ++m_seq;
            target = m_hwnd;
            msg = m_msg;
            intervalMs = m_intervalMs;
        }
        if (target) {
            PostMessageW(target, msg, 0, 0);
        }

        auto wakeAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
        std::unique_lock lock(m_mutex);
        m_cv.wait_until(lock, wakeAt, [&] { return m_stop || m_intervalChanged; });
        if (m_stop) break;
        m_intervalChanged = false;
    }
}

namespace {

double ComputeNetLogPct(const SystemSnapshot& snap) {
    double peakBps = std::max(snap.netDownBps, snap.netUpBps);
    if (peakBps <= 512.0) return 0.0;
    return std::clamp((std::log10(peakBps / 512.0) / 5.3) * 100.0, 4.0, 100.0);
}

double ComputeWindowPeak(const HistoryBuffer& history, const std::array<float, HistoryBuffer::kCapacity>& arr) {
    constexpr size_t kWindow = 24;
    size_t n = std::min(history.count, kWindow);
    size_t start = history.count - n;
    double peak = 0.0;
    for (size_t i = 0; i < n; ++i) {
        peak = std::max(peak, static_cast<double>(history.GetOrdered(arr, start + i)));
    }
    return std::clamp(peak * 1.35, 28.0, 100.0);
}

double ComputeFlyoutNetMax(const HistoryBuffer& history) {
    double peak = 10240.0;
    for (size_t i = 0; i < history.count; ++i) {
        peak = std::max(peak, static_cast<double>(std::max(
            history.GetOrdered(history.netDown, i),
            history.GetOrdered(history.netUp, i))));
    }
    return peak;
}

bool AnyWidgetDragging(const AppState* app) {
    if (!app) return false;
    for (const auto& w : app->widgets) {
        if (w.draggingOffset) return true;
    }
    return false;
}

void AdvanceMenuPill(const FluentMenuSpec& spec, int hoverIdx, MenuAnimState& state, float dt, bool& outDirty) {
    const bool validHover = (hoverIdx >= 0 && hoverIdx < static_cast<int>(spec.items.size()) &&
        !spec.items[static_cast<size_t>(hoverIdx)].isSeparator);
    const float targetAlpha = validHover ? 1.0f : 0.0f;

    if (validHover) {
        const float targetY = RenderEngine::MenuRowTopDip(spec, hoverIdx);
        const float targetDanger = spec.items[static_cast<size_t>(hoverIdx)].isDestructive ? 1.0f : 0.0f;
        if (!state.hasPillPos || state.pillAlpha < 0.05f) {
            state.pillY = targetY;
            state.pillDanger = targetDanger;
            state.hasPillPos = true;
            outDirty = true;
        } else {
            outDirty |= anim::Damp(state.pillY, targetY, 52.0f, dt, 0.15f);
            outDirty |= anim::Damp(state.pillDanger, targetDanger, 32.0f, dt, 0.01f);
        }
    }
    outDirty |= anim::Damp(state.pillAlpha, targetAlpha, 42.0f, dt, 0.005f);
}

void StepAnimations(AppState* app) {
    if (!app) return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = 1.0f / 60.0f;
    if (app->qpcFreq.QuadPart > 0 && app->lastAnimQpc.QuadPart > 0) {
        double raw = static_cast<double>(now.QuadPart - app->lastAnimQpc.QuadPart) /
                     static_cast<double>(app->qpcFreq.QuadPart);
        dt = static_cast<float>(std::clamp(raw, 0.001, 0.05));
    }
    app->lastAnimQpc = now;

    bool anyMoving = false;
    bool metricsDirty = false;

    const bool light = ResolveEffectiveLightTheme(app->cfg);
    const bool minimal = (app->cfg.style == VisualStyle::Minimal);
    const Palette pal = MakePalette(light, minimal);

    const double targetCpu = app->snap.cpuPercent;
    const double targetGpu = app->snap.hasGpuUsage ? app->snap.gpuPercent : 0.0;
    const double targetVram = app->snap.hasVram ? app->snap.vramPercent : 0.0;
    const double targetRam = app->snap.ramPercent;
    const double targetNetLog = ComputeNetLogPct(app->snap);
    const double targetCpuCeil = ComputeWindowPeak(app->history, app->history.cpu);
    const double targetGpuCeil = ComputeWindowPeak(app->history, app->history.gpu);
    const double targetNetMax = ComputeFlyoutNetMax(app->history);

    if (!app->anim.initialized) {
        app->anim.cpuPct = targetCpu;
        app->anim.gpuPct = targetGpu;
        app->anim.vramPct = targetVram;
        app->anim.ramPct = targetRam;
        app->anim.netLogPct = targetNetLog;
        app->anim.cpuSparkCeil = targetCpuCeil;
        app->anim.gpuSparkCeil = targetGpuCeil;
        app->anim.netFlyoutMax = targetNetMax;
        app->anim.historySlide = 0.0f;
        app->anim.initialized = true;
    } else {
        metricsDirty |= anim::DampDouble(app->anim.cpuPct, targetCpu, 16.0f, dt, 0.08);
        metricsDirty |= anim::DampDouble(app->anim.gpuPct, targetGpu, 16.0f, dt, 0.08);
        metricsDirty |= anim::DampDouble(app->anim.vramPct, targetVram, 16.0f, dt, 0.08);
        metricsDirty |= anim::DampDouble(app->anim.ramPct, targetRam, 16.0f, dt, 0.08);
        metricsDirty |= anim::DampDouble(app->anim.netLogPct, targetNetLog, 17.0f, dt, 0.12);
        metricsDirty |= anim::DampDouble(app->anim.cpuSparkCeil, targetCpuCeil, 11.0f, dt, 0.15);
        metricsDirty |= anim::DampDouble(app->anim.gpuSparkCeil, targetGpuCeil, 11.0f, dt, 0.15);
        double netEps = std::max(64.0, targetNetMax * 0.005);
        metricsDirty |= anim::DampDouble(app->anim.netFlyoutMax, targetNetMax, 11.0f, dt, netEps);
        metricsDirty |= anim::Damp(app->anim.historySlide, 0.0f, 18.0f, dt, 0.004f);
    }

    const D2D1_COLOR_F targetCpuCol = PickLoadColor(app->snap.cpuPercent, pal);
    const D2D1_COLOR_F targetGpuCol = PickLoadColor(app->snap.gpuPercent, pal);
    const D2D1_COLOR_F targetVramCol = app->snap.hasVram ? PickLoadColor(app->snap.vramPercent, pal, true) : pal.label;
    const D2D1_COLOR_F targetRamCol = PickLoadColor(app->snap.ramPercent, pal, true);
    const D2D1_COLOR_F targetCpuTempCol = app->snap.hasCpuTemp ? PickTempColor(app->snap.cpuTempC, pal) : pal.label;
    const D2D1_COLOR_F targetGpuTempCol = app->snap.hasGpuTemp ? PickTempColor(app->snap.gpuTempC, pal) : pal.label;

    if (!app->anim.colorsInitialized ||
        app->anim.lastLightTheme != light ||
        app->anim.lastMinimalStyle != minimal) {
        app->anim.cpuCol = targetCpuCol;
        app->anim.gpuCol = targetGpuCol;
        app->anim.vramCol = targetVramCol;
        app->anim.ramCol = targetRamCol;
        app->anim.cpuTempCol = targetCpuTempCol;
        app->anim.gpuTempCol = targetGpuTempCol;
        app->anim.lastLightTheme = light;
        app->anim.lastMinimalStyle = minimal;
        app->anim.colorsInitialized = true;
    } else {
        metricsDirty |= anim::DampColor(app->anim.cpuCol, targetCpuCol, 18.0f, dt);
        metricsDirty |= anim::DampColor(app->anim.gpuCol, targetGpuCol, 18.0f, dt);
        metricsDirty |= anim::DampColor(app->anim.vramCol, targetVramCol, 18.0f, dt);
        metricsDirty |= anim::DampColor(app->anim.ramCol, targetRamCol, 18.0f, dt);
        metricsDirty |= anim::DampColor(app->anim.cpuTempCol, targetCpuTempCol, 18.0f, dt);
        metricsDirty |= anim::DampColor(app->anim.gpuTempCol, targetGpuTempCol, 18.0f, dt);
    }
    anyMoving |= metricsDirty;

    // Taskbar widgets
    for (auto& w : app->widgets) {
        if (!w.hwnd || !w.visible || w.fullscreenPaused) {
            w.hoverProgress = w.hovered ? 1.0f : 0.0f;
            w.pressProgress = w.pressed ? 1.0f : 0.0f;
            continue;
        }
        bool wDirty = metricsDirty;
        bool hpMoving = anim::Damp(w.hoverProgress, w.hovered ? 1.0f : 0.0f, 36.0f, dt);
        bool ppMoving = anim::Damp(w.pressProgress, w.pressed ? 1.0f : 0.0f, 55.0f, dt);
        wDirty |= hpMoving || ppMoving;
        anyMoving |= hpMoving || ppMoving;

        if (!w.draggingOffset && w.hasAnimX) {
            float targetX = static_cast<float>(w.lastPlacement.ptWidgetClient.x);
            if (anim::Damp(w.animX, targetX, 26.0f, dt, 0.35f)) {
                anyMoving = true;
                int curX = static_cast<int>(std::lround(w.animX));
                SetWindowPos(w.hwnd, nullptr, curX, w.lastPlacement.ptWidgetClient.y, 0, 0,
                    SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW);
            }
        }

        if (wDirty) {
            RenderWidget(app, w);
        }
    }

    // Flyout
    if (app->flyoutVisible) {
        bool fDirty = metricsDirty;
        for (int i = 0; i < 4; ++i) {
            bool hMove = anim::Damp(app->flyoutAnim.btnHover[i], (app->flyoutHoverBtn == i) ? 1.0f : 0.0f, 38.0f, dt);
            bool pMove = anim::Damp(app->flyoutAnim.btnPress[i], (app->flyoutPressedBtn == i) ? 1.0f : 0.0f, 60.0f, dt);
            fDirty |= hMove || pMove;
            anyMoving |= hMove || pMove;
        }
        if (fDirty) {
            RenderAndPositionFlyout(app);
        }
    }

    // Main menu pill glide
    if (app->menuVisible) {
        bool mDirty = false;
        AdvanceMenuPill(app->menuSpec, app->menuHoverIdx, app->menuAnim, dt, mDirty);
        anyMoving |= mDirty;
        if (mDirty) {
            RenderMainMenu(app);
        }
    }

    // Submenu pill glide
    if (app->submenuVisible) {
        bool sDirty = false;
        AdvanceMenuPill(app->submenuSpec, app->submenuHoverIdx, app->submenuAnim, dt, sDirty);
        anyMoving |= sDirty;
        if (sDirty) {
            RenderSubmenu(app);
        }
    }

    if (!anyMoving) {
        app->pacer.SleepUntilWoken();
    }
}

void OnTaskbarChangedCallback(void* userData) {
    auto* app = static_cast<AppState*>(userData);
    if (app && app->hwndHost && !app->repositionPending) {
        app->repositionPending = true;
        PostMessageW(app->hwndHost, kMsgReposition, 0, 0);
    }
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = GetApp(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (app->msgTaskbarCreated != 0 && msg == app->msgTaskbarCreated) {
        app->trayAdded = false;
        UpdateTrayIcon(app, true);
        app->host.RefreshHandles();
        SyncWidgetPlacement(app, true);
        return 0;
    }

    switch (msg) {
        case kMsgSensorData: {
            bool hadPrev = (app->lastSensorSeq > 0);
            if (app->sensorWorker.FetchLatest(app->snap, app->history, app->lastSensorSeq)) {
                if (hadPrev && app->history.count >= 2) {
                    app->anim.historySlide = 1.0f;
                }
                bool anyActive = false;
                for (auto& w : app->widgets) {
                    if (!w.fullscreenPaused) {
                        RenderWidget(app, w);
                        anyActive = true;
                    }
                }
                if (anyActive || app->widgets.empty()) {
                    UpdateTrayIcon(app, false);
                }
                if (app->flyoutVisible) {
                    RenderAndPositionFlyout(app);
                }
                if (anyActive || app->flyoutVisible) {
                    RequestAnimationFrame(app);
                }
            }
            return 0;
        }

        case kMsgAnimTick:
            app->pacer.OnTickReceived();
            StepAnimations(app);
            return 0;

        case WM_TIMER:
            if (wParam == kTimerGuardId) {
                if (!AnyWidgetDragging(app)) {
                    SyncWidgetPlacement(app, false);
                }
            } else if (wParam == kTimerGoldId) {
                app->goldPrice.Refresh();
            }
            return 0;

        case kMsgReposition:
            app->repositionPending = false;
            if (!AnyWidgetDragging(app)) {
                SyncWidgetPlacement(app, false);
            }
            return 0;

        case WM_SETTINGCHANGE:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (msg == WM_SETTINGCHANGE) {
                InvalidateSystemThemeCache();
                SyncAnimatedMetricTargetsImmediate(app);
            }
            if (msg == WM_DISPLAYCHANGE) app->host.RefreshHandles(true);
            SyncWidgetPlacement(app, true);
            if (app->flyoutVisible) RenderAndPositionFlyout(app);
            return 0;

        case kMsgTray: {
            UINT ev = LOWORD(lParam);
            if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT) {
                ToggleFlyout(app);
            } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
                POINT pt{};
                GetCursorPos(&pt);
                ShowContextMenu(app, pt);
            }
            return 0;
        }

        case WM_DESTROY:
            CloseMenu(app, false, false);
            HideFlyout(app, false);
            KillTimer(hwnd, kTimerGuardId);
            KillTimer(hwnd, kTimerGoldId);
            app->pacer.Stop();
            app->sensorWorker.Stop();
            app->host.DetachHook();
            RemoveTrayIcon(app);
            if (app->hwndSubmenu) {
                app->renderer.ResetWidgetTarget(app->hwndSubmenu);
                DestroyWindow(app->hwndSubmenu);
                app->hwndSubmenu = nullptr;
            }
            if (app->hwndMenu) {
                app->renderer.ResetWidgetTarget(app->hwndMenu);
                DestroyWindow(app->hwndMenu);
                app->hwndMenu = nullptr;
            }
            DestroyAllWidgets(app);
            if (app->hwndFlyout) {
                app->renderer.ResetWidgetTarget(app->hwndFlyout);
                DestroyWindow(app->hwndFlyout);
                app->hwndFlyout = nullptr;
            }
            app->hwndHost = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void RequestAnimationFrame(AppState* app) {
    if (!app) return;
    if (app->pacer.Wake()) {
        QueryPerformanceCounter(&app->lastAnimQpc);
    }
}

void KickInteractiveAnimation(AppState* app, HWND focusHwnd) {
    if (!app) return;
    // Immediately render Frame 0 with an initial impulse so pointer hover/press
    // never waits 1-2 VSync frames before showing visible response.
    constexpr float kKickDt = 0.016f;
    if (focusHwnd && focusHwnd == app->hwndMenu && app->menuVisible) {
        bool dirty = false;
        AdvanceMenuPill(app->menuSpec, app->menuHoverIdx, app->menuAnim, kKickDt, dirty);
        if (dirty) RenderMainMenu(app);
    } else if (focusHwnd && focusHwnd == app->hwndSubmenu && app->submenuVisible) {
        bool dirty = false;
        AdvanceMenuPill(app->submenuSpec, app->submenuHoverIdx, app->submenuAnim, kKickDt, dirty);
        if (dirty) RenderSubmenu(app);
    } else if (focusHwnd && focusHwnd == app->hwndFlyout && app->flyoutVisible) {
        bool dirty = false;
        for (int i = 0; i < 4; ++i) {
            dirty |= anim::Damp(app->flyoutAnim.btnHover[i], (app->flyoutHoverBtn == i) ? 1.0f : 0.0f, 38.0f, kKickDt);
            dirty |= anim::Damp(app->flyoutAnim.btnPress[i], (app->flyoutPressedBtn == i) ? 1.0f : 0.0f, 60.0f, kKickDt);
        }
        if (dirty) RenderAndPositionFlyout(app);
    } else if (WidgetInstance* w = FindWidget(app, focusHwnd)) {
        if (w->visible && !w->fullscreenPaused) {
            bool dirty = false;
            dirty |= anim::Damp(w->hoverProgress, w->hovered ? 1.0f : 0.0f, 36.0f, kKickDt);
            dirty |= anim::Damp(w->pressProgress, w->pressed ? 1.0f : 0.0f, 55.0f, kKickDt);
            if (dirty) RenderWidget(app, *w);
        }
    }
    RequestAnimationFrame(app);
}

void SyncAnimatedMetricTargetsImmediate(AppState* app) {
    if (!app) return;
    const bool light = ResolveEffectiveLightTheme(app->cfg);
    const bool minimal = (app->cfg.style == VisualStyle::Minimal);
    const Palette pal = MakePalette(light, minimal);
    app->anim.cpuCol = PickLoadColor(app->snap.cpuPercent, pal);
    app->anim.gpuCol = PickLoadColor(app->snap.gpuPercent, pal);
    app->anim.vramCol = app->snap.hasVram ? PickLoadColor(app->snap.vramPercent, pal, true) : pal.label;
    app->anim.ramCol = PickLoadColor(app->snap.ramPercent, pal, true);
    app->anim.cpuTempCol = app->snap.hasCpuTemp ? PickTempColor(app->snap.cpuTempC, pal) : pal.label;
    app->anim.gpuTempCol = app->snap.hasGpuTemp ? PickTempColor(app->snap.gpuTempC, pal) : pal.label;
    app->anim.lastLightTheme = light;
    app->anim.lastMinimalStyle = minimal;
    app->anim.colorsInitialized = true;
}

void ApplyRefreshTimer(AppState* app) {
    if (!app) return;
    app->sensorWorker.SetInterval(app->cfg.refreshMs);
}

void ApplyGoldTimer(AppState* app) {
    if (!app->hwndHost) return;
    KillTimer(app->hwndHost, kTimerGoldId);
    if (app->cfg.showGoldPrice) {
        app->goldPrice.Refresh();
        SetTimer(app->hwndHost, kTimerGoldId, 30000, nullptr);
    }
}

void ExecuteMenuCommand(AppState* app, UINT cmd) {
    if (cmd == 0) return;
    bool changed = false;
    switch (cmd) {
        case IDM_OPEN_FLYOUT:
            ToggleFlyout(app);
            break;
        case IDM_STYLE_CAPSULE:
            app->cfg.style = VisualStyle::Capsule;
            changed = true;
            break;
        case IDM_STYLE_MINIMAL:
            app->cfg.style = VisualStyle::Minimal;
            changed = true;
            break;
        case IDM_THEME_AUTO:
            app->cfg.theme = ThemeMode::Auto;
            changed = true;
            break;
        case IDM_THEME_LIGHT:
            app->cfg.theme = ThemeMode::Light;
            changed = true;
            break;
        case IDM_THEME_DARK:
            app->cfg.theme = ThemeMode::Dark;
            changed = true;
            break;
        case IDM_TOGGLE_BARS:
            app->cfg.showMiniBars = !app->cfg.showMiniBars;
            changed = true;
            break;
        case IDM_ROW2_CPU_FREQ:
            app->cfg.row2Col1 = Row2Col1Mode::CpuFreq;
            changed = true;
            break;
        case IDM_ROW2_MEM_PCT:
            app->cfg.row2Col1 = Row2Col1Mode::MemPct;
            changed = true;
            break;
        case IDM_ROW2_CPU_TEMP:
            app->cfg.row2Col1 = Row2Col1Mode::CpuTemp;
            changed = true;
            break;
        case IDM_ROW2_HIDE:
            app->cfg.row2Col1 = Row2Col1Mode::Hide;
            changed = true;
            break;
        case IDM_TOGGLE_GPUTEMP:
            app->cfg.showGpuTemp = !app->cfg.showGpuTemp;
            changed = true;
            break;
        case IDM_TOGGLE_GOLD:
            app->cfg.showGoldPrice = !app->cfg.showGoldPrice;
            ApplyGoldTimer(app);
            changed = true;
            break;
        case IDM_RATE_500:
            app->cfg.refreshMs = 500;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RATE_1000:
            app->cfg.refreshMs = 1000;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RATE_2000:
            app->cfg.refreshMs = 2000;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RESET_OFFSET:
            ResetAllMonitorOffsets(app->cfg);
            changed = true;
            break;
        case IDM_DISPLAY_ALL:
            app->cfg.displayMode = DisplayTargetMode::All;
            app->cfg.displayDevice.clear();
            changed = true;
            break;
        case IDM_DISPLAY_PRIMARY:
            app->cfg.displayMode = DisplayTargetMode::Primary;
            app->cfg.displayDevice.clear();
            changed = true;
            break;
        case IDM_TOGGLE_AUTOSTART:
            SetAutoStartEnabled(!IsAutoStartEnabled());
            break;
        case IDM_EXIT:
            DestroyWindow(app->hwndHost);
            return;
        default:
            if (cmd >= IDM_DISPLAY_SPEC_BASE && cmd <= IDM_DISPLAY_SPEC_MAX) {
                size_t idx = static_cast<size_t>(cmd - IDM_DISPLAY_SPEC_BASE);
                if (idx < app->menuDisplayDevices.size()) {
                    app->cfg.displayMode = DisplayTargetMode::Specific;
                    app->cfg.displayDevice = app->menuDisplayDevices[idx];
                    changed = true;
                }
            }
            break;
    }

    if (changed) {
        SyncAnimatedMetricTargetsImmediate(app);
        SyncWidgetPlacement(app, true);
        if (app->flyoutVisible) {
            RenderAndPositionFlyout(app);
        }
        SaveConfig(app->cfg);
    }
}

int RunApplication(HINSTANCE hInstance) {
    AppState app;
    app.hInst = hInstance;
    QueryPerformanceFrequency(&app.qpcFreq);
    int smIcon = GetSystemMetrics(SM_CXSMICON);
    app.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, smIcon, smIcon, LR_DEFAULTCOLOR));
    if (!app.hIcon) app.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    app.msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    app.cfg = LoadConfig();

    int exitCode = 0;
    if (!app.renderer.Initialize()) {
        exitCode = 1;
    } else {
        WNDCLASSEXW wcH{};
        wcH.cbSize = sizeof(wcH);
        wcH.lpfnWndProc = HostWndProc;
        wcH.hInstance = hInstance;
        wcH.lpszClassName = kHostClass;
        RegisterClassExW(&wcH);

        WNDCLASSEXW wcW{};
        wcW.cbSize = sizeof(wcW);
        wcW.style = CS_HREDRAW | CS_VREDRAW;
        wcW.lpfnWndProc = WidgetWndProc;
        wcW.hInstance = hInstance;
        wcW.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcW.lpszClassName = kWidgetClass;
        RegisterClassExW(&wcW);

        WNDCLASSEXW wcF{};
        wcF.cbSize = sizeof(wcF);
        wcF.style = CS_HREDRAW | CS_VREDRAW;
        wcF.lpfnWndProc = FlyoutWndProc;
        wcF.hInstance = hInstance;
        wcF.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcF.lpszClassName = kFlyoutClass;
        RegisterClassExW(&wcF);

        WNDCLASSEXW wcM{};
        wcM.cbSize = sizeof(wcM);
        wcM.lpfnWndProc = MenuWndProc;
        wcM.hInstance = hInstance;
        wcM.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcM.lpszClassName = kMenuClass;
        RegisterClassExW(&wcM);

        WNDCLASSEXW wcS{};
        wcS.cbSize = sizeof(wcS);
        wcS.lpfnWndProc = SubmenuWndProc;
        wcS.hInstance = hInstance;
        wcS.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcS.lpszClassName = kSubmenuClass;
        RegisterClassExW(&wcS);

        app.hwndHost = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kHostClass, L"TaskbarMonitorHost", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, hInstance, &app);

        const DWORD popupRenderEx = app.renderer.HasDComp() ? WS_EX_NOREDIRECTIONBITMAP : WS_EX_LAYERED;

        app.hwndFlyout = CreateWindowExW(
            popupRenderEx | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kFlyoutClass, L"TaskbarMonitorFlyout", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        app.hwndMenu = CreateWindowExW(
            popupRenderEx | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kMenuClass, L"TaskbarMonitorMenu", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        app.hwndSubmenu = CreateWindowExW(
            popupRenderEx | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kSubmenuClass, L"TaskbarMonitorSubmenu", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        // Disable default DWM popup transitions so they don't stack on top of our crisp DComp animations.
        if (app.renderer.HasDComp()) {
            BOOL disableTransitions = TRUE;
            for (HWND popup : { app.hwndFlyout, app.hwndMenu, app.hwndSubmenu }) {
                if (popup) {
                    DwmSetWindowAttribute(popup, DWMWA_TRANSITIONS_FORCEDISABLED,
                        &disableTransitions, sizeof(disableTransitions));
                }
            }
        }

        app.pacer.Start(app.hwndHost, kMsgAnimTick);
        app.sensorWorker.Start(app.hwndHost, kMsgSensorData, app.cfg.refreshMs);
        app.host.AttachHook(&OnTaskbarChangedCallback, &app);
        UpdateTrayIcon(&app, true);
        SyncWidgetPlacement(&app, true);

        ApplyGoldTimer(&app);
        SetTimer(app.hwndHost, kTimerGuardId, 350, nullptr);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (app.hIcon) DestroyIcon(app.hIcon);
    return exitCode;
}
