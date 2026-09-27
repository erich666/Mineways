// ExportDialog.cpp — full export options dialog for the wxWidgets Mac build.
// Operates directly on ExportFileData (same struct Win/ExportPrint.cpp uses),
// so SaveVolume() sees the exact same data contract on both platforms.
#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/notebook.h>
#include "ExportDialog.h"
#include "stdafx.h"  // ExportFileData, FILE_TYPE_*, gMtlCostTable, gUnitTypeTable

static const char* kTypeNames[] = {
    "Wavefront OBJ (absolute)",
    "Wavefront OBJ (relative)",
    "USD",
    "Binary STL (Magics)",
    "Binary STL (VisCAM)",
    "ASCII STL",
    "VRML2",
    "Schematic (.schematic)",
    "Sponge Schematic (.schem)",
};
static const char* kTypeExts[] = {
    ".obj", ".obj", ".usda", ".stl", ".stl", ".stl", ".wrl", ".schematic", ".schem",
};
static_assert(sizeof(kTypeNames)/sizeof(kTypeNames[0]) == FILE_TYPE_TOTAL, "type count mismatch");

// ── Default presets, ported from Win/Mineways.cpp initializePrintExportData/
// initializeViewExportData. Windows exposes these as two separate menu items
// ("Export for Rendering" / "Export for 3D Printing"); Mac exposes them as
// two buttons inside one unified dialog instead.
void InitPrintExportData(ExportFileData& e)
{
    memset(&e, 0, sizeof(ExportFileData));
    e.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
    for (int t = 0; t < FILE_TYPE_TOTAL; t++) e.chkCreateModelFiles[t] = 1;
    e.chkCreateZip[FILE_TYPE_WAVEFRONT_ABS_OBJ] = 1; e.chkCreateZip[FILE_TYPE_WAVEFRONT_REL_OBJ] = 1;
    e.chkCreateZip[FILE_TYPE_VRML2] = 1;

    e.radioExportNoMaterials[FILE_TYPE_ASCII_STL] = 1;
    e.radioExportNoMaterials[FILE_TYPE_SCHEMATIC] = 1;
    e.radioExportNoMaterials[FILE_TYPE_SPONGE_SCHEMATIC] = 1;
    e.radioExportMtlColors[FILE_TYPE_BINARY_MAGICS_STL] = 1;
    e.radioExportMtlColors[FILE_TYPE_BINARY_VISCAM_STL] = 1;
    e.radioExportFullTexture[FILE_TYPE_WAVEFRONT_ABS_OBJ] = 1;
    e.radioExportFullTexture[FILE_TYPE_WAVEFRONT_REL_OBJ] = 1;
    e.radioExportFullTexture[FILE_TYPE_VRML2] = 1;
    e.radioExportTileTextures[FILE_TYPE_USD] = 1;

    strncpy(e.tileDirString, "tex", MAX_PATH - 1);
    e.chkTextureRGB = 1;

    e.chkMergeFlattop = 1;
    for (int t = 0; t < FILE_TYPE_TOTAL - 3; t++) e.chkMakeZUp[t] = 1;   // OBJ/USD/STL/VRML-ish; schematic stays 0
    e.chkMakeZUp[FILE_TYPE_VRML2] = 0;
    e.chkCenterModel = 1;
    e.chkCompositeOverlay = 1;
    e.chkBlockFacesAtBorders = 1;
    e.chkLeavesSolid = 1;

    e.radioRotate0 = 1;
    e.radioScaleByBlock = 1;
    e.modelHeightVal = 5.0f;
    for (int t = 0; t < FILE_TYPE_TOTAL; t++) {
        bool custom = (t == FILE_TYPE_BINARY_MAGICS_STL || t == FILE_TYPE_BINARY_VISCAM_STL || t == FILE_TYPE_ASCII_STL);
        float minWall = gMtlCostTable[custom ? PRINT_MATERIAL_CUSTOM_MATERIAL : PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall;
        e.blockSizeVal[t] = 1000.0f * minWall;
        e.hollowThicknessVal[t] = 1000.0f * minWall;
        e.comboPhysicalMaterial[t] = custom ? PRINT_MATERIAL_CUSTOM_MATERIAL : PRINT_MATERIAL_FULL_COLOR_SANDSTONE;
        e.comboModelUnits[t] = UNITS_MILLIMETER;
        e.chkHollow[t] = (t != FILE_TYPE_BINARY_MAGICS_STL && t != FILE_TYPE_BINARY_VISCAM_STL && t != FILE_TYPE_ASCII_STL) ? 1 : 0;
        e.chkSuperHollow[t] = e.chkHollow[t];
    }
    e.costVal = 25.0f;

    e.chkFillBubbles = 1;
    e.chkConnectParts = 1;
    e.chkConnectCornerTips = 1;
    e.chkDeleteFloaters = 1;
    e.chkMakeGroupsObjects = 1;
    e.scaleLightsVal = 15.0f;
    e.scaleEmittersVal = 20.0f;
    e.floaterCountVal = 16;
    e.flags = EXPT_3DPRINT;
}

void InitViewExportData(ExportFileData& e)
{
    InitPrintExportData(e);
    e.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
    for (int t = 0; t < FILE_TYPE_TOTAL; t++) { e.chkCreateZip[t] = 0; e.chkMakeZUp[t] = 0; }

    for (int t = 0; t < FILE_TYPE_TOTAL; t++) {
        e.radioExportNoMaterials[t] = 0; e.radioExportMtlColors[t] = 0;
        e.radioExportFullTexture[t] = 0; e.radioExportTileTextures[t] = 0;
    }
    e.radioExportNoMaterials[FILE_TYPE_ASCII_STL] = 1;
    e.radioExportNoMaterials[FILE_TYPE_SCHEMATIC] = 1;
    e.radioExportNoMaterials[FILE_TYPE_SPONGE_SCHEMATIC] = 1;
    e.radioExportMtlColors[FILE_TYPE_BINARY_MAGICS_STL] = 1;
    e.radioExportMtlColors[FILE_TYPE_BINARY_VISCAM_STL] = 1;
    e.radioExportFullTexture[FILE_TYPE_VRML2] = 1;
    e.radioExportTileTextures[FILE_TYPE_WAVEFRONT_ABS_OBJ] = 1;
    e.radioExportTileTextures[FILE_TYPE_WAVEFRONT_REL_OBJ] = 1;
    e.radioExportTileTextures[FILE_TYPE_USD] = 1;

    e.chkTextureA = 1; e.chkTextureRGBA = 1;
    e.chkExportAll = 1;
    e.modelHeightVal = 100.0f;
    for (int t = 0; t < FILE_TYPE_TOTAL; t++) {
        e.blockSizeVal[t] = 1000.0f;
        e.hollowThicknessVal[t] = 1000.0f;
        e.chkHollow[t] = 0; e.chkSuperHollow[t] = 0;
        e.comboModelUnits[t] = (t == FILE_TYPE_BINARY_MAGICS_STL || t == FILE_TYPE_BINARY_VISCAM_STL || t == FILE_TYPE_ASCII_STL)
            ? UNITS_MILLIMETER : UNITS_METER;
    }
    e.chkSealEntrances = 0; e.chkSealSideTunnels = 0; e.chkFillBubbles = 0;
    e.chkConnectParts = 0; e.chkConnectCornerTips = 0; e.chkConnectAllEdges = 0; e.chkDeleteFloaters = 0;

    e.chkSeparateTypes = 1;
    e.chkMaterialPerFamily = 1;
    e.chkSplitByBlockType = 1;
    e.chkMakeGroupsObjects = 1;
    e.chkCustomMaterial[FILE_TYPE_WAVEFRONT_ABS_OBJ] = 1;
    e.chkCustomMaterial[FILE_TYPE_WAVEFRONT_REL_OBJ] = 1;
    e.chkCompositeOverlay = 0;
    e.chkLeavesSolid = 0;
    e.flags = 0x0;
}

// ── Dialog ──────────────────────────────────────────────────────────────────
namespace {

class ExportOptionsDialog : public wxDialog {
public:
    ExportOptionsDialog(wxWindow* parent, ExportFileData& efd,
                        const wxString& initPath, int selMinX, int selMinY, int selMinZ,
                        int selMaxX, int selMaxY, int selMaxZ)
        : wxDialog(parent, wxID_ANY, "Export Model",
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_efd(efd)
    {
        wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

        top->Add(BuildTopRow(initPath), 0, wxEXPAND | wxALL, 8);
        top->Add(BuildBoundsRow(selMinX, selMinY, selMinZ, selMaxX, selMaxY, selMaxZ),
                 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        wxNotebook* nb = new wxNotebook(this, wxID_ANY);
        nb->AddPage(BuildGeneralPage(nb), "General");
        nb->AddPage(BuildMaterialsPage(nb), "Materials");
        nb->AddPage(BuildSizingPage(nb), "Sizing");
        nb->AddPage(BuildPrintPrepPage(nb), "3D Print Prep");
        nb->AddPage(BuildAdvancedPage(nb), "Advanced");
        top->Add(nb, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

        wxBoxSizer* presetRow = new wxBoxSizer(wxHORIZONTAL);
        auto* renderPreset = new wxButton(this, wxID_ANY, "Load Rendering Defaults");
        auto* printPreset  = new wxButton(this, wxID_ANY, "Load 3D-Printing Defaults");
        renderPreset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            int ft = m_efd.fileType; InitViewExportData(m_efd); m_efd.fileType = ft; LoadFromEfd(); });
        printPreset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            int ft = m_efd.fileType; InitPrintExportData(m_efd); m_efd.fileType = ft; LoadFromEfd(); });
        presetRow->Add(renderPreset, 0, wxRIGHT, 6);
        presetRow->Add(printPreset, 0);
        top->Add(presetRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);

        SetSizerAndFit(top);
        SetMinSize(wxSize(560, 620));
        CentreOnParent();

        LoadFromEfd();
        Bind(wxEVT_BUTTON, &ExportOptionsDialog::OnButton, this, wxID_OK);

        m_typeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            int oldFt = m_efd.fileType;
            if (SaveToEfd(oldFt)) {
                m_efd.fileType = m_typeChoice->GetSelection();
                LoadFromEfd();
            } else {
                // Validation failed; revert the dropdown
                m_typeChoice->SetSelection(oldFt);
            }
        });
    }

    wxString GetPath() const { return m_pathCtrl->GetValue(); }

