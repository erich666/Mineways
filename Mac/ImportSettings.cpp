// ImportSettings.cpp — Ctrl+I "Import Settings" for the wxWidgets/macOS build.
// Ported from Win/Mineways.cpp's importSettings/importModelFile/readAndExecuteScript/
// interpretImportLine/interpretScriptLine. See ImportSettings.h for what's deliberately
// not included.
#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include "ImportSettings.h"
#include "ExportDialog.h"       // InitViewExportData/InitPrintExportData
#include "MacCullingSchemes.h"  // SelectCullingSchemeByName
#include "stdafx.h"             // project types via compat.h -> blockInfo.h etc.
#include <cstdio>
#include <cstring>
#include <cwchar>

// ── Accessors from Mac/MinewaysFrame.cpp (same extern pattern MapPanel.cpp uses) ──────
class MinewaysFrame;
extern MinewaysFrame* gFrame;
extern double& GetCurX();
extern double& GetCurZ();
extern double& GetCurScale();
extern BOOL    IsLoaded();
extern int     GetMinHeight();
extern int     GetMaxHeight();
extern int&    GetCurDepth();
extern int&    GetTargetDepth();
extern BOOL&   GetHighlightOn();
extern Options*    GetOptions();
extern WorldGuide*  GetWorldGuide();
extern ExportFileData* GetEfd();
extern wchar_t*     GetExportPathBuf();   // 4096-wchar_t capacity

#include "MinewaysFrame.h"   // now that ExportFileData etc. are visible, pull in the real class

// ── Constants, ported from Win/Mineways.cpp ────────────────────────────────────────────
#define ISE_NO_DATA_TYPE_FOUND   0
#define ISE_RENDER_DATA_TYPE     1
#define ISE_3D_PRINT_DATA_TYPE   2

#define INTERPRETER_FOUND_ERROR              0x1
#define INTERPRETER_FOUND_NOTHING_USEFUL     0x2
#define INTERPRETER_FOUND_VALID_EXPORT_LINE  0x4
#define INTERPRETER_FOUND_VALID_LINE         0x8
#define INTERPRETER_REDRAW_SCREEN            0x10
#define INTERPRETER_END_READING              0x20
#define INTERPRETER_FOUND_CLOSE              0x40

#define MAX_ERRORS_DISPLAY   20
#define IMPORT_LINE_LENGTH   1024
#define ERROR_MESSAGE_BUFFER_SIZE 1024

#define RENDERING_EXPORT  0
#define PRINTING_EXPORT   1
#define SCHEMATIC_EXPORT  2
#define MAP_EXPORT_MODE   4

// ── ImportedSet: Mac equivalent of Win/Mineways.cpp's ImportedSet, minus WindowSet
// (Mac talks to gFrame directly instead of threading HWNDs through) ───────────────────
struct ImportedSet {
    int errorsFound = 0;
    bool readingModel = true;     // true: parsing an exported file's header. false: running a script.
    bool processData = true;      // false during a script's syntax-check-only first pass.
    int exportTypeFound = ISE_NO_DATA_TYPE_FOUND;
    int minxVal = 0, minyVal = 0, minzVal = 0, maxxVal = 0, maxyVal = 0, maxzVal = 0;
    ExportFileData* pEFD = nullptr;
    // While readingModel is true, pEFD points here (a scratch copy) so a header with an
    // error partway through can't leave the live gEfd half-modified — only committed to
    // GetEfd() on full success. Scripts (readingModel false) point pEFD at GetEfd() directly,
    // since a script is meant to apply each line's effect immediately as it runs.
    ExportFileData scratchEfd = {};
    wxString world;
    wxString terrainFile;
    wxString cullingScheme;
    int lineNumber = 1;
    wxString errorMessages;   // accumulated errors/warnings, newline separated
    bool closeProgram = false;
};

// ── Small string/line utilities, ported 1:1 from Win/Mineways.cpp ─────────────────────

static char* RemoveLeadingWhitespace(char* line)
{
    while (*line == ' ' || *line == '\t') line++;
    return line;
}

// Case-insensitive prefix match: if `b` is a prefix of `a` (case-insensitively), returns
// a pointer just past the matched prefix in `a`; else nullptr.
static char* CompareLCAndSkip(char* a, const char* b)
{
    char* start = a;
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return nullptr;
    }
    return *b ? nullptr : a;
    (void)start;
}

static char* FindLineDataNoCase(char* line, const char* findStr)
{
    char* strPtr = CompareLCAndSkip(line, findStr);
    if (!strPtr) return nullptr;
    return RemoveLeadingWhitespace(strPtr);
}

// Strip trailing "// comment", leading '#'/whitespace (if a model header line), strip
// stray '"' around " path: "/" name: " values, and trailing whitespace.
static wxString PrepareLineData(const wxString& rawLine, bool model)
{
    wxString line = rawLine;
    int slashslash = line.Find("//");
    if (slashslash != wxNOT_FOUND) line = line.Left(slashslash);

    size_t start = 0;
    while (start < line.length() &&
          ((model && line[start] == '#') || line[start] == ' ' || line[start] == '\t'))
        start++;
    line = line.Mid(start);

    if (line.Contains(" path: ") || line.Contains(" name: "))
        line.Replace("\"", "");

    line.Trim();   // trailing whitespace (wx also trims leading, harmless — already done above)
    return line;
}

// Returns true if we're inside (or still inside) a /* */ comment block after this line.
static bool DealWithCommentBlocks(wxString& line, bool commentBlock)
{
    if (commentBlock) {
        int endPos = line.Find("*/");
        if (endPos == wxNOT_FOUND) return true;   // still inside
        line = line.Mid(endPos + 2);
        commentBlock = false;
    }
    for (;;) {
        int startPos = line.Find("/*");
        if (startPos == wxNOT_FOUND) return commentBlock;
        int endPos = line.find("*/", startPos + 2);
        if (endPos == (int)wxString::npos) {
            line = line.Left(startPos);
            return true;
        }
        line = line.Left(startPos) + line.Mid(endPos + 2);
    }
}

// Turns "10, 20, 30 to 40, 50, 60" into "10 20 30 to 40 50 60" for sscanf.
static wxString CleanStringForLocations(const wxString& in)
{
    wxString out;
    bool firstSpace = true;
    for (size_t i = 0; i < in.length(); i++) {
        wxChar c = in[i];
        if (c == ' ' || c == '\t') {
            if (firstSpace) { out += ' '; firstSpace = false; }
        } else if (c == ',') {
            out += ' '; firstSpace = false;
        } else {
            out += c; firstSpace = true;
        }
    }
    return out;
}

