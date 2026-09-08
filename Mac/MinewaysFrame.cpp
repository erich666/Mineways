// MinewaysFrame.cpp — wxWidgets main window, replacing Win32 Mineways.cpp
// Global state mirrors the static variables in Win/Mineways.cpp.
#include <wx/wx.h>
#include <wx/config.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/progdlg.h>
#include "MinewaysFrame.h"
#include "MapPanel.h"
#include "LocationDialog.h"
#include "ExportDialog.h"
#include "MacCullingSchemes.h"
#include "ImportSettings.h"

// Win32 shims + project headers (stdafx.h routes to compat.h on non-WIN32)
#include "stdafx.h"
#include "CullingSchemes.h"   // applyCullingScheme/isBlockCulled declarations
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

// ─── Globals (mirror of Win/Mineways.cpp static globals) ────────────────────
#define MINZOOM 1.0f
static Options gOptions = {
    0,
    BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER |
    BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN | BLF_FLATTEN_SMALL,
    0x0, false, INITIAL_CACHE_SIZE, nullptr
};
static WorldGuide  gWorldGuide;
static int         gVersionID       = 0;
static int         gMinecraftVersion = 0;
static int         gMaxHeight       = INIT_MAP_MAX_HEIGHT;
static int         gMinHeight       = 0;
static double      gCurX            = 0.0;
static double      gCurZ            = 0.0;
static double      gCurScale        = 1.0;
static int         gCurDepth        = INIT_MAP_MAX_HEIGHT;
static BOOL        gLoaded          = FALSE;
static int         gHitsFound[4]    = {0,0,0,0};
static int         gTargetDepth     = SEA_LEVEL;
static BOOL        gHighlightOn     = FALSE;
static int         gStartHiX        = 0;
static int         gStartHiZ        = 0;
static wchar_t     gSelectTerrainPathAndName[MAX_PATH_AND_FILE];
static wchar_t     gSelectTerrainDir[MAX_PATH_AND_FILE];
static wchar_t     gWorldPathDefault[MAX_PATH_AND_FILE];
static wchar_t     gPreferredSeparatorString[2] = { L'/', 0 };
static int         gSpawnX = 0, gSpawnY = 64, gSpawnZ = 0;
static int         gPlayerX = 0, gPlayerY = 64, gPlayerZ = 0;
static int         gOverworldHideStatus = 0;   // HIDEOBSCURED bit, saved across a Nether visit
static float       gMinZoom         = MINZOOM;

// pixel buffer for map (owned here; MapPanel reads it)
static unsigned char* gMapBits  = nullptr;
static int            gMapWidth  = 0;
static int            gMapHeight = 0;

// Scanned world list (parallel to gWorlds in Win/Mineways.cpp)
static wxString gWorldDirs[MAX_WORLDS];
static wxString gWorldDisplayNames[MAX_WORLDS];
static int      gNumWorlds = 0;

// Scanned terrainExt*.png files alongside the app bundle (parallel to gTerrainFiles
// in Win/Mineways.cpp) — alternate texture packs shipped next to the executable,
// not a "recently used" list.
static wxString gTerrainFilePaths[MAX_TERRAIN_FILES];
static wxString gTerrainFileNames[MAX_TERRAIN_FILES];
static int      gNumTerrainFiles = 0;

// Persistent export settings (survive across dialog invocations); mirrors
// Windows' static epd/gExportViewData pattern.
static ExportFileData gEfd = {};
static wchar_t        gExportPath[4096] = {};

// Recent Exports (issue #138 parity): most-recently-exported-or-reopened files, newest
// first. Persisted via wxConfig as recentExport0..recentExport{MAX_RECENT_EXPORTS-1}
// (Windows uses the registry at the equivalent RecentExports key).
static wxString gRecentExports[MAX_RECENT_EXPORTS];
static int      gRecentExportCount = 0;

// The singleton frame (so MapPanel event handlers can reach it)
MinewaysFrame* gFrame = nullptr;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void splitPath(const wchar_t* pathAndName, wchar_t* dir, wchar_t* name)
{
    const wchar_t* slash = wcsrchr(pathAndName, L'/');
    if (!slash) slash = wcsrchr(pathAndName, L'\\');
    if (slash) {
        if (dir) { wcsncpy(dir, pathAndName, slash - pathAndName); dir[slash - pathAndName] = 0; }
        if (name) wcscpy(name, slash + 1);
    } else {
        if (dir)  wcscpy(dir, L".");
        if (name) wcscpy(name, pathAndName);
    }
}

// Progress callback stub (no UI progress for now)
static void progressCB(float /*p*/, wchar_t* /*buf*/) {}

// Persist the full export dialog settings across launches, as a hex-encoded raw byte
// blob (same idiom as MacCullingSchemes.cpp's culled[] persistence). A "size" key guards
// against loading a blob written by a build with a different ExportFileData layout.
static void SaveExportFileData(const ExportFileData& e)
{
    wxConfig cfg("Mineways");
    cfg.Write("exportFileDataSize", (long)sizeof(ExportFileData));
    wxString hex;
    hex.Alloc(sizeof(ExportFileData) * 2);
    const unsigned char* bytes = (const unsigned char*)&e;
    for (size_t i = 0; i < sizeof(ExportFileData); i++)
        hex += wxString::Format("%02x", bytes[i]);
    cfg.Write("exportFileData", hex);
}

static bool LoadExportFileData(ExportFileData& e)
{
    wxConfig cfg("Mineways");
    long savedSize = 0;
    wxString hex;
    if (!cfg.Read("exportFileDataSize", &savedSize) || savedSize != (long)sizeof(ExportFileData))
        return false;
    if (!cfg.Read("exportFileData", &hex) || hex.length() != sizeof(ExportFileData) * 2)
        return false;
    ExportFileData loaded = {};
    unsigned char* bytes = (unsigned char*)&loaded;
    for (size_t i = 0; i < sizeof(ExportFileData); i++) {
        unsigned long v = 0;
        if (!hex.Mid(2 * i, 2).ToULong(&v, 16)) return false;
        bytes[i] = (unsigned char)v;
    }
    if (loaded.fileType < 0 || loaded.fileType >= FILE_TYPE_TOTAL) return false;
    for (int i = 0; i < FILE_TYPE_TOTAL; i++) {
        if (loaded.comboPhysicalMaterial[i] < 0 || loaded.comboPhysicalMaterial[i] >= MTL_COST_TABLE_SIZE ||
            loaded.comboModelUnits[i] < 0 || loaded.comboModelUnits[i] >= MODELS_UNITS_TABLE_SIZE)
            return false;
    }
    loaded.tileDirString[MAX_PATH - 1] = '\0';
    e = loaded;
    return true;
}

// Recent Exports persistence: written immediately on every change (not just at app exit),
// since Windows' saveRecentExportsToRegistry() does the same right after addToRecentExports.
static void SaveRecentExportsToConfig()
{
    wxConfig cfg("Mineways");
    for (int i = 0; i < MAX_RECENT_EXPORTS; i++) {
        wxString key = wxString::Format("recentExport%d", i);
        if (i < gRecentExportCount) cfg.Write(key, gRecentExports[i]);
        else cfg.DeleteEntry(key);
    }
}

// Loads the persisted list, dropping entries whose files no longer exist (same as Windows'
// loadRecentExportsFromRegistry). Called once at startup.
static void LoadRecentExportsFromConfig()
{
    wxConfig cfg("Mineways");
    gRecentExportCount = 0;
    for (int i = 0; i < MAX_RECENT_EXPORTS; i++) {
        wxString path;
        if (cfg.Read(wxString::Format("recentExport%d", i), &path) && !path.IsEmpty() && wxFileExists(path))
            gRecentExports[gRecentExportCount++] = path;
    }
}

// Export progress callback, mirroring Win/Mineways.cpp's updateProgress(): SaveVolume
// passes progress < 0 (e.g. -999.0f) to mean "update the message only, leave the bar
// position alone" — Windows has no export-cancel mechanism either, so this is a plain
// indicator (no wxPD_CAN_ABORT), not a real cancel button.
static wxProgressDialog* gExportProgressDlg = nullptr;
static int gExportProgressLastValue = 0;
static wxString gExportProgressLastMsg;
static void ExportProgressCB(float progress, wchar_t* buf)
{
    if (!gExportProgressDlg) return;
    if (progress >= 0.0f)
        gExportProgressLastValue = (int)wxMax(0.0f, wxMin(100.0f, progress * 100.0f));
    if (buf && wcslen(buf) > 0)
        gExportProgressLastMsg = wxString(buf);
    gExportProgressDlg->Update(gExportProgressLastValue, gExportProgressLastMsg);
}

static void ClearLoadedWorldData()
{
    CloseAll();
    free(gWorldGuide.sch.blocks); gWorldGuide.sch.blocks = nullptr;
    free(gWorldGuide.sch.data);   gWorldGuide.sch.data   = nullptr;
    memset(&gWorldGuide, 0, sizeof(gWorldGuide));
    gWorldGuide.type = WORLD_UNLOADED_TYPE;
    gLoaded = FALSE;
}

// Returns 0 on success, non-zero on error (mirrors Win loadSchematic / loadSpongeSchematic).
static int LoadSchematicFile(const wchar_t* pathAndFile, bool isSponge)
{
    if (isSponge) {
        int w = 0, h = 0, l = 0;
        unsigned char* blocks = nullptr;
        unsigned char* data   = nullptr;
        int rv = GetSpongeSchematic(pathAndFile, &w, &h, &l, &blocks, &data);
        if (rv != 1) { free(blocks); free(data); return 100 + (rv == -1 ? 1 : 5); }
        gWorldGuide.sch.width  = w; gWorldGuide.sch.height = h; gWorldGuide.sch.length = l;
        if (!nbtGetValidatedSchematicVolume(w, h, l, &gWorldGuide.sch.numBlocks)) {
            free(blocks); free(data);
            return 105;
        }
        gWorldGuide.sch.blocks = blocks; gWorldGuide.sch.data = data;
        gVersionID = 2586;
    } else {
        if (GetSchematicWord(pathAndFile, "Width",  &gWorldGuide.sch.width)  != 1) return 101;
        if (GetSchematicWord(pathAndFile, "Height", &gWorldGuide.sch.height) != 1) return 102;
        if (GetSchematicWord(pathAndFile, "Length", &gWorldGuide.sch.length) != 1) return 103;
        if (!nbtGetValidatedSchematicVolume(gWorldGuide.sch.width, gWorldGuide.sch.height,
                                            gWorldGuide.sch.length, &gWorldGuide.sch.numBlocks)) return 104;
        gWorldGuide.sch.blocks = (unsigned char*)malloc(gWorldGuide.sch.numBlocks);
        gWorldGuide.sch.data   = (unsigned char*)malloc(gWorldGuide.sch.numBlocks);
        if (!gWorldGuide.sch.blocks || !gWorldGuide.sch.data) {
            free(gWorldGuide.sch.blocks); gWorldGuide.sch.blocks = nullptr;
            free(gWorldGuide.sch.data);   gWorldGuide.sch.data   = nullptr;
            return 105;
        }
        if (GetSchematicBlocksAndData(pathAndFile, gWorldGuide.sch.numBlocks,
                                       gWorldGuide.sch.blocks, gWorldGuide.sch.data) != 1) {
            free(gWorldGuide.sch.blocks); gWorldGuide.sch.blocks = nullptr;
            free(gWorldGuide.sch.data);   gWorldGuide.sch.data   = nullptr;
            return 105;
        }
        gVersionID = 1343;  // MC 1.12.2
    }

    gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
    gMaxHeight = MAX_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gMinHeight = ZERO_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gWorldGuide.minHeight = gMinHeight;
    gWorldGuide.maxHeight = gMaxHeight;
    return 0;
}

