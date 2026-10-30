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

#include "stdafx.h"
#include <assert.h>
#include <vector>
#include "DarkMode.h"

// saved with the other settings, as HKCU\Software\Eric Haines\Mineways\Theme
#define THEME_REGKEY            L"Software\\Eric Haines\\Mineways"
#define THEME_REGVALUE          L"Theme"

// dark theme colors
#define DARK_WINDOW             RGB(32, 32, 32)
#define DARK_CONTROL            RGB(45, 45, 45)     // edit fields, lists, trackbar channels
#define DARK_HEADER             RGB(55, 55, 55)     // list column headers
#define DARK_TEXT               RGB(228, 228, 228)
#define DARK_DISABLED_TEXT      RGB(128, 128, 128)
#define DARK_BUTTON             RGB(56, 56, 56)
#define DARK_BUTTON_PRESSED     RGB(80, 80, 80)
#define DARK_BORDER             RGB(100, 100, 100)
#define DARK_DEFAULT_BORDER     RGB(170, 170, 170)  // the default push button
#define DARK_MENU_HOT           RGB(65, 65, 65)
#define DARK_THUMB              RGB(170, 170, 170)
#define DARK_THUMB_DISABLED     RGB(85, 85, 85)
#define DARK_MAP_PENDING        RGB(40, 40, 40)
#define DARK_PROGRESS           RGB(64, 128, 200)

// window properties, for subclassed windows
#define PROP_OLDPROC            L"MinewaysDarkModeOldProc"
#define PROP_OWNERDRAWN         L"MinewaysDarkModeOwnerDrawn"
#define PROP_MESSAGEBOX         L"MinewaysDarkModeMessageBox"

static int gChoice = THEME_FOLLOW_WINDOWS;
static bool gDark = false;

static HBRUSH gWindowBrush = NULL;
static HBRUSH gControlBrush = NULL;
static HFONT gMenuFont = NULL;

// Undocumented uxtheme.dll functions, by ordinal; see e.g. https://github.com/ysc3839/win32-darkmode
enum PreferredAppMode { AppModeDefault, AppModeAllowDark, AppModeForceDark, AppModeForceLight, AppModeMax };
typedef PreferredAppMode(WINAPI* fnSetPreferredAppMode)(PreferredAppMode appMode);     // 135, Windows 10 1903 on
typedef bool (WINAPI* fnAllowDarkModeForApp)(bool allow);                              // 135, Windows 10 1809
typedef bool (WINAPI* fnAllowDarkModeForWindow)(HWND hWnd, bool allow);                // 133
typedef void (WINAPI* fnFlushMenuThemes)();                                             // 136
typedef void (WINAPI* fnRefreshImmersiveColorPolicyState)();                            // 104
static fnSetPreferredAppMode pSetPreferredAppMode = NULL;
static fnAllowDarkModeForApp pAllowDarkModeForApp = NULL;
static fnAllowDarkModeForWindow pAllowDarkModeForWindow = NULL;
static fnFlushMenuThemes pFlushMenuThemes = NULL;
static fnRefreshImmersiveColorPolicyState pRefreshImmersiveColorPolicyState = NULL;
static DWORD gBuildNumber = 0;

typedef HRESULT(WINAPI* fnDwmSetWindowAttribute)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);
static fnDwmSetWindowAttribute pDwmSetWindowAttribute = NULL;

// Undocumented messages for drawing the menu bar, from https://github.com/adzm/win32-darkmode
#define WM_UAHDRAWMENU          0x0091
#define WM_UAHDRAWMENUITEM      0x0092
typedef union tagUAHMENUITEMMETRICS {
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;
typedef struct tagUAHMENUPOPUPMETRICS {
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;
typedef struct tagUAHMENU {
    HMENU hmenu;
    HDC hdc;
    DWORD dwFlags;
} UAHMENU;
typedef struct tagUAHMENUITEM {
    int iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;
typedef struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
} UAHDRAWMENUITEM;

static void loadThemeFunctions()
{
    typedef void (WINAPI* fnRtlGetNtVersionNumbers)(LPDWORD major, LPDWORD minor, LPDWORD build);
    fnRtlGetNtVersionNumbers pRtlGetNtVersionNumbers = reinterpret_cast<fnRtlGetNtVersionNumbers>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetNtVersionNumbers"));
    if (pRtlGetNtVersionNumbers == NULL)
        return;
    DWORD major = 0, minor = 0;
    pRtlGetNtVersionNumbers(&major, &minor, &gBuildNumber);
    gBuildNumber &= ~0xF0000000;

    HMODULE dwmapi = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dwmapi)
        pDwmSetWindowAttribute = reinterpret_cast<fnDwmSetWindowAttribute>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));

    // dark menus and title bars arrived with Windows 10 1809
    if (major < 10 || gBuildNumber < 17763)
        return;
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme == NULL)
        return;
    FARPROC ord135 = GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    if (gBuildNumber < 18362)
        pAllowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(ord135);
    else
        pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(ord135);
    pAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
    pFlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    pRefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(104)));
}

