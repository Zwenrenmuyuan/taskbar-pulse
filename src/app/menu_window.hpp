#pragma once

#include "app/app_controller.hpp"

FluentMenuSpec BuildMainMenuSpec(const AppConfig& cfg, bool flyoutVisible);
FluentMenuSpec BuildSubmenuSpec(const AppConfig& cfg, UINT subId);
void RenderMainMenu(AppState* app);
void RenderSubmenu(AppState* app);
void HideSubmenu(AppState* app);
void CloseMenu(AppState* app, bool restoreFlyoutFocus);
void OpenSubmenuForRow(AppState* app, int rowIdx, bool selectFirst);
void ShowContextMenu(AppState* app, POINT ptScreen, bool openedFromFlyout = false);

LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SubmenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