// ─── MapPanel calls this to redraw into gMapBits, then Refresh() itself ──────
// (called from MapPanel.cpp via friend access or by having this function visible)
void RedrawMapIntoBuffer()
{
    if (!gLoaded || !gMapBits || gMapWidth == 0 || gMapHeight == 0) {
        if (gMapBits)
            memset(gMapBits, 0xff, (size_t)gMapWidth * gMapHeight * 4);
        return;
    }
    DrawMap(&gWorldGuide, gCurX, gCurZ,
            gCurDepth - gMinHeight, gMaxHeight,
            gMapWidth, gMapHeight, gCurScale,
            gMapBits, &gOptions, gHitsFound,
            progressCB, gMinecraftVersion, gVersionID);
    // DrawMap writes R,G,B,0xFF per pixel — already correct for wxImage (no swap needed)
}

// Accessors used by MapPanel
double& GetCurX()     { return gCurX; }
double& GetCurZ()     { return gCurZ; }
double& GetCurScale() { return gCurScale; }
float&  GetMinZoom()  { return gMinZoom; }
BOOL    IsLoaded()    { return gLoaded; }
unsigned char* GetMapBits()  { return gMapBits; }
int     GetMapWidth()        { return gMapWidth; }
int     GetMapHeight()       { return gMapHeight; }
int     GetMinHeight()  { return gMinHeight; }
int     GetMaxHeight()  { return gMaxHeight; }
int&    GetCurDepth()   { return gCurDepth; }
int&    GetTargetDepth() { return gTargetDepth; }
BOOL&   GetHighlightOn() { return gHighlightOn; }
int&    GetStartHiX()    { return gStartHiX; }
int&    GetStartHiZ()    { return gStartHiZ; }
Options*    GetOptions()    { return &gOptions; }
WorldGuide* GetWorldGuide() { return &gWorldGuide; }
ExportFileData* GetEfd()        { return &gEfd; }
wchar_t*        GetExportPathBuf() { return gExportPath; }   // 4096-wchar_t capacity buffer