// Is Windows set to dark mode for apps (Settings > Personalization > Colors)?
static bool windowsAppsUseDarkMode()
{
    DWORD value = 1;
    DWORD len = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &len) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

static bool highContrastOn()
{
    HIGHCONTRASTW hc = { sizeof(hc) };
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

// Windows' contrast themes win out, as they're set for a reason
static bool computeDark()
{
    if (highContrastOn())
        return false;
    switch (gChoice) {
    case THEME_DARK:
        return true;
    case THEME_LIGHT:
        return false;
    default:
        return windowsAppsUseDarkMode();
    }
}

// popup menus (and the like) for the whole program
static void applyAppMode()
{
    if (pSetPreferredAppMode)
        pSetPreferredAppMode(gDark ? AppModeForceDark : AppModeForceLight);
    else if (pAllowDarkModeForApp)
        pAllowDarkModeForApp(gDark);
    if (pRefreshImmersiveColorPolicyState)
        pRefreshImmersiveColorPolicyState();
    if (pFlushMenuThemes)
        pFlushMenuThemes();
}

void DarkModeInit()
{
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, THEME_REGKEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD len = sizeof(value);
        DWORD type = 0;
        if (RegQueryValueExW(key, THEME_REGVALUE, NULL, &type, (LPBYTE)&value, &len) == ERROR_SUCCESS
            && type == REG_DWORD && value <= THEME_DARK) {
            gChoice = (int)value;
        }
        RegCloseKey(key);
    }
    loadThemeFunctions();
    gWindowBrush = CreateSolidBrush(DARK_WINDOW);
    gControlBrush = CreateSolidBrush(DARK_CONTROL);
    gDark = computeDark();
    applyAppMode();
}

int DarkModeGetChoice()
{
    return gChoice;
}

bool DarkModeSetChoice(int choice)
{
    assert(choice >= THEME_FOLLOW_WINDOWS && choice <= THEME_DARK);
    gChoice = choice;
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, THEME_REGKEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        DWORD value = (DWORD)choice;
        RegSetValueExW(key, THEME_REGVALUE, 0, REG_DWORD, (LPBYTE)&value, sizeof(value));
        RegCloseKey(key);
    }
    return DarkModeUpdateFromWindows();
}

bool DarkModeUpdateFromWindows()
{
    bool dark = computeDark();
    if (dark == gDark)
        return false;
    gDark = dark;
    applyAppMode();
    return true;
}

bool DarkModeIsOn()
{
    return gDark;
}

COLORREF DarkModeWindowColor()
{
    return gDark ? DARK_WINDOW : GetSysColor(COLOR_WINDOW);
}

COLORREF DarkModeTextColor()
{
    return gDark ? DARK_TEXT : GetSysColor(COLOR_WINDOWTEXT);
}

HBRUSH DarkModeWindowBrush()
{
    return gDark ? gWindowBrush : GetSysColorBrush(COLOR_WINDOW);
}

COLORREF DarkModeMapPendingColor()
{
    return gDark ? DARK_MAP_PENDING : RGB(255, 255, 255);
}

void DarkModeApplyToWindow(HWND hWnd)
{
    if (pAllowDarkModeForWindow)
        pAllowDarkModeForWindow(hWnd, gDark);
    if (pDwmSetWindowAttribute && gBuildNumber >= 17763) {
        // DWMWA_USE_IMMERSIVE_DARK_MODE, which was 19 before Windows 10 20H1
        DWORD attribute = (gBuildNumber >= 18985) ? 20 : 19;
        BOOL dark = gDark ? TRUE : FALSE;
        pDwmSetWindowAttribute(hWnd, attribute, &dark, sizeof(dark));
    }
    if (IsWindowVisible(hWnd)) {
        // redraw the title bar and menu bar now
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        DrawMenuBar(hWnd);
    }
}

