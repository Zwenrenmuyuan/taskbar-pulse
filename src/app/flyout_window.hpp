#pragma once

#include "app/app_controller.hpp"

void HideFlyout(AppState* app, bool animate = true);
void RenderAndPositionFlyout(AppState* app);
void ShowFlyout(AppState* app, HWND anchorWidget = nullptr);
void ToggleFlyout(AppState* app, HWND anchorWidget = nullptr);
LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