// IDBlock wrapper (for status bar lookup)
const char* QueryBlock(int bx, int by, int* mx, int* my, int* mz, int* type, int* dataVal, int* biome)
{
    return IDBlock(bx, by, gCurX, gCurZ,
                   gMapWidth, gMapHeight, gMinHeight, gCurScale,
                   mx, my, mz, type, dataVal, biome,
                   gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
}

// ─── MinewaysFrame ────────────────────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(MinewaysFrame, wxFrame)
    EVT_MENU(ID_OPEN_WORLD,       MinewaysFrame::OnOpenWorld)
    EVT_MENU(ID_OPEN_FILE,        MinewaysFrame::OnOpenFile)
    EVT_MENU(ID_TEST_BLOCK_WORLD, MinewaysFrame::OnTestBlockWorld)
    EVT_MENU(ID_GO_TO_LOCATION,   MinewaysFrame::OnGoToLocation)
    EVT_MENU(ID_CHOOSE_TERRAIN,   MinewaysFrame::OnChooseTerrainFile)
    EVT_MENU(ID_DEFAULT_TERRAIN,  MinewaysFrame::OnDefaultTerrain)
    EVT_MENU_RANGE(ID_TERRAIN_ITEM_BASE, ID_TERRAIN_ITEM_BASE + MAX_TERRAIN_FILES - 1,
                   MinewaysFrame::OnTerrainMenuItem)
    EVT_MENU(ID_CULLING_SCHEMES,  MinewaysFrame::OnCullingSchemes)
    EVT_MENU(ID_EXPORT_OBJ,       MinewaysFrame::OnExportOBJ)
    EVT_MENU(wxID_EXIT,           MinewaysFrame::OnQuit)
    EVT_MENU(wxID_ABOUT,          MinewaysFrame::OnAbout)
    EVT_SLIDER(ID_SLIDER_TOP,     MinewaysFrame::OnSliderTop)
    EVT_SLIDER(ID_SLIDER_BOT,     MinewaysFrame::OnSliderBot)
    EVT_MENU(ID_VIEW_UNDOSELECTION, MinewaysFrame::OnUndoSelection)
    EVT_MENU(ID_JUMP_SPAWN,       MinewaysFrame::OnJumpSpawn)
    EVT_MENU(ID_JUMP_PLAYER,      MinewaysFrame::OnJumpPlayer)
    EVT_MENU(ID_JUMP_MODEL,       MinewaysFrame::OnJumpModel)
    EVT_MENU(ID_VIEW_INFORMATION, MinewaysFrame::OnViewInformation)
    EVT_MENU(ID_VIEW_HELL,        MinewaysFrame::OnViewHell)
    EVT_MENU(ID_VIEW_END,         MinewaysFrame::OnViewEnd)
    EVT_MENU(ID_SHOWALL,          MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_SHOWBIOMES,       MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_ELEVATION_SHADING, MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_LIGHTING,         MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_CAVEMODE,         MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_HIDEOBSCURED,     MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_TRANSPARENT_WATER, MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_MAPGRID,          MinewaysFrame::OnToggleWorldTypeBit)
    EVT_MENU(ID_ZOOMOUTFURTHER,   MinewaysFrame::OnZoomOutFurther)
    EVT_MENU(ID_SELECT_ALL,       MinewaysFrame::OnSelectAll)
    EVT_MENU(ID_RELOAD_WORLD,     MinewaysFrame::OnReloadWorld)
    EVT_MENU(ID_REPEAT_EXPORT,    MinewaysFrame::OnRepeatExport)
    EVT_MENU(ID_EXPORT_MAP,       MinewaysFrame::OnExportMap)
    EVT_MENU(ID_DOWNLOAD_TERRAIN_FILES, MinewaysFrame::OnDownloadTerrainFiles)
    EVT_MENU(ID_IMPORT_SETTINGS, MinewaysFrame::OnImportSettings)
    EVT_MENU_RANGE(ID_RECENT_EXPORT_BASE, ID_RECENT_EXPORT_BASE + MAX_RECENT_EXPORTS - 1,
                   MinewaysFrame::OnRecentExportItem)
    EVT_MENU(ID_HELP_KEYBOARD,         MinewaysFrame::OnHelpURL)
    EVT_MENU(ID_HELP_TROUBLESHOOTING,  MinewaysFrame::OnHelpURL)
    EVT_MENU(ID_HELP_DOCUMENTATION,    MinewaysFrame::OnHelpURL)
    EVT_MENU(ID_HELP_REPORT_BUG,       MinewaysFrame::OnHelpURL)
    EVT_MENU(ID_HELP_GIVE_MORE_MEMORY, MinewaysFrame::OnGiveMoreExportMemory)
    EVT_MENU_RANGE(ID_WORLD_ITEM_BASE, ID_WORLD_ITEM_BASE + MAX_WORLDS - 1,
                   MinewaysFrame::OnWorldMenuItem)
wxEND_EVENT_TABLE()

MinewaysFrame::MinewaysFrame(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, "Mineways", wxDefaultPosition, wxSize(960, 720))
{
    gFrame = this;

    // Separator always '/' on macOS
    SetSeparatorMap(gPreferredSeparatorString);
    SetSeparatorObj(gPreferredSeparatorString);

    // World guide: start unloaded
    gWorldGuide.type = WORLD_UNLOADED_TYPE;
    gWorldGuide.sch.blocks = gWorldGuide.sch.data = nullptr;
    gWorldGuide.nbtVersion = 0;

    // Default terrain file: bundle Resources/ first, then exe dir for standalone runs
    wxString terrainPath = wxStandardPaths::Get().GetResourcesDir() + "/terrainExt.png";
    if (!wxFileExists(terrainPath)) {
        wxFileName exeFN(wxStandardPaths::Get().GetExecutablePath());
        terrainPath = exeFN.GetPath() + "/terrainExt.png";
    }

    // Default world saves path
    wxString savesDir = wxGetHomeDir() + "/Library/Application Support/minecraft/saves";

    // Export defaults, matching Windows' "Export for Rendering" preset
    InitViewExportData(gEfd);

    // Restore persisted settings
    wxConfig cfg("Mineways");
    wxString cfgTerrain, cfgSaves, cfgExportPath;
    if (cfg.Read("terrainPath", &cfgTerrain) && wxFileExists(cfgTerrain))
        terrainPath = cfgTerrain;
    if (cfg.Read("savesDir", &cfgSaves))
        savesDir = cfgSaves;
    if (cfg.Read("exportPath", &cfgExportPath))
        _mwUtf8ToWideBuffer(cfgExportPath.utf8_str(), gExportPath, 4096);
    LoadExportFileData(gEfd);   // falls back to the InitViewExportData preset above if absent/stale
    LoadRecentExportsFromConfig();
    gCurX = cfg.ReadDouble("curX", 0.0);
    gCurZ = cfg.ReadDouble("curZ", 0.0);
    gCurScale = cfg.ReadDouble("curScale", 1.0);
    if (!std::isfinite(gCurScale) || gCurScale < 0.01) gCurScale = 1.0;

    if (!_mwUtf8ToWideBuffer(terrainPath.utf8_str(), gSelectTerrainPathAndName, MAX_PATH_AND_FILE))
        gSelectTerrainPathAndName[0] = L'\0';
    splitPath(gSelectTerrainPathAndName, gSelectTerrainDir, nullptr);
    if (!_mwUtf8ToWideBuffer(savesDir.utf8_str(), gWorldPathDefault, MAX_PATH_AND_FILE))
        gWorldPathDefault[0] = L'\0';

    // Initialize block colors
    SetMapPremultipliedColors(0);

    // Standard culling scheme: seeds barrier/structure-void as culled by default
    applyCullingScheme(nullptr);

    // Build UI
    BuildMenu();
    CreateStatusBar(2);
    SetStatusText("No world loaded", 0);

    // Top panel with two slider rows
    wxPanel* topPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);

    topSizer->Add(new wxStaticText(topPanel, wxID_ANY, "Depth:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    m_sliderTop = new wxSlider(topPanel, ID_SLIDER_TOP, INIT_MAP_MAX_HEIGHT, 0, INIT_MAP_MAX_HEIGHT,
                               wxDefaultPosition, wxSize(200, -1), wxSL_HORIZONTAL);
    m_labelTop = new wxStaticText(topPanel, wxID_ANY, wxString::Format("%d", INIT_MAP_MAX_HEIGHT),
                                  wxDefaultPosition, wxSize(40, -1));
    topSizer->Add(m_sliderTop, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    topSizer->Add(m_labelTop,  0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    topSizer->AddSpacer(20);
    topSizer->Add(new wxStaticText(topPanel, wxID_ANY, "Bottom:"), 0, wxALIGN_CENTER_VERTICAL);
    m_sliderBot = new wxSlider(topPanel, ID_SLIDER_BOT, 0, 0, INIT_MAP_MAX_HEIGHT,
                               wxDefaultPosition, wxSize(200, -1), wxSL_HORIZONTAL);
    m_labelBot = new wxStaticText(topPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(40, -1));
    topSizer->Add(m_sliderBot, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    topSizer->Add(m_labelBot,  0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    topPanel->SetSizer(topSizer);

    // Map panel
    m_mapPanel = new MapPanel(this);
    m_mapPanel->SetFocus();

    // Frame sizer
    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(topPanel, 0, wxEXPAND);
    frameSizer->Add(m_mapPanel, 1, wxEXPAND);
    SetSizer(frameSizer);
    Layout();
}

MinewaysFrame::~MinewaysFrame()
{
    // Persist settings to plist via wxConfig
    wxConfig cfg("Mineways");
    cfg.Write("terrainPath", wxString(gSelectTerrainPathAndName));
    cfg.Write("savesDir", wxString(gWorldPathDefault));
    if (gExportPath[0]) cfg.Write("exportPath", wxString(gExportPath));
    SaveExportFileData(gEfd);
    cfg.Write("curX",     gCurX);
    cfg.Write("curZ",     gCurZ);
    cfg.Write("curScale", gCurScale);

    gFrame = nullptr;
    free(gMapBits);
    gMapBits = nullptr;
}

// Scan the saves directory and populate gWorldDirs[]/gWorldDisplayNames[].
// Mirrors Win/Mineways.cpp's loadWorldList: validates each candidate via
// GetFileVersion (skips unreadable/unsupported saves, e.g. Bedrock folders
// that happen to contain a level.dat) and prefers the level's in-game name
// over the raw folder name.
// Returns the number of worlds found.
static int ScanWorldSaves(const wxString& savesDir)
{
    gNumWorlds = 0;
    wxScopedCharBuffer path = savesDir.utf8_str();
    if (!path) return 0;
    DIR* d = opendir(path.data());
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr && gNumWorlds < MAX_WORLDS) {
        if (de->d_name[0] == '.') continue;         // skip . / .. / hidden
        wxString entry = savesDir + "/" + de->d_name;
        // A valid world directory contains level.dat
        if (!wxFileExists(entry + "/level.dat")) continue;

        wchar_t wpath[MAX_PATH_AND_FILE], fileOpened[MAX_PATH_AND_FILE];
        if (!_mwUtf8ToWideBuffer(entry.utf8_str(), wpath, MAX_PATH_AND_FILE)) continue;
        int version = 0;
        if (GetFileVersion(wpath, &version, fileOpened, MAX_PATH_AND_FILE) != 0)
            continue;   // unreadable/unsupported (e.g. Bedrock) — skip like Windows does

        char levelName[MAX_PATH_AND_FILE] = {};
        wxString display = (GetLevelName(wpath, levelName, MAX_PATH_AND_FILE) == 0 && levelName[0])
            ? wxString::FromUTF8(levelName) : wxFileName(entry).GetFullName();

        gWorldDirs[gNumWorlds] = entry;
        gWorldDisplayNames[gNumWorlds] = display;
        gNumWorlds++;
    }
    closedir(d);
    return gNumWorlds;
}

// Ports Win/Mineways.cpp's loadTerrainList(): scans the app bundle's Resources dir
// (or exe dir fallback) for terrainExt*.png alternate texture packs shipped next to
// the executable. Excludes the _n/_r/_m/_e PBR-channel suffix files, which aren't
// standalone terrain files themselves.
static int ScanTerrainFiles(const wxString& dir)
{
    gNumTerrainFiles = 0;
    wxScopedCharBuffer path = dir.utf8_str();
    if (!path) return 0;
    DIR* d = opendir(path.data());
    if (!d) return 0;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr && gNumTerrainFiles < MAX_TERRAIN_FILES) {
        wxString name = wxString::FromUTF8(de->d_name);
        if (!name.Lower().StartsWith("terrainext") || !name.Lower().EndsWith(".png")) continue;
        wxString lower = name.Lower();
        if (lower.EndsWith("_n.png") || lower.EndsWith("_r.png") ||
            lower.EndsWith("_m.png") || lower.EndsWith("_e.png")) continue;

        gTerrainFilePaths[gNumTerrainFiles] = dir + "/" + name;
        gTerrainFileNames[gNumTerrainFiles] = name;
        gNumTerrainFiles++;
    }
    closedir(d);
    return gNumTerrainFiles;
}

void MinewaysFrame::BuildMenu()
{
    // Scan worlds before building menu so world submenu is populated
    ScanWorldSaves(wxString(gWorldPathDefault));

    // Scan for alternate terrainExt*.png files alongside the default one
    {
        ScanTerrainFiles(wxString(gSelectTerrainDir));
    }

    wxMenuBar* mb = new wxMenuBar;

    wxMenu* fileMenu = new wxMenu;

    // World list submenu
    wxMenu* worldsMenu = new wxMenu;
    worldsMenu->Append(ID_TEST_BLOCK_WORLD, "Test Block World",
                       "Built-in test world showing all block types");
    worldsMenu->Append(ID_OPEN_WORLD, "Browse for World Folder...",
                       "Choose any Minecraft world folder not in the scanned list");
    if (gNumWorlds > 0) {
        worldsMenu->AppendSeparator();
        for (int i = 0; i < gNumWorlds; i++) {
            worldsMenu->Append(ID_WORLD_ITEM_BASE + i, gWorldDisplayNames[i]);
        }
    }
    fileMenu->AppendSubMenu(worldsMenu, "Open World");
    fileMenu->Append(ID_RELOAD_WORLD, "Reload World\tR",
                     "Reload the current world (picks up changes made outside Mineways)");

    fileMenu->Append(ID_OPEN_FILE,  "Open level.dat or Schematic...\tCtrl+O",
                     "Open a level.dat or .schematic/.schem file");
    fileMenu->Append(ID_GO_TO_LOCATION, "Go To Location...\tCtrl+G",
                     "Jump map view to a specific X,Z coordinate");
    // "Choose Terrain File" submenu: [default] + any terrainExt*.png packs found
    // alongside the app bundle (Win/Mineways.cpp's loadTerrainList scans the same way).
    wxMenu* terrainMenu = new wxMenu;
    terrainMenu->Append(ID_DEFAULT_TERRAIN, "[default]");
    if (gNumTerrainFiles > 0) {
        terrainMenu->AppendSeparator();
        for (int i = 0; i < gNumTerrainFiles; i++)
            terrainMenu->Append(ID_TERRAIN_ITEM_BASE + i, gTerrainFileNames[i]);
    }
    fileMenu->AppendSubMenu(terrainMenu, "Choose Terrain File");
    fileMenu->Append(ID_CHOOSE_TERRAIN, "Open Terrain File...\tCtrl+T",
                     "Browse for a custom terrainExt*.png texture atlas");
    fileMenu->Append(ID_DOWNLOAD_TERRAIN_FILES, "Download Terrain Files...",
                     "Open the Mineways textures page in your browser");
    fileMenu->Append(ID_CULLING_SCHEMES, "Culling Schemes...",
                     "Manage block culling schemes (hide blocks from map and export)");
    fileMenu->Append(ID_IMPORT_SETTINGS, "Import Settings...\tCtrl+I",
                     "Re-import settings from a previously-exported file, or run a Mineways script");
    m_recentExportsMenu = new wxMenu;
    fileMenu->AppendSubMenu(m_recentExportsMenu, "Recent Exports");
    PopulateRecentExportsMenu();
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_EXPORT_OBJ, "Export Model...\tCtrl+E",
                     "Export selected region to OBJ");
    fileMenu->Append(ID_REPEAT_EXPORT, "Repeat Export\tCtrl+X",
                     "Re-export using the last output path and settings, without reopening the dialog");
    fileMenu->Append(ID_EXPORT_MAP, "Export Map...\tCtrl+M",
                     "Save the selected region of the 2D map as a PNG image");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "Quit\tCtrl+Q");

    wxMenu* viewMenu = new wxMenu;
    viewMenu->Append(ID_SELECT_ALL, "Select All\tCtrl+A",
                     "Select the entire visible map (or whole schematic)");
    viewMenu->Append(ID_VIEW_UNDOSELECTION, "Undo Selection\tCtrl+Z",
                     "Restore the previous selection");
    viewMenu->AppendSeparator();
    viewMenu->Append(ID_JUMP_SPAWN,   "Jump to Spawn\tF2");
    viewMenu->Append(ID_JUMP_PLAYER,  "Jump to Player\tF3");
    viewMenu->Append(ID_JUMP_MODEL,   "Jump to Model\tF4");
    viewMenu->Append(ID_VIEW_INFORMATION, "Information\tI");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_VIEW_HELL, "View Nether\tF5");
    viewMenu->AppendCheckItem(ID_VIEW_END,  "View The End\tF6");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_SHOWALL,    "Show all objects\tF7");
    viewMenu->AppendCheckItem(ID_SHOWBIOMES, "Show biomes\tF8");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_ELEVATION_SHADING, "Elevation shading\tF");
    viewMenu->AppendCheckItem(ID_LIGHTING,          "Lighting\tL");
    viewMenu->AppendCheckItem(ID_CAVEMODE,          "Cave mode\tC");
    viewMenu->AppendCheckItem(ID_HIDEOBSCURED,      "Hide obscured\tH");
    viewMenu->AppendCheckItem(ID_TRANSPARENT_WATER, "Transparent water\tT");
    viewMenu->AppendCheckItem(ID_MAPGRID,           "Map grid\tM");
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_ZOOMOUTFURTHER, "Zoom out further");

    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(ID_HELP_KEYBOARD, "Help: keyboard\tF1");
    helpMenu->Append(ID_HELP_TROUBLESHOOTING, "Help: troubleshooting");
    helpMenu->Append(ID_HELP_DOCUMENTATION, "Help: documentation");
    helpMenu->Append(ID_HELP_REPORT_BUG, "Report a bug");
    helpMenu->AppendSeparator();
    helpMenu->Append(wxID_ABOUT, "About Mineways");
    helpMenu->AppendSeparator();
    helpMenu->AppendCheckItem(ID_HELP_GIVE_MORE_MEMORY, "Give more export memory!");

    mb->Append(fileMenu, "&File");
    mb->Append(viewMenu, "&View");
    mb->Append(helpMenu, "&Help");
    SetMenuBar(mb);

    wxAcceleratorEntry entries[] = {
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_TAB, ID_IMPORT_SETTINGS),
        wxAcceleratorEntry(wxACCEL_CTRL, 'A', ID_SELECT_ALL),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'c', ID_CAVEMODE),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'C', ID_CAVEMODE),
        wxAcceleratorEntry(wxACCEL_CTRL, 'Q', wxID_EXIT),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'f', ID_ELEVATION_SHADING),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'F', ID_ELEVATION_SHADING),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F6, ID_VIEW_END),
        wxAcceleratorEntry(wxACCEL_CTRL, 'M', ID_EXPORT_MAP),
        wxAcceleratorEntry(wxACCEL_CTRL, 'P', ID_EXPORT_OBJ),
        wxAcceleratorEntry(wxACCEL_CTRL, 'X', ID_REPEAT_EXPORT),
        wxAcceleratorEntry(wxACCEL_CTRL, 'R', ID_EXPORT_OBJ),
        wxAcceleratorEntry(wxACCEL_CTRL, 'S', ID_OPEN_FILE),
        wxAcceleratorEntry(wxACCEL_CTRL, 'T', ID_CHOOSE_TERRAIN),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'v', ID_GO_TO_LOCATION),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'V', ID_GO_TO_LOCATION),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F5, ID_VIEW_HELL),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F1, ID_HELP_KEYBOARD),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F3, ID_JUMP_PLAYER),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F2, ID_JUMP_SPAWN),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'l', ID_LIGHTING),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'L', ID_LIGHTING),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'm', ID_MAPGRID),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'M', ID_MAPGRID),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'h', ID_HIDEOBSCURED),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'H', ID_HIDEOBSCURED),
        wxAcceleratorEntry(wxACCEL_CTRL, 'O', ID_OPEN_WORLD),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'r', ID_RELOAD_WORLD),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'R', ID_RELOAD_WORLD),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F7, ID_SHOWALL),
        wxAcceleratorEntry(wxACCEL_NORMAL, 't', ID_TRANSPARENT_WATER),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'T', ID_TRANSPARENT_WATER),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'i', ID_VIEW_INFORMATION),
        wxAcceleratorEntry(wxACCEL_NORMAL, 'I', ID_VIEW_INFORMATION),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F4, ID_JUMP_MODEL),
        wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F8, ID_SHOWBIOMES),
        wxAcceleratorEntry(wxACCEL_CTRL, 'Z', ID_VIEW_UNDOSELECTION),
    };
    wxAcceleratorTable accel(WXSIZEOF(entries), entries);
    SetAcceleratorTable(accel);
}

