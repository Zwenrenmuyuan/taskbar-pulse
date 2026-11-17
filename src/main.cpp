#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include "app/app_controller.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\Win11TaskbarMonitor_SingleInstance_v1");
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int exitCode = RunApplication(hInstance);

    if (SUCCEEDED(hrCom)) CoUninitialize();
    CloseHandle(hMutex);
    return exitCode;
}
