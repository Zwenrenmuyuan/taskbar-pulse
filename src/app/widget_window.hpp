#pragma once

#include "app/app_controller.hpp"

void EnsureWidgetWindow(AppState* app);
void RenderWidget(AppState* app);
void SyncWidgetPlacement(AppState* app, bool forceRender);
LRESULT CALLBACK WidgetWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
