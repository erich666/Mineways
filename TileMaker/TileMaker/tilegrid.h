// tilegrid.h - grid of image texture filenames found, and related methods

#pragma once

#define	CHANNEL_RED		0
#define	CHANNEL_GREEN	1
#define	CHANNEL_BLUE	2

#define TOTAL_CATEGORIES		9
#define	CATEGORY_RGBA			0
#define	CATEGORY_NORMALS		1
#define	CATEGORY_NORMALS_LONG	2
#define	CATEGORY_METALLIC		3
#define	CATEGORY_EMISSION		4
#define	CATEGORY_ROUGHNESS		5
#define CATEGORY_SPECULAR		6
#define CATEGORY_MER			7
#define CATEGORY_SYNTHETIC		8

#define	FILE_NOT_FOUND			0
#define FILE_FOUND				1
#define FILE_FOUND_AND_IGNORED	2

static const wchar_t* gCatSuffixes[TOTAL_CATEGORIES] = { L"", L"_n", L"_normal", L"_m", L"_e", L"_r", L"_s", L"_mer", L"_y" };
static const char* gCatStrSuffixes[TOTAL_CATEGORIES] = { "", "_n", "_normal", "_m", "_e", "_r", "_s", "_mer", "_y" };

typedef struct FileRecord {
	wchar_t* rootName;
	wchar_t* fullFilename;
	wchar_t* path;
	boolean exists;
} FileRecord;

typedef struct FileGrid {
	int fileCount;
	int totalCategories;
	int totalTiles;
	int categories[TOTAL_CATEGORIES];	// number of files found in a category
	FileRecord fr[TOTAL_CATEGORIES * TOTAL_TILES];
} FileGrid;

static FileGrid gFG;

void initializeFileGrid(FileGrid* pfg);
void addBackslashIfNeeded(wchar_t* dir);
bool dirExists(const wchar_t* path);
int checkTilesInDirectory(FileGrid* pfg, const wchar_t* tilePath, int verbose, int alternate);
int testIfTileExists(FileGrid* fg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose, int alternate, boolean warnDNE);
boolean removePNGsuffix(wchar_t* name);
boolean isPNGfile(wchar_t* name);
int stripTypeSuffix(wchar_t* tileName, const wchar_t** suffixes, int numSuffixes);
int findTileIndex(const wchar_t* tileName, int alternate);
void copyFileRecord(FileGrid* pfg, int category, int fullIndex, FileRecord* srcFR);
void deleteFileFromGrid(FileGrid* pfg, int category, int fullIndex);
