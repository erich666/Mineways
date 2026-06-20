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
#include <CommDlg.h>
#include "Resource.h"
#include "CullingSchemes.h"
#include "nbt.h"	// blockTransCount / blockTransNameAt / blockTransIndexFor
#include "MinewaysMap.h"	// InvalidateMapRenderCache
#include "blockInfo.h"	// BLOCK_BARRIER, BLOCK_STRUCTURE_VOID

// =============================================================================================
// Runtime cull lookup
// =============================================================================================
// gIsCulledByIndex[i] = 1 if BlockTranslations[i] is currently culled. Populated by
// applyCullingScheme(); read by isBlockCulled() via blockTransIndexFor() reverse lookup.
static unsigned char gIsCulledByIndex[NUM_CULL_ENTRIES] = { 0 };
static bool gAnyCulled = false;

// Seed the "always cull by default" set into a culled[] array. These are technical/utility
// blocks (barrier, structure_void) that users essentially never want to see in either the map
// view or in exports — the Standard culling scheme also enforces this so they don't appear
// even with no user scheme active.
static void seedDefaultCulled(unsigned char* culled)
{
    int barrierIdx = blockTransIndexFor(BLOCK_BARRIER, 0);
    if (barrierIdx >= 0 && barrierIdx < NUM_CULL_ENTRIES) culled[barrierIdx] = 1;
    int voidIdx = blockTransIndexFor(BLOCK_STRUCTURE_VOID, 0);
    if (voidIdx >= 0 && voidIdx < NUM_CULL_ENTRIES) culled[voidIdx] = 1;
}

void applyCullingScheme(const unsigned char* culled)
{
    if (culled == NULL) {
        // "Standard" — no user scheme active, but still hide the always-cull defaults.
        memset(gIsCulledByIndex, 0, sizeof(gIsCulledByIndex));
        seedDefaultCulled(gIsCulledByIndex);
    } else {
        memcpy(gIsCulledByIndex, culled, NUM_CULL_ENTRIES);
    }
    gAnyCulled = false;
    for (int i = 0; i < NUM_CULL_ENTRIES; i++) {
        if (gIsCulledByIndex[i]) { gAnyCulled = true; break; }
    }
    // Force the per-chunk render cache to miss on the next drawTheMap(). drawInvalidateUpdate
    // alone only repaints the window from the cached bitmap, which here is now stale.
    InvalidateMapRenderCache();
}

bool isBlockCulled(int type, int dataVal)
{
    if (!gAnyCulled) return false;
    int idx = blockTransIndexFor(type, dataVal);
    if (idx < 0 || idx >= NUM_CULL_ENTRIES) return false;
    return gIsCulledByIndex[idx] != 0;
}

// =============================================================================================
// Registry persistence (CullingManager) — parallel to ColorManager
// =============================================================================================
#define CULLKEY L"Software\\Eric Haines\\Mineways\\CullingSchemes"

static wchar_t gLastItemSelected[255];
static int gLocalCountForNames = 1;

CullingManager::CullingManager()
{
    RegCreateKeyEx(HKEY_CURRENT_USER, CULLKEY, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &key, NULL);
}
CullingManager::~CullingManager()
{
    RegCloseKey(key);
}

void CullingManager::Init(CullingScheme* cs)
{
    memset(cs->culled, 0, sizeof(cs->culled));
    // Pre-check the "almost never wanted in an export" technical blocks (barrier,
    // structure_void). Users creating a fresh culling scheme almost always want these out of
    // the way; they can be unchecked in the editor. Same set is enforced by the Standard
    // scheme via applyCullingScheme(NULL).
    seedDefaultCulled(cs->culled);
}

void CullingManager::create(CullingScheme* cs)
{
    DWORD schemeId, len;
    len = sizeof(schemeId);
    long result = RegQueryValueEx(key, L"schemeId", NULL, NULL, (LPBYTE)&schemeId, &len);
    if (result == ERROR_FILE_NOT_FOUND)
        schemeId = 0;
    schemeId++;
    RegSetValueEx(key, L"schemeId", NULL, REG_DWORD, (LPBYTE)&schemeId, sizeof(schemeId));
    CullingManager::Init(cs);
    cs->id = schemeId;
    save(cs);
}

