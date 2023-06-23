// tilegrid.h - grid of image texture filenames found, and related methods

#pragma once

#define MAX_PATH_AND_FILE (2*MAX_PATH)


#define	CHANNEL_RED		0
#define	CHANNEL_GREEN	1
#define	CHANNEL_BLUE	2

#define TOTAL_CATEGORIES		10
#define	CATEGORY_RGBA			0
#define	CATEGORY_NORMALS		1
#define	CATEGORY_NORMALS_LONG	2
#define	CATEGORY_METALLIC		3
#define	CATEGORY_EMISSION		4
#define	CATEGORY_ROUGHNESS		5
#define CATEGORY_SPECULAR		6
#define CATEGORY_MER			7
#define CATEGORY_SYNTHETIC		8
#define	CATEGORY_HEIGHTMAP		9

#define FILE_NOT_FOUND			 0
#define FILE_FOUND				 1
#define FILE_FOUND_AND_DUPLICATE 2
#define FILE_FOUND_AND_IGNORED	 3

static const wchar_t* gCatSuffixes[TOTAL_CATEGORIES] = { L"", L"_n", L"_normal", L"_m", L"_e", L"_r", L"_s", L"_mer", L"_y", L"_heightmap" };
static const char* gCatStrSuffixes[TOTAL_CATEGORIES] = { "", "_n", "_normal", "_m", "_e", "_r", "_s", "_mer", "_y", "_heightmap" };

#define UNKNOWN_FILE_EXTENSION 0 
#define	PNG_EXTENSION_FOUND 1
#define	TGA_EXTENSION_FOUND	2
#define JPG_EXTENSION_FOUND 3
#define BMP_EXTENSION_FOUND	4

typedef struct FileRecord {
	wchar_t* rootName;
	wchar_t* fullFilename;
	wchar_t* path;
	bool exists;
	int alternateExtensionFound;
} FileRecord;

typedef struct FileGrid {
	int fileCount;
	int totalCategories;
	int totalTiles;
	int categories[TOTAL_CATEGORIES];	// number of files found in a category
	FileRecord fr[TOTAL_CATEGORIES * TOTAL_TILES];
} FileGrid;

static FileGrid gFG;


#define	TOTAL_CHEST_TILES	5
#define CHEST_NORMAL		0
#define CHEST_NORMAL_DOUBLE	1
#define CHEST_NORMAL_LEFT	2
#define CHEST_NORMAL_RIGHT	3
#define CHEST_ENDER			4
static const wchar_t* gChestNames[] = { L"normal", L"normal_double", L"normal_left", L"normal_right", L"ender" };
// OfTemplesAndTotems and RazzleCore use this alternate name, sigh
static const wchar_t* gChestNamesAlt[] = { L"", L"double_normal", L"", L"", L"" };

#define	TOTAL_DECORATED_POT_TILES	1
static const wchar_t* gDecoratedPotNames[] = { L"decorated_pot_base" };
// OfTemplesAndTotems and RazzleCore use this alternate name, sigh
static const wchar_t* gDecoratedPotNamesAlt[] = { L"" };

typedef struct ChestGrid {
	int chestCount;
	int totalCategories;
	int totalTiles;
	int categories[TOTAL_CATEGORIES];
	FileRecord cr[TOTAL_CATEGORIES * TOTAL_CHEST_TILES];
} ChestGrid;

static ChestGrid gCG;

typedef struct DecoratedPotGrid {
	int decoratedPotCount;
	int totalCategories;
	int totalTiles;
	int categories[TOTAL_CATEGORIES];
	FileRecord pr[TOTAL_CATEGORIES * TOTAL_DECORATED_POT_TILES];
} DecoratedPotGrid;

static DecoratedPotGrid gPG;

void initializeFileGrid(FileGrid* pfg);
void initializeChestGrid(ChestGrid* pcg);
void initializeDecoratedPotGrid(DecoratedPotGrid* ppg);
void addBackslashIfNeeded(wchar_t* dir);
int searchDirectoryForTiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid* ppg, const wchar_t* tilePath, size_t origTPLen, int verbose, int alternate, bool topmost, bool warnUnused, bool warnDups);
bool dirExists(const wchar_t* path);
bool createDir(const wchar_t* path);
int checkTilesInDirectory(FileGrid* pfg, const wchar_t* tilePath, int verbose, int alternate);
int testIfTileExists(FileGrid* pfg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose, int alternate, bool warnDNE, bool warnDups);
int testIfChestFile(ChestGrid* pcg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose);
int testIfDecoratedPotFile(DecoratedPotGrid* ppg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose);
bool removeFileType(wchar_t* name);
int isImageFile(wchar_t* name);
int stripTypeSuffix(wchar_t* tileName, const wchar_t** suffixes, int numSuffixes, FileGrid* pfg);
int findTileIndex(const wchar_t* tileName, int alternate);
void clearFileRecordStorage(FileRecord* pfr);
void copyFileRecord(FileGrid* pfg, int category, int destFullIndex, FileRecord* srcFR);
void deleteFileFromGrid(FileGrid* pfg, int category, int fullIndex);
void deleteChestFromGrid(ChestGrid* pcg, int category, int fullIndex);
void deleteDecoratedPotFromGrid(DecoratedPotGrid* ppg, int category, int fullIndex);