void MinewaysFrame::OnMapPanelSize(int w, int h)
{
    gMapWidth  = w;
    gMapHeight = h;
    free(gMapBits);
    gMapBits = (unsigned char*)malloc((size_t)w * h * 4);
    if (gMapBits) memset(gMapBits, 0xff, (size_t)w * h * 4);
}

void MinewaysFrame::UpdateBottomSlider(int depth)
{
    if (m_sliderBot) {
        m_sliderBot->SetValue(depth);
        m_labelBot->SetLabel(wxString::Format("%d", depth));
    }
}

void MinewaysFrame::UpdateStatusBar(int mx, int mz, int my,
                                    const char* label, int /*type*/, int /*dataVal*/, int /*biome*/)
{
    if (label && *label) {
        wxString s = wxString::Format("X=%d Y=%d Z=%d  %s", mx, my, mz, label);
        SetStatusText(s, 0);
    }
}

// ─── Menu handlers ────────────────────────────────────────────────────────────
void MinewaysFrame::OnOpenWorld(wxCommandEvent&)
{
    // Manual directory browse, for worlds outside the scanned saves folder
    wxDirDialog dlg(this, "Select a Minecraft world folder",
                    wxString(gWorldPathDefault),
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    wxString err = LoadWorldFromDir(dlg.GetPath());
    if (!err.IsEmpty()) wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
}

void MinewaysFrame::OnTestBlockWorld(wxCommandEvent&) { LoadTestBlockWorld(); }

void MinewaysFrame::LoadTestBlockWorld()
{
    ClearLoadedWorld();

    gWorldGuide.type  = WORLD_TEST_BLOCK_TYPE;
    gWorldGuide.world[0] = 0;
    gVersionID        = 3953;   // current data version
    gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
    gMinHeight        = ZERO_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gMaxHeight        = MAX_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    // block_alloc uses gWorldGuide.minHeight/maxHeight to size the grid allocation
    gWorldGuide.minHeight = gMinHeight;
    gWorldGuide.maxHeight = gMaxHeight;
    gCurX = gCurZ     = 0.0;
    gCurScale         = 1.0;
    gCurDepth         = gMaxHeight;
    gTargetDepth      = gMinHeight;
    gLoaded           = TRUE;

    SetTitle("Mineways — Test Block World");
    if (m_sliderTop) {
        m_sliderTop->SetRange(gMinHeight, gMaxHeight);
        m_sliderTop->SetValue(gCurDepth);
        m_labelTop->SetLabel(wxString::Format("%d", gCurDepth));
        m_sliderBot->SetRange(gMinHeight, gMaxHeight);
        m_sliderBot->SetValue(gTargetDepth);
        m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth));
    }
    SetStatusText("Test Block World loaded", 0);
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnGoToLocation(wxCommandEvent&)
{
    setLocationData((int)gCurX, (int)gCurZ);
    if (doLocation(this)) {
        int x, z;
        getLocationData(x, z);
        gCurX = x; gCurZ = z;
        if (m_mapPanel) m_mapPanel->RedrawMap();
    }
}

void MinewaysFrame::OnWorldMenuItem(wxCommandEvent& e)
{
    int idx = e.GetId() - ID_WORLD_ITEM_BASE;
    if (idx < 0 || idx >= gNumWorlds) return;
    wxString err = LoadWorldFromDir(gWorldDirs[idx]);
    if (!err.IsEmpty()) wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
}

