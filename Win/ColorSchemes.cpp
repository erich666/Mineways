/*
Copyright (c) 2010, Sean Kasun
All rights reserved.
Modified by Eric Haines, copyright (c) 2011.

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
#include <CommDlg.h>
#include "Resource.h"
#include "ColorSchemes.h"

#define NO_COLOR_ENTRY 0x000100FF
// magenta: tips off user that his color scheme is out of date
#define MISSING_COLOR_ENTRY 0xFF00FFFF

static wchar_t gLastItemSelected[255];

static int gLocalCountForNames = 1;

#define COLORKEY L"Software\\Eric Haines\\Mineways\\ColorSchemes"

ColorManager::ColorManager()
{
    RegCreateKeyEx(HKEY_CURRENT_USER, COLORKEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &key, NULL);
}
ColorManager::~ColorManager()
{
    RegCloseKey(key);
}

void ColorManager::Init(ColorScheme* cs)
{
    int i;
    for (i = 0; i < NUM_BLOCKS_CS; i++)
    {
        cs->colors[i] = blockColor(i);
    }
    // fill in rest, if NUM_BLOCKS_CS is higher than NUM_BLOCKS
    // TODO - never needed now. Someday we might go to a different scheme, based on names of blocks.
    //for (i = NUM_BLOCKS; i < NUM_BLOCKS_CS; i++)
    //{
    //    // fill the rest with almost-black; if we detect this color on loading
    //    // a color scheme in a place that should have been a normal color, then
    //    // we know the old color scheme is out of date and should be fixed on load.
    //    cs->colors[i] = NO_COLOR_ENTRY;
    //}
}
void ColorManager::create(ColorScheme* cs)
{
    DWORD schemeId, len;
    len = sizeof(schemeId);
    long result = RegQueryValueEx(key, L"schemeId", NULL, NULL, (LPBYTE)&schemeId, &len);
    if (result == ERROR_FILE_NOT_FOUND)
        schemeId = 0;
    schemeId++;
    RegSetValueEx(key, L"schemeId", NULL, REG_DWORD, (LPBYTE)&schemeId, sizeof(schemeId));
    ColorManager::Init(cs);
    cs->id = schemeId;
    save(cs);
}
int ColorManager::next(int id, ColorScheme* cs)
{
    TCHAR name[50];
    DWORD nameLen;
    DWORD type, csLen;
    for (;; )
    {
        nameLen = 50;
        csLen = sizeof(ColorScheme);
        LONG result = RegEnumValue(key, id, name, &nameLen, NULL, &type, (LPBYTE)cs, &csLen);
        id++;
        if (result == ERROR_NO_MORE_ITEMS)
            return 0;
        if (type == REG_BINARY)
            return id;
    }
}
void ColorManager::save(ColorScheme* cs)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", cs->id);
    RegSetValueEx(key, keyname, NULL, REG_BINARY, (LPBYTE)cs, sizeof(ColorScheme));
}
void ColorManager::load(ColorScheme* cs)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", cs->id);
    DWORD csLen = sizeof(ColorScheme);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)cs, &csLen);

    // are we upgrading from an old scheme to more blocks? Check all blocks read in for valid colors.
    // 0xcccccccc is a bad color
    bool corrected = false;
    for (int i = 0; i < NUM_BLOCKS_CS; i++) {
        // detect common "no color saved" cases, release and debug, and then assign
        if (cs->colors[i] == 0 || cs->colors[i] == 0xcccccccc || cs->colors[i] == NO_COLOR_ENTRY) {
            cs->colors[i] = blockColor(i);
            corrected = true;
        }
    }

    // if we made changes, i.e., added default colors for missing entries, save the new version
    if (corrected) {
        save(cs);
    }

    /* old code that is no longer needed, since we go well past BLOCK_UNKNOWN:
    // find out how many entries are invalid and fix these by shifting up
    int endLoc = BLOCK_UNKNOWN;
    // Find unknown block entry (for backwards compatibility)
    unsigned int color = blockColor(BLOCK_UNKNOWN);

    // go back through list until unknown block is found. Don't go back past say snow.
    // ignore alpha, because that could get zero'ed.
    while (endLoc > BLOCK_SNOW && (cs->colors[endLoc] & 0xffffff00) != (color & 0xffffff00))
    {
        endLoc--;
    }

    // endLoc is the location of the unknown block in the saved color scheme.
    // Check if a correction is needed, i.e. new blocks have been added.
    if (endLoc < BLOCK_UNKNOWN && endLoc > BLOCK_SNOW)
    {
        // Mind the gap! Move the unknown block up some slots, fill in these slots
        // with missing entries
        int gap = BLOCK_UNKNOWN - endLoc;
        int i;
        // set unknown block attribute to whatever it was before
        cs->colors[endLoc + gap] = cs->colors[endLoc];

        // set "empty" blocks with new default data
        for (i = 0; i < gap; i++)
        {
            // set where unknown block was, on up to end
            cs->colors[endLoc + i] = blockColor(endLoc + i);
        }
        save(cs);
    }
    */
}
// Already has name and data fields, along with unique ID; just copy colors.
// You should load the cs and csSource before calling to ensure they're the same size,
// BLOCK_UNKNOWN
void ColorManager::copy(ColorScheme* cs, ColorScheme* csSource)
{
    wchar_t keyname[50];
    DWORD csLen = sizeof(ColorScheme);

    swprintf(keyname, 50, L"scheme %d", cs->id);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)cs, &csLen);

    swprintf(keyname, 50, L"scheme %d", csSource->id);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)csSource, &csLen);

    for (int i = 0; i < NUM_BLOCKS_CS; i++)
    {
        // copy color
        cs->colors[i] = csSource->colors[i];
    }
    save(cs);
}