// The menu bar's font, for drawing its items
static HFONT menuFont()
{
    if (gMenuFont == NULL) {
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            gMenuFont = CreateFontIndirectW(&ncm.lfMenuFont);
    }
    return gMenuFont;
}

static void fillSolidRect(HDC hdc, const RECT* pRect, COLORREF color)
{
    COLORREF oldColor = SetBkColor(hdc, color);
    ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, pRect, NULL, 0, NULL);
    SetBkColor(hdc, oldColor);
}

static void frameSolidRect(HDC hdc, const RECT* pRect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(hdc, pRect, brush);
    DeleteObject(brush);
}

// Windows draws a light line between the menu bar and the client area; cover it
static void drawMenuBarBottomLine(HWND hWnd)
{
    MENUBARINFO mbi = { sizeof(mbi) };
    if (!GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
        return;
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    MapWindowPoints(hWnd, NULL, (POINT*)&clientRect, 2);
    RECT windowRect;
    GetWindowRect(hWnd, &windowRect);
    OffsetRect(&clientRect, -windowRect.left, -windowRect.top);
    RECT line = clientRect;
    line.bottom = line.top;
    line.top--;
    HDC hdc = GetWindowDC(hWnd);
    FillRect(hdc, &line, gWindowBrush);
    ReleaseDC(hWnd, hdc);
}

bool DarkModeHandleMenuBar(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    if (!gDark)
        return false;
    switch (message) {
    case WM_UAHDRAWMENU:
    {
        UAHMENU* pUDM = (UAHMENU*)lParam;
        MENUBARINFO mbi = { sizeof(mbi) };
        if (!GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
            return false;
        RECT windowRect;
        GetWindowRect(hWnd, &windowRect);
        RECT bar = mbi.rcBar;
        OffsetRect(&bar, -windowRect.left, -windowRect.top);
        FillRect(pUDM->hdc, &bar, gWindowBrush);
        *pResult = 0;
        return true;
    }
    case WM_UAHDRAWMENUITEM:
    {
        UAHDRAWMENUITEM* pUDMI = (UAHDRAWMENUITEM*)lParam;
        wchar_t text[256] = L"";
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_STRING;
        mii.dwTypeData = text;
        mii.cch = _countof(text) - 1;
        GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

        UINT state = pUDMI->dis.itemState;
        COLORREF background = (state & (ODS_HOTLIGHT | ODS_SELECTED)) ? DARK_MENU_HOT : DARK_WINDOW;
        COLORREF textColor = (state & (ODS_INACTIVE | ODS_GRAYED | ODS_DISABLED)) ? DARK_DISABLED_TEXT : DARK_TEXT;
        UINT format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        if (state & ODS_NOACCEL)
            format |= DT_HIDEPREFIX;

        HDC hdc = pUDMI->um.hdc;
        fillSolidRect(hdc, &pUDMI->dis.rcItem, background);
        HGDIOBJ oldFont = menuFont() ? SelectObject(hdc, menuFont()) : NULL;
        int oldMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF oldColor = SetTextColor(hdc, textColor);
        DrawTextW(hdc, text, -1, &pUDMI->dis.rcItem, format);
        SetTextColor(hdc, oldColor);
        SetBkMode(hdc, oldMode);
        if (oldFont)
            SelectObject(hdc, oldFont);
        *pResult = 0;
        return true;
    }
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        *pResult = DefWindowProc(hWnd, message, wParam, lParam);
        drawMenuBarBottomLine(hWnd);
        return true;
    }
    return false;
}

LRESULT DarkModeTrackbarCustomDraw(LPNMCUSTOMDRAW pCustomDraw)
{
    if (!gDark)
        return CDRF_DODEFAULT;
    switch (pCustomDraw->dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        if (pCustomDraw->dwItemSpec == TBCD_CHANNEL) {
            fillSolidRect(pCustomDraw->hdc, &pCustomDraw->rc, DARK_CONTROL);
            frameSolidRect(pCustomDraw->hdc, &pCustomDraw->rc, DARK_BORDER);
            return CDRF_SKIPDEFAULT;
        }
        if (pCustomDraw->dwItemSpec == TBCD_THUMB) {
            COLORREF color = !IsWindowEnabled(pCustomDraw->hdr.hwndFrom) ? DARK_THUMB_DISABLED :
                (pCustomDraw->uItemState & CDIS_SELECTED) ? DARK_TEXT : DARK_THUMB;
            fillSolidRect(pCustomDraw->hdc, &pCustomDraw->rc, color);
            frameSolidRect(pCustomDraw->hdc, &pCustomDraw->rc, DARK_BORDER);
            return CDRF_SKIPDEFAULT;
        }
        break;
    }
    return CDRF_DODEFAULT;
}

// Subclass a window with newProc, keeping its old window procedure for CallWindowProc. Classic subclassing, as
// SetWindowSubclass() needs version 6 of the common controls, which Mineways doesn't use.
static void subclassWindow(HWND hWnd, WNDPROC newProc)
{
    if (GetPropW(hWnd, PROP_OLDPROC) != NULL)
        return;
    WNDPROC oldProc = (WNDPROC)SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)newProc);
    SetPropW(hWnd, PROP_OLDPROC, (HANDLE)oldProc);
}

