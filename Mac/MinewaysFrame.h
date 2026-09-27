#pragma once
#include <wx/wx.h>
#include "MapPanel.h"

struct ExportFileData;   // full definition comes from stdafx.h -> blockInfo.h; forward decl is enough here

class MinewaysFrame : public wxFrame {
public:
    explicit MinewaysFrame(wxWindow* parent);
    ~MinewaysFrame();

    // Called by MapPanel mouse handlers (shared state lives here)
    void UpdateStatusBar(int mx, int mz, int my, const char* label, int type, int dataVal, int biome);
    void UpdateBottomSlider(int depth);
    void OnMapPanelSize(int w, int h);

    // Exposed for Mac/ImportSettings.cpp (header re-import + script execution), which needs
    // to drive the same world/terrain/export/navigation actions the menu items trigger.
    wxString LoadWorldFromDir(const wxString& dir);   // "" on success, error message on failure
    void LoadTestBlockWorld();
    wxString LoadSchematic(const wxString& path);   // "" on success, error message on failure
    void RunExport(ExportFileData& efd, const wchar_t* outputPath);
    void RedrawMap();
    void SetTerrainFile(const wxString& path);
    bool SwitchToNether();   // false if this world has no Nether (schematic/test world)
    bool SwitchToTheEnd();   // false if this world has no End
    void JumpToSpawn();
    void JumpToPlayer();
    bool JumpToModel();      // false if nothing is selected

    // Recent Exports (issue #138 parity): re-opens a previously-exported file the same way
    // File > Import Settings does (or reloads it as a world, for schematics). Exposed so
    // Mac/ImportSettings.cpp's CommandExportFile can record a freshly-written export.
    void AddToRecentExports(const wxString& path);

private:
    MapPanel*    m_mapPanel   = nullptr;
    wxSlider*    m_sliderTop  = nullptr;   // top-depth (ceiling)
    wxSlider*    m_sliderBot  = nullptr;   // bottom-depth (floor)
    wxStaticText* m_labelTop  = nullptr;
    wxStaticText* m_labelBot  = nullptr;

    void BuildMenu();
    void ClearLoadedWorld();

    // File menu
    void OnOpenWorld(wxCommandEvent&);
    void OnOpenFile(wxCommandEvent&);
    void OnTestBlockWorld(wxCommandEvent&);
    void OnWorldMenuItem(wxCommandEvent&);  // dynamic world-list items
    void OnGoToLocation(wxCommandEvent&);
    void OnChooseTerrainFile(wxCommandEvent&);
    void OnDefaultTerrain(wxCommandEvent&);
    void OnTerrainMenuItem(wxCommandEvent&);  // dynamic terrainExt*.png list items
    void OnCullingSchemes(wxCommandEvent&);
    void OnExportOBJ(wxCommandEvent&);
    void OnQuit(wxCommandEvent&);
    // View menu
    void OnSliderTop(wxCommandEvent&);
    void OnSliderBot(wxCommandEvent&);
    void OnUndoSelection(wxCommandEvent&);
    void OnJumpSpawn(wxCommandEvent&);
    void OnJumpPlayer(wxCommandEvent&);
    void OnJumpModel(wxCommandEvent&);
    void OnViewInformation(wxCommandEvent&);
    void OnViewHell(wxCommandEvent&);
    void OnViewEnd(wxCommandEvent&);
    void OnToggleWorldTypeBit(wxCommandEvent&);   // Show all/biomes/elevation/lighting/cave/obscured/water/grid
    void OnZoomOutFurther(wxCommandEvent&);
    void OnSelectAll(wxCommandEvent&);
    void OnReloadWorld(wxCommandEvent&);
    void OnRepeatExport(wxCommandEvent&);
    void OnExportMap(wxCommandEvent&);
    void OnDownloadTerrainFiles(wxCommandEvent&);
    void OnImportSettings(wxCommandEvent&);
    void OnRecentExportItem(wxCommandEvent&);  // dynamic Recent Exports submenu items
    void PopulateRecentExportsMenu();
    wxMenu* m_recentExportsMenu = nullptr;
    // Help menu
    void OnAbout(wxCommandEvent&);
    void OnHelpURL(wxCommandEvent&);   // keyboard/troubleshooting/documentation/report-a-bug (same handler, ID picks URL)
    void OnGiveMoreExportMemory(wxCommandEvent&);

    wxDECLARE_EVENT_TABLE();
};

// IDs
enum {
    ID_OPEN_WORLD      = wxID_HIGHEST + 1,
    ID_OPEN_FILE,
    ID_TEST_BLOCK_WORLD,
    ID_CHOOSE_TERRAIN,
    ID_DEFAULT_TERRAIN,
    ID_CULLING_SCHEMES,
    ID_EXPORT_OBJ,
    ID_SLIDER_TOP,
    ID_SLIDER_BOT,
    ID_GO_TO_LOCATION,
    // View menu
    ID_VIEW_UNDOSELECTION,
    ID_JUMP_SPAWN,
    ID_JUMP_PLAYER,
    ID_JUMP_MODEL,
    ID_VIEW_INFORMATION,
    ID_VIEW_HELL,
    ID_VIEW_END,
    ID_SHOWALL,
    ID_SHOWBIOMES,
    ID_ELEVATION_SHADING,
    ID_LIGHTING,
    ID_CAVEMODE,
    ID_HIDEOBSCURED,
    ID_TRANSPARENT_WATER,
    ID_MAPGRID,
    ID_ZOOMOUTFURTHER,
    ID_SELECT_ALL,
    ID_RELOAD_WORLD,
    ID_REPEAT_EXPORT,
    ID_EXPORT_MAP,
    ID_DOWNLOAD_TERRAIN_FILES,
    ID_IMPORT_SETTINGS,
    // Help menu
    ID_HELP_KEYBOARD,
    ID_HELP_TROUBLESHOOTING,
    ID_HELP_DOCUMENTATION,
    ID_HELP_REPORT_BUG,
    ID_HELP_GIVE_MORE_MEMORY,
    // Dynamic world-list items occupy [ID_WORLD_ITEM_BASE, ID_WORLD_ITEM_BASE+MAX_WORLDS)
    ID_WORLD_ITEM_BASE = wxID_HIGHEST + 100,
    MAX_WORLDS         = 50,
    // Dynamic terrainExt*.png list items occupy [ID_TERRAIN_ITEM_BASE, ID_TERRAIN_ITEM_BASE+MAX_TERRAIN_FILES)
    ID_TERRAIN_ITEM_BASE = wxID_HIGHEST + 200,
    MAX_TERRAIN_FILES    = 45,
    // Recent Exports submenu items occupy [ID_RECENT_EXPORT_BASE, ID_RECENT_EXPORT_BASE+MAX_RECENT_EXPORTS)
    ID_RECENT_EXPORT_BASE = wxID_HIGHEST + 300,
    MAX_RECENT_EXPORTS    = 5,
    ID_RECENT_EXPORTS_PLACEHOLDER = wxID_HIGHEST + 310,
};