void ColorManager::remove(int id)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", id);
    RegDeleteValue(key, keyname);
}
unsigned int ColorManager::blockColor(int type)
{
    unsigned int color = gBlockDefinitions[type].read_color;
    unsigned char r, g, b, a;
    r = (unsigned char)(color >> 16);
    g = (unsigned char)(color >> 8);
    b = (unsigned char)(color);
    float alpha = gBlockDefinitions[type].read_alpha;
    // we used to unmultiply. Now we store the unmultiplied color in gBlockDefinitions
    //r=(unsigned char)(r/alpha);
    //g=(unsigned char)(g/alpha);
    //b=(unsigned char)(b/alpha);
    a = (unsigned char)(alpha * 255);
    color = (r << 24) | (g << 16) | (b << 8) | a;

    return color;
}

INT_PTR CALLBACK ColorSchemes(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ColorSchemeEdit(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

void doColorSchemes(HINSTANCE hInst, HWND hWnd)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_COLORSCHEMES), hWnd, ColorSchemes);
}

wchar_t* getSelectedColorScheme()
{
    return gLastItemSelected;
}

static void validateButtons(HWND hDlg);
static ColorScheme curCS;

INT_PTR CALLBACK ColorSchemes(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND list;
    UNREFERENCED_PARAMETER(lParam);
    ColorScheme cs;
    ColorManager cm;
    switch (message)
    {
    case WM_INITDIALOG:
        {
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int id = cm.next(0, &cs);
            while (id)
            {
                int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
                SendMessage(list, LB_SETITEMDATA, pos, cs.id);
                id = cm.next(id, &cs);
                // get a "likely to be unique" number if the user adds a scheme.
                // not foolproof, we should really check all cs.names to avoid a
                // match, but better than starting at 1.
                gLocalCountForNames++;
            }
            validateButtons(hDlg);
            wcsncpy_s(gLastItemSelected, 255, L"", 1);
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_SCHEMELIST:
            switch (HIWORD(wParam))
            {
            case LBN_SELCHANGE:
            case LBN_SELCANCEL:
                validateButtons(hDlg);
                break;
            }
            break;
        case IDC_EDIT:
        {
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            curCS.id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);
            cm.load(&curCS);
            DialogBox(NULL, MAKEINTRESOURCE(IDD_COLORSCHEME), hDlg, ColorSchemeEdit);
            cm.save(&curCS);
            SendMessage(list, LB_DELETESTRING, item, 0);
            int pos = (int)SendMessage(list, LB_INSERTSTRING, item, (LPARAM)curCS.name);
            SendMessage(list, LB_SETITEMDATA, pos, curCS.id);
            // keep the item selected
            SendMessage(list, LB_SETCURSEL, item, 0);
        }
        break;
        case IDC_COPY:
        {
            // select the highlighted scheme
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            curCS.id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);

            // this ensures the source is the proper size in blocks, same as the new one
            cm.load(&curCS);
            swprintf_s(cs.name, 255, L"%ls %d", curCS.name, gLocalCountForNames);
            gLocalCountForNames++;
            // create the new one
            cm.create(&cs);
            // and copy it over
            cm.copy(&cs, &curCS);
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
            SendMessage(list, LB_SETITEMDATA, pos, cs.id);
            validateButtons(hDlg);
        }
        break;
        case IDC_ADD:
        {
            // all color schemes use the same name by default.
            swprintf_s(cs.name, 255, L"Color Scheme %d", gLocalCountForNames);
            gLocalCountForNames++;
            cm.create(&cs);
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
            SendMessage(list, LB_SETITEMDATA, pos, cs.id);
            validateButtons(hDlg);
        }
        break;
        case IDC_REMOVE:
        {
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            if (item != LB_ERR)
            {
                int id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);
                cm.remove(id);
                SendMessage(list, LB_DELETESTRING, item, 0);
            }
            validateButtons(hDlg);
        }
        break;
        // There's no real cancel, it's the same as OK
        case IDOK:
        case IDCANCEL:
        {
            list = GetDlgItem(hDlg, IDC_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            if (item == LB_ERR)
            {
                wcsncpy_s(gLastItemSelected, 255, L"", 1);
            }
            else
            {
                curCS.id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);
                cm.load(&curCS);
                wcsncpy_s(gLastItemSelected, 255, curCS.name, 255);
            }
        }
        EndDialog(hDlg, LOWORD(wParam));
        return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