// Call the window's old procedure; undo the subclassing when the window goes away
static LRESULT callOldProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC oldProc = (WNDPROC)GetPropW(hWnd, PROP_OLDPROC);
    assert(oldProc);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)oldProc);
        RemovePropW(hWnd, PROP_OLDPROC);
    }
    return CallWindowProcW(oldProc, hWnd, message, wParam, lParam);
}

// The classic status bar draws its text in the system's colors, so draw it all here
static void paintStatusBar(HWND hwndStatus)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwndStatus, &ps);
    RECT client;
    GetClientRect(hwndStatus, &client);
    FillRect(hdc, &client, gWindowBrush);

    HFONT font = (HFONT)SendMessageW(hwndStatus, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = SelectObject(hdc, font ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, DARK_TEXT);
    HPEN pen = CreatePen(PS_SOLID, 1, DARK_BORDER);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    int numParts = (int)SendMessageW(hwndStatus, SB_GETPARTS, 0, 0);
    for (int i = 0; i < numParts; i++) {
        RECT part;
        if (!SendMessageW(hwndStatus, SB_GETRECT, i, (LPARAM)&part))
            continue;
        int length = LOWORD(SendMessageW(hwndStatus, SB_GETTEXTLENGTHW, i, 0));
        std::vector<wchar_t> text(length + 1, 0);
        SendMessageW(hwndStatus, SB_GETTEXTW, i, (LPARAM)text.data());
        RECT textRect = part;
        textRect.left += 4;
        DrawTextW(hdc, text.data(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (i + 1 < numParts) {
            MoveToEx(hdc, part.right, part.top, NULL);
            LineTo(hdc, part.right, part.bottom);
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    SelectObject(hdc, oldFont);
    EndPaint(hwndStatus, &ps);
}

static LRESULT CALLBACK statusBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gDark) {
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_PAINT && wParam == 0) {
            paintStatusBar(hWnd);
            return 0;
        }
    }
    return callOldProc(hWnd, message, wParam, lParam);
}

// The classic progress bar has a light, sunken border, so draw it all here
static void paintProgressBar(HWND hwndProgress)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwndProgress, &ps);
    RECT client;
    GetClientRect(hwndProgress, &client);
    fillSolidRect(hdc, &client, DARK_CONTROL);
    int low = (int)SendMessageW(hwndProgress, PBM_GETRANGE, TRUE, 0);
    int high = (int)SendMessageW(hwndProgress, PBM_GETRANGE, FALSE, 0);
    int pos = (int)SendMessageW(hwndProgress, PBM_GETPOS, 0, 0);
    if (high > low && pos > low) {
        RECT bar = client;
        InflateRect(&bar, -2, -2);
        bar.right = bar.left + (int)((long long)(bar.right - bar.left) * (min(pos, high) - low) / (high - low));
        fillSolidRect(hdc, &bar, DARK_PROGRESS);
    }
    frameSolidRect(hdc, &client, DARK_BORDER);
    EndPaint(hwndProgress, &ps);
}

static LRESULT CALLBACK progressBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gDark) {
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_PAINT && wParam == 0) {
            paintProgressBar(hWnd);
            return 0;
        }
        if (message == WM_NCPAINT) {
            // the progress bar gives itself a light, one pixel static edge
            LRESULT result = callOldProc(hWnd, message, wParam, lParam);
            RECT rect;
            GetWindowRect(hWnd, &rect);
            OffsetRect(&rect, -rect.left, -rect.top);
            HDC hdc = GetWindowDC(hWnd);
            frameSolidRect(hdc, &rect, DARK_BORDER);
            ReleaseDC(hWnd, hdc);
            return result;
        }
    }
    return callOldProc(hWnd, message, wParam, lParam);
}

