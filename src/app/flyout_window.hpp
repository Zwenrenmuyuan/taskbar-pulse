#pragma once

#include "app/app_controller.hpp"

void HideFlyout(AppState* app);
void RenderAndPositionFlyout(AppState* app);
void ShowFlyout(AppState* app);
void ToggleFlyout(AppState* app);
LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
