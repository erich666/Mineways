/*
Copyright (c) 2011, Sean Kasun
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
#include "Mineways.h"
#include "ColorSchemes.h"
#include "ExportPrint.h"
#include "Location.h"
#ifdef SKETCHFAB
#include "publishSkfb.h"
#endif
#include "XZip.h"
#include "rwpng.h"
#include <assert.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <CommDlg.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

// Should really make a full-featured error system, a la https://www.softwariness.com/articles/assertions-in-cpp/, but this'll do for now.
// trick so that there is not a warning that there's a constant value being tested by an "if"
static bool gAlwaysFail = false;
// from http://stackoverflow.com/questions/8487986/file-macro-shows-full-path
#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define MY_ASSERT(val) if ((val) == 0) { \
    wchar_t assertbuf[1024]; \
    wsprintf(assertbuf, L"Serious error in file %S on line %d. Please write me at erich@acm.org and, as best you can, tell me the steps that caused it.", __FILENAME__, __LINE__); \
    FilterMessageBox(NULL, assertbuf, L"Error", MB_OK | MB_TOPMOST); \
} \

// zoomed all the way in. We could allow this to be larger...
// It's useful to have it high for Nether <--> overworld switches
#define MAXZOOM 40.0f
// zoomed all the way out
#define MINZOOM 1.0f
#define DEFAULTZOOM 1.0f

// how far outside the rectangle we'll select the corners and edges of the selection rectangle
#define SELECT_MARGIN 5

#define MAX_LOADSTRING 100

#define NUM_STR_SIZE 10

// window margins - there should be a lot more defines here...
#define MAIN_WINDOW_TOP (30+30)
#define SLIDER_LEFT  55
#define SLIDER_RIGHT 45

// should probably be a subroutine, but too many variables...
#define REDRAW_ALL  drawTheMap();\
                    gBlockLabel=IDBlock(LOWORD(gHoldlParam),HIWORD(gHoldlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,\
                        bitWidth,bitHeight,gMinHeight,gCurScale,&mx,&my,&mz,&type,&dataVal,&biome,gWorldGuide.type==WORLD_LEVEL_TYPE);\
                    updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);\
                    InvalidateRect(hWnd,NULL,FALSE);\
                    UpdateWindow(hWnd);
// InvalidateRect was TRUE in last arg - does it matter?

#define LOG_INFO( FH, OUTPUTSTRING ) \
if (FH) { \
    DWORD br; \
    if ( PortaWrite(FH, OUTPUTSTRING, strlen(OUTPUTSTRING))) { \
        FilterMessageBox(NULL, _T("log file opened but cannot write to it."), _T("Log error"), MB_OK | MB_ICONERROR | MB_TOPMOST); \
        PortaClose(FH); \
        (FH) = 0x0; \
    } \
}

// for outputting waterlogged status for a block
// this part is left off, as those types of blocks are always waterlogged:  ((gBlockDefinitions[type].flags & BLF_WATERLOG) ||
// Since we're just showing status, double-slabs that are waterlogged are shown as such
#define WATERLOGGED_LABEL(type,dataVal) ((gBlockDefinitions[type].flags & BLF_MAYWATERLOG) && (dataVal & WATERLOGGED_BIT))


// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name
// number of worlds in initial world list
#define MAX_WORLDS	50
TCHAR* gWorlds[MAX_WORLDS];			// the directory name for the world, which must be unique and so is the one we show
int gNumWorlds = 0;
#define MAX_TERRAIN_FILES	45
TCHAR* gTerrainFiles[MAX_TERRAIN_FILES];							// number of terrain files in initial list
int gNumTerrainFiles = 0;

static Options gOptions = { 0,   // which world is visible
    BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN | BLF_FLATTEN_SMALL,   // what's exportable (really, set on output)
    0x0,
    0,  // start with low memory
    INITIAL_CACHE_SIZE,	// cache size
    NULL };

static WorldGuide gWorldGuide;
// find versions here: https://minecraft.gamepedia.com/Data_version
static int gVersionID = 0;								// Minecraft version 1.9 (finally) introduced a version number for the releases. 0 means Minecraft world is earlier than 1.9.
// translate the number above into a version number, e.g. 12, 13, 14 for 1.12, 1.13, 1.14:
static int gMinecraftVersion = 0;
static int gMaxHeight = INIT_MAP_MAX_HEIGHT;
static int gMinHeight = 0;
static float gMinZoom = MINZOOM;
static BOOL gSameWorld = FALSE;
static BOOL gHoldSameWorld = FALSE;
static wchar_t gSelectTerrainPathAndName[MAX_PATH_AND_FILE];				//path and file name to selected terrainExt.png file, if any
static wchar_t gSelectTerrainDir[MAX_PATH_AND_FILE];				//path (no file name) to selected terrainExt.png file, if any
static wchar_t gImportFile[MAX_PATH_AND_FILE];					//import file for settings
static wchar_t gImportPath[MAX_PATH_AND_FILE];					//path to import file for settings
static wchar_t gFileOpened[MAX_PATH_AND_FILE];					// world file, for error reporting
static BOOL gLoaded = FALSE;								//world loaded?
static double gCurX, gCurZ;								//current X and Z
static int gLockMouseX = 0;                               // if true, don't allow this coordinate to change with mouse, 
static int gLockMouseZ = 0;
static double gCurScale = DEFAULTZOOM;					    //current zoom scale
static int gCurDepth = INIT_MAP_MAX_HEIGHT;					//current depth
static int gStartHiX, gStartHiZ;						    //starting highlight X and Z

static BOOL gHighlightOn = FALSE;
static BOOL gLeftIsRight = FALSE;

static int gSpawnX, gSpawnY, gSpawnZ;
static int gPlayerX, gPlayerY, gPlayerZ;

// minimum depth output by default, sea level including water (or not, in 1.9; still, not a terrible start)
// note: 51 this gets you to bedrock in deep lakes
#define MIN_OVERWORLD_DEPTH SEA_LEVEL

static int gTargetDepth = MIN_OVERWORLD_DEPTH;								//how far down the depth is stored, for export

// Export 3d print and view data
static ExportFileData gExportPrintData;
static ExportFileData gExportViewData;
static ExportFileData gExportSchematicData;
static ExportFileData gExportSketchfabData;
#ifdef SKETCHFAB
static PublishSkfbData gSkfbPData;
#endif
// this one is set to whichever is active for export or import, 3D printing or rendering
static ExportFileData* gpEFD = NULL;

static int gOverworldHideStatus = 0x0;

static wchar_t gExePath[MAX_PATH_AND_FILE];
static wchar_t gExe[MAX_PATH_AND_FILE];
static wchar_t gExeDirectory[MAX_PATH_AND_FILE];
static wchar_t gWorldPathDefault[MAX_PATH_AND_FILE];
static wchar_t gWorldPathCurrent[MAX_PATH_AND_FILE];

// low, inside, high for selection area, fourth value is minimum height found below selection box
static int gHitsFound[4];
static int gFullLow = 1;
static int gAdjustingSelection = 0;
static bool gShowPrintStats = true;
static bool gShowRenderStats = true;
static int gAutocorrectDepth = 1;

static int gBottomControlEnabled = FALSE;

// export type selected in menu
#define	RENDERING_EXPORT	0
#define	PRINTING_EXPORT		1
#define SCHEMATIC_EXPORT	2
#define SKETCHFAB_EXPORT	3
#define MAP_EXPORT          4

static int gPrintModel = RENDERING_EXPORT;
static BOOL gExported = 0;
static TCHAR gExportPath[MAX_PATH_AND_FILE] = _T("");

static WORD gMinewaysMajorVersion = 0;
static WORD gMinewaysMinorVersion = 0;
static WORD gBuildNumber = 0;
static WORD gRevisionNumber = 0;

static HCURSOR gArrowCursor = NULL;
static HCURSOR gNsCursor = NULL;
static HCURSOR gWeCursor = NULL;
static HCURSOR gNeswCursor = NULL;
static HCURSOR gNwseCursor = NULL;

static wchar_t gSchemeSelected[255];

static int gImportFilterIndex = 1;
static int gOpenFilterIndex = 1;

static bool gScriptExportWarning = false;

ChangeBlockCommand* gChangeBlockCommands = NULL;

static wchar_t gPreferredSeparator = (wchar_t)'\\';
static wchar_t gPreferredSeparatorString[2];
static wchar_t gLesserSeparator = (wchar_t)'/';

static int gArgCount = 0;
static LPWSTR* gArgList = NULL;

static HANDLE gExecutionLogfile = 0x0;

static wchar_t* gCustomCurrency = NULL;

static int gSubError = 0;

static int gOneTimeDrawError = true;
static int gOneTimeDrawWarning = NBT_WARNING_NAME_NOT_FOUND;

static int gInstanceError = true;

static int gInstanceChunkSize = 16;

static int gBiomeSelected = -1;
static int gGroupCount = 0;
static int gGroupCountSize = 10;
static int gGroupCountArray[10];

// for whether various info/warning/error messages pop up
// - can be suppressed when scripts are running
static bool gShowInformational = true;
static bool gShowWarning = true;
static bool gShowError = true;

// left mouse button maps to left by default, etc.
// The left mouse button pans the view, middle selects height of the bottom, right sets or adjusts the selection rectangle
// If, say, gRemapMouse[LEFT_MOUSE_BUTTON_INDEX] is set to 1, that remaps
// the ability of the left mouse (i.e., pan movement) to the middle mouse button instead
static int gRemapMouse[3] = { 0,1,2 };

#define LEFT_MOUSE_BUTTON_INDEX 0
#define MIDDLE_MOUSE_BUTTON_INDEX 1
#define RIGHT_MOUSE_BUTTON_INDEX 2

#define IMPORT_FAILED	0
#define	IMPORT_MODEL	1
#define	IMPORT_SCRIPT	2

#define ISE_NO_DATA_TYPE_FOUND		0
#define ISE_RENDER_DATA_TYPE		1
#define ISE_3D_PRINT_DATA_TYPE		2

// for reading scripts and model headers
// If something could not be parsed correctly:
#define INTERPRETER_FOUND_ERROR				0x1
// If nothing useful was found on the line (nothing matched any commands).
// should be used only at end of parser method.
#define INTERPRETER_FOUND_NOTHING_USEFUL	0x2

// If we found a command having to do with export options:
#define INTERPRETER_FOUND_VALID_EXPORT_LINE 0x4
// If we found a command that was valid otherwise:
#define INTERPRETER_FOUND_VALID_LINE		0x8

// If the command found will result in the screen changing:
#define INTERPRETER_REDRAW_SCREEN			0x10
// If the command found means "stop reading this file":
#define	INTERPRETER_END_READING				0x20
// If the command "Close" to close the program itself was found
#define INTERPRETER_FOUND_CLOSE				0x40


#define MAX_ERRORS_DISPLAY	20

// values passed in for the import and script command system, not to be touched by the system.
typedef struct WindowSet {
    HWND hWnd;
    HWND hwndSlider;
    HWND hwndBottomSlider;
    HWND hwndLabel;
    HWND hwndBottomLabel;
    HWND hwndInfoLabel;
    HWND hwndInfoBottomLabel;
    HWND hwndStatus;
} WindowSet;

typedef struct ImportedSet {
    WindowSet ws;
    int errorsFound;
    bool closeProgram;
    bool readingModel;
    int exportTypeFound;
    int minxVal;
    int minyVal;
    int minzVal;
    int maxxVal;
    int maxyVal;
    int maxzVal;
    ExportFileData* pEFD;		// where to directly save data, no deferral
    ExportFileData* pSaveEFD;	// deferred save, for model load
    char world[MAX_PATH_AND_FILE];
    char terrainFile[MAX_PATH_AND_FILE];
    char colorScheme[MAX_PATH_AND_FILE];
    wchar_t* importFile;
    int lineNumber;
    size_t errorMessagesStringSize;
    wchar_t* errorMessages;
    bool processData;
    bool nether;
    bool theEnd;
    bool logging;
    char logFileName[MAX_PATH_AND_FILE];
    HANDLE logfile;
    ChangeBlockCommand* pCBChead;
    ChangeBlockCommand* pCBClast;
} ImportedSet;

static WindowSet gWS;

// Error codes - see ObjFileManip.h for error numbers, look for MW_NO_ERROR
static struct {
    TCHAR* text;
    TCHAR* caption;
    unsigned int type;
} gPopupInfo[] = {
    {_T("No error"), _T("No error"), MB_OK},	//00
    {_T("Warning: thin walls possible.\n\nThe thickness of a single block is smaller than the recommended wall thickness. Print only if you know what you're doing."), _T("Warning"), MB_OK | MB_ICONWARNING},	// 1
    {_T("Warning: sum of dimensions low.\n\nThe sum of the dimensions of the model is less than 65 mm. This is officially too small for a Shapeways color sandstone model, but they will probably print it anyway."), _T("Warning"), MB_OK | MB_ICONWARNING},	// <<1
    {_T("Warning: too many polygons.\n\nThere are more than one million polygons in file. This is usually too many for Shapeways."), _T("Warning"), MB_OK | MB_ICONWARNING},	// <<2
    {_T("Warning: multiple separate parts found after processing.\n\nThis may not be what you want to print. Increase the value for 'Delete floating parts' to delete these. Try the 'Debug: show separate parts' export option to see if the model is what you expected."), _T("Warning"), MB_OK | MB_ICONWARNING},	// <<3
    {_T("Warning: at least one dimension of the model is too long.\n\nCheck the dimensions for this printer's material: look in the top of the model file itself, using a text editor."), _T("Warning"), MB_OK | MB_ICONWARNING},	// <<4
    {_T("Warning: Mineways encountered an unknown block type in your model. Such blocks are converted to bedrock or, for 1.13+ blocks, to grass. Mineways does not understand blocks added by mods, and uses the older (simpler) schematic format so does not support blocks added in 1.13 or newer versions. If you are not using mods nor exporting 1.13 or newer blocks, your version of Mineways may be out of date. Check http://mineways.com for a newer version."), _T("Warning"), MB_OK | MB_ICONWARNING},	// <<5
    {_T("Warning: too few rows of block textures were found in your terrain\ntexture file. Newer block types will not export properly.\nPlease use the TileMaker program or other image editor\nto make a TerrainExt*.png with 57 rows."), _T("Warning"), MB_OK | MB_ICONWARNING },	// <<6 VERTICAL_TILES
    {_T("Warning: one or more Change Block commands specified location(s) that were outside the selected volume."), _T("Warning"), MB_OK | MB_ICONWARNING },	// <<6
    {_T("Warning: with the large Terrain File you're using, the output texture is extremely large. Other programs make have problems using it. We recommend that you use the 'Export tiles' option instead, or reduce the size of your Terrain File by using the '-t 256' (or smaller) option in TileMaker.\n\nThis warning will not be repeated this session."), _T("Warning"), MB_OK | MB_ICONWARNING },	// <<6

    {_T("Error: no solid blocks found; no file output. If you see something on the map, you likely need to set the Depth slider at the top to 0, or tap the space bar for a reasonable guess."), _T("Export warning"), MB_OK | MB_ICONERROR},	// <<7
    {_T("Error: all solid blocks were deleted; no file output"), _T("Export warning"), MB_OK | MB_ICONERROR},	// <<8
    {_T("Error creating export file; no file output"), _T("Export error"), MB_OK | MB_ICONERROR},	// <<9
    {_T("Error: cannot write to export file"), _T("Export error"), MB_OK | MB_ICONERROR},	// <<10
    {_T("Error: the incoming terrainExt*.png file resolution must be divisible by 16 horizontally and at least 16 pixels wide."), _T("Export error"), MB_OK | MB_ICONERROR},	// <<11
    {_T("Error: the incoming terrainExt*.png file image has fewer than 16 rows of block tiles."), _T("Export error"), MB_OK | MB_ICONERROR},	// <<12
    {_T("Error: the exported volume cannot have a dimension greater than 65535."), _T("Export error"), MB_OK | MB_ICONERROR},	// <<13 MW_DIMENSION_TOO_LARGE
    {_T("Error: cannot read import file."), _T("Import error"), MB_OK | MB_ICONERROR},	// <<14
    {_T("Error: opened import file, but cannot read it properly."), _T("Import error"), MB_OK | MB_ICONERROR},	// <<15
    {_T("Error: out of memory - terrainExt*.png texture is too large. Try 'Help | Give more export memory!', or please use a texture with a lower resolution."), _T("Memory error"), MB_OK | MB_ICONERROR},	// <<16
    {_T("Error: out of memory - volume of world chosen is too large. RESTART PROGRAM, then try 'Help | Give more export memory!'. If that fails, export smaller portions of your world."), _T("Memory error"), MB_OK | MB_ICONERROR},	// <<17
    {_T("Error: directory for individual textures could not be created. Please fix whatever you put for the directory next to the 'Export separate tiles' option. Do not use a path of any sort, just give a folder name."), _T("Internal error"), MB_OK | MB_ICONERROR},	// <<18
    {_T("Error: yikes, internal error! Please let me know what you were doing and what went wrong: erich@acm.org"), _T("Internal error"), MB_OK | MB_ICONERROR},	// <<18

    // old error, but now we don't notice if the file has changed, so we make it identical to the "file missing" error
    // {_T("Error: cannot read your custom terrainExt*.png file.\n\nPNG error: %s"), _T("Export error"), MB_OK|MB_ICONERROR},	// << 19
    {_T("Error: cannot read terrainExt*.png file.\n\nPNG error: %s\n\nPlease check that your terrainExt*.png file is a valid PNG file. If you continue to have problems, download Mineways again."), _T("Export error"), MB_OK | MB_ICONERROR},	// << 19
    {_T("Error writing to export file; partial file output\n\nPNG error: %s.\nThis error can often mean that antivirus software (such as Avast) is blocking Mineways. You need to give Mineways permission to write files. Another possibility is that your directory path is confusing Mineways. Try something simple, like c:\temp."), _T("Export error"), MB_OK | MB_ICONERROR},	// <<21
};

#define RUNNING_SCRIPT_STATUS_MESSAGE L"Running script commands"

#define IMPORT_LINE_LENGTH	1024


// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
static void closeMineways();
static bool startExecutionLogFile(const LPWSTR* argList, int argCount);
static int modifyWindowSizeFromCommandLine(int* x, int* y, const LPWSTR* argList, int argCount);
static int loadWorldFromFilename(wchar_t* pathAndFile, HWND hWnd);
static int getWorldSaveDirectoryFromCommandLine(wchar_t* saveWorldDirectory, const LPWSTR* argList, int argCount);
static int getSuppressFromCommandLine(const LPWSTR* argList, int argCount);
static int getZoomLevelFromCommandLine(float* minZoom, const LPWSTR* argList, int argCount);
static int getTerrainFileFromCommandLine(wchar_t* TerrainFile, const LPWSTR* argList, int argCount);
static bool processCreateArguments(WindowSet& ws, const char** pBlockLabel, LPARAM holdlParam, const LPWSTR* argList, int argCount);
static void runImportOrScript(wchar_t* importFile, WindowSet& ws, const char** pBlockLabel, LPARAM holdlParam, bool dialogOnSuccess);
static int loadSchematic(wchar_t* pathAndFile);
static void setHeightsFromVersionID();
static void testWorldHeight(int& minHeight, int& maxHeight, int mcVersion, int spawnX, int spawnZ, int playerX, int playerZ);
static int loadWorld(HWND hWnd);
static void strcpyLimited(char* dst, int len, const char* src);
static int setWorldPath(TCHAR* path);
//static void homePathMac(TCHAR *path);
static void enableBottomControl(int state, /* HWND hwndBottomSlider, HWND hwndBottomLabel, */ HWND hwndInfoBottomLabel);
static void validateItems(HMENU menu);
static int loadWorldList(HMENU menu);
static int loadTerrainList(HMENU menu);
static void drawTheMap();
static void setUIOnLoadWorld(HWND hWnd, HWND hwndSlider, HWND hwndLabel, HWND hwndInfoLabel, HWND hwndBottomSlider, HWND hwndBottomLabel);
static void updateCursor(LPARAM lParam, BOOL hdragging);
static void gotoSurface(HWND hWnd, HWND hwndSlider, HWND hwndLabel);
static void updateStatus(int mx, int mz, int my, const char* blockLabel, int type, int dataVal, int biome, HWND hwndStatus);
static void sendStatusMessage(HWND hwndStatus, wchar_t* buf);
static void populateColorSchemes(HMENU menu);
static void useCustomColor(int wmId, HWND hWnd, bool invalidate = true);
static int findColorScheme(wchar_t* name);
static void setSlider(HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth, bool update);
static void drawInvalidateUpdate(HWND hWnd);
static void syncCurrentHighlightDepth();
static void copyOverExportPrintData(ExportFileData* pEFD);
static int saveObjFile(HWND hWnd, wchar_t* objFileName, int printModel, wchar_t* terrainFileName, wchar_t* schemeSelected, bool showDialog, bool showStatistics);
#ifdef SKETCHFAB
static int setSketchfabExportSettings();
static LPTSTR prepareSketchfabExportFile(HWND hWnd);
static int processSketchfabExport(PublishSkfbData* skfbPData, wchar_t* objFileName, wchar_t* terrainFileName, wchar_t* schemeSelected);
static int publishToSketchfab(HWND hWnd, wchar_t* objFileName, wchar_t* terrainFileName, wchar_t* schemeSelected);
static bool commandSketchfabPublish(ImportedSet& is, wchar_t* error);
static bool isSketchfabFieldSizeValid(char* field, int size, bool exact = false);
#endif
static void addChangeBlockCommandsToGlobalList(ImportedSet& is);
static void PopupErrorDialogs(int errCode);
static const wchar_t* removePath(const wchar_t* src);
static void initializeExportDialogData();
static void initializePrintExportData(ExportFileData& printData);
static void initializeViewExportData(ExportFileData& viewData);
static void InitializeSchematicExportData(ExportFileData& schematicData);
static void InitializeSketchfabExportData(ExportFileData& sketchfabData);
static int importSettings(wchar_t* importFile, ImportedSet& is, bool dialogOnSuccess);
static bool importModelFile(wchar_t* importFile, ImportedSet& is);
static bool readAndExecuteScript(wchar_t* importFile, ImportedSet& is);
static void initializeImportedSet(ImportedSet& is, ExportFileData* pEFD, wchar_t* importFile);
static int readLine(FILE* fh, char* inputString, int stringLength);
static char* prepareLineData(char* line, bool model);
static bool dealWithCommentBlocks(char* line, bool commentBlock);
static bool startCommentBlock(char* line);
static char* closeCommentBlock(char* line);
static int switchToNether(ImportedSet& is);
static int switchToTheEnd(ImportedSet& is);
static int interpretImportLine(char* line, ImportedSet& is);
static int interpretScriptLine(char* line, ImportedSet& is);
static bool findBitToggle(char* line, ImportedSet& is, char* type, unsigned int bitLocation, unsigned int windowID, int* pRetCode);
static bool testChangeBlockCommand(char* line, ImportedSet& is, int* pRetCode);
static void cleanStringForLocations(char* cleanString, char* strPtr);
static char* findBlockTypeAndData(char* line, int* pType, int* pData, unsigned short* pDataBits, wchar_t* error);
static char* compareLCAndSkip(char* a, char const* b);
static char* skipPastUnsignedInteger(char* strPtr);
static void createCB(ImportedSet& is);
static void addFromRangeToCB(ChangeBlockCommand* pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits);
static void setDefaultFromRangeToCB(ChangeBlockCommand* pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits);
static void addRangeToDataBitsArray(ChangeBlockCommand* pCBC, int fromType, int fromEndType, unsigned short fromDataBits);
static void saveCBinto(ChangeBlockCommand* pCBC, int intoType, int intoData);
static void addDataBitsArray(ChangeBlockCommand* pCBC);
static void saveCBlocation(ChangeBlockCommand* pCBC, int v[6]);
static void deleteCommandBlockSet(ChangeBlockCommand* pCBC);
static char* findLineDataNoCase(char* line, char* findStr);
static char* removeLeadingWhitespace(char* line);
static void cleanseBackslashes(char* line);
static void saveErrorMessage(ImportedSet& is, wchar_t* error, char* restOfLine = NULL);
static void saveWarningMessage(ImportedSet& is, wchar_t* error);
static void saveMessage(ImportedSet& is, wchar_t* error, wchar_t* msgType, int increment, char* restOfLine = NULL);
static bool validBoolean(ImportedSet& is, char* string);
static bool interpretBoolean(char* string);
static bool validMouse(ImportedSet& is, char* string);
static int interpretMouse(char* string);

void formTitle(WorldGuide* pWorldGuide, HWND hWnd);
static void rationalizeFilePath(wchar_t* fileName);
//static void checkUpdate( HINSTANCE hInstance );
static bool splitToPathAndName(wchar_t* pathAndName, wchar_t* path, wchar_t* name);
static bool commandLoadWorld(ImportedSet& is, wchar_t* error);
static bool commandLoadTerrainFile(ImportedSet& is, wchar_t* error);
static bool commandLoadColorScheme(ImportedSet& is, wchar_t* error, bool invalidate = true);
static bool commandExportFile(ImportedSet& is, wchar_t* error, int fileMode, char* fileName);
static bool openLogFile(ImportedSet& is);
//static void logHandles();
static void showLoadWorldError(int loadErr);
static void checkMapDrawErrorCode(int retCode);
static bool saveMapFile(int xmin, int zmin, int xmax, int ymax, int zmax, wchar_t* mapFileName);
static int FilterMessageBox(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType);
static void GoToBedrockHelpOnOK(int retcode);


int APIENTRY _tWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPTSTR lpCmdLine,
    _In_ int nCmdShow)
{
#ifdef TEST_FOR_MEMORY_LEAKS
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // get version info
    gMinewaysMajorVersion = MINEWAYS_MAJOR_VERSION;
    gMinewaysMinorVersion = MINEWAYS_MINOR_VERSION;

    // get location of executable,
    // see https://docs.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew
    GetModuleFileNameW(NULL, gExePath, MAX_PATH_AND_FILE);
    // strip out Mineways.exe from this path.
    splitToPathAndName(gExePath, gExeDirectory, gExe);
#ifdef _DEBUG
    // go to where TileMaker stores its terrainExt files.
    wcscat_s(gExeDirectory, MAX_PATH_AND_FILE, L"\\..\\..\\TileMaker\\TileMaker");

#endif
    // old, normally fine, but doesn't work right when Mineways is called from Omniverse: GetCurrentDirectory(MAX_PATH_AND_FILE, gExeDirectory);

    // which sort of separator? If "\" found, use that one, else "/" assumed.
    if (wcschr(gExeDirectory, (wchar_t)'\\') != NULL) {
        gPreferredSeparator = (wchar_t)'\\';
        gLesserSeparator = (wchar_t)'/';
    }
    else {
        // Mac
        gPreferredSeparator = (wchar_t)'/';
        gLesserSeparator = (wchar_t)'\\';
    }
    gPreferredSeparatorString[0] = gPreferredSeparator;
    gPreferredSeparatorString[1] = (wchar_t)0;
    SetSeparatorMap(gPreferredSeparatorString);
    SetSeparatorObj(gPreferredSeparatorString);

    // get argv, argc from command line.
    gArgList = CommandLineToArgvW(GetCommandLine(), &gArgCount);
    if (gArgList == NULL)
    {
        FilterMessageBox(NULL, L"Unable to parse command line", L"Error", MB_OK | MB_TOPMOST);
    }

    //for (int i = 0; i < gArgCount; i++)
    //{
    //	FilterMessageBox(NULL, gArgList[i], L"Arglist contents", MB_OK);
    //}

    // to force logging, no command line options needed, uncomment this line
    //gExecutionLogfile = PortaCreate(L"mineways_exec.log");

    // start logging system for application itself, to track down startup problems.
    if (!startExecutionLogFile(gArgList, gArgCount)) {
        // bad parse
        exit(EXIT_FAILURE);
    }

    char outputString[1024];
    if (gExecutionLogfile) {
        sprintf_s(outputString, 1024, "Preferred separator: %c\n", (char)gPreferredSeparator);
        LOG_INFO(gExecutionLogfile, outputString);
    }

    // assume terrainExt.png is in .exe's directory to start
    wcscpy_s(gSelectTerrainDir, MAX_PATH_AND_FILE, gExeDirectory);
    wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, gExeDirectory);
    wcscat_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE - wcslen(gSelectTerrainPathAndName), gPreferredSeparatorString);
    wcscat_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE - wcslen(gSelectTerrainPathAndName), L"terrainExt.png");

    // setting this to empty means the last path used (from last session, hopefully) will be used again
    wcscpy_s(gImportFile, MAX_PATH_AND_FILE, L"");
    wcscpy_s(gImportPath, MAX_PATH_AND_FILE, L"");

    gWorldGuide.type = WORLD_UNLOADED_TYPE;
    gWorldGuide.sch.blocks = gWorldGuide.sch.data = NULL;
    gWorldGuide.nbtVersion = 0;

    gSchemeSelected[0] = (wchar_t)0;

    // start it with something, anything...
    gpEFD = &gExportViewData;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LOG_INFO(gExecutionLogfile, "execute initializeExportDialogData\n");
    initializeExportDialogData();
#ifdef SKETCHFAB
    // Initialize Skfb data - not good practice, as std::string is involved
    memset(&gSkfbPData, 0, sizeof(PublishSkfbData));      // cppcheck-suppress 762
#endif

    memset(&gWS, 0, sizeof(gWS));

    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_MINEWAYS, szWindowClass, MAX_LOADSTRING);
    LOG_INFO(gExecutionLogfile, "execute MyRegisterClass\n");
    MyRegisterClass(hInstance);

    // Perform application initialization:
    LOG_INFO(gExecutionLogfile, "execute InitInstance\n");
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    LOG_INFO(gExecutionLogfile, "execute LoadAccelerators\n");
    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MINEWAYS));

    // One-time initialization for mapping and object export
    // set the pcolors properly once. They'll change from color schemes.
    LOG_INFO(gExecutionLogfile, "execute SetMapPremultipliedColors\n");
    SetMapPremultipliedColors(0);

    // Set biome colors - TODO: add texture support, etc.
    LOG_INFO(gExecutionLogfile, "execute PrecomputeBiomeColors\n");
    PrecomputeBiomeColors();

    gArrowCursor = LoadCursor(NULL, IDC_ARROW);
    gNsCursor = LoadCursor(NULL, IDC_SIZENS);
    gWeCursor = LoadCursor(NULL, IDC_SIZEWE);
    gNeswCursor = LoadCursor(NULL, IDC_SIZENESW);
    gNwseCursor = LoadCursor(NULL, IDC_SIZENWSE);

    // Main message loop:
    LOG_INFO(gExecutionLogfile, "execute GetMessage while loop\n");
    // see http://stackoverflow.com/questions/32768924/wm-quit-only-posts-for-thread-and-not-the-window
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            LOG_INFO(gExecutionLogfile, "  got error!\n");
            // handle the error and possibly exit
            DWORD dw = GetLastError();

            //LOG_INFO(gExecutionLogfile, "    about to format message\n");
            //LPVOID lpMsgBuf;
            //FormatMessage(
            //	FORMAT_MESSAGE_ALLOCATE_BUFFER |
            //	FORMAT_MESSAGE_FROM_SYSTEM |
            //	FORMAT_MESSAGE_IGNORE_INSERTS,
            //	NULL,
            //	dw,
            //	MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            //	(LPTSTR)&lpMsgBuf,
            //	0, NULL);

            // Display the error message and exit the process.
            // Look up code here: https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381(v=vs.85).aspx
            if (gExecutionLogfile) {
                sprintf_s(outputString, 1024, "Unexpected termination: failed with error code %d. Please report this problem to erich@acm.org\n", (int)dw);
                LOG_INFO(gExecutionLogfile, outputString);
            }
            wchar_t wString[1024];
            swprintf_s(wString, 1024, L"Unexpected termination: failed with error code %d. Please report this problem to erich@acm.org\n", (int)dw);
            FilterMessageBox(NULL, wString, _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        else
        {
            LOG_INFO(gExecutionLogfile, "  got a message\n");
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                LOG_INFO(gExecutionLogfile, "  translate accelerator passed\n");
                TranslateMessage(&msg);
                LOG_INFO(gExecutionLogfile, "  message translated\n");
                DispatchMessage(&msg);
                LOG_INFO(gExecutionLogfile, "  message dispatched\n");
            }
        }
    }

    return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINEWAYS));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(IDC_MINEWAYS);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_MINEWAYS)); // was IDI_SMALL, but it wasn't smaller

    return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND hWnd;

    hInst = hInstance; // Store instance handle in our global variable

    // initial size
    int x = 514;
    int y = 616;
    // 0 means failed, 1 means x and y changed, 2 means also minimize the window
    int windowStatus = modifyWindowSizeFromCommandLine(&x, &y, gArgList, gArgCount);
    if (windowStatus == 0) {
        exit(EXIT_FAILURE);
    }

    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, x, y, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, (windowStatus == 2) ? SW_SHOWMINIMIZED : nCmdShow);
    DragAcceptFiles(hWnd, TRUE);
    UpdateWindow(hWnd);

    return TRUE;
}


wchar_t* stripWorldName(wchar_t* worldPath)
{
    // find last "/" or "\", as possible
    wchar_t* lastSplitBackslash = wcsrchr(worldPath, '\\') + 1;
    wchar_t* lastSplitSlash = wcsrchr(worldPath, '/') + 1;
    wchar_t* lastSplit = worldPath;
    if (lastSplitBackslash > lastSplit)
        lastSplit = lastSplitBackslash;
    if (lastSplitSlash > lastSplit)
        lastSplit = lastSplitSlash;

    return lastSplit;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
static unsigned char* map = NULL;
static int bitWidth = 0;
static int bitHeight = 0;
static HWND progressBar = NULL;
static HWND statusWindow = NULL;
static HBRUSH ctlBrush = NULL;
// TODO: Warning	C6262	Function uses '16576' bytes of stack.Consider moving some data to heap.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId; // set but not used, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;
    static HWND hwndSlider, hwndBottomSlider, hwndLabel, hwndBottomLabel, hwndInfoLabel, hwndInfoBottomLabel, hwndStatus;
    static BITMAPINFO bmi;
    static HBITMAP bitmap = NULL;
    static HDC hdcMem = NULL;
    static int oldX = 0, oldY = 0;
    static const char* gBlockLabel = "";
    static BOOL dragging = FALSE;
    static BOOL hdragging = FALSE;	// highlight dragging, dragging on the edge of the selected area
    static int moving = 0;
    INITCOMMONCONTROLSEX ice;
    DWORD pos;
    wchar_t text[NUM_STR_SIZE];
    RECT rect;
    TCHAR path[MAX_PATH_AND_FILE];
    TCHAR pathAndFile[MAX_PATH_AND_FILE];
    OPENFILENAME ofn;
    int mx, my, mz, type, dataVal, biome;
    static LPARAM gHoldlParam;
    int on, minx, miny, minz, maxx, maxy, maxz;
    BOOL saveOK;
    wchar_t msgString[1024];

    // Show message
    char outputString[1024];
    if (gExecutionLogfile) {
        sprintf_s(outputString, 1024, "process message %u\n", message);
        LOG_INFO(gExecutionLogfile, outputString);
    }
    //#ifdef _DEBUG
    //    wchar_t buf[100];
    //    swprintf( buf, 100, L"Message: %u\n", message);
    //    OutputDebugStringW( buf );
    //#endif

    switch (message)
    {
    case WM_CREATE:
    {
        validateItems(GetMenu(hWnd));

        int val = getSuppressFromCommandLine(gArgList, gArgCount);
        if (val > 0) {
            // -suppress found on command line.
            LOG_INFO(gExecutionLogfile, " getSuppressFromCommandLine successful\n");
            gShowInformational = false;
            gShowWarning = false;
            gShowError = false;
        }

        // set zoom level, if changed on command line
        float minZoom = 1.0f;
        val = getZoomLevelFromCommandLine(&minZoom, gArgList, gArgCount);
        if (val > 0) {
            // legal zoom level found on command line.
            LOG_INFO(gExecutionLogfile, " getZoomLevelFromCommandLine successful\n");
            assert(minZoom >= 0.0625f && minZoom <= MAXZOOM);
            gMinZoom = minZoom;
        }

        // set terrain file: -t terrainExt*.png
        wchar_t terrainFileName[MAX_PATH_AND_FILE];
        val = getTerrainFileFromCommandLine(terrainFileName, gArgList, gArgCount);
        if (val > 0) {
            // file found on command line.
            LOG_INFO(gExecutionLogfile, " getTerrainFileFromCommandLine successful\n");
            rationalizeFilePath(terrainFileName);
            // lame way to figure out if the path is relative - if no ":"
            wchar_t colon[] = L":";
            if (wcschr(terrainFileName, colon[0]) == NULL) {
                // relative path, so add absolute path to current directory to front.
                wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, gExeDirectory);
                wcscat_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, gPreferredSeparatorString);
                wcscat_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, terrainFileName);
            }
            else {
                wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, terrainFileName);
            }
            splitToPathAndName(gSelectTerrainPathAndName, gSelectTerrainDir, NULL);
            formTitle(&gWorldGuide, hWnd);
        }


        // get new directory for where world saves are located, if any: -s dir
        val = getWorldSaveDirectoryFromCommandLine(gWorldPathDefault, gArgList, gArgCount);
        if (val > 0) {
            // path found on command line, try it out.
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine successful\n");
        }
        else if (val < 0) {
            // path set to none on command line.
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine successfully set to 'none,' with no worlds to be loaded\n");
        }
        else {
            // load default list of worlds
            LOG_INFO(gExecutionLogfile, " setWorldPath\n");
            int retCode = setWorldPath(gWorldPathDefault);

            if (retCode == 0)
            {
                LOG_INFO(gExecutionLogfile, "   couldn't find world saves directory\n");
                // this message will get popped up by loadWorldList
                //FilterMessageBox(NULL, _T("Couldn't find your Minecraft world saves directory. You'll need to guide Mineways to where you save your worlds. Use the 'File -> Open...' option and find your level.dat file for the world. If you're on Windows, go to 'C:\\Users\\Eric\\AppData\\Roaming\\.minecraft\\saves' and find it in your world save directory. For Mac, worlds are usually located at /users/<your name>/Library/Application Support/minecraft/saves. Visit http://mineways.com or email me if you are still stuck."),
                //	_T("Warning"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
            }
        }

        // if not "none"
        if (val >= 0) {
            LOG_INFO(gExecutionLogfile, " loadWorldList\n");
            if (loadWorldList(GetMenu(hWnd)))
            {
                LOG_INFO(gExecutionLogfile, "   world not converted\n");
                int retcode = FilterMessageBox(NULL, _T("Warning:\nAt least one of your worlds has not been converted to the Anvil format. These worlds will be shown as disabled in the Open World menu.\n\nYou might be trying to read a world from Minecraft for Windows 10. Mineways cannot read this type of world, as it is in a different ('Bedrock') format. Go to https://bit.ly/mcbedrock and follow the instructions there to convert your world to the 'Classic' Java format, which Mineways can read. If instead this world is from an early version of Classic Minecraft, load it into the latest Minecraft to convert it. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information."),
                    _T("Warning"), MB_OKCANCEL | MB_ICONWARNING | MB_TOPMOST);
                GoToBedrockHelpOnOK(retcode);
            }
        }
        wcscpy_s(gWorldPathCurrent, MAX_PATH_AND_FILE, gWorldPathDefault);

        LOG_INFO(gExecutionLogfile, " loadTerrainList\n");
        loadTerrainList(GetMenu(hWnd));

        LOG_INFO(gExecutionLogfile, " populateColorSchemes\n");
        populateColorSchemes(GetMenu(hWnd));
        CheckMenuItem(GetMenu(hWnd), IDM_CUSTOMCOLOR, MF_CHECKED);

        ctlBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));

        ice.dwSize = sizeof(INITCOMMONCONTROLSEX);
        ice.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&ice);
        GetClientRect(hWnd, &rect);

        // upper slider itself
        hwndSlider = CreateWindowEx(
            0, TRACKBAR_CLASS, L"Trackbar Control",
            WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
            SLIDER_LEFT, 0, 
            rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30,  // initial width and height of trackbar - make it 383 wide, for the min slider
            hWnd, (HMENU)ID_LAYERSLIDER, NULL, NULL);
        SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELONG(0, gMaxHeight - gMinHeight));
        SendMessage(hwndSlider, TBM_SETPAGESIZE, 0, 10);
        EnableWindow(hwndSlider, FALSE);

        // upper slider number
        hwndLabel = CreateWindowEx(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | ES_RIGHT,
            rect.right - SLIDER_RIGHT, 5, SLIDER_RIGHT - 10, 20,
            hWnd, (HMENU)ID_LAYERLABEL, NULL, NULL);
        _itow_s(gMaxHeight, text, NUM_STR_SIZE);
        SetWindowText(hwndLabel, text);
        EnableWindow(hwndLabel, FALSE);

        // lower slider itself
        hwndBottomSlider = CreateWindowEx(
            0, TRACKBAR_CLASS, L"Trackbar Control",
            WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
            SLIDER_LEFT, 30, rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30,
            hWnd, (HMENU)ID_LAYERBOTTOMSLIDER, NULL, NULL);
        SendMessage(hwndBottomSlider, TBM_SETRANGE, TRUE, MAKELONG(0, gMaxHeight - gMinHeight));
        SendMessage(hwndBottomSlider, TBM_SETPAGESIZE, 0, 10);
        EnableWindow(hwndBottomSlider, FALSE);

        // lower slider number
        hwndBottomLabel = CreateWindowEx(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | ES_RIGHT,
            // location X, Y, width, height
            rect.right - SLIDER_RIGHT, 35, SLIDER_RIGHT - 10, 20,
            hWnd, (HMENU)ID_LAYERBOTTOMLABEL, NULL, NULL);
        _itow_s(gTargetDepth, text, NUM_STR_SIZE);
        SetWindowText(hwndBottomLabel, text);
        EnableWindow(hwndBottomLabel, FALSE);
        // need to set this one (the top slider doesn't need this call, as "all the way to the left" is the default)
        setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth + gMinHeight, true);

        // upper label to left: Height
        hwndInfoLabel = CreateWindowEx(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | ES_LEFT,
            5, 5, SLIDER_LEFT, 20,
            hWnd, (HMENU)ID_LAYERINFOLABEL, NULL, NULL);
        SetWindowText(hwndInfoLabel, L"Height");
        EnableWindow(hwndInfoLabel, FALSE);

        // lower label to left: Depth
        hwndInfoBottomLabel = CreateWindowEx(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | ES_LEFT,
            5, 35, SLIDER_LEFT, 20,
            hWnd, (HMENU)ID_LAYERINFOBOTTOMLABEL, NULL, NULL);
        SetWindowText(hwndInfoBottomLabel, L"Depth");
        EnableWindow(hwndInfoBottomLabel, FALSE);

        statusWindow = hwndStatus = CreateWindowEx(
            0, STATUSCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            -100, -100, 10, 10,
            hWnd, (HMENU)ID_STATUSBAR, NULL, NULL);
        {
            int parts[] = { 300, -1 };
            SendMessage(hwndStatus, SB_SETPARTS, 2, (LPARAM)parts);

            progressBar = CreateWindowEx(
                0, PROGRESS_CLASS, NULL,
                WS_CHILD | WS_VISIBLE,
                0, 0, 10, 10, hwndStatus, (HMENU)ID_PROGRESS, NULL, NULL);
            SendMessage(hwndStatus, SB_GETRECT, 1, (LPARAM)&rect);
            MoveWindow(progressBar, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
            SendMessage(progressBar, PBM_SETSTEP, (WPARAM)5, 0);
            SendMessage(progressBar, PBM_SETPOS, 0, 0);
        }

        gWS.hWnd = hWnd;
        gWS.hwndBottomSlider = hwndBottomSlider;
        gWS.hwndBottomLabel = hwndBottomLabel;
        gWS.hwndInfoBottomLabel = hwndInfoBottomLabel;
        gWS.hwndInfoLabel = hwndInfoLabel;
        gWS.hwndStatus = hwndStatus;
        gWS.hwndSlider = hwndSlider;
        gWS.hwndLabel = hwndLabel;

        rect.top += MAIN_WINDOW_TOP;	// add in two sliders, 30 each
        bitWidth = rect.right - rect.left;
        bitHeight = rect.bottom - rect.top;
        ZeroMemory(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bitWidth;
        bmi.bmiHeader.biHeight = -bitHeight; //flip
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        LOG_INFO(gExecutionLogfile, " CreateDIBSection\n");
        bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&map, NULL, 0);

        // set standard custom color at startup.
        LOG_INFO(gExecutionLogfile, " useCustomColor\n");
        useCustomColor(IDM_CUSTOMCOLOR, hWnd);

        // initialize zoom menu check
        wmId = LOWORD(wParam);
        CheckMenuItem(GetMenu(hWnd), IDM_ZOOMOUTFURTHER, (gMinZoom < MINZOOM) ? MF_CHECKED : MF_UNCHECKED);

        // finally, load any scripts on the command line.
        LOG_INFO(gExecutionLogfile, " processCreateArguments\n");
        processCreateArguments(gWS, &gBlockLabel, gHoldlParam, gArgList, gArgCount);

        //// tooltips for menu items
        //HWND tt = CreateToolTip(ID_FILE_IMPORTSETTINGS, hInst, GetMenu(hWnd), L"Show import settings");
        //if (tt != NULL) {
        //    SendMessage(tt, TTM_ACTIVATE, TRUE, 0);
        //}
    }
    break;
    case WM_LBUTTONDOWN:
        // For the Mac, an option:
        // if control key is held down, consider left-click to be a right-click,
        // i.e. for selection
        if (GetKeyState(VK_CONTROL) < 0)
        {
            gLeftIsRight = TRUE;
            goto RButtonDown;
        }
        // else remapped?
        switch (gRemapMouse[LEFT_MOUSE_BUTTON_INDEX]) {
        case MIDDLE_MOUSE_BUTTON_INDEX:
            goto MButtonDown;
        case RIGHT_MOUSE_BUTTON_INDEX:
            goto RButtonDown;
        }
    LButtonDown:
        dragging = TRUE;
        hdragging = FALSE;// just in case
        SetFocus(hWnd);
        SetCapture(hWnd);
        oldX = LOWORD(lParam);
        oldY = HIWORD(lParam);
        break;
    case WM_RBUTTONDOWN:
        switch (gRemapMouse[RIGHT_MOUSE_BUTTON_INDEX]) {
        case LEFT_MOUSE_BUTTON_INDEX:
            goto LButtonDown;
        case MIDDLE_MOUSE_BUTTON_INDEX:
            goto MButtonDown;
        }
    RButtonDown:
        gAdjustingSelection = 0;
        if (gLoaded)
        {
            int wasDragging = hdragging;
            hdragging = TRUE;
            dragging = FALSE;// just in case
            SetFocus(hWnd);
            SetCapture(hWnd);

            // get mouse position in world space
            (void)IDBlock(LOWORD(lParam), HIWORD(lParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
                bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
            gHoldlParam = lParam;

            gStartHiX = mx;
            gStartHiZ = mz;
            gHighlightOn = TRUE;

            // these track whether a selection height volume has blocks in it,
            // low, medium, high ("medium" means in selected range), and minimum low-height found
            gHitsFound[0] = gHitsFound[1] = gHitsFound[2] = 0;
            // no longer used - now we use a direct call for this very purpose
            gHitsFound[3] = INIT_MAP_MAX_HEIGHT + 1;

            // now to check the corners: is this location near any of them?
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);

            // if we weren't dragging before (making a selection), and there is
            // an active selection, see if we select on the selection border
            if (!wasDragging && on)
            {
                // highlighting is on, check the corners: inside bounds of current selection?
                if ((mx >= minx - SELECT_MARGIN / gCurScale) &&
                    (mx <= maxx + SELECT_MARGIN / gCurScale) &&
                    (mz >= minz - SELECT_MARGIN / gCurScale) &&
                    (mz <= maxz + SELECT_MARGIN / gCurScale))
                {
                    int startx, endx, startz, endz;
                    int innerx = (int)(SELECT_MARGIN / gCurScale);
                    int innerz = (int)(SELECT_MARGIN / gCurScale);
                    gAdjustingSelection = 1;

                    if (innerx > maxx - minx)
                    {
                        innerx = (maxx - minx - 1) / 2;
                    }
                    if (innerz > maxz - minz)
                    {
                        innerz = (maxz - minz - 1) / 2;
                    }

                    if (mx <= minx + innerx)
                    {
                        // in minx zone
                        startx = maxx;
                        endx = mx;
                    }
                    else if (mx >= maxx - innerx)
                    {
                        // in maxx zone
                        startx = minx;
                        endx = mx;
                    }
                    else
                    {
                        // in middle: lock x
                        gLockMouseX = 1;
                        startx = minx;
                        endx = maxx;
                    }

                    if (mz <= minz + innerz)
                    {
                        // in minz zone
                        startz = maxz;
                        endz = mz;
                    }
                    else if (mz >= maxz - innerz)
                    {
                        // in maxz zone
                        startz = minz;
                        endz = mz;
                    }
                    else
                    {
                        // in middle: lock z
                        gLockMouseZ = 1;
                        startz = minz;
                        endz = maxz;
                    }

                    // if in center zone, then it's just a regular mouse down
                    if (gLockMouseX && gLockMouseZ)
                    {
                        gLockMouseX = gLockMouseZ = 0;
                        gAdjustingSelection = 0;
                    }
                    else
                    {
                        // stick the rectangle to the mouse
                        gStartHiX = startx;
                        mx = endx;
                        gStartHiZ = startz;
                        mz = endz;
                    }
                    //#ifdef _DEBUG
                    //                    wchar_t bufa[100];
                    //                    swprintf( bufa, 100, L"startx %d, endx %d, startz %d, endz %d\n", startx, endx, startz, endz );
                    //                    OutputDebugStringW( bufa );
                    //#endif
                }
            }
#ifdef SKETCHFAB
            // Reset Sketchfab path so that we rewrite obj/mtl/PNG + zip when publishing
            deleteFile();
            gSkfbPData.skfbFilePath = "";
#endif
            SetHighlightState(gHighlightOn, gStartHiX, gTargetDepth, gStartHiZ, mx, gCurDepth, mz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
            enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
            validateItems(GetMenu(hWnd));
            drawInvalidateUpdate(hWnd);
        }
        break;
    case WM_MOUSEWHEEL:
        if (gLoaded)
        {
            // if shift is held down, change maximum height
            // if control is held down, change minimum height
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool useControl = (GetKeyState(VK_CONTROL) < 0);
            if (useControl) {
                gTargetDepth += (int)((double)zDelta / WHEEL_DELTA);
                gTargetDepth = clamp(gTargetDepth, gMinHeight, gMaxHeight);
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                enableBottomControl(on, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                REDRAW_ALL;
            }
            else {
                bool useShift = (GetKeyState(VK_SHIFT) < 0);
                if (useShift) {
                    gCurDepth += (int)((double)zDelta / WHEEL_DELTA);
                    gCurDepth = clamp(gCurDepth, gMinHeight, gMaxHeight);
                    SaveHighlightState();
                    validateItems(GetMenu(hWnd));
                    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                    REDRAW_ALL;
                }
                else {
                    // The usual case
                    // ratchet zoom up by 2x when zoom of 8 or higher is reached, so it zooms faster
                    if (gCurScale == 1.0f) {
                        // at 1.0, so can go up or down
                        if (zDelta < 0.0f) {
                            gCurScale -= 0.05f;
                        }
                        else {
                            gCurScale += ((double)zDelta / WHEEL_DELTA) * (pow(gCurScale, 1.2) / gCurScale);
                        }
                    }
                    else if (gCurScale < 1.0f) {
                        if (zDelta < 0.0f) {
                            gCurScale -= 0.05f;
                        }
                        else {
                            gCurScale += 0.05f;
                        }
                        // lock at max of 1.0f, so we don't slip by it, at least for one click
                        gCurScale = clamp(gCurScale, gMinZoom, 1.0f);
                    }
                    else {
                        gCurScale += ((double)zDelta / WHEEL_DELTA) * (pow(gCurScale, 1.2) / gCurScale);
                        // stop at 1.0f so that we don't overzoom low, at least for one click
                        gCurScale = clamp(gCurScale, 1.0f, MAXZOOM);
                    }
                    //char outstring[256];
                    //sprintf_s(outstring, 256, "zDelta is %d, zoom is now %f\n", zDelta, gCurScale);
                    //OutputDebugStringA(outstring);
                    gCurScale = clamp(gCurScale, gMinZoom, MAXZOOM);
                    formTitle(&gWorldGuide, hWnd);
                    drawInvalidateUpdate(hWnd);
                }
            }
        }
        break;
    case WM_LBUTTONUP:
        // if control key was held down on mouse down, consider left-click to be a right-click,
        // i.e. for selection
        if (gLeftIsRight)
        {
            // turn off latch
            gLeftIsRight = FALSE;
            goto RButtonUp;
        }
        switch (gRemapMouse[LEFT_MOUSE_BUTTON_INDEX]) {
        case MIDDLE_MOUSE_BUTTON_INDEX:
            goto MButtonUp;
        case RIGHT_MOUSE_BUTTON_INDEX:
            goto RButtonUp;
        }
     LButtonUp:
        dragging = FALSE;
        hdragging = FALSE;	// just in case
        ReleaseCapture();
        break;
    case WM_MBUTTONDOWN:
        switch (gRemapMouse[MIDDLE_MOUSE_BUTTON_INDEX]) {
        case LEFT_MOUSE_BUTTON_INDEX:
            goto LButtonDown;
        case RIGHT_MOUSE_BUTTON_INDEX:
            goto RButtonDown;
        }
    MButtonDown:
        // set new target depth
        hdragging = FALSE;
        dragging = FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        gBlockLabel = IDBlock(LOWORD(lParam), HIWORD(lParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
            bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
        gHoldlParam = lParam;
        if (my >= gMinHeight && my <= gMaxHeight)
        {
            // special test: if type is a flattop, then select the location one lower for export
            if ((gBlockDefinitions[type].flags & BLF_FLATTEN) && (my > 0))
            {
                my--;
            }
            gTargetDepth = my;
            gTargetDepth = clamp(gTargetDepth, gMinHeight, gMaxHeight);   // should never happen that a flattop is at 0, but just in case
            // also set highlight state to new depths
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
            SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
            // must call setSlider *after* SetHighlightState, otherwise the previous highlight won't get pushed and saved
            setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
            enableBottomControl(on, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);

            updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);

            validateItems(GetMenu(hWnd));
            drawInvalidateUpdate(hWnd);
        }
        break;
    case WM_RBUTTONUP:
        switch (gRemapMouse[RIGHT_MOUSE_BUTTON_INDEX]) {
        case LEFT_MOUSE_BUTTON_INDEX:
            goto LButtonUp;
        case MIDDLE_MOUSE_BUTTON_INDEX:
            goto MButtonUp;
        }
    RButtonUp:
        dragging = FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        ReleaseCapture();

        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);

        hdragging = FALSE;
        // Area selected.
        // Check if this selection is not an adjustment.
        // We test "on" here only because on a remap, the left mouse that selected the mwscript now might become a right mouse up.
        // This test isn't foolproof, but good enough for this obscure feature.
        if (!gAdjustingSelection && on)
        {
            // Not an adjustment, but a new selection. As such, test if there's something below to be selected and
            // there's nothing in the actual selection volume.
            int minHeightFound = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, gMinHeight, gMaxHeight, true, true, maxy);	// note we favor rendering here - for 3D printing the next to last option should be "false"
            if (gHitsFound[0] && !gHitsFound[1])
            {
                // make sure there's some depth to use to replace current target depth
                if (minHeightFound < gTargetDepth)	// was gHitsFound[3]
                {
                    // send warning, set to min height found, then redo!
                    if (gFullLow)
                    {
                        gFullLow = 0;
                        swprintf_s(msgString, 1024, L"All blocks in your selection are below the current depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there is a lower depth, displayed in the status bar at the bottom. You can adjust this depth by using the 'Depth' slider near the top or '[' & ']' keys.\n\nThe depth will be reset to %d to include all visible blocks.",
                            gTargetDepth, minHeightFound);
                    }
                    else
                    {
                        swprintf_s(msgString, 1024, L"All blocks in your selection are below the current depth of %d.\n\nThe depth will be reset to %d to include all visible blocks.",
                            gTargetDepth, minHeightFound);
                    }
                    FilterMessageBox(NULL, msgString,
                        _T("Informational"), MB_OK | MB_ICONINFORMATION);
                    //gTargetDepth = gHitsFound[3]; - no longer used.
                    gTargetDepth = minHeightFound;

                    // update target depth
                    SetHighlightState(gHighlightOn, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                    drawInvalidateUpdate(hWnd);
                }
                else
                {
                    // funny: target depth is lower than min height in lower section, which
                    // means lower section not really set. Don't do any warning, I guess.
                }
            }
            // Else, test if there's something in both volumes and offer to adjust.
            // Don't adjust if target depth is 0, at the bottom; that's the default for schematics,
            // for example, and people want to export the whole schematic.
            // If already set to 0, it was set that way for a good reason.
            else if (gAutocorrectDepth && (gTargetDepth > 0) &&
                ((gHitsFound[1] && (minHeightFound < gTargetDepth)) ||	// we used to also test "gHitsFound[0] &&" at the start, but if the water level just happens to match our current level, we then wouldn't go lower. Always allow lower.
                (!gHitsFound[0] && gHitsFound[1] && (minHeightFound > gTargetDepth))))
            {
                // send warning
                int retval;
                // send warning, set to min height found, then redo!
                if (minHeightFound < gTargetDepth)
                {
                    if (gFullLow)
                    {
                        gFullLow = 0;
                        swprintf_s(msgString, 1024, L"Some blocks in your selection are visible below the current depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there is a depth, shown on the 'Depth' slider near the top. You can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to select all visible blocks?\nSelect 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound);
                    }
                    else
                    {
                        swprintf_s(msgString, 1024, L"Some blocks in your selection are visible below the current depth of %d.\n\nDo you want to set the depth to %d to select all visible blocks? Select 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound);
                    }
                }
                else
                {
                    if (gFullLow)
                    {
                        gFullLow = 0;
                        swprintf_s(msgString, 1024, L"The current selection's depth of %d contains hidden lower layers.\n\nWhen you select, you're selecting in three dimensions, and there is a depth, shown on the 'Depth' slider near the top. You can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to minimize the underground? (\"Yes\" is probably what you want.) Select 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound);
                    }
                    else
                    {
                        swprintf_s(msgString, 1024, L"The current selection's depth of %d contains hidden lower layers.\n\nDo you want to set the depth to %d to minimize the underground? Select 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound);
                    }
                }
                // System modal puts it topmost, and task modal stops things from continuing without an answer. Unfortunately, task modal does not force the dialog on top.
                // We force it here, as it's OK if it gets ignored, but we want people to see it.
                retval = FilterMessageBox(NULL, msgString,
                    _T("Informational"), MB_YESNOCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_TOPMOST);
                if (retval == IDYES)
                {
                    gTargetDepth = minHeightFound;
                    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                    // update target depth
                    SetHighlightState(gHighlightOn, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                    drawInvalidateUpdate(hWnd);
                }
                else if (retval == IDCANCEL)
                {
                    gAutocorrectDepth = 0;
                }
            }
        }
        break;
    case WM_MBUTTONUP:
        switch (gRemapMouse[MIDDLE_MOUSE_BUTTON_INDEX]) {
        case LEFT_MOUSE_BUTTON_INDEX:
            goto LButtonUp;
        case RIGHT_MOUSE_BUTTON_INDEX:
            goto RButtonUp;
        }
    MButtonUp:
        break;
    case WM_MOUSEMOVE:
        if (gLoaded)
        {
            updateCursor(lParam, hdragging);
            // is left-mouse button down and we're changing the viewed area?
            if (dragging)
            {
                // mouse coordinate can now be negative
                int mouseX = LOWORD(lParam);
                if (mouseX > 0x7fff)
                    mouseX -= 0x10000;
                int mouseY = HIWORD(lParam);
                if (mouseY > 0x7fff)
                    mouseY -= 0x10000;
                gCurZ -= (mouseY - oldY) / gCurScale;
                gCurX -= (mouseX - oldX) / gCurScale;
                oldX = mouseX;
                oldY = mouseY;
                drawInvalidateUpdate(hWnd);
            }
            // for given mouse position and world center, determine
            // mx, mz, the world coordinates that the mouse is over,
            // and return the name of the block type it's over

            // mask off highest bit (the negative) for mouse location
            lParam &= 0x7fff7fff;

            gBlockLabel = IDBlock(LOWORD(lParam), HIWORD(lParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
                bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
            gHoldlParam = lParam;
            // is right mouse button down and we're dragging out a selection box?
            if (hdragging && gLoaded)
            {
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                // change map center, in world coordinates, by mouse move
                if (gLockMouseZ)
                {
                    gStartHiZ = minz;
                    mz = maxz;
                }
                if (gLockMouseX)
                {
                    gStartHiX = minx;
                    mx = maxx;
                }
                // update highlight end to this position
//#ifdef _DEBUG
//                wchar_t bufa[100];
//                swprintf( bufa, 100, L"selection box: x %d, y %d to x %d, y %d\n", gStartHiX, gStartHiZ, mx, mz );
//                OutputDebugStringW( bufa );
//#endif
                SetHighlightState(gHighlightOn, gStartHiX, gTargetDepth, gStartHiZ, mx, gCurDepth, mz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
                enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                drawInvalidateUpdate(hWnd);
            }
            updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
        }
        break;
    case WM_KEYDOWN:
        // Check if control key is not being held down. If so,
        // ignore this keypress and assume the .rc file has
        // emitted a corresponding message. Else control+S will
        // have the effect of saving and scrolling the map.
        if (gLoaded && GetKeyState(VK_CONTROL) >= 0)
        {
            //#ifdef _DEBUG
            //            wchar_t outputString[256];
            //            swprintf_s(outputString,256,L"key: %d\n",wParam);
            //            OutputDebugString( outputString );
            //#endif

            BOOL changed = FALSE;
            switch (wParam)
            {
            case VK_UP:
            case 'W':
                moving |= 1;
                break;
            case VK_DOWN:
            case 'S':
                moving |= 2;
                break;
            case VK_LEFT:
            case 'A':
                moving |= 4;
                break;
            case VK_RIGHT:
            case 'D':
                moving |= 8;
                break;
            case VK_PRIOR:
            case 'E':
            case VK_ADD:
            case VK_OEM_PLUS:
                gCurScale += 0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                if (gCurScale > MAXZOOM)
                    gCurScale = MAXZOOM;
                formTitle(&gWorldGuide, hWnd);
                changed = TRUE;
                break;
            case VK_NEXT:
            case 'Q':
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                gCurScale -= 0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                // we go down to original minzoom, as going to true minzoom could just be a crash.
                if (gCurScale < MINZOOM)
                    gCurScale = MINZOOM;
                formTitle(&gWorldGuide, hWnd);
                changed = TRUE;
                break;
                // increment target depth by one
            case VK_OEM_4:    // [
                gTargetDepth++;
                gTargetDepth = clamp(gTargetDepth, gMinHeight, gMaxHeight);
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                enableBottomControl(on, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                REDRAW_ALL;
                break;
                // decrement target depth by one
            case VK_OEM_6:    // ]
                gTargetDepth--;
                gTargetDepth = clamp(gTargetDepth, gMinHeight, gMaxHeight);
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                enableBottomControl(on, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                REDRAW_ALL;
                break;
            case VK_OEM_PERIOD:
            case '.':
            case '>':
                if (gCurDepth > gMinHeight)
                {
                    gCurDepth--;
                    SaveHighlightState();
                    validateItems(GetMenu(hWnd));
                    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                }
                REDRAW_ALL;
                break;
            case VK_OEM_COMMA:
            case ',':
            case '<':
                if (gCurDepth < gMaxHeight)
                {
                    gCurDepth++;
                    SaveHighlightState();
                    validateItems(GetMenu(hWnd));
                    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                }
                REDRAW_ALL;
                break;
            case '0':
                gCurDepth = gMinHeight;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '1':
                gCurDepth = 10;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '2':
                gCurDepth = 20;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '3':
                gCurDepth = 30;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '4':
                gCurDepth = 40;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '5':
                gCurDepth = 51;	// bottom dirt layer of deep lakes
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '6':
                gCurDepth = SEA_LEVEL;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '7':
                gCurDepth = 85;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '8':
                gCurDepth = 106;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case '9':
                gCurDepth = gMaxHeight;
                SaveHighlightState();
                validateItems(GetMenu(hWnd));
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case VK_HOME:
                gCurScale = MAXZOOM;
                formTitle(&gWorldGuide, hWnd);
                changed = TRUE;
                break;
            case VK_END:
                // no, don't go to gMinZoom - it could kinda lock up the program
                gCurScale = MINZOOM;
                formTitle(&gWorldGuide, hWnd);
                changed = TRUE;
                break;
            case VK_ESCAPE:
                // deselect - remove selection
                gHighlightOn = FALSE;
                SetHighlightState(gHighlightOn, 0, gTargetDepth, 0, 0, gCurDepth, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                validateItems(GetMenu(hWnd));
                changed = TRUE;
                break;
            case VK_SPACE:
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                if (on) {
                    bool useOnlyOpaque = !(GetKeyState(VK_SHIFT) < 0);
                    SaveHighlightState();
                    validateItems(GetMenu(hWnd));
                    gTargetDepth = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, gMinHeight, gMaxHeight, true, useOnlyOpaque, maxy);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    REDRAW_ALL;
                }
                break;
            default:
                // unknown key, don't move
                moving = 0;
                break;
            }

            if (moving != 0)
            {
                if (moving & 1) //up
                    gCurZ -= 10.0 / gCurScale;
                if (moving & 2) //down
                    gCurZ += 10.0 / gCurScale;
                if (moving & 4) //left
                    gCurX -= 10.0 / gCurScale;
                if (moving & 8) //right
                    gCurX += 10.0 / gCurScale;
                changed = TRUE;
            }
            if (changed)
            {
                REDRAW_ALL;
            }
        }
        break;
    case WM_KEYUP:
        switch (wParam)
        {
        case VK_UP:
        case 'W':
            moving &= ~1;
            break;
        case VK_DOWN:
        case 'S':
            moving &= ~2;
            break;
        case VK_LEFT:
        case 'A':
            moving &= ~4;
            break;
        case VK_RIGHT:
        case 'D':
            moving &= ~8;
            break;
        }
        break;
    case WM_HSCROLL:
        pos = (DWORD)SendMessage(hwndSlider, TBM_GETPOS, 0, 0);
        gCurDepth = gMaxHeight - pos;
        _itow_s(gCurDepth, text, NUM_STR_SIZE);
        SetWindowText(hwndLabel, text);

        pos = (DWORD)SendMessage(hwndBottomSlider, TBM_GETPOS, 0, 0);
        gTargetDepth = gMaxHeight - pos;
        _itow_s(gTargetDepth, text, NUM_STR_SIZE);
        SetWindowText(hwndBottomLabel, text);

        syncCurrentHighlightDepth();

        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
        SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
        enableBottomControl(on, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
        gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(gHoldlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
            bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
        updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
        SetFocus(hWnd);
        drawInvalidateUpdate(hWnd);
        break;
    case WM_CTLCOLORSTATIC: //color the label and the slider background
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdcStatic, GetSysColor(COLOR_WINDOW));
        return (INT_PTR)ctlBrush;
    }
    break;
    case WM_COMMAND:
        wmId = LOWORD(wParam);
        // set but not used: wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        if (wmId >= IDM_CUSTOMCOLOR && wmId < IDM_CUSTOMCOLOR + MAX_WORLDS)
        {
            useCustomColor(wmId, hWnd);
        }
        else if (wmId > IDM_WORLD && wmId < IDM_WORLD + MAX_WORLDS)
        {
            // Load world from list that's real (not Block Test World, which is IDM_TEST_WORLD, below)

            int loadErr;
            //convert path to utf8
            //WideCharToMultiByte(CP_UTF8,0,worlds[wmId-IDM_WORLD],-1,gWorldGuide.world,MAX_PATH_AND_FILE,NULL,NULL);
            gSameWorld = (wcscmp(gWorldGuide.world, gWorlds[wmId - IDM_WORLD]) == 0);
            wcscpy_s(gWorldGuide.world, MAX_PATH_AND_FILE, gWorlds[wmId - IDM_WORLD]);
            // if this is not the same world, switch back to the aboveground view.
            // TODO: this code is repeated, should really be a subroutine.
            if (!gSameWorld)
            {
                gotoSurface(hWnd, hwndSlider, hwndLabel);
            }
            gWorldGuide.type = WORLD_LEVEL_TYPE;
            loadErr = loadWorld(hWnd);
            if (loadErr)
            {
                showLoadWorldError(loadErr);
                return 0;
            }
            setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
        }
        else if (wmId >= IDM_DEFAULT_TERRAIN && wmId < IDM_DEFAULT_TERRAIN + MAX_TERRAIN_FILES - 1)
        {
            // Load terrain file from list that's real (not the default, which is IDM_DEFAULT_TERRAIN, below)
            wchar_t terrainLoc[MAX_PATH_AND_FILE];
            wcscpy_s(terrainLoc, MAX_PATH_AND_FILE, gExeDirectory);
            wcscat_s(terrainLoc, MAX_PATH_AND_FILE - wcslen(terrainLoc), gPreferredSeparatorString);
            if (gTerrainFiles[wmId - IDM_DEFAULT_TERRAIN] == NULL) {
                wcscat_s(terrainLoc, MAX_PATH_AND_FILE - wcslen(terrainLoc), L"terrainExt.png");
            }
            else {
                wcscat_s(terrainLoc, MAX_PATH_AND_FILE - wcslen(terrainLoc), gTerrainFiles[wmId - IDM_DEFAULT_TERRAIN]);
            }
            // copy file name, since it definitely appears to exist.
            rationalizeFilePath(terrainLoc);
            wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, terrainLoc);
            splitToPathAndName(gSelectTerrainPathAndName, gSelectTerrainDir, NULL);
            formTitle(&gWorldGuide, hWnd);
            // someday might show a check mark by selected (and have to add code to unselect all the rest). We already do show it at the top of the program
            // TODO - it's a little tricky, as the terrain file could be chosen manually - how would that affect this menus?
            //CheckMenuItem(GetMenu(hWnd), wmId, MF_CHECKED);
        }
        else {

            // switch on chosen menu item
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_HELP_URL:
                ShellExecute(NULL, L"open", L"http://mineways.com/reference.html", NULL, NULL, SW_SHOWNORMAL);
                break;
            case ID_HELP_TROUBLESHOOTING:
                ShellExecute(NULL, L"open", L"http://mineways.com/downloads.html#windowsPlatformHelp", NULL, NULL, SW_SHOWNORMAL);
                break;
            case ID_HELP_DOCUMENTATION:
                ShellExecute(NULL, L"open", L"http://mineways.com/mineways.html", NULL, NULL, SW_SHOWNORMAL);
                break;
            case ID_HELP_REPORTABUG:
                ShellExecute(NULL, L"open", L"http://mineways.com/contact.html", NULL, NULL, SW_SHOWNORMAL);
                break;
            case ID_FILE_DOWNLOADTERRAINFILES:
                ShellExecute(NULL, L"open", L"http://mineways.com/textures.html#dl", NULL, NULL, SW_SHOWNORMAL);
                break;
            case IDM_FOCUSVIEW:
                setLocationData((int)gCurX, (int)gCurZ);
                if (doLocation(hInst, hWnd))
                {
                    // successful, so change location
                    int x, z;
                    getLocationData(x, z);
                    gCurX = (double)x;
                    gCurZ = (double)z;
                    REDRAW_ALL;
                }

                //DialogBox(hInst, MAKEINTRESOURCE(IDD_FOCUS_VIEW), hWnd, About);
                break;
            case IDM_VIEW_INFORMATION:
                // show information about world:
                // name, directory name, major & minor version, spawn loc, player loc
            {
                TCHAR infoString[1024];
                switch (gWorldGuide.type) {
                default:
                    // should never reach here, as this control should be off if there's no world loaded
                    assert(0);
                case WORLD_TEST_BLOCK_TYPE:
                    wsprintf(infoString, L"World is the built-in [Block Test World]");
                    break;
                case WORLD_LEVEL_TYPE:
                    // TODO: add this table to convert to strings https://minecraft.fandom.com/wiki/Data_version
                    char levelName[MAX_PATH_AND_FILE];
                    // the only place we ever need the level name, other than populating the world list
                    GetLevelName(gWorldGuide.world, levelName, MAX_PATH_AND_FILE);
                    wsprintf(infoString, L"Level name: %S\nDirectory name: %s\n\nMajor version: 1.%d%s\nData version: %d\nSee http://bit.ly/minedv to convert this\ndata version value more precisely.\n\nSpawn location: %d, %d, %d\n",
                        levelName, stripWorldName(gWorldGuide.world),
                        gMinecraftVersion,
                        (gMinecraftVersion == 8 ? L" or earlier" : L""),
                        gVersionID,
                        // gWorldGuide.nbtVersion, - always the same 19333 or whatever
                        gSpawnX, gSpawnY, gSpawnZ
                    );
                    if (gWorldGuide.isServerWorld) {
                        wcscat_s(infoString, 1024, L"This is a server world; no player location");
                    }
                    else {
                        TCHAR playerString[1024];
                        wsprintf(playerString, L"Player location : %d, %d, %d",
                            gPlayerX, gPlayerY, gPlayerZ
                        );
                        wcscat_s(infoString, 1024, playerString);
                    }
                    break;
                case WORLD_SCHEMATIC_TYPE:
                    wsprintf(infoString, L"Schematic name: %s\n\nWidth (X - east/west): %d\nHeight (Y - vertical): %d\nLength (Z - north/south): %d",
                        stripWorldName(gWorldGuide.world),
                        gWorldGuide.sch.width, gWorldGuide.sch.height, gWorldGuide.sch.length
                    );
                    break;
                }

                FilterMessageBox(
                    NULL,
                    infoString,
                    _T("World information"),
                    MB_OK | MB_ICONINFORMATION
                );
            }
            break;
            case ID_SELECT_ALL:
                if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE) {
                    //gTargetDepth = gMinHeight;
                    //gCurDepth = gWorldGuide.sch.height - 1;

                    // update target depth
                    gHighlightOn = TRUE;
                    SetHighlightState(gHighlightOn, 0, 0, 0, gWorldGuide.sch.width - 1, gWorldGuide.sch.height - 1, gWorldGuide.sch.length - 1, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                    drawInvalidateUpdate(hWnd);
                }
                else {
                    // select visible area, more or less
                    //gTargetDepth = gMinHeight;
                    //gCurDepth = gMaxHeight;

                    // needed for maxy
                    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                    // get screen coordinates, roughly
                    {
                        // gCurX/gCurZ is the center, so find the corners from that
                        minx = (int)(gCurX - (double)bitWidth / (2 * gCurScale));
                        minz = (int)(gCurZ - (double)bitHeight / (2 * gCurScale));
                        maxx = (int)(gCurX + (double)bitWidth / (2 * gCurScale));
                        maxz = (int)(gCurZ + (double)bitHeight / (2 * gCurScale));
                    }

                    // update target depth
                    gHighlightOn = TRUE;
                    gTargetDepth = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, gMinHeight, gMaxHeight, true, true, maxy);
                    SetHighlightState(gHighlightOn, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                    drawInvalidateUpdate(hWnd);
                    //REDRAW_ALL;
                }
                break;
            case IDM_COLOR:
            {
                doColorSchemes(hInst, hWnd);
                populateColorSchemes(GetMenu(hWnd));
                // always go back to the standard color scheme after editing, as editing
                // could have removed the custom scheme being modified.
                wchar_t* schemeSelected = getSelectedColorScheme();
                int item = findColorScheme(schemeSelected);
                if (item > 0)
                {
                    // use item selected in color scheme's create/delete/edit dialog, if found
                    useCustomColor(IDM_CUSTOMCOLOR + item, hWnd);
                }
                else {
                    // nothing selected in color scheme edit system, so set previously-used scheme, if available
                    item = findColorScheme(gSchemeSelected);
                    if (item > 0)
                    {
                        //
                        useCustomColor(IDM_CUSTOMCOLOR + item, hWnd);
                    }
                    else
                    {
                        // no user-defined scheme found, must be Standard
                        useCustomColor(IDM_CUSTOMCOLOR, hWnd);
                    }
                }
            }
            break;
            case IDM_CLOSE:
#ifdef SKETCHFAB
                deleteFile();
#endif
                DestroyWindow(hWnd);
                break;
            case IDM_TEST_WORLD:
                gWorldGuide.world[0] = 0;
                gSameWorld = FALSE;
#ifdef SKETCHFAB
                sprintf_s(gSkfbPData.skfbName, "TestWorld");
#endif
                gotoSurface(hWnd, hwndSlider, hwndLabel);
                gWorldGuide.type = WORLD_TEST_BLOCK_TYPE;
                loadWorld(hWnd);
                setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
                break;
            case IDM_WORLD:
            case IDM_OPEN:
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFile = pathAndFile;
                ofn.lpstrFile[0] = (wchar_t)0;
                ofn.nMaxFile = sizeof(pathAndFile);
                ofn.lpstrFilter = L"World (level.dat) or Schematic\0level.dat;*.schematic\0Minecraft World (level.dat)\0level.dat\0Minecraft Schematic (*.schematic)\0*.schematic\0";
                //ofn.lpstrFilter = L"World file (level.dat)\0level.dat\0";
                ofn.nFilterIndex = gOpenFilterIndex;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                wcscpy_s(path, MAX_PATH_AND_FILE, gWorldPathCurrent);
                ofn.lpstrInitialDir = path;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                ofn.lpstrTitle = L"Open World level.dat or Schematic";

                if (GetOpenFileName(&ofn) == TRUE)
                {
                    // NOTE: if this code changes, also change that for WM_DROPFILES, which (lazily) is a copy of this code
                    int retCode = loadWorldFromFilename(pathAndFile, hWnd);
                    // load worked?
                    if (retCode) {
                        if (retCode == 2) {
                            gotoSurface(hWnd, hwndSlider, hwndLabel);
                        }
                        setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
                    }
                }
                gOpenFilterIndex = ofn.nFilterIndex;
                break;
            case IDM_FILE_SELECTTERRAIN:
                ZeroMemory(&ofn, sizeof(OPENFILENAME));
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hWnd;
                wcscpy_s(pathAndFile, MAX_PATH_AND_FILE, gSelectTerrainPathAndName);
                ofn.lpstrFile = pathAndFile;
                //path[0]=0;
                ofn.nMaxFile = MAX_PATH_AND_FILE;
                ofn.lpstrFilter = L"Terrain File (terrainExt*.png)\0*.png\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                wcscpy_s(path, MAX_PATH_AND_FILE, gSelectTerrainDir);
                ofn.lpstrInitialDir = path;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                ofn.lpstrTitle = L"Open Terrain File";
                if (GetOpenFileName(&ofn) == TRUE)
                {
                    // copy file name, since it definitely appears to exist.
                    // NOTE: if this code changes, also change that for WM_DROPFILES, which (lazily) is a copy of this code
                    rationalizeFilePath(pathAndFile);
                    wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, pathAndFile);
                    splitToPathAndName(gSelectTerrainPathAndName, gSelectTerrainDir, NULL);
                    formTitle(&gWorldGuide, hWnd);
                }
                break;
            case ID_FILE_IMPORTSETTINGS:
                ZeroMemory(&ofn, sizeof(OPENFILENAME));
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hWnd;
                wcscpy_s(pathAndFile, MAX_PATH_AND_FILE, gImportFile);
                ofn.lpstrFile = pathAndFile;
                ofn.nMaxFile = MAX_PATH_AND_FILE;
                ofn.lpstrFilter = L"All files (*.obj;*.usda;*.txt;*.wrl;*.mwscript)\0*.obj;*.usda;*.txt;*.wrl;*.mwscript\0Wavefront OBJ (*.obj)\0*.obj\0Universal Scene Description (*.usda)\0*.usda\0Summary STL text file (*.txt)\0*.txt\0VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0Mineways script file (*.mwscript)\0*.mwscript\0";
                ofn.nFilterIndex = gImportFilterIndex;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                wcscpy_s(path, MAX_PATH_AND_FILE, gImportPath);
                ofn.lpstrInitialDir = path;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                ofn.lpstrTitle = L"Import Settings from model or script file";
                if (GetOpenFileName(&ofn) == TRUE)
                {
                    // copy file name, since it definitely appears to exist.
                    // NOTE: if this code changes, also change that for WM_DROPFILES, which (lazily) is a copy of this code
                    rationalizeFilePath(pathAndFile);
                    wcscpy_s(gImportFile, MAX_PATH_AND_FILE, pathAndFile);
                    splitToPathAndName(gImportFile, gImportPath, NULL);
                    runImportOrScript(gImportFile, gWS, &gBlockLabel, gHoldlParam, true);
                }
                gImportFilterIndex = ofn.nFilterIndex;
                break;
            case IDM_FILE_PRINTOBJ:
            case IDM_FILE_SAVEOBJ:
            case IDM_FILE_SCHEMATIC:
            case IDM_FILE_EXPORTMAP:
                if (!gHighlightOn)
                {
                    // we keep the export options ungrayed now so that they're selectable when the world is loaded
                    FilterMessageBox(NULL, _T("Click and drag with your right mouse button to select an area to export."),
                        _T("Informational"), MB_OK | MB_ICONINFORMATION);
                    break;
                }
                switch (wmId)
                {
                case IDM_FILE_SAVEOBJ:
                    gPrintModel = RENDERING_EXPORT;
                    break;
                case IDM_FILE_PRINTOBJ:
                    gPrintModel = PRINTING_EXPORT;
                    break;
                case IDM_FILE_SCHEMATIC:
                    gPrintModel = SCHEMATIC_EXPORT;
                    break;
                case IDM_FILE_EXPORTMAP:
                    gPrintModel = MAP_EXPORT;
                    break;
                default:
                    MY_ASSERT(gAlwaysFail);
                    gPrintModel = RENDERING_EXPORT;
                }
                if (gPrintModel == SCHEMATIC_EXPORT)
                {
                    // schematic
                    ZeroMemory(&ofn, sizeof(OPENFILENAME));
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = gExportPath;
                    ofn.nMaxFile = MAX_PATH_AND_FILE;
                    ofn.lpstrFilter = L"Schematic file (*.schematic)\0*.schematic\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    wcscpy_s(path, MAX_PATH_AND_FILE, gImportPath);
                    ofn.lpstrInitialDir = path;
                    ofn.lpstrTitle = L"Export Schematic";
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    saveOK = GetSaveFileName(&ofn);

                    gExportSchematicData.fileType = FILE_TYPE_SCHEMATIC;	// always
                }
                else if (gPrintModel == MAP_EXPORT)
                {
                    // export map
                    ZeroMemory(&ofn, sizeof(OPENFILENAME));
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = gExportPath;
                    ofn.nMaxFile = MAX_PATH_AND_FILE;
                    ofn.lpstrFilter = L"Portable Network Graphics (*.png)\0*.png\0";
                    ofn.nFilterIndex = 0;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    wcscpy_s(path, MAX_PATH_AND_FILE, gImportPath);
                    ofn.lpstrInitialDir = path;
                    ofn.lpstrTitle = L"Export Map";
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    saveOK = GetSaveFileName(&ofn);
                }
                else
                {
                    // print model or render model - quite similar
                    ZeroMemory(&ofn, sizeof(OPENFILENAME));
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = gExportPath;
                    ofn.nMaxFile = MAX_PATH_AND_FILE;
                    ofn.lpstrFilter = gPrintModel ? L"Sculpteo: Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0Universal Scene Description (*.usda)\0*.usda\0i.materialise: Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0Shapeways: VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0" :
                        L"Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0Universal Scene Description (*.usda)\0*.usda\0Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0";
                    ofn.nFilterIndex = (gPrintModel ? gExportPrintData.fileType + 1 : gExportViewData.fileType + 1);
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    wcscpy_s(path, MAX_PATH_AND_FILE, gImportPath);
                    ofn.lpstrInitialDir = path;
                    ofn.lpstrTitle = gPrintModel ? L"Export for 3D Printing" : L"Export for Rendering";
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    saveOK = GetSaveFileName(&ofn);
                    // save file type selected, no matter what (even on cancel); we
                    // always set it because even if someone cancels a save, he probably still
                    // wanted the file type chosen.
                    if (gPrintModel)
                    {
                        gExportPrintData.fileType = ofn.nFilterIndex - 1;
                    }
                    else
                    {
                        gExportViewData.fileType = ofn.nFilterIndex - 1;
                    }
                }
                if (saveOK)
                {
                    // if we got this far, then previous export is off, and we also want to ask for export dialog itself.
                    gExported = 0;
                    // TODO: this whole export file name and path stuff could use some work.
                    wcscpy_s(gImportPath, MAX_PATH_AND_FILE, gExportPath);

                    // Yes, have the code drop through at this point, we're all set up to export, so it's like a "repeat"

                case IDM_FILE_REPEATPREVIOUSEXPORT:
                    if (gPrintModel == MAP_EXPORT) {
                        // export 2D map image
                        GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal, gMinHeight);
                        gExported = saveMapFile(gpEFD->minxVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gExportPath);
                    }
                    else {
                        gExported = saveObjFile(hWnd, gExportPath, gPrintModel, gSelectTerrainPathAndName, gSchemeSelected, (gExported == 0), gShowPrintStats);
                    }
                    SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
                    enableBottomControl(1, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);
                    // put target depth to new depth set, if any
                    if (gTargetDepth != gpEFD->maxyVal)
                    {
                        gTargetDepth = gpEFD->minyVal;
                    }
                    gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(gHoldlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
                        bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
                    updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
                    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true);
                }   // matches to "if ( saveOK )"
                break;

            case IDM_PUBLISH_SKFB:
#ifdef SKETCHFAB
                if (!gHighlightOn)
                {
                    // we keep the export options ungrayed now so that they're selectable when the world is loaded
                    FilterMessageBox(NULL, _T("Click and drag with your right mouse button to select an area to export."),
                        _T("Informational"), MB_OK | MB_ICONINFORMATION);
                    break;
                }
                {
                    // Force it to be an rendering export: Relative obj
                    LPTSTR filepath = prepareSketchfabExportFile(hWnd);
                    publishToSketchfab(hWnd, filepath, gSelectTerrainPathAndName, gSchemeSelected);
                }
#else
                FilterMessageBox(NULL, _T("This version of Mineways does not have Sketchfab export enabled - sorry! Try version 5.10."),
                    _T("Informational"), MB_OK | MB_ICONINFORMATION);

#endif
                break;
            case ID_VIEW_UNDOSELECTION:
                UndoHighlight();
                // copy new state to the local variables used here (ugh, that we have so many copies...)
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                gHighlightOn = on;
                gTargetDepth = miny;
                gCurDepth = maxy;
                setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true);
                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                REDRAW_ALL;
                break;
            case IDM_JUMPSPAWN:
                gCurX = gSpawnX;
                gCurZ = gSpawnZ;
                if (gOptions.worldType & HELL)
                {
                    gCurX /= 8.0;
                    gCurZ /= 8.0;
                }
                REDRAW_ALL;
                break;
            case IDM_JUMPPLAYER:
                gCurX = gPlayerX;
                gCurZ = gPlayerZ;
                if (gOptions.worldType & HELL)
                {
                    gCurX /= 8.0;
                    gCurZ /= 8.0;
                }
                REDRAW_ALL;
                break;
            case IDM_VIEW_JUMPTOMODEL: // F4
                if (!gHighlightOn)
                {
                    // we keep the jump option ungrayed now so that it's selectable when the world is loaded
                    FilterMessageBox(NULL, _T("No model selected. To select a model, click and drag with the right mouse button."),
                        _T("Informational"), MB_OK | MB_ICONINFORMATION);
                    break;
                }
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                if (on)
                {
                    gCurX = (minx + maxx) / 2;
                    gCurZ = (minz + maxz) / 2;
                    if (gOptions.worldType & HELL)
                    {
                        gCurX /= 8.0;
                        gCurZ /= 8.0;
                    }
                    REDRAW_ALL;
                }
                break;
            case IDM_SHOWALLOBJECTS:
                // TODO: super-minor bug: if mouse does not move when you turn all objects on, the
                // status bar will continue to list the object there before toggling, e.g. if a wall
                // sign was shown and you toggle Show All Objects off, the wall sign status will still be there.
                gOptions.worldType ^= SHOWALL;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & SHOWALL) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_VIEW_SHOWBIOMES:
                // toggles bit from its previous state
                gOptions.worldType ^= BIOMES;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & BIOMES) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_DEPTH:
                gOptions.worldType ^= DEPTHSHADING;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & DEPTHSHADING) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_LIGHTING:
                gOptions.worldType ^= LIGHTING;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & LIGHTING) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_CAVEMODE:
                gOptions.worldType ^= CAVEMODE;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & CAVEMODE) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_OBSCURED:
                gOptions.worldType ^= HIDEOBSCURED;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_TRANSPARENT_WATER:
                gOptions.worldType ^= TRANSPARENT_WATER;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & TRANSPARENT_WATER) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_MAPGRID:
                gOptions.worldType ^= MAP_GRID;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType & MAP_GRID) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_ZOOMOUTFURTHER:
                gMinZoom = (gMinZoom < MINZOOM) ? MINZOOM : 0.0625f;
                if (gMinZoom < MINZOOM) {
                    FilterMessageBox(NULL, _T("Warning: you can now zoom out further with the mouse scroll wheel, up to 16 blocks wide per pixel. You can see your zoom factor displayed at the top of the program, in the title bar. However, this feature uses up lots of memory as you zoom out, so may cause Mineways to slow or lock up. Massive map display is not what Mineways is made for. You're on your own!"),
                        _T("Warning"), MB_OK | MB_ICONWARNING);
                }
                CheckMenuItem(GetMenu(hWnd), wmId, (gMinZoom < MINZOOM) ? MF_CHECKED : MF_UNCHECKED);
                REDRAW_ALL;
                break;
            case IDM_RELOAD_WORLD:
                // reload world, if loaded
                if (gLoaded)
                {
                    if (loadWorld(hWnd))
                    {
                        // world not loaded properly
                        FilterMessageBox(NULL, _T("Error: cannot reload world."),
                            _T("Read error"), MB_OK | MB_ICONERROR);

                        return 0;
                    }
                    // TODO: reload world list, too, so that new worlds added since now appear.
                    // Ignore any errors (non-Anvil), since these were already reported.
                    // TODO: I couldn't figure out how to remove the existing world list,
                    // so this call just adds more and more copies of the original world list.
                    //loadWorldList(GetMenu(hWnd));
                    setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
                }
                else {
                    FilterMessageBox(NULL, _T("You need to load a world first. Use 'Open World' or 'Open...' and find your level.dat file in %appdata%/.minecraft/saves."), _T("Informational"), MB_OK | MB_ICONINFORMATION);
                }
                break;
            case IDM_HELL:
                if (gWorldGuide.type == WORLD_LEVEL_TYPE) {
                    if (!(gOptions.worldType & HELL))
                    {
                        CheckMenuItem(GetMenu(hWnd), IDM_HELL, MF_CHECKED);
                        gOptions.worldType |= HELL;
                        CheckMenuItem(GetMenu(hWnd), IDM_END, MF_UNCHECKED);
                        gOptions.worldType &= ~ENDER;
                        // change scale as needed
                        gCurX /= 8.0;
                        gCurZ /= 8.0;
                        // it's useless to view Nether from MAP_MAX_HEIGHT
                        if (gCurDepth == gMaxHeight)
                        {
                            gCurDepth = 126;
                            setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                        }
                        gOverworldHideStatus = gOptions.worldType & HIDEOBSCURED;
                        gOptions.worldType |= HIDEOBSCURED;
                        //gTargetDepth=0;
                        // semi-useful, I'm not sure: zoom in when going to nether
                        //gCurScale *= 8.0;
                        //gCurScale = clamp(gCurScale,gMinZoom,MAXZOOM);
                        //formTitle(&gWorldGuide, hWnd);
                    }
                    else
                    {
                        // back to the overworld
                        gotoSurface(hWnd, hwndSlider, hwndLabel);
                    }
                    CheckMenuItem(GetMenu(hWnd), IDM_OBSCURED, (gOptions.worldType & HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
                    CloseAll();
                    // clear selection when you switch from somewhere else to The Nether, or vice versa
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);

                    REDRAW_ALL;
                }

                break;
            case IDM_END:
                if (gWorldGuide.type == WORLD_LEVEL_TYPE) {
                    if (!(gOptions.worldType & ENDER))
                    {
                        // entering Ender, turn off hell if need be
                        CheckMenuItem(GetMenu(hWnd), IDM_END, MF_CHECKED);
                        gOptions.worldType |= ENDER;
                        if (gOptions.worldType & HELL)
                        {
                            // get out of hell zoom
                            gCurX *= 8.0;
                            gCurZ *= 8.0;
                            // and undo other hell stuff
                            if (gCurDepth == 126)
                            {
                                gCurDepth = gMaxHeight;
                                setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                            }
                            // turn off obscured, then restore overworld's obscured status
                            gOptions.worldType &= ~HIDEOBSCURED;
                            CheckMenuItem(GetMenu(hWnd), IDM_OBSCURED, MF_UNCHECKED);
                            gOptions.worldType |= gOverworldHideStatus;
                            // uncheck hell menu item
                            CheckMenuItem(GetMenu(hWnd), IDM_HELL, MF_UNCHECKED);
                            gOptions.worldType &= ~HELL;
                        }
                    }
                    else
                    {
                        // exiting Ender, go back to overworld
                        gotoSurface(hWnd, hwndSlider, hwndLabel);
                    }
                    CloseAll();
                    // clear selection when you switch from somewhere else to The End, or vice versa
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
                    enableBottomControl(gHighlightOn, /* hwndBottomSlider, hwndBottomLabel, */ hwndInfoBottomLabel);

                    REDRAW_ALL;
                }
                break;
            case IDM_HELP_GIVEMEMOREMEMORY:
                // This option can help give you more memory during export.
                // It clears and reloads the world during this process and reallocs chunks to be smaller, the freed memory can then be used for
                // other export processing functions.
                gOptions.moreExportMemory = !gOptions.moreExportMemory;
                CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.moreExportMemory) ? MF_CHECKED : MF_UNCHECKED);
                MinimizeCacheBlocks(gOptions.moreExportMemory);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        validateItems(GetMenu(hWnd));
        break;
    case WM_ERASEBKGND:
    {
        hdc = (HDC)wParam;
        GetClipBox(hdc, &rect);
        rect.bottom = MAIN_WINDOW_TOP;
        HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(hdc, &rect, hb);
        DeleteObject(hb);
    }
    break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        GetClientRect(hWnd, &rect);
        rect.top += MAIN_WINDOW_TOP;
        if (hdcMem == NULL)
        {
            hdcMem = CreateCompatibleDC(hdc);
            SelectObject(hdcMem, bitmap);
        }
        BitBlt(hdc, 0, MAIN_WINDOW_TOP, bitWidth, bitHeight, hdcMem, 0, 0, SRCCOPY);
        EndPaint(hWnd, &ps);
        break;
    case WM_SIZING: //window resizing
        GetClientRect(hWnd, &rect);
        SetWindowPos(hwndSlider, NULL, 0, 0,
            rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndBottomSlider, NULL, 0, 30,
            rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndLabel, NULL, rect.right - SLIDER_RIGHT, 5,
            SLIDER_RIGHT - 10, 20, SWP_NOACTIVATE);
        SetWindowPos(hwndBottomLabel, NULL, rect.right - SLIDER_RIGHT, 35,
            SLIDER_RIGHT - 10, 20, SWP_NOACTIVATE);

        break;
    case WM_SIZE: //resize window
        SendMessage(hwndStatus, WM_SIZE, 0, 0);
        SendMessage(hwndStatus, SB_GETRECT, 1, (LPARAM)&rect);
        MoveWindow(progressBar, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);

        GetClientRect(hWnd, &rect);
        SetWindowPos(hwndSlider, NULL, 0, 0,
            rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndBottomSlider, NULL, 0, 30,
            rect.right - rect.left - SLIDER_RIGHT - SLIDER_LEFT, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndLabel, NULL, rect.right - SLIDER_RIGHT, 5,
            SLIDER_RIGHT - 10, 20, SWP_NOACTIVATE);
        SetWindowPos(hwndBottomLabel, NULL, rect.right - SLIDER_RIGHT, 35,
            SLIDER_RIGHT - 10, 20, SWP_NOACTIVATE);
        rect.top += MAIN_WINDOW_TOP;
        rect.bottom -= 23;
        bitWidth = rect.right - rect.left;
        bitHeight = rect.bottom - rect.top;
        bmi.bmiHeader.biWidth = bitWidth;
        bmi.bmiHeader.biHeight = -bitHeight;
        if (bitmap != NULL)
            DeleteObject(bitmap);
        bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&map, NULL, 0);
        if (hdcMem != NULL)
            SelectObject(hdcMem, bitmap);

        // increase size of status bar if possible, so that biome information gets displayed
        {
            int parts[] = { max(300,bitWidth * 2 / 3),-1 };
            SendMessage(hwndStatus, SB_SETPARTS, 2, (LPARAM)parts);
        }

        {
            // On resize, figure out a better hash table size for cache, if needed.
            // Take the pixels on the screen, compare to cache size * 16^2 (pixels per chunk) times 2.1
            // (as a guess for the number of chunks we might want total, e.g., 2x the screen size).
            // I'd love to kick it to 4.0x the screen size, that's a bit nicer, but risks running out of memory.
            float zoomFactor = min(gMinZoom, 1.0f);
            zoomFactor *= zoomFactor;
            int sizeNeeded = (int)(2.1f * (float)((rect.bottom - rect.top) * (rect.right - rect.left)) / (256.0f * zoomFactor));
            if (sizeNeeded > gOptions.currentCacheSize)
            {
                // make new cache twice the size of the previous cache size, or the sizeNeeded above, whichever is larger
                gOptions.currentCacheSize = (sizeNeeded > 2 * gOptions.currentCacheSize) ? sizeNeeded : 2 * gOptions.currentCacheSize;
                ChangeCache(gOptions.currentCacheSize);
            }
        }
        //InvalidateRect(hWnd,NULL,TRUE);
        //UpdateWindow(hWnd);
        drawTheMap();
        break;

    case WM_DESTROY:
        closeMineways();
        break;

    case WM_DROPFILES:
        {
            //Get the dropped file name.
            wchar_t fileName[MAX_PATH_AND_FILE];
            DragQueryFileW((HDROP)wParam, 0, fileName, MAX_PATH_AND_FILE);
            rationalizeFilePath(fileName);

            // now test for file type and do the right thing
            wchar_t lcFileName[MAX_PATH_AND_FILE];
            wcscpy_s(lcFileName, MAX_PATH_AND_FILE, fileName);
            _wcslwr_s(lcFileName, MAX_PATH_AND_FILE);
            // find where suffix is
            int slen = (int)wcslen(lcFileName);
            wchar_t* spos = lcFileName + slen - 1; // last character in string
            int scount = 0;

            while (*spos != '.' && scount < slen)
            {
                // quit if we hit a directory - no suffix found, quietly exit
                if (*spos == gPreferredSeparator) {
                    goto ExitDropfiles;
                }
                spos--;
                scount++;
            }

            // find a suffix?
            if (scount < slen) {
                // get back to suffix itself
                spos++;
                if (wcscmp(spos, L"obj") == 0 ||
                    wcscmp(spos, L"usda") == 0 ||
                    wcscmp(spos, L"wrl") == 0 ||
                    wcscmp(spos, L"mwscript") == 0 ||
                    wcscmp(spos, L"txt") == 0    // for STL export
                    ) {
                    // import file
                    wcscpy_s(gImportFile, MAX_PATH_AND_FILE, fileName);
                    splitToPathAndName(gImportFile, gImportPath, NULL);
                    runImportOrScript(gImportFile, gWS, &gBlockLabel, gHoldlParam, true);
                }
                else if (wcscmp(spos, L"dat") == 0 ||
                    wcscmp(spos, L"schematic") == 0
                    ) {
                    // attempt to load world or schematic file
                    int retCode = loadWorldFromFilename(fileName, hWnd);
                    // load worked?
                    if (retCode) {
                        if (retCode == 2) {
                            gotoSurface(hWnd, hwndSlider, hwndLabel);
                        }
                        setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
                    }
                }
                // a *little* weak in testing, the directory could contain "terrainExt", but good enough. GIGO.
                else if (wcscmp(spos, L"png") == 0 && wcsstr(fileName, L"terrainExt") != NULL) {
                    // copy file name, since it definitely appears to exist.
                    wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, fileName);
                    splitToPathAndName(gSelectTerrainPathAndName, gSelectTerrainDir, NULL);
                    formTitle(&gWorldGuide, hWnd);
                }
                else {
                    FilterMessageBox(NULL, _T("Mineways does not understand the type of the file you just dropped. You can drag and drop Mineways exported OBJ, USDA, and WRL files, the TXT files for Mineways STL exports, DAT world and old-school SCHEMATIC files, TerrainExt* PNG texture files, and MWSCRIPT scripting files."),
                        _T("Read error"), MB_OK | MB_ICONWARNING);
                }
            }
        }
        ExitDropfiles:
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    if (UnknownBlockRead() && NeedToCheckUnknownBlock())
    {
        // flag this just the first time found, then turn off error and don't check again for this world
        CheckUnknownBlock(false);
        ClearBlockReadCheck();

        // note how this is different that the write schematic message, which notes 1.13 export problems
        FilterMessageBox(NULL, _T("Warning: Mineways encountered an unknown block type in your model. Such blocks are converted to bedrock. Mineways does not understand blocks added by mods. If you are not using mods, your version of Mineways may be out of date. Check http://mineways.com for a newer version of Mineways."),
            _T("Read error"), MB_OK | MB_ICONWARNING);
    }

    return 0;
}

static void closeMineways()
{
    int i;
    if (gArgList) {
        LocalFree(gArgList);
        gArgList = NULL;
    }
    if (gExecutionLogfile) {
        PortaClose(gExecutionLogfile);
        gExecutionLogfile = 0x0;
    }
    // clear memory, if testing for memory leaks
    for (i = 0; i < gNumWorlds; i++) {
        if (gWorlds[i] != NULL) {
            free(gWorlds[i]);
            gWorlds[i] = NULL;
        }
    }
    if (gCustomCurrency != NULL) {
        free(gCustomCurrency);
        gCustomCurrency = NULL;
    }
    Cache_Empty();

    PostQuitMessage(0);
}
// parse on startup, looking for an execution log file name.
// Return true if successful (i.e. no error found) - does NOT mean a log file was opened, just that the parse was OK.
static bool startExecutionLogFile(const LPWSTR* argList, int argCount)
{
    // argument:
    // -l log-file-name
    int argIndex = 1;

    // if execution log file already opened earlier (hard-wired in), just go to part where we write to the file.
    if (gExecutionLogfile)
        goto WriteFile;
    while (argIndex < argCount)
    {
        // is it a script? get past it
        if (wcscmp(argList[argIndex], L"-l") == 0) {
            // found window resize
            argIndex++;
            if (argIndex < argCount) {
                // take next argument as file name for logging
                argList[argIndex];
                // attempt to open file
                // overwrite previous log file
                gExecutionLogfile = PortaCreate(argList[argIndex]);
            WriteFile:
                if (gExecutionLogfile == INVALID_HANDLE_VALUE) {
                    //// try the long file name, using current directory - not needed, it tries the local file anyway.
                    //wchar_t longFileName[MAX_PATH_AND_FILE];
                    //wcscpy_s(longFileName, MAX_PATH_AND_FILE, gCurrentDirectory);
                    //wcscat_s(longFileName, MAX_PATH_AND_FILE - wcslen(longFileName), gPreferredSeparatorString);
                    //wcscat_s(longFileName, MAX_PATH_AND_FILE - wcslen(longFileName), argList[argIndex]);
                    //gExecutionLogfile = PortaCreate(argList[argIndex]);
                    //if (gExecutionLogfile == INVALID_HANDLE_VALUE) {
                    FilterMessageBox(NULL, _T("Cannot open script log file, execution log file is specified by '-l log-file-name'."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
                    return false;
                    //}
                }
                // write one line to start.
                // Print local time as a string.
                errno_t errNum;
                struct tm newtime;
                __time32_t aclock;
                char timeString[256];

                _time32(&aclock);   // Get time in seconds.
                _localtime32_s(&newtime, &aclock);   // Convert time to struct tm form.

                errNum = asctime_s(timeString, 32, &newtime);   // TODO should use strftime?
                if (!errNum)
                {
                    char outputString[256];
                    sprintf_s(outputString, 256, "Mineways version %d.%02d execution log begun %s\n", gMinewaysMajorVersion, gMinewaysMinorVersion, timeString);
                    LOG_INFO(gExecutionLogfile, outputString);
                }
                return true;
            }
            // if we got here, parsing didn't work
            FilterMessageBox(NULL, _T("Command line startup error, execution log file is specified by '-l log-file-name'."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            return false;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    return true;
}

// parse on startup, looking for a window size - has to be done before the window is created, etc.
// 0 = fail, 1 = x and y modified, 2 = also minimize window with -m
static int modifyWindowSizeFromCommandLine(int* x, int* y, const LPWSTR* argList, int argCount)
{
    // arguments possible:
    // -w x y
    // script name. Put double quotes around script name if it has spaces in the file name or directory path
    int retCode = 1;
    int argIndex = 1;
    while (argIndex < argCount)
    {
        // is it a script? get past it
        if (wcscmp(argList[argIndex], L"-w") == 0) {
            // found window resize
            argIndex++;
            if (argIndex < argCount) {
                // convert next argument to an integer
                int valx = _wtoi(argList[argIndex]);
                argIndex++;
                if (valx > 0) {
                    if (argIndex < argCount) {
                        int valy = _wtoi(argList[argIndex]);
                        // next line not needed, since we always return
                        //argIndex++;
                        if (valy > 0) {
                            // found it! Always use the first one found
                            *x = valx;
                            *y = valy;
                            return retCode;
                        }
                    }
                }
            }
            // if we got here, parsing didn't work
            FilterMessageBox(NULL, _T("Command line startup error, window size should be set by \"-w 480 582\" or two other positive integers. Window command ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        else if (wcscmp(argList[argIndex], L"-m") == 0) {
            // found window minimize
            argIndex++;
            retCode = 2;
        }
        else {
            // skip whatever it is, including numerical fields, script file names, etc.
            argIndex++;
        }
    }
    return retCode;
}

static int loadWorldFromFilename(wchar_t* pathAndFile, HWND hWnd)
{
    int retCode = 1;    // assume success

    // first, "rationalize" the gWorldGuide.world name: make it all \'s or all /'s, not both.
    // This will make it nicer for comparing to see if the new world is the same as the previously-loaded world.
    rationalizeFilePath(pathAndFile);

    // schematic or world?
    wchar_t filename[MAX_PATH_AND_FILE];
    splitToPathAndName(pathAndFile, NULL, filename);
    size_t len = wcslen(filename);
    if (_wcsicmp(&filename[len - 10], L".schematic") == 0) {
        // Read schematic as a world.
        gWorldGuide.type = WORLD_SCHEMATIC_TYPE;
        gWorldGuide.sch.repeat = (StrStrI(filename, L"repeat") != NULL);
        gSameWorld = (wcscmp(gWorldGuide.world, pathAndFile) == 0);
        wcscpy_s(gWorldGuide.world, MAX_PATH_AND_FILE, pathAndFile);
        int error = loadWorld(hWnd);
        if (error) {
            //if (loadSchematic(pathAndFile)) {
                // schematic world not loaded properly
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            if (error == 100 + 5) {
                FilterMessageBox(NULL, _T("Error: cannot read newfangled Minecraft schematic. Schematics from FAWE/WorldEdit for 1.13 (and newer) are not supported - it's a lot of boring work for me to add this. I suggest you read the schematic file into a world and have Mineways read that world."),
                    _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
            else {
                FilterMessageBox(NULL, _T("Error: cannot read Minecraft schematic for some unknown reason. Feel free to send it to me for analysis."),
                    _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
            return 0;
        }
        UpdateWindow(hWnd);
    }
    else {
        // read level.dat normal world

        PathRemoveFileSpec(pathAndFile);
        //convert path to utf8
        //WideCharToMultiByte(CP_UTF8,0,path,-1,gWorldGuide.world,MAX_PATH_AND_FILE,NULL,NULL);
        gSameWorld = (wcscmp(gWorldGuide.world, pathAndFile) == 0);
        wcscpy_s(gWorldGuide.world, MAX_PATH_AND_FILE, pathAndFile);

        // if this is not the same world, switch back to the aboveground view.
        if (!gSameWorld)
        {
            // need to go to surface
            retCode = 2;
        }

        UpdateWindow(hWnd);
        gWorldGuide.type = WORLD_LEVEL_TYPE;
        int loadErr = loadWorld(hWnd);
        if (loadErr)
        {
            showLoadWorldError(loadErr);
            return 0;
        }
    }
    // the file read succeeded, so update path to it.
    wcscpy_s(gWorldPathCurrent, MAX_PATH_AND_FILE, pathAndFile);
    return retCode;
}

// return true if a command line directory was found and it's valid, false means not found or not valid.
// fill in saveWorldDirectory with user-specified directory
static int getWorldSaveDirectoryFromCommandLine(wchar_t* saveWorldDirectory, const LPWSTR* argList, int argCount)
{
    // parse to get -s dir
    int argIndex = 1;
    while (argIndex < argCount)
    {
        // look for if the startup directory is specified on the command line
        if (wcscmp(argList[argIndex], L"-s") == 0) {
            // found window resize
            argIndex++;
            if (argIndex < argCount) {
                wcscpy_s(saveWorldDirectory, MAX_PATH_AND_FILE, argList[argIndex]);
                // next line not needed, since we always return
                //argIndex++;
                if (wcscmp(saveWorldDirectory, L"none") == 0) {
                    // empty path
                    wcscpy_s(saveWorldDirectory, MAX_PATH_AND_FILE, L"");
                    return -1;
                }
                if (!PathFileExists(saveWorldDirectory)) {
                    LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine path does not exist\n");
                    wchar_t message[1024];
                    wsprintf(message, _T("Warning:\nThe path \"%s\" you specified on the command line for your saved worlds location does not exist, so default worlds directory will be used. Use \"-s none\" to load no worlds."), saveWorldDirectory);
                    FilterMessageBox(NULL, message,
                        _T("Warning"), MB_OK | MB_ICONWARNING);
                    return 0;
                }
                else {
                    // found it - done
                    return 1;
                }
            }
            // if we got here, parsing didn't work
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine directory not given with -s option\n");
            FilterMessageBox(NULL, _T("Command line startup error, directory is missing. Your save world directory should be set by \"-s <directory>\". Use \"-s none\" to load no worlds. Setting ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    // we return 0 when we don't find a directory that was input on the command line
    return 0;
}

static int getSuppressFromCommandLine(const LPWSTR* argList, int argCount)
{
    // parse to get -suppress
    int argIndex = 1;
    while (argIndex < argCount)
    {
        if (wcscmp(argList[argIndex], L"-suppress") == 0) {
            // found suppress
            return 1;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    // we return 0 when we don't find the right information on the command line
    return 0;
}

static int getZoomLevelFromCommandLine(float* minZoom, const LPWSTR* argList, int argCount)
{
    // parse to get -zl zoom
    int argIndex = 1;
    while (argIndex < argCount)
    {
        if (wcscmp(argList[argIndex], L"-zl") == 0) {
            // found zoom level minimum
            argIndex++;
            if (argIndex < argCount) {
                // convert next argument to an integer
                float valzoom = (float)_wtof(argList[argIndex]);
                argIndex++;
                if (valzoom >= 0.0625f && valzoom <= 1.0f) {
                    *minZoom = valzoom;
                    return 1;
                }
            }
            // if we got here, parsing didn't work
            FilterMessageBox(NULL, _T("Command line startup error. For \"-zl #\", minimum zoom level, the value # must be between 0.0625 and 1. Zoom level command ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    // we return 0 when we don't find the right information on the command line
    return 0;
}

static int getTerrainFileFromCommandLine(wchar_t* terrainFile, const LPWSTR* argList, int argCount)
{
    // parse to get -s dir
    int argIndex = 1;
    while (argIndex < argCount)
    {
        // look for if the startup directory is specified on the command line
        if (wcscmp(argList[argIndex], L"-t") == 0) {
            // found terrain arg tag
            argIndex++;
            if (argIndex < argCount) {
                wcscpy_s(terrainFile, MAX_PATH_AND_FILE, argList[argIndex]);
                // next line not needed, since we always return
                //argIndex++;
                if (!PathFileExists(terrainFile)) {
                    LOG_INFO(gExecutionLogfile, " getTerrainFileFromCommandLine path does not exist\n");
                    wchar_t message[1024];
                    wsprintf(message, _T("Warning:\nThe path \"%s\" you specified on the command line for your terrain file does not exist, so the default terrain is used."), terrainFile);
                    FilterMessageBox(NULL, message,
                        _T("Warning"), MB_OK | MB_ICONWARNING);
                    return 0;
                }
                else {
                    // found it - done
                    return 1;
                }
            }
            // if we got here, parsing didn't work
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine directory not given with -s option\n");
            FilterMessageBox(NULL, _T("Command line startup error, file is missing. Your terrain file should be set by \"-t <filename>\". Setting ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    // we return 0 when we don't find a file that was input on the command line
    return 0;
}


static bool processCreateArguments(WindowSet& ws, const char** pBlockLabel, LPARAM holdlParam, const LPWSTR* argList, int argCount)
{
    // arguments possible:
    // -w x y
    // script name. Put double quotes around script name if it has spaces in the file name or directory path
    int argIndex = 1;
    while (argIndex < argCount)
    {
        if (wcscmp(argList[argIndex], L"-w") == 0) {
            // skip window resize - handled by modifyWindowSizeFromCommandLine
            LOG_INFO(gExecutionLogfile, " skip window resize\n");
            argIndex += 3;
        }
        else if (wcscmp(argList[argIndex], L"-m") == 0) {
            // skip window minimize - handled by modifyWindowSizeFromCommandLine
            LOG_INFO(gExecutionLogfile, " skip window minimize\n");
            argIndex++;
        }
        else if (wcscmp(argList[argIndex], L"-s") == 0) {
            // skip user world save directory
            LOG_INFO(gExecutionLogfile, " skip user world save directory\n");
            argIndex += 2;
        }
        else if (wcscmp(argList[argIndex], L"-l") == 0) {
            // skip logging
            LOG_INFO(gExecutionLogfile, " skip logging\n");
            argIndex += 2;
        }
        else if (wcscmp(argList[argIndex], L"-t") == 0) {
            // skip terrain file
            LOG_INFO(gExecutionLogfile, " skip terrain file\n");
            argIndex += 2;
        }
        else if (wcscmp(argList[argIndex], L"-zl") == 0) {
            // skip minimum zoom level (experimental feature)
            LOG_INFO(gExecutionLogfile, " skip minimum zoom level\n");
            argIndex += 2;
        }
        else if (wcscmp(argList[argIndex], L"-suppress") == 0) {
            // skip minimum zoom level (experimental feature)
            LOG_INFO(gExecutionLogfile, " skip suppressing all popups\n");
            argIndex++;
        }
        else if (*argList[argIndex] == '-') {
            // unknown argument, so list out arguments
            FilterMessageBox(NULL, L"Warning:\nUnknown argument on command line.\nUsage: mineways.exe [-w X Y] [-m] [-s UserSaveDirectory|none] [-t terrainExtYourfile.png] [-l mineways_exec.log] [file1.mwscript [file2.mwscript [...]]]", _T("Warning"), MB_OK | MB_ICONWARNING);
            // abort
            return true;
        }
        // is it a script?
        else {
            // Load a script; if it works fine, don't pop up a dialog
            // TODO: someday maybe add a check that if the argument is a level.dat, try to load that.
            LOG_INFO(gExecutionLogfile, " runImportOrScript\n");
            runImportOrScript(argList[argIndex], ws, pBlockLabel, holdlParam, false);
            argIndex++;
        }
    }
    return true;
}

// kinda overkill, we should note it's loaded only once
static void setUIOnLoadWorld(HWND hWnd, HWND hwndSlider, HWND hwndLabel, HWND hwndInfoLabel, HWND hwndBottomSlider, HWND hwndBottomLabel)
{
    if (!gLoaded) {
        gLoaded = TRUE;
        EnableWindow(hwndSlider, TRUE);
        EnableWindow(hwndLabel, TRUE);
        EnableWindow(hwndInfoLabel, TRUE);
        EnableWindow(hwndBottomSlider, TRUE);
        EnableWindow(hwndBottomLabel, TRUE);
    }
    // we want to set to 383 for new worlds, 255 for old
    SendMessage(hwndSlider, TBM_SETRANGE, TRUE, MAKELONG(0, gMaxHeight - gMinHeight));
    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
    wchar_t text[NUM_STR_SIZE];
    _itow_s(gCurDepth, text, NUM_STR_SIZE);
    SetWindowText(hwndLabel, text);

    SendMessage(hwndBottomSlider, TBM_SETRANGE, TRUE, MAKELONG(0, gMaxHeight - gMinHeight));
    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true);

    validateItems(GetMenu(hWnd));
    drawInvalidateUpdate(hWnd);
}


static void updateCursor(LPARAM lParam, BOOL hdragging)
{
    // change cursor to something special if highlighting an area and not currently creating that area
    int mx, my, mz, type, dataVal, biome;
    int on, minx, miny, minz, maxx, maxy, maxz;

    BOOL cursorSet = FALSE;
    if (gHighlightOn && !hdragging)
    {
        //SetCapture(hWnd);

        // get mouse position in world space
        (void)IDBlock(LOWORD(lParam), HIWORD(lParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
            bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);

        // now to check the corners: is this location near any of them?
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);

        // see if we select on the selection border
        // highlighting is on, check the corners: inside bounds of current selection?
        if ((mx >= minx - SELECT_MARGIN / gCurScale) &&
            (mx <= maxx + SELECT_MARGIN / gCurScale) &&
            (mz >= minz - SELECT_MARGIN / gCurScale) &&
            (mz <= maxz + SELECT_MARGIN / gCurScale))
        {
            int xzone = 0;
            int zzone = 0;
            int innerx = (int)(SELECT_MARGIN / gCurScale);
            int innerz = (int)(SELECT_MARGIN / gCurScale);
            gAdjustingSelection = 1;

            if (innerx > maxx - minx)
            {
                innerx = (maxx - minx - 1) / 2;
            }
            if (innerz > maxz - minz)
            {
                innerz = (maxz - minz - 1) / 2;
            }

            if (mx <= minx + innerx)
            {
                // in minx zone
                xzone = 1;
            }
            else if (mx >= maxx - innerx)
            {
                // in maxx zone
                xzone = 2;
            }

            if (mz <= minz + innerz)
            {
                // in minz zone
                zzone = 1;
            }
            else if (mz >= maxz - innerz)
            {
                // in maxz zone
                zzone = 2;
            }

            if (xzone != 0 || zzone != 0)
            {
                // OK, cursor will be set to something special
                cursorSet = TRUE;
                switch (xzone * 3 + zzone)
                {
                case 1: // zzone min
                case 2: // zzone max
                    SetCursor(gNsCursor);
                    break;
                case 3: // xzone min
                case 6: // xzone max
                    SetCursor(gWeCursor);
                    break;
                case 4: // xzone min, zzone min
                case 8: // xzone max, zzone max
                    SetCursor(gNwseCursor);
                    break;
                case 5: // xzone min, zzone max
                case 7: // xzone max, zzone min
                    SetCursor(gNeswCursor);
                    break;
                default:
                    MY_ASSERT(gAlwaysFail);
                }
            }
        }
    }

    if (!cursorSet)
    {
        // default point
        SetCursor(gArrowCursor);
    }
}

// Note: don't change worldType before calling this method - it sets worldType to overworld itself.
static void gotoSurface(HWND hWnd, HWND hwndSlider, HWND hwndLabel)
{
    if (gOptions.worldType & HELL)
    {
        // get out of hell zoom
        gCurX *= 8.0;
        gCurZ *= 8.0;
        // and undo other hell stuff
        if (gCurDepth == 126)
        {
            gCurDepth = gMaxHeight;
            setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
        }
        // turn off obscured, then restore overworld's obscured status
        gOptions.worldType &= ~HIDEOBSCURED;
        CheckMenuItem(GetMenu(hWnd), IDM_OBSCURED, MF_UNCHECKED);
        gOptions.worldType |= gOverworldHideStatus;
        // uncheck hell menu item
        gOptions.worldType &= ~HELL;
        CheckMenuItem(GetMenu(hWnd), IDM_HELL, MF_UNCHECKED);

        // semi-useful, I'm not sure: zoom out when going back
        //gCurScale /= 8.0;
        //gCurScale = clamp(gCurScale,gMinZoom,MAXZOOM);
        //formTitle(&gWorldGuide, hWnd);
    }
    // Ender is easy, just turn it off 
    gOptions.worldType &= ~ENDER;
    CheckMenuItem(GetMenu(hWnd), IDM_END, MF_UNCHECKED);
}

static void updateStatus(int mx, int mz, int my, const char* blockLabel, int type, int dataVal, int biome, HWND hwndStatus)
{
    wchar_t buf[150];
    char sbuftype[100];
    char sbufbiome[100];
    char sbufmap[100];

    // always show all information - it's fine
    //if ( gOptions.worldType & SHOWALL )
    sprintf_s(sbuftype, 100, " (id %d:%d)", type, dataVal);
    //else
    //    sbuftype[0] = '\0';

    //if ( (gOptions.worldType & BIOMES) && (biome >= 0) )
    // TODO: sometimes the biome comes back as some crazy number - I suspect we're reading uninitialized memory, but it's hard to pin down.
    // Let's just guard against it by not allowing it at all
    if (biome >= 0 && biome < 256)
        sprintf_s(sbufbiome, 100, "; %s biome", gBiomes[biome].name);
    else
        sbufbiome[0] = '\0';

    // also note map chunk file name and chunk itself
    if (gWorldGuide.type == WORLD_TEST_BLOCK_TYPE) {
        sbufmap[0] = '\0';
    }
    else {
        // (((mx>>4) % 32) + 32) % 32) ensures the value is non-negative - there's probably a better way
        sprintf_s(sbufmap, 100, "; r.%d.%d.mca, chunk [%d, %d] {%d, %d}", mx >> 9, mz >> 9, (((mx>>4) % 32) + 32) % 32, (((mz>>4) % 32) + 32) % 32, mx>>4, mz>>4);
    }


    // if my is out of bounds, print dashes
    if (my < -1 + gMinHeight || my >= gMaxHeight + 1)
    {
        //wsprintf(buf,L"%S \t\tBottom %d",blockLabel,gTargetDepth);
        wsprintf(buf, L"%S", blockLabel);	// char to wchar
    }
    else
    {
        // In Nether, show corresponding overworld coordinates
        if (gOptions.worldType & HELL)
            //wsprintf(buf,L"%d,%d; y=%d[%d,%d] %S \t\tBtm %d",mx,mz,my,mx*8,mz*8,blockLabel,gTargetDepth);
            wsprintf(buf, L"%d,%d,y=%d[%d,%d]; %S%S%S%S%S", mx, mz, my, mx * 8, mz * 8, 
                WATERLOGGED_LABEL(type, dataVal) ? "waterlogged " : "",
                blockLabel, sbuftype, sbufbiome, sbufmap);	// char to wchar
        else
            //wsprintf(buf,L"%d,%d; y=%d %S \t\tBottom %d",mx,mz,my,blockLabel,gTargetDepth);
            wsprintf(buf, L"%d,%d,y=%d; %S%S%S%S%S", mx, mz, my, 
                WATERLOGGED_LABEL(type, dataVal) ? "waterlogged " : "",
                blockLabel, sbuftype, sbufbiome, sbufmap);	// char to wchar
    }
    SendMessage(hwndStatus, SB_SETTEXT, 0, (LPARAM)buf);
}

static void sendStatusMessage(HWND hwndStatus, wchar_t* buf)
{
    SendMessage(hwndStatus, SB_SETTEXT, 0, (LPARAM)buf);
}


static int numCustom = 1;
static void populateColorSchemes(HMENU menu)
{
    MENUITEMINFO info;
    for (int i = 1; i < numCustom; i++)
        DeleteMenu(menu, IDM_CUSTOMCOLOR + i, MF_BYCOMMAND);
    info.cbSize = sizeof(MENUITEMINFO);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_DATA;
    info.fType = MFT_STRING;
    numCustom = 1;
    ColorManager cm;
    ColorScheme cs;
    int id = cm.next(0, &cs);
    while (id)
    {
        info.wID = IDM_CUSTOMCOLOR + numCustom;
        info.cch = (UINT)wcslen(cs.name);
        info.dwTypeData = cs.name;
        info.dwItemData = cs.id;
        InsertMenuItem(menu, IDM_CUSTOMCOLOR, FALSE, &info);
        numCustom++;
        id = cm.next(id, &cs);
    }
}
static void useCustomColor(int wmId, HWND hWnd, bool invalidate /*= true*/)
{
    for (int i = 0; i < numCustom; i++)
        CheckMenuItem(GetMenu(hWnd), IDM_CUSTOMCOLOR + i, MF_UNCHECKED);
    CheckMenuItem(GetMenu(hWnd), wmId, MF_CHECKED);
    ColorManager cm;
    ColorScheme cs;
    if (wmId > IDM_CUSTOMCOLOR)
    {
        MENUITEMINFO info;
        info.cbSize = sizeof(MENUITEMINFO);
        info.fMask = MIIM_DATA;
        GetMenuItemInfo(GetMenu(hWnd), wmId, FALSE, &info);
        cs.id = (int)info.dwItemData;
        cm.load(&cs);
    }
    else
    {
        ColorManager::Init(&cs);
        cs.id = 0;
        wcscpy_s(cs.name, 255, L"Standard");
    }
    SetMapPalette(cs.colors, NUM_BLOCKS_CS);
    if (invalidate)
        drawInvalidateUpdate(hWnd);

    // store away for our own use
    wcscpy_s(gSchemeSelected, 255, cs.name);
}
static int findColorScheme(wchar_t* name)
{
    ColorManager cm;
    ColorScheme cs;
    int id = cm.next(0, &cs);
    int count = 1;
    // go through list in same order as created in populateColorSchemes
    while (id)
    {
        if (wcscmp(name, cs.name) == 0)
            return count;
        id = cm.next(id, &cs);
        count++;
    }
    return -1;
}

static void updateProgress(float progress, wchar_t* buf)
{
    // pass in -999.0f or similar to avoid updating the progress bar itself
    if (progress >= 0.0f) {
        SendMessage(progressBar, PBM_SETPOS, (int)(progress * 100), 0);
    }
    // change status only if there's text input
    if (buf != NULL && wcslen(buf) > 0) {
        sendStatusMessage(statusWindow, buf);
    }
}

static void drawTheMap()
{
    if (gLoaded)
        checkMapDrawErrorCode(
            DrawMap(&gWorldGuide, gCurX, gCurZ, gCurDepth - gMinHeight, gMaxHeight, bitWidth, bitHeight, gCurScale, map, &gOptions, gHitsFound, updateProgress, gMinecraftVersion, gVersionID)
        );
    else {
        // avoid clearing nothing at all.
        if (bitWidth > 0 && bitHeight > 0)
            memset(map, 0xff, bitWidth * bitHeight * 4);
        else
            return;	// nothing to draw
    }
    SendMessage(progressBar, PBM_SETPOS, 0, 0);
    for (int i = 0; i < bitWidth * bitHeight * 4; i += 4)
    {
        map[i] ^= map[i + 2];
        map[i + 2] ^= map[i];
        map[i] ^= map[i + 2];
    }
    return;
}

static int loadSchematic(wchar_t* pathAndFile)
{
    // Always read again, even if read before, and recenter.
    // Set spawn and player location to center of model.
    // Read whole schematic in, then 
    CloseAll();

#define CHECK_READ_SCHEMATIC_QUIT( b, e )			\
    if ( (b) != 1 ) {								\
        return (e);		\
    }

    // arbitrarily adding 100 to error numbers to keep them different than loadWorld error numbers
    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Width", &gWorldGuide.sch.width), 100 + 1);
    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Height", &gWorldGuide.sch.height), 100 + 2);
    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Length", &gWorldGuide.sch.length), 100 + 3);

    gWorldGuide.sch.numBlocks = gWorldGuide.sch.width * gWorldGuide.sch.height * gWorldGuide.sch.length;
    if (gWorldGuide.sch.numBlocks <= 0)
        return 100 + 4;

    gWorldGuide.sch.blocks = (unsigned char*)malloc(gWorldGuide.sch.numBlocks);
    gWorldGuide.sch.data = (unsigned char*)malloc(gWorldGuide.sch.numBlocks);

    // explicit CHECK_READ_SCHEMATIC_QUIT( b, e ) since we want to take a corrective action
    // This is where things will fail when the schematic is a FAWE file for 1.13 or newer. See issue https://github.com/erich666/Mineways/issues/40
    if (GetSchematicBlocksAndData(pathAndFile, gWorldGuide.sch.numBlocks, gWorldGuide.sch.blocks, gWorldGuide.sch.data) != 1) {
        // free allocated memory, as it could be a lot
        free(gWorldGuide.sch.blocks);
        gWorldGuide.sch.blocks = NULL;
        free(gWorldGuide.sch.data);
        gWorldGuide.sch.data = NULL;
        return 100 + 5;
    }

    // All data's read in! Now we let the mapping system take over and load on demand.
    gSpawnX = gSpawnY = gSpawnZ = gPlayerX = gPlayerY = gPlayerZ = 0;
    gVersionID = 1343;	// latest 1.12.2 https://minecraft.gamepedia.com/Data_version
    gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
    setHeightsFromVersionID();

    return 0;
}

static void setHeightsFromVersionID()
{
    // this version of 1.17 beta increased height, then went back down; also see MAX_HEIGHT() macro. Here we are offsetting to ground level properly
    gMaxHeight = MAX_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
    gMinHeight = ZERO_WORLD_HEIGHT(gVersionID, gMinecraftVersion);
}

static void testWorldHeight(int& minHeight, int& maxHeight, int mcVersion, int spawnX, int spawnZ, int playerX, int playerZ)
{
    // proceed currently only if version >= 1.17 (1.18 isn't possible yet, but let's future proof it anyway)
    if (mcVersion >= 17) {
        // Read where the spawn and player location is and see heights there.
        // I figure the player and spawn locations have been created, hopefully one of them with the mod.
        // The mod appears to do things where the player is, not the spawn.
        GetChunkHeights(&gWorldGuide, minHeight, maxHeight, mcVersion, spawnX, spawnZ);
        GetChunkHeights(&gWorldGuide, minHeight, maxHeight, mcVersion, playerX, playerZ);
    }
}

// return 1 or 2 or higher if world could not be loaded
static int loadWorld(HWND hWnd)
{
    CloseAll();
    // defaults
    gWorldGuide.minHeight = 0;
    gWorldGuide.maxHeight = 255;

    // delete schematic data stored, if any, since we're loading a new world
    if (gWorldGuide.sch.blocks != NULL) {
        free(gWorldGuide.sch.blocks);
        gWorldGuide.sch.blocks = NULL;
    }
    if (gWorldGuide.sch.data != NULL) {
        free(gWorldGuide.sch.data);
        gWorldGuide.sch.data = NULL;
    }

    gWorldGuide.nbtVersion = 0;
    gWorldGuide.isServerWorld = false;
    switch (gWorldGuide.type) {
    case WORLD_TEST_BLOCK_TYPE:
        // load test world
        MY_ASSERT(gWorldGuide.world[0] == 0);
        gSpawnX = gSpawnY = gSpawnZ = gPlayerX = gPlayerY = gPlayerZ = 0;
        gVersionID = 3105;	// Change this to the current release number https://minecraft.fandom.com/wiki/Data_version
        gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
        setHeightsFromVersionID();
        break;

    case WORLD_LEVEL_TYPE:
        // Don't necessarily clear selection! It's a feature: you can export, then go modify your Minecraft
        // world, then reload and carry on.
        //gHighlightOn=FALSE;
        //SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
        // Get the NBT file type, lowercase "version". Should be 19333 or higher to be Anvil. See http://minecraft.gamepedia.com/Level_format#level.dat_format
        gSubError = GetFileVersion(gWorldGuide.world, &gWorldGuide.nbtVersion, gFileOpened, MAX_PATH_AND_FILE);
        if (gSubError != 0) {
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 1;
        }
        if (gWorldGuide.nbtVersion < 19133)
        {
            // world is really old, pre Anvil
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 2;
        }
        // it's not a good sign if we can't get the spawn location etc. from the world - consider this a failure to load
        gSubError = GetSpawn(gWorldGuide.world, &gSpawnX, &gSpawnY, &gSpawnZ);
        if (gSubError != 0)
        {
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 3;
        }
        if (GetPlayer(gWorldGuide.world, &gPlayerX, &gPlayerY, &gPlayerZ) != 0) {
            // if this fails, it's a server world, so set the values equal to the spawn location; return no error
            // from http://minecraft.gamepedia.com/Level_format
            // Player: The state of the Singleplayer player. This overrides the <player>.dat file with the same name as the
            // Singleplayer player. This is only saved by Servers if it already exists, otherwise it is not saved for server worlds. See Player.dat Format.
            gPlayerX = gSpawnX;
            gPlayerY = gSpawnY;
            gPlayerZ = gSpawnZ;
            gWorldGuide.isServerWorld = true;
        }
        // This may or may not work, so we ignore errors.
        GetFileVersionId(gWorldGuide.world, &gVersionID);
        gMinecraftVersion = DATA_VERSION_TO_RELEASE_NUMBER(gVersionID);
        setHeightsFromVersionID();
        testWorldHeight(gMinHeight, gMaxHeight, gMinecraftVersion, gSpawnX, gSpawnZ, gPlayerX, gPlayerZ);
        break;

    case WORLD_SCHEMATIC_TYPE:
    {
        int error = loadSchematic(gWorldGuide.world);
        if (error) {
            // not differentiated for now
            return error;
        }
    }
    break;

    default:
        MY_ASSERT(gAlwaysFail);
    }

    // keep current state around, we use it to set new window title
    gHoldSameWorld = gSameWorld;

    // if this is the first world you loaded, or not the same world as before (reload), set location to spawn.
    if (!gSameWorld)
    {
        // new world loaded
        gOneTimeDrawError = gInstanceError = true;
        gOneTimeDrawWarning = NBT_WARNING_NAME_NOT_FOUND;

        gCurX = gSpawnX;
        gCurZ = gSpawnZ;
        gSameWorld = TRUE;   // so if we reload
        // zoom out when loading a new world, since location's reset.
        gCurScale = DEFAULTZOOM;
        formTitle(&gWorldGuide, hWnd);

        gCurDepth = gMaxHeight;
        // set lower level height to sea level, or to 0 if it's a schematic
        gTargetDepth = (gWorldGuide.type == WORLD_SCHEMATIC_TYPE) ? 0x0 : MIN_OVERWORLD_DEPTH;
        gHighlightOn = FALSE;
        SetHighlightState(gHighlightOn, 0, gTargetDepth, 0, 0, gCurDepth, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
        // turn on error checking for this new world, to warn of unknown blocks
        CheckUnknownBlock(true);
        // just to be safe, make sure flag is cleared for read check.
        ClearBlockReadCheck();
        // because gSameWorld gets set to 1 by loadWorld()T
        if (!gHoldSameWorld)
        {
            formTitle(&gWorldGuide, hWnd);
            char tmpString[1024];
            sprintf_s(tmpString, "%ws", stripWorldName(gWorldGuide.world));
#ifdef SKETCHFAB
            strcpyLimited(gSkfbPData.skfbName, 49, tmpString);
            // just in case name was too long
            gSkfbPData.skfbName[48] = (char)0;
#endif
        }
    }
    // now done by setUIOnLoadWorld, which should always be called right after loadWorld
    //gLoaded=TRUE;
    return 0;
}

#ifdef SKETCHFAB
// strcpy_s asserts if string is too long. Sketchfab wants a 48 character name (plus 0 char at end).
static void strcpyLimited(char* dst, int len, const char* src)
{
    int i;
    for (i = 0; i < len - 1 || src[i] == (char)0; i++) {
        dst[i] = src[i];
    }
    dst[i] = (char)0;
}
#endif

//static void minecraftJarPath(TCHAR *path)
//{
//	SHGetFolderPath(NULL,CSIDL_APPDATA,NULL,0,path);
//	PathAppend(path,L".minecraft");
//	PathAppend(path,L"bin");
//}

static int setWorldPath(TCHAR* path)
{
    // try the normal Windows location
    SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path);
    PathAppend(path, L".minecraft");
    PathAppend(path, L"saves");

    if (PathFileExists(path))
        return 1;

    // try Mac path
    TCHAR newPath[MAX_PATH_AND_FILE];
    // Workaround for OSX minecraft worlds path
    // Get the username and check in user's Library directory
    wchar_t user[1024];
    DWORD username_len = 1024;
    GetUserName(user, &username_len);
    swprintf_s(path, MAX_PATH_AND_FILE, L"Z:\\Users\\%s\\Library\\Application Support\\minecraft\\saves", user);

    if (PathFileExists(path))
        return 1;

    // Back to the old method. Not sure if we should keep this method...
    wcscpy_s(path, MAX_PATH_AND_FILE, L"./Library/Application Support/minecraft/saves");
    wchar_t msgString[1024];

    // keep on trying and trying...
    for (int i = 0; i < 15; i++)
    {
        if (gDebug)
        {
            swprintf_s(msgString, 1024, L"Try path %s", path);
            FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
        }

        if (PathFileExists(path))
        {
            // convert path to absolute path
            wcscpy_s(newPath, MAX_PATH_AND_FILE, path);
            TCHAR* ret_code = _wfullpath(path, newPath, MAX_PATH_AND_FILE);
            if (gDebug)
            {
                swprintf_s(msgString, 1024, L"Found! Ret code %s, Converted path to %s", (ret_code ? L"OK" : L"NULL"), path);
                FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
            }
            return i + 2;
        }

        wcscpy_s(newPath, MAX_PATH_AND_FILE, L"./.");
        wcscat_s(newPath, MAX_PATH_AND_FILE, path);
        wcscpy_s(path, MAX_PATH_AND_FILE, newPath);
    }

    // failed
    if (gDebug)
    {
        FilterMessageBox(NULL, L"Failed to find path", _T("Warning"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }
    return 0;
}


// Gives the user's home directory on Mac, i.e., /users/eric, using the HOME environment variable
//static void homePathMac(TCHAR *path)
//{
//	wchar_t *home;
//	size_t len;
//	path[0] = 0x0;	// return empty string on failure
//
//	_wdupenv_s( &home, &len, L"HOME" );
//	// just to test a real env variable: _wdupenv_s( &home, &len, L"DXSDK_DIR" );
//	if ( home != NULL )
//	{
//		wcscpy_s(path,MAX_PATH_AND_FILE,home);
//		//PathAppend(path,L"Library");
//		//PathAppend(path,L"Application Support");
//		//PathAppend(path,L"minecraft");
//		//PathAppend(path,L"saves");
//		free(home);
//	}
//	//else
//	//{
//	//	// try something crazy, I really don't know what the Mac wants... TODO! Need someone who knows...
//	//	// ~/Library/Application Support/minecraft/saves/
//	//	wcscpy_s(path,MAX_PATH_AND_FILE,L"~/Library/Application Support/minecraft/saves/*");
//	//}
//}

void flagUnreadableWorld(wchar_t* wcWorld, char* charWorld, int errCode)
{
    char outputString[1024];
    sprintf_s(outputString, 1024, "      detected inability to read or corrupt world file: %s\n", charWorld);
    LOG_INFO(gExecutionLogfile, outputString);

    if (errCode < 0) {
        wchar_t msgString[1024];
        wsprintf(msgString, _T("Warning: The level.dat of world file %s appears to be missing important information. World ignored, error code %d."), wcWorld, errCode);
        FilterMessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
    }
    //else {
        // most likely it's a directory where there is no level.dat, i.e., just some other random directory, so leave off this message.
        //swprintf_s(msgString, 1024, L"Warning: The level.dat of world file %s could not be read, error code %d. World ignored. Please report this problem to erich@acm.org if you think it is in error.", wcWorld, errCode);
        //FilterMessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
    //}
}

static int loadWorldList(HMENU menu)
{
    int oldVersionDetected = 0;
    MENUITEMINFO info;
    info.cbSize = sizeof(MENUITEMINFO);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_DATA;
    info.fType = MFT_STRING;
    TCHAR saveFilesPath[MAX_PATH_AND_FILE];
    HANDLE hFind;
    WIN32_FIND_DATA ffd;

    LOG_INFO(gExecutionLogfile, "   entered loadWorldList\n");
    // first world is actually the block test world
    gNumWorlds = 1;
    memset(gWorlds, 0x0, MAX_WORLDS * sizeof(TCHAR*));

    // uncomment next line to pop up dialogs about progress - useful on Mac to see what directories it's searching through, etc.
    //gDebug = true;

    wcscpy_s(saveFilesPath, MAX_PATH_AND_FILE, gWorldPathDefault);

    wchar_t msgString[1024];
    wcscat_s(saveFilesPath, MAX_PATH_AND_FILE, gPreferredSeparatorString);
    wcscat_s(saveFilesPath, MAX_PATH_AND_FILE, L"*");
    char outputString[1024];
    char pConverted[1024];
    int length = (int)wcslen(saveFilesPath) + 1;
    WcharToChar(saveFilesPath, pConverted, length);
    sprintf_s(outputString, 1024, "    saveFilesPath %s\n", pConverted);
    LOG_INFO(gExecutionLogfile, outputString);
    hFind = FindFirstFile(saveFilesPath, &ffd);

    // did we find the directory at all?
    if (hFind == INVALID_HANDLE_VALUE)
    {
        LOG_INFO(gExecutionLogfile, "    invalid handle value - return 0. This means Mineways couldn't find your Minecraft world saves directory.\n");
        FilterMessageBox(NULL, _T("Mineways couldn't find your Minecraft world saves directory. This likely means that you're using Minecraft Bedrock edition on Windows 10. Mineways supports only Minecraft Classic (Java) world files. But, there is a free utility to convert from one to the other Minecraft world format. See http://mineways.com for more information.\n\nIf you are using Minecraft Classic (Java) and have stored your worlds elsewhere, use the 'File -> Open...' option and find your level.dat file for the world. Save files are normally at 'C:\\Users\\<your name>\\AppData\\Roaming\\.minecraft\\saves'. For Mac, worlds are usually located at '/users/<your name>/Library/Application Support/minecraft/saves'."),
            _T("Warning"), MB_OK | MB_ICONWARNING);
        return 0;
    }

    // If path is empty, don't search, worlds are not to be loaded.
    bool worldFound = true;
    if (wcslen(gWorldPathDefault) > 0) {
        // Avoid infinite loop when searching directory. This shouldn't happen, but let us be absolutely sure.
        int count = 0;
        int failedLoads = 0;
        do
        {
            // Yes, we could really count the number of actual worlds found, but just in case this is a crazy-large directory
            // cut searching short at MAX_WORLDS files of any sort.
            count++;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                length = (int)wcslen(ffd.cFileName) + 1;
                WcharToChar(ffd.cFileName, pConverted, length);
                sprintf_s(outputString, 1024, "    found potential world save directory %s\n", pConverted);
                LOG_INFO(gExecutionLogfile, outputString);

                if (gDebug)
                {
                    swprintf_s(msgString, 1024, L"Found potential world save directory %s", ffd.cFileName);
                    FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
                }

                if (ffd.cFileName[0] != '.')
                {
                    // test if world is in Anvil format
                    int version = 0;
                    TCHAR testAnvil[MAX_PATH_AND_FILE];
                    char levelName[MAX_PATH_AND_FILE];
                    wcscpy_s(testAnvil, MAX_PATH_AND_FILE, gWorldPathDefault);
                    wcscat_s(testAnvil, MAX_PATH_AND_FILE, gPreferredSeparatorString);
                    wcscat_s(testAnvil, MAX_PATH_AND_FILE, ffd.cFileName);

                    length = (int)wcslen(testAnvil) + 1;
                    WcharToChar(testAnvil, pConverted, length);
                    sprintf_s(outputString, 1024, "      trying file %s\n", pConverted);
                    LOG_INFO(gExecutionLogfile, outputString);

                    if (gDebug)
                    {
                        swprintf_s(msgString, 1024, L"Trying file %s", testAnvil);
                        FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
                    }

                LOG_INFO(gExecutionLogfile, "        try to get file version\n");
                int errCode = GetFileVersion(testAnvil, &version, gFileOpened, MAX_PATH_AND_FILE);
                if (errCode != 0) {
                    // unreadable world, for some reason - couldn't read version and LevelName
                    flagUnreadableWorld(testAnvil, pConverted, errCode);
                    // If error code >= 0, no popup is generated; it means something like "there's no level.dat", for example, so we ignore it.
                    if (errCode < 0)
                        failedLoads++;
                    continue;
                }
                LOG_INFO(gExecutionLogfile, "        try to get file level name\n");
                errCode = GetLevelName(testAnvil, levelName, MAX_PATH_AND_FILE);
                if (errCode != 0) {
                    // unreadable world, for some reason - couldn't read version and LevelName
                    flagUnreadableWorld(testAnvil, pConverted, errCode);
                    if (errCode < 0)
                        failedLoads++;
                    continue;
                }

                // Only needed for culling out files we cannot yet read, e.g., 1.18 at one point
                //LOG_INFO(gExecutionLogfile, "        try to get file version ID\n");
                //int versionID;
                //errCode = GetFileVersionId(testAnvil, &versionID);
                //if (errCode != 0) {
                //    // unreadable world, for some reason - couldn't read version and LevelName
                //    //flagUnreadableWorld(testAnvil, pConverted, errCode);
                //    //continue;
                //    // Note, some worlds do this, e.g., 1.7 and 1.8 worlds don't have a version.
                //    // So, set the versionID to 0 and move on.
                //    versionID = 0;
                //}

                /* Only needed for debug. Trying to minimize these calls, see issue https://github.com/erich666/Mineways/issues/31
                int versionId = 0;
                char versionName[MAX_PATH_AND_FILE];
                versionName[0] = (char)0;
                LOG_INFO(gExecutionLogfile, "        try to get file version id\n");
                // This is a newer tag for 1.9 and on, older worlds do not have them
                if (GetFileVersionId(testAnvil, &versionId) != 0) {
                    // older file type, does not have it.
                    LOG_INFO(gExecutionLogfile, "          pre-1.9 file type detected, so no version id, which is fine\n");
                }
                LOG_INFO(gExecutionLogfile, "        try to get file version name\n");
                if (GetFileVersionName(testAnvil, versionName, MAX_PATH_AND_FILE) != 0) {
                    // older file type, does not have it.
                    LOG_INFO(gExecutionLogfile, "          pre-1.9 file type detected, so no version name, which is fine\n");
                }
                */

                sprintf_s(outputString, 1024, "      succeeded, which has folder level name %s\n", levelName);
                LOG_INFO(gExecutionLogfile, outputString);

                if (gDebug)
                {
                    // 0 version ID means earlier than 1.9
                    swprintf_s(msgString, 1024, L"Succeeded with file %s, which has folder level name %S", testAnvil, levelName);
                    FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
                }

                info.wID = IDM_WORLD + gNumWorlds;

                // display the "given name" followed by / the world folder name; both can be useful
                TCHAR worldIDString[MAX_PATH_AND_FILE], wLevelName[MAX_PATH_AND_FILE];
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, levelName, -1, wLevelName, MAX_PATH_AND_FILE);
                wsprintf(worldIDString, L"%s\t/ %s", wLevelName, ffd.cFileName);

                info.cch = (UINT)wcslen(worldIDString);
                info.dwTypeData = worldIDString;
                info.dwItemData = gNumWorlds;
                // if version is pre-Anvil, show world but gray it out
                if (version < 19133)
                {
                    LOG_INFO(gExecutionLogfile, "   file is pre-Anvil\n");
                    oldVersionDetected = 1;
                    // gray it out
                    info.fMask |= MIIM_STATE;
                    info.fState = MFS_DISABLED;
                }
                //else if (DATA_VERSION_TO_RELEASE_NUMBER(versionID) >= 18)
                //{
                //    LOG_INFO(gExecutionLogfile, "   file is 1.18 or newer - new format is not supported yet\n");
                //    // gray it out
                //    info.fMask |= MIIM_STATE;
                //    info.fState = MFS_DISABLED;
                //}
                else
                {
                    //info.fMask |= MIIM_STATE;
                    info.fState = 0x0; // MFS_CHECKED;
                }
                // insert world above IDM_TEST_WORLD in File -> Open World... menu
                InsertMenuItem(menu, IDM_TEST_WORLD, FALSE, &info);
                size_t strLen = wcslen(testAnvil) + 1;
                gWorlds[gNumWorlds] = (TCHAR*)malloc(sizeof(TCHAR) * strLen);
                wcscpy_s(gWorlds[gNumWorlds], strLen, testAnvil);

                // was: setWorldPath(worlds[numWorlds]);
                //PathAppend(worlds[numWorlds],ffd.cFileName);
                gNumWorlds++;
                sprintf_s(outputString, 1024, "    gNumWorlds is now %d\n", gNumWorlds);
                LOG_INFO(gExecutionLogfile, outputString);
                }
            }
        } while (((worldFound = FindNextFile(hFind, &ffd)) != 0) && (gNumWorlds < MAX_WORLDS));

        // Any bad worlds detected? Give advice
        if (failedLoads) {
            // prints the line in the code where the error was returned via (a negated) LINE_ERROR.
            wsprintf(msgString, L"The world%S not loaded may be in a different ('Bedrock') format, used by Minecraft for Windows 10. Click 'OK' to go to https://bit.ly/mcbedrock and follow the instructions there to convert this type of world to the 'Classic' Java format, which Mineways can read. If instead this world is from an early version of Classic Minecraft, load it into the latest Minecraft to convert it. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information.", (failedLoads > 1) ? L"s" : L"");

            int retcode = FilterMessageBox(NULL, msgString,
                _T("Read error"), MB_OKCANCEL | MB_ICONWARNING | MB_TOPMOST);
            GoToBedrockHelpOnOK(retcode);

        }
    }
    FindClose(hFind);

    // did the search stop due to running out of room for worlds?
    char charMsgString[1024];
    if (worldFound)
    {
        sprintf_s(charMsgString, 1024, "Warning: more that %d files detected. Not all worlds have been added to the Open World list.\n", MAX_WORLDS);
        LOG_INFO(gExecutionLogfile, charMsgString);
        swprintf_s(msgString, 1024, L"Warning: more that %d files detected in %s. Not all worlds have been added to the Open World list.", MAX_WORLDS, saveFilesPath);
        FilterMessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
        gNumWorlds = MAX_WORLDS - 1;
    }
    else if (gNumWorlds <= 1) {
        sprintf_s(charMsgString, 1024, "Warning: Mineways found your save directory, but no Minecraft Classic (Java) saved worlds were found. Perhaps you are using Minecraft Windows 10 Bedrock edition instead of Minecraft Classic (Java)? You can convert your worlds from Bedrock to Classic format. See http://mineways.com for instructions on how to convert.\n");
        LOG_INFO(gExecutionLogfile, charMsgString);
        swprintf_s(msgString, 1024, L"Warning: Mineways found your save directory, but no Minecraft Classic (Java) saved worlds were found. Perhaps you are using Minecraft Windows 10 Bedrock edition instead of Minecraft Classic (Java)? You can convert your worlds from from Bedrock to Classic format. See http://mineways.com for instructions on how to convert.\n");
        FilterMessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
    }

    return oldVersionDetected;
}

static int loadTerrainList(HMENU menu)
{
    MENUITEMINFO info;
    info.cbSize = sizeof(MENUITEMINFO);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_DATA;
    info.fType = MFT_STRING;
    HANDLE hFind;
    WIN32_FIND_DATA ffd;

    LOG_INFO(gExecutionLogfile, "   entered loadTerrainList\n");
    // first terrain file is the default terrainExt.png (built in, if missing)
    gNumTerrainFiles = 1;
    memset(gTerrainFiles, 0x0, MAX_TERRAIN_FILES * sizeof(TCHAR*));

    // uncomment next line to pop up dialogs about progress - useful on Mac to see what directories it's searching through, etc.
    //gDebug = true;

    wchar_t terrainSearch[MAX_PATH_AND_FILE];
    wchar_t msgString[1024];
    char outputString[1024];
    char pConverted[1024];
    wcscpy_s(terrainSearch, MAX_PATH_AND_FILE, gExeDirectory);
    wcscat_s(terrainSearch, MAX_PATH_AND_FILE - wcslen(terrainSearch), gPreferredSeparatorString);
    wcscat_s(terrainSearch, MAX_PATH_AND_FILE - wcslen(terrainSearch), L"terrainExt*.png");
    hFind = FindFirstFile(terrainSearch, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        // done - no files found
        return 0;
    }
    size_t length = (int)wcslen(terrainSearch) + 1;
    WcharToChar(terrainSearch, pConverted, (int)length);
    sprintf_s(outputString, 1024, "    looking for files %s\n", pConverted);
    LOG_INFO(gExecutionLogfile, outputString);

    // Avoid infinite loop when searching directory. This shouldn't happen, but let us be absolutely sure.
    int count = 0;
	do {
		// Yes, we could really count the number of actual terrain files found, but just in case this is a crazy-large directory
		// cut searching short at MAX_TERRAIN_FILES files of any sort.
		count++;
        // unlikely, but ignore anything that looks like a directory
		if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			length = (int)wcslen(ffd.cFileName) + 1;
			WcharToChar(ffd.cFileName, pConverted, (int)length);
			sprintf_s(outputString, 1024, "    found potential terrain file %s\n", pConverted);
			LOG_INFO(gExecutionLogfile, outputString);

			if (gDebug)
			{
				swprintf_s(msgString, 1024, L"Found potential terrain file %s", ffd.cFileName);
				FilterMessageBox(NULL, msgString, _T("Informational"), MB_OK | MB_ICONINFORMATION);
			}

            length = wcslen(ffd.cFileName);
            // long enough to have a suffix and .png?
			if (length >= 6)
			{
				info.wID = IDM_DEFAULT_TERRAIN + gNumTerrainFiles;
                // back up 6 characters, e.g., _s.png
                wchar_t* tileSuffix = ffd.cFileName + length - 6;

				// display the terrain file name
				// For now, show the whole PNG. Could make it fancier, but this is clear.
                // Remove suffixed names from display
                if ((_wcsicmp(tileSuffix, L"_n.png") != 0) &&
                    (_wcsicmp(tileSuffix, L"_r.png") != 0) &&
                    (_wcsicmp(tileSuffix, L"_m.png") != 0) &&
                    (_wcsicmp(tileSuffix, L"_e.png") != 0)) {

                    info.cch = (UINT)wcslen(ffd.cFileName);
                    info.dwTypeData = ffd.cFileName;
                    info.dwItemData = gNumTerrainFiles;
                    //info.fMask |= MIIM_STATE;
                    info.fState = 0x0; // MFS_CHECKED;

                    InsertMenuItem(menu, IDM_DEFAULT_TERRAIN, FALSE, &info);
                    gTerrainFiles[gNumTerrainFiles] = (TCHAR*)malloc(sizeof(TCHAR) * MAX_PATH_AND_FILE);
                    wcscpy_s(gTerrainFiles[gNumTerrainFiles], MAX_PATH_AND_FILE, ffd.cFileName);
                    gNumTerrainFiles++;
                    sprintf_s(outputString, 1024, "    gNumTerrainFiles is now %d\n", gNumTerrainFiles);
                    LOG_INFO(gExecutionLogfile, outputString);
                }
			}
		}
	} while ((FindNextFile(hFind, &ffd) != 0) && (gNumTerrainFiles < MAX_TERRAIN_FILES));
	FindClose(hFind);

    // did the search stop due to running out of room for terrain files?
    if (gNumTerrainFiles >= MAX_TERRAIN_FILES)
    {
        char charMsgString[1024];
        sprintf_s(charMsgString, 1024, "Warning: more that %d terrain files detected. Not all terrain files have been added to the list.\n", MAX_TERRAIN_FILES);
        LOG_INFO(gExecutionLogfile, charMsgString);
        swprintf_s(msgString, 1024, L"Warning: more that %d terrain files detected. Not all terrain files have been added to the list.", MAX_TERRAIN_FILES);
        FilterMessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
        gNumTerrainFiles = MAX_TERRAIN_FILES - 1;
    }

    return 0;
}

static void enableBottomControl(int state, /* HWND hwndBottomSlider, HWND hwndBottomLabel, */ HWND hwndInfoBottomLabel)
{
    if (state != gBottomControlEnabled)
    {
        gBottomControlEnabled = state;
        // Currently only the label "depth" is affected by selection;
        // in this way the depth can be modified even if selection is not on. The problem with an inactive
        // slider is that the map window underneath is then active instead, and you'll drag the map.
        //EnableWindow(hwndBottomSlider,state);
        //EnableWindow(hwndBottomLabel,state);
        // or to include arguments but avoid warnings:
        //hwndBottomSlider;
        //hwndBottomLabel;
        EnableWindow(hwndInfoBottomLabel, state);
    }
}


// validate menu items
static void validateItems(HMENU menu)
{
    // gray out options that are not available
    if (gLoaded)
    {
        EnableMenuItem(menu, IDM_JUMPSPAWN, MF_ENABLED);
        EnableMenuItem(menu, IDM_JUMPPLAYER, MF_ENABLED);
        EnableMenuItem(menu, IDM_VIEW_JUMPTOMODEL, MF_ENABLED);
        EnableMenuItem(menu, IDM_FOCUSVIEW, MF_ENABLED);
        EnableMenuItem(menu, IDM_VIEW_INFORMATION, MF_ENABLED);
        EnableMenuItem(menu, IDM_FILE_SAVEOBJ, MF_ENABLED);
        EnableMenuItem(menu, IDM_FILE_PRINTOBJ, MF_ENABLED);
        EnableMenuItem(menu, IDM_FILE_SCHEMATIC, MF_ENABLED);
        EnableMenuItem(menu, IDM_FILE_EXPORTMAP, MF_ENABLED);
#ifdef SKETCHFAB
        EnableMenuItem(menu, IDM_PUBLISH_SKFB, MF_ENABLED);
#else
        EnableMenuItem(menu, IDM_PUBLISH_SKFB, MF_DISABLED);
#endif
        EnableMenuItem(menu, ID_VIEW_UNDOSELECTION, UndoHighlightExists() ? MF_ENABLED : MF_DISABLED);
    }
    else
    {
        EnableMenuItem(menu, IDM_JUMPSPAWN, MF_DISABLED);
        EnableMenuItem(menu, IDM_JUMPPLAYER, MF_DISABLED);
        EnableMenuItem(menu, IDM_VIEW_JUMPTOMODEL, MF_DISABLED);
        EnableMenuItem(menu, IDM_FOCUSVIEW, MF_DISABLED);
        EnableMenuItem(menu, IDM_VIEW_INFORMATION, MF_DISABLED);
        EnableMenuItem(menu, IDM_FILE_SAVEOBJ, MF_DISABLED);
        EnableMenuItem(menu, IDM_FILE_PRINTOBJ, MF_DISABLED);
        EnableMenuItem(menu, IDM_FILE_SCHEMATIC, MF_DISABLED);
        EnableMenuItem(menu, IDM_FILE_EXPORTMAP, MF_DISABLED);
        EnableMenuItem(menu, IDM_PUBLISH_SKFB, MF_DISABLED);
        EnableMenuItem(menu, ID_VIEW_UNDOSELECTION, MF_DISABLED);
    }
    // has a save been done?
    if (gExported)
    {
        EnableMenuItem(menu, IDM_FILE_REPEATPREVIOUSEXPORT, MF_ENABLED);
    }
    else
    {
        EnableMenuItem(menu, IDM_FILE_REPEATPREVIOUSEXPORT, MF_DISABLED);
    }
}

static void setSlider(HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth, bool update)
{
    syncCurrentHighlightDepth();

    wchar_t text[NUM_STR_SIZE];
    SendMessage(hwndSlider, TBM_SETPOS, 1, gMaxHeight - depth);
    _itow_s(depth, text, NUM_STR_SIZE);
    SetWindowText(hwndLabel, text);
    if (update)
    {
        drawInvalidateUpdate(hWnd);
    }
}

static void drawInvalidateUpdate(HWND hWnd)
{
    drawTheMap();
    InvalidateRect(hWnd, NULL, FALSE);
    UpdateWindow(hWnd);
}


static void syncCurrentHighlightDepth()
{
    // changing the depth 
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
    // wherever the target depth is, put it back in that place and replace the other
    if (maxy == gTargetDepth)
    {
        // the rare case, where target depth is larger than current depth
        SetHighlightState(on, minx, gCurDepth, minz, maxx, gTargetDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    }
    else
    {
        SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    }
}

// So that you can change some options in Sculpteo export and they get used for
// Shapeways export, and vice versa. Also works for STL. A few parameters are
// specific to the service, e.g. Z is up, and unit type used, so these are not copied.
static void copyOverExportPrintData(ExportFileData* pEFD)
{
    if (pEFD == NULL) {
        MY_ASSERT(gAlwaysFail);
        return;
    }
    // Whatever is set for OBJ should be set for VRML, and vice versa,
    // so that Sculpteo and Shapeways can both be exported off a single setting, set once.
    // Similarly, do this for all the STL.
    int source = pEFD->fileType;
    int dest[2];
    int count = 0;
    int service = 0;
    int copyMtl[2];
    copyMtl[0] = copyMtl[1] = 1;

    switch (source)
    {
    case FILE_TYPE_WAVEFRONT_ABS_OBJ:
        dest[0] = FILE_TYPE_WAVEFRONT_REL_OBJ;
        dest[1] = FILE_TYPE_VRML2;
        count = 2;
        service = 1;
        break;
    case FILE_TYPE_WAVEFRONT_REL_OBJ:
        dest[0] = FILE_TYPE_WAVEFRONT_ABS_OBJ;
        dest[1] = FILE_TYPE_VRML2;
        count = 2;
        service = 1;
        break;
    case FILE_TYPE_USD:
        // just ignore, we don't really use USD with print anyway
        break;
    case FILE_TYPE_VRML2:
        dest[0] = FILE_TYPE_WAVEFRONT_ABS_OBJ;
        dest[1] = FILE_TYPE_WAVEFRONT_REL_OBJ;
        count = 2;
        service = 1;
        break;

    case FILE_TYPE_BINARY_MAGICS_STL:
        dest[0] = FILE_TYPE_BINARY_VISCAM_STL;
        dest[1] = FILE_TYPE_ASCII_STL;
        // do not copy the setting from color binary to no-color ascii
        copyMtl[1] = 0;
        count = 2;
        break;
    case FILE_TYPE_BINARY_VISCAM_STL:
        dest[0] = FILE_TYPE_BINARY_MAGICS_STL;
        dest[1] = FILE_TYPE_ASCII_STL;
        // do not copy the setting from color binary to no-color ascii
        copyMtl[1] = 0;
        count = 2;
        break;
    case FILE_TYPE_ASCII_STL:
        dest[0] = FILE_TYPE_BINARY_MAGICS_STL;
        dest[1] = FILE_TYPE_BINARY_VISCAM_STL;
        // do not copy the setting from no-color ascii to color binary
        copyMtl[0] = copyMtl[1] = 0;
        count = 2;
        break;
    case FILE_TYPE_SCHEMATIC:
        // just ignore, we don't really share a thing with print, etc.
        break;
    default:
        // unknown, don't copy
        MY_ASSERT(gAlwaysFail);
        return;
    }

    // copy!
    for (int i = 0; i < count; i++)
    {
        if (copyMtl[i])
        {
            pEFD->radioExportNoMaterials[dest[i]] = pEFD->radioExportNoMaterials[source];
            pEFD->radioExportMtlColors[dest[i]] = pEFD->radioExportMtlColors[source];
            pEFD->radioExportSolidTexture[dest[i]] = pEFD->radioExportSolidTexture[source];
            pEFD->radioExportFullTexture[dest[i]] = pEFD->radioExportFullTexture[source];
            pEFD->radioExportTileTextures[dest[i]] = pEFD->radioExportTileTextures[source];
        }

        // don't adjust Z up if (Sculpteo vs. Shapeways) specific, i.e. if service is true
        if (service == 0)
            pEFD->chkMakeZUp[dest[i]] = pEFD->chkMakeZUp[source];

        pEFD->blockSizeVal[dest[i]] = pEFD->blockSizeVal[source];

        // don't do, as this seems file type specific: chkCreateZip[FILE_TYPE_TOTAL];
        // don't do, as this seems file type specific: chkCreateModelFiles[FILE_TYPE_TOTAL];	// i.e. don't delete them at end

        pEFD->hollowThicknessVal[dest[i]] = pEFD->hollowThicknessVal[source];

        // don't do, as Sculpteo sandstone stats are different than Shapeways':
        if (service == 0) {
            pEFD->comboPhysicalMaterial[dest[i]] = pEFD->comboPhysicalMaterial[source];
            // for service, don't do, as Sculpteo and Shapeways use centimeters vs. millimeters
            pEFD->comboModelUnits[dest[i]] = pEFD->comboModelUnits[source];
        }
    }
}

#ifdef SKETCHFAB

static bool isSketchfabFieldSizeValid(char* field, int size, bool exact)
{
    int length = (int)strlen(field);
    if (length > size)
    {
        return false;
    }
    if (exact && length != size)
    {
        return false;
    }

    return true;
}

static bool commandSketchfabPublish(ImportedSet& is, wchar_t* error)
{
    if (!gHighlightOn)
    {
        // we keep the export options ungrayed now so that they're selectable when the world is loaded
        swprintf_s(error, 1024, L"no volume is selected for export; click and drag using the right-mouse button.");
        return false;
    }

    // Report export type and selection
    gPrintModel = SKETCHFAB_EXPORT;
    is.pEFD->minxVal = is.minxVal;
    is.pEFD->minyVal = is.minyVal;
    is.pEFD->minzVal = is.minzVal;
    is.pEFD->maxxVal = is.maxxVal;
    is.pEFD->maxyVal = is.maxyVal;
    is.pEFD->maxzVal = is.maxzVal;
    *is.pSaveEFD = *is.pEFD;
    gpEFD = is.pSaveEFD;
    gOptions.pEFD = gpEFD;

    LPTSTR filepath = prepareSketchfabExportFile(is.ws.hWnd);
    setSketchfabExportSettings();
    processSketchfabExport(&gSkfbPData, filepath, gSelectTerrainPathAndName, gSchemeSelected);
    setPublishSkfbData(&gSkfbPData);
    uploadToSketchfab(hInst, is.ws.hWnd);

    // back to normal
    sendStatusMessage(is.ws.hwndStatus, RUNNING_SCRIPT_STATUS_MESSAGE);

    SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    enableBottomControl(1, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);
    // put target depth to new depth set, if any
    if (gTargetDepth != gpEFD->maxyVal)
    {
        gTargetDepth = gpEFD->minyVal;
    }
    //gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(gHoldlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
    //	bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
    //updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
    setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
    setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, true);

    return true;
}

static int setSketchfabExportSettings()
{
    // export all elements for Skfb
    gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN |
        BLF_FLATTEN_SMALL | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD;

    // Set options for Sketchfab publication. Need to determine best settings here, the user will not have the choice
    gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_SKFB;	// TODO - what happens if per tile is done? Not really an option now, but could be nice from a mipmapping aspect

    gOptions.exportFlags |=
        (gpEFD->chkHollow[gpEFD->fileType] ? EXPT_HOLLOW_BOTTOM : 0x0) |
        ((gpEFD->chkHollow[gpEFD->fileType] && gpEFD->chkSuperHollow[gpEFD->fileType]) ? EXPT_HOLLOW_BOTTOM | EXPT_SUPER_HOLLOW_BOTTOM : 0x0);

    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES; // export groups
    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK; // the norm, instead of single material
    gOptions.exportFlags |= EXPT_OUTPUT_CUSTOM_MATERIAL; // Full material (output the extra values)
    gOptions.exportFlags |= EXPT_OUTPUT_TEXTURE_IMAGES; // Export block full texture
    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_REL_COORDINATES; // OBj relative coordinates
    gOptions.exportFlags |= EXPT_BIOME; // Use biome for export. Currently only the biome at the center of the zone is supported

    return 0;
}

static LPTSTR prepareSketchfabExportFile(HWND hWnd)
{
    OPENFILENAME ofn;
    // Force it to be an rendering export: Relative obj
    gPrintModel = SKETCHFAB_EXPORT;
    // done by InitializeSketchfabExportData
    //gExportSketchfabData.fileType = FILE_TYPE_WAVEFRONT_REL_OBJ;
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = gExportPath;
    ofn.nMaxFile = MAX_PATH_AND_FILE;

    ofn.nFilterIndex = gExportSketchfabData.fileType + 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = L"Publish to Sketchfab";

    // Write zip in temp directory
    LPTSTR tempdir = new TCHAR[MAX_PATH_AND_FILE];
    LPTSTR filepath = new TCHAR[MAX_PATH_AND_FILE];
    GetTempPath(MAX_PATH_AND_FILE, tempdir);

    // OSX workaround since tempdir will not be ok
    // TODO: find a better way to detect OSX + Wine vs Windows
    if (!PathFileExists(tempdir))
    {
        swprintf_s(tempdir, MAX_PATH_AND_FILE, L"\\tmp\\");
    }
    swprintf_s(filepath, MAX_PATH_AND_FILE, L"%sMineways2Skfb", tempdir);

    delete[] tempdir;

    return filepath;
}

void initializeOutputFileList(FileList& outputFileList)
{
    outputFileList.count = 0;
    for (int i = 0; i < MAX_OUTPUT_FILES; i++)
    {
        outputFileList.name[i] = NULL;
    }
    // we don't really need to alloc here, but this is a bit of future proofing, in case
    // the output file list gets used by some method other than addOutputFilenameToList.
    // If uncommented, outputFileList should then add an "allocated count" vs. a count of real file names on the list.
    //for (int i = 0; i < 10; i++)
    //{
    //    outputFileList.name[i] = (wchar_t*)malloc(MAX_PATH_AND_FILE * sizeof(wchar_t));
    //}
}

void freeOutputFileList(FileList& outputFileList)
{
    for (int i = 0; i < outputFileList.count; i++)
    {
        free(outputFileList.name[i]);
    }
    outputFileList.count = 0;
}

// return a failure code (simply "1" at this point, since it pops up its own error) on failure, else return 0 on success.
static int processSketchfabExport(PublishSkfbData* skfbPData, wchar_t* objFileName, wchar_t* terrainFileName, wchar_t* schemeSelected)
{
    FileList outputFileList;
    initializeOutputFileList(outputFileList);

    gpEFD->radioScaleToHeight = 1;
    gpEFD->radioScaleByCost = 0;
    gpEFD->chkCreateModelFiles[gpEFD->fileType] = 0;

    int errCode = SaveVolume(objFileName, gpEFD->fileType, &gOptions, &gWorldGuide, gExeDirectory,
        gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight,
        updateProgress, terrainFileName, schemeSelected, &outputFileList, (int)gMinewaysMajorVersion, (int)gMinewaysMinorVersion, gVersionID, gChangeBlockCommands,
        gInstanceChunkSize, gBiomeSelected, gGroupCount, gGroupCountSize, gGroupCountArray);

    deleteCommandBlockSet(gChangeBlockCommands);
    gChangeBlockCommands = NULL;

    wchar_t wcZip[MAX_PATH_AND_FILE];

    if (errCode < MW_BEGIN_ERRORS) {
        assert(outputFileList.count > 0);

        // first name is always the output file name
        swprintf_s(wcZip, MAX_PATH_AND_FILE, L"%s.zip", outputFileList.name[0]);
        DeleteFile(wcZip);

        HZIP hz = CreateZip(wcZip, 0, ZIP_FILENAME);

        for (int i = 0; i < outputFileList.count; i++)
        {
            const wchar_t* nameOnly = removePath(outputFileList.name[i]);

            if (updateProgress != NULL)
            {
                (*updateProgress)(0.90f + 0.10f * (float)i / (float)outputFileList.count, NULL);
            }

            ZipAdd(hz, nameOnly, outputFileList.name[i], 0, ZIP_FILENAME);

            // delete model files if not needed
            if (!gpEFD->chkCreateModelFiles[gpEFD->fileType])
            {
                DeleteFile(outputFileList.name[i]);
            }
        }
        CloseZip(hz);
    }

    if (updateProgress != NULL)
        (*updateProgress)(1.0f,NULL);

    if (errCode != MW_NO_ERROR)
    {
        PopupErrorDialogs(errCode);
    }

    // Set filepath to skfb data
    if (errCode < MW_BEGIN_ERRORS) {
        // Sketchfab currently uses only char paths, so we must convert.
        // Make the char string twice as long as the wchar_t string should be sufficient?
        // see https://github.com/erich666/Mineways/issues/90
        char filepath[MAX_PATH_AND_FILE * 2];
        size_t retSize;
        setlocale(LC_ALL, "");
        wcstombs_s(&retSize, filepath, MAX_PATH_AND_FILE*2, wcZip, MAX_PATH_AND_FILE*2-1);
        setlocale(LC_ALL, "C");
        if (retSize == 0) {
            // abort - conversion did not work
            wchar_t errbuf[1024];
            wsprintf(errbuf, L"ERROR: I am afraid you have a path name \"%s\" that cannot be converted to a multi-byte character path that Sketchfab can use. If you want to upload to Sketchfab, you will have to do it manually: Export for Rendering, check the \"Create a ZIP file\" box, and then upload the resulting zip file.", wcZip);
            FilterMessageBox(NULL, errbuf, _T("File Path Error"), MB_OK | MB_ICONERROR);

            // clean up - sloppy code, I admit
            freeOutputFileList(outputFileList);
            return 1;
        }
        skfbPData->skfbFilePath = filepath;
    }

    freeOutputFileList(outputFileList);

    return 0;
}

static int publishToSketchfab(HWND hWnd, wchar_t* objFileName, wchar_t* terrainFileName, wchar_t* schemeSelected)
{
    int on;
    int retCode = 0;

    PublishSkfbData* skfbPData = &gSkfbPData;
    // set 'export for rendering' settings
    gpEFD = &gExportSketchfabData;
    gOptions.exportFlags = 0x0;
    gpEFD->flags = 0x0;

    gOptions.pEFD = gpEFD;

    // get selected zone bounds
    GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal, gMinHeight);

    int miny = gpEFD->minyVal;
    int maxy = gpEFD->maxyVal;

    // set epd in skfbPDdata data
    setPublishSkfbData(skfbPData);

    // Open dialog and get user data
    if (!doPublishSkfb(hInst, hWnd))
    {
        return 0;
    }
    // Get updated version of export data (api token, etc)
    getPublishSkfbData(skfbPData);

    // if user changed depths
    if (miny != gpEFD->minyVal || maxy != gpEFD->maxyVal)
    {
        // see if target did not change
        if (gTargetDepth <= gCurDepth)
        {
            gTargetDepth = gpEFD->minyVal;
            gCurDepth = gpEFD->maxyVal;
        }
        else
        {
            gTargetDepth = gpEFD->maxyVal;
            gCurDepth = gpEFD->minyVal;
        }
    }

    // get zone bounds
    SetHighlightState(on, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    setSketchfabExportSettings();

    // Generate files
    if (on) {

        drawInvalidateUpdate(hWnd);

        if (skfbPData->skfbFilePath.empty()) {
            if (processSketchfabExport(skfbPData, objFileName, terrainFileName, schemeSelected) == 0) {
                // no error, do it.
                setPublishSkfbData(skfbPData);
            }
            else {
                // finish progress meter early
                if (updateProgress != NULL)
                    (*updateProgress)(0.0f,NULL);
                return retCode;
            }
        }

        uploadToSketchfab(hInst, hWnd);
        if (updateProgress != NULL)
            (*updateProgress)(0.0f,NULL);
    }

    return retCode;
}
#endif

static void addChangeBlockCommandsToGlobalList(ImportedSet& is)
{
    // if we imported block commands, add them to existing list, if any
    if (is.pCBChead != NULL) {
        if (gChangeBlockCommands != NULL) {
            // need to find end of list and add to it.
            ChangeBlockCommand* pCBC = gChangeBlockCommands;
            while (pCBC->next != NULL) {
                pCBC = pCBC->next;
            }
            // add to end of list
            pCBC->next = is.pCBChead;
        }
        else {
            gChangeBlockCommands = is.pCBChead;
        }
        // note it's transferred
        is.pCBChead = is.pCBClast = NULL;
    }
}

static wchar_t gCommaString1[100];
static wchar_t gCommaString2[100];
static wchar_t gCommaString3[100];
static wchar_t gCommaString4[100];
static wchar_t gCommaString5[100];
static wchar_t gCommaString6[100];
static wchar_t* formatWithCommas(int value, wchar_t *str)
{
    if (value < 1000) {
        swprintf_s(str, 100, L"%d", value);
    }
    else {
        int v1 = value / 1000;
        int v0 = value % 1000;
        if (v1 < 1000) {
            swprintf_s(str, 100, L"%d,%03d", v1, v0);
        }
        else {
            int v2 = v1 / 1000;
            v1 = v1 % 1000;
            if (v2 < 1000) {
                swprintf_s(str, 100, L"%d,%03d,%03d", v2, v1, v0);
            }
            else {
                // high as we go
                int v3 = v2 / 1000;
                v2 = v2 % 1000;
                //if (v3 < 1000) {
                    swprintf_s(str, 100, L"%d,%03d,%03d,%03d", v3, v2, v1, v0);
                //}
            }
        }
    }
    return str;
}

// returns number of files written on successful export, 0 files otherwise.
// showDialog says whether to show the export options dialog or not.
// TODO: Warning	C6262	Function uses '56760' bytes of stack.Consider moving some data to heap.
static int saveObjFile(HWND hWnd, wchar_t* objFileName, int printModel, wchar_t* terrainFileName, wchar_t* schemeSelected, bool showDialog, bool showStatistics)
{
    int on;
    int retCode = 0;

    if (printModel == SKETCHFAB_EXPORT)
    {
        // sketchfab file - treat sort of like a render
        gpEFD = &gExportSketchfabData;
        gOptions.exportFlags = 0x0;
        gpEFD->flags = 0x0;
    }
    else if (printModel == SCHEMATIC_EXPORT)
    {
        // schematic file - treat sort of like a render
        gpEFD = &gExportSchematicData;
        gOptions.exportFlags = 0x0;
        gpEFD->flags = 0x0;
    }
    else if (printModel == PRINTING_EXPORT)
    {
        // print
        gpEFD = &gExportPrintData;
        gOptions.exportFlags = EXPT_3DPRINT;
        gpEFD->flags = EXPT_3DPRINT;
    }
    else
    {
        MY_ASSERT(printModel == RENDERING_EXPORT);
        // render
        gpEFD = &gExportViewData;
        gOptions.exportFlags = 0x0;
        gpEFD->flags = 0x0;
    }
    gOptions.pEFD = gpEFD;

    // to use a preset set of values above, set this true, or break here and jump to line after "if"
    static int gDebugSetBlock = 0;
    if (gDebugSetBlock)
        SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);

    // normal output
    GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal, gMinHeight);

    int miny = gpEFD->minyVal;
    int maxy = gpEFD->maxyVal;

    setExportPrintData(gpEFD);

    if (showDialog && !doExportPrint(hInst, hWnd))
    {
        // canceled, so cancel output
        return 0;
    }

    getExportPrintData(gpEFD);

    copyOverExportPrintData(gpEFD);

    // if user changed depths
    if (miny != gpEFD->minyVal || maxy != gpEFD->maxyVal)
    {
        // see if target did not change
        if (gTargetDepth <= gCurDepth)
        {
            gTargetDepth = gpEFD->minyVal;
            gCurDepth = gpEFD->maxyVal;
        }
        else
        {
            gTargetDepth = gpEFD->maxyVal;
            gCurDepth = gpEFD->minyVal;
        }
    }
    SetHighlightState(on, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);

    // export all
    if (gpEFD->chkExportAll)
    {
        if (printModel == 1)
        {
            // 3d printing
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN |
                BLF_FLATTEN_SMALL | BLF_3D_BIT;
        }
        else
        {
            // rendering or schematic
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN |
                BLF_FLATTEN_SMALL | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD;
        }
    }
    else
    {
        gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN | BLF_FLATTEN_SMALL;
    }

    // export options
    if (gpEFD->radioExportMtlColors[gpEFD->fileType] == 1)
    {
        // if color output is specified, we *must* put out multiple objects, each with its own material
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if (gpEFD->radioExportSolidTexture[gpEFD->fileType] == 1)
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ((gpEFD->radioExportFullTexture[gpEFD->fileType] == 1) ||
        // if fifth option is chosen but not an OBJ file, ignore it and just make it a "full texture"
        (!(gpEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || gpEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ) && (gpEFD->radioExportTileTextures[gpEFD->fileType] == 1)))
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
        // TODO: if we're *viewing* full textures, output all the billboards!
        //  gOptions.saveFilterFlags |= BLF_SMALL_BILLBOARD;
    } // one more option, per tile with OBJ, comes later on down

    gOptions.exportFlags |=
        (gpEFD->chkFillBubbles ? EXPT_FILL_BUBBLES : 0x0) |
        ((gpEFD->chkFillBubbles && gpEFD->chkSealEntrances) ? EXPT_FILL_BUBBLES | EXPT_SEAL_ENTRANCES : 0x0) |
        ((gpEFD->chkFillBubbles && gpEFD->chkSealSideTunnels) ? EXPT_FILL_BUBBLES | EXPT_SEAL_SIDE_TUNNELS : 0x0) |

        (gpEFD->chkConnectParts ? EXPT_CONNECT_PARTS : 0x0) |
        // is it better to force part connection on if corner tips or edges are on? I feel like the
        // dialog should take care of this, not allowing these options if the controlling algorithm is set.
        (gpEFD->chkConnectCornerTips ? (EXPT_CONNECT_PARTS | EXPT_CONNECT_CORNER_TIPS) : 0x0) |
        (gpEFD->chkConnectAllEdges ? (EXPT_CONNECT_PARTS | EXPT_CONNECT_ALL_EDGES) : 0x0) |
        (gpEFD->chkDeleteFloaters ? EXPT_DELETE_FLOATING_OBJECTS : 0x0) |

        (gpEFD->chkHollow[gpEFD->fileType] ? EXPT_HOLLOW_BOTTOM : 0x0) |
        ((gpEFD->chkHollow[gpEFD->fileType] && gpEFD->chkSuperHollow[gpEFD->fileType]) ? EXPT_HOLLOW_BOTTOM | EXPT_SUPER_HOLLOW_BOTTOM : 0x0) |

        // materials are forced on if using debugging mode - just an internal override, doesn't need to happen in dialog.
        (gpEFD->chkShowParts ? EXPT_DEBUG_SHOW_GROUPS | EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK : 0x0) |
        (gpEFD->chkShowWelds ? EXPT_DEBUG_SHOW_WELDS | EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK : 0x0);

    //

    // set OBJ group and material output state
    if (gpEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || gpEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
    {
        // Export separate types?
        if (gpEFD->chkSeparateTypes)
        {
            MY_ASSERT(gpEFD->chkIndividualBlocks[gpEFD->fileType] == 0);
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES;

            // Material per block?
            if (gpEFD->chkMaterialPerFamily)
            {
                gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK;
            }
        }
        else if (gpEFD->chkIndividualBlocks[gpEFD->fileType])
        {
            // these must be on for individual block export, plus grouping by block (separate material for each block)
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_INDIVIDUAL_BLOCKS | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK ;
            if (gpEFD->chkMaterialPerFamily == 1)
            {
                gOptions.exportFlags |= EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
            }
        }

        // "Split by block type"?
        if (gpEFD->chkSplitByBlockType)
        {
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE;
        }

        // make each material group an object, too?
        if (gpEFD->chkMakeGroupsObjects)
        {
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MAKE_GROUPS_OBJECTS;
        }
        if (gpEFD->chkCustomMaterial[gpEFD->fileType])
        {
            // if G3D is chosen, we output the full material
            gOptions.exportFlags |= EXPT_OUTPUT_CUSTOM_MATERIAL;
            //if (gOptions.exportFlags & (EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_TEXTURE_SWATCHES))
            //{
            //    // G3D - use this option only if textures are on.
            //    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL;
            //}
        }
        // if in debugging mode, force groups and material type

        // check if we're exporting relative coordinates
        if (gpEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
        {
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_REL_COORDINATES;
        }
        // we set tile export options last, as these override some of those above, and can be turned off by others.
        if (gpEFD->radioExportTileTextures[gpEFD->fileType] == 1)
        {
            // Tile export must have these
            gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
                // Old requirements - handy for toggling and testing:
                // we must export a material per block (a single material is impossible)
                //EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK |
                // for tiles we also need to track the material changes within a block family or type
                //EXPT_OUTPUT_OBJ_SEPARATE_TYPES |
                // we also need this one, to make sure different materials within a block get tracked
                //EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE;
            ;
        }
    }

    // set USD
    else if (gpEFD->fileType == FILE_TYPE_USD)
    {
        // TODOUSD - this will evolve as I understand USD better
        if (gpEFD->chkIndividualBlocks[gpEFD->fileType])
        {
            // these must be on for individual block export, plus grouping by block (separate material for each block)
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_INDIVIDUAL_BLOCKS | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK;
            if (gpEFD->chkMaterialPerFamily == 1)
            {
                gOptions.exportFlags |= EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
            }
        }
        // we set tile export options last, as these override some of those above, and can be turned off by others.
        if (gpEFD->radioExportTileTextures[gpEFD->fileType] == 1)
        {
            // Tile export must have these
            gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
            // Old requirements - handy for toggling and testing:
            // we must export a material per block (a single material is impossible)
            //EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK |
            // for tiles we also need to track the material changes within a block family or type
            //EXPT_OUTPUT_OBJ_SEPARATE_TYPES |
            // we also need this one, to make sure different materials within a block get tracked
            //EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE;
            ;
        }
        if (gpEFD->chkCustomMaterial[gpEFD->fileType])
        {
            // if G3D is chosen, we output the full material
            gOptions.exportFlags |= EXPT_OUTPUT_CUSTOM_MATERIAL;
        }
        if (gpEFD->chkExportMDL)
        {
            gOptions.exportFlags |= EXPT_EXPORT_MDL;
        }
    }
    // STL files never need grouping by material, and certainly don't export textures
    else if (gpEFD->fileType == FILE_TYPE_ASCII_STL)
    {
        int unsupportedCodes = (EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE |
            EXPT_DEBUG_SHOW_GROUPS | EXPT_DEBUG_SHOW_WELDS);
        if (gOptions.exportFlags & unsupportedCodes)
        {
            FilterMessageBox(NULL, _T("Note: color output is not supported for ASCII text STL.\nFile will contain no colors."),
                _T("Informational"), MB_OK | MB_ICONINFORMATION);
        }
        // ASCII STL in particular cannot export any materials at all.
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ((gpEFD->fileType == FILE_TYPE_BINARY_MAGICS_STL) || (gpEFD->fileType == FILE_TYPE_BINARY_VISCAM_STL))
    {
        int unsupportedCodes = (EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
        if (gOptions.exportFlags & unsupportedCodes)
        {
            if (gpEFD->fileType == FILE_TYPE_BINARY_VISCAM_STL)
            {
                FilterMessageBox(NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain VisCAM colors instead."),
                    _T("Informational"), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                FilterMessageBox(NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain Materialise Magics colors instead."),
                    _T("Informational"), MB_OK | MB_ICONINFORMATION);
            }
        }
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if (gpEFD->fileType == FILE_TYPE_VRML2)
    {
        // are we outputting color textures?
        if (gOptions.exportFlags & EXPT_OUTPUT_TEXTURE)
        {
            if (gOptions.exportFlags & EXPT_3DPRINT)
            {
                // if printing, we don't need to group by material, as it can be one huge pile of data
                gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
            }
            // else if we're outputting for rendering, VRML then outputs grouped by material, unless it's a single material output
            // (in which case this flag isn't turned on anyway).
        }
    }
    else if (gpEFD->fileType == FILE_TYPE_SCHEMATIC)
    {
        // really, ignore all options for Schematic - set how you want, but they'll all be ignored except rotation around the Y axis.
        gOptions.exportFlags &= 0x0;
    }
    else
    {
        // unknown file type?
        MY_ASSERT(gAlwaysFail);
    }

    if (gpEFD->chkBiome)
    {
        gOptions.exportFlags |= EXPT_BIOME;
    }

    // if showing debug groups, we need to turn off full image texturing so we get the largest group as semitransparent
    // (and full textures would just be confusing for debug, anyway)
    if (gOptions.exportFlags & EXPT_DEBUG_SHOW_GROUPS)
    {
        if (gOptions.exportFlags & (EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES))
        {
            gOptions.exportFlags &= ~(EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
            gOptions.exportFlags |= EXPT_OUTPUT_TEXTURE_SWATCHES;
        }
        // we don't want individual blocks for debugging
        gOptions.exportFlags &= ~EXPT_INDIVIDUAL_BLOCKS;
    }

    // OK, all set, let's go!
    FileList outputFileList;
    initializeOutputFileList(outputFileList);
    if (on) {
        // redraw, in case the bounds were changed
        drawInvalidateUpdate(hWnd);

        int errCode = SaveVolume(objFileName, gpEFD->fileType, &gOptions, &gWorldGuide, gExeDirectory,
            gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight,
            updateProgress, terrainFileName, schemeSelected, &outputFileList, (int)gMinewaysMajorVersion, (int)gMinewaysMinorVersion, gVersionID, gChangeBlockCommands,
            gInstanceChunkSize, gBiomeSelected, gGroupCount, gGroupCountSize, gGroupCountArray);
        deleteCommandBlockSet(gChangeBlockCommands);
        gChangeBlockCommands = NULL;

        if (errCode < MW_BEGIN_ERRORS) {
            // note how many files were output - if an error occurred, we output and zip nothing
            retCode = outputFileList.count;

            // zip it up - test that there's something to zip, in case of errors. Note that the first
            // file saved in ObjManip.c is the one used as the zip file's name.
            if (updateProgress != NULL)
            {
                (*updateProgress)(-999.0f, L"Output zip");
            }

            if (gpEFD->chkCreateZip[gpEFD->fileType] && (outputFileList.count > 0))
            {
                wchar_t wcZip[MAX_PATH_AND_FILE];
                // we add .zip not (just) out of laziness, but this helps differentiate obj from wrl from stl.
                swprintf_s(wcZip, MAX_PATH_AND_FILE, L"%s.zip", outputFileList.name[0]);

                DeleteFile(wcZip);
                HZIP hz = CreateZip(wcZip, 0, ZIP_FILENAME);

                // if we are zipping tiles, it's a bit different
                int i;

                // disassemble the directory of the first file
                wchar_t path[MAX_PATH_AND_FILE];
                wchar_t filepathpiece[MAX_PATH_AND_FILE];
                wchar_t* relativeFile;
                // strips off file name - assumes first file is "real" and not in some subdirectory!
                StripLastString(outputFileList.name[0], path, filepathpiece);
                // strips off subdirectory name and puts in piece
                for (i = 0; i < outputFileList.count; i++) {
                    relativeFile = RemoveGivenPath(outputFileList.name[i], path);
                    // reality: if you're zipping and using separate tiles, I'm not going to delete those tiles.
                    // I'm also going to zip the whole folder, vs. messing around trying to export just the tiles needed.
                    // TODO - really should just export tiles needed, but this functionality is a bit tricky.
                    if (ZipAdd(hz, relativeFile, outputFileList.name[i], 0, ZIP_FILENAME) != ZR_OK)
                    {
                        retCode |= MW_CANNOT_WRITE_TO_FILE;
                        DWORD errorCode = GetLastError();
                        // if we need to get really fancy, include code at https://docs.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
                        wchar_t errbuf[1024];
                        wsprintf(errbuf, L"Warning: Not all files were saved to the ZIP file! Error code: %d. Please report this problem to me at erich@acm.org.", (int)errorCode); \
                        FilterMessageBox(NULL, errbuf, _T("Internal ZIP error"), MB_OK | MB_ICONERROR);
                        break;
                    }

                    // delete model files if not needed
                    if (!gpEFD->chkCreateModelFiles[gpEFD->fileType])
                    {
                        DeleteFile(outputFileList.name[i]);
                    }
                    if (updateProgress != NULL)
                    {
                        (*updateProgress)(0.90f + 0.10f * (float)i / (float)outputFileList.count,NULL);
                    }

                    // was AddFolderContent(hz, path, piece );
                }

                CloseZip(hz);

                // now delete subdirectories, if empty. Assumes subdirectories come after main directory files
                wchar_t prevdelpath[MAX_PATH_AND_FILE];
                prevdelpath[0] = (wchar_t)0;
                for (i = outputFileList.count-1; i >= 0; i--) {
                    relativeFile = RemoveGivenPath(outputFileList.name[i], path);
                    wchar_t delpath[MAX_PATH_AND_FILE];
                    StripLastString(relativeFile, delpath, filepathpiece);
                    if (wcscmp(delpath, prevdelpath) != 0 && wcslen(delpath) > 0) {
                        // we could be more clever here and try to remove the directory only if it
                        RemoveDirectory(delpath);
                        wcscpy_s(prevdelpath, MAX_PATH_AND_FILE, delpath);
                    }
                }
            }
        }
        if (updateProgress != NULL)
        {
            (*updateProgress)(1.0f,NULL);
        }

        // check if world is 1.12 or earlier, USD export, and instancing - warn once per world
        if (gModel.instancing && (gMinecraftVersion <= 12) && gInstanceError) {
            gInstanceError = false;
            FilterMessageBox(NULL, _T("Warning: Exporting individual blocks from this older world may not work well, due to incomplete data. For a better conversion, consider first reading your world into a recent version of Minecraft so that it is saved into a newer file format."),
                _T("Export limitation"), MB_OK | MB_ICONWARNING);
        }

        // show errors first
        if (errCode != MW_NO_ERROR)
        {
            // TODO: really, for scripting, if we're logging errors, these should all actually go into the .log file
            PopupErrorDialogs(errCode);
        }

        if (errCode < MW_BEGIN_ERRORS) {

            // output stats, if printing or rendering and there *are* stats (showStatistics is 0 if there's nothing in the volume)
            if (showStatistics) {
                if (printModel == PRINTING_EXPORT && gShowPrintStats) {
                    if (gOptions.cost > 0.0f && outputFileList.count > 0) {
                        wchar_t* currency = gMtlCostTable[gOptions.pEFD->fileType].currency;
                        if (gCustomCurrency != NULL &&
                            gOptions.pEFD->comboPhysicalMaterial[gOptions.pEFD->fileType] == PRINT_MATERIAL_CUSTOM_MATERIAL)
                        {
                            currency = gCustomCurrency;
                        }
                        int retval;
                        wchar_t msgString[2000];
                        swprintf_s(msgString, 2000, L"3D Print Statistics:\n\nApproximate cost is %s %0.2f\nBase is %d x %d blocks, %d blocks high\nEach block is %0.1f mm high (%0.2f inches high)\nInches: base is %0.1f x %0.1f inches, %0.1f inches high\nCentimeters: Base is %0.1f x %0.1f cm, %0.1f cm high\nTotal number of blocks: %d\nTotal cubic centimeters: %0.1f\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                            currency,
                            gOptions.cost,
                            gOptions.dimensions[0], gOptions.dimensions[2], gOptions.dimensions[1],
                            gOptions.block_mm, gOptions.block_inch,
                            gOptions.dim_inches[0], gOptions.dim_inches[2], gOptions.dim_inches[1],
                            gOptions.dim_cm[0], gOptions.dim_cm[2], gOptions.dim_cm[1],
                            gOptions.totalBlocks, gOptions.totalBlocks * gOptions.block_mm * gOptions.block_mm * gOptions.block_mm / 1000.0f);
                        retval = FilterMessageBox(NULL, msgString,
                            _T("Informational"), MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_TOPMOST);
                        if (retval != IDYES)
                        {
                            gShowPrintStats = false;
                        }
                    }
                }
                else if (printModel == RENDERING_EXPORT && gShowRenderStats) {
                    int retval;
                    wchar_t msgString[2000];
                    if (gBiomeSelected >= 0) {
                        if (gModel.options->pEFD->chkDecimate ) {
                            swprintf_s(msgString, 2000, L"Export Statistics:\n\n%s solid blocks and %s billboard blocks converted to %s quad faces (%s triangles) with %s vertices. Simplification saved %s quads.\nBiome used: #%d - %S.\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                                formatWithCommas(gModel.blockCount, gCommaString1),
                                formatWithCommas(gModel.billboardCount, gCommaString2),
                                formatWithCommas(gModel.faceCount, gCommaString3),
                                formatWithCommas(2 * gModel.faceCount, gCommaString4),  // TODO: could actually get a better exact count of triangles; in 3D printing sloped rails produce triangles, which we don't account for
                                formatWithCommas(gModel.vertexCount, gCommaString5),
                                formatWithCommas(gModel.simplifyFaceSavings, gCommaString6),
                                gBiomeSelected,
                                gBiomes[gBiomeSelected].name
                            );
                        }
                        else {
                            swprintf_s(msgString, 2000, L"Export Statistics:\n\n%s solid blocks and %s billboard blocks converted to %s quad faces (%s triangles) with %s vertices.\nBiome used: #%d - %S.\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                                formatWithCommas(gModel.blockCount, gCommaString1),
                                formatWithCommas(gModel.billboardCount, gCommaString2),
                                formatWithCommas(gModel.faceCount, gCommaString3),
                                formatWithCommas(2 * gModel.faceCount, gCommaString4),
                                formatWithCommas(gModel.vertexCount, gCommaString5),
                                gBiomeSelected,
                                gBiomes[gBiomeSelected].name
                            );
                        }
                    }
                    else {
                        if (gModel.options->pEFD->chkDecimate) {
                            swprintf_s(msgString, 2000, L"Export Statistics:\n\n%s solid blocks and %s billboard blocks converted to %s quad faces (%s triangles) with %s vertices. Simplification saved %s quads.\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                                formatWithCommas(gModel.blockCount, gCommaString1),
                                formatWithCommas(gModel.billboardCount, gCommaString2),
                                formatWithCommas(gModel.faceCount, gCommaString3),
                                formatWithCommas(2 * gModel.faceCount, gCommaString4),
                                formatWithCommas(gModel.vertexCount, gCommaString5),
                                formatWithCommas(gModel.simplifyFaceSavings, gCommaString6)
                                );
                        }
                        else {
                            swprintf_s(msgString, 2000, L"Export Statistics:\n\n%s solid blocks and %s billboard blocks converted to %s quad faces (%s triangles) with %s vertices.\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                                formatWithCommas(gModel.blockCount, gCommaString1),
                                formatWithCommas(gModel.billboardCount, gCommaString2),
                                formatWithCommas(gModel.faceCount, gCommaString3),
                                formatWithCommas(2 * gModel.faceCount, gCommaString4),
                                formatWithCommas(gModel.vertexCount, gCommaString5)
                            );
                        }
                    }
                    retval = FilterMessageBox(NULL, msgString,
                        _T("Informational"), MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_TOPMOST);
                    if (retval != IDYES)
                    {
                        gShowRenderStats = false;
                    }
                }
            }
        }

        // clear progress bar
        if (updateProgress != NULL)
            (*updateProgress)(0.0f,NULL);
    }
    freeOutputFileList(outputFileList);

    return retCode;
}


static void PopupErrorDialogs(int errCode)
{
    // pop up all errors flagged
    for (int errNo = MW_NUM_CODES - 1; errNo >= 0; errNo--)
    {
        if ((1 << errNo) & errCode)
        {
            // check if it's a PNG error
            if ((1 << errNo) >= MW_BEGIN_PNG_ERRORS)
            {
                // PNG errors have extra information, i.e., what the PNG error string is.
                TCHAR errString[1024], wcString[1024];
                int pngError = (errCode >> MW_NUM_CODES);
                size_t newsize = strlen(lodepng_error_text(pngError)) + 1;
                size_t convertedChars = 0;
                mbstowcs_s(&convertedChars, wcString, newsize, lodepng_error_text(pngError), _TRUNCATE);
                wsprintf(errString, gPopupInfo[errNo + 1].text, wcString);
                FilterMessageBox(
                    NULL,
                    errString,
                    gPopupInfo[errNo + 1].caption,
                    gPopupInfo[errNo + 1].type
                );
            }
            else if ((1 << errNo) == MW_MULTIPLE_GROUPS_FOUND)
            {
                // Show what the group sizes are.
                TCHAR errString[1024], sizesString[20];
                wsprintf(errString, L"%s\n\nNumber of separate floating groups: %d\nSizes:", gPopupInfo[errNo + 1].text, gGroupCount);
                // sort values in gGroupArray by size, largest to smallest
                int i, j;
                int size = min(gGroupCount, gGroupCountSize);
                for (i = 0; i < size - 1; i++) {
                    // Last i elements are already in place 
                    for (j = 0; j < size - i - 1; j++) {
                        if (gGroupCountArray[j] < gGroupCountArray[j + 1]) {
                            // swap
                            int temp = gGroupCountArray[j];
                            gGroupCountArray[j] = gGroupCountArray[j + 1];
                            gGroupCountArray[j + 1] = temp;
                        }
                    }
                }

                for (i = 0; i < gGroupCount && i < gGroupCountSize; i++) {
                    // output number
                    wsprintf(sizesString, L" %d", gGroupCountArray[i]);
                    wcscat_s(errString, 1024, sizesString);
                    if (i < gGroupCount - 1 && i < gGroupCountSize - 1) {
                        wcscat_s(errString, 1024, L",");
                    }
                    else {
                        // at last possible element in array
                        if (gGroupCount <= gGroupCountSize) {
                            wcscat_s(errString, 1024, L".");
                        }
                        else {
                            //gGroupCount is larger than gGroupCountSize, so not all values are saved - stop output and note all values are not shown
                            wcscat_s(errString, 1024, L", ...");
                            break;
                        }
                    }
                }

                FilterMessageBox(
                    NULL,
                    errString,
                    gPopupInfo[errNo + 1].caption,
                    gPopupInfo[errNo + 1].type
                );
            }
            else
            {
                //int msgboxID = 
                FilterMessageBox(
                    NULL,
                    gPopupInfo[errNo + 1].text,
                    gPopupInfo[errNo + 1].caption,
                    gPopupInfo[errNo + 1].type
                );
            }

            //switch (msgboxID)
            //{
            //case IDCANCEL:
            //    break;
            //case IDTRYAGAIN:
            //    break;
            //case IDCONTINUE:
            //    break;

            //}
        }
    }
}

// yes, this it totally lame, copying code from MinewaysMap
static const wchar_t* removePath(const wchar_t* src)
{
    // find last \ in string
    const wchar_t* strPtr = wcsrchr(src, (wchar_t)'\\');
    if (strPtr)
        // found a \, so move up past it
        strPtr++;
    else
    {
        // look for /
        strPtr = wcsrchr(src, (wchar_t)'/');
        if (strPtr)
            // found a /, so move up past it
            strPtr++;
        else
            // no \ or / found, just return string itself
            return src;
    }

    return strPtr;
}

#define INIT_ALL_FILE_TYPES( a, v0,v1,v2,v3,v4,v5,v6,v7)    \
    (a)[FILE_TYPE_WAVEFRONT_REL_OBJ] = (v0);    \
    (a)[FILE_TYPE_WAVEFRONT_ABS_OBJ] = (v1);    \
    (a)[FILE_TYPE_USD] = (v2);    \
    (a)[FILE_TYPE_BINARY_MAGICS_STL] = (v3);    \
    (a)[FILE_TYPE_BINARY_VISCAM_STL] = (v4);    \
    (a)[FILE_TYPE_ASCII_STL] = (v5);    \
    (a)[FILE_TYPE_VRML2] = (v6);	\
    (a)[FILE_TYPE_SCHEMATIC] = (v7);

static void initializeExportDialogData()
{
    initializePrintExportData(gExportPrintData);
    initializeViewExportData(gExportViewData);
    InitializeSchematicExportData(gExportSchematicData);
    InitializeSketchfabExportData(gExportSketchfabData);
}

static void initializePrintExportData(ExportFileData& printData)
{
    // by default, make everything 0 - off. Yes, even floats, cppcheck
    memset(&printData, 0, sizeof(ExportFileData));  // cppcheck-suppress 758

    // turn stuff on
    printData.fileType = FILE_TYPE_VRML2;

    INIT_ALL_FILE_TYPES(printData.chkCreateZip,            1, 1, 0, 0, 0, 0, 1, 0);
    // I used to set the last value to 0, meaning only the zip would be created. The idea
    // was that the naive user would then only have the zip, and so couldn't screw up
    // when uploading the model file. But this setting is a pain if you want to preview
    // the model file, you have to always remember to check the box so you can get the
    // preview files. So, now it's off.
    INIT_ALL_FILE_TYPES(printData.chkCreateModelFiles,     1, 1, 1, 1, 1, 1, 1, 1);

    // OBJ and VRML have color, depending...
    // order: Sculpteo OBJ, relative OBJ, USDA, i.materialize STL, VISCAM STL, ASCII STL, Shapeways VRML, (Schematic)
    INIT_ALL_FILE_TYPES(printData.radioExportNoMaterials,  0, 0, 0, 0, 0, 1, 0, 1);
    // might as well export color with OBJ and binary STL - nice for previewing
    INIT_ALL_FILE_TYPES(printData.radioExportMtlColors,    0, 0, 0, 1, 1, 0, 0, 0);
    INIT_ALL_FILE_TYPES(printData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES(printData.radioExportFullTexture,  1, 1, 0, 0, 0, 0, 1, 0); // for 3D printing, nice to be able to load just the large RGB texture
    INIT_ALL_FILE_TYPES(printData.radioExportTileTextures, 0, 0, 1, 0, 0, 0, 0, 0);

    strcpy_s(printData.tileDirString, MAX_PATH, "tex");

    printData.chkTextureRGB = 1;
    printData.chkTextureA = 0;
    printData.chkTextureRGBA = 0;

    printData.chkMergeFlattop = 1;
    // Shapeways imports VRML files and displays them with Y up, that is, it
    // rotates them itself. Sculpteo imports OBJ, and likes Z is up, so we export with this on.
    // STL uses Z is up, even though i.materialise's previewer shows Y is up.
    INIT_ALL_FILE_TYPES(printData.chkMakeZUp, 1, 1, 1, 1, 1, 1, 0, 0);
    printData.chkCenterModel = 1;
    printData.chkExportAll = 0;
    printData.chkFatten = 0;
    printData.chkBiome = 0;
    printData.chkCompositeOverlay = 1;	// never allow 0 for 3D printing, as this would create tiles floating above the surface
    printData.chkBlockFacesAtBorders = 1; // never allow 0 for 3D printing, as this would make the surface non-manifold
    printData.chkDecimate = 0; // never allow 0 for 3D printing, as this would make the surface potentially have T-junctions
    printData.chkLeavesSolid = 1; // never allow 0 for 3D printing, as this would make the surface non-manifold 

    printData.radioRotate0 = 1;

    printData.radioScaleByBlock = 1;
    printData.modelHeightVal = 5.0f;    // 5 cm target height
    INIT_ALL_FILE_TYPES(printData.blockSizeVal,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall);
    printData.costVal = 25.00f;

    printData.chkSealEntrances = 0; // left off by default: useful, but the user should want to do this
    printData.chkSealSideTunnels = 0; // left off by default: useful, but the user should want to do this
    printData.chkFillBubbles = 1;
    printData.chkConnectParts = 1;
    printData.chkConnectCornerTips = 1;
    // it's actually better to start with manifold off and see if there are lots of groups.
    printData.chkConnectAllEdges = 0;
    printData.chkDeleteFloaters = 1;
    printData.chkMeltSnow = 0;

    printData.chkShowParts = 0;
    printData.chkShowWelds = 0;

    // should normally just have one material and group
    printData.chkSeparateTypes = 0;
    INIT_ALL_FILE_TYPES(printData.chkIndividualBlocks, 0, 0, 0, 0, 0, 0, 0, 0);
    printData.chkMaterialPerFamily = 0;
    printData.chkSplitByBlockType = 0;
    printData.chkMakeGroupsObjects = 0;
    // shouldn't really matter, now that both versions don't use the diffuse color when texturing
    INIT_ALL_FILE_TYPES(printData.chkCustomMaterial, 0, 0, 1, 0, 0, 0, 0, 0);
    printData.chkExportMDL = 1;

    printData.scaleLightsVal = 30.0f;
    printData.scaleEmittersVal = 1000.0f;

    printData.floaterCountVal = 16;
    INIT_ALL_FILE_TYPES(printData.chkHollow,      1, 1, 1, 0, 0, 0, 1, 0);
    INIT_ALL_FILE_TYPES(printData.chkSuperHollow, 1, 1, 1, 0, 0, 0, 1, 0);
    INIT_ALL_FILE_TYPES(printData.hollowThicknessVal,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall);	// last one is schematic "material"

    // materials selected
    INIT_ALL_FILE_TYPES(printData.comboPhysicalMaterial, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE);
    // defaults: for Sculpteo OBJ, mm (was cm - affects first two values here); for USD, who knows; for i.materialise, mm; for other STL, cm; for Shapeways VRML, mm
    INIT_ALL_FILE_TYPES(printData.comboModelUnits, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER);

    printData.flags = EXPT_3DPRINT;
}

static void initializeViewExportData(ExportFileData& viewData)
{
    //////////////////////////////////////////////////////
    // copy view data from print, and change what's needed
    initializePrintExportData(viewData);

// defining this makes USDA be the normal export file format. Good for testing but should be turned off for a normal release
//#define FAVOR_USD
#ifdef FAVOR_USD
    // Now that I've figured out that Blender can show materials OK, change to "true spec"
    viewData.fileType = FILE_TYPE_USD;
#else
    viewData.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
#endif

    // order: Sculpteo OBJ, relative OBJ, USDA, i.materialize STL, VISCAM STL, ASCII STL, Shapeways VRML, (Schematic)
    // don't really need to create a zip for rendering output
    INIT_ALL_FILE_TYPES(viewData.chkCreateZip,            0, 0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES(viewData.chkCreateModelFiles,     1, 1, 1, 1, 1, 1, 1, 1);

    INIT_ALL_FILE_TYPES(viewData.radioExportNoMaterials,  0, 0, 0, 0, 0, 1, 0, 1);
    INIT_ALL_FILE_TYPES(viewData.radioExportMtlColors,    0, 0, 0, 1, 1, 0, 0, 0);
    INIT_ALL_FILE_TYPES(viewData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES(viewData.radioExportFullTexture,  0, 0, 0, 0, 0, 0, 1, 0);  // was 1's for OBJ; now just for VRML, which doesn't have individual texture export code
    INIT_ALL_FILE_TYPES(viewData.radioExportTileTextures, 1, 1, 1, 0, 0, 0, 0, 0);  // USD uses tile per block, only; really, a better default anyway

    strcpy_s(viewData.tileDirString, MAX_PATH, "tex");

    viewData.chkTextureRGB = 1;
    viewData.chkTextureA = 1;
    viewData.chkTextureRGBA = 1;

    viewData.chkExportAll = 1;
    // for renderers, assume Y is up, which is the norm
    INIT_ALL_FILE_TYPES(viewData.chkMakeZUp, 0, 0, 0, 0, 0, 0, 0, 0);

    viewData.modelHeightVal = 100.0f;    // 100 cm - view doesn't need a minimum, really
    INIT_ALL_FILE_TYPES(viewData.blockSizeVal,
        1000.0f,	// 1 meter
        1000.0f,
        1000.0f,
        1000.0f,
        1000.0f,
        1000.0f,
        1000.0f,
        1000.0f);
    viewData.costVal = 25.00f;

    viewData.chkSealEntrances = 0;
    viewData.chkSealSideTunnels = 0;
    viewData.chkFillBubbles = 0;
    viewData.chkConnectParts = 0;
    viewData.chkConnectCornerTips = 0;
    viewData.chkConnectAllEdges = 0;
    viewData.chkDeleteFloaters = 0;
    INIT_ALL_FILE_TYPES(viewData.chkHollow,      0, 0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES(viewData.chkSuperHollow, 0, 0, 0, 0, 0, 0, 0, 0);

    viewData.chkSeparateTypes = 1;
    INIT_ALL_FILE_TYPES(viewData.chkIndividualBlocks, 0, 0, 0, 0, 0, 0, 0, 0);
    viewData.chkMaterialPerFamily = 1;
    viewData.chkSplitByBlockType = 1;
    viewData.chkMakeGroupsObjects = 0;  // keeping the training wheels on for Blender. Setting to 1 can be "surprising".
    INIT_ALL_FILE_TYPES(viewData.chkCustomMaterial, 1, 1, 1, 0, 0, 0, 0, 0);
    viewData.chkCompositeOverlay = 0;
    viewData.chkBlockFacesAtBorders = 1;
    viewData.chkDecimate = 0;
    viewData.chkLeavesSolid = 0;
    viewData.chkExportMDL = 1;
    viewData.scaleLightsVal = 30.0f;
    viewData.scaleEmittersVal = 1000.0f;

    viewData.floaterCountVal = 16;
    // mostly irrelevant for viewing, though centimeters can be useful
    INIT_ALL_FILE_TYPES(viewData.hollowThicknessVal, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f);    // 1 meter
    INIT_ALL_FILE_TYPES(viewData.comboPhysicalMaterial, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE);
    // For USD it was centimeters, but now meters are supported well.
    INIT_ALL_FILE_TYPES(viewData.comboModelUnits, UNITS_METER, UNITS_METER, UNITS_METER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_METER, UNITS_METER);

    // TODO someday allow getting rid of floaters, that would be cool.
    //gExportSchematicData.chkDeleteFloaters = 1;

    viewData.flags = 0x0;
}

static void InitializeSchematicExportData(ExportFileData& schematicData)
{
    //////////////////////////////////////////////////////
    // copy schematic data from view, and change what's needed
    initializeViewExportData(schematicData);
    strcpy_s(schematicData.tileDirString, MAX_PATH, "");
    schematicData.fileType = FILE_TYPE_SCHEMATIC;	// always
    schematicData.chkMergeFlattop = 0;
}

static void InitializeSketchfabExportData(ExportFileData& sketchfabData)
{
    //////////////////////////////////////////////////////
    // copy sketchfab data from view, and change what's needed
    initializeViewExportData(sketchfabData);
    sketchfabData.fileType = FILE_TYPE_WAVEFRONT_REL_OBJ;	// always
}


// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// importFile is a script file, but can also be an .obj, .usda, or .wrl file treated as one, as well as the .txt statistics files from STL exports
static void runImportOrScript(wchar_t* importFile, WindowSet& ws, const char** pBlockLabel, LPARAM holdlParam, bool dialogOnSuccess)
{
    ImportedSet is;
    is.ws = ws;
    int retCode = importSettings(importFile, is, dialogOnSuccess);
    wchar_t msgString[1024];
    int mx, my, mz, type, dataVal, biome;

    // not really necessary, since we're just trying to open the file, not output or compare its name to anything.
    // rationalizeFilePath(importFile);
    switch (retCode) {
    case IMPORT_MODEL:
        if (strlen(is.world) > 0)
        {
            if (!commandLoadWorld(is, msgString)) {
                FilterMessageBox(NULL, msgString, _T("Import error"), MB_OK | MB_ICONWARNING);
                // quit; since we can't load new world, anything else is pointless.
                return;
            }
        }

        // if world viewed is nether or The End, load here.
        if (is.nether) {
            if (switchToNether(is)) {
                FilterMessageBox(NULL, L"Attempt to switch to the Nether failed", _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }
        else if (is.theEnd) {
            assert(is.nether == false);
            if (switchToTheEnd(is)) {
                FilterMessageBox(NULL, L"Attempt to switch to The End failed", _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }

        // see if we can load the terrain file
        if (strlen(is.terrainFile) > 0)
        {
            if (!commandLoadTerrainFile(is, msgString)) {
                FilterMessageBox(NULL, msgString, _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }

        // see if we can load the color scheme
        if (strlen(is.colorScheme) > 0)
        {
            // don't invalidate on load, as we know we'll do it later
            if (!commandLoadColorScheme(is, msgString, false)) {
                FilterMessageBox(NULL, msgString, _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }


        // cross-over any values that are semi-shared to other file formats
        copyOverExportPrintData(gpEFD);

        gCurX = (gpEFD->minxVal + gpEFD->maxxVal) / 2;
        gCurZ = (gpEFD->minzVal + gpEFD->maxzVal) / 2;

        if (gHighlightOn)
        {
            // reload world in order to clear out any previous selection displayed.
            sendStatusMessage(ws.hwndStatus, L"Importing model: loading world");
            loadWorld(ws.hWnd);
            setUIOnLoadWorld(ws.hWnd, ws.hwndSlider, ws.hwndLabel, ws.hwndInfoLabel, ws.hwndBottomSlider, ws.hwndBottomLabel);
        }
        sendStatusMessage(ws.hwndStatus, L"");	// done

        gHighlightOn = true;
        SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
        enableBottomControl(1, /* ws.hwndBottomSlider, ws.hwndBottomLabel, */ ws.hwndInfoBottomLabel);
        // put target (bottom) depth to new depth set, if any
        gTargetDepth = gpEFD->minyVal;
        // adjust maximum height up, but not down. We export the maximum height at which
        // something is found, when normally this height is *set* to 255.
        // On second thought, no: if someone is importing an underground area, they want the height.
        //if ( gCurDepth < gpEFD->maxyVal )
        //{
        gCurDepth = gpEFD->maxyVal;
        //}
        *pBlockLabel = IDBlock(LOWORD(holdlParam), HIWORD(holdlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
            bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
        updateStatus(mx, mz, my, *pBlockLabel, type, dataVal, biome, ws.hwndStatus);
        setSlider(ws.hWnd, ws.hwndSlider, ws.hwndLabel, gCurDepth, false);
        setSlider(ws.hWnd, ws.hwndBottomSlider, ws.hwndBottomLabel, gTargetDepth, false);

        // make biome display match biome setting
        if (gpEFD->chkBiome)
            // turn bit on
            gOptions.worldType |= BIOMES;
        else
            // turn bit off
            gOptions.worldType &= ~BIOMES;
        CheckMenuItem(GetMenu(ws.hWnd), IDM_VIEW_SHOWBIOMES, (gOptions.worldType & BIOMES) ? MF_CHECKED : MF_UNCHECKED);

        // redraw, as selection bounds will change
        drawInvalidateUpdate(ws.hWnd);

        // and note which import was done
        if (dialogOnSuccess)
        {
            FilterMessageBox(
                NULL,
                (gpEFD->flags & EXPT_3DPRINT) ?
                _T("Previous settings for 3D printing imported.") :
                _T("Previous settings for rendering imported."),
                _T("Informational"),
                MB_OK | MB_ICONINFORMATION
            );
        }
        break;

    case IMPORT_SCRIPT:
        addChangeBlockCommandsToGlobalList(is);
        break;
    case IMPORT_FAILED:
    default:
        break;
    }
}

// True means script ran successfully, false that something bad happened. This code generates and displays the error messages found.
static int importSettings(wchar_t* importFile, ImportedSet& is, bool dialogOnSuccess)
{
    // this will get initialized again by the readers, but since we check is.logging on exit here, initialize now anyway.
    ExportFileData dummyEfd;
    initializeImportedSet(is, &dummyEfd, importFile);

    int retCode = IMPORT_FAILED;
    /* This doesn't work, for some reason. It's a kind of terrible idea, anyway, putting a level.dat's path on the command line
    // If the user is trying to load a level.dat file (which could happen on the command line), try that for them
    if (wcsstr(importFile, L"level.dat")) {
        retCode = loadWorldFromFilename(importFile, is.ws.hWnd);
        // we don't care if we get back a 1 or 2; treat as 1
        if (retCode) {
            retCode = IMPORT_MODEL;
            setUIOnLoadWorld(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, is.ws.hwndInfoLabel, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel);
        }
        goto Exit;
    }
    */

    // Read header of exported file, as written by writeStatistics(), and get export settings from it,
    // or read file as a set of scripting commands.
    FILE* fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH_AND_FILE];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        FilterMessageBox(NULL, buf, _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        retCode = IMPORT_FAILED;
        goto Exit;
    }

    // Read first line.
    char lineString[IMPORT_LINE_LENGTH];

    readLine(fh, lineString, IMPORT_LINE_LENGTH);
    fclose(fh);

    //  Is it in export file format?
    bool exported = false;
    if ((strstr(lineString, "# Wavefront OBJ file made by Mineways") != NULL) ||
        (strstr(lineString, "#usda 1.0") != NULL) ||
        (strstr(lineString, "#VRML V2.0 utf8") != NULL) ||
        (strstr(lineString, "# Minecraft world:") != NULL) ||
        (strstr(lineString, "# World:") != NULL) || // newer style
        (strstr(lineString, "# Extracted from Minecraft world") != NULL)) {
        exported = true;

        // set up for export
        sendStatusMessage(is.ws.hwndStatus, L"Importing model file's settings");

        retCode = importModelFile(importFile, is) ? IMPORT_MODEL : 0;
    }
    else
    {
        sendStatusMessage(is.ws.hwndStatus, L"Running script");

        retCode = readAndExecuteScript(importFile, is) ? IMPORT_SCRIPT : 0;
    }

Exit:
    // deal with errors in a consistent way
    if (is.logging) {
        // output to error log
        if (is.logfile) {
#ifdef WIN32
            DWORD br;
#endif
            char outputString[256];
            char* pConverted = NULL;
            size_t length = (is.errorMessages == NULL) ? 0 : wcslen(is.errorMessages);
            if (length == 0) {
                sprintf_s(outputString, 256, "No errors or warnings");
                if (PortaWrite(is.logfile, outputString, strlen(outputString))) goto OnDone;
            }
            else {
                // output errors and warnings
                // convert to char
                length++;
                pConverted = (char*)malloc(length * sizeof(char));
                WcharToChar(is.errorMessages, pConverted, (int)length);
                if (PortaWrite(is.logfile, pConverted, strlen(pConverted))) {
                    goto OnDone;
                }
            }

            sprintf_s(outputString, 256, "\n========================================================\n\n");
            if (PortaWrite(is.logfile, outputString, strlen(outputString))) goto OnDone;

        OnDone:
            if (pConverted) {
                free(pConverted);
                pConverted = NULL;
            }
            PortaClose(is.logfile);
            is.logfile = NULL;
            free(is.errorMessages);
            is.errorMessages = NULL;
            is.errorMessagesStringSize = 0;

            if (length > 0) {
                wchar_t msgString[1024];
                if (is.errorsFound > 0) {
                    swprintf_s(msgString, 1024, L"Errors found in script file - processing aborted. Check log file %S",
                        is.logFileName);
                    FilterMessageBox(NULL, msgString, _T("Script error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
                }
                else {
                    swprintf_s(msgString, 1024, L"Processing finished. Warnings found in script file. Check log file %S",
                        is.logFileName);
                    FilterMessageBox(NULL, msgString, _T("Script error"), MB_OK | MB_ICONWARNING);
                }
            }
        }
        else {
            MY_ASSERT(gAlwaysFail);
        }
    }
    else {
        // output to screen
        // Are there errors, or just warnings?
        if (is.errorsFound) {
            // run-time error!
            FilterMessageBox(NULL, is.errorMessages, is.readingModel ? _T("Import error") : _T("Script error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
            deleteCommandBlockSet(is.pCBChead);
            is.pCBChead = is.pCBClast = NULL;
            retCode = IMPORT_FAILED;
        }
        else if (is.errorMessages && (is.errorMessages[0] != (wchar_t)0)) {
            FilterMessageBox(NULL, is.errorMessages, is.readingModel ? _T("Import warning") : _T("Script warning"), MB_OK | MB_ICONWARNING);
        }
        else {
            if (!is.readingModel && dialogOnSuccess)
                FilterMessageBox(NULL, _T("Script successfully finished running."), _T("Informational"), MB_OK | MB_ICONINFORMATION);
        }
    }
    if (is.errorMessages != NULL)
    {
        free(is.errorMessages);
        is.errorMessages = NULL;
    }

    // after all that, should the program be closed?
    if (is.closeProgram)
    {
        closeMineways();
    }
    return retCode;
}

// true if all went well
static bool importModelFile(wchar_t* importFile, ImportedSet& is)
{
    FILE* fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH_AND_FILE];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        saveErrorMessage(is, buf);
        return false;
    }

    char lineString[IMPORT_LINE_LENGTH];

    ExportFileData efd;
    // just to set some defaults that aren't bizarre; not sure this is needed
    initializeViewExportData(efd);
    initializeImportedSet(is, &efd, importFile);

    // Read lines until done and process them.
    int readCode;
    int isRet;
    int count = 0;
    bool retCode = true;

    do {
        readCode = readLine(fh, lineString, IMPORT_LINE_LENGTH);
        is.lineNumber++;

        if (readCode < 0)
        {
            fclose(fh);
            saveErrorMessage(is, L"data on line was longer than 1024 characters - aborting!");
            return false;
        }

        // process line, since it's valid
        char* cleanedLine = prepareLineData(lineString, true);

        isRet = interpretImportLine(cleanedLine, is);
        if (isRet & (INTERPRETER_FOUND_VALID_LINE | INTERPRETER_FOUND_VALID_EXPORT_LINE)) {
            // found a valid line
            count = 0;
        }
        if ((isRet & INTERPRETER_FOUND_VALID_EXPORT_LINE) && (is.exportTypeFound == ISE_NO_DATA_TYPE_FOUND)) {
            // trying to change an export setting, but the file type was never set.
            // Note that flagging an error here will end the loop and show this error.
            saveErrorMessage(is, L"cannot change an export setting until the export mode and file type are set. Expected to first see a line in the header starting 'Created for'.");
        }
        else if (isRet & INTERPRETER_FOUND_NOTHING_USEFUL) {
            // if we read some number of lines in a row and there's no interpretation for them, end it.
            count++;
        }
        // else, negative isRet means we immediately end, for whatever reason, e.g. we found something in the file that makes things want to terminate.
    } while ((is.errorsFound == 0) && (readCode >= 1) && (is.logging || (count < MAX_ERRORS_DISPLAY)) && !(isRet & (INTERPRETER_FOUND_ERROR | INTERPRETER_END_READING)));

    // is data valid?
    if ((is.errorsFound == 0) && (is.exportTypeFound != ISE_NO_DATA_TYPE_FOUND) && !(isRet & INTERPRETER_FOUND_ERROR)) {
        // copy box over, since it was read in before we knew which EFD to use, 3d printing or "viewing" (rendering)
        is.pEFD->minxVal = is.minxVal;
        is.pEFD->minyVal = is.minyVal;
        is.pEFD->minzVal = is.minzVal;
        is.pEFD->maxxVal = is.maxxVal;
        is.pEFD->maxyVal = is.maxyVal;
        is.pEFD->maxzVal = is.maxzVal;
        *is.pSaveEFD = *is.pEFD;
        gpEFD = is.pSaveEFD;
    }
    else {
        // if there are no other error messages, then check if there was a problem of no file type being found
        if ((is.errorsFound == 0) && (is.exportTypeFound == ISE_NO_DATA_TYPE_FOUND) && !(isRet & INTERPRETER_FOUND_ERROR)) {
            saveErrorMessage(is, L"Error: could not determine whether this file was a 3D printing file or rendering file. Expected to see a line in the header starting 'Created for'.");
        }
        retCode = false;
    }
    fclose(fh);
    return retCode;
}

// true if all went well
static bool readAndExecuteScript(wchar_t* importFile, ImportedSet& is)
{
    FILE* fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH_AND_FILE];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        saveErrorMessage(is, buf);
        return false;
    }

    char lineString[IMPORT_LINE_LENGTH];

    // Different than above: try to run through script once without execution.
    // if no errors found, then run through for real.
    // Also, /* and */ are used for comments.
    ExportFileData dummyEfd;
    initializeImportedSet(is, &dummyEfd, importFile);
    is.readingModel = false;
    is.processData = false;

    // check
    sendStatusMessage(is.ws.hwndStatus, L"Checking script for syntax errors");

    // First test: read lines until done
    int readCode;
    int isRet;
    bool retCode = true;

    bool commentBlock = false;
    char* cleanedLine;
    do {
        readCode = readLine(fh, lineString, IMPORT_LINE_LENGTH);
        is.lineNumber++;

        if (readCode < 0)
        {
            fclose(fh);
            saveErrorMessage(is, L"data on line was longer than 1024 characters - aborting!");
            retCode = false;
            goto Exit;
        }

        cleanedLine = lineString;

        bool nextCommentBlock = dealWithCommentBlocks(cleanedLine, commentBlock);

        if (!commentBlock)
        {
            // process line, since it's valid
            cleanedLine = prepareLineData(cleanedLine, false);

            // first try to run various script commands
            isRet = interpretScriptLine(cleanedLine, is);
            // test if nothing on line. Errors will go into the error list, so we don't check isRet < 0
            if (isRet & INTERPRETER_FOUND_NOTHING_USEFUL)
            {
                // didn't find anything on line, so check importer flags
                isRet = interpretImportLine(cleanedLine, is);
                if (isRet & INTERPRETER_FOUND_NOTHING_USEFUL) {
                    // found something we can't ignore and can't process - warn.
                    saveErrorMessage(is, L"syntax error, cannot interpret command.", cleanedLine);
                }
            }
            if (isRet & INTERPRETER_FOUND_CLOSE)
            {
                is.closeProgram = true;
            }
        }
        commentBlock = nextCommentBlock;
        // go until we've hit the end of the file, or hit the maximum error count of MAX_ERRORS_DISPLAY (more makes the dialog too long).
    } while ((readCode >= 1) && (is.logging || (is.errorsFound < MAX_ERRORS_DISPLAY)) && !is.closeProgram);

    if (is.errorsFound) {
        if (!is.logging && (is.errorsFound >= MAX_ERRORS_DISPLAY)) {
            saveMessage(is, L"Error limit of 20 errors reached - scan aborted.", L"Error", 0);
        }
        retCode = false;
        goto Exit;
    }

    // OK, script looks good, read it for real and process it.
    fclose(fh);
    if (!is.logging) {
        // clear out errors, ready for more
        free(is.errorMessages);
        is.errorMessages = NULL;
        is.errorMessagesStringSize = 0;
    }

    // set up real data to change, directly!
    ExportFileData* pEFD = &gExportPrintData;
    if (gpEFD == &gExportViewData)
    {
        pEFD = &gExportViewData;
    }
    // initialize wipes these out, but we need the log file to be around,
    // to write to at end.
    if (is.logging) {
        bool saveLoggingStatus = is.logging;
        HANDLE saveLoggingFileHandle = is.logfile;
        wchar_t* saveErrorMessages = is.errorMessages;
        size_t saveErrorMessagesStringSize = is.errorMessagesStringSize;
        initializeImportedSet(is, pEFD, importFile);
        is.logging = saveLoggingStatus;
        is.logfile = saveLoggingFileHandle;
        is.errorMessages = saveErrorMessages;
        is.errorMessagesStringSize = saveErrorMessagesStringSize;
    }
    else {
        initializeImportedSet(is, pEFD, importFile);
    }

    is.readingModel = false;
    is.processData = true;

    err = _wfopen_s(&fh, importFile, L"rt");
    if (err != 0) {
        wchar_t buf[MAX_PATH_AND_FILE];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        saveErrorMessage(is, buf);
        return false;
    }

    sendStatusMessage(is.ws.hwndStatus, RUNNING_SCRIPT_STATUS_MESSAGE);

    commentBlock = false;
    do {
        readCode = readLine(fh, lineString, IMPORT_LINE_LENGTH);
        is.lineNumber++;

        if (readCode < 0)
        {
            fclose(fh);
            // shouldn't reach here, already tested above, but just in case...
            saveErrorMessage(is, L"data on line was longer than 1024 characters - aborting!");
            retCode = false;
            goto Exit;
        }

        cleanedLine = lineString;

        bool nextCommentBlock = dealWithCommentBlocks(cleanedLine, commentBlock);

        if (!commentBlock)
        {
            // process line, since it's valid
            cleanedLine = prepareLineData(cleanedLine, false);

            // first try to run various script commands
            isRet = interpretScriptLine(cleanedLine, is);
            if (isRet & INTERPRETER_FOUND_NOTHING_USEFUL)
            {
                // didn't find anything on line, so check importer flags
                isRet = interpretImportLine(cleanedLine, is);
                if (isRet & INTERPRETER_FOUND_NOTHING_USEFUL) {
                    // found something we can't ignore and can't process - warn.
                    // Should never reach here, this was supposed to be caught earlier in the first pass, but just in case...
                    saveErrorMessage(is, L"syntax error, cannot interpret command.", cleanedLine);
                }
            }
            if ((isRet & INTERPRETER_FOUND_VALID_EXPORT_LINE) && (is.exportTypeFound == ISE_NO_DATA_TYPE_FOUND) && !gScriptExportWarning) {
                drawInvalidateUpdate(is.ws.hWnd);
                // trying to change an export setting, but the file type was never set.
                saveWarningMessage(is, L"export option set, but you might want to use 'Set render type:' or 'Set 3D print type:' to first explicitly set the type of export. The export setting was changed for the last-used (or default) file type.");
                // do this just once a session.
                gScriptExportWarning = true;
            }
            if (isRet & INTERPRETER_REDRAW_SCREEN)
            {
                // main display should be redrawn.
                drawInvalidateUpdate(is.ws.hWnd);
            }
            if (isRet & INTERPRETER_FOUND_CLOSE)
            {
                is.closeProgram = true;
            }
        }
        commentBlock = nextCommentBlock;
        // go until we've hit the end of the file, or any error was found, or we hit a Close
    } while ((readCode >= 1) && (is.errorsFound == 0) && !is.closeProgram);
    sendStatusMessage(is.ws.hwndStatus, L"Script done");

Exit:
    fclose(fh);
    return retCode;
}

static void initializeImportedSet(ImportedSet& is, ExportFileData* pEFD, wchar_t* importFile)
{
    // The "constants" area is stuff that the calling method passes in, such as window locations, etc. These are never to be touched.
    WindowSet ws = is.ws;

    // these are the defaults for reading in a model as a script.
    memset(&is, 0, sizeof(ImportedSet));
    // restore the constants area
    is.ws = ws;

    //is.errorsFound = 0;	// until proven otherwise
    is.readingModel = true;
    is.exportTypeFound = ISE_NO_DATA_TYPE_FOUND;
    // set bounds to current values, if any
    int on = true;	// dummy
    GetHighlightState(&on, &is.minxVal, &is.minyVal, &is.minzVal, &is.maxxVal, &is.maxyVal, &is.maxzVal, gMinHeight);
    is.pEFD = pEFD;
    // set by what was last exported, if anything
    if (gpEFD == &gExportViewData)
    {
        is.pSaveEFD = &gExportViewData;
    }
    else
    {
        // default if nothing exported
        is.pSaveEFD = &gExportPrintData;
    }
    is.world[0] = (char)0;
    is.terrainFile[0] = (char)0;
    is.colorScheme[0] = (char)0;
    is.importFile = importFile;
    // set by memset
    //is.lineNumber = 0;
    //is.errorMessagesStringSize = 0;
    //is.errorMessages = NULL;
    is.processData = true;
}

// Read line, does not include \n at end.
// Returns 1 if valid line, 0 if valid and end of file detected, and -1 if line read was longer than buffer
static int readLine(FILE* fh, char* inputString, int stringLength)
{
    MY_ASSERT(stringLength > 0);	// avoid killing memory we shouldn't touch
    int pos = 0;
    int c;
    do {
        c = fgetc(fh);
        if (c != EOF)
            inputString[pos++] = (char)c;
        // line too long for input?
        if (pos >= stringLength - 1)
        {
            inputString[stringLength - 1] = '\0';
            return -1;
        }
    } while (c != EOF && c != '\n');
    // subtract off end of line
    if (c == '\n')
        pos--;
    inputString[pos] = '\0';

    return (c != EOF) ? 1 : 0;
}

// prepare line data: delete comment part of line, delete leading "#" if true and white space. Return new line start pointer.
static char* prepareLineData(char* line, bool model)
{
    char* lineLoc = line;
    char* strPtr = strstr(lineLoc, "//");
    if (strPtr != NULL) {
        // remove comment from end of line - note that this can also lose http:// data!
        *strPtr = (char)0;
    }

    // ignore pound comment, space, and tab characters
    boolean cont = true;
    while ((*lineLoc != (char)0) && cont) {
        if ((*lineLoc == '#' && model) || (*lineLoc == ' ') || (*lineLoc == '\t')) {
            lineLoc++;
        }
        else
        {
            cont = false;
        }
    }
    
    // double apostrophe characters around file names are not needed and cause problems, remove if reading a file.
    // Must keep otherwise, see https://github.com/erich666/Mineways/issues/69
    if (strstr(line, " path: ") || strstr(line, " name: ")) {
        char* parseLoc = lineLoc;
        while (*parseLoc != (char)0) {
            if (*parseLoc == '"') {
                // delete the current character, i.e., " 
                char* delLoc = parseLoc;
                while (*delLoc != (char)0) {
                    *delLoc++ = delLoc[1];
                }
            }
            parseLoc++;
        }
    }

    return lineLoc;
}

// Return true if we've entered a comment block. The line may still have useful stuff in it to process.
static bool dealWithCommentBlocks(char* line, bool commentBlock)
{
    // are we currently in a comment block?
    if (commentBlock) {
        // inside a /* comment block - see if it ends with a */ in this line
        line = closeCommentBlock(line);
        if (line)
        {
            // found the end of a comment block, so check if another block is starting, and note this and continue
            if (startCommentBlock(line))
            {
                // found the beginning of a comment block, so note this and continue;
                // the line is cleaned of this start and can be processed
                commentBlock = true;
            }
            else {
                // really is closed, no new one started.
                commentBlock = false;
            }
        }
        // else, still in comment block, so ignore this line
    }

    if (!commentBlock) {
        if (startCommentBlock(line))
        {
            // found the beginning of a comment block, so note this and continue;
            // the line is cleaned of this start and can be processed
            commentBlock = true;
        }
    }
    // true if we entered or are still in a comment block.
    return commentBlock;
}

// Find if a comment block starts in this line. Comment it out. Leaves beginning location of line unchanged.
static bool startCommentBlock(char* line)
{
    char* strPtr = strstr(line, "/*");
    if (strPtr != NULL)
    {
        // found the start of a comment block
        *strPtr = (char)0;
        // do we now find the end of a comment block in what remains of the line?
        char* endPtr = strstr(strPtr + 2, "*/");
        if (endPtr != NULL)
        {
            // started and ended comment block. Remove this comment from line and call again!
            // (note: line length is overstated here, but we know it can fit, since we're using the line's contents shifted over)
            strcat_s(line, IMPORT_LINE_LENGTH, endPtr + 2);
            return startCommentBlock(line);
        }
        // comment block started, didn't end
        return true;
    }
    return false;
}

// comment block is open, so close it if possible. Return rest of line after closing block, else NULL means not closed.
static char* closeCommentBlock(char* line)
{
    char* strPtr = strstr(line, "*/");
    if (strPtr != NULL)
    {
        // found the end of a comment block
        return strPtr + 2;
    }
    return NULL;
}

// Return 0 if this executes properly. Else return code should be passed up chain.
static int switchToNether(ImportedSet& is)
{
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE || gWorldGuide.type == WORLD_TEST_BLOCK_TYPE)
    {
        saveWarningMessage(is, L"attempt to switch to Nether but this world has none.");
        return INTERPRETER_FOUND_ERROR;
    }
    gOptions.worldType |= HELL;
    gOptions.worldType &= ~ENDER;
    // change scale as needed
    gCurX /= 8.0;
    gCurZ /= 8.0;
    // it's useless to view Nether from MAP_MAX_HEIGHT
    if (gCurDepth == gMaxHeight)
    {
        gCurDepth = 126;
        setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
    }
    gOverworldHideStatus = gOptions.worldType & HIDEOBSCURED;
    gOptions.worldType |= HIDEOBSCURED;

    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_OBSCURED, (gOptions.worldType & HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_HELL, (gOptions.worldType & HELL) ? MF_CHECKED : MF_UNCHECKED);
    if (gOptions.worldType & ENDER)
    {
        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_UNCHECKED);
        gOptions.worldType &= ~ENDER;
    }
    CloseAll();
    // clear selection when you switch from somewhere else to The Nether, or vice versa
    gHighlightOn = FALSE;
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
    enableBottomControl(gHighlightOn, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);

    return 0;
}

// Return 0 if this executes properly. Else return code should be passed up chain.
static int switchToTheEnd(ImportedSet& is)
{
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE || gWorldGuide.type == WORLD_TEST_BLOCK_TYPE)
    {
        saveWarningMessage(is, L"attempt to switch to The End level but this world has none.");
        return INTERPRETER_FOUND_ERROR;
    }
    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_CHECKED);
    // entering Ender, turn off hell if need be
    gOptions.worldType |= ENDER;
    if (gOptions.worldType & HELL)
    {
        // get out of hell zoom
        gCurX *= 8.0;
        gCurZ *= 8.0;
        // and undo other hell stuff
        if (gCurDepth == 126)
        {
            gCurDepth = gMaxHeight;
            setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
        }
        // turn off obscured, then restore overworld's obscured status
        gOptions.worldType &= ~HIDEOBSCURED;
        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_OBSCURED, MF_UNCHECKED);
        gOptions.worldType |= gOverworldHideStatus;
        // uncheck hell menu item
        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_HELL, MF_UNCHECKED);
        gOptions.worldType &= ~HELL;
    }

    CloseAll();
    // clear selection when you switch from somewhere else to The End, or vice versa
    gHighlightOn = FALSE;
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
    enableBottomControl(gHighlightOn, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);

    return 0;
}

static int interpretImportLine(char* line, ImportedSet& is)
{
    char* strPtr;
    int i, ret;
    char string1[100], string2[100], string3[100], string4[100];
    wchar_t error[1024];
    int modelStyle = ISE_NO_DATA_TYPE_FOUND;
    //int mx, my, mz, type, dataVal, biome;

    // if line is blank, let's move on, shall we?
    if (line[0] == (char)0)
        return INTERPRETER_FOUND_NOTHING_USEFUL;

    // find save file name
    // old style
    strPtr = findLineDataNoCase(line, "Extracted from Minecraft world saves/");
    if (strPtr == NULL) {
        // new script style:
        strPtr = findLineDataNoCase(line, "Minecraft world: ");
    }
    if (strPtr == NULL) {
        // newer still script style:
        strPtr = findLineDataNoCase(line, "World: ");
    }
    if (strPtr != NULL) {
        if (*strPtr == (char)0) {
            saveErrorMessage(is, L"no world given.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            // model or scripting - save path
            strcpy_s(is.world, MAX_PATH_AND_FILE, strPtr);
            if (!is.readingModel) {
                // scripting: load it
                // doesn't work as expected: sendStatusMessage(is.ws.hwndStatus, L"Importing model: loading world");
                if (!commandLoadWorld(is, error)) {
                    saveErrorMessage(is, error);
                    return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // find whether we're reading in a rendering or 3d printing file.
    // Use this only when reading in a model.
    strPtr = findLineDataNoCase(line, "Created for ");
    if (strPtr != NULL)
    {
        // print or render?
        if (*strPtr == '3') {
            modelStyle = ISE_3D_PRINT_DATA_TYPE;
        }
        else if (*strPtr == 'V') {
            modelStyle = ISE_RENDER_DATA_TYPE;
        }
        else {
            saveErrorMessage(is, L"could not determine whether model file is for 3D printing or rendering (a.k.a. viewing).", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
    }
    else {
        // try script command instead
        strPtr = findLineDataNoCase(line, "Set render type:");
        if (strPtr != NULL) {
            modelStyle = ISE_RENDER_DATA_TYPE;	// render
        }
        else {
            strPtr = findLineDataNoCase(line, "Set 3D print type:");
            if (strPtr != NULL)
                modelStyle = ISE_3D_PRINT_DATA_TYPE;	// 3d print
        }
    }
    // did we find "Created for" or "Set render type:"?
    if (strPtr != NULL) {
        MY_ASSERT(modelStyle != ISE_NO_DATA_TYPE_FOUND);
        is.exportTypeFound = modelStyle;
        if (is.readingModel) {
            if (is.exportTypeFound == ISE_3D_PRINT_DATA_TYPE) {
                *is.pEFD = gExportPrintData;
                is.pSaveEFD = &gExportPrintData;
                initializePrintExportData(*is.pEFD);
            }
            else {
                *is.pEFD = gExportViewData;
                is.pSaveEFD = &gExportViewData;
                initializeViewExportData(*is.pEFD);
            }
        }
        // else scripting - save?
        else if (is.processData) {
            if (is.exportTypeFound == ISE_3D_PRINT_DATA_TYPE) {
                is.pEFD = is.pSaveEFD = &gExportPrintData;
            }
            else {
                is.pEFD = is.pSaveEFD = &gExportViewData;
            }
            // make it active
            gpEFD = is.pEFD;
        }

        // file type - if not found, abort;
        // older files don't have this info
        if (strstr(strPtr, "Wavefront OBJ absolute indices"))
        {
            is.pEFD->fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
        }
        else if (strstr(strPtr, "Wavefront OBJ relative indices"))
        {
            is.pEFD->fileType = FILE_TYPE_WAVEFRONT_REL_OBJ;
        }
        else if (strstr(strPtr, "USD 1.0")) // just USDA for now
        {
            is.pEFD->fileType = FILE_TYPE_USD;
        }
        else if (strstr(strPtr, "Binary STL iMaterialise"))
        {
            is.pEFD->fileType = FILE_TYPE_BINARY_MAGICS_STL;
        }
        else if (strstr(strPtr, "Binary STL VisCAM"))
        {
            is.pEFD->fileType = FILE_TYPE_BINARY_VISCAM_STL;
        }
        else if (strstr(strPtr, "ASCII STL"))
        {
            is.pEFD->fileType = FILE_TYPE_ASCII_STL;
        }
        else if (strstr(strPtr, "VRML 2.0"))
        {
            is.pEFD->fileType = FILE_TYPE_VRML2;
        }
        else
        {
            if (is.readingModel) {
                // can't figure it out from the file (must be old style, before this info was added), so figure it out
                // from the file name itself.
                if (wcsstr(is.importFile, L".obj"))
                {
                    is.pEFD->fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
                }
                else if (wcsstr(is.importFile, L".txt"))
                {
                    is.pEFD->fileType = FILE_TYPE_BINARY_MAGICS_STL;
                }
                else if (wcsstr(is.importFile, L".usda"))
                {
                    is.pEFD->fileType = FILE_TYPE_USD;
                }
                else if (wcsstr(is.importFile, L".wrl"))
                {
                    is.pEFD->fileType = FILE_TYPE_VRML2;
                }
                else {
                    saveErrorMessage(is, L"could not determine what type of model file (OBJ, USD, VRML, STL) is being read.", strPtr);
                    return INTERPRETER_FOUND_ERROR;
                }
            }
            else {
                saveErrorMessage(is, L"could not determine what type of model file (OBJ, USD, VRML, STL) is desired.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        // survived - return
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Terrain file name:");
    if (strPtr != NULL) {
        if (*strPtr == (char)0) {
            saveErrorMessage(is, L"no terrain file given.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            // TODO - somehow, this message never gets replaced when reloading world. sendStatusMessage(is.ws.hwndStatus, L"Importing model: reading terrain file");
            strcpy_s(is.terrainFile, MAX_PATH_AND_FILE, strPtr);
            cleanseBackslashes(is.terrainFile);
            if (!is.readingModel) {
                if (!commandLoadTerrainFile(is, error)) {
                    saveErrorMessage(is, error);
                    return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "View Overworld");
    if (strPtr != NULL) {
        if (is.processData) {
            // simply ignored if read inside a model, since overworld is the default
            if (!is.readingModel) {
                if (gLoaded) {
                    gotoSurface(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel);
                    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_OBSCURED, (gOptions.worldType & HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
                    if (gOptions.worldType & ENDER)
                    {
                        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_UNCHECKED);
                        gOptions.worldType &= ~ENDER;
                    }
                    CloseAll();
                    // clear selection when you switch from somewhere else to The Nether, or vice versa
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_CLEAR);
                    enableBottomControl(gHighlightOn, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);
                }
                else {
                    // warning: selection set but no world is loaded
                    saveErrorMessage(is, L"attempt to view Overworld but no world is loaded.");
                    return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "View Nether");
    if (strPtr != NULL) {
        if (is.processData) {
            is.nether = true;
            if (!is.readingModel) {
                if (gLoaded) {
                    //sendStatusMessage(is.ws.hwndStatus, L"Importing model: switching to Nether");
                    ret = switchToNether(is);
                    if (ret)
                        return ret;
                }
                else {
                    // warning: selection set but no world is loaded
                    saveErrorMessage(is, L"attempt to view Nether but no world is loaded.");
                    return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "View The End");
    if (strPtr != NULL) {
        if (is.processData) {
            is.theEnd = true;
            if (!is.readingModel) {
                if (gLoaded) {
                    //sendStatusMessage(is.ws.hwndStatus, L"Importing model: switching to The End world");
                    ret = switchToTheEnd(is);
                    if (ret)
                        return ret;
                }
                else {
                    // warning: selection set but no world is loaded
                    saveErrorMessage(is, L"attempt to view The End but no world is loaded.");
                    return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "Color scheme:");
    if (strPtr != NULL) {
        if (*strPtr == (char)0) {
            saveErrorMessage(is, L"no color scheme given.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            strcpy_s(is.colorScheme, MAX_PATH_AND_FILE, strPtr);
            if (!is.readingModel) {
                if (!commandLoadColorScheme(is, error)) {
                    // we happen to know this method only returns warnings.
                    saveWarningMessage(is, error);
                    return INTERPRETER_FOUND_VALID_LINE;
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // find size - we do this operation the same earlier in script interpretation, as it needs to immediately take effect.
    strPtr = findLineDataNoCase(line, "Selection location min to max:");
    // alternate format
    if (strPtr == NULL)
        strPtr = findLineDataNoCase(line, "Selection location:");
    if (strPtr != NULL) {
        bool noSelection = false;
        // note we store the box separately, as it will get cleared when we run into 
        int v[6];
        char cleanString[1024];
        cleanStringForLocations(cleanString, strPtr);
        if (6 != sscanf_s(cleanString, "%d %d %d to %d %d %d",
            &v[0], &v[1], &v[2],
            &v[3], &v[4], &v[5])) {
            if (1 == sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
            {
                if (_stricmp(string1, "none") == 0) {
                    noSelection = true;
                }
                else if (_stricmp(string1, "all") == 0) {
                    // well, not quite all, but good enough for almost all schematics
                    // TODO: could make it apply to just schematics, but this is actually kind of handy
                    // for Creative superflat worlds where you just want to export a model.
                    v[0] = v[2] = -5000;
                    v[1] = gMinHeight;
                    v[3] = v[5] = 5000;
                    v[4] = gMaxHeight;
                }
                else {
                    // bad parse - warn and quit
                    goto SelectionParseError;
                }
            }
            else {
                // bad parse - warn and quit
            SelectionParseError:
                saveErrorMessage(is, L"could not read selection values 'x, y, z to x, y, z' or 'none' tag.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        if (!noSelection) {
            // check Y bounds
            // unfortunately, the world is read in AFTER the selection range is set, so we have to trust it's not wonky
            if (v[1] < ABSOLUTE_MIN_MAP_HEIGHT || v[1] > ABSOLUTE_MAX_MAP_HEIGHT) {
                saveErrorMessage(is, L"selection out of bounds; minimum y value outside of valid range.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
            if (v[4] < ABSOLUTE_MIN_MAP_HEIGHT || v[4] > ABSOLUTE_MAX_MAP_HEIGHT) {
                saveErrorMessage(is, L"selection out of bounds; maximum y value outside of valid range.", strPtr);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        // if we process the data in this run, do it
        if (is.processData) {
            // two cases: reading in a model's header or scripting. Model header reading defers loading the world, so process the data.
            if (is.readingModel || gLoaded) {
                // is nothing selected? i.e., "Selection location: none"?
                if (noSelection) {
                    // for scripting we could test that there's no world loaded, but it's fine to call this then anyway.
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, gTargetDepth, 0, 0, gCurDepth, 0, gMinHeight, gMaxHeight, gLoaded ? HIGHLIGHT_UNDO_PUSH : HIGHLIGHT_UNDO_CLEAR);
                }
                else {
                    // yes, a real selection is being made
                    is.minxVal = v[0];
                    is.minyVal = v[1];
                    is.minzVal = v[2];
                    is.maxxVal = v[3];
                    is.maxyVal = v[4];
                    is.maxzVal = v[5];
                    // are we using scripting?
                    if (!is.readingModel) {
                        // Scripting; is a world loaded? If not, then don't set the selection
                        if (gLoaded) {
                            gCurX = (is.minxVal + is.maxxVal) / 2;
                            gCurZ = (is.minzVal + is.maxzVal) / 2;

                            gHighlightOn = true;
                            SetHighlightState(1, is.minxVal, is.minyVal, is.minzVal, is.maxxVal, is.maxyVal, is.maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
                            enableBottomControl(1, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);
                            // put target (bottom) depth to new depth set, if any
                            gTargetDepth = is.minyVal;
                            gCurDepth = is.maxyVal;

                            // don't bother updating status in commands, do that after all is done
                            //gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(1) - MAIN_WINDOW_TOP, gCurX, gCurZ,
                            //	bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
                            //updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, is.ws.hwndStatus);
                            setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
                            setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, false);
                        }
                        //else {
                        //    // shouldn't really be able to reach this line, as is.readingModel is false and gLoaded is false.
                        //    // Moved this message further down.
                        //    saveErrorMessage(is, L"selection set but no world is loaded.");
                        //    return INTERPRETER_FOUND_ERROR;
                        //}
                    }
                }
            }
            else {
                // we are not reading in a model's header (where a deferred selection is fine), i.e., we're scripting and a world is not loaded.
                saveErrorMessage(is, L"selection set but no world is loaded. For scripting, you must load a world manually before using a script or must include the 'Minecraft world: your world directory' command in your script.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // find whether we're reading in a rendering or 3d printing file
    strPtr = findLineDataNoCase(line, "Units for the model vertex data itself:");
    if (strPtr != NULL) {
        // found selection, parse it
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find units for the model itself.");
            return INTERPRETER_FOUND_ERROR;
        }
        for (i = 0; i < MODELS_UNITS_TABLE_SIZE; i++)
        {
            if (_stricmp(gUnitTypeTable[i].name, string1) == 0)
            {
                if (is.processData)
                    is.pEFD->comboModelUnits[is.pEFD->fileType] = i;
                break;
            }
        }
        // units found?
        if (i >= MODELS_UNITS_TABLE_SIZE)
        {
            saveErrorMessage(is, L"could not interpret unit type for the model itself.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    // lighting and atmosphere
    int retCode = 0;
    if (findBitToggle(line, is, "Elevation shading", DEPTHSHADING, IDM_DEPTH, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Lighting", LIGHTING, IDM_LIGHTING, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Transparent water", TRANSPARENT_WATER, IDM_TRANSPARENT_WATER, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Map grid", MAP_GRID, IDM_MAPGRID, &retCode))
        return retCode;

    strPtr = findLineDataNoCase(line, "File type:");
    if (strPtr != NULL) {
        // found selection, parse it
        if (1 != sscanf_s(strPtr, "Export %s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find Export string for file type (solid color, textured, etc.)."); return INTERPRETER_FOUND_ERROR;
        }
        // shortcut here, just look for first word after "Export "
        char* outputTypeString[] = {
            "no", // "Export no materials",
            "solid", // "Export solid material colors only (no textures)",
            "richer", // "Export richer color textures",
            "noise", // "Export noise textures with color",
            "full", // "Export full color texture patterns",
            "all", // "Export all textures to three large images",
            "tiles", // "Export tiles for textures"
            "separate", // "Export separate textures"
            "individual" // "Export individual textures"
        };
        // should really use a struct - corresponds to those above.
        int outputTypeCorrespondence[] = { 0,1,2,2,3,3,4,4,4 };
        int outputTypeEntries = sizeof(outputTypeCorrespondence) / sizeof(outputTypeCorrespondence[0]);
        for (i = 0; i < outputTypeEntries; i++)
        {
            if (_stricmp(outputTypeString[i], string1) == 0)
            {
                break;
            }
        }
        if (i >= outputTypeEntries) {
            saveErrorMessage(is, L"could not interpret file type (solid color, textured, etc.).", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            is.pEFD->radioExportNoMaterials[is.pEFD->fileType] = 0;
            is.pEFD->radioExportMtlColors[is.pEFD->fileType] = 0;
            is.pEFD->radioExportSolidTexture[is.pEFD->fileType] = 0;
            is.pEFD->radioExportFullTexture[is.pEFD->fileType] = 0;
            is.pEFD->radioExportTileTextures[is.pEFD->fileType] = 0;
            switch (outputTypeCorrespondence[i])
            {
            case 0:
                is.pEFD->radioExportNoMaterials[is.pEFD->fileType] = 1;
                break;
            case 1:
                is.pEFD->radioExportMtlColors[is.pEFD->fileType] = 1;
                break;
            case 2:
                is.pEFD->radioExportSolidTexture[is.pEFD->fileType] = 1;
                break;
            case 3:
                is.pEFD->radioExportFullTexture[is.pEFD->fileType] = 1;
                break;
            case 4:
                is.pEFD->radioExportTileTextures[is.pEFD->fileType] = 1;
                // and retrieve path
                {
                    strPtr = findLineDataNoCase(line, "File type: Export individual textures to directory ");
                    if (strPtr != NULL) {
                        strcpy_s(is.pEFD->tileDirString, MAX_PATH, strPtr);
                    }
                    else {
                        // old format
                        strPtr = findLineDataNoCase(line, "File type: Export separate textures to directory ");
                        if (strPtr != NULL) {
                            strcpy_s(is.pEFD->tileDirString, MAX_PATH, strPtr);
                        }
                        else {
                            // old format
                            strPtr = findLineDataNoCase(line, "File type: Export tiles for textures to directory ");
                            if (strPtr != NULL) {
                                strcpy_s(is.pEFD->tileDirString, MAX_PATH, strPtr);
                            }
                        }
                    }
                }
                break;
            default:
                assert(0);
                break;
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Texture output RGB:");
    if (strPtr != NULL) {
        // this next chunk could almost be a subroutine, but there's so much passed in and out, not worth it.
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Texture output RGB' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkTextureRGB = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Texture output A:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Texture output A' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkTextureA = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Texture output RGBA:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Texture output RGBA' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkTextureRGBA = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Make groups objects:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Make groups objects' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMakeGroupsObjects = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Export separate objects:");
    if (strPtr == NULL) {
        // new name to match export dialog's text, as of 10.13
        strPtr = findLineDataNoCase(line, "Export separate types:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Export separate objects' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkSeparateTypes = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Individual blocks:");
    if (strPtr == NULL) {
        // new name to match export dialog's text, as of 10.13
        strPtr = findLineDataNoCase(line, "Export individual blocks:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Individual blocks' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkIndividualBlocks[is.pEFD->fileType] = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    // try both variants
    strPtr = findLineDataNoCase(line, "Material per family:");
    if (strPtr == NULL) {
        strPtr = findLineDataNoCase(line, "Material per object:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Material per family' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMaterialPerFamily = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Split materials into subtypes:");
    if (strPtr == NULL) {
        // new, somewhat less confusing name, as of 7.0
        strPtr = findLineDataNoCase(line, "Split by block type:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Split by block type' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkSplitByBlockType = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "G3D full material:");
    if (strPtr == NULL) {
        // new name to match export dialog's text, as of 10.13
        strPtr = findLineDataNoCase(line, "Custom material:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'G3D full material' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkCustomMaterial[is.pEFD->fileType] = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Export MDL:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Export MDL' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkExportMDL = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Light scale:");
    if (strPtr != NULL) {
        float fLightScale;
        if (1 != sscanf_s(strPtr, "%f", &fLightScale) )
        {
            saveErrorMessage(is, L"could not interpret value for Light scale command."); return INTERPRETER_FOUND_ERROR;
        }
        if (fLightScale < 0.0f) {
            saveErrorMessage(is, L"Light scale value must be a non-negative number.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            is.pEFD->scaleLightsVal = fLightScale;
        }

        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Surface emit scale:");
    if (strPtr != NULL) {
        float fSurfaceEmitScale;
        if (1 != sscanf_s(strPtr, "%f", &fSurfaceEmitScale))
        {
            saveErrorMessage(is, L"could not interpret value for Surface emit scale command."); return INTERPRETER_FOUND_ERROR;
        }
        if (fSurfaceEmitScale < 0.0f) {
            saveErrorMessage(is, L"Surface emit scale value must be a non-negative number.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            is.pEFD->scaleEmittersVal = fSurfaceEmitScale;
        }

        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Export lesser blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Export lesser blocks' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkExportAll = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Fatten lesser blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Fatten lesser blocks' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkFatten = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Simplify mesh:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Simplify mesh' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkDecimate = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Create composite overlay faces:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Create composite overlay faces' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkCompositeOverlay = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Center model:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Center model' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkCenterModel = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Create block faces at the borders:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Create block faces at the borders' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkBlockFacesAtBorders = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Make tree leaves solid:");
    if (strPtr == NULL) {
        // new name to match export dialog's text, as of 10.13
        strPtr = findLineDataNoCase(line, "Tree leaves solid:");
    }
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Make tree leaves solid' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkLeavesSolid = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Use biomes:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Use biomes' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData) {
            is.pEFD->chkBiome = interpretBoolean(string1);
            if (!is.readingModel) {
                if (is.pEFD->chkBiome)
                    // turn bit on
                    gOptions.worldType |= BIOMES;
                else
                    // turn bit off
                    gOptions.worldType &= ~BIOMES;
                CheckMenuItem(GetMenu(is.ws.hWnd), IDM_VIEW_SHOWBIOMES, (gOptions.worldType & BIOMES) ? MF_CHECKED : MF_UNCHECKED);
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    float floatVal = 0.0f;
    strPtr = findLineDataNoCase(line, "Rotate model ");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%f degrees", &floatVal))
        {
            saveErrorMessage(is, L"could not interpret degrees value for Rotate model.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if ((floatVal != 0.0f) && (floatVal != 90.0f) && (floatVal != 180.0f) && (floatVal != 270.0f)) {
            saveErrorMessage(is, L"model scale value must be a positive number for Rotate model.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            is.pEFD->radioRotate0 = (floatVal == 0.0f);
            is.pEFD->radioRotate90 = (floatVal == 90.0f);
            is.pEFD->radioRotate180 = (floatVal == 180.0f);
            is.pEFD->radioRotate270 = (floatVal == 270.0f);

            // just in case
            if (!(is.pEFD->radioRotate90 || is.pEFD->radioRotate180 || is.pEFD->radioRotate270)) {
                //if (!is.pEFD->radioRotate0) {
                    // nothing set, so set one
                is.pEFD->radioRotate0 = 1;
                //} // else 0 degrees is set
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Make Z the up direction instead of Y:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Make Z' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMakeZUp[is.pEFD->fileType] = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Scale model by ");
    if (strPtr != NULL) {
        bool materialSet = false;
        is.pEFD->radioScaleByBlock = is.pEFD->radioScaleByCost = is.pEFD->radioScaleToHeight = is.pEFD->radioScaleToMaterial = 0;
        // oddly, "making each block 100 mmm high" passes - maybe the end of the string is ignored after %f?
        if (1 == sscanf_s(strPtr, "making each block %f mm high", &floatVal))
        {
            if (floatVal <= 0.0f) {
                saveErrorMessage(is, L"model scale value must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
            }
            if (is.processData) {
                is.pEFD->radioScaleByBlock = 1;
                is.pEFD->blockSizeVal[is.pEFD->fileType] = floatVal;
            }

            goto SetPrintMaterialType;
        }
        else
        {
            // terrible hackery and laziness: look for 3, 2, or 1 word materials. fgets or read() might be better...
            if (5 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s %s %s", &floatVal, string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2), string3, (unsigned)_countof(string3), string4, (unsigned)_countof(string4)))
            {
                if (_stricmp(string4, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                strcat_s(string1, _countof(string1), " ");
                strcat_s(string1, _countof(string1), string2);
                strcat_s(string1, _countof(string1), " ");
                strcat_s(string1, _countof(string1), string3);
                if (is.processData) {
                    is.pEFD->radioScaleByCost = 1;
                    is.pEFD->costVal = floatVal;
                }
                materialSet = true;

                goto SetPrintMaterialType;
            }
            else if (4 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s %s", &floatVal, string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2), string3, (unsigned)_countof(string3)))
            {
                if (_stricmp(string3, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                strcat_s(string1, _countof(string1), " ");
                strcat_s(string1, _countof(string1), string2);
                if (is.processData) {
                    is.pEFD->radioScaleByCost = 1;
                    is.pEFD->costVal = floatVal;
                }
                materialSet = true;

                goto SetPrintMaterialType;
            }
            else if (3 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s", &floatVal, string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2)))
            {
                if (_stricmp(string2, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (is.processData) {
                    is.pEFD->radioScaleByCost = 1;
                    is.pEFD->costVal = floatVal;
                }
                materialSet = true;

                goto SetPrintMaterialType;
            }
            else if (1 == sscanf_s(strPtr, "fitting to a height of %f cm", &floatVal))
            {
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"height must be a positive number for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (is.processData) {
                    is.pEFD->radioScaleToHeight = 1;
                    is.pEFD->modelHeightVal = floatVal;
                }

                goto SetPrintMaterialType;
            }
            else
            {
                // terrible hackery and laziness: look for 3, 2, or 1 word materials. fgets or read() might be better...
                if (4 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s %s %s", string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2), string3, (unsigned)_countof(string3), string4, (unsigned)_countof(string4)))
                {
                    if (_stricmp(string4, "material") != 0)
                    {
                        saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                    }
                    strcat_s(string1, _countof(string1), " ");
                    strcat_s(string1, _countof(string1), string2);
                    strcat_s(string1, _countof(string1), " ");
                    strcat_s(string1, _countof(string1), string3);
                    if (is.processData) {
                        is.pEFD->radioScaleToMaterial = 1;
                    }
                    materialSet = true;

                    goto SetPrintMaterialType;
                }
                else if (3 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s %s", string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2), string3, (unsigned)_countof(string3)))
                {
                    if (_stricmp(string3, "material") != 0)
                    {
                        saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                    }
                    strcat_s(string1, _countof(string1), " ");
                    strcat_s(string1, _countof(string1), string2);
                    if (is.processData) {
                        is.pEFD->radioScaleToMaterial = 1;
                    }
                    materialSet = true;

                    goto SetPrintMaterialType;
                }
                else if (2 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s", string1, (unsigned)_countof(string1), string2, (unsigned)_countof(string2)))
                {
                    if (_stricmp(string2, "material") != 0)
                    {
                        saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                    }
                    if (is.processData) {
                        is.pEFD->radioScaleToMaterial = 1;
                    }
                    materialSet = true;

                    goto SetPrintMaterialType;
                }
            }
        }
        saveErrorMessage(is, L"could not understand scale line for Scale model command.", strPtr);
        return INTERPRETER_FOUND_ERROR;

    SetPrintMaterialType:
        // if by cost or by material, need to set the material type.
        if (materialSet)
        {
            for (i = 0; i < MTL_COST_TABLE_SIZE; i++)
            {
                // ignore case
                if (_stricmp(string1, gMtlCostTable[i].name) == 0)
                {
                    break;
                }
            }
            if (i >= MTL_COST_TABLE_SIZE)
            {
                saveErrorMessage(is, L"could not find name of material for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
            }

            if (is.processData)
                is.pEFD->comboPhysicalMaterial[is.pEFD->fileType] = i;
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Fill air bubbles:");
    if (strPtr != NULL) {
        if (3 != sscanf_s(strPtr, "%s Seal off entrances: %s Fill in isolated tunnels in base of model: %s",
            string1, (unsigned)_countof(string1),
            string2, (unsigned)_countof(string2),
            string3, (unsigned)_countof(string3)
        ))
        {
            saveErrorMessage(is, L"could not find all boolean values for Fill air bubbles commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (!validBoolean(is, string2)) return INTERPRETER_FOUND_ERROR;
        if (!validBoolean(is, string3)) return INTERPRETER_FOUND_ERROR;

        if (is.processData) {
            is.pEFD->chkFillBubbles = interpretBoolean(string1);
            is.pEFD->chkSealEntrances = interpretBoolean(string2);
            is.pEFD->chkSealSideTunnels = interpretBoolean(string3);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Connect parts sharing an edge:");
    if (strPtr != NULL) {
        if (3 != sscanf_s(strPtr, "%s Connect corner tips: %s Weld all shared edges: %s",
            string1, (unsigned)_countof(string1),
            string2, (unsigned)_countof(string2),
            string3, (unsigned)_countof(string3)
        ))
        {
            saveErrorMessage(is, L"could not find all boolean values for Connect parts commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (!validBoolean(is, string2)) return INTERPRETER_FOUND_ERROR;
        if (!validBoolean(is, string3)) return INTERPRETER_FOUND_ERROR;

        if (is.processData) {
            is.pEFD->chkConnectParts = interpretBoolean(string1);
            is.pEFD->chkConnectCornerTips = interpretBoolean(string2);
            is.pEFD->chkConnectAllEdges = interpretBoolean(string3);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    int intVal = 0;
    strPtr = findLineDataNoCase(line, "Delete floating objects:");
    if (strPtr != NULL) {
        if (2 != sscanf_s(strPtr, "trees and parts smaller than %d blocks: %s",
            &intVal,
            string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find all parameters needed for Delete floating objects command.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (intVal < 0) {
            saveErrorMessage(is, L"number of blocks cannot be negative.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData) {
            is.pEFD->chkDeleteFloaters = interpretBoolean(string1);
            is.pEFD->floaterCountVal = intVal;
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Hollow out bottom of model, making the walls ");
    if (strPtr != NULL) {
        if (3 != sscanf_s(strPtr, "%f mm thick: %s Superhollow: %s",
            &floatVal,
            string1, (unsigned)_countof(string1),
            string2, (unsigned)_countof(string2)
        ))
        {
            saveErrorMessage(is, L"could not find all parameters needed for Hollow commands.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (!validBoolean(is, string2)) return INTERPRETER_FOUND_ERROR;

        if (is.processData) {
            is.pEFD->hollowThicknessVal[is.pEFD->fileType] = floatVal;
            is.pEFD->chkHollow[is.pEFD->fileType] = interpretBoolean(string1);
            is.pEFD->chkSuperHollow[is.pEFD->fileType] = interpretBoolean(string2);
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Melt snow blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Melt snow blocks' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMeltSnow = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Debug: show separate parts as colors:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Debug parts' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkShowParts = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Debug: show weld blocks in bright colors:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Debug weld blocks' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkShowWelds = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    // note end of file if reading a model
    if (is.readingModel) {
        if (findLineDataNoCase(line, "Full current path")) {
            // end - not an error; we simply found a line that notes there's no model import settings past this line
            return INTERPRETER_FOUND_VALID_EXPORT_LINE | INTERPRETER_END_READING;
        }
    }

    // nothing found
    return INTERPRETER_FOUND_NOTHING_USEFUL;
}

static int interpretScriptLine(char* line, ImportedSet& is)
{
    char* strPtr, * strPtr2;
    char string1[100], string2[100], string3[100];
    int on, minx, miny, minz, maxx, maxy, maxz;
    wchar_t error[1024];
    int retCode = INTERPRETER_FOUND_VALID_LINE;

    // if line is blank, let's move on, shall we? By saying this is valid, we say we have processed it.
    if (line[0] == (char)0)
        return INTERPRETER_FOUND_VALID_LINE;

    // export world, the king of commands...
    strPtr = findLineDataNoCase(line, "Export ");
    if (strPtr != NULL) {
        int model = -1;
        strPtr2 = findLineDataNoCase(strPtr, "for Rendering:");
        if (strPtr2 != NULL) {
            model = RENDERING_EXPORT;
        }
        else {
            strPtr2 = findLineDataNoCase(strPtr, "for 3D Printing:");
            if (strPtr2 != NULL) {
                model = PRINTING_EXPORT;
            }
            else {
                strPtr2 = findLineDataNoCase(strPtr, "Schematic:");
                if (strPtr2 != NULL) {
                    model = SCHEMATIC_EXPORT;
                }
                else {
                    strPtr2 = findLineDataNoCase(strPtr, "Map:");
                    if (strPtr2 != NULL) {
                        model = MAP_EXPORT;  // 3 is sketchfab
                    }
                }
            }
        }
        if (model == -1) {
            // didn't find a full match, so try other commands
            goto JumpToSpawn;
        }
        // ok, have the model mode, is there a file name?
        if (strPtr2[0] == (char)0) {
            saveErrorMessage(is, L"no export file name provided.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            if (!commandExportFile(is, error, model, strPtr2)) {
                saveErrorMessage(is, error);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

#ifdef SKETCHFAB

    strPtr = findLineDataNoCase(line, "Sketchfab token: ");
    if (strPtr != NULL) {
        strcpy_s(gSkfbPData.skfbApiToken, SKFB_TOKEN_LIMIT + 1, strPtr);
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Sketchfab title: ");
    if (strPtr != NULL) {
        if (!isSketchfabFieldSizeValid(strPtr, SKFB_NAME_LIMIT))
        {
            saveErrorMessage(is, L"Sketchfab title is too long (max 48 char.)");
            return INTERPRETER_FOUND_ERROR;
        }
        strcpy_s(gSkfbPData.skfbName, SKFB_NAME_LIMIT + 1, strPtr);
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Sketchfab description: ");
    if (strPtr != NULL) {
        if (!isSketchfabFieldSizeValid(strPtr, SKFB_DESC_LIMIT))
        {
            saveErrorMessage(is, L"Sketchfab description is too long (max 1024 char.)");
            return INTERPRETER_FOUND_ERROR;
        }
        strcpy_s(gSkfbPData.skfbDescription, SKFB_DESC_LIMIT + 1, strPtr);
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Sketchfab tags: ");
    if (strPtr != NULL) {
        if (!isSketchfabFieldSizeValid(strPtr, SKFB_TAG_LIMIT))
        {
            saveErrorMessage(is, L"Sketchfab tags is too long (max 29 char.)");
            return INTERPRETER_FOUND_ERROR;
        }
        strcpy_s(gSkfbPData.skfbTags, SKFB_TAG_LIMIT + 1, strPtr);
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Sketchfab private");
    if (strPtr != NULL) {
        gSkfbPData.skfbPrivate = true;
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Sketchfab password: ");
    if (strPtr != NULL) {
        if (!isSketchfabFieldSizeValid(strPtr, SKFB_PASSWORD_LIMIT))
        {
            saveErrorMessage(is, L"Sketchfab password is too long (max 64 char.)");
            return INTERPRETER_FOUND_ERROR;
        }
        strcpy_s(gSkfbPData.skfbPassword, SKFB_PASSWORD_LIMIT + 1, strPtr);
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Publish to Sketchfab");
    if (strPtr != NULL) {
        if (gSkfbPData.skfbApiToken[0] == '\0') {
            saveErrorMessage(is, L"No API Token provided.");
            return INTERPRETER_FOUND_ERROR;
        }
        else if (!isSketchfabFieldSizeValid(&gSkfbPData.skfbApiToken[0], SKFB_TOKEN_LIMIT, true))
        {
            saveErrorMessage(is, L"Sketchfab api token is invalid (should be 32 char)");
            return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            if (!commandSketchfabPublish(is, error)) {
                saveErrorMessage(is, error);
                return INTERPRETER_FOUND_ERROR;
            }
        }

        return INTERPRETER_FOUND_VALID_LINE;
    }


#endif

JumpToSpawn:
    strPtr = findLineDataNoCase(line, "Jump to Spawn");
    if (strPtr != NULL) {
        if (is.processData) {
            if (gLoaded) {
                gCurX = gSpawnX;
                gCurZ = gSpawnZ;
                if (gOptions.worldType & HELL)
                {
                    gCurX /= 8.0;
                    gCurZ /= 8.0;
                }
            }
            else {
                saveErrorMessage(is, L"Jump to Spawn command failed, as no world has been loaded.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }


    strPtr = findLineDataNoCase(line, "Jump to Player");
    if (strPtr != NULL) {
        if (is.processData) {
            if (gLoaded) {
                gCurX = gPlayerX;
                gCurZ = gPlayerZ;
                if (gOptions.worldType & HELL)
                {
                    gCurX /= 8.0;
                    gCurZ /= 8.0;
                }
            }
            else {
                saveErrorMessage(is, L"Jump to Player command failed, as no world has been loaded.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }


    strPtr = findLineDataNoCase(line, "Jump to Model");
    if (strPtr != NULL) {
        if (is.processData) {
            if (gLoaded) {
                if (!gHighlightOn)
                {
                    saveErrorMessage(is, L"Jump to Model command failed, as nothing has been selected.");
                    return INTERPRETER_FOUND_ERROR;
                }
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                // should always be on, but just in case...
                if (on)
                {
                    gCurX = (minx + maxx) / 2;
                    gCurZ = (minz + maxz) / 2;
                    if (gOptions.worldType & HELL)
                    {
                        gCurX /= 8.0;
                        gCurZ /= 8.0;
                    }
                }
                else {
                    MY_ASSERT(gAlwaysFail);
                }
            }
            else {
                saveErrorMessage(is, L"Jump to Model command failed, as no world has been loaded.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }


    strPtr = findLineDataNoCase(line, "Reset export options:");
    if (strPtr != NULL) {
        if (strstr(strPtr, "Render")) {
            if (is.processData) {
                initializeViewExportData(gExportViewData);
            }
        }
        else if (strstr(strPtr, "3D Print")) {
            if (is.processData) {
                initializePrintExportData(gExportPrintData);
            }
        }
        else if (strstr(strPtr, "Schematic")) {
            if (is.processData) {
                InitializeSchematicExportData(gExportSchematicData);
            }
        }
        else {
            saveErrorMessage(is, L"could not determine what is to be reset. Options are 'Render', '3D Print', and 'Schematic'.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }


    strPtr = findLineDataNoCase(line, "Focus view:");
    if (strPtr != NULL) {
        // note we store the box separately, as it will get cleared when we run into 
        char cleanString[1024];
        cleanStringForLocations(cleanString, strPtr);
        int v[2];
        if (2 != sscanf_s(cleanString, "%d %d", &v[0], &v[1])) {
            // bad parse - warn and quit
            saveErrorMessage(is, L"could not read 'Focus view' coordinates.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            // is a world loaded? If not, then don't set the selection
            if (gLoaded) {
                gCurX = v[0];
                gCurZ = v[1];
            }
            else {
                // error: selection set but no world is loaded
                saveErrorMessage(is, L"focus view set but no world is loaded.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }


    strPtr = findLineDataNoCase(line, "Zoom:");
    if (strPtr != NULL) {
        // note we store the box separately, as it will get cleared when we run into 
        int v;
        if (1 != sscanf_s(strPtr, "%d", &v)) {
            // bad parse - warn and quit
            saveErrorMessage(is, L"could not read 'Zoom' value.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if ((v < 1) || (v > 40)) {
            saveErrorMessage(is, L"zoom factor must be from 1 to 40, inclusive.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            // is a world loaded? If not, then don't set the selection
            if (gLoaded) {
                gCurScale = v;
            }
            else {
                // error: selection set but no world is loaded
                saveErrorMessage(is, L"zoom set but no world is loaded.");
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    if (findBitToggle(line, is, "Show all objects", SHOWALL, IDM_SHOWALLOBJECTS, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Show biomes", BIOMES, IDM_VIEW_SHOWBIOMES, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Elevation shading", DEPTHSHADING, IDM_DEPTH, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Lighting", LIGHTING, IDM_LIGHTING, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Cave mode", CAVEMODE, IDM_CAVEMODE, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Hide obscured", HIDEOBSCURED, IDM_OBSCURED, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Transparent water", TRANSPARENT_WATER, IDM_TRANSPARENT_WATER, &retCode))
        return retCode;
    if (findBitToggle(line, is, "Map grid", MAP_GRID, IDM_MAPGRID, &retCode))
        return retCode;

    strPtr = findLineDataNoCase(line, "Give more export memory:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Give more export memory' command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            gOptions.moreExportMemory = interpretBoolean(string1);
            MinimizeCacheBlocks(gOptions.moreExportMemory);
            CheckMenuItem(GetMenu(is.ws.hWnd), IDM_HELP_GIVEMEMOREMEMORY, (gOptions.moreExportMemory) ? MF_CHECKED : MF_UNCHECKED);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Close");
    if (strPtr != NULL) {
        removeLeadingWhitespace(strPtr);
        if (strPtr[0] != (char)0)
        {
            saveErrorMessage(is, L"command Close must be the only word on the line, other than comments.");
            return INTERPRETER_FOUND_ERROR;
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_FOUND_CLOSE;
    }

    ////////////////////////////////////
    // Keyboard-related commands

    strPtr = findLineDataNoCase(line, "Select minimum height:");
    if (strPtr != NULL) {
        // set value for testing
        int minHeight = 0;
        if (1 == sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1))) {
            // first, is it simply a number?
            if (1 == sscanf_s(strPtr, "%d", &minHeight))
            {
                // it's simply a number. Is it + or -?
                if ((string1[0] == (char)'+') || (string1[0] == (char)'-')) {
                    if (is.processData) {
                        // it's a relative offset
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                        minHeight += miny;
                        clamp(minHeight, gMinHeight, gMaxHeight);
                    }
                    else {
                        //ignore value for now, not processing yet, so reset it
                        minHeight = 0;
                    }
                }
            }
            else {
                // not a number - look for a V or v
                if ((string1[0] == (char)'V') || (string1[0] == (char)'v')) {
                    // compare to value after, if any
                    if (is.processData) {
                        // "V" means ignore transparent blocks, such as ocean
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
                        if (on) {
                            int heightFound = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, gMinHeight, gMaxHeight, true, (string1[0] == (char)'V'), maxy);
                            if (1 == sscanf_s(&string1[1], "%d", &minHeight)) {
                                minHeight = heightFound > minHeight ? heightFound : minHeight;
                            }
                            else {
                                // no number after, just use the height directly
                                minHeight = heightFound;
                            }
                        }
                        else {
                            saveErrorMessage(is, L"the V/v option for Select minimum height command requires that an area is selected."); return INTERPRETER_FOUND_ERROR;
                        }
                    }
                }
                else {
                    saveErrorMessage(is, L"could not find integer value or V/v, V#, or v# for Select minimum height command."); return INTERPRETER_FOUND_ERROR;
                }
            }
        }
        else {
            saveErrorMessage(is, L"could not find integer value or V, V#, or V# for Select minimum height command."); return INTERPRETER_FOUND_ERROR;
        }

        if (minHeight < gMinHeight || minHeight > gMaxHeight) {
            wsprintf(error, L"value must be between %d and %d, inclusive, for Select minimum height command.", gMinHeight, gMaxHeight);
            saveErrorMessage(is, error, strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            gTargetDepth = minHeight;
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
            SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
            setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, false);
            enableBottomControl(on, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "Select maximum height:");
    if (strPtr != NULL) {
        int maxHeight;
        if (1 != sscanf_s(strPtr, "%d", &maxHeight))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Select maximum height' command."); return INTERPRETER_FOUND_ERROR;
        }
        if (maxHeight < gMinHeight || maxHeight > gMaxHeight) {
            wsprintf(error, L"value must be between %d and %d, inclusive, for Select maximum height command.", gMinHeight, gMaxHeight);
            saveErrorMessage(is, error, strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            gCurDepth = maxHeight;
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, gMinHeight);
            SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_PUSH);
            setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    if (testChangeBlockCommand(line, is, &retCode)) return retCode;

    strPtr = findLineDataNoCase(line, "Clear change block commands");
    if (strPtr != NULL) {
        deleteCommandBlockSet(is.pCBChead);
        is.pCBChead = is.pCBClast = NULL;
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Save log file:");
    if (strPtr != NULL) {
        if (*strPtr == (char)0) {
            saveErrorMessage(is, L"no log file given.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.logging && !is.processData) {
            saveWarningMessage(is, L"ignored: log file was opened earlier.");
            return INTERPRETER_FOUND_VALID_LINE;
        }
        // so that we know, to flag errors later.
        is.logging = true;
        // NOTE: we open the log file immediately, not on the second pass
        if (!is.processData) {
            strcpy_s(is.logFileName, MAX_PATH_AND_FILE, strPtr);
            if (!openLogFile(is)) {
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Set mouse order:");
    if (strPtr != NULL) {
        if (3 != sscanf_s(strPtr, "%s %s %s",
            string1, (unsigned)_countof(string1),
            string2, (unsigned)_countof(string2),
            string3, (unsigned)_countof(string3)
        ))
        {
            saveErrorMessage(is, L"could not find all left|middle|right values for Set mouse order command.\nFormat is 'Set mouse order: [left|middle|right] [left|middle|right] [left|middle|right]", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if (!validMouse(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (!validMouse(is, string2)) return INTERPRETER_FOUND_ERROR;
        if (!validMouse(is, string3)) return INTERPRETER_FOUND_ERROR;

        int leftRemap = interpretMouse(string1);
        int middleRemap = interpretMouse(string2);
        int rightRemap = interpretMouse(string3);

        // a simple loopless way to check that all the mouse buttons were remapped to something unique
        int list[3] = {-1, -1, -1};
        list[leftRemap] = list[middleRemap] = list[rightRemap] = 0;
        if (list[0] + list[1] + list[2] < 0) {
            saveErrorMessage(is, L"must use left, middle, and right once each for Set mouse order command.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            // remap!
            gRemapMouse[leftRemap] = LEFT_MOUSE_BUTTON_INDEX;
            gRemapMouse[middleRemap] = MIDDLE_MOUSE_BUTTON_INDEX;
            gRemapMouse[rightRemap] = RIGHT_MOUSE_BUTTON_INDEX;
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Reset mouse");
    if (strPtr != NULL) {
        if (is.processData) {
            gRemapMouse[LEFT_MOUSE_BUTTON_INDEX] = LEFT_MOUSE_BUTTON_INDEX;
            gRemapMouse[MIDDLE_MOUSE_BUTTON_INDEX] = MIDDLE_MOUSE_BUTTON_INDEX;
            gRemapMouse[RIGHT_MOUSE_BUTTON_INDEX] = RIGHT_MOUSE_BUTTON_INDEX;
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Set unknown block ID:");
    if (strPtr != NULL) {
        int val;
        if (1 != sscanf_s(strPtr, "%d", &val))
        {
            saveErrorMessage(is, L"could not find ID value for 'Set unknown block ID' command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (val < 0 || val >= NUM_BLOCKS_DEFINED) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            SetUnknownBlockID(val);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Show informational:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Show informational' command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            gShowInformational = interpretBoolean(string1);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Show warning:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Show warning' command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            gShowWarning = interpretBoolean(string1);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Show error:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for 'Show error' command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            gShowError = interpretBoolean(string1);
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Custom printer ");
    if (strPtr != NULL) {
        float floatVal, floatVal2, floatVal3;
        char buf[MAX_PATH_AND_FILE];
        if (1 == sscanf_s(strPtr, "minimum wall thickness: %f", &floatVal))
        {
            if (is.processData) {
                // this one is actually stored in meters.
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall = floatVal * MM_TO_METERS;
                // for all exporters currently using this material, change their blockSizeVal to match this value.
                for (int i = 0; i < 7; i++) {
                    // a bit sleazy, this happens directly on the 3d printer pEFDs
                    if (gExportPrintData.comboPhysicalMaterial[i] == PRINT_MATERIAL_CUSTOM_MATERIAL)
                        gExportPrintData.blockSizeVal[i] = floatVal;
                    if (gExportViewData.comboPhysicalMaterial[i] == PRINT_MATERIAL_CUSTOM_MATERIAL)
                        gExportViewData.blockSizeVal[i] = floatVal;
                }
            }
        }
        else if (1 == sscanf_s(strPtr, "cost per ccm: %f", &floatVal))
        {
            if (is.processData) {
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].costPerCubicCentimeter = floatVal;
            }
        }
        else if (1 == sscanf_s(strPtr, "cost per printer ccm: %f", &floatVal))
        {
            if (is.processData) {
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].costPerMachineCC = floatVal;
            }
        }
        else if (1 == sscanf_s(strPtr, "handling cost: %f", &floatVal))
        {
            if (is.processData) {
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].costHandling = floatVal;
            }
        }
        else if (1 == sscanf_s(strPtr, "minimum cost: %f", &floatVal))
        {
            if (is.processData) {
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].costMinimum = floatVal;
            }
        }
        else if (1 == sscanf_s(strPtr, "currency: %s", &buf, (unsigned)_countof(buf)))
        {
            if (is.processData) {
                if (gCustomCurrency) {
                    free(gCustomCurrency);
                    gCustomCurrency = NULL;
                }
                size_t size = (strlen(buf) + 1) * sizeof(wchar_t);
                gCustomCurrency = (wchar_t*)malloc(size);
                size_t dummySize = 0;
                mbstowcs_s(&dummySize, gCustomCurrency, size / 2, buf, (size / 2) - 1);
            }
        }
        else {
            char cleanString[1024];
            cleanStringForLocations(cleanString, strPtr);
            if (3 == sscanf_s(cleanString, "maximum size: %f %f %f", &floatVal, &floatVal2, &floatVal3))
            {
                if (is.processData) {
                    gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].maxSize[0] = floatVal;
                    gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].maxSize[1] = floatVal2;
                    gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].maxSize[2] = floatVal3;
                }
            }

            else {
                wsprintf(error, L"could not understand command %S", line);
                saveErrorMessage(is, error);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    // USDA control commands - not in the UI yet... TODO USD
    strPtr = findLineDataNoCase(line, "Chunk size:");
    if (strPtr != NULL) {
        int v;
        if (1 != sscanf_s(strPtr, "%d", &v)) {
            // bad parse - warn and quit
            saveErrorMessage(is, L"could not read 'Chunk size' value.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if ((v < 0) || (v >= 512)) {
            saveErrorMessage(is, L"chunk size must be from 0 to 511, with '0' meaninng no chunking.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            gInstanceChunkSize = v;
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // something on line, but means nothing - warn user
    return INTERPRETER_FOUND_NOTHING_USEFUL;
}

static bool findBitToggle(char* line, ImportedSet& is, char* type, unsigned int bitLocation, unsigned int windowID, int* pRetCode)
{
    *pRetCode = INTERPRETER_FOUND_NOTHING_USEFUL;	// until proven otherwise
    char commandString[1024];
    strcpy_s(commandString, 1024, type);
    strcat_s(commandString, 1024, ":");
    char* strPtr = findLineDataNoCase(line, commandString);
    if (strPtr != NULL) {
        char string1[100];
        if (1 != sscanf_s(strPtr, "%s", string1, (unsigned)_countof(string1)))
        {
            wchar_t error[1024];
            wsprintf(error, L"could not find boolean value for '%S' command.", type);
            saveErrorMessage(is, error);
            *pRetCode = INTERPRETER_FOUND_ERROR;
            return true;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            if (interpretBoolean(string1))
                gOptions.worldType |= bitLocation;	// turn bit on
            else
                gOptions.worldType &= ~bitLocation;	// turn bit off
            CheckMenuItem(GetMenu(is.ws.hWnd), windowID, (gOptions.worldType & bitLocation) ? MF_CHECKED : MF_UNCHECKED);
        }
        *pRetCode = INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
        return true;
    }
    // didn't find anything
    return false;
}

static bool testChangeBlockCommand(char* line, ImportedSet& is, int* pRetCode)
{
    // types of commands:
    // fromOne or fromMulti or fromAll - fromOne is a single block and perhaps data, from Multi is the bit table of everything, fromAll is everything except air
    // to is always a single type and data
    // location is always a range, even if identical
    // linked list of these commands, one after another

    *pRetCode = INTERPRETER_FOUND_NOTHING_USEFUL;	// until proven otherwise
    int fromType, fromData, fromEndType, fromEndData;
    unsigned short fromDataBits, fromEndDataBits;
    int toType, toData;
    fromEndType = 0;	// make compiler happy
    fromEndDataBits = 0xffff; // make compiler happy

    char* strPtr = findLineDataNoCase(line, "Change blocks:");
    if (strPtr != NULL) {
        // let the fun begin:
        // [from "grass block","Sand"-"gravel",55:2] [to 76:3] [at x, y, z[ to x, y, z]]
        //
        // We need at least a from, a to, or a location
        //
        // look for "from"
        wchar_t error[1024];
        bool foundSomething = false;
        if (strstr(strPtr, "from ") == strPtr) {
            // found "from ", so digest it.
            foundSomething = true;
            strPtr = findLineDataNoCase(strPtr, "from ");
            MY_ASSERT(strPtr);
            bool keepLooking = true;
            boolean cbCreated = false;
            int fromBlockCount = 0;
            do {
                // is there an initial block type and optional data next?
                // note: fromData and fromEndData really aren't used here; they're meant for "to" strings
                strPtr = findBlockTypeAndData(strPtr, &fromType, &fromData, &fromDataBits, error);
                if (error[0] != (wchar_t)0) {
                    saveErrorMessage(is, error); *pRetCode = INTERPRETER_FOUND_ERROR; return true;
                }
                if (strPtr != NULL) {
                    // found one, is it a range?
                    fromBlockCount++;
                    if (*strPtr == (char)'-') {
                        // yes, so see if there's another, making a range
                        fromBlockCount++;
                        strPtr++;
                        strPtr = findBlockTypeAndData(strPtr, &fromEndType, &fromEndData, &fromEndDataBits, error);
                        if (error[0] != (wchar_t)0) {
                            saveErrorMessage(is, error); *pRetCode = INTERPRETER_FOUND_ERROR; return true;
                        }
                        if (strPtr == NULL) {
                            // did not find a trailing block type and data
                            saveErrorMessage(is, L"found a starting block type for Change blocks command, but no ending block type.", strPtr);
                            *pRetCode = INTERPRETER_FOUND_ERROR;
                            return true;
                        }
                    }
                    else {
                        // not a range, so simply copy
                        fromEndType = fromType;
                        fromEndData = fromData;
                        fromEndDataBits = fromDataBits;
                    }
                }
                else {
                    // did not find an initial block type after the word "from"
                    if (fromBlockCount == 0)
                    {
                        saveErrorMessage(is, L"found 'from' for Change blocks command, but no block type follows it.", strPtr);
                        *pRetCode = INTERPRETER_FOUND_ERROR;
                        return true;
                    }
                    // else found one or more blocks, so we're good here.
                }
                if (fromDataBits != fromEndDataBits)
                {
                    saveErrorMessage(is, L"found 'from' range of types in Change blocks command, but :data cannot be set for a range.", strPtr);
                    *pRetCode = INTERPRETER_FOUND_ERROR;
                    return true;
                }
                // at this point we know we have found some single or pair of found items, so add them in
                if (!cbCreated) {
                    cbCreated = true;
                    if (is.processData)
                        createCB(is);
                }
                if (is.processData) {
                    if (fromType <= fromEndType) {
                        addFromRangeToCB(is.pCBClast, (unsigned char)fromType, (unsigned char)fromEndType, fromDataBits);
                    }
                    else {
                        // allowed, since it could be block name to block name
                        saveWarningMessage(is, L"the from range is saved from high to low; accepted as a range, but it's better to go low to high.");
                        addFromRangeToCB(is.pCBClast, (unsigned char)fromEndType, (unsigned char)fromType, fromDataBits);
                    }
                }

                // check if there is another block type in range; if so, continue search
                if (*strPtr == (char)',') {
                    // yes, we'll keep looking
                    strPtr++;
                }
                else {
                    // we're done
                    keepLooking = false;
                }
            } while (keepLooking);
        }
        else {
            // no "from" directive found, so the range is everything except air.
            if (is.processData) {
                createCB(is);
                // set all 16 bits to be flagged for all solid stuff;
                // if you want everything, you need to say "from 0-255"
                setDefaultFromRangeToCB(is.pCBClast, 1, 255, 0xffff);
            }
        }

        // now look for "to".
        if (strstr(strPtr, "to ") == strPtr) {
            // found "to ", so digest it.
            foundSomething = true;
            strPtr = findLineDataNoCase(strPtr, "to ");
            MY_ASSERT(strPtr);

            // is there an initial block type and optional data next?
            strPtr = findBlockTypeAndData(strPtr, &toType, &toData, NULL, error);
            if (error[0] != (wchar_t)0) {
                saveErrorMessage(is, error); *pRetCode = INTERPRETER_FOUND_ERROR; return true;
            }
            if (strPtr == NULL) {
                saveErrorMessage(is, L"found 'to' for Change blocks command, but no block type follows it.", strPtr);
                *pRetCode = INTERPRETER_FOUND_ERROR;
                return true;
            }
            // we're done
            if (is.processData)
                saveCBinto(is.pCBClast, toType, toData);
        } // else, no "to" given, so air is assumed, which is the default 0:0

        // finally, look for location(s)
        if (strstr(strPtr, "at ") == strPtr) {
            // found "at ", so digest it.
            foundSomething = true;
            strPtr = findLineDataNoCase(strPtr, "at ");

            int v[6];
            char cleanString[1024];
            cleanStringForLocations(cleanString, strPtr);

            if (3 == sscanf_s(cleanString, "%d %d %d",
                &v[0], &v[1], &v[2])) {
                v[3] = v[0];
                v[4] = v[1];
                v[5] = v[2];
            }
            else {
                // bad parse of first location - warn and quit
                saveErrorMessage(is, L"could not read location values 'x y z[ to x y z]'.", strPtr);
                *pRetCode = INTERPRETER_FOUND_ERROR;
                return true;
            }
            // get to "to", if possible
            strPtr = strstr(cleanString, "to ");
            if (strPtr != NULL) {
                // found a "to"
                strPtr = findLineDataNoCase(strPtr, "to ");
                if (3 != sscanf_s(strPtr, "%d %d %d",
                    &v[3], &v[4], &v[5])) {
                    saveErrorMessage(is, L"could not read location values 'x y z to x y z'.", strPtr);
                    *pRetCode = INTERPRETER_FOUND_ERROR;
                    return true;
                }
            }
            // survived, so save
            if (is.processData) {
                saveCBlocation(is.pCBClast, v);
            }
        }
        else if (foundSomething && (*strPtr != (char)0)) {
            saveErrorMessage(is, L"the Change blocks command has some unknown data where the location should be.", strPtr);
            *pRetCode = INTERPRETER_FOUND_ERROR;
            return true;
        }
        if (!foundSomething) {
            saveErrorMessage(is, L"For Change blocks command, nothing useful found after colon. Format is 'from 12 to 1 at 23,0,-18 to 24,255,44'.", strPtr);
            *pRetCode = INTERPRETER_FOUND_ERROR;
            return true;
        }
        // it's all good.
        // Final little fix: if no from location is specified, to location is specified, and a volume is specified, the "from"
        // range should be everything, including air.
        if (is.processData) {
            if (!is.pCBClast->hasFrom && is.pCBClast->hasInto && is.pCBClast->hasLocation) {
                // yes, this should be 255 - we don't allow setting or using block numbers > 255; use the names instead.
                setDefaultFromRangeToCB(is.pCBClast, 0, 255, 0xffff);
            }
        }
        *pRetCode = INTERPRETER_FOUND_VALID_LINE;
        return true;
    }
    // didn't find anything
    return false;
}

// copy string, removing additional whitespace and commas
static void cleanStringForLocations(char* cleanString, char* strPtr)
{
    bool firstSpace = true;
    do {
        if ((*strPtr == ' ') || (*strPtr == '\t')) {
            if (firstSpace) {
                *cleanString++ = ' ';
                firstSpace = false;
            } // else skip copying until whitespace is over
        }
        else if (*strPtr == ',') {
            // substitute a ' ' for a ','
            *cleanString++ = ' ';
            firstSpace = false;
        }
        else {
            *cleanString++ = *strPtr;
            firstSpace = true;
        }
    } while (*strPtr++);
}


// by default, assume nothing is set
static void createCB(ImportedSet& is)
{
    ChangeBlockCommand* pCBC = (ChangeBlockCommand*)calloc(1, sizeof(ChangeBlockCommand));
    //memset(pCBC, 0, sizeof(ChangeBlockCommand));

    if (is.pCBChead == NULL) {
        // this is the first CBC, so save the first command there
        is.pCBChead = pCBC;
    }
    else {
        MY_ASSERT(is.pCBClast);
        is.pCBClast->next = pCBC;
    }
    // store the last element in the list, ready to add on to it.
    is.pCBClast = pCBC;
}

static void addFromRangeToCB(ChangeBlockCommand* pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits)
{
    // two ways to store data:
    // one is a simple range: 5-50, say, and the data bits must all be the same, e.g. 0xffff (all data)
    // the other is complex, and so needs separate bits for every entry in a giant table
    MY_ASSERT(pCBC);
    MY_ASSERT(fromType <= fromEndType);
    // what sort of data is already in the current change block command?
    if (pCBC->hasFrom)
    {
        // We already have some "from" block(s) assigned, so we're adding to them.
        // Are we in fact already using the bits array?
        if (pCBC->useFromArray) {
            // simply add to bits array
            addRangeToDataBitsArray(pCBC, fromType, fromEndType, fromDataBits);
        }
        else {
            // we have only one previous block type range stored.
            // Does this new data use the same data bits? Or, do the ranges not overlap?
            if ((fromDataBits != pCBC->simpleFromDataBits) ||
                (fromType > pCBC->simpleFromTypeEnd + 1) ||
                (fromEndType < pCBC->simpleFromTypeBegin - 1)) {
                // no, so must add array and the different ranges
                addDataBitsArray(pCBC);
                pCBC->useFromArray = true;
                addRangeToDataBitsArray(pCBC, pCBC->simpleFromTypeBegin, pCBC->simpleFromTypeEnd, pCBC->simpleFromDataBits);
                addRangeToDataBitsArray(pCBC, fromType, fromEndType, fromDataBits);
            }
            // the data bits match, and the ranges overlap, so merge
            else {
                if (fromType < pCBC->simpleFromTypeBegin) {
                    pCBC->simpleFromTypeBegin = fromType;
                }
                if (fromEndType > pCBC->simpleFromTypeEnd) {
                    pCBC->simpleFromTypeEnd = fromEndType;
                }
            }
        }
    }
    else
    {
        // no "from" already, this is the first.
        pCBC->hasFrom = true;
        pCBC->simpleFromTypeBegin = fromType;
        pCBC->simpleFromTypeEnd = fromEndType;
        pCBC->simpleFromDataBits = fromDataBits;
    }
}

static void setDefaultFromRangeToCB(ChangeBlockCommand* pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits)
{
    //pCBC->hasFrom = false; - default, should already be set as such
    pCBC->simpleFromTypeBegin = fromType;
    pCBC->simpleFromTypeEnd = fromEndType;
    pCBC->simpleFromDataBits = fromDataBits;
}

static void addRangeToDataBitsArray(ChangeBlockCommand* pCBC, int fromType, int fromEndType, unsigned short fromDataBits)
{
    // go through range and add bits to each field
    MY_ASSERT(fromType <= fromEndType);
    for (int type = fromType; type <= fromEndType; type++) {
        pCBC->fromDataBitsArray[type] |= fromDataBits;
    }
}

static void saveCBinto(ChangeBlockCommand* pCBC, int intoType, int intoData)
{
    // if someone cleverly tries to pick a block using the nbt.cpp values, convert here so that the type is properly a number > 255, as needed
    if ((intoData & HIGH_BIT) && (intoType != BLOCK_HEAD) && (intoType != BLOCK_FLOWER_POT)) {
        intoData &= 0x7F;
        intoType |= 0x100;
    }
    pCBC->intoType = (unsigned short)intoType;
    pCBC->intoData = (unsigned char)intoData;
    pCBC->hasInto = true;
}

static void addDataBitsArray(ChangeBlockCommand* pCBC)
{
    pCBC->fromDataBitsArray = (unsigned short*)calloc(256, sizeof(unsigned short));
    //memset(pCBC->fromDataBitsArray, 0, 256 * sizeof(unsigned short));
}

static void saveCBlocation(ChangeBlockCommand* pCBC, int v[6])
{
    int tv;
    //if ((v[0] == v[3]) && (v[1] == v[4]) && (v[2] == v[5]))
    //{
        // single location
    //}
    // set low to high
    if (v[0] > v[3]) {
        tv = v[0]; v[0] = v[3]; v[3] = tv;
    }
    if (v[1] > v[4]) {
        tv = v[1]; v[1] = v[4]; v[4] = tv;
    }
    if (v[2] > v[5]) {
        tv = v[2]; v[2] = v[5]; v[5] = tv;
    }
    pCBC->minxVal = v[0];
    pCBC->minyVal = v[1];
    pCBC->minzVal = v[2];
    pCBC->maxxVal = v[3];
    pCBC->maxyVal = v[4];
    pCBC->maxzVal = v[5];
    pCBC->hasLocation = true;
}

static void deleteCommandBlockSet(ChangeBlockCommand* pCBC)
{
    while (pCBC) {
        if (pCBC->fromDataBitsArray) {
            free(pCBC->fromDataBitsArray);
        }
        pCBC = pCBC->next;
    }
}


// if pDataBits is NULL, then only one data type is legal (basically, this is for the "into" case)
// return NULL if no block was found at all (calling method will explain the error, i.e., what sort of type we were looking for).
// error[0] is > 0 if there is a syntax error of any sort within the block[:data] string,
// else return rest of line that should be parsed
// this method removes leading whitespace then go past any trailing whitespace
static char* findBlockTypeAndData(char* line, int* pType, int* pData, unsigned short* pDataBits, wchar_t* error)
{
    error[0] = (wchar_t)0;

    char* strPtr = removeLeadingWhitespace(line);

    // first find a block, if any
    if (*strPtr == '\"') {
        strPtr++;
        // there might be a string block identifier
        for (int type = 0; type < NUM_BLOCKS_DEFINED; type++) {
            // compare, caseless, to block types until we find a match
            char* foundPtr = compareLCAndSkip(strPtr, gBlockDefinitions[type].name);
            if (foundPtr != NULL) {
                // found a match!
                // now, is the next character a "? If not, it's not really a match
                if (*foundPtr == '\"') {
                    // yes, it's really a block, we have our type
                    *pType = type;
                    // move beyond this part of the string and look for a data field
                    strPtr = foundPtr + 1;
                    // break out of loop
                    goto TestForDataString;
                }
                // else continue. For example, you might have found "glass" when in fact "glass pane" is specified.
            }
        }
        // could not find type in list
        wsprintf(error, L"block type is unknown. Rest of line: %S", strPtr); return line;
    }
    else {
        // scan for an integer identifier
        if (1 != sscanf_s(strPtr, "%d", pType))
        {
            wsprintf(error, L"block type number or name not found. Rest of line: %S", strPtr); return line;
        }
        // found it - is it legal?
        if (*pType < 0 || *pType > 255) {
            wsprintf(error, L"block type must be between 0 and 255, inclusive. Rest of line: %S", strPtr); return line;
        }
        strPtr = skipPastUnsignedInteger(strPtr);
    }

TestForDataString:
    // now look for the optional data field or fields
    if (*strPtr == ':') {
        // there's indeed a data type.
        strPtr++;
        if (1 != sscanf_s(strPtr, "%d", pData))
        {
            wsprintf(error, L"data value for block not found after colon. Rest of line: %S", strPtr); return line;
        }
        if (*pData < 0 || *pData > 15) {
            wsprintf(error, L"first data value %d must be in the range 0 to 15. Rest of line: %S", *pData, strPtr); return line;
        }
        strPtr = skipPastUnsignedInteger(strPtr);
        // so is there a range of data? Rare, but possible... Valid only if pDataBits is needed (i.e., is "from")
        int secondData = *pData;
        if (*strPtr == '-') {
            if (pDataBits == NULL) {
                wsprintf(error, L"second data value in range should not be there; only a single data value should be specified, if at all. Rest of line: %S", strPtr); return line;
            }
            strPtr++;
            // look for second data type
            if (1 != sscanf_s(strPtr, "%d", &secondData))
            {
                wsprintf(error, L"second data value in range for block not found. Rest of line: %S", strPtr); return line;
            }
            if (secondData < 0 || secondData > 15) {
                wsprintf(error, L"second data value %d must be in the range 0 to 15. Rest of line: %S", secondData, strPtr); return line;
            }
            if (*pData > secondData) {
                wsprintf(error, L"first data value %d must not be greater than second %d. Rest of line: %S", *pData, secondData, strPtr); return line;
            }
            strPtr = skipPastUnsignedInteger(strPtr);
        }
        // compute data bits, if needed
        if (pDataBits != NULL) {
            *pDataBits = 0;
            for (int i = *pData; i <= secondData; i++) {
                *pDataBits |= (0x1 << i);
            }
        }
    }
    else {
        // no data field
        if (pDataBits != NULL) {
            // for "from" that means all of them
            *pDataBits = 0xffff;	// all 16 bits on
            *pData = -1;
        }
        else {
            // for "to" that means data == 0
            *pData = 0;
        }
    }

    // at end, skip past remaining whitespace, if any
    return removeLeadingWhitespace(strPtr);
}

// return character location in left string that is a character beyond right string, if matched.
// if no match, return NULL.
static char* compareLCAndSkip(char* a, char const* b)
{
    for (; *a && *b; a++, b++) {
        int d = tolower(*a) - tolower(*b);
        if (d != 0)
            return NULL;
    }
    // survived: if we're not at the end of the "b" string, what we're comparing to, then we haven't parsed through and matched the whole string;
    // else, return pointer to next character in our string.
    return *b ? NULL : a++;
}

static char* skipPastUnsignedInteger(char* strPtr)
{
    while ((*strPtr >= '0') && (*strPtr <= '9'))
        strPtr++;
    return strPtr;
}

// Go through line, delete comment part of line, delete leading "#" and white space, compare to string, if found then
// return non-whitespace location past the string found, else NULL. Does NOT modify line itself.
/*
static char *findLineData(char *line, char *findStr)
{
    // we now have the tasty part of the line, so compare the test string and make sure it is exactly at the beginning of this prepared line
    if (line != strstr(line, findStr))
    {
        // didn't find it at the beginning of this line - return
        return NULL;
    }

    // found it! Move to beyond the content
    line += strlen(findStr);

    // get rid of leading white space of last bit of line after matched string
    return removeLeadingWhitespace(line);
}
*/

static char* findLineDataNoCase(char* line, char* findStr)
{
    // we now have the tasty part of the line, so compare the test string and make sure it is exactly at the beginning of this prepared line
    char* strPtr = compareLCAndSkip(line, findStr);
    if (strPtr == NULL)
    {
        // didn't find it at the beginning of this line - return
        return NULL;
    }

    // found it! Move to beyond the content
    // get rid of leading white space of last bit of line after matched string
    return removeLeadingWhitespace(strPtr);
}

// line is assumed to exist, and returned pointer will exist
static char* removeLeadingWhitespace(char* line)
{
    // get rid of leading white space of last bit of line after matched string
    boolean cont = true;
    while ((*line != (char)0) && cont) {
        if ((*line == ' ') || (*line == '\t')) {
            line++;
        }
        else
        {
            cont = false;
        }
    }
    return line;
}

static void cleanseBackslashes(char* line)
{
    // search for two backslashes together (four here, because they're escaped)
    char* loc = strstr(line, "\\\\");
    while (loc != NULL) {
        // go to and remove second slash
        loc++;
        strcpy_s(loc, strlen(loc), loc + 1);
        loc--;
        loc = strstr(loc, "\\\\");
    }
}

static void saveErrorMessage(ImportedSet& is, wchar_t* error, char* restOfLine)
{
    saveMessage(is, error, L"Error", 1, restOfLine);
}
static void saveWarningMessage(ImportedSet& is, wchar_t* error)
{
    saveMessage(is, error, L"Warning", 0, NULL);
}
static void saveMessage(ImportedSet& is, wchar_t* error, wchar_t* msgType, int increment, char* restOfLine)
{
    if (is.errorMessages == NULL) {
        is.errorMessagesStringSize = 1024;
        is.errorMessages = (wchar_t*)malloc(is.errorMessagesStringSize * sizeof(wchar_t));
        is.errorMessages[0] = (wchar_t)0;
    }

    size_t oldlength = wcslen(is.errorMessages);
    size_t addlength = 50 + wcslen(error) + ((restOfLine != NULL) ? strlen(restOfLine) : 0);
    // enough room?
    if (is.errorMessagesStringSize < oldlength + addlength) {
        is.errorMessagesStringSize *= 2;
        // just to be really really sure, add some more
        is.errorMessagesStringSize += addlength;
        //wchar_t* oldStr = is.errorMessages;
        is.errorMessages = (wchar_t*)realloc(is.errorMessages, is.errorMessagesStringSize * sizeof(wchar_t));
        //memcpy(is.errorMessages, oldStr, (oldlength + 1) * sizeof(wchar_t));
        //free(oldStr);
    }
    // append error message
    // If error does not start with "Error" or "Warning" then add this, and line number.
    if (wcsstr(error, msgType) != error)
    {
        wchar_t buf[50];
        wsprintf(buf, L"%s reading line %d: ", msgType, is.lineNumber);
        wcscat_s(is.errorMessages, is.errorMessagesStringSize, buf);
    }
    wcscat_s(is.errorMessages, is.errorMessagesStringSize, error);
    if (restOfLine && (*restOfLine != (char)0))
    {
        wcscat_s(is.errorMessages, is.errorMessagesStringSize, L" Rest of line: ");

        wchar_t badCommand[IMPORT_LINE_LENGTH];
        size_t convertedChars = 0;
        mbstowcs_s(&convertedChars, badCommand, (size_t)IMPORT_LINE_LENGTH, restOfLine, IMPORT_LINE_LENGTH);
        size_t lineLimit = 80;
        if (wcslen(badCommand) > lineLimit)
        {
            // bad command line very long, add "..." to end.
            badCommand[lineLimit - 3] = badCommand[lineLimit - 2] = badCommand[lineLimit - 1] = (wchar_t)'.';
            badCommand[lineLimit] = (wchar_t)0;
        }
        wcscat_s(is.errorMessages, is.errorMessagesStringSize, badCommand);
    }
    wcscat_s(is.errorMessages, is.errorMessagesStringSize, L"\n");
    is.errorsFound += increment;
}

static bool validBoolean(ImportedSet& is, char* string)
{
    if ((string[0] == 'Y') || (string[0] == 'y') || (string[0] == 'T') || (string[0] == 't') || (string[0] == '1') ||
        (string[0] == 'N') || (string[0] == 'n') || (string[0] == 'F') || (string[0] == 'f') || (string[0] == '0')
        ) {
        return true;
    }
    else {
        saveErrorMessage(is, L"invalid boolean on line.");
        return false;
    }
}

static bool interpretBoolean(char* string)
{
    // YES, yes, TRUE, true, 1
    return ((string[0] == 'Y') || (string[0] == 'y') || (string[0] == 'T') || (string[0] == 't') || (string[0] == '1'));
}

static bool validMouse(ImportedSet& is, char* string)
{
    if (_stricmp(string,"left")==0 || _stricmp(string, "middle") == 0 || _stricmp(string, "right") == 0)
    {
        return true;
    }
    else {
        saveErrorMessage(is, L"invalid mouse button value on line; must be one of 'left', 'middle', 'right'.");
        return false;
    }
}

static int interpretMouse(char* string)
{
    if (_stricmp(string, "left") == 0) {
        return 0;
    }
    else if (_stricmp(string, "middle") == 0) {
        return 1;
    }
    else if (_stricmp(string, "right") == 0) {
        return 2;
    }
    return -1;
}

void formTitle(WorldGuide* pWorldGuide, HWND hWnd)
{
    wchar_t title[MAX_PATH_AND_FILE];

    wcscpy_s(title, MAX_PATH_AND_FILE - 1, L"Mineways: ");

    if (pWorldGuide->type == WORLD_TEST_BLOCK_TYPE)
    {
        wcscat_s(title, MAX_PATH_AND_FILE - 1, L"[Block Test World]");
    }
    else
    {
        wcscat_s(title, MAX_PATH_AND_FILE - 1, stripWorldName(pWorldGuide->world));
    }

    TCHAR infoString[1024];
    wsprintf(infoString, L" (1.%d%s)",
        gMinecraftVersion,
        (gMinecraftVersion == 8 ? L"?" : L""));
    wcscat_s(title, MAX_PATH_AND_FILE - 1, infoString);

    // Really, there should always be a path and name.
    // We could test if it's terrain.png and ignore, I guess (assumes someone hasn't used the same name elsewhere
    //if (wcslen(gSelectTerrainPathAndName) > 0)
    const wchar_t* terrainName = removePath(gSelectTerrainPathAndName);
    if (_wcsicmp(terrainName, L"terrainExt.png") != 0)
    {
        // get terrain file name and append
        wcscat_s(title, MAX_PATH_AND_FILE - 1, L"; ");
        wcscat_s(title, MAX_PATH_AND_FILE - 1, terrainName);
    }

    // get zoom level, note if != 1
    if (gCurScale != 1.0f) {
        wchar_t zoomString[MAX_PATH_AND_FILE];
        if ( gCurScale >= 2.0f )
            swprintf_s(zoomString, MAX_PATH_AND_FILE, L"; Zoom %d", (int)(gCurScale+0.5f));
        else
            swprintf_s(zoomString, MAX_PATH_AND_FILE, L"; Zoom %0.3f", gCurScale);

        wcscat_s(title, MAX_PATH_AND_FILE - 1, zoomString);
    }

    SetWindowTextW(hWnd, title);
}

// change any '/' to '\', or vice versa, as preferred
static void rationalizeFilePath(wchar_t* fileName)
{
    if (fileName != NULL) {
        while (*fileName != (wchar_t)0) {
            if (*fileName == gLesserSeparator) {
                *fileName = gPreferredSeparator;
            }
            fileName++;
        }
    }
}

// internet check - sadly, does not link under x64. See stdafx.h for includes and for link errors
// The update.txt file consists of this line:
// 4 14 http://www.realtimerendering.com/erich/minecraft/public/mineways/downloads.html
// version, version sub, and URL for where to update
//static void checkUpdate(HINSTANCE /*hInstance*/)
/*
{
    // We could just check this once a week or similar, but nah, check every time and let the user know.
    // For more elaborate controls, see http://www.codeproject.com/Articles/5415/Simple-update-check-function
    // This code is derived from that code, which didn't work with wchar_t.

    // get versions from file on website, compare with:
    HINTERNET hInet = InternetOpen(_T("Update search"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, NULL);
    HINTERNET hUrl = InternetOpenUrl(hInet, _T("http://www.realtimerendering.com/erich/minecraft/public/mineways/update.txt"), NULL, (DWORD)-1L,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE |
        INTERNET_FLAG_NO_CACHE_WRITE |WININET_API_FLAG_ASYNC, NULL);

    if (hUrl)
    {
        //wchar_t szBuffer[512];
        char szBuffer[512];
        DWORD dwRead;
        if (InternetReadFile(hUrl, szBuffer, sizeof(szBuffer), &dwRead))
        {
            if (dwRead > 0)
            {
                // end the string
                szBuffer[dwRead] = 0;
                // we currently don't use the URL read in - it's normally the download location.
                char url[512];
                int major, minor;
                int found = sscanf_s(szBuffer,"%d %d %s",&major,&minor,url,_countof(url));
                if ( found == 3 )
                {
                    if ( major > gMajorVersion || (major == gMajorVersion && minor > gMinorVersion) )
                    {
                        wchar_t msgString[1024];
                        swprintf_s( msgString, 1024, L"You currently use version %d.%02d of this program. A newer version %d.%02d is available at http://mineways.com.",
                                (int)gMajorVersion, (int)gMinorVersion, major, minor );
                        FilterMessageBox( NULL, msgString,
                            _T("Informational"), MB_OK|MB_ICONINFORMATION);
                        // Note: we could send the user to http://mineways.com, but then the program could be binary modified to send them to another site.
                        // Keep it simple, just warn them.
                    }
                    //else
                    //MsgUpdateNotAvailable(dwMS, dwLS);
                }
            }
            //else
                //MsgUpdateNoCheck(dwMS, dwLS);

        }
        InternetCloseHandle(hUrl);
    }
    else
        //MsgUpdateNoCheck(dwMS, dwLS);

    InternetCloseHandle(hInet);
}
*/

// return true if a path was found; note you can put NULL for path or name
static bool splitToPathAndName(wchar_t* pathAndName, wchar_t* path, wchar_t* name)
{
    wchar_t tempPathAndName[1024];
    wcscpy_s(tempPathAndName, pathAndName);
    wchar_t* lastPtr = wcsrchr(tempPathAndName, (wchar_t)'\\');
    if (lastPtr != NULL) {
        if (name != NULL)
            wcscpy_s(name, MAX_PATH_AND_FILE, lastPtr + 1);
        if (path != NULL) {
            *lastPtr = (wchar_t)0;
            wcscpy_s(path, MAX_PATH_AND_FILE, tempPathAndName);
        }
    }
    else {
        lastPtr = wcsrchr(tempPathAndName, (wchar_t)'/');
        if (lastPtr != NULL) {
            if (name != NULL)
                wcscpy_s(name, MAX_PATH_AND_FILE, lastPtr + 1);
            if (path != NULL) {
                *lastPtr = (wchar_t)0;
                wcscpy_s(path, MAX_PATH_AND_FILE, tempPathAndName);
            }
        }
        else {
            // no path
            if (name != NULL)
                wcscpy_s(name, MAX_PATH_AND_FILE, tempPathAndName);
            if (path != NULL)
                wcscpy_s(path, MAX_PATH_AND_FILE, L".");
            return false;
        }
    }
    return true;
}


// true if it worked; false if there's an error, which is returned in *error.
static bool commandLoadWorld(ImportedSet& is, wchar_t* error)
{
    // see if we can load the world
    size_t dummySize = 0;
    wchar_t backupWorld[MAX_PATH_AND_FILE];
    wcscpy_s(backupWorld, MAX_PATH_AND_FILE, gWorldGuide.world);
    int backupWorldType = gWorldGuide.type;
    mbstowcs_s(&dummySize, gWorldGuide.world, (size_t)MAX_PATH_AND_FILE, is.world, MAX_PATH_AND_FILE);
    if (wcslen(gWorldGuide.world) > 0) {
        // first, "rationalize" the gWorldGuide.world name: make it all \'s or all /'s, not both.
        // This will make it nicer for comparing to see if the new world is the same as the previously-loaded world.
        rationalizeFilePath(gWorldGuide.world);

        // world name found - can we load it?
        wchar_t warningWorld[MAX_PATH_AND_FILE];
        wcscpy_s(warningWorld, MAX_PATH_AND_FILE, gWorldGuide.world);

        // first, is it the test world?
        if (wcscmp(gWorldGuide.world, L"[Block Test World]") == 0)
        {
            // the test world is noted by the empty string.
            gWorldGuide.world[0] = (wchar_t)0;
            gWorldGuide.type = WORLD_TEST_BLOCK_TYPE;
            gSameWorld = FALSE;
#ifdef SKETCHFAB
            sprintf_s(gSkfbPData.skfbName, "TestWorld");
#endif
        }
        else {
            // test if it's a schematic file or a normal world, and set TYPE appropriately.
            wchar_t filename[MAX_PATH_AND_FILE];
            splitToPathAndName(gWorldGuide.world, NULL, filename);
            size_t len = wcslen(filename);
            if (_wcsicmp(&filename[len - 10], L".schematic") == 0) {
                // Read schematic as a world.
                gWorldGuide.type = WORLD_SCHEMATIC_TYPE;
                gWorldGuide.sch.repeat = (StrStrI(filename, L"repeat") != NULL);
            }
            else {
                gWorldGuide.type = WORLD_LEVEL_TYPE;
            }
            // was the world passed in as just a directory name, or as a full path?
            if (wcschr(gWorldGuide.world, gPreferredSeparator) == NULL) {
                // just the name, so add the path.
                // this gives us the whole path
                wchar_t testAnvil[MAX_PATH_AND_FILE];
                wcscpy_s(testAnvil, MAX_PATH_AND_FILE, gWorldPathDefault);
                wcscat_s(testAnvil, MAX_PATH_AND_FILE, gPreferredSeparatorString);
                wcscat_s(testAnvil, MAX_PATH_AND_FILE, gWorldGuide.world);
                wcscpy_s(gWorldGuide.world, MAX_PATH_AND_FILE, testAnvil);
            }
        }

        // if the world is already loaded, don't reload it.
        if (wcscmp(backupWorld, gWorldGuide.world) != 0 || (gSameWorld == FALSE))
        {
            // not the same, attempt to load!
            gSameWorld = FALSE;
            if (loadWorld(is.ws.hWnd))	// uses gWorldGuide.world
            {
                // could not load world, so restore old world, if any;
                wcscpy_s(gWorldGuide.world, MAX_PATH_AND_FILE, backupWorld);
                if (gWorldGuide.world[0] != 0) {
                    gWorldGuide.type = backupWorldType;
                    loadWorld(is.ws.hWnd);	// uses gWorldGuide.world
                }
                swprintf_s(error, 1024, L"Mineways attempted to load world \"%s\" but could not do so. The full path was \"%s\". Either the world could not be found, or the world name is some wide character string that could not be stored in your import file. Please load the world manually and then try importing again.", warningWorld, gFileOpened);
                return false;
            } // else success with just world folder name, and it's already saved to gWorldGuide.world
            // world loaded, so turn things on, etc.
            setUIOnLoadWorld(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, is.ws.hwndInfoLabel, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel);
        }
    }
    else {
        // world didn't convert over - unlikely to hit this one
        swprintf_s(error, 1024, L"Mineways attempted to load world \"%S\" but could not do so. Either the world could not be found, or the world name is some wide character string that could not be stored in your import file. Please load the world manually and then try importing again.", is.world);
        return false;
    }
    return true;
}

// true if it worked; false if there's an error, which is returned in *error.
static bool commandLoadTerrainFile(ImportedSet& is, wchar_t* error)
{
    FILE* fh;
    errno_t err;
    wchar_t terrainFileName[MAX_PATH_AND_FILE], tempPath[MAX_PATH_AND_FILE], tempName[MAX_PATH_AND_FILE];

    size_t dummySize = 0;
    mbstowcs_s(&dummySize, terrainFileName, (size_t)MAX_PATH_AND_FILE, is.terrainFile, MAX_PATH_AND_FILE);
    // first, "rationalize" the name: make it all \'s or all /'s, not both.
    rationalizeFilePath(terrainFileName);
    if (!splitToPathAndName(terrainFileName, tempPath, tempName)) {
        // no path, so check if the name is simply "default" which means "the user used the internally-stored terrainExt"
        if (wcscmp(tempName, L"default") != 0) {
            // just a file name found, so make test path with directory
            if (wcslen(tempName) > 0)
            {
                wcscpy_s(terrainFileName, MAX_PATH_AND_FILE, gSelectTerrainDir);
                wcscat_s(terrainFileName, MAX_PATH_AND_FILE - wcslen(terrainFileName), gPreferredSeparatorString);
                wcscat_s(terrainFileName, MAX_PATH_AND_FILE - wcslen(terrainFileName), tempName);
            }
            else {
                // something odd happened - filename is empty
                swprintf_s(error, 1024, L"Terrain file \"%S\" not possible to convert to a file name. Please select the terrain file manually.", is.terrainFile);
                formTitle(&gWorldGuide, is.ws.hWnd);
                return false;
            }
            err = _wfopen_s(&fh, terrainFileName, L"rt");
            if (err != 0) {
                // can't find it at all, so generate error.
                swprintf_s(error, 1024, L"Terrain file \"%S\" was not found. Please select the terrain file manually.", is.terrainFile);
                formTitle(&gWorldGuide, is.ws.hWnd);
                return false;
            }
            // success, copy file and path (directory is fine)
            wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, terrainFileName);
            formTitle(&gWorldGuide, is.ws.hWnd);
            fclose(fh);
        }
    }
    else {
        // path found: try the file as a full path.
        err = _wfopen_s(&fh, terrainFileName, L"rt");
        if (err != 0) {
            // can't find it at all, so generate error.
            swprintf_s(error, 1024, L"Terrain file \"%S\" was not found. Please select the terrain file manually.", is.terrainFile);
            formTitle(&gWorldGuide, is.ws.hWnd);
            return false;
        }
        // success, copy file name&path, and directory
        wcscpy_s(gSelectTerrainPathAndName, MAX_PATH_AND_FILE, terrainFileName);
        wcscpy_s(gSelectTerrainDir, MAX_PATH_AND_FILE, tempPath);
        formTitle(&gWorldGuide, is.ws.hWnd);
        fclose(fh);
    }
    return true;
}

// true if it worked; false if there's an error, which is returned in *error.
static bool commandLoadColorScheme(ImportedSet& is, wchar_t* error, bool invalidate)
{
    wchar_t backupColorScheme[255];
    wcscpy_s(backupColorScheme, 255, gSchemeSelected);
    size_t dummySize = 0;
    mbstowcs_s(&dummySize, gSchemeSelected, 255, is.colorScheme, 255);
    // if the incoming scheme, now in gSchemeSelected, is different than backupColorScheme,
    // then change the color scheme. Changing the color scheme is costly, as a full redraw is then needed.
    if (wcscmp(gSchemeSelected, backupColorScheme) != 0) {
        int item = findColorScheme(gSchemeSelected);
        if (item > 0)
        {
            useCustomColor(IDM_CUSTOMCOLOR + item, is.ws.hWnd, invalidate);
        }
        else if (wcscmp(gSchemeSelected, L"Standard") == 0)
        {
            useCustomColor(IDM_CUSTOMCOLOR, is.ws.hWnd, invalidate);
        }
        else
        {
            // warning - not found, so don't use it.
            swprintf_s(error, 1024, L"Warning: Mineways attempted to load color scheme \"%s\" but could not do so. Either the color scheme could not be found, or the scheme name is some wide character string that could not be stored in your import file. Please select the color scheme from the menu manually.", gSchemeSelected);

            // restore old scheme
            wcscpy_s(gSchemeSelected, 255, backupColorScheme);
            findColorScheme(gSchemeSelected);
            return false;
        }
    }
    return true;
}

// true if it worked
static bool commandExportFile(ImportedSet& is, wchar_t* error, int fileMode, char* fileName)
{
    if (!gHighlightOn)
    {
        // we keep the export options ungrayed now so that they're selectable when the world is loaded
        swprintf_s(error, 1024, L"no volume is selected for export; click and drag using the right-mouse button.");
        return false;
    }

    // 0 - render, 1 - 3d print, 2 - schematic (,3 - sketchfab, but that should not reach here), 4 - map
    gPrintModel = fileMode;

    wchar_t wcharFileName[MAX_PATH_AND_FILE];

    size_t dummySize = 0;
    mbstowcs_s(&dummySize, wcharFileName, (size_t)MAX_PATH_AND_FILE, fileName, MAX_PATH_AND_FILE);
    // first, "rationalize" the name: make it all \'s or all /'s, not both.
    rationalizeFilePath(wcharFileName);

    // if there are any change commands, transfer them now for processing
    addChangeBlockCommandsToGlobalList(is);

    // return number of files exported; 0 means failed
    wchar_t statusbuf[MAX_PATH_AND_FILE];
    wchar_t exportName[MAX_PATH_AND_FILE];
    splitToPathAndName(wcharFileName, NULL, exportName);
    wsprintf(statusbuf, L"Script exporting %s", exportName);
    sendStatusMessage(is.ws.hwndStatus, statusbuf);

    if (gPrintModel == MAP_EXPORT) {
        // export 2D map image
        int on;
        GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal, gMinHeight);
        gExported = saveMapFile(gpEFD->minxVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, wcharFileName);
        if (gExported == 0) {
            sendStatusMessage(is.ws.hwndStatus, L"Script export map operation failed");
            swprintf_s(error, 1024, L"export map operation failed.");
            return false;
        }
    }
    else {
        // export model
        gExported = saveObjFile(is.ws.hWnd, wcharFileName, gPrintModel, gSelectTerrainPathAndName, gSchemeSelected, false, false);
        if (gExported == 0)
        {
            sendStatusMessage(is.ws.hwndStatus, L"Script export operation failed");
            swprintf_s(error, 1024, L"export operation failed.");
            return false;
        }
    }
    // back to normal
    sendStatusMessage(is.ws.hwndStatus, RUNNING_SCRIPT_STATUS_MESSAGE);

    SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    enableBottomControl(1, /* is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, */ is.ws.hwndInfoBottomLabel);
    // put target depth to new depth set, if any
    if (gTargetDepth != gpEFD->maxyVal)
    {
        gTargetDepth = gpEFD->minyVal;
    }
    //gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(gHoldlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
    //	bitWidth, bitHeight, gMinHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
    //updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
    setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
    setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, true);

    return true;
}

static bool openLogFile(ImportedSet& is)
{
#ifdef WIN32
    DWORD br;
#endif
    // try to open log file, and write header to it.
    // see if we can load the world
    size_t dummySize = 0;
    wchar_t wFileName[MAX_PATH_AND_FILE];
    char outputString[256];
    mbstowcs_s(&dummySize, wFileName, (size_t)MAX_PATH_AND_FILE, is.logFileName, MAX_PATH_AND_FILE);
    rationalizeFilePath(wFileName);
    if (wcslen(wFileName) > 0) {
        // overwrite previous log file
        is.logfile = PortaCreate(wFileName);
        if (is.logfile == INVALID_HANDLE_VALUE) {
            saveErrorMessage(is, L"cannot open script log file.", is.logFileName);
            goto OnFail;
        }

        strcpy_s(outputString, 256, "Start processing script\n");
        if (PortaWrite(is.logfile, outputString, strlen(outputString))) goto OnFail;

        // Print local time as a string.
        errno_t errNum;
        struct tm newtime;
        __time32_t aclock;
        char timeString[256];

        _time32(&aclock);   // Get time in seconds.
        _localtime32_s(&newtime, &aclock);   // Convert time to struct tm form.

        errNum = asctime_s(timeString, 32, &newtime);   // TODO should use strftime?
        if (!errNum)
        {
            sprintf_s(outputString, 256, "  %s\n", timeString);
            if (PortaWrite(is.logfile, outputString, strlen(outputString))) goto OnFail;
        }

        return true;
    }

OnFail:
    is.logging = false;
    is.logFileName[0] = (char)0;
    if (is.logfile != 0)
    {
        PortaClose(is.logfile);
        is.logfile = NULL;
    }
    return false;
}

/* for debugging https://github.com/erich666/Mineways/issues/31
static void logHandles()
{
    DWORD handles_count;
    GetProcessHandleCount(GetCurrentProcess(), &handles_count);
    char outputString[1024];
    sprintf_s(outputString, 1024, "Log handles: %d", (int)handles_count);
    LOG_INFO(gExecutionLogfile, outputString);
}
*/

static void showLoadWorldError(int loadErr)
{
    // world not loaded properly
    wchar_t fullbuf[2048];
    wchar_t extrabuf[1024];
    int retcode;
    wsprintf(extrabuf, _T("Sub-error code %d. Please write me at erich@acm.org and, as best you can, tell me the error message, sub-error code, and what directories your world and mineways.exe is located in."), gSubError);
    switch (loadErr) {
    case 1:
        if (gSubError > 0) {
            wsprintf(fullbuf, _T("Error: cannot read or find your world for some reason. Path attempted: \"%s\". If yours is a Java (Classic) world, one idea: try copying your world save directory to some simple location such as C:\\temp and use File | Open...\n\n%s\n\nIf that doesn't work, the world may be in a different ('Bedrock') format, used by Minecraft for Windows 10. Click 'OK' to go to https://bit.ly/mcbedrock and follow the instructions there to convert this type of world to the 'Classic' Java format, which Mineways can read. If instead this world is from an early version of Classic Minecraft, load it into the latest Minecraft to convert it. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information."), gFileOpened, extrabuf);
        }
        // To be honest, I'm not sure how -3 could ever be set here, but it evidently can be...
        else if (gSubError == ERROR_GET_FILE_VERSION_DATA || gSubError == ERROR_GET_FILE_VERSION_VERSION ) {
            wsprintf(fullbuf, _T("Error: cannot read world's file version.\n\nThe world may be in a different ('Bedrock') format, used by Minecraft for Windows 10. Click 'OK' to go to https://bit.ly/mcbedrock and follow the instructions there to convert this type of world to the 'Classic' Java format, which Mineways can read. If instead this world is from an early version of Classic Minecraft, load it into the latest Minecraft to convert it. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information."));
        }
        else {
            // prints the line in the code where the error was returned via (a negated) LINE_ERROR.
            wsprintf(fullbuf, _T("Error: cannot load world for some reason. Path attempted: \"%s\".\n\n%s\n\nThe world may be in a different ('Bedrock') format, used by Minecraft for Windows 10. Click 'OK' to go to https://bit.ly/mcbedrock and follow the instructions there to convert this type of world to the 'Classic' Java format, which Mineways can read. If instead this world is from an early version of Classic Minecraft, load it into the latest Minecraft to convert it. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information."), gFileOpened, extrabuf);
        }
        retcode = FilterMessageBox(NULL, fullbuf,
            _T("Read error"), MB_OKCANCEL | MB_ICONERROR | MB_TOPMOST);
        GoToBedrockHelpOnOK(retcode);
        break;
    case 2:
        wsprintf(fullbuf, _T("Error: world has not been converted to the Anvil format.\n\nTo make a world readable by Mineways, install the latest Classic (Java) Minecraft you can, load the world in it, then quit. Doing so will convert your world to a format Mineways understands.\n\nIf this does not work, the world may be in a different ('Bedrock') format, used in Minecraft for Windows 10. Click 'OK' to go to https://bit.ly/mcbedrock and follow the instructions there to convert this type of world to the 'Classic' Java format, which Mineways can read. A third possibility is that this is some modded world in a format that Mineways does not support. There's not a lot that can be done about that, but feel free to contact me on Discord or by email. See the http://mineways.com site for support information."));

        retcode = FilterMessageBox(NULL, fullbuf,
            _T("Read error"), MB_OKCANCEL | MB_ICONERROR | MB_TOPMOST);
        GoToBedrockHelpOnOK(retcode);
        break;
    case 3:
        wsprintf(fullbuf, _T("Error: cannot read world's spawn location - every world should have one.\n\n%s"), extrabuf);
        FilterMessageBox(NULL, fullbuf,
            _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        break;
    default:
        wsprintf(fullbuf, _T("Error: cannot read world. Unknown error code, which is very strange... Please send me the level.dat file.\n\n%s"), extrabuf);
        FilterMessageBox(NULL, fullbuf,
            _T("Read error"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        break;
    }
}

static void checkMapDrawErrorCode(int retCode)
{
    wchar_t fullbuf[2048];
    // Goofy errors codes, I admit it. Negative means serious error, otherwise > 2 are one-time warning bits
    if (retCode < 0) {
        // error - show an error reading?
        if (gOneTimeDrawError) {
            gOneTimeDrawError = false;
            if (retCode == ERROR_INFLATE) {
                wsprintf(fullbuf, _T("Error: status != Z_STREAM_END error. Tell Eric to increase the size of CHUNK_INFLATE_MAX again and cross his fingers."));
            }
            else {
                int bx, bz;
                GetBadChunkLocation(&bx, &bz);
                wsprintf(fullbuf, _T("Error: chunk (%d, %d) read error at nbt.cpp line %d. Mineways does not support betas or mods. Also, make sure you have downloaded the latest version of Mineways from mineways.com. If it's neither of these, please report it as a bug (see the Help menu)."), bx, bz, -retCode);
            }
            FilterMessageBox(NULL, fullbuf,
                _T("Warning"), MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
    }
    else if (gOneTimeDrawWarning & retCode) {
        // NBT_WARNING_NAME_NOT_FOUND is the only one now
        // currently the only warning - we will someday look at bits, I guess, in retCode
        wsprintf(fullbuf, _T("Warning: at least one unknown block type '%S' was encountered and turned into '%S'.\n\nIf you are not running a Minecraft beta, mod, or conversion, please download the latest version of Mineways from mineways.com. If you think Mineways has a bug, please report it (see the Help menu)."),
            MapUnknownBlockName(), gBlockDefinitions[GetUnknownBlockID()].name);
        FilterMessageBox(NULL, fullbuf,
            _T("Warning"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
        gOneTimeDrawWarning &= ~NBT_WARNING_NAME_NOT_FOUND;
    }
}


static bool saveMapFile(int xmin, int zmin, int xmax, int ymax, int zmax, wchar_t* mapFileName)
{
    int temp, retCode;
    if (xmin > xmax) {
        // swap
        temp = xmin;
        xmin = xmax;
        xmax = temp;
    }
    if (zmin > zmax) {
        // swap
        temp = zmin;
        zmin = zmax;
        zmax = temp;
    }
    int w = xmax - xmin + 1;
    int h = zmax - zmin + 1;
    int zoom = (int)(gCurScale + 0.5f);

    // first, can we even make such an image?
    progimage_info* mapimage;
    mapimage = new progimage_info();
    mapimage->width = zoom * w;
    mapimage->height = zoom * h;

    // resize and clear
    mapimage->image_data.resize(w * h * 3 * zoom * zoom * sizeof(unsigned char), 0x0);
    unsigned char* imageDst = &mapimage->image_data[0];

    // turn off highlight for map draw
    SetHighlightState(0, xmin, gTargetDepth, zmin, xmax, ymax, zmax, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);

    checkMapDrawErrorCode(
        DrawMapToArray(imageDst, &gWorldGuide, xmin, zmin, ymax, gMaxHeight, w, h, zoom, &gOptions, gHitsFound, updateProgress, gMinecraftVersion, gVersionID)
    );

    // check if map file has ".png" at the end - if not, add it.
    wchar_t mapFileNameSafe[MAX_PATH_AND_FILE];
    EnsureSuffix(mapFileNameSafe, mapFileName, L".png");

    // 0 means success. Currently we don't say what goes wrong otherwise.
    retCode = writepng(mapimage, 3, mapFileNameSafe);

    writepng_cleanup(mapimage);
    delete mapimage;

    // turn highlight back on, now that we're done
    SetHighlightState(gHighlightOn, xmin, gTargetDepth, zmin, xmax, gCurDepth, zmax, gMinHeight, gMaxHeight, HIGHLIGHT_UNDO_IGNORE);
    return (retCode == 0);
}

static int FilterMessageBox(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType)
{
    wchar_t statusbuf[1024];
    if (!gShowInformational && (uType & MB_ICONINFORMATION)) {
        swprintf_s(statusbuf, 1024, L"Informational: %s", lpText);
        sendStatusMessage(gWS.hwndStatus, statusbuf);
        return 1;
    }
    if (!gShowWarning && (uType & MB_ICONWARNING)) {
        swprintf_s(statusbuf, 1024, L"Warning: %s", lpText);
        sendStatusMessage(gWS.hwndStatus, statusbuf);
        return 1;
    }
    if (!gShowError && (uType & MB_ICONERROR)) {
        swprintf_s(statusbuf, 1024, L"ERROR: %s", lpText);
        sendStatusMessage(gWS.hwndStatus, statusbuf);
        return 1;
    }
    // note: we make all these message boxes system modal - they should always be in the user's face until dismissed.
    return MessageBox(hWnd, lpText, lpCaption, uType | MB_TOPMOST);
}

static void GoToBedrockHelpOnOK( int retcode )
{
    if (retcode == IDOK)
    {
        std::string bedrockUrl = "https://bit.ly/mcbedrock";
        wchar_t* wcharBedrockUrl = new wchar_t[4096];
        MultiByteToWideChar(CP_ACP, 0, bedrockUrl.c_str(), (int)(bedrockUrl.size() + 1), wcharBedrockUrl, 4096);
        ShellExecute(NULL, L"open", wcharBedrockUrl, NULL, NULL, SW_SHOWNORMAL);
    }
}