static void validateButtons(HWND hDlg)
{
    HWND edit = GetDlgItem(hDlg, IDC_EDIT);
    HWND copy = GetDlgItem(hDlg, IDC_COPY);
    HWND remove = GetDlgItem(hDlg, IDC_REMOVE);
    HWND list = GetDlgItem(hDlg, IDC_SCHEMELIST);
    int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
    if (item == LB_ERR)
    {
        EnableWindow(edit, FALSE);
        EnableWindow(copy, FALSE);
        EnableWindow(remove, FALSE);
    }
    else
    {
        EnableWindow(edit, TRUE);
        EnableWindow(copy, TRUE);
        EnableWindow(remove, TRUE);
    }
}

INT_PTR CALLBACK ColorSchemeEdit(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int curSel;
    static wchar_t row[255];
    NMLVDISPINFO* info;
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetDlgItemText(hDlg, IDC_SCHEMENAME, curCS.name);

        HWND ctl = GetDlgItem(hDlg, IDC_CURCOLOR);
        EnableWindow(ctl, FALSE);
        ctl = GetDlgItem(hDlg, IDC_CURALPHA);
        EnableWindow(ctl, FALSE);

        HWND lv = GetDlgItem(hDlg, IDC_COLORLIST);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT);

        LVCOLUMN lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
        for (int i = 0; i < 4; i++)
        {
            lvc.iSubItem = i;
            switch (i)
            {
            case 0:
                lvc.pszText = L"Id";
                lvc.cx = 40;
                break;
            case 1:
                lvc.pszText = L"Name";
                lvc.cx = 200;
                break;
            case 2:
                lvc.pszText = L"Color";
                lvc.cx = 80;
                break;
            case 3:
                lvc.pszText = L"Alpha";
                lvc.cx = 50;
                break;
            }
            lvc.fmt = LVCFMT_LEFT;
            ListView_InsertColumn(lv, i, &lvc);
        }
        LVITEM item;
        item.mask = LVIF_TEXT | LVIF_STATE;
        item.iSubItem = 0;
        item.state = 0;
        item.stateMask = 0;
        item.pszText = LPSTR_TEXTCALLBACK;
        for (int i = 0; i < NUM_BLOCKS_CS; i++)
        {
            item.iItem = i;
            ListView_InsertItem(lv, &item);
        }
    }
    return (INT_PTR)TRUE;
    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case LVN_ITEMCHANGED:
        {
            LPNMLISTVIEW item = (LPNMLISTVIEW)lParam;
            if (item->uNewState & LVIS_SELECTED)
            {
                curSel = item->iItem;
                swprintf(row, 255, L"#%06x", curCS.colors[item->iItem] >> 8);
                SetDlgItemText(hDlg, IDC_CURCOLOR, row);
                swprintf(row, 255, L"%d", (int)curCS.colors[item->iItem] & 0xff);
                SetDlgItemText(hDlg, IDC_CURALPHA, row);
                HWND ctl = GetDlgItem(hDlg, IDC_CURCOLOR);
                EnableWindow(ctl, TRUE);
                ctl = GetDlgItem(hDlg, IDC_CURALPHA);
                EnableWindow(ctl, TRUE);
            }
        }
        break;
        case LVN_GETDISPINFO:
            info = (NMLVDISPINFO*)lParam;
            switch (info->item.iSubItem)
            {
            case 0:
                swprintf(row, 255, L"%d.", info->item.iItem);
                break;
            case 1:
                swprintf(row, 255, L"%S", gBlockDefinitions[info->item.iItem].name);	// char to wchar
                break;
            case 2:
                swprintf(row, 255, L"#%06x", curCS.colors[info->item.iItem] >> 8);
                break;
            case 3:
                swprintf(row, 255, L"%d", (int)curCS.colors[info->item.iItem] & 0xff);
                break;
            }
            info->item.pszText = row;
            break;
        }
        break;
    case WM_COMMAND:
        switch (HIWORD(wParam))
        {
        case EN_CHANGE:
            switch (LOWORD(wParam))
            {
            case IDC_SCHEMENAME:
                GetDlgItemText(hDlg, IDC_SCHEMENAME, curCS.name, 255);
                break;
            case IDC_CURCOLOR:
            {
                HWND lv = GetDlgItem(hDlg, IDC_COLORLIST);
                GetDlgItemText(hDlg, IDC_CURCOLOR, row, 255);
                unsigned int color = 0;
                for (int i = 0; row[i]; i++)
                {
                    if (row[i] >= '0' && row[i] <= '9')
                    {
                        color <<= 4;
                        color |= row[i] - '0';
                    }
                    if (row[i] >= 'a' && row[i] <= 'f')
                    {
                        color <<= 4;
                        color |= row[i] + 10 - 'a';
                    }
                    if (row[i] >= 'A' && row[i] <= 'F')
                    {
                        color <<= 4;
                        color |= row[i] + 10 - 'A';
                    }
                }
                curCS.colors[curSel] &= 0xff;
                curCS.colors[curSel] |= color << 8;
                ListView_RedrawItems(lv, curSel, curSel);
            }
            break;
            case IDC_CURALPHA:
            {
                HWND lv = GetDlgItem(hDlg, IDC_COLORLIST);
                curCS.colors[curSel] &= ~0xff;
                curCS.colors[curSel] |= GetDlgItemInt(hDlg, IDC_CURALPHA, NULL, FALSE) & 0xff;
                ListView_RedrawItems(lv, curSel, curSel);
            }
            }
            break;
        }
        switch (LOWORD(wParam))
        {
        case IDC_HIDE_ALL_BLOCKS:
        {
            // set all blocks' alphas to zero. Useful for printing out just one material
            for (int i = 0; i < NUM_BLOCKS_CS; i++)
            {
                curCS.colors[i] &= ~0xff;
            }
            HWND lv = GetDlgItem(hDlg, IDC_COLORLIST);
            ListView_RedrawItems(lv, 0, NUM_BLOCKS_CS - 1);
        }
        break;
        case IDC_HIDE_TREE_BLOCKS:
        {
            // set tree-related blocks zero. Useful for removing trees from large terrain prints (trees are hard to print well).
            curCS.colors[BLOCK_LOG] &= ~0xff;
            curCS.colors[BLOCK_AD_LOG] &= ~0xff;
            curCS.colors[BLOCK_LEAVES] &= ~0xff;
            curCS.colors[BLOCK_AD_LEAVES] &= ~0xff;

            HWND lv = GetDlgItem(hDlg, IDC_COLORLIST);
            ListView_RedrawItems(lv, 0, NUM_BLOCKS_CS - 1);
        }
        break;
        case IDCANCEL:
        case IDOK:
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        }
        break;
    }
    return (INT_PTR)FALSE;
}