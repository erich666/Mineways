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
#include "publishSkfb.h"
#include "XZip.h"
#include "lodepng.h"
#include <assert.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <CommDlg.h>
#include <stdio.h>
#include <math.h>

// Should really make a full-featured error system, a la https://www.softwariness.com/articles/assertions-in-cpp/, but this'll do for now.
// trick so that there is not a warning that there's a constant value being tested by an "if"
static bool gAlwaysFail = false;
// from http://stackoverflow.com/questions/8487986/file-macro-shows-full-path
#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define MY_ASSERT(val) if ((val) == 0) { \
    wchar_t assertbuf[1024]; \
    wsprintf(assertbuf, L"Serious error in file %S on line %d. Please write me at erich@acm.org and, as best you can, tell me the steps that caused it.", __FILENAME__, __LINE__); \
    MessageBox(NULL, assertbuf, L"Error", MB_OK | MB_SYSTEMMODAL); \
} \

// zoomed all the way in. We could allow this to be larger...
// It's useful to have it high for Nether <--> overworld switches
#define MAXZOOM 40.0
// zoomed all the way out
#define MINZOOM 1.0

// how far outside the rectangle we'll select the corners and edges of the selection rectangle
#define SELECT_MARGIN 5

#define MAX_LOADSTRING 100

// window margins - there should be a lot more defines here...
#define MAIN_WINDOW_TOP (30+30)
#define SLIDER_LEFT	90

// should probably be a subroutine, but too many variables...
#define REDRAW_ALL  draw();\
                    gBlockLabel=IDBlock(LOWORD(gHoldlParam),HIWORD(gHoldlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,\
                        bitWidth,bitHeight,gCurScale,&mx,&my,&mz,&type,&dataVal,&biome,gWorldGuide.type==WORLD_LEVEL_TYPE);\
                    updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);\
                    InvalidateRect(hWnd,NULL,FALSE);\
                    UpdateWindow(hWnd);
// InvalidateRect was TRUE in last arg - does it matter?

