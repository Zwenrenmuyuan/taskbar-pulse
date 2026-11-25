#pragma once

#include "app/app_controller.hpp"

WidgetInstance* FindWidget(AppState* app, HWND hwnd);
const WidgetInstance* FindPreferredWidget(const AppState* app, HWND preferredHwnd = nullptr);
void RenderWidget(AppState* app, WidgetInstance& widget);
void DestroyAllWidgets(AppState* app);
void SyncWidgetPlacement(AppState* app, bool forceRender);
LRESULT CALLBACK WidgetWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