void MinewaysFrame::OnCullingSchemes(wxCommandEvent&)
{
    doCullingSchemesMac(this);
    if (m_mapPanel && gLoaded) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnChooseTerrainFile(wxCommandEvent&)
{
    wxString currentPath(gSelectTerrainPathAndName);
    wxString defaultDir  = wxFileName(currentPath).GetPath();
    wxString defaultFile = wxFileName(currentPath).GetFullName();

    wxFileDialog dlg(this, "Choose Terrain File", defaultDir, defaultFile,
                     "Terrain files (terrainExt*.png)|terrainExt*.png|PNG files (*.png)|*.png",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    SetTerrainFile(dlg.GetPath());
}

void MinewaysFrame::OnDefaultTerrain(wxCommandEvent&)
{
    wxString terrainPath = wxStandardPaths::Get().GetResourcesDir() + "/terrainExt.png";
    if (!wxFileExists(terrainPath)) {
        wxFileName exeFN(wxStandardPaths::Get().GetExecutablePath());
        terrainPath = exeFN.GetPath() + "/terrainExt.png";
    }
    SetTerrainFile(terrainPath);
}

void MinewaysFrame::OnTerrainMenuItem(wxCommandEvent& e)
{
    int idx = e.GetId() - ID_TERRAIN_ITEM_BASE;
    if (idx < 0 || idx >= gNumTerrainFiles) return;
    SetTerrainFile(gTerrainFilePaths[idx]);
}

// Shared by the browse dialog, [default], the scanned terrainExt*.png submenu, and
// Mac/ImportSettings.cpp's "Terrain file name:" header/script command.
void MinewaysFrame::SetTerrainFile(const wxString& path)
{
    wchar_t terrainPath[MAX_PATH_AND_FILE];
    if (!_mwUtf8ToWideBuffer(path.utf8_str(), terrainPath, MAX_PATH_AND_FILE)) {
        wxMessageBox("The terrain file path is invalid or too long.", "Terrain file error",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    wcscpy(gSelectTerrainPathAndName, terrainPath);
    splitPath(gSelectTerrainPathAndName, gSelectTerrainDir, nullptr);
    RedrawMap();
}

void MinewaysFrame::OnOpenFile(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Open world file or schematic", "", "",
                     "Minecraft files (level.dat;*.schematic;*.schem)|level.dat;*.schematic;*.schem|All files (*)|*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    wxString path = dlg.GetPath();
    // If the user picked level.dat, open its parent directory as the world
    if (path.EndsWith("level.dat")) {
        wxString err = LoadWorldFromDir(wxFileName(path).GetPath());
        if (!err.IsEmpty()) wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
        return;
    }

    bool isSponge = path.Lower().EndsWith(".schem");
    bool isLegacy = path.Lower().EndsWith(".schematic");
    if (!isSponge && !isLegacy) {
        wxMessageBox("Unrecognised file type. Open level.dat, .schematic, or .schem files.",
                     "Open error", wxOK | wxICON_WARNING, this);
        return;
    }
    wxString err = LoadSchematic(path);
    if (!err.IsEmpty())
        wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
}

// Returns an empty string on success, or a human-readable error message on failure.
wxString MinewaysFrame::LoadSchematic(const wxString& path)
{
    ClearLoadedWorld();

    bool isSponge = path.Lower().EndsWith(".schem");
    wchar_t schematicPath[MAX_PATH_AND_FILE];
    if (!_mwUtf8ToWideBuffer(path.utf8_str(), schematicPath, MAX_PATH_AND_FILE))
        return "The schematic path is invalid or too long.";

    gWorldGuide.type = WORLD_SCHEMATIC_TYPE;
    gWorldGuide.sch.isSponge = isSponge;
    gWorldGuide.sch.repeat   = (path.Find("repeat") != wxNOT_FOUND);
    wcscpy(gWorldGuide.world, schematicPath);

    int err = LoadSchematicFile(gWorldGuide.world, isSponge);
    if (err != 0) {
        ClearLoadedWorld();
        return wxString::Format("Could not load schematic (error %d).", err);
    }

    gCurX = gWorldGuide.sch.width  / 2.0;
    gCurZ = gWorldGuide.sch.length / 2.0;
    gCurScale  = 1.0;
    gCurDepth  = gMaxHeight;
    gTargetDepth = gMinHeight;
    gLoaded = TRUE;

    if (m_sliderTop) {
        m_sliderTop->SetRange(gMinHeight, gMaxHeight);
        m_sliderTop->SetValue(gCurDepth);
        m_labelTop->SetLabel(wxString::Format("%d", gCurDepth));
    }
    if (m_sliderBot) {
        m_sliderBot->SetRange(gMinHeight, gMaxHeight);
        m_sliderBot->SetValue(gTargetDepth);
        m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth));
    }

    wxString name = wxFileName(path).GetName();
    SetTitle(wxString::Format("Mineways — %s", name));
    SetStatusText(wxString::Format("Schematic: %s  %dx%dx%d",
                                   name, gWorldGuide.sch.width,
                                   gWorldGuide.sch.height, gWorldGuide.sch.length), 0);
    RedrawMap();
    return wxString();
}

// Translates ExportFileData into the exportFlags bitmask SaveVolume() actually reads,
// ported from Win/Mineways.cpp's saveObjFile() (~line 5296-5542) so the dialog's options
// have the same effect on both platforms.
static int BuildExportFlags(const ExportFileData& e, wxWindow* parent)
{
    int flags = e.flags;
    int ft = e.fileType;
    if (ft < 0 || ft >= FILE_TYPE_TOTAL) ft = FILE_TYPE_WAVEFRONT_ABS_OBJ;

    if (e.radioExportMtlColors[ft])
        flags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    else if (e.radioExportSolidTexture[ft])
        flags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    else if (e.radioExportFullTexture[ft] ||
             (!(ft == FILE_TYPE_WAVEFRONT_ABS_OBJ || ft == FILE_TYPE_WAVEFRONT_REL_OBJ) && e.radioExportTileTextures[ft]))
        flags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;

    flags |=
        (e.chkFillBubbles ? EXPT_FILL_BUBBLES : 0) |
        ((e.chkFillBubbles && e.chkSealEntrances) ? (EXPT_FILL_BUBBLES | EXPT_SEAL_ENTRANCES) : 0) |
        ((e.chkFillBubbles && e.chkSealSideTunnels) ? (EXPT_FILL_BUBBLES | EXPT_SEAL_SIDE_TUNNELS) : 0) |
        (e.chkConnectParts ? EXPT_CONNECT_PARTS : 0) |
        (e.chkConnectCornerTips ? (EXPT_CONNECT_PARTS | EXPT_CONNECT_CORNER_TIPS) : 0) |
        (e.chkConnectAllEdges ? (EXPT_CONNECT_PARTS | EXPT_CONNECT_ALL_EDGES) : 0) |
        (e.chkDeleteFloaters ? EXPT_DELETE_FLOATING_OBJECTS : 0) |
        (e.chkHollow[ft] ? EXPT_HOLLOW_BOTTOM : 0) |
        ((e.chkHollow[ft] && e.chkSuperHollow[ft]) ? (EXPT_HOLLOW_BOTTOM | EXPT_SUPER_HOLLOW_BOTTOM) : 0) |
        (e.chkShowParts ? (EXPT_DEBUG_SHOW_GROUPS | EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK) : 0) |
        (e.chkShowWelds ? (EXPT_DEBUG_SHOW_WELDS | EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK) : 0);

    if (ft == FILE_TYPE_WAVEFRONT_ABS_OBJ || ft == FILE_TYPE_WAVEFRONT_REL_OBJ) {
        if (e.chkSeparateTypes) {
            flags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES;
            if (e.chkMaterialPerFamily) flags |= EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK;
        } else if (e.chkIndividualBlocks[ft]) {
            flags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_INDIVIDUAL_BLOCKS | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK;
            if (e.chkMaterialPerFamily) flags |= EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
        }
        if (e.chkSplitByBlockType) flags |= EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE;
        if (e.chkMakeGroupsObjects) flags |= EXPT_OUTPUT_OBJ_MAKE_GROUPS_OBJECTS;
        if (e.chkCustomMaterial[ft]) flags |= EXPT_OUTPUT_CUSTOM_MATERIAL;
        if (ft == FILE_TYPE_WAVEFRONT_REL_OBJ) flags |= EXPT_OUTPUT_OBJ_REL_COORDINATES;
        if (e.radioExportTileTextures[ft])
            flags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    } else if (ft == FILE_TYPE_USD) {
        if (e.chkIndividualBlocks[ft]) {
            flags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_INDIVIDUAL_BLOCKS | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK;
            if (e.chkMaterialPerFamily) flags |= EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
        }
        if (e.radioExportTileTextures[ft])
            flags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
        if (e.chkCustomMaterial[ft]) flags |= EXPT_OUTPUT_CUSTOM_MATERIAL;
        if (e.chkExportMDL) flags |= EXPT_EXPORT_MDL;
    } else if (ft == FILE_TYPE_ASCII_STL) {
        int unsupported = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES |
                          EXPT_OUTPUT_SEPARATE_TEXTURE_TILES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_DEBUG_SHOW_GROUPS | EXPT_DEBUG_SHOW_WELDS;
        if (flags & unsupported)
            wxMessageBox("Color output is not supported for ASCII STL; file will contain no colors.",
                        "Informational", wxOK | wxICON_INFORMATION, parent);
        flags &= ~unsupported;
        flags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    } else if (ft == FILE_TYPE_BINARY_MAGICS_STL || ft == FILE_TYPE_BINARY_VISCAM_STL) {
        int unsupported = EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
        if (flags & unsupported)
            wxMessageBox("Texture output is not supported for binary STL; file will contain solid colors instead.",
                        "Informational", wxOK | wxICON_INFORMATION, parent);
        flags &= ~unsupported;
        flags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    } else if (ft == FILE_TYPE_VRML2) {
        if ((flags & EXPT_OUTPUT_TEXTURE) && (flags & EXPT_3DPRINT))
            flags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    } else if (ft == FILE_TYPE_SCHEMATIC || ft == FILE_TYPE_SPONGE_SCHEMATIC) {
        flags = 0;   // schematic export ignores all options except Y-axis rotation
    }

    if (e.chkBiome) flags |= EXPT_BIOME;

    if (flags & EXPT_DEBUG_SHOW_GROUPS) {
        if (flags & (EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES)) {
            flags &= ~(EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
            flags |= EXPT_OUTPUT_TEXTURE_SWATCHES;
        }
        flags &= ~EXPT_INDIVIDUAL_BLOCKS;
    }
    return flags;
}

void MinewaysFrame::OnExportOBJ(wxCommandEvent&)
{
    if (!gLoaded) {
        wxMessageBox("Load a world first.", "No world loaded", wxOK | wxICON_WARNING, this);
        return;
    }

    // Get current selection bounds
    int on = 0;
    int minx = 0, miny = 0, minz = 0, maxx = 0, maxy = 0, maxz = 0;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    if (!on) {
        wxMessageBox("Please right-click drag on the map to select a region first.",
                     "No selection", wxOK | wxICON_WARNING, this);
        return;
    }

    // Pre-fill bounds into the persistent ExportFileData
    gEfd.minxVal = minx; gEfd.minyVal = miny; gEfd.minzVal = minz;
    gEfd.maxxVal = maxx; gEfd.maxyVal = maxy; gEfd.maxzVal = maxz;

    if (doExportDialog(this, gEfd, gExportPath, 4096, minx, miny, minz, maxx, maxy, maxz) != wxID_OK)
        return;

    RunExport(gEfd, gExportPath);
}

// Shared by OnExportOBJ (after the dialog returns OK) and OnRepeatExport/Ctrl+X
// (reuses the last-used efd/path without reopening the dialog).
void MinewaysFrame::RunExport(ExportFileData& efd, const wchar_t* outputPath)
{
    // Re-derive the export bounds from the live highlight state, same as Win/Mineways.cpp's
    // saveObjFile does unconditionally before every export. Without this, a script's
    // "Selection location..." command (which only updates the map highlight via
    // SetHighlightState, not efd directly) would export with stale/zero bounds.
    int on;
    GetHighlightState(&on, &efd.minxVal, &efd.minyVal, &efd.minzVal, &efd.maxxVal, &efd.maxyVal, &efd.maxzVal, gMinHeight);

    gOptions.pEFD = &efd;
    gOptions.exportFlags = BuildExportFlags(efd, this);
    gOptions.saveFilterFlags = efd.chkExportAll
        ? (BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE |
           BLF_FLATTEN | BLF_FLATTEN_SMALL | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD)
        : (BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE |
           BLF_FLATTEN | BLF_FLATTEN_SMALL);

    // Exe/bundle directory for texture lookup
    wchar_t curDir[MAX_PATH_AND_FILE];
    wxFileName exeFN(wxStandardPaths::Get().GetExecutablePath());
    if (!_mwUtf8ToWideBuffer(exeFN.GetPath().utf8_str(), curDir, MAX_PATH_AND_FILE)) {
        wxMessageBox("The application resource path is invalid or too long.", "Export failed",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    // Biome/group state
    static int userSelectedBiome = -1, biomeIndex = -1;
    static int groupCount = 0, groupCountSize = 10, groupCountArray[10] = {};

    // Quick I/O probe: can we create a file in the chosen directory at all?
    {
        wxFileName outputFileName(outputPath);
        wxString probePrefix = outputFileName.GetPathWithSep() + ".mineways-write-test-";
        wxString probePath = wxFileName::CreateTempFileName(probePrefix);
        if (probePath.empty()) {
            wxMessageBox(wxString::Format(
                "Cannot create a file in the export directory — check the path and permissions.\nDirectory: %s",
                outputFileName.GetPath()),
                "Export pre-check failed", wxOK | wxICON_ERROR, this);
            return;
        }
        wxRemoveFile(probePath);
    }

    FileList outputFileList;
    outputFileList.count = 0;
    for (int i = 0; i < MAX_OUTPUT_FILES; i++) outputFileList.name[i] = nullptr;

    // SaveVolume() runs synchronously on the main thread; wxProgressDialog::Update()
    // pumps enough of the event loop to keep the bar and window responsive-looking
    // during that blocking call. No wxPD_CAN_ABORT: like Windows, there is no
    // mid-export cancellation mechanism in the underlying export code.
    gExportProgressLastValue = 0;
    gExportProgressLastMsg = "Exporting...";
    wxProgressDialog progressDlg("Exporting", gExportProgressLastMsg, 100, this,
                                 wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_AUTO_HIDE);
    gExportProgressDlg = &progressDlg;

    int errCode = SaveVolume((wchar_t*)outputPath, efd.fileType,
        &gOptions, &gWorldGuide, curDir,
        efd.minxVal, efd.minyVal, efd.minzVal,
        efd.maxxVal, efd.maxyVal, efd.maxzVal,
        gMinHeight, gMaxHeight,
        ExportProgressCB, gSelectTerrainPathAndName, (wchar_t*)getSelectedCullingSchemeW(),
        &outputFileList,
        MAC_MINEWAYS_VERSION_MAJOR, MAC_MINEWAYS_VERSION_MINOR, gVersionID,
        nullptr /*changeBlock*/, 16 /*instanceChunkSize*/,
        userSelectedBiome, biomeIndex,
        groupCount, groupCountSize, groupCountArray);

    gExportProgressDlg = nullptr;

    if (errCode >= MW_BEGIN_NOTHING_TO_DO && errCode < MW_BEGIN_ERRORS) {
        // Codes in this range (e.g. MW_NO_BLOCKS_FOUND, MW_ALL_BLOCKS_DELETED) mean
        // SaveVolume deliberately wrote nothing — not an error, but not success either.
        // Mirrors Win/Mineways.cpp's `errCode < MW_BEGIN_NOTHING_TO_DO` success gate.
        wxMessageBox("No file was written: nothing exportable was found in the selected region.",
                     "Nothing exported", wxOK | wxICON_WARNING, this);
    } else if (errCode == MW_NO_ERROR || errCode < MW_BEGIN_ERRORS) {
        // Build message before freeing the list
        wxString msg = "Export complete.";
        if (outputFileList.count > 0) {
            for (int i = 0; i < outputFileList.count; i++) {
                if (outputFileList.name[i]) {
                    msg += "\n" + wxString(outputFileList.name[i]);
                }
            }
        } else {
            msg += "\n(expected: " + wxString(outputPath) + ")";
        }
        wxMessageBox(msg, "Export done", wxOK | wxICON_INFORMATION, this);

        // Issue #138 parity: record successful model exports in the Recent Exports menu.
        // Use the first actually-written file so the recorded path has the on-disk suffix
        // SaveVolume adds when the user's typed name is missing one.
        if (outputFileList.count > 0 && outputFileList.name[0]) {
            AddToRecentExports(wxString(outputFileList.name[0]));
        } else {
            AddToRecentExports(wxString(outputPath));
        }
    } else {
        // Show path even on failure so user can diagnose
        wxMessageBox(wxString::Format("Export failed (error %d).\nAttempted path: %s",
                     errCode, wxString(outputPath)),
                     "Export error", wxOK | wxICON_ERROR, this);
    }

    // Free file list
    for (int i = 0; i < outputFileList.count; i++) free(outputFileList.name[i]);
}

void MinewaysFrame::OnQuit(wxCommandEvent&) { Close(true); }

void MinewaysFrame::OnAbout(wxCommandEvent&)
{
    // Matches Win/Mineways.rc's IDD_ABOUTBOX static text verbatim.
    wxMessageBox("Mineways, Version " MAC_MINEWAYS_VERSION_STRING "\n"
                 "Copyright (c) 2011 Eric Haines\n\n"
                 "Free and open source Minecraft model exporter.\n"
                 "Visit http://mineways.com for docs and code.\n\n"
                 "Works with Minecraft versions 1.4 through 1.19.\n\n"
                 "Based on the open source program minutor,\n"
                 "Copyright (c) 2011, Sean Kasun\n\n"
                 "wxWidgets macOS build.",
                 "About Mineways", wxOK | wxICON_INFORMATION, this);
}

// URLs match Win/Mineways.cpp's IDM_HELP_URL/ID_HELP_TROUBLESHOOTING/
// ID_HELP_DOCUMENTATION/ID_HELP_REPORTABUG ShellExecute() calls.
void MinewaysFrame::OnHelpURL(wxCommandEvent& e)
{
    wxString url;
    switch (e.GetId()) {
    case ID_HELP_KEYBOARD:
        url = "https://www.realtimerendering.com/erich/minecraft/public/mineways/reference.html";
        break;
    case ID_HELP_TROUBLESHOOTING:
        url = "https://www.realtimerendering.com/erich/minecraft/public/mineways/downloads.html#windowsPlatformHelp";
        break;
    case ID_HELP_DOCUMENTATION:
        url = "https://www.realtimerendering.com/erich/minecraft/public/mineways/mineways.html";
        break;
    case ID_HELP_REPORT_BUG:
        url = "https://www.realtimerendering.com/erich/minecraft/public/mineways/contact.html";
        break;
    default: return;
    }
    wxLaunchDefaultBrowser(url);
}

void MinewaysFrame::OnDownloadTerrainFiles(wxCommandEvent&)
{
    wxLaunchDefaultBrowser("https://www.realtimerendering.com/erich/minecraft/public/mineways/textures.html#dl");
}

void MinewaysFrame::OnImportSettings(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Import Settings from model or script file", "", "",
                     "All files (*)|*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    wxString errorMsg;
    bool ok = ImportSettingsFile(this, dlg.GetPath(), errorMsg);
    if (!ok) {
        wxMessageBox(errorMsg.IsEmpty() ? "Import failed." : errorMsg,
                     "Import error", wxOK | wxICON_ERROR, this);
    } else if (!errorMsg.IsEmpty()) {
        // non-fatal warnings collected along the way
        wxMessageBox(errorMsg, "Import warnings", wxOK | wxICON_WARNING, this);
    }
    RedrawMap();
}

// Recent Exports (issue #138 parity): rebuild the submenu from gRecentExports[]. If empty,
// show a disabled placeholder so the submenu indicator doesn't lead nowhere.
void MinewaysFrame::PopulateRecentExportsMenu()
{
    if (!m_recentExportsMenu) return;
    while (m_recentExportsMenu->GetMenuItemCount() > 0)
        m_recentExportsMenu->Destroy(m_recentExportsMenu->FindItemByPosition(0));

    if (gRecentExportCount == 0) {
        m_recentExportsMenu->Append(ID_RECENT_EXPORTS_PLACEHOLDER, "(no recent exports)")->Enable(false);
    } else {
        for (int i = 0; i < gRecentExportCount; i++) {
            wxString basename = wxFileName(gRecentExports[i]).GetFullName();
            wxString label = wxString::Format("&%d %s (%s)", i + 1, basename, gRecentExports[i]);
            m_recentExportsMenu->Append(ID_RECENT_EXPORT_BASE + i, label);
        }
    }
}

// Adds `path` to the front of the Recent Exports list (moving it there if already present,
// no duplicates), persists, and refreshes the menu. Ported from Win/Mineways.cpp's
// addToRecentExports.
void MinewaysFrame::AddToRecentExports(const wxString& path)
{
    if (path.IsEmpty()) return;

    for (int i = 0; i < gRecentExportCount; i++) {
        if (gRecentExports[i].CmpNoCase(path) == 0) {
            for (int j = i; j < gRecentExportCount - 1; j++) gRecentExports[j] = gRecentExports[j + 1];
            gRecentExportCount--;
            break;
        }
    }
    int shiftEnd = (gRecentExportCount < MAX_RECENT_EXPORTS) ? gRecentExportCount : MAX_RECENT_EXPORTS - 1;
    for (int i = shiftEnd; i > 0; i--) gRecentExports[i] = gRecentExports[i - 1];
    gRecentExports[0] = path;
    if (gRecentExportCount < MAX_RECENT_EXPORTS) gRecentExportCount++;

    SaveRecentExportsToConfig();
    PopulateRecentExportsMenu();
}

// A Recent Exports submenu item was clicked. Re-opens the file the same way File > Import
// Settings does — except schematics, which reopen as a world (mirrors the drag-drop / File >
// Open dispatch), matching Win/Mineways.cpp's IDM_RECENT_EXPORT_BASE handler.
void MinewaysFrame::OnRecentExportItem(wxCommandEvent& e)
{
    int idx = e.GetId() - ID_RECENT_EXPORT_BASE;
    if (idx < 0 || idx >= gRecentExportCount) return;

    wxString chosen = gRecentExports[idx];
    if (!wxFileExists(chosen)) {
        wxMessageBox(wxString::Format(
            "The recent export file\n\n    %s\n\nis no longer at that location, so it has been removed from the list.",
            chosen), "File not found", wxOK | wxICON_WARNING, this);
        for (int j = idx; j < gRecentExportCount - 1; j++) gRecentExports[j] = gRecentExports[j + 1];
        gRecentExportCount--;
        SaveRecentExportsToConfig();
        PopulateRecentExportsMenu();
        return;
    }

    wxString lower = chosen.Lower();
    bool isSchematicLike = lower.EndsWith(".schematic") || lower.EndsWith(".schem");
    if (isSchematicLike) {
        wxString err = LoadSchematic(chosen);
        if (!err.IsEmpty()) wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
    } else {
        wxString errorMsg;
        bool ok = ImportSettingsFile(this, chosen, errorMsg);
        if (!ok) {
            wxMessageBox(errorMsg.IsEmpty() ? "Import failed." : errorMsg,
                         "Import error", wxOK | wxICON_ERROR, this);
        } else if (!errorMsg.IsEmpty()) {
            wxMessageBox(errorMsg, "Import warnings", wxOK | wxICON_WARNING, this);
        }
    }
    RedrawMap();
    AddToRecentExports(chosen);   // bump to top
}

void MinewaysFrame::OnGiveMoreExportMemory(wxCommandEvent&)
{
    gOptions.moreExportMemory = !gOptions.moreExportMemory;
    if (GetMenuBar()) GetMenuBar()->Check(ID_HELP_GIVE_MORE_MEMORY, gOptions.moreExportMemory);
    MinimizeCacheBlocks(gOptions.moreExportMemory);
}

void MinewaysFrame::OnReloadWorld(wxCommandEvent&)
{
    if (!gLoaded || gWorldGuide.type != WORLD_LEVEL_TYPE) {
        wxMessageBox("You need to load a world first.", "No world loaded", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString err = LoadWorldFromDir(wxString(gWorldGuide.world));
    if (!err.IsEmpty()) wxMessageBox(err, "Load error", wxOK | wxICON_ERROR, this);
}

void MinewaysFrame::OnRepeatExport(wxCommandEvent&)
{
    if (!gLoaded) {
        wxMessageBox("Load a world first.", "No world loaded", wxOK | wxICON_WARNING, this);
        return;
    }
    if (!gExportPath[0]) {
        wxMessageBox("No previous export to repeat — use Export Model... first.",
                     "Nothing to repeat", wxOK | wxICON_WARNING, this);
        return;
    }
    RunExport(gEfd, gExportPath);
}

// Ports Win/Mineways.cpp's saveMapFile(): renders the selected region fresh at the
// current zoom level via DrawMapToArray (not just a screenshot of the on-screen
// buffer) and writes it as a PNG. Both DrawMapToArray and writepng are shared,
// platform-independent code already compiled into the Mac binary.
void MinewaysFrame::OnExportMap(wxCommandEvent&)
{
    if (!gLoaded) {
        wxMessageBox("Load a world first.", "No world loaded", wxOK | wxICON_WARNING, this);
        return;
    }
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    if (!on) {
        wxMessageBox("Right-click drag on the map to select a region to export first.",
                     "No selection", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxFileDialog fd(this, "Export Map", "", "", "PNG image (*.png)|*.png",
                    wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (fd.ShowModal() != wxID_OK) return;
    wxString path = fd.GetPath();
    if (!path.Lower().EndsWith(".png")) path += ".png";

    if (minx > maxx) wxSwap(minx, maxx);
    if (minz > maxz) wxSwap(minz, maxz);
    int64_t widthBlocks = (int64_t)maxx - minx + 1;
    int64_t heightBlocks = (int64_t)maxz - minz + 1;
    if (widthBlocks <= 0 || heightBlocks <= 0 ||
        widthBlocks > (std::numeric_limits<int>::max)() || heightBlocks > (std::numeric_limits<int>::max)() ||
        !std::isfinite(gCurScale) || gCurScale > (std::numeric_limits<int>::max)() - 0.5) {
        wxMessageBox("The selected map dimensions are too large to export.",
                     "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    }
    int w = (int)widthBlocks;
    int h = (int)heightBlocks;
    int zoom = (int)(gCurScale + 0.5);
    if (zoom < 1) zoom = 1;

    size_t pixelWidth = (size_t)w * (size_t)zoom;
    size_t pixelHeight = (size_t)h * (size_t)zoom;
    constexpr size_t MAX_EXPORT_MAP_BYTES = (size_t)1024 * 1024 * 1024;
    if (pixelWidth > (size_t)(std::numeric_limits<int>::max)() ||
        pixelHeight > (size_t)(std::numeric_limits<int>::max)() ||
        pixelWidth > SIZE_MAX / pixelHeight ||
        pixelWidth * pixelHeight > MAX_EXPORT_MAP_BYTES / 3) {
        wxMessageBox("The selected map would require more than 1 GiB of image memory. "
                     "Reduce the selection size or zoom level.",
                     "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    }
    size_t imageBytes = pixelWidth * pixelHeight * 3;

    progimage_info mapimage;
    mapimage.width = (int)pixelWidth;
    mapimage.height = (int)pixelHeight;
    try {
        mapimage.image_data.resize(imageBytes, 0x0);
    } catch (const std::bad_alloc&) {
        wxMessageBox("Not enough memory is available to render the selected map.",
                     "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    } catch (const std::length_error&) {
        wxMessageBox("The selected map dimensions are too large to export.",
                     "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    }

    // Suppress the selection-rectangle overlay while rendering to the array —
    // it's drawn from the same highlight state the interactive map uses.
    SetHighlightState(0, minx, gTargetDepth, minz, maxx, maxy, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    int retCode = DrawMapToArray(mapimage.image_data.data(), &gWorldGuide, minx, minz,
                                 maxy - gMinHeight, gMaxHeight, w, h, zoom, &gOptions, gHitsFound,
                                 progressCB, gMinecraftVersion, gVersionID);
    SetHighlightState(gHighlightOn, minx, gTargetDepth, minz, maxx, gCurDepth, maxz,
                      gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);

    if (retCode < 0) {
        wxMessageBox("Could not render the map for export (chunk read error).",
                     "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    }

    const wchar_t* convertedPath = path.wc_str();
    size_t convertedPathLength = wcslen(convertedPath);
    if (convertedPathLength >= MAX_PATH_AND_FILE) {
        wxMessageBox("The PNG output path is too long.", "Export Map failed", wxOK | wxICON_ERROR, this);
        return;
    }
    wchar_t wpath[MAX_PATH_AND_FILE];
    memcpy(wpath, convertedPath, (convertedPathLength + 1) * sizeof(wchar_t));
    int pngResult = writepng(&mapimage, 3, wpath);
    writepng_cleanup(&mapimage);

    if (pngResult == 0)
        wxMessageBox("Map exported to\n" + path, "Export Map done", wxOK | wxICON_INFORMATION, this);
    else
        wxMessageBox("Failed to write PNG file:\n" + path, "Export Map failed", wxOK | wxICON_ERROR, this);

    if (m_mapPanel) m_mapPanel->RedrawMap();
}

// Dragging either depth slider must update the Y bound of an already-drawn
// selection, not just future ones — otherwise the selection used by Export
// silently keeps whatever Y bound was in effect when the box was drawn,
// making the sliders look like they "do nothing". Mirrors Win/Mineways.cpp's
// slider handlers, which re-call SetHighlightState with the existing X/Z
// bounds whenever gCurDepth/gTargetDepth changes.
static void ReapplyHighlightDepth()
{
    if (!gHighlightOn) return;
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz,
                      gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
}

void MinewaysFrame::OnSliderTop(wxCommandEvent&)
{
    gCurDepth = m_sliderTop->GetValue();
    m_labelTop->SetLabel(wxString::Format("%d", gCurDepth));
    ReapplyHighlightDepth();
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnSliderBot(wxCommandEvent&)
{
    gTargetDepth = m_sliderBot->GetValue();
    m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth));
    ReapplyHighlightDepth();
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnSelectAll(wxCommandEvent&)
{
    if (!gLoaded) return;
    int on, minx, miny, minz, maxx, maxy, maxz;
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE) {
        minx = 0; minz = 0; maxx = gWorldGuide.sch.width - 1; maxz = gWorldGuide.sch.length - 1;
        maxy = gWorldGuide.sch.height - 1;
        gTargetDepth = gMinHeight;
    } else {
        // needed for maxy
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
        // select the visible map area, roughly, from the current center/scale/panel size
        minx = (int)(gCurX - (double)gMapWidth  / (2 * gCurScale));
        minz = (int)(gCurZ - (double)gMapHeight / (2 * gCurScale));
        maxx = (int)(gCurX + (double)gMapWidth  / (2 * gCurScale));
        maxz = (int)(gCurZ + (double)gMapHeight / (2 * gCurScale));
        gTargetDepth = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz,
                                                 gMinHeight, gMaxHeight, true, true, maxy);
    }
    gHighlightOn = TRUE;
    SetHighlightState(gHighlightOn, minx, gTargetDepth, minz, maxx, gCurDepth, maxz,
                      gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
    if (m_sliderBot) {
        m_sliderBot->SetValue(gTargetDepth);
        m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth));
    }
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnUndoSelection(wxCommandEvent&)
{
    UndoHighlight();
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    gHighlightOn = on;
    gTargetDepth = miny;
    gCurDepth = maxy;
    if (m_sliderBot) { m_sliderBot->SetValue(gTargetDepth); m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth)); }
    if (m_sliderTop) { m_sliderTop->SetValue(gCurDepth);    m_labelTop->SetLabel(wxString::Format("%d", gCurDepth)); }
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::JumpToSpawn()
{
    if (!gLoaded) return;
    gCurX = gSpawnX; gCurZ = gSpawnZ;
    if (gOptions.worldType & HELL) { gCurX /= 8.0; gCurZ /= 8.0; }
    RedrawMap();
}

void MinewaysFrame::JumpToPlayer()
{
    if (!gLoaded) return;
    gCurX = gPlayerX; gCurZ = gPlayerZ;
    if (gOptions.worldType & HELL) { gCurX /= 8.0; gCurZ /= 8.0; }
    RedrawMap();
}

// Despite the name, Windows' IDM_VIEW_JUMPTOMODEL just centers the view on the
// current selection's midpoint (Win/Mineways.cpp:2481-2500) — not a tracked
// "last exported model" as the name might suggest.
bool MinewaysFrame::JumpToModel()
{
    if (!gHighlightOn) return false;
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    if (!on) return false;
    gCurX = (minx + maxx) / 2;
    gCurZ = (minz + maxz) / 2;
    if (gOptions.worldType & HELL) { gCurX /= 8.0; gCurZ /= 8.0; }
    RedrawMap();
    return true;
}

void MinewaysFrame::OnJumpSpawn(wxCommandEvent&) { JumpToSpawn(); }
void MinewaysFrame::OnJumpPlayer(wxCommandEvent&) { JumpToPlayer(); }

void MinewaysFrame::OnJumpModel(wxCommandEvent&)
{
    if (!JumpToModel())
        wxMessageBox("No model selected. To select a model, right-click drag on the map.",
                     "Informational", wxOK | wxICON_INFORMATION, this);
}

void MinewaysFrame::OnViewInformation(wxCommandEvent&)
{
    wxString info;
    switch (gWorldGuide.type) {
    case WORLD_TEST_BLOCK_TYPE:
        info = "World is the built-in [Block Test World]";
        break;
    case WORLD_LEVEL_TYPE: {
        char levelName[MAX_PATH_AND_FILE] = {};
        GetLevelName(gWorldGuide.world, levelName, MAX_PATH_AND_FILE);
        info = wxString::Format(
            "Level name: %s\nDirectory name: %s\n\n"
            "Major version: 1.%d%s\nData version: %d\n\n"
            "Spawn location: %d, %d, %d\n",
            levelName, wxString(gWorldGuide.world),
            gMinecraftVersion, (gMinecraftVersion == 8 ? " or earlier" : ""),
            gVersionID, gSpawnX, gSpawnY, gSpawnZ);
        if (gWorldGuide.isServerWorld)
            info += "This is a server world; no player location";
        else
            info += wxString::Format("Player location: %d, %d, %d", gPlayerX, gPlayerY, gPlayerZ);
        break;
    }
    case WORLD_SCHEMATIC_TYPE:
        info = wxString::Format(
            "Width (X - east/west): %d\nHeight (Y - vertical): %d\nLength (Z - north/south): %d",
            gWorldGuide.sch.width, gWorldGuide.sch.height, gWorldGuide.sch.length);
        break;
    default:
        info = "No world loaded.";
        break;
    }
    wxMessageBox(info, "World Information", wxOK | wxICON_INFORMATION, this);
}

// Both Nether and End views reuse the same world's chunk loader with the HELL/ENDER
// worldType bits set — MinewaysMap.cpp's directory-building already picks the DIM-1/DIM1
// subfolder from those bits, so this is state bookkeeping, not new chunk-loading logic.
bool MinewaysFrame::SwitchToNether()
{
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE || gWorldGuide.type == WORLD_TEST_BLOCK_TYPE) return false;
    if (!(gOptions.worldType & HELL)) {
        gOptions.worldType |= HELL;
        gOptions.worldType &= ~ENDER;
        gCurX /= 8.0; gCurZ /= 8.0;
        if (gCurDepth == gMaxHeight) {
            gCurDepth = 126;
            if (m_sliderTop) { m_sliderTop->SetValue(gCurDepth); m_labelTop->SetLabel(wxString::Format("%d", gCurDepth)); }
        }
        gOverworldHideStatus = gOptions.worldType & HIDEOBSCURED;
        gOptions.worldType |= HIDEOBSCURED;
    } else {
        gCurX *= 8.0; gCurZ *= 8.0;
        if (gCurDepth == 126) {
            gCurDepth = gMaxHeight;
            if (m_sliderTop) { m_sliderTop->SetValue(gCurDepth); m_labelTop->SetLabel(wxString::Format("%d", gCurDepth)); }
        }
        gOptions.worldType &= ~HIDEOBSCURED;
        gOptions.worldType |= gOverworldHideStatus;
        gOptions.worldType &= ~HELL;
    }
    CloseAll();
    gHighlightOn = FALSE;
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
    if (GetMenuBar()) {
        GetMenuBar()->Check(ID_VIEW_HELL, (gOptions.worldType & HELL) != 0);
        GetMenuBar()->Check(ID_VIEW_END,  (gOptions.worldType & ENDER) != 0);
        GetMenuBar()->Check(ID_HIDEOBSCURED, (gOptions.worldType & HIDEOBSCURED) != 0);
    }
    RedrawMap();
    return true;
}

bool MinewaysFrame::SwitchToTheEnd()
{
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE || gWorldGuide.type == WORLD_TEST_BLOCK_TYPE) return false;
    if (!(gOptions.worldType & ENDER)) {
        gOptions.worldType |= ENDER;
        if (gOptions.worldType & HELL) {
            gCurX *= 8.0; gCurZ *= 8.0;
            if (gCurDepth == 126) {
                gCurDepth = gMaxHeight;
                if (m_sliderTop) { m_sliderTop->SetValue(gCurDepth); m_labelTop->SetLabel(wxString::Format("%d", gCurDepth)); }
            }
            gOptions.worldType &= ~HIDEOBSCURED;
            gOptions.worldType |= gOverworldHideStatus;
            gOptions.worldType &= ~HELL;
        }
    } else {
        gOptions.worldType &= ~ENDER;
    }
    CloseAll();
    gHighlightOn = FALSE;
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
    if (GetMenuBar()) {
        GetMenuBar()->Check(ID_VIEW_HELL, (gOptions.worldType & HELL) != 0);
        GetMenuBar()->Check(ID_VIEW_END,  (gOptions.worldType & ENDER) != 0);
        GetMenuBar()->Check(ID_HIDEOBSCURED, (gOptions.worldType & HIDEOBSCURED) != 0);
    }
    RedrawMap();
    return true;
}

void MinewaysFrame::OnViewHell(wxCommandEvent&)
{
    if (gWorldGuide.type == WORLD_LEVEL_TYPE) SwitchToNether();
}

void MinewaysFrame::OnViewEnd(wxCommandEvent&)
{
    if (gWorldGuide.type == WORLD_LEVEL_TYPE) SwitchToTheEnd();
}

void MinewaysFrame::RedrawMap()
{
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::ClearLoadedWorld()
{
    ClearLoadedWorldData();
    SetTitle("Mineways");
    SetStatusText("No world loaded", 0);
    RedrawMap();
}

void MinewaysFrame::OnToggleWorldTypeBit(wxCommandEvent& e)
{
    int bit = 0;
    switch (e.GetId()) {
    case ID_SHOWALL:            bit = SHOWALL; break;
    case ID_SHOWBIOMES:         bit = BIOMES; break;
    case ID_ELEVATION_SHADING:  bit = DEPTHSHADING; break;
    case ID_LIGHTING:           bit = LIGHTING; break;
    case ID_CAVEMODE:           bit = CAVEMODE; break;
    case ID_HIDEOBSCURED:       bit = HIDEOBSCURED; break;
    case ID_TRANSPARENT_WATER:  bit = TRANSPARENT_WATER; break;
    case ID_MAPGRID:            bit = MAP_GRID; break;
    default: return;
    }
    gOptions.worldType ^= bit;
    if (GetMenuBar()) GetMenuBar()->Check(e.GetId(), (gOptions.worldType & bit) != 0);
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

void MinewaysFrame::OnZoomOutFurther(wxCommandEvent&)
{
    bool enabling = (gMinZoom >= MINZOOM);
    gMinZoom = enabling ? 0.0625f : MINZOOM;
    if (enabling) {
        wxMessageBox("You can now zoom out further with the mouse scroll wheel, up to 16 "
                     "blocks wide per pixel. This uses a lot of memory as you zoom out and "
                     "may slow or lock up Mineways. You're on your own!",
                     "Warning", wxOK | wxICON_WARNING, this);
    }
    if (GetMenuBar()) GetMenuBar()->Check(ID_ZOOMOUTFURTHER, enabling);
    if (m_mapPanel) m_mapPanel->RedrawMap();
}

// ─── World loading ────────────────────────────────────────────────────────────
// Returns "" on success, or a human-readable error message on failure (caller shows it).
wxString MinewaysFrame::LoadWorldFromDir(const wxString& dir)
{
    ClearLoadedWorld();

    wchar_t wdir[MAX_PATH_AND_FILE];
    if (!_mwUtf8ToWideBuffer(dir.utf8_str(), wdir, MAX_PATH_AND_FILE))
        return "The world directory path is invalid or too long.";

    // Detect and set the world type
    gWorldGuide.type = WORLD_LEVEL_TYPE;
    wcscpy(gWorldGuide.world, wdir);
    wcscpy(gWorldGuide.directory, wdir);
    gWorldGuide.nbtVersion = 0;
    gWorldGuide.isServerWorld = false;
    wxFileName dimensionsDir = wxFileName::DirName(dir);
    dimensionsDir.AppendDir("dimensions");
    gWorldGuide.newFormat = wxDirExists(dimensionsDir.GetFullPath());

    // Verify it's a valid Anvil world
    wchar_t fileOpened[MAX_PATH_AND_FILE] = {};
    int nbtVer = 0;
    if (GetFileVersion(wdir, &nbtVer, fileOpened, MAX_PATH_AND_FILE) != 0) {
        ClearLoadedWorld();
        return "Could not read level.dat in this folder.";
    }
    gWorldGuide.nbtVersion = nbtVer;

    // Spawn/player position from level.dat (kept in globals for Jump to Spawn/Player)
    int spawnX = 0, spawnY = 64, spawnZ = 0;
    int playerX = 0, playerY = 64, playerZ = 0;
    int dimension = 0;
    GetSpawn(wdir, &spawnX, &spawnY, &spawnZ);
    if (GetPlayer(wdir, &playerX, &playerY, &playerZ, &dimension) != 0) {
        playerX = spawnX; playerY = spawnY; playerZ = spawnZ;
        gWorldGuide.isServerWorld = true;
    }
    gSpawnX = spawnX; gSpawnY = spawnY; gSpawnZ = spawnZ;
    gPlayerX = playerX; gPlayerY = playerY; gPlayerZ = playerZ;

    // Version detect
    GetFileVersionId(wdir, &gVersionID);
    gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
    gMinHeight = ZERO_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gMaxHeight = MAX_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gWorldGuide.minHeight = gMinHeight;
    gWorldGuide.maxHeight = gMaxHeight;

    // Center view on spawn
    gCurX = (double)spawnX;
    gCurZ = (double)spawnZ;
    gCurDepth = gMaxHeight;
    gTargetDepth = gMinHeight;
    gCurScale = 1.0;

    gLoaded = TRUE;

    SetTitle(wxString::Format("Mineways — %s", dir.AfterLast('/')));
    if (m_sliderTop) {
        m_sliderTop->SetRange(gMinHeight, gMaxHeight);
        m_sliderTop->SetValue(gCurDepth);
        m_labelTop->SetLabel(wxString::Format("%d", gCurDepth));
        m_sliderBot->SetRange(gMinHeight, gMaxHeight);
        m_sliderBot->SetValue(gTargetDepth);
        m_labelBot->SetLabel(wxString::Format("%d", gTargetDepth));
    }
    SetStatusText(wxString::Format("Loaded: %s  (spawn %d,%d,%d)",
                  dir.AfterLast('/'), spawnX, spawnY, spawnZ), 0);

    RedrawMap();
    return wxString();
}