int CullingManager::next(int id, CullingScheme* cs)
{
    TCHAR name[50];
    DWORD nameLen;
    DWORD type, csLen;
    for (;;)
    {
        nameLen = 50;
        csLen = sizeof(CullingScheme);
        LONG result = RegEnumValue(key, id, name, &nameLen, NULL, &type, (LPBYTE)cs, &csLen);
        id++;
        if (result == ERROR_NO_MORE_ITEMS) return 0;
        if (type == REG_BINARY) return id;
    }
}

void CullingManager::save(CullingScheme* cs)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", cs->id);
    RegSetValueEx(key, keyname, NULL, REG_BINARY, (LPBYTE)cs, sizeof(CullingScheme));
}

void CullingManager::load(CullingScheme* cs)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", cs->id);
    DWORD csLen = sizeof(CullingScheme);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)cs, &csLen);
}

void CullingManager::copy(CullingScheme* cs, CullingScheme* csSource)
{
    wchar_t keyname[50];
    DWORD csLen = sizeof(CullingScheme);

    swprintf(keyname, 50, L"scheme %d", cs->id);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)cs, &csLen);

    swprintf(keyname, 50, L"scheme %d", csSource->id);
    RegQueryValueEx(key, keyname, NULL, NULL, (LPBYTE)csSource, &csLen);

    memcpy(cs->culled, csSource->culled, sizeof(cs->culled));
    save(cs);
}

void CullingManager::remove(int id)
{
    wchar_t keyname[50];
    swprintf(keyname, 50, L"scheme %d", id);
    RegDeleteValue(key, keyname);
}

// =============================================================================================
// Dialog procs
// =============================================================================================
INT_PTR CALLBACK CullingSchemes(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK CullingSchemeEdit(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

void doCullingSchemes(HINSTANCE hInst, HWND hWnd)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_CULLINGSCHEMES), hWnd, CullingSchemes);
}

wchar_t* getSelectedCullingScheme()
{
    return gLastItemSelected;
}

static void validateButtons(HWND hDlg);
static CullingScheme curCS;