#define LOG_INFO( FH, OUTPUTSTRING ) \
if (FH) { \
    DWORD br; \
    if ( PortaWrite(FH, OUTPUTSTRING, strlen(OUTPUTSTRING))) { \
        MessageBox(NULL, _T("log file opened but cannot write to it."), _T("Log error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL); \
        PortaClose(FH); \
        (FH) = 0x0; \
    } \
}


// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name
#define MAX_WORLDS	1000
TCHAR *gWorlds[MAX_WORLDS];							// up to 1000 worlds
int gNumWorlds = 0;

static Options gOptions = {0,   // which world is visible
    BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP | BLF_FLATSIDE,   // what's exportable (really, set on output)
    0x0,
    0,  // start with low memory
    INITIAL_CACHE_SIZE,	// cache size
    NULL};

static WorldGuide gWorldGuide;
static int gVersionId = 0;								// Minecraft version 1.9 (finally) introduced a version number for the releases. 0 means Minecraft world is earlier than 1.9.
static BOOL gSameWorld=FALSE;
static BOOL gHoldSameWorld=FALSE;
static wchar_t gSelectTerrainPathAndName[MAX_PATH];				//path and file name to selected terrainExt.png file, if any
static wchar_t gSelectTerrainDir[MAX_PATH];				//path (no file name) to selected terrainExt.png file, if any
static wchar_t gImportFile[MAX_PATH];					//import file for settings
static wchar_t gImportPath[MAX_PATH];					//path to import file for settings
static BOOL gLoaded=FALSE;								//world loaded?
static double gCurX,gCurZ;								//current X and Z
static int gLockMouseX=0;                               // if true, don't allow this coordinate to change with mouse, 
static int gLockMouseZ=0;
static double gCurScale=MINZOOM;					    //current scale
static int gCurDepth=MAP_MAX_HEIGHT;					//current depth
static int gStartHiX,gStartHiZ;						    //starting highlight X and Z

static BOOL gHighlightOn=FALSE;
static BOOL gLeftIsRight=FALSE;

static int gSpawnX,gSpawnY,gSpawnZ;
static int gPlayerX,gPlayerY,gPlayerZ;

// minimum depth output by default, sea level including water (or not, in 1.9; still, not a terrible start)
// note: 51 this gets you to bedrock in deep lakes
#define MIN_OVERWORLD_DEPTH SEA_LEVEL

static int gTargetDepth=MIN_OVERWORLD_DEPTH;								//how far down the depth is stored, for export

// Export 3d print and view data
static ExportFileData gExportPrintData;
static ExportFileData gExportViewData;
static ExportFileData gExportSchematicData;
static PublishSkfbData gSkfbPData;
// this one is set to whichever is active for export or import, 3D printing or rendering
static ExportFileData *gpEFD = NULL;

static int gOverworldHideStatus=0x0;

static wchar_t gCurrentDirectory[MAX_PATH];
static wchar_t gWorldPathDefault[MAX_PATH];
static wchar_t gWorldPathCurrent[MAX_PATH];

LPTSTR filepath = new TCHAR[MAX_PATH];
LPTSTR tempdir = new TCHAR[MAX_PATH];

// low, inside, high for selection area, fourth value is minimum height found below selection box
static int gHitsFound[4];
static int gFullLow=1;
static int gAdjustingSelection=0;
static bool gShowPrintStats=true;
static int gAutocorrectDepth=1;

static int gBottomControlEnabled = FALSE;

static int gPrintModel = 0;	// 1 is print, 0 is render, 2 is schematic
static BOOL gExported=0;
static TCHAR gExportPath[MAX_PATH] = _T("");

static WORD gMajorVersion = 0;
static WORD gMinorVersion = 0;
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

ChangeBlockCommand *gChangeBlockCommands = NULL;

static wchar_t gPreferredSeparator = (wchar_t)'\\';
static wchar_t gPreferredSeparatorString[2];
static wchar_t gLesserSeparator = (wchar_t)'/';

static int gArgCount = 0;
static LPWSTR *gArgList = NULL;

static HANDLE gExecutionLogfile = 0x0;

static wchar_t *gCustomCurrency = NULL;

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
    ExportFileData *pEFD;		// where to directly save data, no deferral
    ExportFileData *pSaveEFD;	// deferred save, for model load
    char world[MAX_PATH];
    char terrainFile[MAX_PATH];
    char colorScheme[MAX_PATH];
    wchar_t *importFile;
    int lineNumber;
    size_t errorMessagesStringSize;
    wchar_t *errorMessages;
    bool processData;
    bool nether;
    bool theEnd;
    bool logging;
    char logFileName[MAX_PATH];
    HANDLE logfile;
    ChangeBlockCommand *pCBChead;
    ChangeBlockCommand *pCBClast;
} ImportedSet;

static WindowSet gWS;

// Error codes - see ObjFileManip.h for error numbers, look for MW_NO_ERROR
static struct {
    TCHAR *text;
    TCHAR *caption;
    unsigned int type;
} gPopupInfo[]={
    {_T("No error"), _T("No error"), MB_OK},	//00
    {_T("Warning: thin walls possible.\n\nThe thickness of a single block is smaller than the recommended wall thickness. Print only if you know what you're doing."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// 1
    {_T("Warning: sum of dimensions low.\n\nThe sum of the dimensions of the model is less than 65 mm. This is officially too small for a Shapeways color sandstone model, but they will probably print it anyway."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<1
    {_T("Warning: too many polygons.\n\nThere are more than one million polygons in file. This is usually too many for Shapeways."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<2
    {_T("Warning: multiple separate parts found after processing.\n\nThis may not be what you want to print. Increase the value for 'Delete floating parts' to delete these. Try the 'Debug: show separate parts' export option to see if the model is what you expected."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<3
    {_T("Warning: at least one dimension of the model is too long.\n\nCheck the dimensions for this printer's material: look in the top of the model file itself, using a text editor."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<4
    {_T("Warning: Mineways encountered an unknown block type in your model. Such blocks are converted to bedrock. Mineways does not understand blocks added by mods. If you are not using mods, your version of Mineways may be out of date. Check http://mineways.com for a newer version of Mineways."), _T("Informational"), MB_OK|MB_ICONINFORMATION},	// <<5
    {_T("Warning: too few rows of block textures were found in your terrain\ntexture file. Newer block types will not export properly.\nPlease use the TileMaker program or other image editor\nto make a TerrainExt.png with 24 rows."), _T("Informational"), MB_OK | MB_ICONINFORMATION },	// <<6
    {_T("Warning: one or more Change Block commands specified location(s) that were outside the selected volume."), _T("Informational"), MB_OK | MB_ICONINFORMATION },	// <<6

    {_T("Error: no solid blocks found; no file output"), _T("Export warning"), MB_OK|MB_ICONWARNING},	// <<7
    {_T("Error: all solid blocks were deleted; no file output"), _T("Export warning"), MB_OK|MB_ICONWARNING},	// <<8
    {_T("Error creating export file; no file output"), _T("Export error"), MB_OK|MB_ICONERROR},	// <<9
    {_T("Error: cannot write to export file"), _T("Export error"), MB_OK|MB_ICONERROR},	// <<10
    {_T("Error: the incoming terrainExt.png file resolution must be divisible by 16 horizontally and at least 16 pixels wide."), _T("Export error"), MB_OK|MB_ICONERROR},	// <<11
    {_T("Error: the incoming terrainExt.png file image has fewer than 16 rows of block tiles."), _T("Export error"), MB_OK|MB_ICONERROR},	// <<12
    {_T("Error: the exported volume cannot have a dimension greater than 65535."), _T("Export error"), MB_OK|MB_ICONERROR},	// <<13 MW_DIMENSION_TOO_LARGE
    {_T("Error: cannot read import file."), _T("Import error"), MB_OK|MB_ICONERROR},	// <<14
    {_T("Error: opened import file, but cannot read it properly."), _T("Import error"), MB_OK|MB_ICONERROR},	// <<15
    {_T("Error: out of memory - terrainExt.png texture is too large. Try 'Help | Give more export memory!', or please use a texture with a lower resolution."), _T("Memory error"), MB_OK|MB_ICONERROR},	// <<16
    {_T("Error: out of memory - volume of world chosen is too large. RESTART PROGRAM, then try 'Help | Give more export memory!'. If that fails, export smaller portions of your world."), _T("Memory error"), MB_OK|MB_ICONERROR},	// <<17
    {_T("Error: yikes, internal error! Please let me know what you were doing and what went wrong: erich@acm.org"), _T("Internal error"), MB_OK|MB_ICONERROR},	// <<18

    // old error, but now we don't notice if the file has changed, so we make it identical to the "file missing" error
    // {_T("Error: cannot read your custom terrainExt.png file.\n\nPNG error: %s"), _T("Export error"), MB_OK|MB_ICONERROR},	// << 19
    {_T("Error: cannot read terrainExt.png file.\n\nPNG error: %s\n\nPlease check that your terrainExt.png file is a valid PNG file. If you continue to have problems, download Mineways again."), _T("Export error"), MB_OK|MB_ICONERROR},	// << 19
    {_T("Error: cannot read terrainExt.png file.\n\nPNG error: %s\n\nPlease check that your terrainExt.png file is a valid PNG file. If you continue to have problems, download Mineways again."), _T("Export error"), MB_OK|MB_ICONERROR},	// << 20
    {_T("Error writing to export file; partial file output\n\nPNG error: %s"), _T("Export error"), MB_OK|MB_ICONERROR},	// <<21
};

#define RUNNING_SCRIPT_STATUS_MESSAGE L"Running script commands"

#define IMPORT_LINE_LENGTH	1024

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
static void closeMineways();
static bool startExecutionLogFile(const LPWSTR *argList, int argCount);
static bool modifyWindowSizeFromCommandLine(int *x, int *y, const LPWSTR *argList, int argCount);
static bool getWorldSaveDirectoryFromCommandLine(wchar_t *saveWorldDirectory, const LPWSTR *argList, int argCount);
static bool processCreateArguments(WindowSet & ws, const char **pBlockLabel, LPARAM holdlParam, const LPWSTR *argList, int argCount);
static void runImportOrScript(wchar_t *importFile, WindowSet & ws, const char **pBlockLabel, LPARAM holdlParam, bool dialogOnSuccess);
static int loadSchematic(wchar_t *pathAndFile);
static int loadWorld(HWND hWnd);
static int setWorldPath(TCHAR *path);
//static void homePathMac(TCHAR *path);
static void enableBottomControl( int state, HWND hwndBottomSlider, HWND hwndBottomLabel, HWND hwndInfoBottomLabel );
static void validateItems(HMENU menu);
static int loadWorldList(HMENU menu);
static void draw();
static void setUIOnLoadWorld(HWND hWnd, HWND hwndSlider, HWND hwndLabel, HWND hwndInfoLabel, HWND hwndBottomSlider, HWND hwndBottomLabel);
static void updateCursor(LPARAM lParam, BOOL hdragging);
static void gotoSurface( HWND hWnd, HWND hwndSlider, HWND hwndLabel);
static void updateStatus(int mx, int mz, int my, const char *blockLabel, int type, int dataVal, int biome, HWND hwndStatus);
static void populateColorSchemes(HMENU menu);
static void useCustomColor(int wmId,HWND hWnd);
static int findColorScheme(wchar_t* name);
static void setSlider( HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth, bool update );
static void drawInvalidateUpdate(HWND hWnd);
static void syncCurrentHighlightDepth();
static void copyOverExportPrintData( ExportFileData *pEFD );
static int saveObjFile(HWND hWnd, wchar_t *objFileName, int printModel, wchar_t *terrainFileName, wchar_t *schemeSelected, bool showDialog, bool showStatistics);
static int publishToSketchfab(HWND hWnd, wchar_t *objFileName, wchar_t *terrainFileName, wchar_t *schemeSelected);
static void addChangeBlockCommandsToGlobalList(ImportedSet & is);
static void PopupErrorDialogs( int errCode );
static const wchar_t *removePath( const wchar_t *src );
static void initializeExportDialogData();
static void initializePrintExportData(ExportFileData &printData);
static void initializeViewExportData(ExportFileData &viewData);
static void InitializeSchematicExportData(ExportFileData &schematicData);
static int importSettings(wchar_t *importFile, ImportedSet & is, bool dialogOnSuccess);
static bool importModelFile(wchar_t *importFile, ImportedSet & is);
static bool readAndExecuteScript(wchar_t *importFile, ImportedSet & is);
static void initializeImportedSet(ImportedSet & is, ExportFileData *pEFD, wchar_t *importFile);
static int readLine(FILE *fh, char *inputString, int stringLength);
static char *prepareLineData(char *line, bool model);
static bool dealWithCommentBlocks(char *line, bool commentBlock);
static bool startCommentBlock(char *line);
static char *closeCommentBlock(char *line);
static int switchToNether(ImportedSet & is);
static int switchToTheEnd(ImportedSet & is);
static int interpretImportLine(char *line, ImportedSet & is);
static int interpretScriptLine(char *line, ImportedSet & is);
static bool findBitToggle(char *line, ImportedSet & is, char *type, unsigned int bitLocation, unsigned int windowID, int *pRetCode);
static bool testChangeBlockCommand(char *line, ImportedSet & is, int *pRetCode);
static void cleanStringForLocations(char *cleanString, char *strPtr);
static char *findBlockTypeAndData(char *line, int *pType, int *pData, unsigned short *pDataBits, wchar_t *error);
static char *compareLCAndSkip(char *a, char const *b);
static char *skipPastUnsignedInteger(char *strPtr);
static void createCB(ImportedSet & is);
static void addFromRangeToCB(ChangeBlockCommand *pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits);
static void setDefaultFromRangeToCB(ChangeBlockCommand *pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits);
static void addRangeToDataBitsArray(ChangeBlockCommand *pCBC, int fromType, int fromEndType, unsigned short fromDataBits);
static void saveCBinto(ChangeBlockCommand *pCBC, unsigned char intoType, unsigned char intoData);
static void addDataBitsArray(ChangeBlockCommand *pCBC);
static void saveCBlocation(ChangeBlockCommand *pCBC, int v[6]);
static void deleteCommandBlockSet(ChangeBlockCommand *pCBC);
static char *findLineDataNoCase(char *line, char *findStr);
static char *removeLeadingWhitespace(char *line);
static void saveErrorMessage(ImportedSet & is, wchar_t *error, char *restOfLine = NULL);
static void saveWarningMessage(ImportedSet & is, wchar_t *error);
static void saveMessage(ImportedSet & is, wchar_t *error, wchar_t *msgType, int increment, char *restOfLine = NULL);
static bool validBoolean(ImportedSet & is, char *string);
static bool interpretBoolean(char *string);
static void formTitle(WorldGuide *pWorldGuide, wchar_t *title);
static void rationalizeFilePath(wchar_t *fileName);
//static void checkUpdate( HINSTANCE hInstance );
static bool splitToPathAndName(wchar_t *pathAndName, wchar_t *path, wchar_t *name);
static bool commandLoadWorld(ImportedSet & is, wchar_t *error);
static bool commandLoadTerrainFile(ImportedSet & is, wchar_t *error);
static bool commandLoadColorScheme(ImportedSet & is, wchar_t *error);
static bool commandExportFile(ImportedSet & is, wchar_t *error, int fileMode, char *fileName);
static bool openLogFile(ImportedSet & is);

int APIENTRY _tWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
#ifdef TEST_FOR_MEMORY_LEAKS
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // get version info
    gMajorVersion = MINEWAYS_MAJOR_VERSION;
    gMinorVersion = MINEWAYS_MINOR_VERSION;

    GetCurrentDirectory(MAX_PATH, gCurrentDirectory);
    // which sort of separator? If "\" found, use that one, else "/" assumed.
    if (wcschr(gCurrentDirectory, (wchar_t)'\\') != NULL) {
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

    // get argv, argc from command line.
    gArgList = CommandLineToArgvW(GetCommandLine(), &gArgCount);
    if (gArgList == NULL)
    {
        MessageBox(NULL, L"Unable to parse command line", L"Error", MB_OK | MB_SYSTEMMODAL);
    }

    //for (int i = 0; i < gArgCount; i++)
    //{
    //	MessageBox(NULL, gArgList[i], L"Arglist contents", MB_OK);
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
    wcscpy_s(gSelectTerrainDir, MAX_PATH, gCurrentDirectory);
    wcscpy_s(gSelectTerrainPathAndName, MAX_PATH, gCurrentDirectory);
    wcscat_s(gSelectTerrainPathAndName, MAX_PATH - wcslen(gSelectTerrainPathAndName), L"\\terrainExt.png");

    // setting this to empty means the last path used (from last session, hopefully) will be used again
    wcscpy_s(gImportPath, MAX_PATH, L"");

    gWorldGuide.type = WORLD_UNLOADED_TYPE;
    gWorldGuide.sch.blocks = gWorldGuide.sch.data = NULL;

    gImportFile[0] = (wchar_t)0;
    gSchemeSelected[0] = (wchar_t)0;

    // start it with something, anything...
    gpEFD = &gExportViewData;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LOG_INFO(gExecutionLogfile, "execute initializeExportDialogData\n");
    initializeExportDialogData();
    // Initialize Skfb data
    memset(&gSkfbPData, 0, sizeof(PublishSkfbData));

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
    SetMapPremultipliedColors();

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
                char outputString[1024];
                sprintf_s(outputString, 1024, "Unexpected termination: failed with error code %d. Please report this problem to erich@acm.org\n", dw);
                LOG_INFO(gExecutionLogfile, outputString);
            }
            wchar_t wString[1024];
            swprintf_s(wString, 1024, L"Unexpected termination: failed with error code %d. Please report this problem to erich@acm.org\n", dw);
            MessageBox(NULL, wString, _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
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

    return (int) msg.wParam;
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

    wcex.style			= CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc	= WndProc;
    wcex.cbClsExtra		= 0;
    wcex.cbWndExtra		= 0;
    wcex.hInstance		= hInstance;
    wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINEWAYS));
    wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_MINEWAYS);
    wcex.lpszClassName	= szWindowClass;
    wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

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

    int x = 480;
    int y = 582;
    if (!modifyWindowSizeFromCommandLine(&x, &y, gArgList, gArgCount)) {
        exit(EXIT_FAILURE);
    }

    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, x, y, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}


wchar_t* stripWorldName(wchar_t* worldPath)
{
    // find last "/" or "\", as possible
    wchar_t *lastSplitBackslash = wcsrchr( worldPath, '\\' )+1;
    wchar_t *lastSplitSlash = wcsrchr( worldPath, '/' )+1;
    wchar_t *lastSplit = worldPath;
    if ( lastSplitBackslash > lastSplit )
        lastSplit = lastSplitBackslash;
    if ( lastSplitSlash > lastSplit )
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
static unsigned char *map=NULL;
static int bitWidth=0;
static int bitHeight=0;
static HWND progressBar=NULL;
static HBRUSH ctlBrush=NULL;
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId; // set but not used, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;
    static HWND hwndSlider,hwndBottomSlider,hwndLabel,hwndBottomLabel,hwndInfoLabel,hwndInfoBottomLabel,hwndStatus;
    static BITMAPINFO bmi;
    static HBITMAP bitmap=NULL;
    static HDC hdcMem=NULL;
    static int oldX=0,oldY=0;
    static const char *gBlockLabel="";
    static BOOL dragging=FALSE;
    static BOOL hdragging=FALSE;	// highlight dragging, dragging on the edge of the selected area
    static int moving=0;
    INITCOMMONCONTROLSEX ice;
    DWORD pos;
    wchar_t text[4];
    RECT rect;
    TCHAR path[MAX_PATH];
    TCHAR pathAndFile[MAX_PATH];
    OPENFILENAME ofn;
    int mx,my,mz,type,dataVal,biome;
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

        validateItems(GetMenu(hWnd));

        // get new directory for where world saves are located, if any: -s dir
        if (getWorldSaveDirectoryFromCommandLine(gWorldPathDefault, gArgList, gArgCount)) {
            // path found on command line, try it out.
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine successful\n");
        }
        else {
            // load default list of worlds
            LOG_INFO(gExecutionLogfile, " setWorldPath\n");
            int retCode = setWorldPath(gWorldPathDefault);

            if (retCode == 0)
            {
                LOG_INFO(gExecutionLogfile, "   couldn't find world saves directory\n");
                // this message will get popped up by loadWorldList
                //MessageBox(NULL, _T("Couldn't find your Minecraft world saves directory. You'll need to guide Mineways to where you save your worlds. Use the 'File -> Open...' option and find your level.dat file for the world. If you're on Windows, go to 'C:\\Users\\Eric\\AppData\\Roaming\\.minecraft\\saves' and find it in your world save directory. For Mac, worlds are usually located at /users/<your name>/Library/Application Support/minecraft/saves. Visit http://mineways.com or email me if you are still stuck."),
                //	_T("Informational"), MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
            }
        }

        LOG_INFO(gExecutionLogfile, " loadWorldList\n");
        if ( loadWorldList(GetMenu(hWnd)) )
        {
            LOG_INFO(gExecutionLogfile, "   world not converted\n");
            MessageBox(NULL, _T("Warning:\nAt least one of your worlds has not been converted to the Anvil format.\nThese worlds will be shown as disabled in the Open World menu.\nTo convert a world, run Minecraft 1.2 or later and play it, then quit."),
                _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
        }
        wcscpy_s(gWorldPathCurrent, MAX_PATH, gWorldPathDefault);

        LOG_INFO(gExecutionLogfile, " populateColorSchemes\n");
        populateColorSchemes(GetMenu(hWnd));
        CheckMenuItem(GetMenu(hWnd),IDM_CUSTOMCOLOR,MF_CHECKED);

        ctlBrush=CreateSolidBrush(GetSysColor(COLOR_WINDOW));

        ice.dwSize=sizeof(INITCOMMONCONTROLSEX);
        ice.dwICC=ICC_BAR_CLASSES;
        InitCommonControlsEx(&ice);
        GetClientRect(hWnd,&rect);
        hwndSlider=CreateWindowEx(
            0,TRACKBAR_CLASS,L"Trackbar Control",
            WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
            SLIDER_LEFT,0,rect.right-rect.left-40-SLIDER_LEFT,30,
            hWnd,(HMENU)ID_LAYERSLIDER,NULL,NULL);
        SendMessage(hwndSlider,TBM_SETRANGE,TRUE,MAKELONG(0,MAP_MAX_HEIGHT));
        SendMessage(hwndSlider,TBM_SETPAGESIZE,0,10);
        EnableWindow(hwndSlider,FALSE);

        hwndLabel=CreateWindowEx(
            0,L"STATIC",NULL,
            WS_CHILD | WS_VISIBLE | ES_RIGHT,
            rect.right-40,5,30,20,
            hWnd,(HMENU)ID_LAYERLABEL,NULL,NULL);
        SetWindowText(hwndLabel,MAP_MAX_HEIGHT_STRING);
        EnableWindow(hwndLabel,FALSE);

        hwndBottomSlider=CreateWindowEx(
            0,TRACKBAR_CLASS,L"Trackbar Control",
            WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
            SLIDER_LEFT,30,rect.right-rect.left-40-SLIDER_LEFT,30,
            hWnd,(HMENU)ID_LAYERBOTTOMSLIDER,NULL,NULL);
        SendMessage(hwndBottomSlider,TBM_SETRANGE,TRUE,MAKELONG(0,MAP_MAX_HEIGHT));
        SendMessage(hwndBottomSlider,TBM_SETPAGESIZE,0,10);
        EnableWindow(hwndBottomSlider,FALSE);

        hwndBottomLabel=CreateWindowEx(
            0,L"STATIC",NULL,
            WS_CHILD | WS_VISIBLE | ES_RIGHT,
            rect.right-40,35,30,20,
            hWnd,(HMENU)ID_LAYERBOTTOMLABEL,NULL,NULL);
        SetWindowText(hwndBottomLabel,SEA_LEVEL_STRING);
        EnableWindow(hwndBottomLabel,FALSE);

        setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true );

        // label to left
        hwndInfoLabel=CreateWindowEx(
            0,L"STATIC",NULL,
            WS_CHILD | WS_VISIBLE | ES_LEFT,
            5,5,SLIDER_LEFT,20,
            hWnd,(HMENU)ID_LAYERINFOLABEL,NULL,NULL);
        SetWindowText(hwndInfoLabel,L"Max height");
        EnableWindow(hwndInfoLabel,FALSE);

        hwndInfoBottomLabel=CreateWindowEx(
            0,L"STATIC",NULL,
            WS_CHILD | WS_VISIBLE | ES_LEFT,
            5,35,SLIDER_LEFT,20,
            hWnd,(HMENU)ID_LAYERINFOBOTTOMLABEL,NULL,NULL);
        SetWindowText(hwndInfoBottomLabel,L"Lower depth");
        EnableWindow(hwndInfoBottomLabel,FALSE);

        hwndStatus=CreateWindowEx(
            0,STATUSCLASSNAME,NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            -100,-100,10,10,
            hWnd,(HMENU)ID_STATUSBAR,NULL,NULL);
        {
            int parts[]={300,-1};
            RECT rect;
            SendMessage(hwndStatus,SB_SETPARTS,2,(LPARAM)parts);

            progressBar=CreateWindowEx(
                0,PROGRESS_CLASS,NULL,
                WS_CHILD | WS_VISIBLE,
                0,0,10,10,hwndStatus,(HMENU)ID_PROGRESS,NULL,NULL);
            SendMessage(hwndStatus,SB_GETRECT,1,(LPARAM)&rect);
            MoveWindow(progressBar,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);
            SendMessage(progressBar,PBM_SETSTEP,(WPARAM)5,0);
            SendMessage(progressBar,PBM_SETPOS,0,0);
        }

        gWS.hWnd = hWnd;
        gWS.hwndBottomSlider = hwndBottomSlider;
        gWS.hwndBottomLabel = hwndBottomLabel;
        gWS.hwndInfoBottomLabel = hwndInfoBottomLabel;
        gWS.hwndInfoLabel = hwndInfoLabel;
        gWS.hwndStatus = hwndStatus;
        gWS.hwndSlider = hwndSlider;
        gWS.hwndLabel = hwndLabel;

        rect.top+=MAIN_WINDOW_TOP;	// add in two sliders, 30 each
        bitWidth=rect.right-rect.left;
        bitHeight=rect.bottom-rect.top;
        ZeroMemory(&bmi.bmiHeader,sizeof(BITMAPINFOHEADER));
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=bitWidth;
        bmi.bmiHeader.biHeight=-bitHeight; //flip
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        LOG_INFO(gExecutionLogfile, " CreateDIBSection\n");
        bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&map, NULL, 0);

        // set standard custom color at startup.
        LOG_INFO(gExecutionLogfile, " useCustomColor\n");
        useCustomColor(IDM_CUSTOMCOLOR, hWnd);

        // finally, load any scripts on the command line.
        LOG_INFO(gExecutionLogfile, " processCreateArguments\n");
        processCreateArguments(gWS, &gBlockLabel, gHoldlParam, gArgList, gArgCount);
        break;
    case WM_LBUTTONDOWN:
        // if control key is held down, consider left-click to be a right-click,
        // i.e. for selection
        if ( GetKeyState(VK_CONTROL) < 0 )
        {
            gLeftIsRight = TRUE;
            goto RButtonDown;
        }
        dragging=TRUE;
        hdragging=FALSE;// just in case
        SetFocus(hWnd);
        SetCapture(hWnd);
        oldX=LOWORD(lParam);
        oldY=HIWORD(lParam);
        break;
    case WM_RBUTTONDOWN:
RButtonDown:
        gAdjustingSelection = 0;
        if (gLoaded)
        {
            int wasDragging = hdragging;
            hdragging=TRUE;
            dragging=FALSE;// just in case
            SetFocus(hWnd);
            SetCapture(hWnd);

            // get mouse position in world space
            (void)IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
            gHoldlParam=lParam;

            gStartHiX=mx;
            gStartHiZ=mz;
            gHighlightOn=TRUE;

            // these track whether a selection height volume has blocks in it,
            // low, medium, high, and minimum low-height found
            gHitsFound[0] = gHitsFound[1] = gHitsFound[2] = 0;
            // no longer used - now we use a direct call for this very purpose
            gHitsFound[3] = MAP_MAX_HEIGHT+1;

            // now to check the corners: is this location near any of them?
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );

            // if we weren't dragging before (making a selection), and there is
            // an active selection, see if we select on the selection border
            if ( !wasDragging && on )
            {
                // highlighting is on, check the corners: inside bounds of current selection?
                if ( ( mx >= minx - SELECT_MARGIN/gCurScale ) && 
                    ( mx <= maxx + SELECT_MARGIN/gCurScale ) &&
                    ( mz >= minz - SELECT_MARGIN/gCurScale ) &&
                    ( mz <= maxz + SELECT_MARGIN/gCurScale ) )
                {
                    int startx,endx,startz,endz;
                    int innerx = (int)(SELECT_MARGIN/gCurScale);
                    int innerz = (int)(SELECT_MARGIN/gCurScale);
                    gAdjustingSelection = 1;

                    if ( innerx > maxx-minx )
                    {
                        innerx = (maxx-minx-1)/2;
                    }
                    if ( innerz > maxz-minz )
                    {
                        innerz = (maxz-minz-1)/2;
                    }

                    if ( mx <= minx + innerx )
                    {
                        // in minx zone
                        startx = maxx;
                        endx = mx;
                    }
                    else if ( mx >= maxx - innerx )
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

                    if ( mz <= minz + innerz )
                    {
                        // in minz zone
                        startz = maxz;
                        endz = mz;
                    }
                    else if ( mz >= maxz - innerz )
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
                    if ( gLockMouseX && gLockMouseZ )
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
            // Reset Sketchfab path so that we rewrite obj/mtl/PNG + zip when publishing
            deleteFile();
            gSkfbPData.skfbFilePath = "";
            SetHighlightState(gHighlightOn,gStartHiX,gTargetDepth,gStartHiZ,mx,gCurDepth,mz);
            enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
            validateItems(GetMenu(hWnd));
            drawInvalidateUpdate(hWnd);
        }
        break;
    case WM_MOUSEWHEEL:
        if (gLoaded)
        {
            int zDelta=GET_WHEEL_DELTA_WPARAM(wParam);
            // ratchet zoom up by 2x when zoom of 8 or higher is reached, so it zooms faster
            gCurScale+=((double)zDelta/WHEEL_DELTA)*(pow(gCurScale,1.2)/gCurScale);
            gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
            drawInvalidateUpdate(hWnd);
        }
        break;
    case WM_LBUTTONUP:
        // if control key was held down on mouse down, consider left-click to be a right-click,
        // i.e. for selection
        if ( gLeftIsRight )
        {
            // turn off latch
            gLeftIsRight = FALSE;
            goto RButtonUp;
        }
        dragging=FALSE;
        hdragging=FALSE;	// just in case
        ReleaseCapture();
        break;
    case WM_MBUTTONDOWN:
        // set new target depth
        hdragging=FALSE;
        dragging=FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        int mx,mz;
        gBlockLabel=IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
            bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
        gHoldlParam=lParam;
        if ( my >= 0 && my <= MAP_MAX_HEIGHT )
        {
            // special test: if type is a flattop, then select the location one lower for export
            if ( (gBlockDefinitions[type].flags&BLF_FLATTOP) && (my>0) )
            {
                my--;
            }
            gTargetDepth = my;
            gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);   // should never happen that a flattop is at 0, but just in case
            // also set highlight state to new depths
            setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false );
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
            SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
            enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );

            updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);

            validateItems(GetMenu(hWnd));
            drawInvalidateUpdate(hWnd);
        }
        break;
    case WM_RBUTTONUP:
RButtonUp:
        dragging=FALSE;		// just in case
        gLockMouseX = gLockMouseZ = 0;
        ReleaseCapture();

        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );

        hdragging=FALSE;
        // Area selected.
        // Check if this selection is not an adjustment
        if ( !gAdjustingSelection )
        {
            // Not an adjustment, but a new selection. As such, test if there's something below to be selected and
            // there's nothing in the actual selection volume.
            assert(on);
            int minHeightFound = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, true, true, maxy);
            if (gHitsFound[0] && !gHitsFound[1])
            {
                // make sure there's some lower depth to use to replace current target depth
                if (minHeightFound < gTargetDepth)	// was gHitsFound[3]
                {
                    // send warning, set to min height found, then redo!
                    if ( gFullLow )
                    {
                        gFullLow = 0;
                        swprintf_s(msgString,1024,L"All blocks in your selection are below the current lower depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, displayed in the status bar at the bottom.\nYou can adjust this depth by using the lower slider or '[' & ']' keys.\n\nThe depth will be reset to %d to include all visible blocks.",
                            gTargetDepth, minHeightFound);
                    }
                    else
                    {
                        swprintf_s(msgString,1024,L"All blocks in your selection are below the current lower depth of %d.\n\nThe depth will be reset to %d to include all visible blocks.",
                            gTargetDepth, minHeightFound);
                    }
                    MessageBox( NULL, msgString,
                        _T("Informational"), MB_OK|MB_ICONINFORMATION);
                    //gTargetDepth = gHitsFound[3]; - no longer used.
                    gTargetDepth = minHeightFound;

                    setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false );
                    // update target depth
                    SetHighlightState(gHighlightOn,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
                    enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                    drawInvalidateUpdate(hWnd);
                }
                else
                {
                    // funny: target depth is lower than min height in lower section, which
                    // means lower section not really set. Don't do any warning, I guess.
                }
            }
            // else, test if there's something in both volumes and offer to adjust.
            else if ( gAutocorrectDepth &&
                ((  gHitsFound[0] && gHitsFound[1] && ( minHeightFound < gTargetDepth ) ) ||
                ( !gHitsFound[0] && gHitsFound[1] && ( minHeightFound > gTargetDepth) )) )
            {
                // send warning
                int retval;
                // send warning, set to min height found, then redo!
                if ( minHeightFound < gTargetDepth )
                {
                    if ( gFullLow )
                    {
                        gFullLow = 0;
                        swprintf_s(msgString,1024,L"Some blocks in your selection are visible below the current lower depth of %d.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, shown on the second slider at the top.\nYou can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to select all visible blocks?\nSelect 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound );
                    }
                    else
                    {
                        swprintf_s(msgString,1024,L"Some blocks in your selection are visible below the current lower depth of %d.\n\nDo you want to set the depth to %d to select all visible blocks?\nSelect 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound );
                    }
                }
                else
                {
                    if ( gFullLow )
                    {
                        gFullLow = 0;
                        swprintf_s(msgString,1024,L"The current selection Lower depth of %d contains hidden lower layers.\n\nWhen you select, you're selecting in three dimensions, and there\nis a lower depth, shown on the \"Lower\" slider.\nYou can adjust this depth by using this slider or '[' & ']' keys.\n\nDo you want to set the depth to %d to minimize the underground? (\"Yes\" is probably what you want.)\nSelect 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound );
                    }
                    else
                    {
                        swprintf_s(msgString,1024,L"The current selection Lower depth of %d contains hidden lower layers.\n\nDo you want to set the depth to %d to minimize the underground?\nSelect 'Cancel' to turn off this autocorrection system.\n\nUse the spacebar later if you want to make this type of correction for a given selection.",
                            gTargetDepth, minHeightFound );
                    }
                }
                // System modal puts it topmost, and task modal stops things from continuing without an answer. Unfortunately, task modal does not force the dialog on top.
                // We force it here, as it's OK if it gets ignored, but we want people to see it.
                retval = MessageBox( NULL, msgString,
                    _T("Informational"), MB_YESNOCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_SYSTEMMODAL);
                if ( retval == IDYES )
                {
                    gTargetDepth = minHeightFound;
                    setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false );
                    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                    // update target depth
                    SetHighlightState(gHighlightOn,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
                    enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                    drawInvalidateUpdate(hWnd);
                }
                else if ( retval == IDCANCEL )
                {
                    gAutocorrectDepth = 0;
                }
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (gLoaded)
        {
            updateCursor( lParam, hdragging);
            // is left-mouse button down and we're changing the viewed area?
            if (dragging)
            {
                // mouse coordinate can now be negative
                int mouseX = LOWORD(lParam);
                if ( mouseX > 0x7fff )
                    mouseX -= 0x10000;
                int mouseY = HIWORD(lParam);
                if ( mouseY > 0x7fff )
                    mouseY -= 0x10000;
                gCurZ-=(mouseY-oldY)/gCurScale;
                gCurX-=(mouseX-oldX)/gCurScale;
                oldX=mouseX;
                oldY=mouseY;
                drawInvalidateUpdate(hWnd);
            }
            // for given mouse position and world center, determine
            // mx, mz, the world coordinates that the mouse is over,
            // and return the name of the block type it's over

            // mask off highest bit (the negative) for mouse location
            lParam &= 0x7fff7fff;

            gBlockLabel=IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
            gHoldlParam=lParam;
            // is right mouse button down and we're dragging out a selection box?
            if (hdragging && gLoaded)
            {
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                // change map center, in world coordinates, by mouse move
                if ( gLockMouseZ )
                {
                    gStartHiZ = minz;
                    mz = maxz;
                }
                if ( gLockMouseX )
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
                SetHighlightState(gHighlightOn,gStartHiX,gTargetDepth,gStartHiZ,mx,gCurDepth,mz);
                enableBottomControl( gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                drawInvalidateUpdate(hWnd);
            }
            updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);
        }
        break;
    case WM_KEYDOWN:
        // Check if control key is not being held down. If so,
        // ignore this keypress and assume the .rc file has
        // emitted a corresponding message. Else control+S will
        // have the effect of saving and scrolling the map.
        if ( gLoaded && GetKeyState(VK_CONTROL) >= 0 )
        {
//#ifdef _DEBUG
//            wchar_t outputString[256];
//            swprintf_s(outputString,256,L"key: %d\n",wParam);
//            OutputDebugString( outputString );
//#endif

            BOOL changed=FALSE;
            switch (wParam)
            {
            case VK_UP:
            case 'W':
                moving|=1;
                break;
            case VK_DOWN:
            case 'S':
                moving|=2;
                break;
            case VK_LEFT:
            case 'A':
                moving|=4;
                break;
            case VK_RIGHT:
            case 'D':
                moving|=8;
                break;
            case VK_PRIOR:
            case 'E':
            case VK_ADD:
            case VK_OEM_PLUS:
               gCurScale+=0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                if (gCurScale>MAXZOOM)
                    gCurScale=MAXZOOM;
                changed=TRUE;
                break;
            case VK_NEXT:
            case 'Q':
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                gCurScale-=0.5; // 0.25*pow(gCurScale,1.2)/gCurScale;
                if (gCurScale<MINZOOM)
                    gCurScale=MINZOOM;
                changed=TRUE;
                break;
                // increment target depth by one
            case VK_OEM_4:    // [
                gTargetDepth++;
                gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);
                setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false );
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
                enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                REDRAW_ALL;
                break;
                // decrement target depth by one
            case VK_OEM_6:    // ]
                gTargetDepth--;
                gTargetDepth = clamp(gTargetDepth,0,MAP_MAX_HEIGHT);
                setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false );
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
                SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
                enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                REDRAW_ALL;
                break;
            case VK_OEM_PERIOD:
            case '.':
            case '>':
                if (gCurDepth>0)
                {
                    gCurDepth--;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                }
                REDRAW_ALL;
                break;
            case VK_OEM_COMMA:
            case ',':
            case '<':
                if (gCurDepth<MAP_MAX_HEIGHT)
                {
                    gCurDepth++;
                    setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                }
                REDRAW_ALL;
                break;
            case '0':
                gCurDepth=0;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '1':
                gCurDepth=10;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '2':
                gCurDepth=20;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '3':
                gCurDepth=30;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '4':
                gCurDepth=40;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '5':
                gCurDepth=51;	// bottom dirt layer of deep lakes
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '6':
                gCurDepth=SEA_LEVEL;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '7':
                gCurDepth=85;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '8':
                gCurDepth=106;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case '9':
                gCurDepth=MAP_MAX_HEIGHT;
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                REDRAW_ALL;
                break;
            case VK_HOME:
                gCurScale=MAXZOOM;
                changed=TRUE;
                break;
            case VK_END:
                gCurScale=MINZOOM;
                changed=TRUE;
                break;
            case VK_ESCAPE:
                // deselect - remove selection
                gHighlightOn=FALSE;
                SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0);
                changed=TRUE;
                break;
            case VK_SPACE:
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
                if (on) {
                    bool useOnlyOpaque= !(GetKeyState(VK_SHIFT) < 0);
                    gTargetDepth = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, true, useOnlyOpaque, maxy);
                    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                    REDRAW_ALL;
                }
                break;
            default:
                // unknown key, don't move
                moving=0;
                break;
            }

            if (moving!=0)
            {
                if (moving&1) //up
                    gCurZ-=10.0/gCurScale;
                if (moving&2) //down
                    gCurZ+=10.0/gCurScale;
                if (moving&4) //left
                    gCurX-=10.0/gCurScale;
                if (moving&8) //right
                    gCurX+=10.0/gCurScale;
                changed=TRUE;
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
            moving&=~1;
            break;
        case VK_DOWN:
        case 'S':
            moving&=~2;
            break;
        case VK_LEFT:
        case 'A':
            moving&=~4;
            break;
        case VK_RIGHT:
        case 'D':
            moving&=~8;
            break;
        }
        break;
    case WM_HSCROLL:
        pos=(DWORD)SendMessage(hwndSlider,TBM_GETPOS,0,0);
        _itow_s(MAP_MAX_HEIGHT-pos,text,10);
        SetWindowText(hwndLabel,text);
        gCurDepth=MAP_MAX_HEIGHT-pos;

        pos=(DWORD)SendMessage(hwndBottomSlider,TBM_GETPOS,0,0);
        _itow_s(MAP_MAX_HEIGHT-pos,text,10);
        SetWindowText(hwndBottomLabel,text);
        gTargetDepth=MAP_MAX_HEIGHT-pos;

        syncCurrentHighlightDepth();

        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
        SetHighlightState(on,minx,gTargetDepth,minz,maxx,gCurDepth,maxz);
        enableBottomControl( on, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
        gBlockLabel=IDBlock(LOWORD(gHoldlParam),HIWORD(gHoldlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
            bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
        updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);
        SetFocus(hWnd);
        drawInvalidateUpdate(hWnd);
        break;
    case WM_CTLCOLORSTATIC: //color the label and the slider background
        {
            HDC hdcStatic=(HDC)wParam;
            SetTextColor(hdcStatic,GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdcStatic,GetSysColor(COLOR_WINDOW));
            return (INT_PTR)ctlBrush;
        }
        break;
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        // set but not used: wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        if (wmId >= IDM_CUSTOMCOLOR && wmId < IDM_CUSTOMCOLOR + 1000)
        {
            useCustomColor(wmId, hWnd);
        }

        if (wmId>IDM_WORLD && wmId<IDM_WORLD+999)
        {
            // Load world from list that's real (not Block Test World, which is IDM_TEST_WORLD, below)
            
            int loadErr;
            //convert path to utf8
            //WideCharToMultiByte(CP_UTF8,0,worlds[wmId-IDM_WORLD],-1,gWorldGuide.world,MAX_PATH,NULL,NULL);
            gSameWorld = (wcscmp(gWorldGuide.world,gWorlds[wmId-IDM_WORLD])==0);
            wcscpy_s(gWorldGuide.world,MAX_PATH,gWorlds[wmId-IDM_WORLD]);
            // if this is not the same world, switch back to the aboveground view.
            // TODO: this code is repeated, should really be a subroutine.
            if (!gSameWorld)
            {
                gotoSurface( hWnd, hwndSlider, hwndLabel);
            }
            gWorldGuide.type = WORLD_LEVEL_TYPE;
            loadErr = loadWorld(hWnd);
            if ( loadErr )
            {
                // world not loaded properly
                switch (loadErr) {
                case 1:
                    MessageBox(NULL, _T("Error: cannot read world's file version."),
                        _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                    break;
                case 2:
                    MessageBox(NULL, _T("Error: world has not been converted to the Anvil format.\nTo convert a world, run Minecraft 1.2 or later and play it, then quit.\nTo use Mineways on an old-style McRegion world, download\nVersion 1.15 from the mineways.com site."),
                        _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                    break;
                case 3:
                    MessageBox(NULL, _T("Error: cannot read world's spawn location - every world should have one."),
                        _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                    break;
                default:
                    MessageBox( NULL, _T("Error: cannot read world. Unknown error code, which is very strange... Please send me the level.dat file."),
                        _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                    break;
                }

                return 0;
            }
            setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
        }
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case ID_SELECT_ALL:
            if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE) {
                gTargetDepth = 0;
                gCurDepth = gWorldGuide.sch.height - 1;

                setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, false);
                // update target depth
                gHighlightOn = TRUE;
                SetHighlightState(gHighlightOn, 0, 0, 0, gWorldGuide.sch.width - 1, gWorldGuide.sch.height - 1, gWorldGuide.sch.length - 1);
                enableBottomControl(gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel);
                drawInvalidateUpdate(hWnd);
            }
            break;
        case IDM_COLOR:
            {
                doColorSchemes(hInst,hWnd);
                populateColorSchemes(GetMenu(hWnd));
                // always go back to the standard color scheme after editing, as editing
                // could have removed the custom scheme being modified.
                wchar_t *schemeSelected = getSelectedColorScheme();
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
            deleteFile();
            DestroyWindow(hWnd);
            break;
        case IDM_TEST_WORLD:
            gWorldGuide.world[0] = 0;
            gSameWorld = FALSE;
            sprintf_s(gSkfbPData.skfbName, "TestWorld");
            gotoSurface( hWnd, hwndSlider, hwndLabel);
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
            wcscpy_s(path, MAX_PATH, gWorldPathCurrent);
            ofn.lpstrInitialDir = path;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileName(&ofn) == TRUE)
            {
                // first, "rationalize" the gWorldGuide.world name: make it all \'s or all /'s, not both.
                // This will make it nicer for comparing to see if the new world is the same as the previously-loaded world.
                rationalizeFilePath(pathAndFile);

                // schematic or world?
                wchar_t filename[MAX_PATH];
                splitToPathAndName(pathAndFile, NULL, filename);
                size_t len = wcslen(filename);
                if (_wcsicmp(&filename[len - 10], L".schematic") == 0) {
                    // Read schematic as a world.
                    gWorldGuide.type = WORLD_SCHEMATIC_TYPE;
                    gWorldGuide.sch.repeat = (StrStrI(filename, L"repeat") != NULL);
                    gSameWorld = (wcscmp(gWorldGuide.world, pathAndFile) == 0);
                    wcscpy_s(gWorldGuide.world, MAX_PATH, pathAndFile);
                    if (loadWorld(hWnd)) {
                    //if (loadSchematic(pathAndFile)) {
                        // schematic world not loaded properly
                        gWorldGuide.type = WORLD_UNLOADED_TYPE;
                        MessageBox(NULL, _T("Error: cannot read Minecraft schematic."),
                            _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

                        return 0;
                    }
                    UpdateWindow(hWnd);
                }
                else {
                    // read level.dat normal world

                    PathRemoveFileSpec(pathAndFile);
                    //convert path to utf8
                    //WideCharToMultiByte(CP_UTF8,0,path,-1,gWorldGuide.world,MAX_PATH,NULL,NULL);
                    gSameWorld = (wcscmp(gWorldGuide.world, pathAndFile) == 0);
                    wcscpy_s(gWorldGuide.world, MAX_PATH, pathAndFile);

                    // if this is not the same world, switch back to the aboveground view.
                    if (!gSameWorld)
                    {
                        gotoSurface(hWnd, hwndSlider, hwndLabel);
                    }

                    UpdateWindow(hWnd);
                    gWorldGuide.type = WORLD_LEVEL_TYPE;
                    if (loadWorld(hWnd))
                    {
                        // world not loaded properly
                        MessageBox(NULL, _T("Error: cannot read world. Perhaps you are trying to read in an Education Edition, Pocket Edition, or Windows 10 Minecraft Beta world? Mineways cannot read these, as they are in a different format. You can manually convert your world to the 'classic' Minecraft format, which Mineways can read. Go to http://mineways.com/mineways.html, search on `pocket edition', click on the link, and follow those instructions."),
                            _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

                        return 0;
                    }
                }
                // the file read succeeded, so update path to it.
                wcscpy_s(gWorldPathCurrent, MAX_PATH, pathAndFile);
                setUIOnLoadWorld(hWnd, hwndSlider, hwndLabel, hwndInfoLabel, hwndBottomSlider, hwndBottomLabel);
            }
            gOpenFilterIndex = ofn.nFilterIndex;
            break;
        case IDM_FILE_SELECTTERRAIN:
            ZeroMemory(&ofn,sizeof(OPENFILENAME));
            ofn.lStructSize=sizeof(OPENFILENAME);
            ofn.hwndOwner=hWnd;
            wcscpy_s(pathAndFile,MAX_PATH,gSelectTerrainPathAndName);
            ofn.lpstrFile = pathAndFile;
            //path[0]=0;
            ofn.nMaxFile=MAX_PATH;
            ofn.lpstrFilter=L"Terrain File (terrainExt.png)\0*.png\0";
            ofn.nFilterIndex=1;
            ofn.lpstrFileTitle=NULL;
            ofn.nMaxFileTitle=0;
            wcscpy_s(path, MAX_PATH, gSelectTerrainDir);
            ofn.lpstrInitialDir = path;
            ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)==TRUE)
            {
                // copy file name, since it definitely appears to exist.
                rationalizeFilePath(pathAndFile);
                wcscpy_s(gSelectTerrainPathAndName, MAX_PATH, pathAndFile);
                splitToPathAndName(gSelectTerrainPathAndName, gSelectTerrainDir, NULL);
            }
            break;
        case ID_FILE_IMPORTSETTINGS:
            ZeroMemory(&ofn,sizeof(OPENFILENAME));
            ofn.lStructSize=sizeof(OPENFILENAME);
            ofn.hwndOwner=hWnd;
            wcscpy_s(pathAndFile, MAX_PATH, gImportFile);
            ofn.lpstrFile = pathAndFile;
            ofn.nMaxFile=MAX_PATH;
            ofn.lpstrFilter = L"All files (*.obj;*.txt;*.wrl;*.mwscript)\0*.obj;*.txt;*.wrl;*.mwscript\0Wavefront OBJ (*.obj)\0*.obj\0Summary STL text file (*.txt)\0*.txt\0VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0Mineways script file (*.mwscript)\0*.mwscript\0";
            ofn.nFilterIndex = gImportFilterIndex;
            ofn.lpstrFileTitle=NULL;
            ofn.nMaxFileTitle=0;
            wcscpy_s(path, MAX_PATH, gImportPath);
            ofn.lpstrInitialDir = path;
            ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileName(&ofn)==TRUE)
            {
                // copy file name, since it definitely appears to exist.
                rationalizeFilePath(pathAndFile);
                wcscpy_s(gImportFile, MAX_PATH, pathAndFile);
                splitToPathAndName(gImportFile, gImportPath, NULL);
                runImportOrScript(gImportFile, gWS, &gBlockLabel, gHoldlParam, true);
            }
            gImportFilterIndex = ofn.nFilterIndex;
            break;
        case IDM_FILE_PRINTOBJ:
        case IDM_FILE_SAVEOBJ:
        case IDM_FILE_SCHEMATIC:
            if ( !gHighlightOn )
            {
                // we keep the export options ungrayed now so that they're selectable when the world is loaded
                MessageBox( NULL, _T("Click and drag with your right mouse button to select an area to export."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
                break;
            }
            switch ( wmId )
            {
            case IDM_FILE_SAVEOBJ:
                gPrintModel = 0;
                break;
            case IDM_FILE_PRINTOBJ:
                gPrintModel = 1;
                break;
            case IDM_FILE_SCHEMATIC:
                gPrintModel = 2;
                break;
            default:
                MY_ASSERT(gAlwaysFail);
                gPrintModel = 0;
            }
            {
                if ( gPrintModel == 2 )
                {
                    // schematic
                    ZeroMemory(&ofn,sizeof(OPENFILENAME));
                    ofn.lStructSize=sizeof(OPENFILENAME);
                    ofn.hwndOwner=hWnd;
                    ofn.lpstrFile=gExportPath;
                    //gExportPath[0]=0;
                    ofn.nMaxFile=MAX_PATH;
                    ofn.lpstrFilter= L"Schematic file (*.schematic)\0*.schematic\0";
                    ofn.nFilterIndex= 1;
                    ofn.lpstrFileTitle=NULL;
                    ofn.nMaxFileTitle=0;
                    wcscpy_s(path, MAX_PATH, gImportPath);
                    ofn.lpstrInitialDir = path;
                    ofn.lpstrTitle = L"Save Model to Schematic File";
                    ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    saveOK = GetSaveFileName(&ofn);

                    gExportSchematicData.fileType = FILE_TYPE_SCHEMATIC;	// always
                }
                else
                {
                    // print model or render model - quite similar
                    ZeroMemory(&ofn,sizeof(OPENFILENAME));
                    ofn.lStructSize=sizeof(OPENFILENAME);
                    ofn.hwndOwner=hWnd;
                    ofn.lpstrFile=gExportPath;
                    //gExportPath[0]=0;
                    ofn.nMaxFile=MAX_PATH;
                    ofn.lpstrFilter= gPrintModel ? L"Sculpteo: Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0i.materialise: Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0Shapeways: VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0" :
                        L"Wavefront OBJ, absolute (*.obj)\0*.obj\0Wavefront OBJ, relative (*.obj)\0*.obj\0Binary Materialise Magics STL stereolithography file (*.stl)\0*.stl\0Binary VisCAM STL stereolithography file (*.stl)\0*.stl\0ASCII text STL stereolithography file (*.stl)\0*.stl\0VRML 2.0 (VRML 97) file (*.wrl)\0*.wrl\0";
                    ofn.nFilterIndex=(gPrintModel ? gExportPrintData.fileType+1 : gExportViewData.fileType+1);
                    ofn.lpstrFileTitle=NULL;
                    ofn.nMaxFileTitle=0;
                    wcscpy_s(path, MAX_PATH, gImportPath);
                    ofn.lpstrInitialDir = path;
                    ofn.lpstrTitle = gPrintModel ? L"Save Model for 3D Printing" : L"Save Model for Rendering";
                    ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                    saveOK = GetSaveFileName(&ofn);
                    // save file type selected, no matter what (even on cancel); we
                    // always set it because even if someone cancels a save, he probably still
                    // wanted the file type chosen.
                    if ( gPrintModel )
                    {
                        gExportPrintData.fileType = ofn.nFilterIndex-1;
                    }
                    else
                    {
                        gExportViewData.fileType = ofn.nFilterIndex-1;
                    }
                }
                if ( saveOK )
                {
                    // if we got this far, then previous export is off, and we also want to ask for export dialog itself.
                    gExported=0;
                    wcscpy_s(gImportFile, MAX_PATH, gExportPath);

            case IDM_FILE_REPEATPREVIOUSEXPORT:
                gExported = saveObjFile(hWnd, gExportPath, gPrintModel, gSelectTerrainPathAndName, gSchemeSelected, (gExported==0), gShowPrintStats);

                SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );
                enableBottomControl( 1, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel );
                // put target depth to new depth set, if any
                if ( gTargetDepth != gpEFD->maxyVal )
                {
                    gTargetDepth = gpEFD->minyVal;
                }
                gBlockLabel=IDBlock(LOWORD(gHoldlParam),HIWORD(gHoldlParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
                    bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
                updateStatus(mx,mz,my,gBlockLabel,type,dataVal,biome,hwndStatus);
                setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
                setSlider( hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true );
                }   // matches to "if ( saveOK )"
            }
            break;
        case IDM_PUBLISH_SKFB:
            if ( !gHighlightOn )
            {
                // we keep the export options ungrayed now so that they're selectable when the world is loaded
                MessageBox( NULL, _T("Click and drag with your right mouse button to select an area to export."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
                break;
            }
            // Force it to be an rendering export: Relative obj
            gPrintModel=0;
            gExportViewData.fileType=FILE_TYPE_WAVEFRONT_REL_OBJ;
            ZeroMemory(&ofn,sizeof(OPENFILENAME));
            ofn.lStructSize=sizeof(OPENFILENAME);
            ofn.hwndOwner=hWnd;
            ofn.lpstrFile=gExportPath;
            ofn.nMaxFile=MAX_PATH;

            ofn.nFilterIndex=gExportViewData.fileType+1;
            ofn.lpstrFileTitle=NULL;
            ofn.nMaxFileTitle=0;
            ofn.lpstrInitialDir=NULL;
            ofn.Flags=OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            // Write zip in temp directory
            GetTempPath(MAX_PATH, tempdir);

            // OSX workaround since tempdir will not be ok
            // TODO: find a better way to detect OSX + Wine vs Windows
            if ( !PathFileExists(tempdir) )
            {
                swprintf_s(tempdir, MAX_PATH, L"\\tmp\\");
            }
            swprintf_s(filepath, MAX_PATH, L"%sMineways2Skfb", tempdir);
            publishToSketchfab(hWnd, filepath, gSelectTerrainPathAndName, gSchemeSelected);
            break;
        case IDM_JUMPSPAWN:
            gCurX=gSpawnX;
            gCurZ=gSpawnZ;
            if (gOptions.worldType&HELL)
            {
                gCurX/=8.0;
                gCurZ/=8.0;
            }
            REDRAW_ALL;
            break;
        case IDM_JUMPPLAYER:
            gCurX=gPlayerX;
            gCurZ=gPlayerZ;
            if (gOptions.worldType&HELL)
            {
                gCurX/=8.0;
                gCurZ/=8.0;
            }
            REDRAW_ALL;
            break;
        case IDM_VIEW_JUMPTOMODEL: // F4
            if ( !gHighlightOn )
            {
                // we keep the jump option ungrayed now so that it's selectable when the world is loaded
                MessageBox( NULL, _T("No model selected. To select a model, click and drag with the right mouse button."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
                break;
            }
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
            if ( on )
            {
                gCurX=(minx+maxx)/2;
                gCurZ=(minz+maxz)/2;
                if (gOptions.worldType&HELL)
                {
                    gCurX/=8.0;
                    gCurZ/=8.0;
                }
                REDRAW_ALL;
            }
            break;
        case IDM_SHOWALLOBJECTS:
            // TODO: super-minor bug: if mouse does not move when you turn all objects on, the
            // status bar will continue to list the object there before toggling, e.g. if a wall
            // sign was shown and you toggle Show All Objects off, the wall sign status will still be there.
            gOptions.worldType^=SHOWALL;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&SHOWALL)?MF_CHECKED:MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_VIEW_SHOWBIOMES:
            // toggles bit from its previous state
            gOptions.worldType ^= BIOMES;
            CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType&BIOMES) ? MF_CHECKED : MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_DEPTH:
            gOptions.worldType ^= DEPTHSHADING;
            CheckMenuItem(GetMenu(hWnd), wmId, (gOptions.worldType&DEPTHSHADING) ? MF_CHECKED : MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_LIGHTING:
            gOptions.worldType^=LIGHTING;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&LIGHTING)?MF_CHECKED:MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_CAVEMODE:
            gOptions.worldType^=CAVEMODE;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&CAVEMODE)?MF_CHECKED:MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_OBSCURED:
            gOptions.worldType^=HIDEOBSCURED;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.worldType&HIDEOBSCURED)?MF_CHECKED:MF_UNCHECKED);
            REDRAW_ALL;
            break;
        case IDM_RELOAD_WORLD:
            // reload world, if loaded
            if ( gLoaded )
            {
                if (loadWorld(hWnd))
                {
                    // world not loaded properly
                    MessageBox( NULL, _T("Error: cannot reload world."),
                        _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

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
                MessageBox(NULL, _T("You need to load a world first. Use 'Open World' or 'Open...' and find your level.dat file in %appdata%/.minecraft/saves."), _T("Informational"), MB_OK | MB_ICONINFORMATION);
            }
            break;
        case IDM_HELL:
            if (gWorldGuide.type == WORLD_LEVEL_TYPE) {
                if (!(gOptions.worldType&HELL))
                {
                    CheckMenuItem(GetMenu(hWnd), IDM_HELL, MF_CHECKED);
                    gOptions.worldType |= HELL;
                    CheckMenuItem(GetMenu(hWnd), IDM_END, MF_UNCHECKED);
                    gOptions.worldType &= ~ENDER;
                    // change scale as needed
                    gCurX /= 8.0;
                    gCurZ /= 8.0;
                    // it's useless to view Nether from MAP_MAX_HEIGHT
                    if (gCurDepth == MAP_MAX_HEIGHT)
                    {
                        gCurDepth = 126;
                        setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
                    }
                    gOverworldHideStatus = gOptions.worldType&HIDEOBSCURED;
                    gOptions.worldType |= HIDEOBSCURED;
                    //gTargetDepth=0;
                    // semi-useful, I'm not sure: zoom in when going to nether
                    //gCurScale *= 8.0;
                    //gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
                }
                else
                {
                    // back to the overworld
                    gotoSurface(hWnd, hwndSlider, hwndLabel);
                }
                CheckMenuItem(GetMenu(hWnd), IDM_OBSCURED, (gOptions.worldType&HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
                CloseAll();
                // clear selection when you switch from somewhere else to The Nether, or vice versa
                gHighlightOn = FALSE;
                SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0);
                enableBottomControl(gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel);

                REDRAW_ALL;
            }

            break;
        case IDM_END:
            if (gWorldGuide.type == WORLD_LEVEL_TYPE) {
                if (!(gOptions.worldType&ENDER))
                {
                    // entering Ender, turn off hell if need be
                    CheckMenuItem(GetMenu(hWnd), IDM_END, MF_CHECKED);
                    gOptions.worldType |= ENDER;
                    if (gOptions.worldType&HELL)
                    {
                        // get out of hell zoom
                        gCurX *= 8.0;
                        gCurZ *= 8.0;
                        // and undo other hell stuff
                        if (gCurDepth == 126)
                        {
                            gCurDepth = MAP_MAX_HEIGHT;
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
                SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0);
                enableBottomControl(gHighlightOn, hwndBottomSlider, hwndBottomLabel, hwndInfoBottomLabel);

                REDRAW_ALL;
            }
            break;
        case IDM_HELP_GIVEMEMOREMEMORY:
            // If you are using the 32-bit version, this option can help give you more memory during export.
            // It clears and reloads the world during this process, the freed memory can then be used for
            // other export processing functions.
            gOptions.moreExportMemory = !gOptions.moreExportMemory;
            CheckMenuItem(GetMenu(hWnd),wmId,(gOptions.moreExportMemory)?MF_CHECKED:MF_UNCHECKED);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        validateItems(GetMenu(hWnd));
        break;
    case WM_ERASEBKGND:
        {
            hdc=(HDC)wParam;
            GetClipBox(hdc,&rect);
            rect.bottom=MAIN_WINDOW_TOP;
            HBRUSH hb=CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(hdc,&rect,hb);
            DeleteObject(hb);
        }
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        GetClientRect(hWnd,&rect);
        rect.top+=MAIN_WINDOW_TOP;
        if (hdcMem==NULL)
        {
            hdcMem=CreateCompatibleDC(hdc);
            SelectObject(hdcMem,bitmap);
        }
        BitBlt(hdc,0,MAIN_WINDOW_TOP,bitWidth,bitHeight,hdcMem,0,0,SRCCOPY);
        EndPaint(hWnd, &ps);
        break;
    case WM_SIZING: //window resizing
        GetClientRect(hWnd,&rect);
        SetWindowPos(hwndSlider,NULL,0,0,
            rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndBottomSlider,NULL,0,30,
            rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndLabel,NULL,rect.right-40,5,
            30,20,SWP_NOACTIVATE);
        SetWindowPos(hwndBottomLabel,NULL,rect.right-40,35,
            30,20,SWP_NOACTIVATE);

        break;
    case WM_SIZE: //resize window
        SendMessage(hwndStatus,WM_SIZE,0,0);
        SendMessage(hwndStatus,SB_GETRECT,1,(LPARAM)&rect);
        MoveWindow(progressBar,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);

        GetClientRect(hWnd,&rect);
        SetWindowPos(hwndSlider,NULL,0,0,
            rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndBottomSlider,NULL,0,30,
            rect.right-rect.left-40-SLIDER_LEFT,30,SWP_NOMOVE|SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(hwndLabel,NULL,rect.right-40,5,
            30,20,SWP_NOACTIVATE);
        SetWindowPos(hwndBottomLabel,NULL,rect.right-40,35,
            30,20,SWP_NOACTIVATE);
        rect.top+=MAIN_WINDOW_TOP;
        rect.bottom-=23;
        bitWidth=rect.right-rect.left;
        bitHeight=rect.bottom-rect.top;
        bmi.bmiHeader.biWidth=bitWidth;
        bmi.bmiHeader.biHeight=-bitHeight;
        if (bitmap!=NULL)
            DeleteObject(bitmap);
        bitmap=CreateDIBSection(NULL,&bmi,DIB_RGB_COLORS,(void **)&map,NULL,0);
        if (hdcMem!=NULL)
            SelectObject(hdcMem,bitmap);

        // increase size of status bar if possible, so that biome information gets displayed
        {
            int parts[]={max(300,bitWidth*2/3),-1};
            SendMessage(hwndStatus,SB_SETPARTS,2,(LPARAM)parts);
        }

        // On resize, figure out a better hash table size for cache, if needed.
        if ( (rect.bottom-rect.top) * (rect.right-rect.left) > 256 * gOptions.currentCacheSize )
        {
            // make new cache twice the size of the screen's needs, should be enough I hope.
            gOptions.currentCacheSize = 2 * (rect.bottom-rect.top) * (rect.right-rect.left) / 256;
            ChangeCache( gOptions.currentCacheSize );
        }
        //InvalidateRect(hWnd,NULL,TRUE);
        //UpdateWindow(hWnd);
        draw();
        break;

    case WM_DESTROY:
        closeMineways();
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    if ( UnknownBlockRead() && NeedToCheckUnknownBlock() )
    {
        // flag this just the first time found, then turn off error and don't check again for this world
        CheckUnknownBlock( false );
        ClearBlockReadCheck();

        MessageBox( NULL, _T("Warning: Mineways encountered an unknown block type in your model. Such blocks are converted to bedrock. Mineways does not understand blocks added by mods. If you are not using mods, your version of Mineways may be out of date. Check http://mineways.com for a newer version of Mineways."),
            _T("Read error"), MB_OK|MB_ICONERROR);
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
static bool startExecutionLogFile(const LPWSTR *argList, int argCount)
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
                    //wchar_t longFileName[MAX_PATH];
                    //wcscpy_s(longFileName, MAX_PATH, gCurrentDirectory);
                    //wcscat_s(longFileName, MAX_PATH - wcslen(longFileName), gPreferredSeparatorString);
                    //wcscat_s(longFileName, MAX_PATH - wcslen(longFileName), argList[argIndex]);
                    //gExecutionLogfile = PortaCreate(argList[argIndex]);
                    //if (gExecutionLogfile == INVALID_HANDLE_VALUE) {
                    MessageBox(NULL, _T("Cannot open script log file, execution log file is specified by '-l log-file-name'."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
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

                errNum = asctime_s(timeString, 32, &newtime);
                if (!errNum)
                {
                    char outputString[256];
                    sprintf_s(outputString, 256, "Mineways version %d.%02d execution log begun %s\n", gMajorVersion, gMinorVersion, timeString);
                    LOG_INFO(gExecutionLogfile, outputString);
                }
                return true;
            }
            // if we got here, parsing didn't work
            MessageBox(NULL, _T("Command line startup error, execution log file is specified by '-l log-file-name'."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
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
static bool modifyWindowSizeFromCommandLine(int *x, int *y, const LPWSTR *argList, int argCount)
{
    // arguments possible:
    // -w x y
    // script name. Put double quotes around script name if it has spaces in the file name or directory path
    int argIndex = 1;
    while (argIndex < argCount)
    {
        // is it a script? get past it
        if (wcscmp(argList[argIndex], L"-w") == 0) {
            // found window resize
            argIndex++;
            int valx = 0;
            int valy = 0;
            if (argIndex < argCount) {
                // convert next argument to an integer
                valx = _wtoi(argList[argIndex]);
                argIndex++;
                if (valx > 0) {
                    if (argIndex < argCount) {
                        valy = _wtoi(argList[argIndex]);
                        argIndex++;
                        if (valy > 0) {
                            // found it! Always use the first one found
                            *x = valx;
                            *y = valy;
                            return true;
                        }
                    }
                }
            }
            // if we got here, parsing didn't work
            MessageBox(NULL, _T("Command line startup error, window size should be set by \"-w 480 582\" or two other positive integers. Window command ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
            return false;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    return true;
}

// return true if a command line directory was found and it's valid, false means not found or not valid.
// fill in saveWorldDirectory with user-specified directory
static bool getWorldSaveDirectoryFromCommandLine(wchar_t *saveWorldDirectory, const LPWSTR *argList, int argCount)
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
                wcscpy_s(saveWorldDirectory, MAX_PATH, argList[argIndex]);
                argIndex++;
                if (!PathFileExists(saveWorldDirectory)) {
                    LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine path does not exist\n");
                    MessageBox(NULL, _T("Warning:\nThe path you specified on the command line for your saved worlds location does not exist. The default directory will be used instead."),
                        _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                    return false;
                }
                else {
                    // found it - done
                    return true;
                }
            }
            // if we got here, parsing didn't work
            LOG_INFO(gExecutionLogfile, " getWorldSaveDirectoryFromCommandLine directory not given with -s option\n");
            MessageBox(NULL, _T("Command line startup error, directory is missing. Your save world directory should be set by \"-s directory\". Setting ignored."), _T("Command line startup error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
            return false;
        }
        else {
            // skip whatever it is.
            argIndex++;
        }
    }
    // we return false when we don't find a directory that was input on the command line
    return false;
}


static bool processCreateArguments(WindowSet & ws, const char **pBlockLabel, LPARAM holdlParam, const LPWSTR *argList, int argCount)
{
    // arguments possible:
    // -w x y
    // script name. Put double quotes around script name if it has spaces in the file name or directory path
    int argIndex = 1;
    while (argIndex < argCount)
    {
        if (wcscmp(argList[argIndex], L"-w") == 0) {
            // skip window resize
            LOG_INFO(gExecutionLogfile, " skip window resize\n");
            argIndex += 3;
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
        else if (*argList[argIndex] == '-') {
            // unknown argument, so list out arguments
            MessageBox(NULL, L"Warning:\nUnknown argument on command line.\nUsage: mineways.exe [-w X Y] [-s UserSaveDirectory] [-l mineways_exec.log] [file1.mwscript [file2.mwscript [...]]]", _T("Warning"), MB_OK | MB_ICONWARNING);
            // abort
            return true;
        }
        // is it a script?
        else {
            // Load a script; if it works fine, don't pop up a dialog
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
    setSlider(hWnd, hwndSlider, hwndLabel, gCurDepth, false);
    setSlider(hWnd, hwndBottomSlider, hwndBottomLabel, gTargetDepth, true);
    validateItems(GetMenu(hWnd));
    drawInvalidateUpdate(hWnd);
}


static void updateCursor( LPARAM lParam, BOOL hdragging)
{
    // change cursor to something special if highlighting an area and not currently creating that area
    int mx,my,mz,type,dataVal,biome;
    int on, minx, miny, minz, maxx, maxy, maxz;

    BOOL cursorSet = FALSE;
    if ( gHighlightOn && !hdragging )
    {
        //SetCapture(hWnd);

        // get mouse position in world space
        (void)IDBlock(LOWORD(lParam),HIWORD(lParam)-MAIN_WINDOW_TOP,gCurX,gCurZ,
            bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);

        // now to check the corners: is this location near any of them?
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );

        // see if we select on the selection border
        // highlighting is on, check the corners: inside bounds of current selection?
        if ( ( mx >= minx - SELECT_MARGIN/gCurScale ) && 
            ( mx <= maxx + SELECT_MARGIN/gCurScale ) &&
            ( mz >= minz - SELECT_MARGIN/gCurScale ) &&
            ( mz <= maxz + SELECT_MARGIN/gCurScale ) )
        {
            int xzone = 0;
            int zzone = 0;
            int innerx = (int)(SELECT_MARGIN/gCurScale);
            int innerz = (int)(SELECT_MARGIN/gCurScale);
            gAdjustingSelection = 1;

            if ( innerx > maxx-minx )
            {
                innerx = (maxx-minx-1)/2;
            }
            if ( innerz > maxz-minz )
            {
                innerz = (maxz-minz-1)/2;
            }

            if ( mx <= minx + innerx )
            {
                // in minx zone
                xzone = 1;
            }
            else if ( mx >= maxx - innerx )
            {
                // in maxx zone
                xzone = 2;
            }

            if ( mz <= minz + innerz )
            {
                // in minz zone
                zzone = 1;
            }
            else if ( mz >= maxz - innerz )
            {
                // in maxz zone
                zzone = 2;
            }

            if ( xzone != 0 || zzone != 0 )
            {
                // OK, cursor will be set to something special
                cursorSet = TRUE;
                switch ( xzone*3 + zzone )
                {
                case 1: // zzone min
                case 2: // zzone max
                    SetCursor( gNsCursor );
                    break;
                case 3: // xzone min
                case 6: // xzone max
                    SetCursor( gWeCursor );
                    break;
                case 4: // xzone min, zzone min
                case 8: // xzone max, zzone max
                    SetCursor( gNwseCursor );
                    break;
                case 5: // xzone min, zzone max
                case 7: // xzone max, zzone min
                    SetCursor( gNeswCursor );
                    break;
                default:
                    MY_ASSERT(gAlwaysFail);
                }
            }
        }
    }

    if ( !cursorSet )
    {
        // default point
        SetCursor( gArrowCursor );
    }
}

// Note: don't change worldType before calling this method - it sets worldType to overworld itself.
static void gotoSurface( HWND hWnd, HWND hwndSlider, HWND hwndLabel)
{
    if (gOptions.worldType&HELL)
    {
        // get out of hell zoom
        gCurX*=8.0;
        gCurZ*=8.0;
        // and undo other hell stuff
        if ( gCurDepth == 126 )
        {
            gCurDepth = MAP_MAX_HEIGHT;
            setSlider( hWnd, hwndSlider, hwndLabel, gCurDepth, false );
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
        //gCurScale = clamp(gCurScale,MINZOOM,MAXZOOM);
    }
    // Ender is easy, just turn it off 
    gOptions.worldType&=~ENDER;
    CheckMenuItem(GetMenu(hWnd),IDM_END,MF_UNCHECKED);
}

static void updateStatus(int mx, int mz, int my, const char *blockLabel, int type, int dataVal, int biome, HWND hwndStatus)
{
    wchar_t buf[150];
    char sbuftype[100];
    char sbufbiome[100];

    // always show all information - it's fine
    //if ( gOptions.worldType & SHOWALL )
        sprintf_s( sbuftype, 100, " (id %d:%d)", type, dataVal );
    //else
    //    sbuftype[0] = '\0';

    //if ( (gOptions.worldType & BIOMES) && (biome >= 0) )
    if ( biome >= 0 )
        sprintf_s( sbufbiome, 100, " - %s biome", gBiomes[biome].name );
    else
        sbufbiome[0] = '\0';


    // if my is out of bounds, print dashes
    if ( my < -1 || my >= MAP_MAX_HEIGHT+1 )
    {
        //wsprintf(buf,L"%S \t\tBottom %d",blockLabel,gTargetDepth);
        wsprintf(buf,L"%S",blockLabel);	// char to wchar
    }
    else
    {

        // In Nether, show corresponding overworld coordinates
        if ( gOptions.worldType&HELL)
            //wsprintf(buf,L"%d,%d; y=%d[%d,%d] %S \t\tBtm %d",mx,mz,my,mx*8,mz*8,blockLabel,gTargetDepth);
            wsprintf(buf,L"%d,%d; y=%d[%d,%d] %S%S%S",mx,mz,my,mx*8,mz*8,blockLabel,sbuftype,sbufbiome);	// char to wchar
        else
            //wsprintf(buf,L"%d,%d; y=%d %S \t\tBottom %d",mx,mz,my,blockLabel,gTargetDepth);
            wsprintf(buf,L"%d,%d; y=%d %S%S%S",mx,mz,my,blockLabel,sbuftype,sbufbiome);	// char to wchar
    }
    SendMessage(hwndStatus,SB_SETTEXT,0,(LPARAM)buf);
}


static int numCustom=1;
static void populateColorSchemes(HMENU menu)
{
    MENUITEMINFO info;
    for (int i=1;i<numCustom;i++)
        DeleteMenu(menu,IDM_CUSTOMCOLOR+i,MF_BYCOMMAND);
    info.cbSize=sizeof(MENUITEMINFO);
    info.fMask=MIIM_FTYPE|MIIM_ID|MIIM_STRING|MIIM_DATA;
    info.fType=MFT_STRING;
    numCustom=1;
    ColorManager cm;
    ColorScheme cs;
    int id=cm.next(0,&cs);
    while (id)
    {
        info.wID=IDM_CUSTOMCOLOR+numCustom;
        info.cch=(UINT)wcslen(cs.name);
        info.dwTypeData=cs.name;
        info.dwItemData=cs.id;
        InsertMenuItem(menu,IDM_CUSTOMCOLOR,FALSE,&info);
        numCustom++;
        id=cm.next(id,&cs);
    }
}
static void useCustomColor(int wmId,HWND hWnd)
{
    for (int i=0;i<numCustom;i++)
        CheckMenuItem(GetMenu(hWnd),IDM_CUSTOMCOLOR+i,MF_UNCHECKED);
    CheckMenuItem(GetMenu(hWnd),wmId,MF_CHECKED);
    ColorManager cm;
    ColorScheme cs;
    if (wmId>IDM_CUSTOMCOLOR)
    {
        MENUITEMINFO info;
        info.cbSize=sizeof(MENUITEMINFO);
        info.fMask=MIIM_DATA;
        GetMenuItemInfo(GetMenu(hWnd),wmId,FALSE,&info);
        cs.id=(int)info.dwItemData;
        cm.load(&cs);
    }
    else
    {
        ColorManager::Init(&cs);
        cs.id = 0;
        wcscpy_s(cs.name, 255, L"Standard");
    }
    SetMapPalette(cs.colors,256);
    drawInvalidateUpdate(hWnd);

    // store away for our own use
    wcscpy_s(gSchemeSelected, 255, cs.name);
}
static int findColorScheme(wchar_t* name)
{
    ColorManager cm;
    ColorScheme cs;
    int id=cm.next(0,&cs);
    int count = 1;
    // go through list in same order as created in populateColorSchemes
    while (id)
    {
        if ( wcscmp( name, cs.name) == 0 )
            return count;
        id=cm.next(id,&cs);
        count++;
    }
    return -1;
}

static void updateProgress(float progress)
{
    SendMessage(progressBar,PBM_SETPOS,(int)(progress*100),0);
}

static void draw()
{
    if (gLoaded)
        DrawMap(&gWorldGuide,gCurX,gCurZ,gCurDepth,bitWidth,bitHeight,gCurScale,map,gOptions,gHitsFound,updateProgress);
    else {
        // avoid clearing nothing at all.
        if (bitWidth > 0 && bitHeight > 0)
            memset(map, 0xff, bitWidth*bitHeight * 4);
        else
            return;	// nothing to draw
    }
    SendMessage(progressBar,PBM_SETPOS,0,0);
    for (int i=0;i<bitWidth*bitHeight*4;i+=4)
    {
        map[i]^=map[i+2];
        map[i+2]^=map[i];
        map[i]^=map[i+2];
    }
}

static int loadSchematic(wchar_t *pathAndFile)
{
    // Always read again, even if read before, and recenter.
    // Set spawn and player location to center of model.
    // Read whole schematic in, then 
    CloseAll();

#define CHECK_READ_SCHEMATIC_QUIT( b )			\
    if ( (b) != 1 ) {						\
        return 1;		\
    }

    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Width", &gWorldGuide.sch.width));
    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Height", &gWorldGuide.sch.height));
    CHECK_READ_SCHEMATIC_QUIT(GetSchematicWord(pathAndFile, "Length", &gWorldGuide.sch.length));

    gWorldGuide.sch.numBlocks = gWorldGuide.sch.width * gWorldGuide.sch.height * gWorldGuide.sch.length;
    if (gWorldGuide.sch.numBlocks <= 0)
        return 1;

    gWorldGuide.sch.blocks = (unsigned char *)malloc(gWorldGuide.sch.numBlocks);
    gWorldGuide.sch.data = (unsigned char *)malloc(gWorldGuide.sch.numBlocks);

    CHECK_READ_SCHEMATIC_QUIT(GetSchematicBlocksAndData(pathAndFile, gWorldGuide.sch.numBlocks, gWorldGuide.sch.blocks, gWorldGuide.sch.data));

    // All data's read in! Now we let the mapping system take over and load on demand.
    gSpawnX = gSpawnY = gSpawnZ = gPlayerX = gPlayerY = gPlayerZ = 0;
    gVersionId = 99999;	// always assumed to be the latest one.

    return 0;
}

// return 1 or 2 or higher if world could not be loaded
static int loadWorld(HWND hWnd)
{
    int version;
    CloseAll();

    // delete schematic data stored, if any, since we're loading a new world
    if (gWorldGuide.sch.blocks != NULL) {
        free(gWorldGuide.sch.blocks);
        gWorldGuide.sch.blocks = NULL;
    }
    if (gWorldGuide.sch.data != NULL) {
        free(gWorldGuide.sch.data);
        gWorldGuide.sch.data = NULL;
    }

    switch (gWorldGuide.type) {
    case WORLD_TEST_BLOCK_TYPE:
        // load test world
        MY_ASSERT(gWorldGuide.world[0] == 0);
        gSpawnX = gSpawnY = gSpawnZ = gPlayerX = gPlayerY = gPlayerZ = 0;
        gVersionId = 99999;	// always assumed to be the latest one.
        break;

    case WORLD_LEVEL_TYPE:
        // Don't necessarily clear selection! It's a feature: you can export, then go modify your Minecraft
        // world, then reload and carry on.
        //gHighlightOn=FALSE;
        //SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0);
        // Get the NBT file type, lowercase "version". Should be 19333 or higher to be Anvil. See http://minecraft.gamepedia.com/Level_format#level.dat_format
        if (GetFileVersion(gWorldGuide.world, &version) != 1) {
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 1;
        }
        if (version < 19133)
        {
            // world is really old, pre Anvil
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 2;
        }
        // it's not a good sign if we can't get the spawn location etc. from the world - consider this a failure to load
        if (GetSpawn(gWorldGuide.world, &gSpawnX, &gSpawnY, &gSpawnZ) != 1)
        {
            gWorldGuide.type = WORLD_UNLOADED_TYPE;
            return 3;
        }
        if (GetPlayer(gWorldGuide.world, &gPlayerX, &gPlayerY, &gPlayerZ) != 1) {
            // if this fails, it's a server world, so set the values equal to the spawn location
            // from http://minecraft.gamepedia.com/Level_format
            // Player: The state of the Singleplayer player.This overrides the <player>.dat file with the same name as the
            // Singleplayer player. This is only saved by Servers if it already exists, otherwise it is not saved for server worlds.See Player.dat Format.
            gPlayerX = gSpawnX;
            gPlayerY = gSpawnY;
            gPlayerZ = gSpawnZ;
        }
        // This may or may not work, so we ignore errors.
        GetFileVersionId(gWorldGuide.world, &gVersionId);
        break;

    case WORLD_SCHEMATIC_TYPE:
        loadSchematic(gWorldGuide.world);
        break;

    default:
        MY_ASSERT(gAlwaysFail);
    }

    // keep current state around, we use it to set new window title
    gHoldSameWorld = gSameWorld;

    // if this is the first world you loaded, or not the same world as before (reload), set location to spawn.
    if ( !gSameWorld )
    {
        gCurX=gSpawnX;
        gCurZ=gSpawnZ;
        gSameWorld=TRUE;   // so if we reload
        // zoom out when loading a new world, since location's reset.
        gCurScale=MINZOOM;

        gCurDepth = MAP_MAX_HEIGHT;
        // set lower level height to sea level, or to 0 if it's a schematic
        gTargetDepth = (gWorldGuide.type == WORLD_SCHEMATIC_TYPE ) ? 0x0 : MIN_OVERWORLD_DEPTH;
        gHighlightOn=FALSE;
        SetHighlightState(gHighlightOn,0,gTargetDepth,0,0,gCurDepth,0);
        // turn on error checking for this new world, to warn of unknown blocks
        CheckUnknownBlock( true );
        // just to be safe, make sure flag is cleared for read check.
        ClearBlockReadCheck();
        // because gSameWorld gets set to 1 by loadWorld()T
        if (!gHoldSameWorld)
        {
            wchar_t title[MAX_PATH];
            formTitle(&gWorldGuide, title);
            sprintf_s(gSkfbPData.skfbName, "%ws", stripWorldName(gWorldGuide.world));
            SetWindowTextW(hWnd, title);
        }
    }
    // now done by setUIOnLoadWorld, which should always be called right after loadWorld
    //gLoaded=TRUE;
    return 0;
}

//static void minecraftJarPath(TCHAR *path)
//{
//	SHGetFolderPath(NULL,CSIDL_APPDATA,NULL,0,path);
//	PathAppend(path,L".minecraft");
//	PathAppend(path,L"bin");
//}

static int setWorldPath(TCHAR *path)
{
    // try the normal Windows location
    SHGetFolderPath(NULL,CSIDL_APPDATA,NULL,0,path);
    PathAppend(path,L".minecraft");
    PathAppend(path,L"saves");

    if ( PathFileExists( path ) )
        return 1;

    // try Mac path
    TCHAR newPath[MAX_PATH];
    // Workaround for OSX minecraft worlds path
    // Get the username and check in user's Library directory
    wchar_t user[1024];
    DWORD username_len = 1025;
    GetUserName(user, &username_len);
    swprintf_s(path, MAX_PATH, L"Z:\\Users\\%s\\Library\\Application Support\\minecraft\\saves", user);

    if ( PathFileExists( path ) )
        return 1;

    // Back to the old method. Not sure if we should keep this method...
    wcscpy_s(path, MAX_PATH, L"./Library/Application Support/minecraft/saves");
    wchar_t msgString[1024];

    // keep on trying and trying...
    for ( int i = 0; i < 15; i++ )
    {
        if ( gDebug )
        {
            swprintf_s(msgString,1024,L"Try path %s", path );
            MessageBox( NULL, msgString, _T("Informational"), MB_OK|MB_ICONINFORMATION);
        }

        if ( PathFileExists( path ) )
        {
            // convert path to absolute path
            wcscpy_s(newPath, MAX_PATH, path);
            TCHAR *ret_code = _wfullpath( path, newPath, MAX_PATH );
            if ( gDebug )
            {
                swprintf_s(msgString,1024,L"Found! Ret code %s, Converted path to %s", (ret_code ? L"OK": L"NULL"), path );
                MessageBox( NULL, msgString, _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }
            return i+2;
        }

        wcscpy_s(newPath, MAX_PATH, L"./.");
        wcscat_s(newPath, MAX_PATH, path);
        wcscpy_s(path, MAX_PATH, newPath);
    }

    // failed
    if ( gDebug )
    {
        MessageBox(NULL, L"Failed to find path", _T("Informational"), MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
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
//		wcscpy_s(path,MAX_PATH,home);
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
//	//	wcscpy_s(path,MAX_PATH,L"~/Library/Application Support/minecraft/saves/*");
//	//}
//}

void flagUnreadableWorld(wchar_t *wcWorld, char *charWorld)
{
    char outputString[1024];
    sprintf_s(outputString, 1024, "      detected corrupt world file: %s\n", charWorld);
    LOG_INFO(gExecutionLogfile, outputString);

    wchar_t msgString[1024];
    swprintf_s(msgString, 1024, L"Warning: The level.dat of world file %s appears to be missing important information; it might be corrupt. World ignored.", wcWorld);
    MessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
}

static int loadWorldList(HMENU menu)
{
    int oldVersionDetected = 0;
    MENUITEMINFO info;
    info.cbSize=sizeof(MENUITEMINFO);
    info.fMask=MIIM_FTYPE|MIIM_ID|MIIM_STRING|MIIM_DATA;
    info.fType=MFT_STRING;
    TCHAR saveFilesPath[MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_DATA ffd;

    LOG_INFO(gExecutionLogfile, "   entered loadWorldList\n");
    // first world is actually the block test world
    gNumWorlds = 1;
    memset(gWorlds, 0x0, 1000*sizeof(TCHAR *));

    // uncomment next line to pop up dialogs about progress - useful on Mac to see what directories it's searching through, etc.
    //gDebug = true;

    wcscpy_s(saveFilesPath,MAX_PATH,gWorldPathDefault);

    wchar_t msgString[1024];
    wcscat_s(saveFilesPath,MAX_PATH,L"/*");
    char outputString[1024];
    char pConverted[1024];
    int length = (int)wcslen(saveFilesPath) + 1;
    WcharToChar(saveFilesPath, pConverted, length);
    sprintf_s(outputString, 1024, "    saveFilesPath %s\n", pConverted);
    LOG_INFO(gExecutionLogfile, outputString);
    hFind = FindFirstFile(saveFilesPath, &ffd);

    // did we find the directory at all?
    if ( hFind == INVALID_HANDLE_VALUE )
    {
        LOG_INFO(gExecutionLogfile, "    invalid handle value - return 0\n");
        MessageBox(NULL, _T("Mineways couldn't find your Minecraft world saves directory. You'll need to guide Mineways to where you save your worlds. Use the 'File -> Open...' option and find your level.dat file for the world. If you're on Windows, go to 'C:\\Users\\Eric\\AppData\\Roaming\\.minecraft\\saves' and find it in your world save directory. For Mac, worlds are usually located at /users/<your name>/Library/Application Support/minecraft/saves. Visit http://mineways.com or email me if you are still stuck."),
            _T("Informational"), MB_OK|MB_ICONINFORMATION);
        return 0;
    }

    // Avoid infinite loop when searching directory. This shouldn't happen, but let us be absolutely sure.
    int count = 0;
    do
    {
        // Yes, we could really count the number of actual worlds found, but just in case this is a crazy-large directory
        // cut searching short at 1000 files of any sort.
        count++;
        if (ffd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        {
            length = (int)wcslen(ffd.cFileName) + 1;
            WcharToChar(ffd.cFileName, pConverted, length);
            sprintf_s(outputString, 1024, "    found potential world save directory %s\n", pConverted);
            LOG_INFO(gExecutionLogfile, outputString);
            
            if (gDebug)
            {
                swprintf_s(msgString,1024,L"Found potential world save directory %s", ffd.cFileName );
                MessageBox( NULL, msgString, _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }

            if (ffd.cFileName[0]!='.')
            {
                // test if world is in Anvil format
                int version;
                TCHAR testAnvil[MAX_PATH];
                char levelName[MAX_PATH];
                wcscpy_s(testAnvil, MAX_PATH, gWorldPathDefault);
                wcscat_s(testAnvil, MAX_PATH, gPreferredSeparatorString);
                wcscat_s(testAnvil, MAX_PATH, ffd.cFileName);

                length = (int)wcslen(testAnvil) + 1;
                WcharToChar(testAnvil, pConverted, length);
                sprintf_s(outputString, 1024, "      trying file %s\n", pConverted);
                LOG_INFO(gExecutionLogfile, outputString);

                if (gDebug)
                {
                    swprintf_s(msgString,1024,L"Trying file %s", testAnvil );
                    MessageBox( NULL, msgString, _T("Informational"), MB_OK|MB_ICONINFORMATION);
                }

                LOG_INFO(gExecutionLogfile, "        try to get file version\n");
                if (GetFileVersion(testAnvil, &version) != 1) {
                    // unreadable world, for some reason - couldn't read version and LevelName
                    if (GetFileVersion(testAnvil, &version) != -1)
                        // 0 means level.dat exists, but data could not be found
                        flagUnreadableWorld(testAnvil, pConverted);
                    continue;
                }
                LOG_INFO(gExecutionLogfile, "        try to get file level name\n");
                if (GetLevelName(testAnvil, levelName, MAX_PATH) != 1) {
                    // unreadable world, for some reason - couldn't read version and LevelName
                    if (GetLevelName(testAnvil, levelName, MAX_PATH) != -1)
                        // 0 means level.dat exists, but data could not be found
                        flagUnreadableWorld(testAnvil, pConverted);
                    continue;
                }

                int versionId = 0;
                char versionName[MAX_PATH];
                versionName[0] = (char)0;
                LOG_INFO(gExecutionLogfile, "        try to get file version id\n" );
                // This is a newer tag for 1.9 and on, older worlds do not have them
                if (GetFileVersionId(testAnvil, &versionId) != 1) {
                    // older file type, does not have it.
                    LOG_INFO(gExecutionLogfile, "          pre-1.9 file type detected, so no version id, which is fine\n");
                }
                LOG_INFO(gExecutionLogfile, "        try to get file version name\n");
                if (GetFileVersionName(testAnvil, versionName, MAX_PATH) != 1) {
                    // older file type, does not have it.
                    LOG_INFO(gExecutionLogfile, "          pre-1.9 file type detected, so no version name, which is fine\n");
                }

                sprintf_s(outputString, 1024, "      succeeded, which has version ID %d and version name %s, and folder level name %s\n", versionId, versionName, levelName);
                LOG_INFO(gExecutionLogfile, outputString);

                if (gDebug)
                {
                    // 0 version ID means earlier than 1.9
                    swprintf_s(msgString, 1024, L"Succeeded with file %s, which has version ID %d and version name %S, and folder level name %S", testAnvil, versionId, versionName, levelName);
                    MessageBox( NULL, msgString, _T("Informational"), MB_OK|MB_ICONINFORMATION);
                }

                info.wID=IDM_WORLD+gNumWorlds;

                // display the "given name" followed by / the world folder name; both can be useful
                TCHAR worldIDString[MAX_PATH], wLevelName[MAX_PATH];
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,levelName,-1,wLevelName,MAX_PATH);
                wsprintf(worldIDString,L"%s\t/ %s", wLevelName, ffd.cFileName);

                info.cch=(UINT)wcslen(worldIDString);
                info.dwTypeData=worldIDString;
                info.dwItemData=gNumWorlds;
                // if version is pre-Anvil, show world but gray it out
                if (version < 19133)
                {
                    LOG_INFO(gExecutionLogfile, "   file is pre-Anvil\n");
                    oldVersionDetected = 1;
                    // gray it out
                    info.fMask |= MIIM_STATE;
                    info.fState = MFS_DISABLED;
                }
                else
                {
                    //info.fMask |= MIIM_STATE;
                    info.fState = 0x0; // MFS_CHECKED;
                }
                InsertMenuItem(menu,IDM_TEST_WORLD,FALSE,&info);
                gWorlds[gNumWorlds]=(TCHAR*)malloc(sizeof(TCHAR)*MAX_PATH);
                wcscpy_s(gWorlds[gNumWorlds], MAX_PATH, testAnvil);
                // was: setWorldPath(worlds[numWorlds]);
                //PathAppend(worlds[numWorlds],ffd.cFileName);
                gNumWorlds++;
                sprintf_s(outputString, 1024, "    gNumWorlds is now %d\n", gNumWorlds);
                LOG_INFO(gExecutionLogfile, outputString);
            }
        }
    } while ((FindNextFile(hFind, &ffd) != 0) && (count < MAX_WORLDS) && (gNumWorlds < MAX_WORLDS));

    if (count >= MAX_WORLDS)
    {
        LOG_INFO(gExecutionLogfile, "Warning: more than 1000 files detected in save directory!\n");
        swprintf_s(msgString, 1024, L"Warning: more that 1000 files detected in %s. Not all worlds may have been read.", saveFilesPath);
        MessageBox(NULL, msgString, _T("Warning"), MB_OK | MB_ICONWARNING);
    }

    return oldVersionDetected;
}

static void enableBottomControl( int state, HWND hwndBottomSlider, HWND hwndBottomLabel, HWND hwndInfoBottomLabel )
{
    if ( state != gBottomControlEnabled )
    {
        gBottomControlEnabled = state;
        // Currently only the label "Lower depth" is affected by selection;
        // in this way the depth can be modified even if selection is not on. The problem with an inactive
        // slider is that the map window underneath is then active instead, and you'll drag the map.
        //EnableWindow(hwndBottomSlider,state);
        //EnableWindow(hwndBottomLabel,state);
        hwndBottomSlider;
        hwndBottomLabel;
        EnableWindow(hwndInfoBottomLabel,state);
    }
}


// validate menu items
static void validateItems(HMENU menu)
{
    // gray out options that are not available
    if (gLoaded)
    {
        EnableMenuItem(menu,IDM_JUMPSPAWN,MF_ENABLED);
        EnableMenuItem(menu,IDM_JUMPPLAYER,MF_ENABLED);
        // more correct is to gray if nothing is selected, but people don't know how to ungray
        //EnableMenuItem(menu,IDM_VIEW_JUMPTOMODEL,gHighlightOn?MF_ENABLED:MF_DISABLED);
        //EnableMenuItem(menu,IDM_FILE_SAVEOBJ,gHighlightOn?MF_ENABLED:MF_DISABLED);
        //EnableMenuItem(menu,IDM_FILE_PRINTOBJ,gHighlightOn?MF_ENABLED:MF_DISABLED);
        EnableMenuItem(menu,IDM_VIEW_JUMPTOMODEL,MF_ENABLED);
        //EnableMenuItem(menu,ID_FILE_IMPORTSETTINGS,MF_ENABLED);
        EnableMenuItem(menu,IDM_FILE_SAVEOBJ,MF_ENABLED);
        EnableMenuItem(menu,IDM_FILE_PRINTOBJ,MF_ENABLED);
        EnableMenuItem(menu,IDM_FILE_SCHEMATIC,MF_ENABLED);
        EnableMenuItem(menu,IDM_PUBLISH_SKFB,MF_ENABLED);
    }
    else
    {
        EnableMenuItem(menu,IDM_JUMPSPAWN,MF_DISABLED);
        EnableMenuItem(menu,IDM_JUMPPLAYER,MF_DISABLED);
        //EnableMenuItem(menu,ID_FILE_IMPORTSETTINGS,MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_SAVEOBJ,MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_PRINTOBJ,MF_DISABLED);
        EnableMenuItem(menu,IDM_FILE_SCHEMATIC,MF_DISABLED);
        EnableMenuItem(menu,IDM_VIEW_JUMPTOMODEL,MF_DISABLED);
        EnableMenuItem(menu,IDM_PUBLISH_SKFB,MF_DISABLED);
    }
    // has a save been done?
    if (gExported)
    {
        EnableMenuItem(menu,IDM_FILE_REPEATPREVIOUSEXPORT,MF_ENABLED);
    }
    else
    {
        EnableMenuItem(menu,IDM_FILE_REPEATPREVIOUSEXPORT,MF_DISABLED);
    }
}

static void setSlider( HWND hWnd, HWND hwndSlider, HWND hwndLabel, int depth, bool update )
{
    syncCurrentHighlightDepth();

    wchar_t text[4];
    SendMessage(hwndSlider,TBM_SETPOS,1,MAP_MAX_HEIGHT-depth);
    _itow_s(depth,text,10);
    SetWindowText(hwndLabel,text);
    if ( update )
    {
        drawInvalidateUpdate(hWnd);
    }
}

static void drawInvalidateUpdate(HWND hWnd)
{
    draw();
    InvalidateRect(hWnd, NULL, FALSE);
    UpdateWindow(hWnd);
}


static void syncCurrentHighlightDepth()
{
    // changing the depth 
    int on, minx, miny, minz, maxx, maxy, maxz;
    GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz );
    // wherever the target depth is, put it back in that place and replace the other
    if ( maxy == gTargetDepth )
    {
        // the rare case, where target depth is larger than current depth
        SetHighlightState(on, minx, gCurDepth, minz, maxx, gTargetDepth, maxz);
    }
    else
    {
        SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz);
    }
}

// So that you can change some options in Sculpteo export and they get used for
// Shapeways export, and vice versa. Also works for STL. A few parameters are
// specific to the service, e.g. Z is up, and unit type used, so these are not copied.
static void copyOverExportPrintData(ExportFileData *pEFD)
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

    switch ( source )
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
    for ( int i = 0; i < count; i++ )
    {
        if ( copyMtl[i] )
        {
            pEFD->radioExportNoMaterials[dest[i]] = pEFD->radioExportNoMaterials[source];
            pEFD->radioExportMtlColors[dest[i]] = pEFD->radioExportMtlColors[source];
            pEFD->radioExportSolidTexture[dest[i]] = pEFD->radioExportSolidTexture[source];
            pEFD->radioExportFullTexture[dest[i]] = pEFD->radioExportFullTexture[source];
        }

        // don't adjust Z up if (Sculpteo vs. Shapeways) specific, i.e. if service is true
        if ( service == 0 )
            pEFD->chkMakeZUp[dest[i]] = pEFD->chkMakeZUp[source];

        pEFD->blockSizeVal[dest[i]] = pEFD->blockSizeVal[source];

        // don't do, as this seems file type specific: chkCreateZip[FILE_TYPE_TOTAL];
        // don't do, as this seems file type specific: chkCreateModelFiles[FILE_TYPE_TOTAL];	// i.e. don't delete them at end

        pEFD->hollowThicknessVal[dest[i]] = pEFD->hollowThicknessVal[source];

        // don't do, as Sculpteo sandstone stats are different than Shapeways':
        if ( service == 0 )
            pEFD->comboPhysicalMaterial[dest[i]] = pEFD->comboPhysicalMaterial[source];
        // for service, don't do, as Sculpteo and Shapeways use centimeters vs. millimeters
        if ( service == 0 )
            pEFD->comboModelUnits[dest[i]] = pEFD->comboModelUnits[source];
    }
}

static int publishToSketchfab(HWND hWnd, wchar_t *objFileName, wchar_t *terrainFileName, wchar_t *schemeSelected)
{
    int on;
    int retCode = 0;

    PublishSkfbData* skfbPData = &gSkfbPData;
    // set 'export for rendering' settings
    gpEFD = &gExportViewData;
    gOptions.exportFlags = 0x0;
    gpEFD->flags = 0x0;

    gOptions.pEFD = gpEFD;

    // get selected zone bounds
    GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal );

    int miny = gpEFD->minyVal;
    int maxy = gpEFD->maxyVal;

    // set epd in skfbPDdata data
    setPublishSkfbData(skfbPData);

    // Open dialog and get user data
    if ( !doPublishSkfb(hInst,hWnd) )
    {
        return 0;
    }
    // Get updated version of export data (api token, etc)
    getPublishSkfbData(skfbPData);

    // if user changed depths
    if ( miny != gpEFD->minyVal || maxy != gpEFD->maxyVal )
    {
        // see if target did not change
        if ( gTargetDepth <= gCurDepth )
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
    SetHighlightState(on, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );

    // export all ellements for Skfb
    gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |
        BLF_FLATSIDE | BLF_3D_BIT;

    // Set options for Sketchfab publication. Need to determine best settings here, the user will not have the choice
    gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_SKFB;

    gOptions.exportFlags |=
        (gpEFD->chkHollow[gpEFD->fileType] ? EXPT_HOLLOW_BOTTOM : 0x0) |
        ((gpEFD->chkHollow[gpEFD->fileType] && gpEFD->chkSuperHollow[gpEFD->fileType]) ? EXPT_HOLLOW_BOTTOM|EXPT_SUPER_HOLLOW_BOTTOM : 0x0);

    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_GROUPS; // export groups
    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MULTIPLE_MTLS; // the norm, instead of single material
    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_FULL_MATERIAL; // Full material (output the extra values)
    gOptions.exportFlags |= EXPT_OUTPUT_TEXTURE_IMAGES; // Export block full texture
    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_REL_COORDINATES; // OBj relative coordinates
    gOptions.exportFlags |= EXPT_BIOME; // Use biome for export. Currently only the biome at the center of the zone is supported

    // Generate files
    FileList outputFileList;
    outputFileList.count = 0;
    if ( on ) {

        drawInvalidateUpdate(hWnd);
        gpEFD->radioScaleToHeight = 1;
        gpEFD->radioScaleByCost = 0;
        gpEFD->chkCreateModelFiles[gpEFD->fileType] = 0;
        // if skfbFilePath is empty, that means that it has not been created yet or selection has changed
        if (skfbPData->skfbFilePath.empty()){
            int errCode = SaveVolume(objFileName, gpEFD->fileType, &gOptions, &gWorldGuide, gCurrentDirectory,
                gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal,
                updateProgress, terrainFileName, schemeSelected, &outputFileList, (int)gMajorVersion, (int)gMinorVersion, gVersionId, gChangeBlockCommands);
            deleteCommandBlockSet(gChangeBlockCommands);
            gChangeBlockCommands = NULL;

            wchar_t wcZip[MAX_PATH];

            swprintf_s(wcZip, MAX_PATH, L"%s.zip", outputFileList.name[0]);
            DeleteFile(wcZip);

            HZIP hz = CreateZip(wcZip,0,ZIP_FILENAME);
            for ( int i = 0; i < outputFileList.count; i++ )
            {
                const wchar_t *nameOnly = removePath( outputFileList.name[i] ) ;

                if (*updateProgress)
                { (*updateProgress)(0.90f + 0.10f*(float)i/(float)outputFileList.count);}

                ZipAdd(hz,nameOnly, outputFileList.name[i], 0, ZIP_FILENAME);

                // delete model files if not needed
                if ( !gpEFD->chkCreateModelFiles[gpEFD->fileType] )
                {
                    DeleteFile(outputFileList.name[i]);
                }
            }
            CloseZip(hz);

            if (*updateProgress)
                (*updateProgress)(1.0f);

            if ( errCode != MW_NO_ERROR )
            {
                PopupErrorDialogs( errCode );
            }

            // Set filepath to skfb data
            std::wstring file(wcZip);
            std::string filepath(file.begin(), file.end());
            skfbPData->skfbFilePath = filepath;
            setPublishSkfbData(skfbPData);
        }

        uploadToSketchfab(hInst, hWnd);
        if (*updateProgress)
            (*updateProgress)(0.0f);
    }

    return retCode;
}

static void addChangeBlockCommandsToGlobalList(ImportedSet & is)
{
    // if we imported block commands, add them to existing list, if any
    if (is.pCBChead != NULL) {
        if (gChangeBlockCommands != NULL) {
            // need to find end of list and add to it.
            ChangeBlockCommand *pCBC = gChangeBlockCommands;
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

// returns number of files written on successful export, 0 files otherwise.
// showDialog says whether to show the export options dialog or not.
static int saveObjFile(HWND hWnd, wchar_t *objFileName, int printModel, wchar_t *terrainFileName, wchar_t *schemeSelected, bool showDialog, bool showStatistics)
{
    int on;
    int retCode = 0;

    if ( printModel == 2 )
    {
        // schematic file - treat sort of like a render
        gpEFD = &gExportSchematicData;
        gOptions.exportFlags = 0x0;
        gpEFD->flags = 0x0;
    }
    else if ( printModel == 1 )
    {
        // print
        gpEFD = &gExportPrintData;
        gOptions.exportFlags = EXPT_3DPRINT;
        gpEFD->flags = EXPT_3DPRINT;
    }
    else
    {
        // render
        gpEFD = &gExportViewData;
        gOptions.exportFlags = 0x0;
        gpEFD->flags = 0x0;
    }
    gOptions.pEFD = gpEFD;

    // to use a preset set of values above, set this true, or break here and jump to line after "if"
    static int gDebugSetBlock=0;
    if ( gDebugSetBlock )
        SetHighlightState( 1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );

    // normal output
    GetHighlightState(&on, &gpEFD->minxVal, &gpEFD->minyVal, &gpEFD->minzVal, &gpEFD->maxxVal, &gpEFD->maxyVal, &gpEFD->maxzVal );

    int miny = gpEFD->minyVal;
    int maxy = gpEFD->maxyVal;

    setExportPrintData(gpEFD);

    if ( showDialog && !doExportPrint(hInst,hWnd) )
    {
        // canceled, so cancel output
        return 0;
    }

    getExportPrintData(gpEFD);

    copyOverExportPrintData(gpEFD);

    // if user changed depths
    if ( miny != gpEFD->minyVal || maxy != gpEFD->maxyVal )
    {
        // see if target did not change
        if ( gTargetDepth <= gCurDepth )
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
    SetHighlightState(on, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal );

    // export all
    if ( gpEFD->chkExportAll )
    {
        if ( printModel == 1 )
        {
            // 3d printing
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |
                BLF_FLATSIDE | BLF_3D_BIT;
        }
        else
        {
            // rendering or schematic
            gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |
                BLF_FLATSIDE | BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD;
        }
    }
    else
    {
        gOptions.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP | BLF_FLATSIDE;
    }

    // export options
    if ( gpEFD->radioExportMtlColors[gpEFD->fileType] == 1 )
    {
        // if color output is specified, we *must* put out multiple objects, each with its own material
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_GROUPS | EXPT_OUTPUT_OBJ_MULTIPLE_MTLS | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ( gpEFD->radioExportSolidTexture[gpEFD->fileType] == 1 )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ( gpEFD->radioExportFullTexture[gpEFD->fileType] == 1 )
    {
        gOptions.exportFlags |= EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
        // TODO: if we're *viewing* full textures, output all the billboards!
        //  gOptions.saveFilterFlags |= BLF_SMALL_BILLBOARD;
    }

    gOptions.exportFlags |=
        (gpEFD->chkFillBubbles ? EXPT_FILL_BUBBLES : 0x0) |
        ((gpEFD->chkFillBubbles&&gpEFD->chkSealEntrances) ? EXPT_FILL_BUBBLES|EXPT_SEAL_ENTRANCES : 0x0) |
        ((gpEFD->chkFillBubbles&&gpEFD->chkSealSideTunnels) ? EXPT_FILL_BUBBLES|EXPT_SEAL_SIDE_TUNNELS : 0x0) |

        (gpEFD->chkConnectParts ? EXPT_CONNECT_PARTS : 0x0) |
        // is it better to force part connection on if corner tips or edges are on? I feel like the
        // dialog should take care of this, not allowing these options if the controlling algorithm is set.
        (gpEFD->chkConnectCornerTips ? (EXPT_CONNECT_PARTS|EXPT_CONNECT_CORNER_TIPS) : 0x0) |
        (gpEFD->chkConnectAllEdges ? (EXPT_CONNECT_PARTS|EXPT_CONNECT_ALL_EDGES) : 0x0) |
        (gpEFD->chkDeleteFloaters ? EXPT_DELETE_FLOATING_OBJECTS : 0x0) |

        (gpEFD->chkHollow[gpEFD->fileType] ? EXPT_HOLLOW_BOTTOM : 0x0) |
        ((gpEFD->chkHollow[gpEFD->fileType] && gpEFD->chkSuperHollow[gpEFD->fileType]) ? EXPT_HOLLOW_BOTTOM|EXPT_SUPER_HOLLOW_BOTTOM : 0x0) |

        // materials are forced on if using debugging mode - just an internal override, doesn't need to happen in dialog.
        (gpEFD->chkShowParts ? EXPT_DEBUG_SHOW_GROUPS|EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_OBJ_GROUPS|EXPT_OUTPUT_OBJ_MULTIPLE_MTLS : 0x0) |
        (gpEFD->chkShowWelds ? EXPT_DEBUG_SHOW_WELDS|EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_OBJ_GROUPS|EXPT_OUTPUT_OBJ_MULTIPLE_MTLS : 0x0);


    // set OBJ group and material output state
    if ( gpEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || gpEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
    {
        if (gpEFD->chkMultipleObjects)
        {
            MY_ASSERT(gpEFD->chkIndividualBlocks == 0);
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_GROUPS;

            if (gpEFD->chkMaterialPerType)
            {
                gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MULTIPLE_MTLS;
            }
        }
        else if (gpEFD->chkIndividualBlocks)
        {
            // these must be on for individual block export, plus grouping by block
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_GROUPS | EXPT_OUTPUT_OBJ_MULTIPLE_MTLS | EXPT_GROUP_BY_BLOCK;
            if (gpEFD->chkMaterialPerType == 1)
            {
                gOptions.exportFlags |= EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
            }
        }

        if (gpEFD->chkMaterialSubtypes)
        {
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_MATERIAL_SUBTYPES;
        }

        if (gpEFD->chkG3DMaterial)
        {
            // if G3D is chosen, we output the full material
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_FULL_MATERIAL;
            //if (gOptions.exportFlags & (EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_TEXTURE_SWATCHES))
            //{
            //    // G3D - use this option only if textures are on.
            //    gOptions.exportFlags |= EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL;
            //}
        }
        // if in debugging mode, force groups and material type

        // check if we're exporting relative coordinates
        if ( gpEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
        {
            gOptions.exportFlags |= EXPT_OUTPUT_OBJ_REL_COORDINATES;
        }
    }
    // STL files never need grouping by material, and certainly don't export textures
    else if ( gpEFD->fileType == FILE_TYPE_ASCII_STL )
    {
        int unsupportedCodes = (EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE|
            EXPT_DEBUG_SHOW_GROUPS|EXPT_DEBUG_SHOW_WELDS);
        if ( gOptions.exportFlags & unsupportedCodes )
        {
            MessageBox( NULL, _T("Note: color output is not supported for ASCII text STL.\nFile will contain no colors."),
                _T("Informational"), MB_OK|MB_ICONINFORMATION);
        }
        // ASCII STL in particular cannot export any materials at all.
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ( ( gpEFD->fileType == FILE_TYPE_BINARY_MAGICS_STL ) || ( gpEFD->fileType == FILE_TYPE_BINARY_VISCAM_STL ) )
    {
        int unsupportedCodes = (EXPT_OUTPUT_TEXTURE_SWATCHES | EXPT_OUTPUT_TEXTURE_IMAGES);
        if ( gOptions.exportFlags & unsupportedCodes )
        {
            if ( gpEFD->fileType == FILE_TYPE_BINARY_VISCAM_STL )
            {
                MessageBox( NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain VisCAM colors instead."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }
            else
            {
                MessageBox( NULL, _T("Note: texture output is not supported for binary STL.\nFile will contain Materialise Magics colors instead."),
                    _T("Informational"), MB_OK|MB_ICONINFORMATION);
            }
        }
        gOptions.exportFlags &= ~unsupportedCodes;

        // we never have to group by material for STL, as there are no material groups.
        gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    }
    else if ( gpEFD->fileType == FILE_TYPE_VRML2 )
    {
        // are we outputting color textures?
        if ( gOptions.exportFlags & EXPT_OUTPUT_TEXTURE )
        {
            if ( gOptions.exportFlags & EXPT_3DPRINT)
            {
                // if printing, we don't need to group by material, as it can be one huge pile of data
                gOptions.exportFlags &= ~EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
            }
            // else if we're outputting for rendering, VRML then outputs grouped by material, unless it's a single material output
            // (in which case this flag isn't turned on anyway).
        }
    }
    else if ( gpEFD->fileType == FILE_TYPE_SCHEMATIC )
    {
        // really, ignore all options for Schematic - set how you want, but they'll all be ignored except rotation around the Y axis.
        gOptions.exportFlags &= 0x0;
    }
    else
    {
        // unknown file type?
        MY_ASSERT(gAlwaysFail);
    }

    if ( gpEFD->chkBiome )
    {
        gOptions.exportFlags |= EXPT_BIOME;
    }

    // if showing debug groups, we need to turn off full image texturing so we get the largest group as semitransparent
    // (and full textures would just be confusing for debug, anyway)
    if ( gOptions.exportFlags & EXPT_DEBUG_SHOW_GROUPS )
    {
        if ( gOptions.exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
        {
            gOptions.exportFlags &= ~EXPT_OUTPUT_TEXTURE_IMAGES;
            gOptions.exportFlags |= EXPT_OUTPUT_TEXTURE_SWATCHES;
        }
        // we don't want to group by block for debugging
        gOptions.exportFlags &= ~EXPT_GROUP_BY_BLOCK;
    }

    // OK, all set, let's go!
    FileList outputFileList;
    outputFileList.count = 0;
    if ( on ) {
        // redraw, in case the bounds were changed
        drawInvalidateUpdate(hWnd);

        int errCode = SaveVolume(objFileName, gpEFD->fileType, &gOptions, &gWorldGuide, gCurrentDirectory,
            gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal,
            updateProgress, terrainFileName, schemeSelected, &outputFileList, (int)gMajorVersion, (int)gMinorVersion, gVersionId, gChangeBlockCommands);
        deleteCommandBlockSet(gChangeBlockCommands);
        gChangeBlockCommands = NULL;

        // note how many files were output
        retCode = outputFileList.count;

        // zip it up - test that there's something to zip, in case of errors. Note that the first
        // file saved in ObjManip.c is the one used as the zip file's name.
        if ( gpEFD->chkCreateZip[gpEFD->fileType] && (outputFileList.count > 0) )
        {
            wchar_t wcZip[MAX_PATH];
            // we add .zip not (just) out of laziness, but this helps differentiate obj from wrl from stl.
            swprintf_s(wcZip,MAX_PATH,L"%s.zip",outputFileList.name[0]);

            DeleteFile(wcZip);
            HZIP hz = CreateZip(wcZip,0,ZIP_FILENAME);
            for ( int i = 0; i < outputFileList.count; i++ )
            {
                const wchar_t *nameOnly = removePath( outputFileList.name[i] ) ;

                if (*updateProgress)
                { (*updateProgress)(0.90f + 0.10f*(float)i/(float)outputFileList.count);}

                ZipAdd(hz,nameOnly, outputFileList.name[i], 0, ZIP_FILENAME);

                // delete model files if not needed
                if ( !gpEFD->chkCreateModelFiles[gpEFD->fileType] )
                {
                    DeleteFile(outputFileList.name[i]);
                }
            }
            CloseZip(hz);
        }
        if (*updateProgress)
        { (*updateProgress)(1.0f);}


        // output stats, if printing and there *are* stats
        if (showStatistics && (printModel == 1) && gOptions.cost > 0.0f && outputFileList.count > 0 &&
            // is not schematic?
            (gOptions.pEFD->fileType != FILE_TYPE_SCHEMATIC))
        {
            wchar_t *currency = gMtlCostTable[gOptions.pEFD->fileType].currency;
            if (gCustomCurrency != NULL &&
                gOptions.pEFD->comboPhysicalMaterial[gOptions.pEFD->fileType] == PRINT_MATERIAL_CUSTOM_MATERIAL )
            {
                currency = gCustomCurrency;
            }
            int retval;
            wchar_t msgString[2000];
            swprintf_s(msgString,2000,L"3D Print Statistics:\n\nApproximate cost is %s %0.2f\nBase is %d x %d blocks, %d blocks high\nEach block is %0.1f mm high (%0.2f inches high)\nInches: base is %0.1f x %0.1f inches, %0.1f inches high\nCentimeters: Base is %0.1f x %0.1f cm, %0.1f cm high\nTotal number of blocks: %d\nTotal cubic centimeters: %0.1f\n\nDo you want to have statistics continue to be\ndisplayed on each export for this session?",
                currency,
                gOptions.cost,
                gOptions.dimensions[0], gOptions.dimensions[2], gOptions.dimensions[1],
                gOptions.block_mm, gOptions.block_inch,
                gOptions.dim_inches[0], gOptions.dim_inches[2], gOptions.dim_inches[1], 
                gOptions.dim_cm[0], gOptions.dim_cm[2], gOptions.dim_cm[1], 
                gOptions.totalBlocks, gOptions.totalBlocks*gOptions.block_mm*gOptions.block_mm*gOptions.block_mm/1000.0f);
            retval = MessageBox( NULL, msgString,
                _T("Informational"), MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1 | MB_SYSTEMMODAL);
            if ( retval != IDYES )
            {
                gShowPrintStats = false;
            }
        }

        if ( errCode != MW_NO_ERROR )
        {
            PopupErrorDialogs( errCode );
        }
        // clear progress bar
        if (*updateProgress)
            (*updateProgress)(0.0f);
    }

    return retCode;
}

static void PopupErrorDialogs( int errCode )
{
    // pop up all errors flagged
    for ( int errNo = MW_NUM_CODES-1; errNo >= 0; errNo-- )
    {
        if ( (1<<errNo) & errCode )
        {
            // check if it's a PNG error
            if ( (1<<errNo) >= MW_BEGIN_PNG_ERRORS )
            {
                // PNG errors have extra information, i.e., what the PNG error string is.
                TCHAR errString[1024],wcString[1024];
                int pngError = (errCode>>MW_NUM_CODES);
                size_t newsize = strlen(lodepng_error_text(pngError))+1;
                size_t convertedChars = 0;
                mbstowcs_s(&convertedChars, wcString, newsize, lodepng_error_text(pngError), _TRUNCATE);
                wsprintf( errString, gPopupInfo[errNo+1].text, wcString );
                MessageBox(
                    NULL,
                    errString,
                    gPopupInfo[errNo+1].caption,
                    gPopupInfo[errNo+1].type
                    );
            }
            else
            {
                //int msgboxID = 
                MessageBox(
                    NULL,
                    gPopupInfo[errNo+1].text,
                    gPopupInfo[errNo+1].caption,
                    gPopupInfo[errNo+1].type
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
static const wchar_t *removePath( const wchar_t *src )
{
    // find last \ in string
    const wchar_t *strPtr = wcsrchr(src, (wchar_t)'\\');
    if ( strPtr )
        // found a \, so move up past it
        strPtr++;
    else
    {
        // look for /
        strPtr = wcsrchr(src,(wchar_t)'/');
        if ( strPtr )
            // found a /, so move up past it
            strPtr++;
        else
            // no \ or / found, just return string itself
            return src;
    }

    return strPtr;
}

#define INIT_ALL_FILE_TYPES( a, v0,v1,v2,v3,v4,v5,v6)    \
    (a)[FILE_TYPE_WAVEFRONT_REL_OBJ] = (v0);    \
    (a)[FILE_TYPE_WAVEFRONT_ABS_OBJ] = (v1);    \
    (a)[FILE_TYPE_BINARY_MAGICS_STL] = (v2);    \
    (a)[FILE_TYPE_BINARY_VISCAM_STL] = (v3);    \
    (a)[FILE_TYPE_ASCII_STL] = (v4);    \
    (a)[FILE_TYPE_VRML2] = (v5);	\
    (a)[FILE_TYPE_SCHEMATIC] = (v6);

static void initializeExportDialogData()
{
    initializePrintExportData(gExportPrintData);
    initializeViewExportData(gExportViewData);
    InitializeSchematicExportData(gExportSchematicData);
}

static void initializePrintExportData(ExportFileData &printData)
{
    // by default, make everything 0 - off
    memset(&printData, 0, sizeof(ExportFileData));

    // turn stuff on
    printData.fileType = FILE_TYPE_VRML2;

    INIT_ALL_FILE_TYPES(printData.chkCreateZip, 1, 1, 0, 0, 0, 1, 0);
    // I used to set the last value to 0, meaning only the zip would be created. The idea
    // was that the naive user would then only have the zip, and so couldn't screw up
    // when uploading the model file. But this setting is a pain if you want to preview
    // the model file, you have to always remember to check the box so you can get the
    // preview files. So, now it's off.
    INIT_ALL_FILE_TYPES(printData.chkCreateModelFiles, 1, 1, 1, 1, 1, 1, 1);

    // OBJ and VRML have color, depending...
    // order: Sculpteo OBJ, relative OBJ, i.materialize STL, VISCAM STL, ASCII STL, Shapeways VRML
    INIT_ALL_FILE_TYPES(printData.radioExportNoMaterials, 0, 0, 0, 0, 1, 0, 1);
    // might as well export color with OBJ and binary STL - nice for previewing
    INIT_ALL_FILE_TYPES(printData.radioExportMtlColors, 0, 0, 1, 1, 0, 0, 0);
    INIT_ALL_FILE_TYPES(printData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES(printData.radioExportFullTexture, 1, 1, 0, 0, 0, 1, 0);

    printData.chkMergeFlattop = 1;
    // Shapeways imports VRML files and displays them with Y up, that is, it
    // rotates them itself. Sculpteo imports OBJ, and likes Z is up, so we export with this on.
    // STL uses Z is up, even though i.materialise's previewer shows Y is up.
    INIT_ALL_FILE_TYPES(printData.chkMakeZUp, 1, 1, 1, 1, 1, 0, 0);
    printData.chkCenterModel = 1;
    printData.chkExportAll = 0;
    printData.chkFatten = 0;
    printData.chkBiome = 0;
    printData.chkCompositeOverlay = 1;	// never allow 0 for 3D printing, as this would create tiles floating above the surface
    printData.chkBlockFacesAtBorders = 1; // never allow 0 for 3D printing, as this would make the surface non-manifold
    printData.chkLeavesSolid = 1; // never allow 0 for 3D printing, as this would make the surface non-manifold 

    printData.radioRotate0 = 1;

    printData.radioScaleByBlock = 1;
    printData.modelHeightVal = 5.0f;    // 5 cm target height
    INIT_ALL_FILE_TYPES(printData.blockSizeVal,
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
    printData.chkMultipleObjects = 0;
    printData.chkIndividualBlocks = 0;
    printData.chkMaterialPerType = 0;
    printData.chkMaterialSubtypes = 0;
    // shouldn't really matter, now that both versions don't use the diffuse color when texturing
    printData.chkG3DMaterial = 0;

    printData.floaterCountVal = 16;
    INIT_ALL_FILE_TYPES(printData.chkHollow, 1, 1, 0, 0, 0, 1, 0);
    INIT_ALL_FILE_TYPES(printData.chkSuperHollow, 1, 1, 0, 0, 0, 1, 0);
    INIT_ALL_FILE_TYPES(printData.hollowThicknessVal,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall,
        METERS_TO_MM * gMtlCostTable[PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall);	// last one is schematic "material"

    // materials selected
    INIT_ALL_FILE_TYPES(printData.comboPhysicalMaterial, PRINT_MATERIAL_FCS_SCULPTEO, PRINT_MATERIAL_FCS_SCULPTEO, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE);
    // defaults: for Sculpteo OBJ, cm; for i.materialise, mm; for other STL, cm; for Shapeways VRML, mm
    INIT_ALL_FILE_TYPES(printData.comboModelUnits, UNITS_CENTIMETER, UNITS_CENTIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER, UNITS_MILLIMETER);

    printData.flags = EXPT_3DPRINT;
}

static void initializeViewExportData(ExportFileData &viewData)
{
    //////////////////////////////////////////////////////
    // copy view data from print, and change what's needed
    initializePrintExportData(viewData);

    // Now that I've figured out that Blender can show materials OK, change to "true spec"
    viewData.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;

    // don't really need to create a zip for rendering output
    INIT_ALL_FILE_TYPES( viewData.chkCreateZip,         0, 0, 0, 0, 0, 0, 0);
    INIT_ALL_FILE_TYPES( viewData.chkCreateModelFiles,  1, 1, 1, 1, 1, 1, 1);

    INIT_ALL_FILE_TYPES( viewData.radioExportNoMaterials,  0, 0, 0, 0, 1, 0, 1);  
    INIT_ALL_FILE_TYPES( viewData.radioExportMtlColors,    0, 0, 1, 1, 0, 0, 0);  
    INIT_ALL_FILE_TYPES( viewData.radioExportSolidTexture, 0, 0, 0, 0, 0, 0, 0);  
    INIT_ALL_FILE_TYPES( viewData.radioExportFullTexture,  1, 1, 0, 0, 0, 1, 0);  

    viewData.chkExportAll = 1; 
    // for renderers, assume Y is up, which is the norm
    INIT_ALL_FILE_TYPES( viewData.chkMakeZUp, 0, 0, 0, 0, 0, 0, 0);  

    viewData.modelHeightVal = 100.0f;    // 100 cm - view doesn't need a minimum, really
    INIT_ALL_FILE_TYPES( viewData.blockSizeVal,
        1000.0f,	// 1 meter
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
    INIT_ALL_FILE_TYPES( viewData.chkHollow, 0,0,0,0,0,0,0);
    INIT_ALL_FILE_TYPES( viewData.chkSuperHollow, 0,0,0,0,0,0,0);
    // G3D material off by default for rendering
    viewData.chkMultipleObjects = 1;
    viewData.chkIndividualBlocks = 0;
    viewData.chkMaterialPerType = 1;
    viewData.chkMaterialSubtypes = 0;
    viewData.chkG3DMaterial = 0;
    viewData.chkCompositeOverlay = 0;
    viewData.chkBlockFacesAtBorders = 1;
    viewData.chkLeavesSolid = 0;

    viewData.floaterCountVal = 16;
    // irrelevant for viewing
    INIT_ALL_FILE_TYPES(viewData.hollowThicknessVal, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f);    // 1 meter
    INIT_ALL_FILE_TYPES(viewData.comboPhysicalMaterial, PRINT_MATERIAL_FCS_SCULPTEO, PRINT_MATERIAL_FCS_SCULPTEO, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_CUSTOM_MATERIAL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, PRINT_MATERIAL_FULL_COLOR_SANDSTONE);
    INIT_ALL_FILE_TYPES( viewData.comboModelUnits,UNITS_METER,UNITS_METER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_MILLIMETER,UNITS_METER,UNITS_METER);

    // TODO someday allow getting rid of floaters, that would be cool.
    //gExportSchematicData.chkDeleteFloaters = 1;

    viewData.flags = 0x0;
}

static void InitializeSchematicExportData(ExportFileData &schematicData)
{
    //////////////////////////////////////////////////////
    // copy schematic data from view, and change what's needed
    initializeViewExportData(schematicData);
    schematicData.fileType = FILE_TYPE_SCHEMATIC;	// always
    schematicData.chkMergeFlattop = 0;
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

static void runImportOrScript(wchar_t *importFile, WindowSet & ws, const char **pBlockLabel, LPARAM holdlParam, bool dialogOnSuccess)
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
                MessageBox(NULL, msgString, _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }

        // if world viewed is nether or The End, load here.
        if (is.nether) {
            if (switchToNether(is)) {
                MessageBox(NULL, L"Attempt to switch to the Nether failed", _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }
        else if (is.theEnd) {
            assert(is.nether == false);
            if (switchToTheEnd(is)) {
                MessageBox(NULL, L"Attempt to switch to The End failed", _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }

        // see if we can load the terrain file
        if (strlen(is.terrainFile) > 0)
        {
            if (!commandLoadTerrainFile(is, msgString)) {
                MessageBox(NULL, msgString, _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }

        // see if we can load the color scheme
        if (strlen(is.colorScheme) > 0)
        {
            if (!commandLoadColorScheme(is, msgString)) {
                MessageBox(NULL, msgString, _T("Import warning"), MB_OK | MB_ICONWARNING);
            }
        }


        // cross-over any values that are semi-shared to other file formats
        copyOverExportPrintData(gpEFD);

        gCurX = (gpEFD->minxVal + gpEFD->maxxVal) / 2;
        gCurZ = (gpEFD->minzVal + gpEFD->maxzVal) / 2;

        if (gHighlightOn)
        {
            // reload world in order to clear out any previous selection displayed.
            loadWorld(ws.hWnd);
            setUIOnLoadWorld(ws.hWnd, ws.hwndSlider, ws.hwndLabel, ws.hwndInfoLabel, ws.hwndBottomSlider, ws.hwndBottomLabel);
        }

        gHighlightOn = true;
        SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal);
        enableBottomControl(1, ws.hwndBottomSlider, ws.hwndBottomLabel, ws.hwndInfoBottomLabel);
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
            bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
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
        CheckMenuItem(GetMenu(ws.hWnd), IDM_VIEW_SHOWBIOMES, (gOptions.worldType&BIOMES) ? MF_CHECKED : MF_UNCHECKED);

        // redraw, as selection bounds will change
        drawInvalidateUpdate(ws.hWnd);

        // and note which import was done
        if (dialogOnSuccess)
        {
            MessageBox(
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
static int importSettings(wchar_t *importFile, ImportedSet & is, bool dialogOnSuccess)
{
    // this will get initialized again by the readers, but since we check is.logging on exit here, initialize now anyway.
    ExportFileData dummyEfd;
    initializeImportedSet(is, &dummyEfd, importFile);

    // Read header of exported file, as written by writeStatistics(), and get export settings from it,
    // or read file as a set of scripting commands.
    int retCode = 0;
    FILE *fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH + 100];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        MessageBox(NULL, buf, _T("Read error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
        retCode = 0;
        goto Exit;
    }

    // Read first line.
    char lineString[IMPORT_LINE_LENGTH];

    readLine(fh, lineString, IMPORT_LINE_LENGTH);
    fclose(fh);

    //  Is it in export file format?
    bool exported = false;
    if ((strstr(lineString, "# Wavefront OBJ file made by Mineways") != NULL) ||
        (strstr(lineString, "#VRML V2.0 utf8") != NULL) ||
        (strstr(lineString, "# Minecraft world:") != NULL) ||	// stl
        (strstr(lineString, "# Extracted from Minecraft world") != NULL)) {
        exported = true;

        // set up for export
        retCode = importModelFile(importFile, is) ? IMPORT_MODEL : 0;
    }
    else
    {
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
            char *pConverted = NULL;
            size_t length = wcslen(is.errorMessages);
            if (length == 0) {
                sprintf_s(outputString, 256, "No errors or warnings");
                if (PortaWrite(is.logfile, outputString, strlen(outputString))) goto OnDone;
            }
            else {
                // output errors and warnings
                // convert to char
                length++;
                pConverted = (char*)malloc(length*sizeof(char));
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

            if (length > 0) {
                wchar_t msgString[1024];
                if (is.errorsFound > 0) {
                    swprintf_s(msgString, 1024, L"Errors found in script file - processing aborted. Check log file %S",
                        is.logFileName);
                    MessageBox(NULL, msgString, _T("Script error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                }
                else {
                    swprintf_s(msgString, 1024, L"Processing finished. Warnings found in script file. Check log file %S",
                        is.logFileName);
                    MessageBox(NULL, msgString, _T("Script error"), MB_OK | MB_ICONERROR);
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
            MessageBox(NULL, is.errorMessages, is.readingModel ? _T("Import error") : _T("Script error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
            deleteCommandBlockSet(is.pCBChead);
            is.pCBChead = is.pCBClast = NULL;
            retCode = false;
        }
        else if (is.errorMessages && (is.errorMessages[0] != (wchar_t)0)) {
            MessageBox(NULL, is.errorMessages, is.readingModel ? _T("Import warning") : _T("Script warning"), MB_OK | MB_ICONWARNING);
        }
        else {
            if (!is.readingModel && dialogOnSuccess)
                MessageBox(NULL, _T("Script successfully finished running."), _T("Informational"), MB_OK | MB_ICONINFORMATION);
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
static bool importModelFile(wchar_t *importFile, ImportedSet & is)
{
    FILE *fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH + 100];
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
            retCode = false;
            goto Exit;
        }

        // process line, since it's valid
        char *cleanedLine = prepareLineData(lineString, true);

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
    Exit:
    fclose(fh);
    return retCode;
}

// true if all went well
static bool readAndExecuteScript(wchar_t *importFile, ImportedSet & is)
{
    FILE *fh;
    errno_t err = _wfopen_s(&fh, importFile, L"rt");

    if (err != 0) {
        wchar_t buf[MAX_PATH + 100];
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
    SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Checking script for syntax errors");

    // First test: read lines until done
    int readCode;
    int isRet;
    bool retCode = true;

    bool commentBlock = false;
    char *cleanedLine;
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
    // clear out errors, ready for more
    free(is.errorMessages);
    is.errorMessages = NULL;
    is.errorMessagesStringSize = 0;

    // set up real data to change, directly!
    ExportFileData *pEFD = &gExportPrintData;
    if (gpEFD == &gExportViewData)
    {
        pEFD = &gExportViewData;
    }
    initializeImportedSet(is, pEFD, importFile);
    is.readingModel = false;
    is.processData = true;

    err = _wfopen_s(&fh, importFile, L"rt");
    if (err != 0) {
        wchar_t buf[MAX_PATH + 100];
        wsprintf(buf, L"Error: could not read file %s", importFile);
        saveErrorMessage(is, buf);
        return false;
    }

    SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)RUNNING_SCRIPT_STATUS_MESSAGE);

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
    SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Script done");

Exit :
    fclose(fh);
    return retCode;
}

static void initializeImportedSet(ImportedSet & is, ExportFileData *pEFD, wchar_t *importFile)
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
    is.minxVal = is.minyVal = is.minzVal = is.maxxVal = is.maxyVal = is.maxzVal = 0;
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
static int readLine(FILE *fh, char *inputString, int stringLength)
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
static char *prepareLineData(char *line, bool model)
{
    char *lineLoc = line;
    char *strPtr = strstr(lineLoc, "//");
    if (strPtr != NULL) {
        // remove comment from end of line - note that this can also lose http:// data!
        *strPtr = (char)0;
    }

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
    return lineLoc;
}

// Return true if we've entered a comment block. The line may still have useful stuff in it to process.
static bool dealWithCommentBlocks(char *line, bool commentBlock)
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
static bool startCommentBlock(char *line)
{
    char *strPtr = strstr(line, "/*");
    if (strPtr != NULL)
    {
        // found the start of a comment block
        *strPtr = (char)0;
        // do we now find the end of a comment block in what remains of the line?
        char *endPtr = strstr(strPtr+2, "*/");
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
static char *closeCommentBlock(char *line)
{
    char *strPtr = strstr(line, "*/");
    if (strPtr != NULL)
    {
        // found the end of a comment block
        return strPtr+2;
    }
    return NULL;
}

// Return 0 if this executes properly. Else return code should be passed up chain.
static int switchToNether(ImportedSet & is)
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
    if (gCurDepth == MAP_MAX_HEIGHT)
    {
        gCurDepth = 126;
        setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
    }
    gOverworldHideStatus = gOptions.worldType&HIDEOBSCURED;
    gOptions.worldType |= HIDEOBSCURED;

    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_OBSCURED, (gOptions.worldType&HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_HELL, (gOptions.worldType&HELL) ? MF_CHECKED : MF_UNCHECKED);
    if (gOptions.worldType&ENDER)
    {
        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_UNCHECKED);
        gOptions.worldType &= ~ENDER;
    }
    CloseAll();
    // clear selection when you switch from somewhere else to The Nether, or vice versa
    gHighlightOn = FALSE;
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0);
    enableBottomControl(gHighlightOn, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);

    return 0;
}

// Return 0 if this executes properly. Else return code should be passed up chain.
static int switchToTheEnd(ImportedSet & is)
{
    if (gWorldGuide.type == WORLD_SCHEMATIC_TYPE || gWorldGuide.type == WORLD_TEST_BLOCK_TYPE)
    {
        saveWarningMessage(is, L"attempt to switch to The End level but this world has none.");
        return INTERPRETER_FOUND_ERROR;
    }
    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_CHECKED);
    // entering Ender, turn off hell if need be
    gOptions.worldType |= ENDER;
    if (gOptions.worldType&HELL)
    {
        // get out of hell zoom
        gCurX *= 8.0;
        gCurZ *= 8.0;
        // and undo other hell stuff
        if (gCurDepth == 126)
        {
            gCurDepth = MAP_MAX_HEIGHT;
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
    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0);
    enableBottomControl(gHighlightOn, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);

    return 0;
}

static int interpretImportLine(char *line, ImportedSet & is)
{
    char *strPtr;
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
    if (strPtr == NULL)
        // new script style:
        strPtr = findLineDataNoCase(line, "Minecraft world: ");
    if (strPtr != NULL) {
        if (*strPtr == (char)0) {
            saveErrorMessage(is, L"no world given.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            // model or scripting - save path
            strcpy_s(is.world, MAX_PATH, strPtr);
            if (!is.readingModel) {
                // scripting: load it
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
        } else {
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
                else if (wcsstr(is.importFile, L".wrl"))
                {
                    is.pEFD->fileType = FILE_TYPE_VRML2;
                }
                else {
                    saveErrorMessage(is, L"could not determine what type of model file (OBJ, VRML, STL) is being read.", strPtr);
                    return INTERPRETER_FOUND_ERROR;
                }
            }
            else {
                saveErrorMessage(is, L"could not determine what type of model file (OBJ, VRML, STL) is desired.", strPtr);
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
            strcpy_s(is.terrainFile, MAX_PATH, strPtr);
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
                    CheckMenuItem(GetMenu(is.ws.hWnd), IDM_OBSCURED, (gOptions.worldType&HIDEOBSCURED) ? MF_CHECKED : MF_UNCHECKED);
                    if (gOptions.worldType&ENDER)
                    {
                        CheckMenuItem(GetMenu(is.ws.hWnd), IDM_END, MF_UNCHECKED);
                        gOptions.worldType &= ~ENDER;
                    }
                    CloseAll();
                    // clear selection when you switch from somewhere else to The Nether, or vice versa
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, 0, 0, 0, 0, 0);
                    enableBottomControl(gHighlightOn, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);
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
            strcpy_s(is.colorScheme, MAX_PATH, strPtr);
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
            if (1 == sscanf_s(strPtr, "%s", string1, _countof(string1)))
            {
                if (_stricmp(string1, "none") == 0) {
                    noSelection = true;
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
        if (is.processData) {
            // two cases: scripting and model loading. Model loading defers loading the world, so process the data.
            if (is.readingModel || gLoaded) {
                if (noSelection) {
                    // for scripting we could test that there's no world loaded, but it's fine to call this then anyway.
                    gHighlightOn = FALSE;
                    SetHighlightState(gHighlightOn, 0, gTargetDepth, 0, 0, gCurDepth, 0);
                }
                else {
                    // yes, a selection
                    is.minxVal = v[0];
                    is.minyVal = v[1];
                    is.minzVal = v[2];
                    is.maxxVal = v[3];
                    is.maxyVal = v[4];
                    is.maxzVal = v[5];
                    if (!is.readingModel) {
                        // is a world loaded? If not, then don't set the selection
                        if (gLoaded) {
                            gCurX = (is.minxVal + is.maxxVal) / 2;
                            gCurZ = (is.minzVal + is.maxzVal) / 2;

                            gHighlightOn = true;
                            SetHighlightState(1, is.minxVal, is.minyVal, is.minzVal, is.maxxVal, is.maxyVal, is.maxzVal);
                            enableBottomControl(1, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);
                            // put target (bottom) depth to new depth set, if any
                            gTargetDepth = is.minyVal;
                            gCurDepth = is.maxyVal;

                            // don't bother updating status in commands, do that after all is done
                            //gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(1) - MAIN_WINDOW_TOP, gCurX, gCurZ,
                            //	bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
                            //updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, is.ws.hwndStatus);
                            setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
                            setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, false);
                        }
                        else {
                            saveErrorMessage(is, L"selection set but no world is loaded.");
                            return INTERPRETER_FOUND_ERROR;
                        }
                    }
                }
            }
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    // find whether we're reading in a rendering or 3d printing file
    strPtr = findLineDataNoCase(line, "Units for the model vertex data itself:");
    if (strPtr != NULL) {
        // found selection, parse it
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
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

    strPtr = findLineDataNoCase(line, "File type:");
    if (strPtr != NULL) {
        // found selection, parse it
        if (1 != sscanf_s(strPtr, "Export %s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find Export string for file type (solid color, textured, etc.)."); return INTERPRETER_FOUND_ERROR;
        }
        // shortcut here, just look for first word
        char *outputTypeString[] = {
            "no", // "Export no materials",
            "solid", // "Export solid material colors only (no textures)",
            "richer", // "Export richer color textures",
            "full", // "Export full color texture patterns"
        };
        for (i = 0; i < 4; i++)
        {
            if (_stricmp(outputTypeString[i], string1) == 0)
            {
                break;
            }
        }
        if (i >= 4) {
            saveErrorMessage(is, L"could not interpret file type (solid color, textured, etc.).", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if (is.processData) {
            is.pEFD->radioExportNoMaterials[is.pEFD->fileType] = 0;
            is.pEFD->radioExportMtlColors[is.pEFD->fileType] = 0;
            is.pEFD->radioExportSolidTexture[is.pEFD->fileType] = 0;
            is.pEFD->radioExportFullTexture[is.pEFD->fileType] = 0;
            switch (i)
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
            default:
            case 4:
                break;
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Export separate objects:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Export separate objects command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMultipleObjects = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Individual blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Individual blocks command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkIndividualBlocks = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Material per object:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Material per object command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMaterialPerType = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Split materials into subtypes:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Split materials into subtypes command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMaterialSubtypes = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "G3D full material:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for G3D full material command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkG3DMaterial = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Make Z the up direction instead of Y:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Make Z command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMakeZUp[is.pEFD->fileType] = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Create composite overlay faces:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Create composite overlay faces command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkCompositeOverlay = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Center model:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Center model command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkCenterModel = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Export lesser blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Export lesser blocks command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkExportAll = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Fatten lesser blocks:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Fatten lesser blocks command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkFatten = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Make tree leaves solid:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Make tree leaves solid command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkLeavesSolid = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Create block faces at the borders:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Create block faces at the borders command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkBlockFacesAtBorders = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }

    strPtr = findLineDataNoCase(line, "Use biomes:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Use biomes command."); return INTERPRETER_FOUND_ERROR;
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
                CheckMenuItem(GetMenu(is.ws.hWnd), IDM_VIEW_SHOWBIOMES, (gOptions.worldType&BIOMES) ? MF_CHECKED : MF_UNCHECKED);
            }
        }
        return INTERPRETER_FOUND_VALID_EXPORT_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    float floatVal = 0.0f;
    strPtr = findLineDataNoCase(line, "Rotate model ");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%f degrees", &floatVal))
        {
            saveErrorMessage(is, L"could not interpret degrees value.", strPtr); return INTERPRETER_FOUND_ERROR;
        }
        if ((floatVal != 0.0f) && (floatVal != 90.0f) && (floatVal != 180.0f) && (floatVal != 270.0f)) {
            saveErrorMessage(is, L"model scale value must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            is.pEFD->radioRotate0 = is.pEFD->radioRotate90 = is.pEFD->radioRotate180 = is.pEFD->radioRotate270 = 0;

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


    strPtr = findLineDataNoCase(line, "Scale model by ");
    if (strPtr != NULL) {
        bool materialSet = false;
        is.pEFD->radioScaleByBlock = is.pEFD->radioScaleByCost = is.pEFD->radioScaleToHeight = is.pEFD->radioScaleToMaterial = 0;
        // oddly, "making each block 100 mmm high" passes - maybe the end of the string is ignored after %f?
        if (1 == sscanf_s(strPtr, "making each block %f mm high", &floatVal))
        {
            if (floatVal <= 0.0f) {
                saveErrorMessage(is, L"model scale value must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
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
            if (5 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s %s %s", &floatVal, string1, _countof(string1), string2, _countof(string2), string3, _countof(string3), string4, _countof(string4)))
            {
                if (_stricmp(string4, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
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
            else if (4 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s %s", &floatVal, string1, _countof(string1), string2, _countof(string2), string3, _countof(string3)))
            {
                if (_stricmp(string3, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
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
            else if (3 == sscanf_s(strPtr, "aiming for a cost of %f for the %s %s", &floatVal, string1, _countof(string1), string2, _countof(string2)))
            {
                if (_stricmp(string2, "material") != 0)
                {
                    saveErrorMessage(is, L"could not find proper material string for Scale model command.", strPtr); return INTERPRETER_FOUND_ERROR;
                }
                if (floatVal <= 0.0f) {
                    saveErrorMessage(is, L"cost must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
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
                    saveErrorMessage(is, L"height must be a positive number.", strPtr); return INTERPRETER_FOUND_ERROR;
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
                if (4 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s %s %s", string1, _countof(string1), string2, _countof(string2), string3, _countof(string3), string4, _countof(string4)))
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
                else if (3 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s %s", string1, _countof(string1), string2, _countof(string2), string3, _countof(string3)))
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
                else if (2 == sscanf_s(strPtr, "using the minimum wall thickness for the %s %s", string1, _countof(string1), string2, _countof(string2)))
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
            string1, _countof(string1),
            string2, _countof(string2),
            string3, _countof(string3)
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
            string1, _countof(string1),
            string2, _countof(string2),
            string3, _countof(string3)
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
            string1, _countof(string1)))
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
            string1, _countof(string1),
            string2, _countof(string2)
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
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Melt snow blocks command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkMeltSnow = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Debug: show separate parts as colors:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Debug parts command."); return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;

        if (is.processData)
            is.pEFD->chkShowParts = interpretBoolean(string1);
        return INTERPRETER_FOUND_VALID_EXPORT_LINE;
    }


    strPtr = findLineDataNoCase(line, "Debug: show weld blocks in bright colors:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Debug weld blocks command."); return INTERPRETER_FOUND_ERROR;
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

static int interpretScriptLine(char *line, ImportedSet & is)
{
    char *strPtr, *strPtr2;
    char string1[100];
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
            model = 0;
        }
        else {
            strPtr2 = findLineDataNoCase(strPtr, "for 3D Printing:");
            if (strPtr2 != NULL) {
                model = 1;
            }
            else {
                strPtr2 = findLineDataNoCase(strPtr, "Schematic:");
                if (strPtr2 != NULL) {
                    model = 2;
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


    JumpToSpawn:
    strPtr = findLineDataNoCase(line, "Jump to Spawn");
    if (strPtr != NULL) {
        if (is.processData) {
            if (gLoaded) {
                gCurX = gSpawnX;
                gCurZ = gSpawnZ;
                if (gOptions.worldType&HELL)
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
                if (gOptions.worldType&HELL)
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
                GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
                // should always be on, but just in case...
                if (on)
                {
                    gCurX = (minx + maxx) / 2;
                    gCurZ = (minz + maxz) / 2;
                    if (gOptions.worldType&HELL)
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
        if (1 != sscanf_s(strPtr, "%d, %d", &v)) {
            // bad parse - warn and quit
            saveErrorMessage(is, L"could not read 'Zoom' value.", strPtr);
            return INTERPRETER_FOUND_ERROR;
        }
        if ((v < 1) || (v > 15)) {
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

    if (findBitToggle(line, is, "Show all objects", SHOWALL, IDM_SHOWALLOBJECTS, &retCode)) return retCode;
    if (findBitToggle(line, is, "Show biomes", BIOMES, IDM_VIEW_SHOWBIOMES, &retCode)) return retCode;
    if (findBitToggle(line, is, "Elevation shading", DEPTHSHADING, IDM_DEPTH, &retCode)) return retCode;
    if (findBitToggle(line, is, "Lighting", LIGHTING, IDM_LIGHTING, &retCode)) return retCode;
    if (findBitToggle(line, is, "Cave mode", CAVEMODE, IDM_CAVEMODE, &retCode)) return retCode;
    if (findBitToggle(line, is, "Hide obscured", HIDEOBSCURED, IDM_OBSCURED, &retCode)) return retCode;

    strPtr = findLineDataNoCase(line, "Give more export memory:");
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            saveErrorMessage(is, L"could not find boolean value for Give more export memory command.");
            return INTERPRETER_FOUND_ERROR;
        }
        if (!validBoolean(is, string1)) return INTERPRETER_FOUND_ERROR;
        if (is.processData)
        {
            gOptions.moreExportMemory = interpretBoolean(string1);
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
        if (1 == sscanf_s(strPtr, "%s", string1, _countof(string1))) {
            // first, is it simply a number?
            if (1 == sscanf_s(strPtr, "%d", &minHeight))
            {
                // it's simply a number. Is it + or -?
                if ((string1[0] == (char)'+') || (string1[0] == (char)'-')) {
                    if (is.processData) {
                        // it's a relative offset
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
                        minHeight += miny;
                        clamp(minHeight, 0, 255);
                    } else {
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
                        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
                        if (on) {
                            int heightFound = GetMinimumSelectionHeight(&gWorldGuide, &gOptions, minx, minz, maxx, maxz, true, (string1[0] == (char)'V'), maxy);
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

        if (minHeight < 0 || minHeight > MAP_MAX_HEIGHT) {
            saveErrorMessage(is, L"value must be between 0 and 255, inclusive, for Select minimum height command.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            gTargetDepth = minHeight;
            setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, false);
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
            SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz);
            enableBottomControl(on, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);
        }
        return INTERPRETER_FOUND_VALID_LINE | INTERPRETER_REDRAW_SCREEN;
    }

    strPtr = findLineDataNoCase(line, "Select maximum height:");
    if (strPtr != NULL) {
        int minHeight;
        if (1 != sscanf_s(strPtr, "%d", &minHeight))
        {
            saveErrorMessage(is, L"could not find boolean value for Select maximum height command."); return INTERPRETER_FOUND_ERROR;
        }
        if (minHeight < 0 || minHeight > MAP_MAX_HEIGHT) {
            saveErrorMessage(is, L"value must be between 0 and 255, inclusive, for Select maximum height command.", strPtr); return INTERPRETER_FOUND_ERROR;
        }

        if (is.processData) {
            gCurDepth = minHeight;
            setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, false);
            GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz);
            SetHighlightState(on, minx, gTargetDepth, minz, maxx, gCurDepth, maxz);
            enableBottomControl(on, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);
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
        if (is.logging) {
            saveWarningMessage(is, L"ignored: log file was opened earlier.");
            return INTERPRETER_FOUND_VALID_LINE;
        }
        // so that we know, to flag errors later.
        is.logging = true;
        // NOTE: we open the log file immediately, not on the second pass
        if (!is.processData) {
            strcpy_s(is.logFileName, MAX_PATH, strPtr);
            if (!openLogFile(is)) {
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    strPtr = findLineDataNoCase(line, "Custom printer ");
    if (strPtr != NULL) {
        float floatVal, floatVal2, floatVal3;
        char buf[MAX_PATH];
        if (1 == sscanf_s(strPtr, "minimum wall thickness: %f", &floatVal))
        {
            if (is.processData) {
                // this one is actually stored in meters.
                gMtlCostTable[PRINT_MATERIAL_CUSTOM_MATERIAL].minWall = floatVal*MM_TO_METERS;
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
        else if (1 == sscanf_s(strPtr, "currency: %s", &buf, _countof(buf)))
        {
            if (is.processData) {
                if (gCustomCurrency) {
                    free(gCustomCurrency);
                    gCustomCurrency = NULL;
                }
                size_t size = (strlen(buf) + 1)*sizeof(wchar_t);
                gCustomCurrency = (wchar_t *)malloc(size);
                size_t dummySize = 0;
                mbstowcs_s(&dummySize, gCustomCurrency, size/2, buf, (size/2)-1);
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
                wchar_t error[1024];
                wsprintf(error, L"could not understand command %S", line);
                saveErrorMessage(is, error);
                return INTERPRETER_FOUND_ERROR;
            }
        }
        return INTERPRETER_FOUND_VALID_LINE;
    }

    // something on line, but means nothing - warn user
    return INTERPRETER_FOUND_NOTHING_USEFUL;
}

static bool findBitToggle(char *line, ImportedSet & is, char *type, unsigned int bitLocation, unsigned int windowID, int *pRetCode)
{
    *pRetCode = INTERPRETER_FOUND_NOTHING_USEFUL;	// until proven otherwise
    char string1[100];
    char commandString[1024];
    strcpy_s(commandString, 1024, type);
    strcat_s(commandString, 1024, ":");
    char *strPtr = findLineDataNoCase(line, commandString);
    if (strPtr != NULL) {
        if (1 != sscanf_s(strPtr, "%s", string1, _countof(string1)))
        {
            wchar_t error[1024];
            wsprintf(error, L"could not find boolean value for %S command.", type);
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

static bool testChangeBlockCommand(char *line, ImportedSet & is, int *pRetCode)
{
    // types of commands:
    // fromOne or fromMulti or fromAll - fromOne is a single block and perhaps data, from Multi is the bit table of everything, fromAll is everything except air
    // to is always a single type and data
    // location is always a range, even if identical
    // linked list of these commands, one after another

    *pRetCode = INTERPRETER_FOUND_NOTHING_USEFUL;	// until proven otherwise
    wchar_t error[1024];
    bool keepLooking;
    int fromBlockCount = 0;
    int fromType, fromData, fromEndType, fromEndData;
    unsigned short fromDataBits, fromEndDataBits;
    int toType, toData;
    bool foundSomething = false;
    fromEndType = 0;	// make compiler happy
    fromEndDataBits = 0xffff; // make compiler happy

    char *strPtr = findLineDataNoCase(line, "Change blocks:");
    if (strPtr != NULL) {
        // let the fun begin:
        // [from "grass block","Sand"-"gravel",55:2] [to 76:3] [at x, y, z[ to x, y, z]]
        //
        // We need at least a from, a to, or a location
        //
        // look for "from"
        if (strstr(strPtr, "from ") == strPtr){
            // found "from ", so digest it.
            foundSomething = true;
            strPtr = findLineDataNoCase(strPtr, "from ");
            MY_ASSERT(strPtr);
            keepLooking = true;
            boolean cbCreated = false;
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
                    } else {
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
        if (strstr(strPtr, "to ") == strPtr){
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
                saveCBinto(is.pCBClast, (unsigned char)toType, (unsigned char)toData);
        } // else, no "to" given, so air is assumed, which is the default 0:0

        // finally, look for location(s)
        if (strstr(strPtr, "at ") == strPtr){
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
        else if (foundSomething && (*strPtr != (char)0)){
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
static void cleanStringForLocations(char *cleanString, char *strPtr)
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
static void createCB(ImportedSet & is)
{
    ChangeBlockCommand *pCBC = (ChangeBlockCommand *)malloc(sizeof(ChangeBlockCommand));
    memset(pCBC, 0, sizeof(ChangeBlockCommand));

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

static void addFromRangeToCB(ChangeBlockCommand *pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits)
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
            if ((fromDataBits != pCBC->simpleFromDataBits)||
                (fromType > pCBC->simpleFromTypeEnd + 1) ||
                (fromEndType < pCBC->simpleFromTypeBegin - 1)){
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

static void setDefaultFromRangeToCB(ChangeBlockCommand *pCBC, unsigned char fromType, unsigned char fromEndType, unsigned short fromDataBits)
{
    //pCBC->hasFrom = false; - default, should already be set as such
    pCBC->simpleFromTypeBegin = fromType;
    pCBC->simpleFromTypeEnd = fromEndType;
    pCBC->simpleFromDataBits = fromDataBits;
}

static void addRangeToDataBitsArray(ChangeBlockCommand *pCBC, int fromType, int fromEndType, unsigned short fromDataBits)
{
    // go through range and add bits to each field
    MY_ASSERT(fromType <= fromEndType);
    for (int type = fromType; type <= fromEndType; type++) {
        pCBC->fromDataBitsArray[type] |= fromDataBits;
    }
}

static void saveCBinto(ChangeBlockCommand *pCBC, unsigned char intoType, unsigned char intoData)
{
    pCBC->intoType = intoType;
    pCBC->intoData = intoData;
    pCBC->hasInto = true;
}

static void addDataBitsArray(ChangeBlockCommand *pCBC)
{
    pCBC->fromDataBitsArray = (unsigned short *)malloc(256 * sizeof(unsigned short));
    memset(pCBC->fromDataBitsArray, 0, 256 * sizeof(unsigned short));
}

static void saveCBlocation(ChangeBlockCommand *pCBC, int v[6])
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

static void deleteCommandBlockSet(ChangeBlockCommand *pCBC)
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
static char *findBlockTypeAndData(char *line, int *pType, int *pData, unsigned short *pDataBits, wchar_t *error)
{
    error[0] = (wchar_t)0;

    char *strPtr = removeLeadingWhitespace(line);

    // first find a block, if any
    if (*strPtr == '\"') {
        strPtr++;
        // there might be a string block identifier
        for (int type = 0; type <= 255; type++) {
            // compare, caseless, to block types until we find a match
            char *foundPtr = compareLCAndSkip(strPtr, gBlockDefinitions[type].name);
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
static char *compareLCAndSkip(char *a, char const *b)
{
    for (;*a && *b; a++, b++) {
        int d = tolower(*a) - tolower(*b);
        if (d != 0)
            return NULL;
    }
    // survived: if we're not at the end of the "b" string, what we're comparing to, then we haven't parsed through and matched the whole string;
    // else, return pointer to next character in our string.
    return *b ? NULL : a++;
}

static char *skipPastUnsignedInteger(char *strPtr)
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

static char *findLineDataNoCase(char *line, char *findStr)
{
    // we now have the tasty part of the line, so compare the test string and make sure it is exactly at the beginning of this prepared line
    char *strPtr = compareLCAndSkip(line, findStr);
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
static char *removeLeadingWhitespace(char *line)
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

static void saveErrorMessage(ImportedSet & is, wchar_t *error, char *restOfLine)
{
    saveMessage(is, error, L"Error", 1, restOfLine);
}
static void saveWarningMessage(ImportedSet & is, wchar_t *error)
{
    saveMessage(is, error, L"Warning", 0, NULL);
}
static void saveMessage(ImportedSet & is, wchar_t *error, wchar_t *msgType, int increment, char *restOfLine )
{
    if (is.errorMessages == NULL) {
        is.errorMessagesStringSize = 1024;
        is.errorMessages = (wchar_t *)malloc(is.errorMessagesStringSize*sizeof(wchar_t));
        is.errorMessages[0] = (wchar_t)0;
    }

    size_t oldlength = wcslen(is.errorMessages);
    size_t addlength = 50 + wcslen(error) + ((restOfLine != NULL) ? strlen(restOfLine) : 0);
    // enough room?
    if (is.errorMessagesStringSize < oldlength + addlength) {
        is.errorMessagesStringSize *= 2;
        // just to be really really sure, add some more
        is.errorMessagesStringSize += addlength;
        wchar_t *oldStr = is.errorMessages;
        is.errorMessages = (wchar_t *)malloc(is.errorMessagesStringSize*sizeof(wchar_t));
        memcpy(is.errorMessages, oldStr, (oldlength + 1)*sizeof(wchar_t));
        free(oldStr);
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

static bool validBoolean(ImportedSet & is, char *string)
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

static bool interpretBoolean(char *string)
{
    // YES, yes, TRUE, true, 1
    return ((string[0] == 'Y') || (string[0] == 'y') || (string[0] == 'T') || (string[0] == 't') || (string[0] == '1'));
}

static void formTitle(WorldGuide *pWorldGuide, wchar_t *title)
{
    wcscpy_s( title, MAX_PATH-1, L"Mineways: " );

    if (pWorldGuide->type == WORLD_TEST_BLOCK_TYPE)
    {
        wcscat_s(title, MAX_PATH - 1, L"[Block Test World]");
    }
    else
    {
        wcscat_s(title, MAX_PATH - 1, stripWorldName(pWorldGuide->world));
    }
}

// change any '/' to '\', or vice versa, as preferred
static void rationalizeFilePath(wchar_t *fileName)
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
                        MessageBox( NULL, msgString,
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
static bool splitToPathAndName( wchar_t *pathAndName, wchar_t *path, wchar_t *name)
{
    wchar_t tempPathAndName[1024];
    wcscpy_s(tempPathAndName, pathAndName);
    wchar_t *lastPtr = wcsrchr(tempPathAndName, (wchar_t)'\\');
    if (lastPtr != NULL) {
        if (name != NULL)
            wcscpy_s(name, MAX_PATH, lastPtr + 1);
        if (path != NULL) {
            *lastPtr = (wchar_t)0;
            wcscpy_s(path, MAX_PATH, tempPathAndName);
        }
    }
    else {
        lastPtr = wcsrchr(tempPathAndName, (wchar_t)'/');
        if (lastPtr != NULL) {
            if (name != NULL)
                wcscpy_s(name, MAX_PATH, lastPtr + 1);
            if (path != NULL) {
                *lastPtr = (wchar_t)0;
                wcscpy_s(path, MAX_PATH, tempPathAndName);
            }
        }
        else {
            // no path
            if (name != NULL)
                wcscpy_s(name, MAX_PATH, tempPathAndName);
            if (path != NULL)
                path[0] = (wchar_t)0;
            return false;
        }
    }
    return true;
}


// true if it worked; false if there's an error, which is returned in *error.
static bool commandLoadWorld(ImportedSet & is, wchar_t *error)
{
    // see if we can load the world
    size_t dummySize = 0;
    wchar_t backupWorld[MAX_PATH];
    wcscpy_s(backupWorld, MAX_PATH, gWorldGuide.world);
    int backupWorldType = gWorldGuide.type;
    mbstowcs_s(&dummySize, gWorldGuide.world, (size_t)MAX_PATH, is.world, MAX_PATH);
    if (wcslen(gWorldGuide.world) > 0) {
        // first, "rationalize" the gWorldGuide.world name: make it all \'s or all /'s, not both.
        // This will make it nicer for comparing to see if the new world is the same as the previously-loaded world.
        rationalizeFilePath(gWorldGuide.world);

        // world name found - can we load it?
        wchar_t warningWorld[MAX_PATH];
        wcscpy_s(warningWorld, MAX_PATH, gWorldGuide.world);

        // first, is it the test world?
        if (wcscmp(gWorldGuide.world, L"[Block Test World]") == 0)
        {
            // the test world is noted by the empty string.
            gWorldGuide.world[0] = (wchar_t)0;
            gWorldGuide.type = WORLD_TEST_BLOCK_TYPE;
            gSameWorld = FALSE;
            sprintf_s(gSkfbPData.skfbName, "TestWorld");
        }
        else {
            // test if it's a schematic file or a normal world, and set TYPE appropriately.
            wchar_t filename[MAX_PATH];
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
                wchar_t testAnvil[MAX_PATH];
                wcscpy_s(testAnvil, MAX_PATH, gWorldPathDefault);
                wcscat_s(testAnvil, MAX_PATH, gPreferredSeparatorString);
                wcscat_s(testAnvil, MAX_PATH, gWorldGuide.world);
                wcscpy_s(gWorldGuide.world, MAX_PATH, testAnvil);
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
                wcscpy_s(gWorldGuide.world, MAX_PATH, backupWorld);
                if (gWorldGuide.world[0] != 0) {
                    gWorldGuide.type = backupWorldType;
                    loadWorld(is.ws.hWnd);	// uses gWorldGuide.world
                }
                swprintf_s(error, 1024, L"Mineways attempted to load world \"%s\" but could not do so. Either the world could not be found, or the world name is some wide character string that could not be stored in your import file. Please load the world manually and then try importing again.", warningWorld);
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
static bool commandLoadTerrainFile(ImportedSet & is, wchar_t *error)
{
    FILE *fh;
    errno_t err;
    wchar_t terrainFileName[MAX_PATH], tempPath[MAX_PATH], tempName[MAX_PATH];

    size_t dummySize = 0;
    mbstowcs_s(&dummySize, terrainFileName, (size_t)MAX_PATH, is.terrainFile, MAX_PATH);
    // first, "rationalize" the name: make it all \'s or all /'s, not both.
    rationalizeFilePath(terrainFileName);
    if (!splitToPathAndName(terrainFileName, tempPath, tempName)) {
        // just a file name found, so make test path with directory
        if (wcslen(tempName) > 0)
        {
            wcscpy_s(terrainFileName, MAX_PATH, gSelectTerrainDir);
            wcscat_s(terrainFileName, MAX_PATH - wcslen(terrainFileName), L"\\");
            wcscat_s(terrainFileName, MAX_PATH - wcslen(terrainFileName), tempName);
        }
        else {
            // something odd happened - filename is empty
            swprintf_s(error, 1024, L"terrain file \"%S\" not possible to convert to a file name. Please select the terrain file manually.", is.terrainFile);
            return false;
        }
        err = _wfopen_s(&fh, terrainFileName, L"rt");
        if (err != 0) {
            // can't find it at all, so generate error.
            swprintf_s(error, 1024, L"terrain file \"%S\" was not found. Please select the terrain file manually.", is.terrainFile);
            return false;
        }
        // success, copy file and path (directory is fine)
        wcscpy_s(gSelectTerrainPathAndName, MAX_PATH, terrainFileName);
    }
    else {
        // path found: try the file as a full path.
        err = _wfopen_s(&fh, terrainFileName, L"rt");
        if (err != 0) {
            // can't find it at all, so generate error.
            swprintf_s(error, 1024, L"terrain file \"%S\" was not found. Please select the terrain file manually.", is.terrainFile);
            return false;
        }
        // success, copy file name&path, and directory
        wcscpy_s(gSelectTerrainPathAndName, MAX_PATH, terrainFileName);
        wcscpy_s(gSelectTerrainDir, MAX_PATH, tempPath);
    }
    fclose(fh);
    return true;
}

// true if it worked; false if there's an error, which is returned in *error.
static bool commandLoadColorScheme(ImportedSet & is, wchar_t *error)
{
    wchar_t backupColorScheme[255];
    wcscpy_s(backupColorScheme, 255, gSchemeSelected);
    size_t dummySize = 0;
    mbstowcs_s(&dummySize, gSchemeSelected, 255, is.colorScheme, 255);
    int item = findColorScheme(gSchemeSelected);
    if (item > 0)
    {
        useCustomColor(IDM_CUSTOMCOLOR + item, is.ws.hWnd);
    }
    else if (wcscmp(gSchemeSelected, L"Standard") == 0)
    {
        useCustomColor(IDM_CUSTOMCOLOR, is.ws.hWnd);
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
    return true;
}

// true if it worked
static bool commandExportFile(ImportedSet & is, wchar_t *error, int fileMode, char *fileName)
{
    if (!gHighlightOn)
    {
        // we keep the export options ungrayed now so that they're selectable when the world is loaded
        swprintf_s(error, 1024, L"no volume is selected for export; click and drag using the right-mouse button.");
        return false;
    }

    // 0 - render, 1 - 3d print, 2 - schematic
    gPrintModel = fileMode;

    wchar_t wcharFileName[MAX_PATH];

    size_t dummySize = 0;
    mbstowcs_s(&dummySize, wcharFileName, (size_t)MAX_PATH, fileName, MAX_PATH);
    // first, "rationalize" the name: make it all \'s or all /'s, not both.
    rationalizeFilePath(wcharFileName);

    // if there are any change commands, transfer them now for processing
    addChangeBlockCommandsToGlobalList(is);

    // return number of files exported; 0 means failed
    wchar_t statusbuf[MAX_PATH + 100];
    wchar_t exportName[MAX_PATH];
    splitToPathAndName(wcharFileName, NULL, exportName);
    wsprintf(statusbuf, L"Script exporting %s", exportName);
    SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)statusbuf);

    gExported = saveObjFile(is.ws.hWnd, wcharFileName, gPrintModel, gSelectTerrainPathAndName, gSchemeSelected, false, false);
    if (gExported == 0)
    {
        SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Script export operation failed");
        swprintf_s(error, 1024, L"export operation failed.");
        return false;
    }
    // back to normal
    SendMessage(is.ws.hwndStatus, SB_SETTEXT, 0, (LPARAM)RUNNING_SCRIPT_STATUS_MESSAGE);

    SetHighlightState(1, gpEFD->minxVal, gpEFD->minyVal, gpEFD->minzVal, gpEFD->maxxVal, gpEFD->maxyVal, gpEFD->maxzVal);
    enableBottomControl(1, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, is.ws.hwndInfoBottomLabel);
    // put target depth to new depth set, if any
    if (gTargetDepth != gpEFD->maxyVal)
    {
        gTargetDepth = gpEFD->minyVal;
    }
    //gBlockLabel = IDBlock(LOWORD(gHoldlParam), HIWORD(gHoldlParam) - MAIN_WINDOW_TOP, gCurX, gCurZ,
    //	bitWidth, bitHeight, gCurScale, &mx, &my, &mz, &type, &dataVal, &biome, gWorldGuide.type == WORLD_SCHEMATIC_TYPE);
    //updateStatus(mx, mz, my, gBlockLabel, type, dataVal, biome, hwndStatus);
    setSlider(is.ws.hWnd, is.ws.hwndSlider, is.ws.hwndLabel, gCurDepth, false);
    setSlider(is.ws.hWnd, is.ws.hwndBottomSlider, is.ws.hwndBottomLabel, gTargetDepth, true);

    return true;
}

static bool openLogFile(ImportedSet & is)
{
#ifdef WIN32
    DWORD br;
#endif
    // try to open log file, and write header to it.
    // see if we can load the world
    size_t dummySize = 0;
    wchar_t wFileName[MAX_PATH];
    char outputString[256];
    mbstowcs_s(&dummySize, wFileName, (size_t)MAX_PATH, is.logFileName, MAX_PATH);
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

        errNum = asctime_s(timeString, 32, &newtime);
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