// Static text: disabled text is drawn embossed, in light colors, whatever its parent says, so draw it here
static LRESULT CALLBACK labelProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gDark && message == WM_PAINT && wParam == 0) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT client;
        GetClientRect(hWnd, &client);
        // the parent's background, as WM_CTLCOLORSTATIC would give
        fillSolidRect(hdc, &client, DARK_WINDOW);
        wchar_t text[1024] = L"";
        GetWindowTextW(hWnd, text, _countof(text));
        // a static control with no font set uses the system font
        HFONT font = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
        HGDIOBJ oldFont = SelectObject(hdc, font ? (HGDIOBJ)font : GetStockObject(SYSTEM_FONT));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, IsWindowEnabled(hWnd) ? DARK_TEXT : DARK_DISABLED_TEXT);
        LONG style = GetWindowLongW(hWnd, GWL_STYLE);
        LONG type = style & SS_TYPEMASK;
        UINT format = (type == SS_RIGHT) ? DT_RIGHT : (type == SS_CENTER) ? DT_CENTER : DT_LEFT;
        format |= (type == SS_LEFTNOWORDWRAP) ? DT_SINGLELINE : DT_WORDBREAK;
        if (style & SS_NOPREFIX)
            format |= DT_NOPREFIX;
        DrawTextW(hdc, text, -1, &client, format | DT_EXPANDTABS);
        SelectObject(hdc, oldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    return callOldProc(hWnd, message, wParam, lParam);
}

void DarkModeApplyToLabel(HWND hwndLabel)
{
    subclassWindow(hwndLabel, labelProc);
    InvalidateRect(hwndLabel, NULL, TRUE);
}

// How many pixels wide a window's border is, from WS_BORDER and the 3D edge extended styles
static int borderWidth(HWND hWnd)
{
    LONG style = GetWindowLongW(hWnd, GWL_STYLE);
    LONG exStyle = GetWindowLongW(hWnd, GWL_EXSTYLE);
    return ((style & WS_BORDER) ? 1 : 0) + ((exStyle & WS_EX_CLIENTEDGE) ? 2 : 0) + ((exStyle & WS_EX_STATICEDGE) ? 1 : 0);
}

// Edit fields and lists: the classic border is drawn in light colors, so draw over it
static LRESULT CALLBACK borderProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gDark && message == WM_NCPAINT) {
        LRESULT result = callOldProc(hWnd, message, wParam, lParam);
        RECT rect;
        GetWindowRect(hWnd, &rect);
        OffsetRect(&rect, -rect.left, -rect.top);
        HDC hdc = GetWindowDC(hWnd);
        int width = borderWidth(hWnd);
        for (int i = 0; i < width; i++) {
            frameSolidRect(hdc, &rect, (i == 0) ? DARK_BORDER : DARK_CONTROL);
            InflateRect(&rect, -1, -1);
        }
        ReleaseDC(hWnd, hdc);
        return result;
    }
    return callOldProc(hWnd, message, wParam, lParam);
}

void DarkModeApplyToStatusBar(HWND hwndStatus, HWND hwndProgress)
{
    subclassWindow(hwndStatus, statusBarProc);
    if (hwndProgress) {
        subclassWindow(hwndProgress, progressBarProc);
        InvalidateRect(hwndProgress, NULL, TRUE);
    }
    InvalidateRect(hwndStatus, NULL, TRUE);
}

