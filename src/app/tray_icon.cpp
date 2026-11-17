#include "app/tray_icon.hpp"
#include <shellapi.h>
#include <cstdio>
#include <cwchar>

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
        app->trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (app->trayAdded) {
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &nid);
        }
    } else if (app->trayAdded) {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void RemoveTrayIcon(AppState* app) {
    if (!app->trayAdded || !app->hwndHost) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = app->hwndHost;
    nid.uID = kTrayUid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    app->trayAdded = false;
}