private:
    ExportFileData& m_efd;   // caller's struct; only overwritten on validated OK

    wxChoice*   m_typeChoice = nullptr;
    wxTextCtrl* m_pathCtrl = nullptr;
    wxTextCtrl* m_minX = nullptr, *m_minY = nullptr, *m_minZ = nullptr;
    wxTextCtrl* m_maxX = nullptr, *m_maxY = nullptr, *m_maxZ = nullptr;

    wxRadioButton *m_rot0 = nullptr, *m_rot90 = nullptr, *m_rot180 = nullptr, *m_rot270 = nullptr;
    wxCheckBox *m_chkCenter = nullptr, *m_chkZUp = nullptr, *m_chkZip = nullptr, *m_chkCreateFiles = nullptr;

    wxRadioButton *m_matNone = nullptr, *m_matColors = nullptr, *m_matSolidTex = nullptr,
                  *m_matFullTex = nullptr, *m_matTiles = nullptr;
    wxTextCtrl* m_tileDir = nullptr;
    wxCheckBox *m_chkRGB = nullptr, *m_chkA = nullptr, *m_chkRGBA = nullptr;
    wxCheckBox *m_chkBiome = nullptr, *m_chkBorders = nullptr, *m_chkLeaves = nullptr, *m_chkComposite = nullptr;
    wxTextCtrl* m_lightScale = nullptr; wxTextCtrl* m_emitScale = nullptr;

    wxRadioButton *m_scaleHeight = nullptr, *m_scaleMaterial = nullptr, *m_scaleBlock = nullptr, *m_scaleCost = nullptr;
    wxTextCtrl *m_modelHeight = nullptr, *m_blockSize = nullptr, *m_cost = nullptr;
    wxChoice *m_physMaterial = nullptr, *m_units = nullptr;

    wxCheckBox *m_chkFillBubbles = nullptr, *m_chkSealEntrances = nullptr, *m_chkSealSideTunnels = nullptr;
    wxCheckBox *m_chkConnectParts = nullptr, *m_chkCornerTips = nullptr, *m_chkAllEdges = nullptr;
    wxCheckBox *m_chkDeleteFloaters = nullptr; wxTextCtrl* m_floatCount = nullptr;
    wxCheckBox *m_chkHollow = nullptr, *m_chkSuperHollow = nullptr; wxTextCtrl* m_hollowThickness = nullptr;
    wxCheckBox *m_chkMeltSnow = nullptr, *m_chkExportAll = nullptr, *m_chkFatten = nullptr;
    wxCheckBox *m_chkShowParts = nullptr, *m_chkShowWelds = nullptr;

    wxCheckBox *m_chkMakeGroups = nullptr, *m_chkSeparateTypes = nullptr, *m_chkIndividualBlocks = nullptr;
    wxCheckBox *m_chkMaterialPerFamily = nullptr, *m_chkSplitByType = nullptr, *m_chkCustomMaterial = nullptr;
    wxCheckBox *m_chkExportMDL = nullptr, *m_chkDoubledBillboards = nullptr, *m_chkDecimate = nullptr;

    static wxBoxSizer* LabeledField(wxWindow* parent, const wxString& label, wxTextCtrl*& outCtrl, int width = 70)
    {
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        outCtrl = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxSize(width, -1));
        row->Add(outCtrl, 0);
        return row;
    }

    wxSizer* BuildTopRow(const wxString& initPath)
    {
        wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 4, 8);
        grid->AddGrowableCol(1);

        wxArrayString typeChoices;
        for (auto& n : kTypeNames) typeChoices.Add(n);
        m_typeChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, typeChoices);
        m_typeChoice->SetSelection(m_efd.fileType >= 0 && m_efd.fileType < FILE_TYPE_TOTAL ? m_efd.fileType : 0);
        grid->Add(new wxStaticText(this, wxID_ANY, "File type:"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(m_typeChoice, 1, wxEXPAND);

        wxBoxSizer* pathRow = new wxBoxSizer(wxHORIZONTAL);
        m_pathCtrl = new wxTextCtrl(this, wxID_ANY, initPath, wxDefaultPosition, wxSize(320, -1));
        auto* browseBtn = new wxButton(this, wxID_ANY, "Browse...");
        browseBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            int sel = m_typeChoice->GetSelection();
            if (sel < 0 || sel >= FILE_TYPE_TOTAL) sel = 0;
            wxString wildcard = wxString::Format("Export files (*%s)|*%s|All files (*)|*", kTypeExts[sel], kTypeExts[sel]);
            wxFileDialog fd(this, "Save export as", "", "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (m_pathCtrl->GetValue().length() > 0) fd.SetPath(m_pathCtrl->GetValue());
            if (fd.ShowModal() == wxID_OK) m_pathCtrl->SetValue(fd.GetPath());
        });
        pathRow->Add(m_pathCtrl, 1, wxEXPAND);
        pathRow->Add(browseBtn, 0, wxLEFT, 6);
        grid->Add(new wxStaticText(this, wxID_ANY, "Output:"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(pathRow, 1, wxEXPAND);
        return grid;
    }

    wxSizer* BuildBoundsRow(int selMinX, int selMinY, int selMinZ, int selMaxX, int selMaxY, int selMaxZ)
    {
        wxStaticBoxSizer* box = new wxStaticBoxSizer(wxHORIZONTAL, this, "Export bounds (X/Y/Z)");
        auto field = [&](int val) {
            return new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", val), wxDefaultPosition, wxSize(60, -1));
        };
        box->Add(new wxStaticText(this, wxID_ANY, "Min:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        box->Add(m_minX = field(selMinX), 0, wxRIGHT, 4);
        box->Add(m_minY = field(selMinY), 0, wxRIGHT, 4);
        box->Add(m_minZ = field(selMinZ), 0, wxRIGHT, 12);
        box->Add(new wxStaticText(this, wxID_ANY, "Max:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        box->Add(m_maxX = field(selMaxX), 0, wxRIGHT, 4);
        box->Add(m_maxY = field(selMaxY), 0, wxRIGHT, 4);
        box->Add(m_maxZ = field(selMaxZ), 0);
        return box;
    }

    wxPanel* BuildGeneralPage(wxWindow* parent)
    {
        wxPanel* p = new wxPanel(parent);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* rotBox = new wxStaticBoxSizer(wxHORIZONTAL, p, "Rotation");
        m_rot0   = new wxRadioButton(p, wxID_ANY, "0°", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_rot90  = new wxRadioButton(p, wxID_ANY, "90°");
        m_rot180 = new wxRadioButton(p, wxID_ANY, "180°");
        m_rot270 = new wxRadioButton(p, wxID_ANY, "270°");
        for (auto* r : {m_rot0, m_rot90, m_rot180, m_rot270}) rotBox->Add(r, 0, wxRIGHT, 10);
        s->Add(rotBox, 0, wxEXPAND | wxALL, 8);

        m_chkCenter = new wxCheckBox(p, wxID_ANY, "Center model (roughly around 0,0,0)");
        m_chkZUp = new wxCheckBox(p, wxID_ANY, "Z is up (instead of Y)");
        m_chkZip = new wxCheckBox(p, wxID_ANY, "Create ZIP of output files");
        m_chkCreateFiles = new wxCheckBox(p, wxID_ANY, "Keep model files (uncheck to delete after zipping)");
        for (auto* c : {m_chkCenter, m_chkZUp, m_chkZip, m_chkCreateFiles})
            s->Add(c, 0, wxLEFT | wxBOTTOM, 8);

        p->SetSizerAndFit(s);
        return p;
    }

    wxPanel* BuildMaterialsPage(wxWindow* parent)
    {
        wxPanel* p = new wxPanel(parent);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* matBox = new wxStaticBoxSizer(wxVERTICAL, p, "Materials");
        m_matNone     = new wxRadioButton(p, wxID_ANY, "No materials", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_matColors   = new wxRadioButton(p, wxID_ANY, "Solid colors");
        m_matSolidTex = new wxRadioButton(p, wxID_ANY, "Solid noisy textures");
        m_matFullTex  = new wxRadioButton(p, wxID_ANY, "Full (mosaic) textures");
        m_matTiles    = new wxRadioButton(p, wxID_ANY, "Individual tile textures (per block face)");
        for (auto* r : {m_matNone, m_matColors, m_matSolidTex, m_matFullTex, m_matTiles})
            matBox->Add(r, 0, wxTOP, 3);
        wxBoxSizer* tileRow = new wxBoxSizer(wxHORIZONTAL);
        tileRow->Add(new wxStaticText(p, wxID_ANY, "Tile subdirectory:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_tileDir = new wxTextCtrl(p, wxID_ANY, "tex", wxDefaultPosition, wxSize(120, -1));
        tileRow->Add(m_tileDir);
        matBox->Add(tileRow, 0, wxTOP, 6);
        s->Add(matBox, 0, wxEXPAND | wxALL, 8);

        wxStaticBoxSizer* texBox = new wxStaticBoxSizer(wxHORIZONTAL, p, "Texture channels to (re)export");
        m_chkRGB = new wxCheckBox(p, wxID_ANY, "RGB");
        m_chkA = new wxCheckBox(p, wxID_ANY, "A");
        m_chkRGBA = new wxCheckBox(p, wxID_ANY, "RGBA");
        for (auto* c : {m_chkRGB, m_chkA, m_chkRGBA}) texBox->Add(c, 0, wxRIGHT, 10);
        s->Add(texBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        m_chkBiome = new wxCheckBox(p, wxID_ANY, "Use biome at center of export area");
        m_chkBorders = new wxCheckBox(p, wxID_ANY, "Create block faces at borders");
        m_chkLeaves = new wxCheckBox(p, wxID_ANY, "Tree leaves solid (non-transparent)");
        m_chkComposite = new wxCheckBox(p, wxID_ANY, "Composite overlays (vines/ladders/rails) onto texture");
        for (auto* c : {m_chkBiome, m_chkBorders, m_chkLeaves, m_chkComposite})
            s->Add(c, 0, wxLEFT | wxBOTTOM, 8);

        wxStaticBoxSizer* usdBox = new wxStaticBoxSizer(wxHORIZONTAL, p, "USD only");
        usdBox->Add(LabeledField(p, "Light scale:", m_lightScale, 60), 0, wxRIGHT, 12);
        usdBox->Add(LabeledField(p, "Surface emit scale:", m_emitScale, 60), 0);
        s->Add(usdBox, 0, wxEXPAND | wxALL, 8);

        p->SetSizerAndFit(s);
        return p;
    }

    wxPanel* BuildSizingPage(wxWindow* parent)
    {
        wxPanel* p = new wxPanel(parent);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

        wxStaticBoxSizer* box = new wxStaticBoxSizer(wxVERTICAL, p, "Model size / cost");
        m_scaleHeight   = new wxRadioButton(p, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_scaleMaterial = new wxRadioButton(p, wxID_ANY, "Minimize for physical material (thinnest safe walls)");
        m_scaleBlock    = new wxRadioButton(p, wxID_ANY, "Fixed block size");
        m_scaleCost     = new wxRadioButton(p, wxID_ANY, "Target a material cost");

        wxBoxSizer* heightRow = new wxBoxSizer(wxHORIZONTAL);
        heightRow->Add(m_scaleHeight, 0, wxALIGN_CENTER_VERTICAL);
        heightRow->Add(LabeledField(p, "Target height (cm):", m_modelHeight, 60), 0, wxLEFT, 4);
        box->Add(heightRow, 0, wxTOP, 4);
        box->Add(m_scaleMaterial, 0, wxTOP, 6);

        wxBoxSizer* blockRow = new wxBoxSizer(wxHORIZONTAL);
        blockRow->Add(m_scaleBlock, 0, wxALIGN_CENTER_VERTICAL);
        blockRow->Add(LabeledField(p, "Block size:", m_blockSize, 60), 0, wxLEFT, 4);
        box->Add(blockRow, 0, wxTOP, 6);

        wxBoxSizer* costRow = new wxBoxSizer(wxHORIZONTAL);
        costRow->Add(m_scaleCost, 0, wxALIGN_CENTER_VERTICAL);
        costRow->Add(LabeledField(p, "Cost:", m_cost, 70), 0, wxLEFT, 4);
        box->Add(costRow, 0, wxTOP, 6);
        s->Add(box, 0, wxEXPAND | wxALL, 8);

        wxFlexGridSizer* comboGrid = new wxFlexGridSizer(2, 2, 6, 8);
        m_physMaterial = new wxChoice(p, wxID_ANY);
        for (int i = 0; i < MTL_COST_TABLE_SIZE; i++) {
            m_physMaterial->Append(wxString(gMtlCostTable[i].wname));
        }
        m_units = new wxChoice(p, wxID_ANY);
        for (int i = 0; i < MODELS_UNITS_TABLE_SIZE; i++) {
            m_units->Append(wxString(gUnitTypeTable[i].wname));
        }
        comboGrid->Add(new wxStaticText(p, wxID_ANY, "Physical material:"), 0, wxALIGN_CENTER_VERTICAL);
        comboGrid->Add(m_physMaterial);
        comboGrid->Add(new wxStaticText(p, wxID_ANY, "Model units:"), 0, wxALIGN_CENTER_VERTICAL);
        comboGrid->Add(m_units);
        s->Add(comboGrid, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        p->SetSizerAndFit(s);
        return p;
    }

    wxPanel* BuildPrintPrepPage(wxWindow* parent)
    {
        wxPanel* p = new wxPanel(parent);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

        m_chkFillBubbles = new wxCheckBox(p, wxID_ANY, "Fill air bubbles (hollow interior volumes)");
        s->Add(m_chkFillBubbles, 0, wxLEFT | wxTOP, 8);
        m_chkSealEntrances = new wxCheckBox(p, wxID_ANY, "  Seal entrances");
        m_chkSealSideTunnels = new wxCheckBox(p, wxID_ANY, "  Seal isolated side tunnels");
        s->Add(m_chkSealEntrances, 0, wxLEFT, 24);
        s->Add(m_chkSealSideTunnels, 0, wxLEFT | wxBOTTOM, 24);

        m_chkConnectParts = new wxCheckBox(p, wxID_ANY, "Connect parts sharing an edge");
        s->Add(m_chkConnectParts, 0, wxLEFT, 8);
        m_chkCornerTips = new wxCheckBox(p, wxID_ANY, "  Connect parts touching only at a corner");
        m_chkAllEdges = new wxCheckBox(p, wxID_ANY, "  Weld all shared edges");
        s->Add(m_chkCornerTips, 0, wxLEFT, 24);
        s->Add(m_chkAllEdges, 0, wxLEFT | wxBOTTOM, 24);

        m_chkDeleteFloaters = new wxCheckBox(p, wxID_ANY, "Delete floating objects smaller than:");
        wxBoxSizer* floatRow = new wxBoxSizer(wxHORIZONTAL);
        floatRow->Add(m_chkDeleteFloaters, 0, wxALIGN_CENTER_VERTICAL);
        floatRow->Add(LabeledField(p, "blocks", m_floatCount, 50), 0, wxLEFT, 4);
        s->Add(floatRow, 0, wxLEFT | wxBOTTOM, 8);

        m_chkHollow = new wxCheckBox(p, wxID_ANY, "Hollow out the bottom");
        s->Add(m_chkHollow, 0, wxLEFT, 8);
        wxBoxSizer* hollowRow = new wxBoxSizer(wxHORIZONTAL);
        hollowRow->Add(LabeledField(p, "  Wall thickness:", m_hollowThickness, 60), 0);
        s->Add(hollowRow, 0, wxLEFT, 24);
        m_chkSuperHollow = new wxCheckBox(p, wxID_ANY, "  Superhollow (more aggressive)");
        s->Add(m_chkSuperHollow, 0, wxLEFT | wxBOTTOM, 24);

        m_chkMeltSnow = new wxCheckBox(p, wxID_ANY, "Melt snow blocks");
        s->Add(m_chkMeltSnow, 0, wxLEFT | wxBOTTOM, 8);

        m_chkExportAll = new wxCheckBox(p, wxID_ANY, "Export precise/detailed partial blocks");
        s->Add(m_chkExportAll, 0, wxLEFT, 8);
        m_chkFatten = new wxCheckBox(p, wxID_ANY, "  Fatten partial blocks (print more reliably)");
        s->Add(m_chkFatten, 0, wxLEFT | wxBOTTOM, 24);

        wxStaticBoxSizer* dbgBox = new wxStaticBoxSizer(wxHORIZONTAL, p, "Debug");
        m_chkShowParts = new wxCheckBox(p, wxID_ANY, "Show separated parts");
        m_chkShowWelds = new wxCheckBox(p, wxID_ANY, "Show welds");
        dbgBox->Add(m_chkShowParts, 0, wxRIGHT, 10);
        dbgBox->Add(m_chkShowWelds, 0);
        s->Add(dbgBox, 0, wxEXPAND | wxALL, 8);

        p->SetSizerAndFit(s);
        return p;
    }

    wxPanel* BuildAdvancedPage(wxWindow* parent)
    {
        wxPanel* p = new wxPanel(parent);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);

        m_chkMakeGroups = new wxCheckBox(p, wxID_ANY, "Make each group a separate object (OBJ)");
        m_chkSeparateTypes = new wxCheckBox(p, wxID_ANY, "Export separate types (each block type its own group)");
        m_chkIndividualBlocks = new wxCheckBox(p, wxID_ANY, "Individual blocks (USD instancing)");
        m_chkMaterialPerFamily = new wxCheckBox(p, wxID_ANY, "Material per family (mosaic textures only)");
        m_chkSplitByType = new wxCheckBox(p, wxID_ANY, "Split by block type (separate material per type)");
        m_chkCustomMaterial = new wxCheckBox(p, wxID_ANY, "Extended PBR materials/textures where available");
        m_chkExportMDL = new wxCheckBox(p, wxID_ANY, "Export MDL shaders (USD only)");
        m_chkDoubledBillboards = new wxCheckBox(p, wxID_ANY, "Doubled billboards (two single-sided faces back to back)");
        m_chkDecimate = new wxCheckBox(p, wxID_ANY, "Simplify mesh (reduce polygon count)");
        for (auto* c : {m_chkMakeGroups, m_chkSeparateTypes, m_chkIndividualBlocks, m_chkMaterialPerFamily,
                        m_chkSplitByType, m_chkCustomMaterial, m_chkExportMDL, m_chkDoubledBillboards, m_chkDecimate})
            s->Add(c, 0, wxLEFT | wxTOP, 8);

        p->SetSizerAndFit(s);
        return p;
    }

    // ponytail: per-fileType fields load once at dialog-open time for whatever fileType
    // was current, rather than live-reloading when the File Type dropdown changes (Windows
    // does reload live). Upgrade path: bind m_typeChoice's selection event to re-run this.
    void LoadFromEfd()
    {
        int ft = m_efd.fileType;
        if (ft < 0 || ft >= FILE_TYPE_TOTAL) {
            ft = FILE_TYPE_WAVEFRONT_ABS_OBJ;
            m_efd.fileType = ft;
        }
        m_typeChoice->SetSelection(ft);

        m_rot0->SetValue(!!m_efd.radioRotate0);
        m_rot90->SetValue(!!m_efd.radioRotate90);
        m_rot180->SetValue(!!m_efd.radioRotate180);
        m_rot270->SetValue(!!m_efd.radioRotate270);
        m_chkCenter->SetValue(!!m_efd.chkCenterModel);
        m_chkZUp->SetValue(!!m_efd.chkMakeZUp[ft]);
        m_chkZip->SetValue(!!m_efd.chkCreateZip[ft]);
        m_chkCreateFiles->SetValue(m_efd.chkCreateModelFiles[ft] || !m_efd.chkCreateZip[ft]);

        if (m_efd.radioExportMtlColors[ft]) m_matColors->SetValue(true);
        else if (m_efd.radioExportSolidTexture[ft]) m_matSolidTex->SetValue(true);
        else if (m_efd.radioExportTileTextures[ft]) m_matTiles->SetValue(true);
        else if (m_efd.radioExportFullTexture[ft]) m_matFullTex->SetValue(true);
        else m_matNone->SetValue(true);
        m_tileDir->SetValue(wxString::FromUTF8(m_efd.tileDirString));
        m_chkRGB->SetValue(!!m_efd.chkTextureRGB);
        m_chkA->SetValue(!!m_efd.chkTextureA);
        m_chkRGBA->SetValue(!!m_efd.chkTextureRGBA);
        m_chkBiome->SetValue(!!m_efd.chkBiome);
        m_chkBorders->SetValue(!!m_efd.chkBlockFacesAtBorders);
        m_chkLeaves->SetValue(!!m_efd.chkLeavesSolid);
        m_chkComposite->SetValue(!!m_efd.chkCompositeOverlay);
        m_lightScale->SetValue(wxString::Format("%g", m_efd.scaleLightsVal > 0 ? m_efd.scaleLightsVal : 15.0f));
        m_emitScale->SetValue(wxString::Format("%g", m_efd.scaleEmittersVal > 0 ? m_efd.scaleEmittersVal : 20.0f));

        if (m_efd.radioScaleToMaterial) m_scaleMaterial->SetValue(true);
        else if (m_efd.radioScaleByCost) m_scaleCost->SetValue(true);
        else if (m_efd.radioScaleByBlock) m_scaleBlock->SetValue(true);
        else m_scaleHeight->SetValue(true);
        m_modelHeight->SetValue(wxString::Format("%g", m_efd.modelHeightVal > 0 ? m_efd.modelHeightVal : 5.0f));
        m_blockSize->SetValue(wxString::Format("%g", m_efd.blockSizeVal[ft] > 0 ? m_efd.blockSizeVal[ft] : 1.0f));
        m_cost->SetValue(wxString::Format("%0.2f", m_efd.costVal > 0 ? m_efd.costVal : 25.0f));
        int material = m_efd.comboPhysicalMaterial[ft];
        if (material < 0 || material >= MTL_COST_TABLE_SIZE) material = PRINT_MATERIAL_FULL_COLOR_SANDSTONE;
        int units = m_efd.comboModelUnits[ft];
        if (units < 0 || units >= MODELS_UNITS_TABLE_SIZE) units = UNITS_MILLIMETER;
        m_efd.comboPhysicalMaterial[ft] = material;
        m_efd.comboModelUnits[ft] = units;
        m_physMaterial->SetSelection(material);
        m_units->SetSelection(units);

        m_chkFillBubbles->SetValue(!!m_efd.chkFillBubbles);
        m_chkSealEntrances->SetValue(!!m_efd.chkSealEntrances);
        m_chkSealSideTunnels->SetValue(!!m_efd.chkSealSideTunnels);
        m_chkConnectParts->SetValue(!!m_efd.chkConnectParts);
        m_chkCornerTips->SetValue(!!m_efd.chkConnectCornerTips);
        m_chkAllEdges->SetValue(!!m_efd.chkConnectAllEdges);
        m_chkDeleteFloaters->SetValue(!!m_efd.chkDeleteFloaters);
        m_floatCount->SetValue(wxString::Format("%d", m_efd.floaterCountVal > 0 ? m_efd.floaterCountVal : 16));
        m_chkHollow->SetValue(!!m_efd.chkHollow[ft]);
        m_hollowThickness->SetValue(wxString::Format("%g", m_efd.hollowThicknessVal[ft] > 0 ? m_efd.hollowThicknessVal[ft] : 3.0f));
        m_chkSuperHollow->SetValue(!!m_efd.chkSuperHollow[ft]);
        m_chkMeltSnow->SetValue(!!m_efd.chkMeltSnow);
        m_chkExportAll->SetValue(!!m_efd.chkExportAll);
        m_chkFatten->SetValue(!!m_efd.chkFatten);
        m_chkShowParts->SetValue(!!m_efd.chkShowParts);
        m_chkShowWelds->SetValue(!!m_efd.chkShowWelds);

        m_chkMakeGroups->SetValue(!!m_efd.chkMakeGroupsObjects);
        m_chkSeparateTypes->SetValue(!!m_efd.chkSeparateTypes);
        m_chkIndividualBlocks->SetValue(!!m_efd.chkIndividualBlocks[ft]);
        m_chkMaterialPerFamily->SetValue(!!m_efd.chkMaterialPerFamily);
        m_chkSplitByType->SetValue(!!m_efd.chkSplitByBlockType);
        m_chkCustomMaterial->SetValue(!!m_efd.chkCustomMaterial[ft]);
        m_chkExportMDL->SetValue(!!m_efd.chkExportMDL);
        m_chkDoubledBillboards->SetValue(!!m_efd.chkDoubledBillboards);
        m_chkDecimate->SetValue(!!m_efd.chkDecimate);
    }

    // Returns false (and shows an error) if validation fails; caller should not close the dialog.
    bool SaveToEfd(int targetFileType = -1)
    {
        int ft = (targetFileType != -1) ? targetFileType : m_typeChoice->GetSelection();
        if (ft < 0 || ft >= FILE_TYPE_TOTAL) ft = 0;
        m_efd.fileType = ft;

        long v;
        auto parseInt = [&](wxTextCtrl* c, int& out) -> bool {
            return c->GetValue().Trim().ToLong(&v) && (out = (int)v, true);
        };
        auto parseFloat = [&](wxTextCtrl* c, float& out) -> bool {
            double d;
            if (!c->GetValue().Trim().ToDouble(&d)) return false;
            out = (float)d; return true;
        };

        int minx, miny, minz, maxx, maxy, maxz;
        if (!parseInt(m_minX, minx) || !parseInt(m_minY, miny) || !parseInt(m_minZ, minz) ||
            !parseInt(m_maxX, maxx) || !parseInt(m_maxY, maxy) || !parseInt(m_maxZ, maxz)) {
            wxMessageBox("Bad (non-numeric) value detected in the bounds fields.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }
        m_efd.minxVal = minx; m_efd.minyVal = miny; m_efd.minzVal = minz;
        m_efd.maxxVal = maxx; m_efd.maxyVal = maxy; m_efd.maxzVal = maxz;

        m_efd.radioRotate0 = m_rot0->GetValue(); m_efd.radioRotate90 = m_rot90->GetValue();
        m_efd.radioRotate180 = m_rot180->GetValue(); m_efd.radioRotate270 = m_rot270->GetValue();
        m_efd.chkCenterModel = m_chkCenter->GetValue();
        m_efd.chkMakeZUp[ft] = m_chkZUp->GetValue();
        m_efd.chkCreateZip[ft] = m_chkZip->GetValue();
        m_efd.chkCreateModelFiles[ft] = m_chkCreateFiles->GetValue();

        m_efd.radioExportNoMaterials[ft] = m_matNone->GetValue();
        m_efd.radioExportMtlColors[ft] = m_matColors->GetValue();
        m_efd.radioExportSolidTexture[ft] = m_matSolidTex->GetValue();
        m_efd.radioExportFullTexture[ft] = m_matFullTex->GetValue();
        m_efd.radioExportTileTextures[ft] = m_matTiles->GetValue();
        wxScopedCharBuffer tileDir = m_tileDir->GetValue().utf8_str();
        if (!tileDir || tileDir.length() >= MAX_PATH) {
            wxMessageBox(wxString::Format("Tile directory must be at most %d UTF-8 bytes.", MAX_PATH - 1),
                         "Folder name error", wxOK | wxICON_ERROR, this);
            return false;
        }
        memcpy(m_efd.tileDirString, tileDir.data(), tileDir.length() + 1);
        {
            static const char badchar[] = "<>|?*:/\\";
            for (size_t i = 0; i < sizeof(badchar) - 1; i++) {
                if (strchr(m_efd.tileDirString, badchar[i])) {
                    wxMessageBox("Illegal character <>|?*:/\\ detected in the tile subdirectory name; "
                                 "it cannot be a path, just a simple folder name.", "Folder name error",
                                 wxOK | wxICON_ERROR, this);
                    return false;
                }
            }
        }
        m_efd.chkTextureRGB = m_chkRGB->GetValue();
        m_efd.chkTextureA = m_chkA->GetValue();
        m_efd.chkTextureRGBA = m_chkRGBA->GetValue();
        m_efd.chkBiome = m_chkBiome->GetValue();
        m_efd.chkBlockFacesAtBorders = m_chkBorders->GetValue();
        m_efd.chkLeavesSolid = m_chkLeaves->GetValue();
        m_efd.chkCompositeOverlay = m_chkComposite->GetValue();

        m_efd.radioScaleToHeight = m_scaleHeight->GetValue();
        m_efd.radioScaleToMaterial = m_scaleMaterial->GetValue();
        m_efd.radioScaleByBlock = m_scaleBlock->GetValue();
        m_efd.radioScaleByCost = m_scaleCost->GetValue();
        m_efd.comboPhysicalMaterial[ft] = m_physMaterial->GetSelection();
        m_efd.comboModelUnits[ft] = m_units->GetSelection();
        if (m_efd.comboPhysicalMaterial[ft] < 0 || m_efd.comboPhysicalMaterial[ft] >= MTL_COST_TABLE_SIZE ||
            m_efd.comboModelUnits[ft] < 0 || m_efd.comboModelUnits[ft] >= MODELS_UNITS_TABLE_SIZE) {
            wxMessageBox("Choose a valid physical material and model unit.", "Value error",
                         wxOK | wxICON_ERROR, this);
            return false;
        }

        if (!parseFloat(m_modelHeight, m_efd.modelHeightVal) ||
            !parseFloat(m_blockSize, m_efd.blockSizeVal[ft]) ||
            !parseFloat(m_cost, m_efd.costVal) ||
            !parseInt(m_floatCount, m_efd.floaterCountVal) ||
            !parseFloat(m_hollowThickness, m_efd.hollowThicknessVal[ft]) ||
            (ft == FILE_TYPE_USD && (!parseFloat(m_lightScale, m_efd.scaleLightsVal) ||
                                     !parseFloat(m_emitScale, m_efd.scaleEmittersVal)))) {
            wxMessageBox("Bad (non-numeric) value detected in options dialog.", "Non-numeric value error",
                         wxOK | wxICON_ERROR, this);
            return false;
        }

        if (m_efd.radioScaleToHeight && m_efd.modelHeightVal <= 0.0f) {
            wxMessageBox("Model height must be a positive number.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }
        if (m_efd.radioScaleByBlock && m_efd.blockSizeVal[ft] <= 0.0f) {
            wxMessageBox("Block size must be a positive number.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }
        if (ft == FILE_TYPE_USD && (m_efd.scaleLightsVal < 0.0f || m_efd.scaleEmittersVal < 0.0f)) {
            wxMessageBox("Light/emit scale must be non-negative.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }
        if (m_efd.radioScaleByCost) {
            int pm = m_efd.comboPhysicalMaterial[ft];
            if (m_efd.costVal <= (gMtlCostTable[pm].costHandling + gMtlCostTable[pm].costMinimum)) {
                wxMessageBox("The target cost is below the minimum handling + material cost for this material.",
                             "Value error", wxOK | wxICON_ERROR, this);
                return false;
            }
        }
        if (m_efd.chkDeleteFloaters && m_efd.floaterCountVal < 0) {
            wxMessageBox("Floating-object deletion size cannot be negative.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }
        if (m_efd.chkHollow[ft] && m_efd.hollowThicknessVal[ft] < 0.0f) {
            wxMessageBox("Hollow wall thickness cannot be negative.", "Value error", wxOK | wxICON_ERROR, this);
            return false;
        }

        // Sub-options are meaningless (and ignored on Windows too) if their parent checkbox is off.
        m_efd.chkFillBubbles = m_chkFillBubbles->GetValue();
        m_efd.chkSealEntrances = m_efd.chkFillBubbles ? m_chkSealEntrances->GetValue() : 0;
        m_efd.chkSealSideTunnels = m_efd.chkFillBubbles ? m_chkSealSideTunnels->GetValue() : 0;
        m_efd.chkConnectParts = m_chkConnectParts->GetValue();
        m_efd.chkConnectCornerTips = m_efd.chkConnectParts ? m_chkCornerTips->GetValue() : 0;
        m_efd.chkConnectAllEdges = m_efd.chkConnectParts ? m_chkAllEdges->GetValue() : 0;
        m_efd.chkDeleteFloaters = m_chkDeleteFloaters->GetValue();
        m_efd.chkHollow[ft] = m_chkHollow->GetValue();
        m_efd.chkSuperHollow[ft] = m_efd.chkHollow[ft] ? m_chkSuperHollow->GetValue() : 0;
        m_efd.chkMeltSnow = m_chkMeltSnow->GetValue();
        m_efd.chkExportAll = m_chkExportAll->GetValue();
        m_efd.chkFatten = m_efd.chkExportAll ? m_chkFatten->GetValue() : 0;
        m_efd.chkShowParts = m_chkShowParts->GetValue();
        m_efd.chkShowWelds = m_chkShowWelds->GetValue();

        m_efd.chkMakeGroupsObjects = m_chkMakeGroups->GetValue();
        m_efd.chkSeparateTypes = m_chkSeparateTypes->GetValue();
        m_efd.chkIndividualBlocks[ft] = m_chkIndividualBlocks->GetValue();
        m_efd.chkMaterialPerFamily = m_chkMaterialPerFamily->GetValue();
        m_efd.chkSplitByBlockType = m_chkSplitByType->GetValue();
        m_efd.chkCustomMaterial[ft] = m_chkCustomMaterial->GetValue();
        m_efd.chkExportMDL = m_chkExportMDL->GetValue();
        m_efd.chkDoubledBillboards = m_chkDoubledBillboards->GetValue();
        m_efd.chkDecimate = m_chkDecimate->GetValue();

        return true;
    }

    void OnButton(wxCommandEvent& e)
    {
        wxString path = m_pathCtrl->GetValue().Trim();
        if (path.IsEmpty()) {
            wxMessageBox("Please choose an output file path.", "Missing path", wxOK | wxICON_WARNING, this);
            return;
        }
        if (path.StartsWith("~/") || path == "~") {
            const char* home = getenv("HOME");
            if (home) path = wxString::FromUTF8(home) + path.Mid(1);
        }
        int sel = m_typeChoice->GetSelection();
        if (sel < 0 || sel >= FILE_TYPE_TOTAL) sel = 0;
        if (!path.Lower().EndsWith(kTypeExts[sel])) path += kTypeExts[sel];
        m_pathCtrl->SetValue(path);

        if (!SaveToEfd()) return;   // validation failed; stay open (matches Windows' return FALSE pattern)
        EndModal(wxID_OK);
    }
};

}  // namespace

int doExportDialog(wxWindow* parent, ExportFileData& efd,
                   wchar_t* outputPath, size_t outputPathLen,
                   int selMinX, int selMinY, int selMinZ,
                   int selMaxX, int selMaxY, int selMaxZ)
{
    ExportOptionsDialog dlg(parent, efd, outputPath[0] ? wxString(outputPath) : wxString(),
                            selMinX, selMinY, selMinZ, selMaxX, selMaxY, selMaxZ);
    if (dlg.ShowModal() != wxID_OK) return wxID_CANCEL;

    const wchar_t* convertedPath = dlg.GetPath().wc_str();
    size_t convertedPathLength = wcslen(convertedPath);
    if (convertedPathLength >= outputPathLen) {
        wxMessageBox("The export path is too long.", "Path error", wxOK | wxICON_ERROR, parent);
        return wxID_CANCEL;
    }
    memcpy(outputPath, convertedPath, (convertedPathLength + 1) * sizeof(wchar_t));
    return wxID_OK;
}