// Draw a push button we've made owner drawn (see themeDialogChild)
static void drawPushButton(HWND hDlg, const DRAWITEMSTRUCT* pDis)
{
    HDC hdc = pDis->hDC;
    RECT rect = pDis->rcItem;
    bool pressed = (pDis->itemState & ODS_SELECTED) != 0;
    bool disabled = (pDis->itemState & ODS_DISABLED) != 0;
    LRESULT defId = SendMessageW(hDlg, DM_GETDEFID, 0, 0);
    bool isDefault = (HIWORD(defId) == DC_HASDEFID) && (LOWORD(defId) == pDis->CtlID);

    fillSolidRect(hdc, &rect, pressed ? DARK_BUTTON_PRESSED : DARK_BUTTON);
    frameSolidRect(hdc, &rect, (isDefault && !disabled) ? DARK_DEFAULT_BORDER : DARK_BORDER);

    wchar_t text[256] = L"";
    GetWindowTextW(pDis->hwndItem, text, _countof(text));
    HFONT font = (HFONT)SendMessageW(pDis->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? DARK_DISABLED_TEXT : DARK_TEXT);
    RECT textRect = rect;
    if (pressed)
        OffsetRect(&textRect, 1, 1);
    UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    if (pDis->itemState & ODS_NOACCEL)
        format |= DT_HIDEPREFIX;
    DrawTextW(hdc, text, -1, &textRect, format);
    if (oldFont)
        SelectObject(hdc, oldFont);

    if ((pDis->itemState & ODS_FOCUS) && !(pDis->itemState & ODS_NOFOCUSRECT)) {
        RECT focusRect = rect;
        InflateRect(&focusRect, -3, -3);
        SetTextColor(hdc, DARK_TEXT);
        DrawFocusRect(hdc, &focusRect);
    }
}

// A list's column header: the classic one is drawn in the system's colors
static LRESULT headerCustomDraw(LPNMCUSTOMDRAW pCustomDraw)
{
    switch (pCustomDraw->dwDrawStage) {
    case CDDS_PREPAINT:
    {
        RECT client;
        GetClientRect(pCustomDraw->hdr.hwndFrom, &client);
        fillSolidRect(pCustomDraw->hdc, &client, DARK_HEADER);
        return CDRF_NOTIFYITEMDRAW;
    }
    case CDDS_ITEMPREPAINT:
    {
        HDC hdc = pCustomDraw->hdc;
        RECT rect = pCustomDraw->rc;
        fillSolidRect(hdc, &rect, DARK_HEADER);
        RECT divider = { rect.right - 1, rect.top, rect.right, rect.bottom };
        fillSolidRect(hdc, &divider, DARK_BORDER);
        RECT bottom = { rect.left, rect.bottom - 1, rect.right, rect.bottom };
        fillSolidRect(hdc, &bottom, DARK_BORDER);

        wchar_t text[256] = L"";
        HDITEMW item = { 0 };
        item.mask = HDI_TEXT;
        item.pszText = text;
        item.cchTextMax = _countof(text);
        SendMessageW(pCustomDraw->hdr.hwndFrom, HDM_GETITEMW, pCustomDraw->dwItemSpec, (LPARAM)&item);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, DARK_TEXT);
        rect.left += 6;
        DrawTextW(hdc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static bool isClass(HWND hWnd, const wchar_t* className)
{
    wchar_t name[64];
    return GetClassNameW(hWnd, name, _countof(name)) && _wcsicmp(name, className) == 0;
}

static BOOL CALLBACK removeOwnerDrawnProp(HWND hChild, LPARAM)
{
    RemovePropW(hChild, PROP_OWNERDRAWN);
    return TRUE;
}

static LRESULT CALLBACK dialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gDark) {
        switch (message) {
        case WM_CTLCOLORDLG:
            return (LRESULT)gWindowBrush;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            // static text, check boxes, radio buttons, group boxes; and read-only or disabled edit fields
            HDC hdc = (HDC)wParam;
            HWND control = (HWND)lParam;
            bool field = isClass(control, L"Edit");
            SetTextColor(hdc, IsWindowEnabled(control) ? DARK_TEXT : DARK_DISABLED_TEXT);
            SetBkColor(hdc, field ? DARK_CONTROL : DARK_WINDOW);
            return (LRESULT)(field ? gControlBrush : gWindowBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DARK_TEXT);
            SetBkColor(hdc, DARK_CONTROL);
            return (LRESULT)gControlBrush;
        }
        case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* pDis = (const DRAWITEMSTRUCT*)lParam;
            if (pDis->CtlType == ODT_BUTTON && GetPropW(pDis->hwndItem, PROP_OWNERDRAWN)) {
                drawPushButton(hDlg, pDis);
                return TRUE;
            }
            break;
        }
        case WM_NOTIFY:
        {
            LPNMHDR pHdr = (LPNMHDR)lParam;
            if (pHdr->code == NM_CUSTOMDRAW && isClass(pHdr->hwndFrom, WC_HEADERW))
                return headerCustomDraw((LPNMCUSTOMDRAW)lParam);
            break;
        }
        case WM_ERASEBKGND:
            // a message box paints its own light band behind its buttons
            if (GetPropW(hDlg, PROP_MESSAGEBOX)) {
                RECT client;
                GetClientRect(hDlg, &client);
                FillRect((HDC)wParam, &client, gWindowBrush);
                return 1;
            }
            break;
        case WM_PAINT:
            if (GetPropW(hDlg, PROP_MESSAGEBOX) && wParam == 0) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hDlg, &ps);
                FillRect(hdc, &ps.rcPaint, gWindowBrush);
                EndPaint(hDlg, &ps);
                return 0;
            }
            break;
        }
    }
    if (message == WM_DESTROY) {
        EnumChildWindows(hDlg, removeOwnerDrawnProp, 0);
        RemovePropW(hDlg, PROP_MESSAGEBOX);
    }
    return callOldProc(hDlg, message, wParam, lParam);
}

