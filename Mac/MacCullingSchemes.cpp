// MacCullingSchemes.cpp — Culling Schemes for the wxWidgets/macOS build.
// Replaces CullingStubs.cpp. Provides:
//   - isBlockCulled / applyCullingScheme (runtime hot path, same as Win CullingSchemes.cpp)
//   - CullingManager equivalent using wxConfig instead of the Win32 registry
//   - doCullingSchemesMac — scheme manager + block-filter editor dialogs
#include <wx/wx.h>
#include <wx/config.h>
#include <wx/checklst.h>
#include <wx/sizer.h>
#include <climits>
#include "MacCullingSchemes.h"
#include "stdafx.h"   // project types via compat.h -> blockInfo.h etc.
#include "CullingSchemes.h"   // NUM_CULL_ENTRIES, applyCullingScheme/isBlockCulled decls
                               // (CullingManager's HKEY field is fine: compat.h types it as void*)

// ──────────────────────────────────────────────────────────────────────────────
// Runtime cull state (mirrors Win/CullingSchemes.cpp top section)
// ──────────────────────────────────────────────────────────────────────────────
static unsigned char gIsCulledByIndex[NUM_CULL_ENTRIES] = {};
static bool gAnyCulled = false;

static void seedDefaultCulled(unsigned char* culled)
{
    int bi = blockTransIndexFor(BLOCK_BARRIER, 0);
    if (bi >= 0 && bi < NUM_CULL_ENTRIES) culled[bi] = 1;
    int vi = blockTransIndexFor(BLOCK_STRUCTURE_VOID, 0);
    if (vi >= 0 && vi < NUM_CULL_ENTRIES) culled[vi] = 1;
}

// Called by ObjFileManip.cpp and MinewaysMap.cpp via CullingSchemes.h
void applyCullingScheme(const unsigned char* culled)
{
    if (culled == nullptr) {
        memset(gIsCulledByIndex, 0, sizeof(gIsCulledByIndex));
        seedDefaultCulled(gIsCulledByIndex);
    } else {
        memcpy(gIsCulledByIndex, culled, NUM_CULL_ENTRIES);
    }
    gAnyCulled = false;
    for (int i = 0; i < NUM_CULL_ENTRIES; i++)
        if (gIsCulledByIndex[i]) { gAnyCulled = true; break; }
    InvalidateMapRenderCache();
}