static bool ValidBoolean(ImportedSet& is, const char* string1);
static void SaveErrorMessage(ImportedSet& is, const wxString& error, const wxString& restOfLine = wxString());
static void SaveWarningMessage(ImportedSet& is, const wxString& warning);

static bool InterpretBoolean(const char* string1)
{
    return (string1[0] == 'Y' || string1[0] == 'y' || string1[0] == 'T' || string1[0] == 't' || string1[0] == '1');
}

static bool ValidBoolean(ImportedSet& is, const char* string1)
{
    if (string1[0] == 'Y' || string1[0] == 'y' || string1[0] == 'T' || string1[0] == 't' || string1[0] == '1' ||
        string1[0] == 'N' || string1[0] == 'n' || string1[0] == 'F' || string1[0] == 'f' || string1[0] == '0')
        return true;
    SaveErrorMessage(is, "invalid boolean on line.");
    return false;
}

static void SaveMessage(ImportedSet& is, const wxString& msg, const wxString& msgType,
                        int increment, const wxString& restOfLine)
{
    wxString full;
    if (msg.Lower().StartsWith(msgType.Lower()))
        full = msg;
    else
        full = wxString::Format("%s reading line %d: %s", msgType, is.lineNumber, msg);
    if (!restOfLine.IsEmpty()) {
        wxString shown = restOfLine;
        if (shown.length() > 80) shown = shown.Left(77) + "...";
        full += " Rest of line: " + shown;
    }
    is.errorMessages += full + "\n";
    is.errorsFound += increment;
}
static void SaveErrorMessage(ImportedSet& is, const wxString& error, const wxString& restOfLine)
{
    SaveMessage(is, error, "Error", 1, restOfLine);
}
static void SaveWarningMessage(ImportedSet& is, const wxString& warning)
{
    SaveMessage(is, warning, "Warning", 0, wxString());
}

// findBitToggle: "<type>: yes/no" lines that XOR a gOptions.worldType bit (the render-mode
// toggles). Returns true if this line matched (regardless of success/failure); *retCode
// holds the interpreter result in that case.
static bool FindBitToggle(char* line, ImportedSet& is, const char* type, unsigned int bitLocation, int* retCode)
{
    *retCode = INTERPRETER_FOUND_NOTHING_USEFUL;
    char commandString[256];
    snprintf(commandString, sizeof(commandString), "%s:", type);
    char* strPtr = FindLineDataNoCase(line, commandString);
    if (!strPtr) return false;

    char string1[100] = {};
    if (sscanf(strPtr, "%99s", string1) != 1) {
        SaveErrorMessage(is, wxString::Format("could not find boolean value for '%s' command.", type));
        *retCode = INTERPRETER_FOUND_ERROR;
        return true;
    }
    if (!ValidBoolean(is, string1)) { *retCode = INTERPRETER_FOUND_ERROR; return true; }
    if (is.processData) {
        Options* opts = GetOptions();
        if (InterpretBoolean(string1)) opts->worldType |= bitLocation;
        else opts->worldType &= ~bitLocation;
    }
    *retCode = INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    return true;
}

// ── Command helpers, ported from Win/Mineways.cpp's commandLoadWorld/commandLoadTerrainFile/
// commandLoadCullingScheme/commandExportFile. Mac's LoadWorldFromDir only handles real Anvil
// world directories, so world-type dispatch (test world / schematic / directory) happens here.
static bool CommandLoadWorld(ImportedSet& is, wxString& error)
{
    wxString world = is.world;
    if (world.IsEmpty()) { error = "no world given."; return false; }

    if (world == "[Block Test World]") {
        gFrame->LoadTestBlockWorld();
        return true;
    }
    wxString lower = world.Lower();
    if (lower.EndsWith(".schematic") || lower.EndsWith(".schem")) {
        wxString err = gFrame->LoadSchematic(world);
        if (!err.IsEmpty()) { error = err; return false; }
        return true;
    }
    wxString err = gFrame->LoadWorldFromDir(world);
    if (!err.IsEmpty()) {
        error = wxString::Format("Mineways attempted to load world \"%s\" but could not do so: %s", world, err);
        return false;
    }
    return true;
}

static bool CommandLoadTerrainFile(ImportedSet& is, wxString& error)
{
    if (is.terrainFile.CmpNoCase("default") == 0) {
        wxString terrainPath = wxStandardPaths::Get().GetResourcesDir() + "/terrainExt.png";
        if (!wxFileExists(terrainPath)) {
            wxFileName exeFN(wxStandardPaths::Get().GetExecutablePath());
            terrainPath = exeFN.GetPath() + "/terrainExt.png";
        }
        gFrame->SetTerrainFile(terrainPath);
        return true;
    }
    if (!wxFileExists(is.terrainFile)) {
        error = wxString::Format("Terrain file \"%s\" was not found. Please select the terrain file manually.", is.terrainFile);
        return false;
    }
    gFrame->SetTerrainFile(is.terrainFile);
    return true;
}

static bool CommandLoadCullingScheme(ImportedSet& is, wxString& error)
{
    if (!SelectCullingSchemeByName(is.cullingScheme)) {
        error = wxString::Format("Mineways attempted to load culling scheme \"%s\" but could not do so. Please select it from the menu manually.", is.cullingScheme);
        return false;
    }
    return true;
}

// fileMode: RENDERING_EXPORT/PRINTING_EXPORT/SCHEMATIC_EXPORT/MAP_EXPORT_MODE
static bool CommandExportFile(ImportedSet& is, wxString& error, int fileMode, const wxString& fileName)
{
    if (!GetHighlightOn()) {
        error = "no volume is selected for export; use 'Selection location min to max:' or select on the map first.";
        return false;
    }
    if (fileMode == MAP_EXPORT_MODE) {
        int on, minx, miny, minz, maxx, maxy, maxz;
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, GetMinHeight());
        // Mac doesn't expose a script-callable "export map" primitive separate from the
        // menu's OnExportMap (which itself prompts for a path) — reuse its rendering path
        // by driving DrawMapToArray/writepng directly is out of scope for this pass; report
        // clearly rather than silently no-op.
        error = "Export Map: is not yet supported from Import Settings/scripts (use File > Export Map instead).";
        return false;
    }

    wchar_t wpath[MAX_PATH_AND_FILE];
    if (!_mwUtf8ToWideBuffer(fileName.utf8_str(), wpath, MAX_PATH_AND_FILE)) {
        error = "export path is invalid or too long.";
        return false;
    }
    wcsncpy(GetExportPathBuf(), wpath, 4095);
    GetExportPathBuf()[4095] = L'\0';

    ExportFileData* efd = GetEfd();
    if (fileMode == SCHEMATIC_EXPORT) {
        bool isSponge = fileName.Lower().EndsWith(".schem");
        efd->fileType = isSponge ? FILE_TYPE_SPONGE_SCHEMATIC : FILE_TYPE_SCHEMATIC;
    }
    gFrame->RunExport(*efd, GetExportPathBuf());
    return true;
}

