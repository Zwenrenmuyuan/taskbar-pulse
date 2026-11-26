#include "app/tray_icon.hpp"
#include <shellapi.h>
#include <cstdio>
#include <cwchar>

namespace {

void CALLBACK TrayModifyWorker(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
    auto* state = static_cast<TrayAsyncState*>(context);
    for (;;) {
        NOTIFYICONDATAW nid{};
        bool haveWork = false;
        AcquireSRWLockExclusive(&state->lock);
        if (!state->shutdown && state->hasPending) {
            nid = state->pendingNid;
            state->hasPending = false;
            haveWork = true;
        }
        ReleaseSRWLockExclusive(&state->lock);

        if (!haveWork) break;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void EnsureTrayAsyncWork(TrayAsyncState& state) {
    if (!state.work) {
        state.work = CreateThreadpoolWork(&TrayModifyWorker, &state, nullptr);
    }
}

} // namespace

void UpdateTrayIcon(AppState* app, bool add) {
    if (!app->hwndHost) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = app->hwndHost;
    nid.uID = kTrayUid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kMsgTray;
    nid.hIcon = app->hIcon;

    if (app->snap.hasGpuUsage && app->snap.hasGpuTemp) {
        _snwprintf_s(nid.szTip, _countof(nid.szTip), _TRUNCATE,
            L"系统状态栏监控\nCPU %.1f%% · GPU %.0f%% %.0f°C · 内存 %.1fGB",
            app->snap.cpuPercent, app->snap.gpuPercent, app->snap.gpuTempC, app->snap.ramUsedGb);
    } else {
        _snwprintf_s(nid.szTip, _countof(nid.szTip), _TRUNCATE,
            L"系统状态栏监控\nCPU %.1f%% · 内存 %.1fGB",
            app->snap.cpuPercent, app->snap.ramUsedGb);
    }

    if (app->cfg.showGoldPrice) {
        GoldQuote gold = app->goldPrice.Snapshot();
        size_t tipLen = wcslen(nid.szTip);
        if (gold.cnyPerGram > 0.0) {
            _snwprintf_s(nid.szTip + tipLen, _countof(nid.szTip) - tipLen, _TRUNCATE,
                L"\n国际黄金 %.2f 元/克%ls\n报价时间 %02u-%02u %02u:%02u:%02u",
                gold.cnyPerGram, gold.fetchSucceeded ? L"" : L"（未更新）",
                gold.updatedLocal.wMonth, gold.updatedLocal.wDay,
                gold.updatedLocal.wHour, gold.updatedLocal.wMinute, gold.updatedLocal.wSecond);
        } else {
            wcscat_s(nid.szTip, L"\n国际金价：暂无报价");
        }
    }

    if (add) {
        EnsureTrayAsyncWork(app->trayAsync);
        AcquireSRWLockExclusive(&app->trayAsync.lock);
        app->trayAsync.shutdown = false;
        app->trayAsync.hasPending = false;
        app->trayAsync.lastTip = nid.szTip;
        ReleaseSRWLockExclusive(&app->trayAsync.lock);

        app->trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (app->trayAdded) {
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
        }
    } else if (app->trayAdded) {
        if (app->trayAsync.lastTip == nid.szTip) return;
        app->trayAsync.lastTip = nid.szTip;

        EnsureTrayAsyncWork(app->trayAsync);
        if (app->trayAsync.work) {
            AcquireSRWLockExclusive(&app->trayAsync.lock);
            app->trayAsync.pendingNid = nid;
            app->trayAsync.hasPending = true;
            ReleaseSRWLockExclusive(&app->trayAsync.lock);
            SubmitThreadpoolWork(app->trayAsync.work);
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
    }
}

void RemoveTrayIcon(AppState* app) {
    if (!app) return;
    if (app->trayAsync.work) {
        AcquireSRWLockExclusive(&app->trayAsync.lock);
        app->trayAsync.shutdown = true;
        app->trayAsync.hasPending = false;
        ReleaseSRWLockExclusive(&app->trayAsync.lock);

        WaitForThreadpoolWorkCallbacks(app->trayAsync.work, TRUE);
        CloseThreadpoolWork(app->trayAsync.work);
        app->trayAsync.work = nullptr;
    }
    if (!app->trayAdded || !app->hwndHost) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = app->hwndHost;
    nid.uID = kTrayUid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    app->trayAdded = false;
}