bool isBlockCulled(int type, int dataVal)
{
    if (!gAnyCulled) return false;
    int idx = blockTransIndexFor(type, dataVal);
    if (idx < 0 || idx >= NUM_CULL_ENTRIES) return false;
    return gIsCulledByIndex[idx] != 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// CullingScheme struct (matches Win header for API compatibility)
// ──────────────────────────────────────────────────────────────────────────────
struct MacCullingScheme {
    int  id;
    char name[255];   // UTF-8 for wxString interop
    unsigned char culled[NUM_CULL_ENTRIES];
};

// ──────────────────────────────────────────────────────────────────────────────
// wxConfig persistence helpers
// ──────────────────────────────────────────────────────────────────────────────
static wxString cfgPath(int id) { return wxString::Format("/CullingSchemes/scheme_%d", id); }

static void saveScheme(const MacCullingScheme& cs)
{
    wxConfig cfg("Mineways");
    cfg.SetPath(cfgPath(cs.id));
    cfg.Write("name", wxString::FromUTF8(cs.name));
    // culled[] as hex string
    wxString hex;
    hex.Alloc(NUM_CULL_ENTRIES * 2);
    for (int i = 0; i < NUM_CULL_ENTRIES; i++)
        hex += wxString::Format("%02x", cs.culled[i]);
    cfg.Write("culled", hex);
    cfg.SetPath("/");
}

static bool loadScheme(int id, MacCullingScheme& cs)
{
    if (id <= 0) return false;

    wxConfig cfg("Mineways");
    cfg.SetPath(cfgPath(id));
    wxString name, hex;
    if (!cfg.Read("name", &name) || !cfg.Read("culled", &hex)) {
        cfg.SetPath("/"); return false;
    }

    if (hex.length() != NUM_CULL_ENTRIES * 2) {
        cfg.SetPath("/"); return false;
    }

    MacCullingScheme decoded = {};
    wxScopedCharBuffer utf8Name = name.utf8_str();
    if (!utf8Name || strlen(utf8Name.data()) > sizeof(decoded.name) - 1) {
        cfg.SetPath("/"); return false;
    }
    strcpy(decoded.name, utf8Name.data());
    decoded.id = id;
    for (int i = 0; i < NUM_CULL_ENTRIES; i++) {
        wxUniChar high = hex[2*i];
        wxUniChar low = hex[2*i + 1];
        auto isHexDigit = [](wxUniChar c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        };
        unsigned long v = 0;
        if (!isHexDigit(high) || !isHexDigit(low) ||
            !hex.Mid(2*i, 2).ToULong(&v, 16) || v > 0xff) {
            cfg.SetPath("/"); return false;
        }
        decoded.culled[i] = v ? 1 : 0;
    }
    cfg.SetPath("/");
    cs = decoded;
    return true;
}

static void deleteScheme(int id)
{
    wxConfig cfg("Mineways");
    cfg.DeleteGroup(cfgPath(id));
}

static int nextSchemeId()
{
    wxConfig cfg("Mineways");
    long id = 0;
    if (!cfg.Read("/CullingSchemes/schemeId", &id) || id < 0 || id >= INT_MAX)
        id = 0;
    id++;
    cfg.Write("/CullingSchemes/schemeId", id);
    return (int)id;
}

// Enumerate all saved scheme IDs (returns sorted list)
static wxArrayInt listSchemeIds()
{
    wxConfig cfg("Mineways");
    wxArrayInt ids;
    wxString grp; long cookie;
    cfg.SetPath("/CullingSchemes");
    bool more = cfg.GetFirstGroup(grp, cookie);
    while (more) {
        if (grp.StartsWith("scheme_")) {
            long v = 0;
            if (grp.Mid(7).ToLong(&v) && v > 0 && v <= INT_MAX)
                ids.push_back((int)v);
        }
        more = cfg.GetNextGroup(grp, cookie);
    }
    cfg.SetPath("/");
    ids.Sort([](int* a, int* b){ return (*a > *b) - (*a < *b); });
    return ids;
}

static MacCullingScheme gCurCS;
static wchar_t gLastSelected[255] = {};

const wchar_t* getSelectedCullingSchemeW() { return gLastSelected; }

bool SelectCullingSchemeByName(const wxString& name)
{
    if (name.IsEmpty() || name.CmpNoCase("Standard") == 0) {
        applyCullingScheme(nullptr);
        wcscpy(gLastSelected, L"Standard");
        return true;
    }
    for (int id : listSchemeIds()) {
        MacCullingScheme cs;
        if (!loadScheme(id, cs)) continue;
        if (wxString::FromUTF8(cs.name).CmpNoCase(name) == 0) {
            applyCullingScheme(cs.culled);
            wcsncpy(gLastSelected, name.wc_str(), 254);
            gLastSelected[254] = L'\0';
            return true;
        }
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Block-filter editor dialog
// ──────────────────────────────────────────────────────────────────────────────
class CullEditorDialog : public wxDialog {
public:
    CullEditorDialog(wxWindow* parent, MacCullingScheme& cs)
        : wxDialog(parent, wxID_ANY, "Edit Culling Scheme",
                   wxDefaultPosition, wxSize(480, 600),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_cs(cs)
    {
        memcpy(&m_saved, &cs, sizeof(cs));

        wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

        // Name row
        m_nameCtrl = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(cs.name));
        wxFlexGridSizer* nameRow = new wxFlexGridSizer(1, 2, 4, 8);
        nameRow->AddGrowableCol(1);
        nameRow->Add(new wxStaticText(this, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
        nameRow->Add(m_nameCtrl, 1, wxEXPAND);
        top->Add(nameRow, 0, wxEXPAND | wxALL, 8);

        // Filter row
        m_filterCtrl = new wxTextCtrl(this, wxID_ANY, "");
        wxFlexGridSizer* filterRow = new wxFlexGridSizer(1, 2, 4, 8);
        filterRow->AddGrowableCol(1);
        filterRow->Add(new wxStaticText(this, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL);
        filterRow->Add(m_filterCtrl, 1, wxEXPAND);
        top->Add(filterRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        // Button row: Hide All / Show All / Hide Listed / Show Listed
        wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
        auto* hideAll  = new wxButton(this, wxID_ANY, "Hide All");
        auto* showAll  = new wxButton(this, wxID_ANY, "Show All");
        auto* hideListed = new wxButton(this, wxID_ANY, "Hide Listed");
        auto* showListed = new wxButton(this, wxID_ANY, "Show Listed");
        btnRow->Add(hideAll,    0, wxRIGHT, 4);
        btnRow->Add(showAll,    0, wxRIGHT, 4);
        btnRow->Add(hideListed, 0, wxRIGHT, 4);
        btnRow->Add(showListed, 0);
        top->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        // Block list with checkboxes
        m_list = new wxCheckListBox(this, wxID_ANY);
        top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(top);

        // Populate list and wire events
        Populate("");

        m_filterCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            SyncToCS();
            Populate(m_filterCtrl->GetValue().utf8_string());
        });
        hideAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            memset(m_cs.culled, 1, NUM_CULL_ENTRIES); Populate(m_filterCtrl->GetValue().utf8_string()); });
        showAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            memset(m_cs.culled, 0, NUM_CULL_ENTRIES); Populate(m_filterCtrl->GetValue().utf8_string()); });
        hideListed->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetListedTo(true); });
        showListed->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetListedTo(false); });

        Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            if (e.GetId() == wxID_OK) {
                SyncToCS();
                wxString nm = m_nameCtrl->GetValue().Trim();
                if (!nm.IsEmpty()) { strncpy(m_cs.name, nm.utf8_str(), 254); m_cs.name[254] = '\0'; }
            } else {
                memcpy(&m_cs, &m_saved, sizeof(m_cs));  // restore on Cancel
            }
            e.Skip();
        });
    }