static BOOL CALLBACK themeDialogChild(HWND hChild, LPARAM lParam)
{
    HWND hDlg = (HWND)lParam;
    if (isClass(hChild, L"Button")) {
        // Classic push buttons are drawn in the system's colors whatever their parent says, so they're made owner drawn,
        // which they otherwise act the same as.
        LONG style = GetWindowLongW(hChild, GWL_STYLE);
        LONG type = style & BS_TYPEMASK;
        if ((type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) && GetParent(hChild) == hDlg) {
            if (type == BS_DEFPUSHBUTTON)
                // note the default button, as an owner drawn button can't be one; done before changing the style, as
                // DM_SETDEFID sets the button's style
                SendMessageW(hDlg, DM_SETDEFID, GetDlgCtrlID(hChild), 0);
            SetPropW(hChild, PROP_OWNERDRAWN, (HANDLE)1);
            SetWindowLongW(hChild, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
            InvalidateRect(hChild, NULL, TRUE);
        }
    }
    else if (isClass(hChild, L"Static")) {
        LONG type = GetWindowLongW(hChild, GWL_STYLE) & SS_TYPEMASK;
        if (type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT || type == SS_LEFTNOWORDWRAP)
            DarkModeApplyToLabel(hChild);
    }
    else if (isClass(hChild, WC_LISTVIEWW)) {
        SendMessageW(hChild, LVM_SETBKCOLOR, 0, (LPARAM)DARK_CONTROL);
        SendMessageW(hChild, LVM_SETTEXTBKCOLOR, 0, (LPARAM)DARK_CONTROL);
        SendMessageW(hChild, LVM_SETTEXTCOLOR, 0, (LPARAM)DARK_TEXT);
        // the list passes its header's custom draw notifications on to the dialog
    }
    else if (isClass(hChild, PROGRESS_CLASSW)) {
        subclassWindow(hChild, progressBarProc);
    }
    if ((isClass(hChild, L"Edit") || isClass(hChild, L"ListBox") || isClass(hChild, WC_LISTVIEWW)) &&
        borderWidth(hChild) > 0) {
        subclassWindow(hChild, borderProc);
        SetWindowPos(hChild, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    return TRUE;
}

void DarkModeApplyToDialog(HWND hDlg)
{
    if (!gDark)
        return;
    DarkModeApplyToWindow(hDlg);
    subclassWindow(hDlg, dialogProc);
    EnumChildWindows(hDlg, themeDialogChild, (LPARAM)hDlg);
}

// MessageBox() makes its dialog box itself, so catch it as it appears
static HHOOK gMessageBoxHook = NULL;
static LRESULT CALLBACK messageBoxHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE && isClass((HWND)wParam, L"#32770")) {
        SetPropW((HWND)wParam, PROP_MESSAGEBOX, (HANDLE)1);
        DarkModeApplyToDialog((HWND)wParam);
    }
    return CallNextHookEx(gMessageBoxHook, code, wParam, lParam);
}

int DarkModeMessageBox(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    if (!gDark)
        return MessageBoxW(hWnd, lpText, lpCaption, uType);
    HHOOK previousHook = gMessageBoxHook;
    gMessageBoxHook = SetWindowsHookExW(WH_CBT, messageBoxHookProc, NULL, GetCurrentThreadId());
    int retval = MessageBoxW(hWnd, lpText, lpCaption, uType);
    if (gMessageBoxHook)
        UnhookWindowsHookEx(gMessageBoxHook);
    gMessageBoxHook = previousHook;
    return retval;
}