INT_PTR CALLBACK CullingSchemes(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND list;
    UNREFERENCED_PARAMETER(lParam);
    CullingScheme cs;
    CullingManager cm;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
        int id = cm.next(0, &cs);
        while (id)
        {
            int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
            SendMessage(list, LB_SETITEMDATA, pos, cs.id);
            id = cm.next(id, &cs);
            gLocalCountForNames++;
        }
        validateButtons(hDlg);
        wcsncpy_s(gLastItemSelected, 255, L"", 1);
    }
    return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_CULL_SCHEMELIST:
            switch (HIWORD(wParam))
            {
            case LBN_SELCHANGE:
            case LBN_SELCANCEL:
                validateButtons(hDlg);
                break;
            }
            break;
        case IDC_CULL_EDIT:
        {
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            curCS.id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);
            cm.load(&curCS);
            DialogBox(NULL, MAKEINTRESOURCE(IDD_CULLINGSCHEME), hDlg, CullingSchemeEdit);
            cm.save(&curCS);
            SendMessage(list, LB_DELETESTRING, item, 0);
            int pos = (int)SendMessage(list, LB_INSERTSTRING, item, (LPARAM)curCS.name);
            SendMessage(list, LB_SETITEMDATA, pos, curCS.id);
            SendMessage(list, LB_SETCURSEL, item, 0);
        }
        break;
        case IDC_CULL_COPY:
        {
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
            int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
            curCS.id = (int)SendMessage(list, LB_GETITEMDATA, item, 0);
            cm.load(&curCS);
            swprintf_s(cs.name, 255, L"%ls %d", curCS.name, gLocalCountForNames);
            gLocalCountForNames++;
            cm.create(&cs);
            cm.copy(&cs, &curCS);
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
            int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
            SendMessage(list, LB_SETITEMDATA, pos, cs.id);
            validateButtons(hDlg);
        }
        break;
        case IDC_CULL_ADD:
        {
            swprintf_s(cs.name, 255, L"Culling Scheme %d", gLocalCountForNames);
            gLocalCountForNames++;
            cm.create(&cs);
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
            int pos = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)cs.name);
            SendMessage(list, LB_SETITEMDATA, pos, cs.id);
            validateButtons(hDlg);
        }
        break;
        case IDC_CULL_REMOVE:
        {
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
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
        case IDOK:
        case IDCANCEL:
        {
            list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
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
    HWND edit = GetDlgItem(hDlg, IDC_CULL_EDIT);
    HWND copy = GetDlgItem(hDlg, IDC_CULL_COPY);
    HWND remove = GetDlgItem(hDlg, IDC_CULL_REMOVE);
    HWND list = GetDlgItem(hDlg, IDC_CULL_SCHEMELIST);
    int item = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
    BOOL en = (item != LB_ERR);
    EnableWindow(edit, en);
    EnableWindow(copy, en);
    EnableWindow(remove, en);
}

// ---------------------------------------------------------------------------------------------
// Editor dialog — name + filter + Hide All button + checkbox list
// ---------------------------------------------------------------------------------------------
//
// The ListView (LVS_EX_CHECKBOXES) reflows on filter text change: clear all rows and re-add
// just those whose BlockTranslations name contains the filter substring. Each row stashes its
// BlockTranslations index in lParam so the (type,dataVal)→cull map survives filtering.
//
// LVS_EX_CHECKBOXES triggers LVN_ITEMCHANGED on every check toggle, the channel we listen on
// to mutate curCS.culled[]. The gPopulating flag suppresses callbacks during full repopulates
// so the programmatic state changes aren't interpreted as user clicks.

static bool gPopulating = false;

// Snapshot of curCS taken on editor open, used to undo all in-memory mutations if the user
// closes via Cancel (or the title-bar X). The parent dialog unconditionally cm.save(&curCS)
// after the editor returns, so on Cancel we restore the saved snapshot here to make that
// save a no-op (same bytes back to the registry).
static CullingScheme gPreEditCS;

// Walk the ListView's currently-visible rows and copy each row's checkbox state into
// curCS.culled[lParam]. This is the canonical "capture what the user sees" step — call it
// before doing anything that re-flows the list (filter change, Hide/Show Listed, OK), so
// curCS.culled[] always reflects the latest user input even if a stray LVN_ITEMCHANGED was
// missed. Skipped while gPopulating is true so the population's own SetCheckState calls
// don't get echoed back as "user input."
static void scanListIntoCulled(HWND lv)
{
    if (gPopulating) return;
    int count = ListView_GetItemCount(lv);
    for (int row = 0; row < count; row++) {
        LVITEM lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.iItem = row;
        lvi.mask = LVIF_PARAM;
        if (!ListView_GetItem(lv, &lvi)) continue;
        int idx = (int)lvi.lParam;
        if (idx < 0 || idx >= NUM_CULL_ENTRIES) continue;
        curCS.culled[idx] = ListView_GetCheckState(lv, row) ? 1 : 0;
    }
}

static void populateList(HWND lv, const wchar_t* filter)
{
    gPopulating = true;
    ListView_DeleteAllItems(lv);

    // Convert WCHAR filter → lowercase ASCII for substring matching against the lowercase
    // BlockTranslations names.
    char filterAscii[256];
    int j = 0;
    if (filter) {
        for (int i = 0; filter[i] && j < (int)sizeof(filterAscii) - 1; i++) {
            wchar_t c = filter[i];
            if (c < 128) filterAscii[j++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : (char)c);
        }
    }
    filterAscii[j] = '\0';
    int filterLen = j;

    int rows = blockTransCount();
    int inserted = 0;
    for (int i = 0; i < rows; i++) {
        const char* nm = NULL;
        if (!blockTransNameAt(i, &nm) || nm == NULL) continue;

        if (filterLen > 0) {
            // case-insensitive substring search
            const char* p = nm;
            bool found = false;
            while (*p) {
                int k = 0;
                while (filterAscii[k] && p[k] &&
                    (((p[k] >= 'A' && p[k] <= 'Z') ? p[k] + 32 : p[k]) == filterAscii[k])) k++;
                if (k == filterLen) { found = true; break; }
                p++;
            }
            if (!found) continue;
        }

        wchar_t wname[256];
        size_t conv = 0;
        mbstowcs_s(&conv, wname, 256, nm, _TRUNCATE);

        // Set the checkbox state IMAGE atomically with the insert via LVIF_STATE — doing it
        // in two steps (Insert then SetCheckState) was unreliable on reopen: the state-image
        // assignment didn't survive whatever path Windows takes after WM_INITDIALOG, so checks
        // saved to the registry never appeared in the editor on the next open.
        LVITEM lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE;
        lvi.iItem = inserted;
        lvi.iSubItem = 0;
        lvi.pszText = wname;
        lvi.lParam = (LPARAM)i;
        lvi.stateMask = LVIS_STATEIMAGEMASK;
        lvi.state = INDEXTOSTATEIMAGEMASK(curCS.culled[i] ? 2 : 1);
        int gotIdx = ListView_InsertItem(lv, &lvi);
        // Belt-and-suspenders: also call SetCheckState after the insert. LVIF_STATE alone
        // in InsertItem wasn't reliable across reopens (the state image read back fine but
        // the checkbox didn't render); SetCheckState alone after a plain insert had the
        // mirror problem. Doing both is what actually displays the checkmark.
        ListView_SetCheckState(lv, gotIdx, curCS.culled[i] ? TRUE : FALSE);
        inserted++;
    }
    gPopulating = false;
}

INT_PTR CALLBACK CullingSchemeEdit(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // Snapshot for Cancel/X-close undo. Anything the user does in this session that
        // mutates curCS will be rolled back on Cancel by memcpy'ing this back into curCS.
        memcpy(&gPreEditCS, &curCS, sizeof(CullingScheme));

        // Set up the ListView FIRST so the EN_CHANGE side effect from setting the name
        // field below can't fire populateList against a half-configured LV. The filter
        // edit's template default is already empty, so we don't set it here at all —
        // setting it would fire EN_CHANGE and run our filter handler before init finished.
        HWND lv = GetDlgItem(hDlg, IDC_CULL_LIST);
        ListView_SetExtendedListViewStyle(lv,
            LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES);

        // Size the single column to fill the LV's client area so there's no header divider
        // at the column edge (it looks like a draggable splitter for a phantom second column).
        RECT lvRect;
        GetClientRect(lv, &lvRect);
        LVCOLUMN lvc;
        memset(&lvc, 0, sizeof(lvc));
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.pszText = (LPWSTR)L"Block name";
        lvc.cx = lvRect.right - lvRect.left;
        ListView_InsertColumn(lv, 0, &lvc);

        populateList(lv, NULL);

        // Set the name last — the EN_CHANGE handler only re-reads curCS.name, no LV side effect.
        SetDlgItemText(hDlg, IDC_CULL_SCHEMENAME, curCS.name);
    }
    return (INT_PTR)TRUE;
    case WM_NOTIFY:
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_CLICK:
        {
            // Toggle the row's checkbox on a click anywhere within the row. The flag
            // LVHT_ONITEMSTATEICON is unreliable for this — with LVS_EX_FULLROWSELECT
            // Windows sets it for ANY row click, not just clicks on the checkbox. So
            // instead we compute the state-icon rect ourselves: it sits to the LEFT of
            // the LVIR_ICON rect (which marks the regular icon column). If the click X
            // landed in that strip, Windows already toggled — skip; otherwise we toggle.
            LPNMITEMACTIVATE act = (LPNMITEMACTIVATE)lParam;
            if (act->iItem < 0) break;
            HWND lv = GetDlgItem(hDlg, IDC_CULL_LIST);
            RECT iconRect;
            bool onStateIcon = false;
            if (ListView_GetItemRect(lv, act->iItem, &iconRect, LVIR_ICON)) {
                // State icon strip = [item.left, icon.left). For LVS_EX_CHECKBOXES the
                // state icon is always present, so iconRect.left will be > item.left.
                if (act->ptAction.x < iconRect.left) onStateIcon = true;
            }
            if (onStateIcon) break;
            BOOL nowChecked = !ListView_GetCheckState(lv, act->iItem);
            ListView_SetCheckState(lv, act->iItem, nowChecked);
            // Mirror to curCS.culled[] immediately so the change is captured even if the
            // resulting LVN_ITEMCHANGED path doesn't fire reliably.
            LVITEM lvi;
            memset(&lvi, 0, sizeof(lvi));
            lvi.iItem = act->iItem;
            lvi.mask = LVIF_PARAM;
            if (ListView_GetItem(lv, &lvi)) {
                int idx = (int)lvi.lParam;
                if (idx >= 0 && idx < NUM_CULL_ENTRIES) {
                    curCS.culled[idx] = nowChecked ? 1 : 0;
                }
            }
        }
        break;
        case LVN_ITEMCHANGED:
        {
            if (gPopulating) break;
            LPNMLISTVIEW it = (LPNMLISTVIEW)lParam;
            if ((it->uChanged & LVIF_STATE) == 0) break;
            // Checkbox state lives in the state-image bits (1<<12 = unchecked, 2<<12 = checked)
            int oldChecked = ((it->uOldState & LVIS_STATEIMAGEMASK) >> 12) - 1;
            int newChecked = ((it->uNewState & LVIS_STATEIMAGEMASK) >> 12) - 1;
            if (oldChecked < 0 || newChecked < 0) break;
            if (oldChecked == newChecked) break;
            // Retrieve the BlockTranslations index stashed in lParam when the row was inserted.
            LVITEM lvi;
            memset(&lvi, 0, sizeof(lvi));
            lvi.iItem = it->iItem;
            lvi.mask = LVIF_PARAM;
            HWND lv = GetDlgItem(hDlg, IDC_CULL_LIST);
            if (!ListView_GetItem(lv, &lvi)) break;
            int idx = (int)lvi.lParam;
            if (idx < 0 || idx >= NUM_CULL_ENTRIES) break;
            curCS.culled[idx] = (newChecked != 0) ? 1 : 0;
        }
        break;
        }
        break;
    case WM_COMMAND:
        switch (HIWORD(wParam))
        {
        case EN_CHANGE:
            switch (LOWORD(wParam))
            {
            case IDC_CULL_SCHEMENAME:
                GetDlgItemText(hDlg, IDC_CULL_SCHEMENAME, curCS.name, 255);
                break;
            case IDC_CULL_FILTER:
            {
                wchar_t buf[256];
                GetDlgItemText(hDlg, IDC_CULL_FILTER, buf, 256);
                // Snapshot the current view's check states into curCS.culled[] before the
                // list reflows — otherwise checks made under the previous filter would be
                // lost when the user types a new filter substring.
                HWND lv = GetDlgItem(hDlg, IDC_CULL_LIST);
                scanListIntoCulled(lv);
                populateList(lv, buf);
            }
            break;
            }
            break;
        }
        switch (LOWORD(wParam))
        {
        case IDC_CULL_HIDE_ALL:
        case IDC_CULL_SHOW_LISTED:
        {
            // "Hide Listed Blocks" / "Show Listed Blocks" — bulk-toggle just the rows currently
            // visible in the ListView (i.e., the post-filter subset). When the filter is empty
            // every row is visible, which collapses to "Hide All Blocks" / "Show All Blocks"
            // semantics; when a filter narrows the list, only those rows are affected, so a
            // user can curate by family ("type 'log' → Hide Listed").
            unsigned char target = (LOWORD(wParam) == IDC_CULL_HIDE_ALL) ? 1 : 0;
            HWND lv = GetDlgItem(hDlg, IDC_CULL_LIST);
            // Capture user checks made via direct clicking before we overwrite the visible
            // rows with the bulk target — preserves out-of-view rows the user already set.
            scanListIntoCulled(lv);
            int count = ListView_GetItemCount(lv);
            for (int row = 0; row < count; row++) {
                LVITEM lvi;
                memset(&lvi, 0, sizeof(lvi));
                lvi.iItem = row;
                lvi.mask = LVIF_PARAM;
                if (!ListView_GetItem(lv, &lvi)) continue;
                int idx = (int)lvi.lParam;	// BlockTranslations index stashed at populate time
                if (idx < 0 || idx >= NUM_CULL_ENTRIES) continue;
                curCS.culled[idx] = target;
            }
            // Repopulate so the checkbox state matches the new culled[] values. The filter text
            // is preserved so the user keeps their current narrowing.
            wchar_t buf[256];
            GetDlgItemText(hDlg, IDC_CULL_FILTER, buf, 256);
            populateList(lv, buf);
        }
        break;
        case IDOK:
        {
            // Final capture: scan the current view's check states so the on-save culled[]
            // reflects exactly what the user sees. Defends against any missed WM_NOTIFY.
            scanListIntoCulled(GetDlgItem(hDlg, IDC_CULL_LIST));
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
        {
            // Undo any in-memory edits so the parent's unconditional cm.save(&curCS) writes
            // back the same bytes that were there before the editor opened.
            memcpy(&curCS, &gPreEditCS, sizeof(CullingScheme));
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        }
        break;
    }
    return (INT_PTR)FALSE;
}