// Handles a single boolean "Label: yes/no" line by setting *pField. Used inline (not a
// function) so it can `return` out of InterpretImportLine directly.
#define BOOL_FIELD_LINE(label, fieldLValue) \
    strPtr = FindLineDataNoCase(line, label); \
    if (strPtr) { \
        char string1[100] = {}; \
        if (sscanf(strPtr, "%99s", string1) != 1) { \
            SaveErrorMessage(is, "could not find boolean value for '" label "' command."); \
            return INTERPRETER_FOUND_ERROR; \
        } \
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR; \
        if (is.processData) (fieldLValue) = InterpretBoolean(string1); \
        return INTERPRETER_FOUND_VALID_EXPORT_LINE; \
    }

// The property-setting parser shared by header re-import and script execution (is.readingModel
// distinguishes the two — most branches behave identically either way). Ported from
// Win/Mineways.cpp's interpretImportLine (~line 7100-8273).
static int InterpretImportLine(char* line, ImportedSet& is)
{
    char* strPtr;
    float floatVal;

    if (line[0] == 0) return INTERPRETER_FOUND_NOTHING_USEFUL;

    // World: (also "Minecraft world: " and legacy "Extracted from Minecraft world saves/")
    strPtr = FindLineDataNoCase(line, "Extracted from Minecraft world saves/");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Minecraft world: ");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "World: ");
    if (strPtr) {
        if (*strPtr == 0) { SaveErrorMessage(is, "no world given."); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) {
            is.world = wxString::FromUTF8(strPtr);
            if (!is.readingModel) {
                wxString error;
                if (!CommandLoadWorld(is, error)) { SaveErrorMessage(is, error); return INTERPRETER_FOUND_ERROR; }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // Created for <Render|3D print> ... / Set render type: / Set 3D print type:
    int modelStyle = ISE_NO_DATA_TYPE_FOUND;
    strPtr = FindLineDataNoCase(line, "Created for ");
    if (strPtr) {
        if (*strPtr == '3') modelStyle = ISE_3D_PRINT_DATA_TYPE;
        else if (*strPtr == 'V') modelStyle = ISE_RENDER_DATA_TYPE;
        else { SaveErrorMessage(is, "could not determine whether model file is for 3D printing or rendering."); return INTERPRETER_FOUND_ERROR; }
    } else {
        strPtr = FindLineDataNoCase(line, "Set render type:");
        if (strPtr) modelStyle = ISE_RENDER_DATA_TYPE;
        else {
            strPtr = FindLineDataNoCase(line, "Set 3D print type:");
            if (strPtr) modelStyle = ISE_3D_PRINT_DATA_TYPE;
        }
    }
    if (strPtr) {
        is.exportTypeFound = modelStyle;
        ExportFileData* efd = is.readingModel ? &is.scratchEfd : GetEfd();
        if (is.readingModel || is.processData) {
            // Mac has one shared gEfd (not separate view/print instances like Windows); a
            // script's "Set render/3D print type:" just resets that shared struct to the
            // matching preset, same as the dialog's "Load ... Defaults" buttons.
            if (modelStyle == ISE_3D_PRINT_DATA_TYPE) InitPrintExportData(*efd);
            else InitViewExportData(*efd);
        }
        is.pEFD = efd;

        if (strstr(strPtr, "Wavefront OBJ absolute indices")) efd->fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
        else if (strstr(strPtr, "Wavefront OBJ relative indices")) efd->fileType = FILE_TYPE_WAVEFRONT_REL_OBJ;
        else if (strstr(strPtr, "USD 1.0")) efd->fileType = FILE_TYPE_USD;
        else if (strstr(strPtr, "Binary STL iMaterialise")) efd->fileType = FILE_TYPE_BINARY_MAGICS_STL;
        else if (strstr(strPtr, "Binary STL VisCAM")) efd->fileType = FILE_TYPE_BINARY_VISCAM_STL;
        else if (strstr(strPtr, "ASCII STL")) efd->fileType = FILE_TYPE_ASCII_STL;
        else if (strstr(strPtr, "VRML 2.0")) efd->fileType = FILE_TYPE_VRML2;
        else {
            SaveErrorMessage(is, "could not determine what type of model file (OBJ, USD, VRML, STL) is being read/desired.");
            return INTERPRETER_FOUND_ERROR;
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Terrain file name:");
    if (strPtr) {
        if (*strPtr == 0) { SaveErrorMessage(is, "no terrain file given."); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) {
            is.terrainFile = wxString::FromUTF8(strPtr);
            is.terrainFile.Replace("\\\\", "\\");
            if (!is.readingModel) {
                wxString error;
                if (!CommandLoadTerrainFile(is, error)) { SaveErrorMessage(is, error); return INTERPRETER_FOUND_ERROR; }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "View Overworld");
    if (strPtr) {
        if (is.processData && !is.readingModel) {
            if (!IsLoaded()) { SaveErrorMessage(is, "attempt to view Overworld but no world is loaded."); return INTERPRETER_FOUND_ERROR; }
            Options* opts = GetOptions();
            if (opts->worldType & (HELL | ENDER)) {
                if (opts->worldType & HELL) gFrame->SwitchToNether();   // toggles off since already Nether
                else gFrame->SwitchToTheEnd();
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "View Nether");
    if (strPtr) {
        if (is.processData) {
            if (!is.readingModel) {
                if (!IsLoaded()) { SaveErrorMessage(is, "attempt to view Nether but no world is loaded."); return INTERPRETER_FOUND_ERROR; }
                if (!gFrame->SwitchToNether()) { SaveWarningMessage(is, "attempt to switch to Nether but this world has none."); return INTERPRETER_FOUND_VALID_LINE; }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "View The End");
    if (strPtr) {
        if (is.processData) {
            if (!is.readingModel) {
                if (!IsLoaded()) { SaveErrorMessage(is, "attempt to view The End but no world is loaded."); return INTERPRETER_FOUND_ERROR; }
                if (!gFrame->SwitchToTheEnd()) { SaveWarningMessage(is, "attempt to switch to The End but this world has none."); return INTERPRETER_FOUND_VALID_LINE; }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "Culling scheme:");
    if (strPtr) {
        if (*strPtr == 0) { SaveErrorMessage(is, "no culling scheme given."); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) {
            is.cullingScheme = wxString::FromUTF8(strPtr);
            if (!is.readingModel) {
                wxString error;
                if (!CommandLoadCullingScheme(is, error)) { SaveWarningMessage(is, error); return INTERPRETER_FOUND_VALID_LINE; }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // Selection location min to max: / Selection location:
    strPtr = FindLineDataNoCase(line, "Selection location min to max:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Selection location:");
    if (strPtr) {
        bool noSelection = false;
        int v[6] = {};
        wxString cleaned = CleanStringForLocations(wxString::FromUTF8(strPtr));
        if (sscanf(cleaned.utf8_str(), "%d %d %d to %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
            char string1[100] = {};
            if (sscanf(strPtr, "%99s", string1) == 1 && _stricmp(string1, "none") == 0) {
                noSelection = true;
            } else if (sscanf(strPtr, "%99s", string1) == 1 && _stricmp(string1, "all") == 0) {
                v[0] = v[2] = -5000; v[1] = GetMinHeight();
                v[3] = v[5] = 5000;  v[4] = GetMaxHeight();
            } else {
                SaveErrorMessage(is, "could not read selection values 'x, y, z to x, y, z' or 'none' tag.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        if (is.processData) {
            if (is.readingModel || IsLoaded()) {
                if (noSelection) {
                    GetHighlightOn() = FALSE;
                    SetHighlightState(FALSE, 0, GetTargetDepth(), 0, 0, GetCurDepth(), 0, GetMinHeight(), GetMaxHeight(),
                                      IsLoaded() ? HIGHLIGHT_UNDO_PUSH : HIGHLIGHT_UNDO_CLEAR);
                } else {
                    is.minxVal = v[0]; is.minyVal = v[1]; is.minzVal = v[2];
                    is.maxxVal = v[3]; is.maxyVal = v[4]; is.maxzVal = v[5];
                    if (!is.readingModel) {
                        if (IsLoaded()) {
                            GetCurX() = (is.minxVal + is.maxxVal) / 2;
                            GetCurZ() = (is.minzVal + is.maxzVal) / 2;
                            GetHighlightOn() = TRUE;
                            SetHighlightState(1, is.minxVal, is.minyVal, is.minzVal, is.maxxVal, is.maxyVal, is.maxzVal,
                                              GetMinHeight(), GetMaxHeight(), HIGHLIGHT_UNDO_PUSH);
                            GetTargetDepth() = is.minyVal;
                            GetCurDepth() = is.maxyVal;
                        }
                    }
                }
            } else {
                SaveErrorMessage(is, "selection set but no world is loaded. For scripting, load a world manually first or include a 'Minecraft world:' command.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "Units for the model vertex data itself:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find units for the model itself."); return INTERPRETER_FOUND_ERROR; }
        int i;
        for (i = 0; i < MODELS_UNITS_TABLE_SIZE; i++) {
            char nameBuf[64];
            if (!_mwWideToUtf8Buffer(gUnitTypeTable[i].wname, nameBuf, sizeof(nameBuf))) continue;
            if (_stricmp(nameBuf, string1) == 0) { if (is.processData) is.pEFD->comboModelUnits[is.pEFD->fileType] = i; break; }
        }
        if (i >= MODELS_UNITS_TABLE_SIZE) { SaveErrorMessage(is, "could not interpret unit type for the model itself.", strPtr); return INTERPRETER_FOUND_ERROR; }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    // Lighting/atmosphere toggles
    {
        int retCode = 0;
        if (FindBitToggle(line, is, "Elevation shading", DEPTHSHADING, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Lighting", LIGHTING, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Transparent water", TRANSPARENT_WATER, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Map grid", MAP_GRID, &retCode)) return retCode;
    }

    // File type: Export <no|solid|richer|noise|full|all|tiles|separate|individual> ...
    strPtr = FindLineDataNoCase(line, "File type:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "Export %99s", string1) != 1) {
            SaveErrorMessage(is, "could not find Export string for file type (solid color, textured, etc.).");
            return INTERPRETER_FOUND_ERROR;
        }
        static const char* outputTypeString[] = { "no", "solid", "richer", "noise", "full", "all", "tiles", "separate", "individual" };
        static const int outputTypeCorrespondence[] = { 0, 1, 2, 2, 3, 3, 4, 4, 4 };
        const int n = sizeof(outputTypeCorrespondence) / sizeof(outputTypeCorrespondence[0]);
        int i;
        for (i = 0; i < n; i++) if (_stricmp(outputTypeString[i], string1) == 0) break;
        if (i >= n) { SaveErrorMessage(is, "could not interpret file type (solid color, textured, etc.).", strPtr); return INTERPRETER_FOUND_ERROR; }
        char* tileDir = nullptr;
        if (outputTypeCorrespondence[i] == 4) {
            tileDir = FindLineDataNoCase(line, "File type: Export individual textures to directory ");
            if (!tileDir) tileDir = FindLineDataNoCase(line, "File type: Export separate textures to directory ");
            if (!tileDir) tileDir = FindLineDataNoCase(line, "File type: Export tiles for textures to directory ");
            if (tileDir && strlen(tileDir) >= MAX_PATH) {
                SaveErrorMessage(is, wxString::Format(
                    "tile texture directory is too long (maximum %d UTF-8 bytes).", MAX_PATH - 1));
                return INTERPRETER_FOUND_ERROR;
            }
        }
        if (is.processData) {
            ExportFileData* efd = is.pEFD;
            int ft = efd->fileType;
            efd->radioExportNoMaterials[ft] = efd->radioExportMtlColors[ft] = efd->radioExportSolidTexture[ft] =
                efd->radioExportFullTexture[ft] = efd->radioExportTileTextures[ft] = 0;
            switch (outputTypeCorrespondence[i]) {
            case 0: efd->radioExportNoMaterials[ft] = 1; break;
            case 1: efd->radioExportMtlColors[ft] = 1; break;
            case 2: efd->radioExportSolidTexture[ft] = 1; break;
            case 3: efd->radioExportFullTexture[ft] = 1; break;
            case 4: {
                efd->radioExportTileTextures[ft] = 1;
                if (tileDir) memcpy(efd->tileDirString, tileDir, strlen(tileDir) + 1);
                break;
            }
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Texture output RGB:", is.pEFD->chkTextureRGB)
    BOOL_FIELD_LINE("Texture output A:", is.pEFD->chkTextureA)
    BOOL_FIELD_LINE("Texture output RGBA:", is.pEFD->chkTextureRGBA)
    BOOL_FIELD_LINE("Make groups objects:", is.pEFD->chkMakeGroupsObjects)

    strPtr = FindLineDataNoCase(line, "Export separate objects:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Export separate types:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Export separate objects' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkSeparateTypes = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Individual blocks:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Export individual blocks:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Individual blocks' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkIndividualBlocks[is.pEFD->fileType] = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Material per family:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Material per object:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Material per family' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkMaterialPerFamily = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Split materials into subtypes:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Split by block type:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Split by block type' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkSplitByBlockType = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = FindLineDataNoCase(line, "G3D full material:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Custom material:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'G3D full material' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkCustomMaterial[is.pEFD->fileType] = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Export MDL:", is.pEFD->chkExportMDL)

    strPtr = FindLineDataNoCase(line, "Light scale:");
    if (strPtr) {
        if (sscanf(strPtr, "%f", &floatVal) != 1) { SaveErrorMessage(is, "could not interpret value for Light scale command."); return INTERPRETER_FOUND_ERROR; }
        if (floatVal < 0.0f) { SaveErrorMessage(is, "Light scale value must be a non-negative number.", strPtr); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) is.pEFD->scaleLightsVal = floatVal;
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }
    strPtr = FindLineDataNoCase(line, "Surface emit scale:");
    if (strPtr) {
        if (sscanf(strPtr, "%f", &floatVal) != 1) { SaveErrorMessage(is, "could not interpret value for Surface emit scale command."); return INTERPRETER_FOUND_ERROR; }
        if (floatVal < 0.0f) { SaveErrorMessage(is, "Surface emit scale value must be a non-negative number.", strPtr); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) is.pEFD->scaleEmittersVal = floatVal;
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Export lesser blocks:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Export lesser blocks' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkExportAll = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }
    BOOL_FIELD_LINE("Fatten lesser blocks:", is.pEFD->chkFatten)
    BOOL_FIELD_LINE("Simplify mesh:", is.pEFD->chkDecimate)
    BOOL_FIELD_LINE("Double all billboard faces:", is.pEFD->chkDoubledBillboards)
    BOOL_FIELD_LINE("Create composite overlay faces:", is.pEFD->chkCompositeOverlay)
    BOOL_FIELD_LINE("Center model:", is.pEFD->chkCenterModel)
    BOOL_FIELD_LINE("Create block faces at the borders:", is.pEFD->chkBlockFacesAtBorders)

    strPtr = FindLineDataNoCase(line, "Make tree leaves solid:");
    if (!strPtr) strPtr = FindLineDataNoCase(line, "Tree leaves solid:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Tree leaves solid' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) is.pEFD->chkLeavesSolid = InterpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Use biomes:", is.pEFD->chkBiome)

    strPtr = FindLineDataNoCase(line, "Rotate model ");
    if (strPtr) {
        if (sscanf(strPtr, "%f degrees", &floatVal) != 1) { SaveErrorMessage(is, "could not interpret degrees value for Rotate model.", strPtr); return INTERPRETER_FOUND_ERROR; }
        if (floatVal != 0.0f && floatVal != 90.0f && floatVal != 180.0f && floatVal != 270.0f) {
            SaveErrorMessage(is, "rotation value must be 0, 90, 180, or 270 for Rotate model.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            ExportFileData* efd = is.pEFD;
            efd->radioRotate0 = (floatVal == 0.0f); efd->radioRotate90 = (floatVal == 90.0f);
            efd->radioRotate180 = (floatVal == 180.0f); efd->radioRotate270 = (floatVal == 270.0f);
            if (!(efd->radioRotate90 || efd->radioRotate180 || efd->radioRotate270)) efd->radioRotate0 = 1;
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Make Z the up direction instead of Y:", is.pEFD->chkMakeZUp[is.pEFD->fileType])

    // Scale model by <making each block %f mm high | aiming for a cost of %f for the <material> | fitting to a height of %f cm | using the minimum wall thickness for the <material>>
    strPtr = FindLineDataNoCase(line, "Scale model by ");
    if (strPtr) {
        ExportFileData* efd = is.pEFD;
        bool materialSet = false;
        char matBuf[300] = {};
        if (is.processData) {
            efd->radioScaleByBlock = efd->radioScaleByCost = efd->radioScaleToHeight = efd->radioScaleToMaterial = 0;
        }
        if (sscanf(strPtr, "making each block %f mm high", &floatVal) == 1) {
            if (floatVal <= 0.0f) { SaveErrorMessage(is, "model scale value must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
            if (is.processData) { efd->radioScaleByBlock = 1; efd->blockSizeVal[efd->fileType] = floatVal; }
        } else {
            const char* afterCostPrefix = strstr(strPtr, "aiming for a cost of ");
            const char* afterWallPrefix = strstr(strPtr, "using the minimum wall thickness for the ");
            if (afterCostPrefix == strPtr && sscanf(strPtr, "aiming for a cost of %f for the %299[^\n]", &floatVal, matBuf) == 2) {
                char* matEnd = strstr(matBuf, " material");
                if (!matEnd) { SaveErrorMessage(is, "could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
                *matEnd = 0;
                if (floatVal <= 0.0f) { SaveErrorMessage(is, "cost must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
                if (is.processData) { efd->radioScaleByCost = 1; efd->costVal = floatVal; }
                materialSet = true;
            } else if (afterWallPrefix == strPtr && sscanf(strPtr, "using the minimum wall thickness for the %299[^\n]", matBuf) == 1) {
                char* matEnd = strstr(matBuf, " material");
                if (!matEnd) { SaveErrorMessage(is, "could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
                *matEnd = 0;
                if (is.processData) efd->radioScaleToMaterial = 1;
                materialSet = true;
            } else if (sscanf(strPtr, "fitting to a height of %f cm", &floatVal) == 1) {
                if (floatVal <= 0.0f) { SaveErrorMessage(is, "height must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
                if (is.processData) { efd->radioScaleToHeight = 1; efd->modelHeightVal = floatVal; }
            } else {
                SaveErrorMessage(is, "could not understand scale line for Scale model command.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        if (materialSet && is.processData) {
            int i;
            for (i = 0; i < MTL_COST_TABLE_SIZE; i++) if (_stricmp(matBuf, gMtlCostTable[i].name) == 0) break;
            if (i >= MTL_COST_TABLE_SIZE) { SaveErrorMessage(is, "could not find name of material for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR; }
            efd->comboPhysicalMaterial[efd->fileType] = i;
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    // Fill air bubbles: <bool> Seal off entrances: <bool> Fill in isolated tunnels in base of model: <bool>
    strPtr = FindLineDataNoCase(line, "Fill air bubbles:");
    if (strPtr) {
        char s1[100] = {}, s2[100] = {}, s3[100] = {};
        if (sscanf(strPtr, "%99s Seal off entrances: %99s Fill in isolated tunnels in base of model: %99s", s1, s2, s3) != 3) {
            SaveErrorMessage(is, "could not find all boolean values for Fill air bubbles commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!ValidBoolean(is, s1) || !ValidBoolean(is, s2) || !ValidBoolean(is, s3)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) {
            is.pEFD->chkFillBubbles = InterpretBoolean(s1);
            is.pEFD->chkSealEntrances = InterpretBoolean(s2);
            is.pEFD->chkSealSideTunnels = InterpretBoolean(s3);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    // Connect parts sharing an edge: <bool> Connect corner tips: <bool> Weld all shared edges: <bool>
    strPtr = FindLineDataNoCase(line, "Connect parts sharing an edge:");
    if (strPtr) {
        char s1[100] = {}, s2[100] = {}, s3[100] = {};
        if (sscanf(strPtr, "%99s Connect corner tips: %99s Weld all shared edges: %99s", s1, s2, s3) != 3) {
            SaveErrorMessage(is, "could not find all boolean values for Connect parts commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!ValidBoolean(is, s1) || !ValidBoolean(is, s2) || !ValidBoolean(is, s3)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) {
            is.pEFD->chkConnectParts = InterpretBoolean(s1);
            is.pEFD->chkConnectCornerTips = InterpretBoolean(s2);
            is.pEFD->chkConnectAllEdges = InterpretBoolean(s3);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Delete floating objects:", is.pEFD->chkDeleteFloaters)

    // Hollow out bottom of model, making the walls <f> mm thick: <bool> Superhollow: <bool>
    strPtr = FindLineDataNoCase(line, "Hollow out bottom of model, making the walls ");
    if (strPtr) {
        char s1[100] = {}, s2[100] = {};
        if (sscanf(strPtr, "%f mm thick: %99s Superhollow: %99s", &floatVal, s1, s2) != 3) {
            SaveErrorMessage(is, "could not find all parameters needed for Hollow commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!ValidBoolean(is, s1) || !ValidBoolean(is, s2)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) {
            is.pEFD->hollowThicknessVal[is.pEFD->fileType] = floatVal;
            is.pEFD->chkHollow[is.pEFD->fileType] = InterpretBoolean(s1);
            is.pEFD->chkSuperHollow[is.pEFD->fileType] = InterpretBoolean(s2);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    BOOL_FIELD_LINE("Melt snow blocks:", is.pEFD->chkMeltSnow)
    BOOL_FIELD_LINE("Debug: show separate parts as colors:", is.pEFD->chkShowParts)
    BOOL_FIELD_LINE("Debug: show weld blocks in bright colors:", is.pEFD->chkShowWelds)

    if (is.readingModel && FindLineDataNoCase(line, "Full current path")) {
        return INTERPRETER_FOUND_VALID_EXPORT_LINE | INTERPRETER_END_READING;
    }

    return INTERPRETER_FOUND_NOTHING_USEFUL;
}
#undef BOOL_FIELD_LINE

// Script-only action commands (not valid in a model header): world export, jump/focus/zoom
// navigation, reset options, the render-mode toggles (same bits, different menu), give-more-
// memory, and Close. Ported from Win/Mineways.cpp's interpretScriptLine (~line 8275-9059),
// minus Sketchfab (#ifdef SKETCHFAB block — no Mac Sketchfab integration), mouse remapping,
// custom printer materials, block-name translation, chunk size, unknown-block-ID override,
// log file management, and "Change blocks:" (see ImportSettings.h for why).
static int InterpretScriptLine(char* line, ImportedSet& is)
{
    char* strPtr;
    if (line[0] == 0) return INTERPRETER_FOUND_VALID_LINE;

    // Export <for Rendering|for 3D Printing|Schematic|Map>: <path>
    strPtr = FindLineDataNoCase(line, "Export ");
    if (strPtr) {
        int model = -1;
        char* strPtr2 = FindLineDataNoCase(strPtr, "for Rendering:");
        if (strPtr2) model = RENDERING_EXPORT;
        else {
            strPtr2 = FindLineDataNoCase(strPtr, "for 3D Printing:");
            if (strPtr2) model = PRINTING_EXPORT;
            else {
                strPtr2 = FindLineDataNoCase(strPtr, "Schematic:");
                if (strPtr2) model = SCHEMATIC_EXPORT;
                else {
                    strPtr2 = FindLineDataNoCase(strPtr, "Map:");
                    if (strPtr2) model = MAP_EXPORT_MODE;
                }
            }
        }
        if (model == -1) goto TryOtherCommands;
        if (*strPtr2 == 0) { SaveErrorMessage(is, "no export file name provided."); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) {
            wxString error;
            if (!CommandExportFile(is, error, model, wxString::FromUTF8(strPtr2))) {
                SaveErrorMessage(is, error);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

TryOtherCommands:
    strPtr = FindLineDataNoCase(line, "Jump to Spawn");
    if (strPtr) {
        if (is.processData) {
            if (!IsLoaded()) { SaveErrorMessage(is, "Jump to Spawn command failed, as no world has been loaded."); return INTERPRETER_FOUND_ERROR; }
            gFrame->JumpToSpawn();
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }
    strPtr = FindLineDataNoCase(line, "Jump to Player");
    if (strPtr) {
        if (is.processData) {
            if (!IsLoaded()) { SaveErrorMessage(is, "Jump to Player command failed, as no world has been loaded."); return INTERPRETER_FOUND_ERROR; }
            gFrame->JumpToPlayer();
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }
    strPtr = FindLineDataNoCase(line, "Jump to Model");
    if (strPtr) {
        if (is.processData) {
            if (!IsLoaded()) { SaveErrorMessage(is, "Jump to Model command failed, as no world has been loaded."); return INTERPRETER_FOUND_ERROR; }
            if (!gFrame->JumpToModel()) { SaveErrorMessage(is, "Jump to Model command failed, as nothing has been selected."); return INTERPRETER_FOUND_ERROR; }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "Reset export options:");
    if (strPtr) {
        if (is.processData) {
            ExportFileData* efd = GetEfd();
            if (strstr(strPtr, "Render")) InitViewExportData(*efd);
            else if (strstr(strPtr, "3D Print")) InitPrintExportData(*efd);
            else if (strstr(strPtr, "Schematic")) InitViewExportData(*efd);   // Mac has no separate schematic settings blob
            else { SaveErrorMessage(is, "could not determine what is to be reset. Options are 'Render', '3D Print', and 'Schematic'.", strPtr); return INTERPRETER_FOUND_ERROR; }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Focus view:");
    if (strPtr) {
        wxString cleaned = CleanStringForLocations(wxString::FromUTF8(strPtr));
        int v[2];
        if (sscanf(cleaned.utf8_str(), "%d %d", &v[0], &v[1]) != 2) {
            SaveErrorMessage(is, "could not read 'Focus view' coordinates.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            if (!IsLoaded()) { SaveErrorMessage(is, "focus view set but no world is loaded."); return INTERPRETER_FOUND_ERROR; }
            GetCurX() = v[0]; GetCurZ() = v[1];
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = FindLineDataNoCase(line, "Zoom:");
    if (strPtr) {
        int v;
        if (sscanf(strPtr, "%d", &v) != 1) { SaveErrorMessage(is, "could not read 'Zoom' value.", strPtr); return INTERPRETER_FOUND_ERROR; }
        if (v < 1 || v > 40) { SaveErrorMessage(is, "zoom factor must be from 1 to 40, inclusive.", strPtr); return INTERPRETER_FOUND_ERROR; }
        if (is.processData) GetCurScale() = v;
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    {
        int retCode = 0;
        if (FindBitToggle(line, is, "Show all objects", SHOWALL, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Show biomes", BIOMES, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Elevation shading", DEPTHSHADING, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Lighting", LIGHTING, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Cave mode", CAVEMODE, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Hide obscured", HIDEOBSCURED, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Transparent water", TRANSPARENT_WATER, &retCode)) return retCode;
        if (FindBitToggle(line, is, "Map grid", MAP_GRID, &retCode)) return retCode;
    }

    strPtr = FindLineDataNoCase(line, "Give more export memory:");
    if (strPtr) {
        char string1[100] = {};
        if (sscanf(strPtr, "%99s", string1) != 1) { SaveErrorMessage(is, "could not find boolean value for 'Give more export memory' command."); return INTERPRETER_FOUND_ERROR; }
        if (!ValidBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData) {
            GetOptions()->moreExportMemory = InterpretBoolean(string1);
            MinimizeCacheBlocks(GetOptions()->moreExportMemory);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = FindLineDataNoCase(line, "Close");
    if (strPtr) {
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_FOUND_CLOSE;
    }

    return INTERPRETER_FOUND_NOTHING_USEFUL;
}

// ── File readers ────────────────────────────────────────────────────────────────────
enum LineReadResult { LINE_READ_OK, LINE_READ_EOF, LINE_READ_ERROR };

class Utf8LineReader {
public:
    Utf8LineReader(const wxString& path, wxString& error) : m_path(path), m_file(path, "rb")
    {
        if (!m_file.IsOpened()) error = wxString::Format("Error: could not open file %s", path);
    }

    bool IsOpened() const { return m_file.IsOpened(); }

    LineReadResult ReadLine(wxString& line, wxString& error)
    {
        char bytes[IMPORT_LINE_LENGTH];
        size_t length = 0;
        for (;;) {
            if (m_bufferPos == m_bufferLength) {
                m_bufferLength = m_file.Read(m_buffer, sizeof(m_buffer));
                m_bufferPos = 0;
                if (m_bufferLength == 0) {
                    if (m_file.Error()) {
                        error = wxString::Format("Error: failed while reading file %s", m_path);
                        return LINE_READ_ERROR;
                    }
                    if (length == 0) return LINE_READ_EOF;
                    break;
                }
            }

            char c = m_buffer[m_bufferPos++];
            if (m_skipLF) {
                m_skipLF = false;
                if (c == '\n') continue;
            }
            if (c == '\n') break;
            if (c == '\r') { m_skipLF = true; break; }
            if (length >= sizeof(bytes) - 1) {
                error = "data on line was longer than 1023 UTF-8 bytes - aborting!";
                return LINE_READ_ERROR;
            }
            bytes[length++] = c;
        }

        size_t wideLength = wxConvUTF8.ToWChar(nullptr, 0, bytes, length);
        if (wideLength == wxCONV_FAILED) {
            error = wxString::Format("Error: file is not valid UTF-8: %s", m_path);
            return LINE_READ_ERROR;
        }
        wchar_t wide[IMPORT_LINE_LENGTH];
        if (wideLength > 0 && wxConvUTF8.ToWChar(wide, WXSIZEOF(wide), bytes, length) == wxCONV_FAILED) {
            error = wxString::Format("Error: could not decode UTF-8 file %s", m_path);
            return LINE_READ_ERROR;
        }
        line.assign(wide, wideLength);
        return LINE_READ_OK;
    }

private:
    wxString m_path;
    wxFFile m_file;
    char m_buffer[8192];
    size_t m_bufferPos = 0;
    size_t m_bufferLength = 0;
    bool m_skipLF = false;
};

static bool CopyLineToImportBuffer(const wxString& line, char (&lineBuf)[IMPORT_LINE_LENGTH],
                                   wxString& error)
{
    wxScopedCharBuffer utf8 = line.utf8_str();
    size_t byteLength = utf8.length();
    if (byteLength >= sizeof(lineBuf)) {
        error = "data on line was longer than 1023 UTF-8 bytes - aborting!";
        return false;
    }
    memcpy(lineBuf, utf8.data(), byteLength);
    lineBuf[byteLength] = 0;
    return true;
}

// Ported from Win/Mineways.cpp's importModelFile: reads a previously-exported file's
// '#'-comment header line by line via InterpretImportLine(readingModel=true), stopping at
// "Full current path" (writeStatistics's marker for "end of settings header") or after
// MAX_ERRORS_DISPLAY consecutive uninterpretable lines. Commits to the live gEfd only on
// full success (see ImportedSet::scratchEfd).
static bool ImportModelFile(const wxString& path, ImportedSet& is)
{
    wxString err;
    Utf8LineReader reader(path, err);
    if (!reader.IsOpened()) { is.errorMessages += err + "\n"; return false; }

    is.pEFD = &is.scratchEfd;
    InitViewExportData(is.scratchEfd);   // sane defaults if "Created for"/"Set render type:" is never found
    is.readingModel = true;
    is.processData = true;

    int consecutiveUseless = 0;
    bool endReading = false;
    while (!endReading) {
        wxString line;
        LineReadResult readResult = reader.ReadLine(line, err);
        if (readResult == LINE_READ_EOF) break;
        if (readResult == LINE_READ_ERROR) { is.errorMessages += err + "\n"; return false; }
        is.lineNumber++;
        wxString cleaned = PrepareLineData(line, true);
        char lineBuf[IMPORT_LINE_LENGTH];
        if (!CopyLineToImportBuffer(cleaned, lineBuf, err)) { is.errorMessages += err + "\n"; return false; }

        int ret = InterpretImportLine(lineBuf, is);
        if (ret & (INTERPRETER_FOUND_VALID_LINE | INTERPRETER_FOUND_VALID_EXPORT_LINE)) {
            consecutiveUseless = 0;
        }
        if ((ret & INTERPRETER_FOUND_VALID_EXPORT_LINE) && is.exportTypeFound == ISE_NO_DATA_TYPE_FOUND) {
            SaveErrorMessage(is, "cannot change an export setting until the export mode and file type are set. Expected to first see a line in the header starting 'Created for'.");
        } else if (ret & INTERPRETER_FOUND_NOTHING_USEFUL) {
            consecutiveUseless++;
        }
        if (ret & INTERPRETER_END_READING) endReading = true;
        if ((is.errorsFound > 0) || (consecutiveUseless >= MAX_ERRORS_DISPLAY) || (ret & INTERPRETER_FOUND_ERROR)) break;
    }

    if (is.errorsFound == 0 && is.exportTypeFound != ISE_NO_DATA_TYPE_FOUND) {
        is.scratchEfd.minxVal = is.minxVal; is.scratchEfd.minyVal = is.minyVal; is.scratchEfd.minzVal = is.minzVal;
        is.scratchEfd.maxxVal = is.maxxVal; is.scratchEfd.maxyVal = is.maxyVal; is.scratchEfd.maxzVal = is.maxzVal;
        *GetEfd() = is.scratchEfd;
        return true;
    }
    if (is.errorsFound == 0 && is.exportTypeFound == ISE_NO_DATA_TYPE_FOUND) {
        SaveErrorMessage(is, "could not determine whether this file was a 3D printing file or rendering file. Expected to see a line in the header starting 'Created for'.");
    }
    return false;
}

// Ported from Win/Mineways.cpp's readAndExecuteScript: two passes over the file — first a
// syntax-check-only dry run (processData=false, so nothing actually happens), then if that's
// clean, a real execution pass. Each line tries InterpretScriptLine first, then falls back to
// InterpretImportLine (a script is a superset of the header syntax plus action commands).
static bool ReadAndExecuteScript(const wxString& path, ImportedSet& is)
{
    auto runPass = [&](bool processData) -> bool {
        wxString err;
        Utf8LineReader reader(path, err);
        if (!reader.IsOpened()) { is.errorMessages += err + "\n"; return false; }
        is.readingModel = false;
        is.processData = processData;
        is.pEFD = GetEfd();
        bool commentBlock = false;
        for (;;) {
            wxString line;
            LineReadResult readResult = reader.ReadLine(line, err);
            if (readResult == LINE_READ_EOF) break;
            if (readResult == LINE_READ_ERROR) { is.errorMessages += err + "\n"; return false; }
            is.lineNumber++;
            bool nextCommentBlock = DealWithCommentBlocks(line, commentBlock);
            if (!commentBlock) {
                wxString cleaned = PrepareLineData(line, false);
                char lineBuf[IMPORT_LINE_LENGTH];
                if (!CopyLineToImportBuffer(cleaned, lineBuf, err)) { is.errorMessages += err + "\n"; return false; }

                int ret = InterpretScriptLine(lineBuf, is);
                if (ret & INTERPRETER_FOUND_NOTHING_USEFUL) {
                    ret = InterpretImportLine(lineBuf, is);
                    if (ret & INTERPRETER_FOUND_NOTHING_USEFUL)
                        SaveErrorMessage(is, "syntax error, cannot interpret command.", cleaned);
                }
                if (ret & INTERPRETER_FOUND_CLOSE) is.closeProgram = true;
            }
            commentBlock = nextCommentBlock;
            if ((is.errorsFound > 0) || is.closeProgram) break;
        }
        return true;
    };

    is.errorsFound = 0;
    is.lineNumber = 1;
    if (!runPass(false)) return false;   // syntax check only — no side effects
    if (is.errorsFound > 0) return false;

    is.errorMessages.Clear();
    is.errorsFound = 0;
    is.lineNumber = 1;
    is.closeProgram = false;
    return runPass(true) && is.errorsFound == 0;    // for real
}

// ── Top-level entry point ──────────────────────────────────────────────────────────────
bool ImportSettingsFile(wxWindow* parent, const wxString& path, wxString& errorOut)
{
    (void)parent;
    wxString err;
    Utf8LineReader reader(path, err);
    wxString firstLine;
    LineReadResult readResult = reader.IsOpened() ? reader.ReadLine(firstLine, err) : LINE_READ_ERROR;
    if (readResult != LINE_READ_OK) {
        errorOut = err.IsEmpty() ? wxString::Format("Error: could not read file %s", path) : err;
        return false;
    }

    // Same detection Win/Mineways.cpp's importSettings uses: does the first line look like
    // one of Mineways' own export headers? If not, treat the file as a script.
    bool exported = firstLine.Contains("# Wavefront OBJ file made by Mineways") ||
                    firstLine.Contains("#usda 1.0") ||
                    firstLine.Contains("#VRML V2.0 utf8") ||
                    firstLine.Contains("# Minecraft world:") ||
                    firstLine.Contains("# World:") ||
                    firstLine.Contains("# Extracted from Minecraft world");

    if (!exported && !path.Lower().EndsWith(".mwscript")) {
        errorOut = "This file is not a recognized Mineways export settings file. Only files with the .mwscript extension can be executed as scripts.";
        return false;
    }

    ImportedSet is;
    bool ok = exported ? ImportModelFile(path, is) : ReadAndExecuteScript(path, is);
    errorOut = is.errorMessages;
    return ok;
}