private:
    MacCullingScheme& m_cs;
    MacCullingScheme  m_saved;
    wxTextCtrl*       m_nameCtrl  = nullptr;
    wxTextCtrl*       m_filterCtrl = nullptr;
    wxCheckListBox*   m_list       = nullptr;
    // m_indices[row] → BlockTranslations index for that row
    wxArrayInt        m_indices;

    void Populate(const std::string& filter)
    {
        m_list->Clear();
        m_indices.clear();
        int n = blockTransCount();
        for (int i = 0; i < n; i++) {
            const char* nm = nullptr;
            if (!blockTransNameAt(i, &nm) || !nm) continue;
            if (!filter.empty()) {
                // case-insensitive substring
                std::string s(nm);
                auto it = std::search(s.begin(), s.end(), filter.begin(), filter.end(),
                    [](char a, char b){ return tolower(a)==tolower(b); });
                if (it == s.end()) continue;
            }
            m_list->Append(wxString::FromUTF8(nm));
            int row = (int)m_indices.size();
            m_indices.push_back(i);
            m_list->Check(row, m_cs.culled[i] != 0);
        }
    }

    void SyncToCS()
    {
        for (int row = 0; row < (int)m_indices.size(); row++) {
            int idx = m_indices[row];
            if (idx >= 0 && idx < NUM_CULL_ENTRIES)
                m_cs.culled[idx] = m_list->IsChecked(row) ? 1 : 0;
        }
    }

    void SetListedTo(bool hide)
    {
        SyncToCS();
        for (int idx : m_indices) {
            if (idx >= 0 && idx < NUM_CULL_ENTRIES)
                m_cs.culled[idx] = hide ? 1 : 0;
        }
        for (int row = 0; row < (int)m_indices.size(); row++)
            m_list->Check(row, hide);
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Scheme manager dialog
// ──────────────────────────────────────────────────────────────────────────────
void doCullingSchemesMac(wxWindow* parent)
{
    wxDialog dlg(parent, wxID_ANY, "Culling Schemes",
                 wxDefaultPosition, wxSize(360, 400),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

    // Scheme list
    auto* listBox = new wxListBox(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   0, nullptr, wxLB_SINGLE);
    top->Add(new wxStaticText(&dlg, wxID_ANY, "Schemes:"), 0, wxLEFT | wxTOP, 8);
    top->Add(listBox, 1, wxEXPAND | wxALL, 8);

    // Buttons
    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    auto* btnNew    = new wxButton(&dlg, wxID_ANY, "New");
    auto* btnEdit   = new wxButton(&dlg, wxID_ANY, "Edit...");
    auto* btnCopy   = new wxButton(&dlg, wxID_ANY, "Copy");
    auto* btnDelete = new wxButton(&dlg, wxID_ANY, "Delete");
    btnRow->Add(btnNew,    0, wxRIGHT, 4);
    btnRow->Add(btnEdit,   0, wxRIGHT, 4);
    btnRow->Add(btnCopy,   0, wxRIGHT, 4);
    btnRow->Add(btnDelete, 0);
    top->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    top->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
    dlg.SetSizerAndFit(top);
    dlg.CentreOnParent();

    // Populate list
    wxArrayInt ids = listSchemeIds();
    wxArrayInt rowIds;  // parallel to listBox rows
    for (int id : ids) {
        MacCullingScheme cs;
        if (loadScheme(id, cs)) {
            listBox->Append(wxString::FromUTF8(cs.name));
            rowIds.push_back(id);
        }
    }

    auto updateButtons = [&]() {
        bool sel = (listBox->GetSelection() != wxNOT_FOUND);
        btnEdit->Enable(sel);
        btnCopy->Enable(sel);
        btnDelete->Enable(sel);
    };
    updateButtons();

    listBox->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { updateButtons(); });

    btnNew->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        int id = nextSchemeId();
        MacCullingScheme cs = {};
        cs.id = id;
        snprintf(cs.name, 255, "Culling Scheme %d", id);
        seedDefaultCulled(cs.culled);
        saveScheme(cs);
        listBox->Append(wxString::FromUTF8(cs.name));
        rowIds.push_back(id);
        listBox->SetSelection((int)rowIds.size() - 1);
        updateButtons();
    });

    btnEdit->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        int sel = listBox->GetSelection();
        if (sel == wxNOT_FOUND) return;
        MacCullingScheme cs;
        if (!loadScheme(rowIds[sel], cs)) return;
        CullEditorDialog ed(&dlg, cs);
        if (ed.ShowModal() == wxID_OK) {
            saveScheme(cs);
            listBox->SetString(sel, wxString::FromUTF8(cs.name));
        }
    });

    btnCopy->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        int sel = listBox->GetSelection();
        if (sel == wxNOT_FOUND) return;
        MacCullingScheme src;
        if (!loadScheme(rowIds[sel], src)) return;
        int id = nextSchemeId();
        MacCullingScheme cs = src;
        cs.id = id;
        snprintf(cs.name, 255, "%s copy", src.name);
        saveScheme(cs);
        listBox->Append(wxString::FromUTF8(cs.name));
        rowIds.push_back(id);
        listBox->SetSelection((int)rowIds.size() - 1);
        updateButtons();
    });

    btnDelete->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        int sel = listBox->GetSelection();
        if (sel == wxNOT_FOUND) return;
        deleteScheme(rowIds[sel]);
        listBox->Delete(sel);
        rowIds.erase(rowIds.begin() + sel);
        if (sel >= (int)rowIds.size()) sel = (int)rowIds.size() - 1;
        if (sel >= 0) listBox->SetSelection(sel);
        updateButtons();
    });

    if (dlg.ShowModal() == wxID_OK) {
        int sel = listBox->GetSelection();
        if (sel != wxNOT_FOUND && loadScheme(rowIds[sel], gCurCS)) {
            if (_mwUtf8ToWideBuffer(gCurCS.name, gLastSelected, 255)) {
                applyCullingScheme(gCurCS.culled);
            } else {
                applyCullingScheme(nullptr);
                wcscpy(gLastSelected, L"Standard");
            }
        } else {
            applyCullingScheme(nullptr);  // Standard
            wcscpy(gLastSelected, L"Standard");
        }
    }
}
