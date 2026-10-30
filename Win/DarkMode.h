/*
Copyright (c) 2026, Eric Haines
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

// Light and dark themes for Mineways' windows: View > Theme. Mineways uses the classic (unthemed) common controls,
// so most of the dark theme is done by giving controls dark colors, plus drawing a few things itself: push buttons,
// trackbars, the status bar and the menu bar. The menu bar, the popup menus and the title bars use undocumented
// (but widely used) uxtheme.dll functions and window messages, available from Windows 10 1809 on; on older Windows
// those parts just stay light.

#pragma once

// View > Theme choices, saved in the registry
#define THEME_FOLLOW_WINDOWS    0
#define THEME_LIGHT             1
#define THEME_DARK              2

// Read the saved choice and set up; call once, before any windows are made
void DarkModeInit();
int DarkModeGetChoice();
// Save the choice; returns true if dark mode is now on or off where it wasn't before
bool DarkModeSetChoice(int choice);
// For THEME_FOLLOW_WINDOWS, check Windows' app mode again, e.g. on WM_SETTINGCHANGE; returns true if it changed
bool DarkModeUpdateFromWindows();
bool DarkModeIsOn();

// Colors and brushes for windows: the system's for the light theme
COLORREF DarkModeWindowColor();
COLORREF DarkModeTextColor();
HBRUSH DarkModeWindowBrush();
// color of map areas not drawn yet: white, or dark gray
COLORREF DarkModeMapPendingColor();

// Top level window: title bar and menus to match the theme; call when made and when the theme changes
void DarkModeApplyToWindow(HWND hWnd);
// The menu bar: call first thing in the window procedure; returns true if the message was handled, with its result
bool DarkModeHandleMenuBar(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
// A trackbar's NM_CUSTOMDRAW; returns the result to give back
LRESULT DarkModeTrackbarCustomDraw(LPNMCUSTOMDRAW pCustomDraw);
// Status bar and its progress bar; call when made and when the theme changes
void DarkModeApplyToStatusBar(HWND hwndStatus, HWND hwndProgress);
// Static text, so that disabled text isn't drawn embossed in light colors; call when made
void DarkModeApplyToLabel(HWND hwndLabel);
// Dialog boxes: call from WM_INITDIALOG
void DarkModeApplyToDialog(HWND hDlg);
// MessageBox(), in the theme
int DarkModeMessageBox(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
