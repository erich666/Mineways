// ChannelMixer : Read in a 3 channel texture and separate out the layers to 3 grayscale textures.

#include "rwpng.h"
#include "tiles.h"
#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>


// how the green and blue tiles get tinted
#define FOLIAGE_GREEN	0x8cbd57
#define WATER_BLUE		0x295dfe
#define REDSTONE_RED	0xd60000

#define INC_AND_TEST_ARG_INDEX( loc )		argLoc++; \
											if (argLoc == argc) { \
												printHelp(); \
												return 1; \
											}

typedef struct Channel {
	boolean	inUse;
	wchar_t suffix[MAX_PATH];
} Channel;

#define	CHANNEL_RED		0
#define	CHANNEL_GREEN	1
#define	CHANNEL_BLUE	2

#define TOTAL_INPUT_TILES	1000

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

static const wchar_t* gCatSuffixes[TOTAL_CATEGORIES] = { L"", L"_n", L"_normal", L"_m", L"_e", L"_r", L"_s", L"_mer", L"_y" };
static const char* gCatStrSuffixes[TOTAL_CATEGORIES] = { "", "_n", "_normal", "_m", "_e", "_r", "_s", "_mer", "_y" };

typedef struct Tile {
	wchar_t name[MAX_PATH];
	progimage_info image;
} Tile;

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
	int types[TOTAL_CATEGORIES];
	FileRecord fr[TOTAL_CATEGORIES * TOTAL_TILES];
} FileGrid;

static FileGrid gFG;

#define MAX_PATH_AND_FILE (2*MAX_PATH)

static int gErrorCount = 0;
static int gWarningCount = 0;

static wchar_t gErrorString[MAX_PATH];
// 1000 errors of 100 characters each - sounds sufficient
#define CONCAT_ERROR_LENGTH	(1000*100)
static wchar_t gConcatErrorString[CONCAT_ERROR_LENGTH];

void initializeFileGrid(FileGrid* pfg);
bool dirExists(const wchar_t* path);
int checkTilesInDirectory(FileGrid* pfg, const wchar_t* tilePath, int verbose, int alternate);
boolean testIfTileExists(FileGrid* fg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose, int alternate);
boolean removePNGsuffix(wchar_t* name);
int stripTypeSuffix(wchar_t* tileName, const wchar_t** suffixes, int numSuffixes);
int findTileIndex(const wchar_t* tileName, int alternate);
progimage_info* allocateImage(progimage_info* source_ptr);


void printHelp();
//int readTilesInDirectory(const wchar_t* inputDirectory, const wchar_t* inputSuffix, int& tilesFound, Tile* tiles, boolean verbose);
//void loadAndProcessTile(const wchar_t* inputDirectory, const wchar_t* inputSuffix, const wchar_t* inputFile, int& tilesFound, Tile* tiles, boolean verbose);
//static int buildPathAndReadTile(const wchar_t* tilePath, const wchar_t* fileName, int& tilesFound, Tile* tiles, const wchar_t* tileRootName);
//static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, boolean verbose);
static void reportReadError(int rc, const wchar_t* filename);
static void saveErrorForEnd();

void copyFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);
void processSpecularFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);
void processMERFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);

static int isChannelAllBlack(progimage_info* src, int channel);
static int isNearlyGrayscale(progimage_info* src);
static void copyOneChannel(progimage_info* dst, int channel, progimage_info* src);
static void invertChannel(progimage_info* dst);


int wmain(int argc, wchar_t* argv[])
{
	int argLoc = 1;
	int i;
	wchar_t inputSuffix[MAX_PATH] = L"_mer";
	wchar_t inputDirectory[MAX_PATH] = L"";
	wchar_t outputDirectory[MAX_PATH] = L"";
	wchar_t synthOutSuffix[MAX_PATH] = L"_y";	// _y.png is synthesized (composited) textures
	wchar_t negateSuffix[MAX_PATH] = L"";
	static Channel outChannel[3];
	boolean verbose = false;
	//boolean splitFiles = false;
	//boolean createLeaves = false;
	//boolean negateChannel = false;

	initializeFileGrid(&gFG);

	// two means even try the alternate name list
	int alternate = 2;
	Tile tiles[TOTAL_INPUT_TILES];

	// by default assume _mer input and _m/_e/_r output PNGs
	for (i = 0; i < 3; i++) {
		outChannel[i].inUse = true;
	}
	wcscpy_s(outChannel[CHANNEL_RED].suffix, MAX_PATH, L"_m");
	wcscpy_s(outChannel[CHANNEL_GREEN].suffix, MAX_PATH, L"_e");
	wcscpy_s(outChannel[CHANNEL_BLUE].suffix, MAX_PATH, L"_r");

	while (argLoc < argc)
	{
		if (wcscmp(argv[argLoc], L"-i") == 0)
		{
			// input directory
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(inputDirectory, MAX_PATH, argv[argLoc]);
		}
		else if (wcscmp(argv[argLoc], L"-o") == 0)
		{
			// output directory
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(outputDirectory, MAX_PATH, argv[argLoc]);
		}
		/*
		else if (wcscmp(argv[argLoc], L"-y") == 0)
		{
			// create sYnthesized color composited leaves, grass, water, etc. - suffix is "_y"
			createLeaves = true;
		}
		else if (wcscmp(argv[argLoc], L"-im") == 0)
		{
			// set Input suffix for Merged file, such as -im _mer
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(inputSuffix, MAX_PATH, argv[argLoc]);
			splitFiles = true;
		}
		else if (wcscmp(argv[argLoc], L"-oc") == 0)
		{
			// set custom Output suffixes for Separate files
			for (i = 0; i < 3; i++) {
				INC_AND_TEST_ARG_INDEX(argLoc);
				wcscpy_s(outChannel[i].suffix, MAX_PATH, argv[argLoc]);
				outChannel[i].inUse = true;
			}
		}
		else if (wcscmp(argv[argLoc], L"-n") == 0)
		{
			// negate a channel being output - typically the roughness or specular channel.
			// The suffix must match one of the output suffixes.
			// Typical usage: -n _s
			// Really, this inverts the channel, but "-i" is already in use.
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(negateSuffix, MAX_PATH, argv[argLoc]);
			negateChannel = true;
		}
		else if (wcscmp(argv[argLoc], L"-oy") == 0)
		{
			// create sYnthesized color composited leaves, grass, water, etc., with custom suffix given
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(synthOutSuffix, MAX_PATH, argv[argLoc]);
			createLeaves = true;
		}*/
		else if (wcscmp(argv[argLoc], L"-v") == 0)
		{
			// verbose: tell when normal things happen
			verbose = true;
		}
		else
		{
			printHelp();
			return 1;
		}
		argLoc++;
	}

	if (verbose)
		wprintf(L"ChannelMixer version 1.00\n");  // change version above, too

	if (!dirExists(inputDirectory)) {
		wprintf(L"\nERROR: Input directory '%s' does not exist.\n", inputDirectory);
		return 1;
	}

	// from https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	if (CreateDirectoryW(outputDirectory, NULL) ||
		ERROR_ALREADY_EXISTS != GetLastError()) {
		wprintf(L"\nERROR: Output directory '%s' could not be created.\n", outputDirectory);
		return 1;
	}

	// add \ to end of directory paths
	if (wcscmp(&inputDirectory[wcslen(inputDirectory) - 1], L"\\") != 0)
	{
		wcscat_s(inputDirectory, MAX_PATH, L"\\");
	}
	if (wcscmp(&outputDirectory[wcslen(outputDirectory) - 1], L"\\") != 0)
	{
		wcscat_s(outputDirectory, MAX_PATH, L"\\");
	}

	// How it works:
	// Look through all directories for files, warn of duplicates that get ignored. TODO: just have one directory now
	// Copy files over if output directory is different than input directory.
	// Split _mer and _s files into _m _e _r files.

	boolean sameDir = (_wcsicmp(inputDirectory, outputDirectory) == 0);

	// look through tiles in tiles directory, see which exist
	if (checkTilesInDirectory(&gFG, inputDirectory, verbose, alternate) == 0) {
		wprintf(L"\nERROR: Input directory '%s' does not have any Minecraft image textures of interest - nothing to do.\n", inputDirectory);
		return 1;
	}

	// See what we need to do. If sameDir is false, we need to copy over all color and normals files.
	// Work we always do: look at _s files and _mer files and split out.
	// _s files might be grayscale (roughness only) or SME.
	if (!sameDir) {
		copyFiles(&gFG, outputDirectory, verbose);
	}

	if (gFG.types[CATEGORY_SPECULAR] > 0) {
		processSpecularFiles(&gFG, outputDirectory, verbose);
	}

	if (gFG.types[CATEGORY_MER] > 0) {
		processMERFiles(&gFG, outputDirectory, verbose);
	}

	//if (createLeaves) {
	//	createCompositedLeaves(inputDirectory, outputDirectory, synthOutSuffix, verbose);
	//}

	if (gErrorCount)
		wprintf(L"\nERROR SUMMARY:\n%s\n", gConcatErrorString);

	if (gErrorCount || gWarningCount)
		wprintf(L"Summary: %d error%S and %d warning%S were generated.\n", gErrorCount, (gErrorCount == 1) ? "" : "s", gWarningCount, (gWarningCount == 1) ? "" : "s");

	return 0;
}

void printHelp()
{
	wprintf(L"ChannelMixer version 1.00\n");  // change version below, too
	wprintf(L"usage: ChannelMixer [-i inputTexturesDirectory] [-o outputTexturesDirectory]\n"); // [-im _mer] [-oc _m _e _r] \n");
	wprintf(L"  -i inputTexturesDirectory - directory of textures to search and process.\n        If none given, current directory.\n");
	wprintf(L"  -o outputTexturesDirectory - directory where resulting textures will go.\n        If none given, current directory.\n");
	//wprintf(L"  -y - create composite colored tiles, such as grass block sides, for reuse.\n      Suffix of output files is '*_y.png'.\n");
	//wprintf(L"  -im _mer - the merged input file suffix, e.g., 'anvil_mer.png'.\n");
	//wprintf(L"  -oc _m _e _r - the output channel file suffixes, e.g., 'anvil_m.png',\n        'anvil_e.png', 'anvil_r.png' are produced from the RGB input channels.\n");
	//wprintf(L"  -n _r - iNvert the output _r channel (making it shininess, or vice versa).\n");
	//wprintf(L"  -oy _y - create composite colored tiles, with given suffix ('_y' is default).\n");
	wprintf(L"  -v - verbose, explain everything going on. Default: display only warnings.\n");
}

void initializeFileGrid(FileGrid* pfg)
{
	int i;
	pfg->fileCount = 0;
	pfg->totalCategories = TOTAL_CATEGORIES;
	pfg->totalTiles = TOTAL_TILES;
	for (i = 0; i < TOTAL_CATEGORIES; i++) {
		pfg->types[i] = 0;
	}
	for (i = 0; i < TOTAL_CATEGORIES * TOTAL_TILES; i++) {
		pfg->fr[i].rootName = NULL;
		pfg->fr[i].fullFilename = NULL;
		pfg->fr[i].path = NULL;
		pfg->fr[i].exists = false;
	}
}

// from https://stackoverflow.com/questions/8233842/how-to-check-if-directory-exist-using-c-and-winapi
bool dirExists(const wchar_t* path)
{
	DWORD ftyp = GetFileAttributesW(path);
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false;  //something is wrong with your path!

	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;   // this is a directory!

	return false;    // this is not a directory!
}

// returns number of useful tiles found
int checkTilesInDirectory(FileGrid* pfg, const wchar_t* tilePath, int verbose, int alternate)
{
	HANDLE hFind;
	WIN32_FIND_DATA ffd;
	pfg->fileCount = 0;

	wchar_t tileSearch[MAX_PATH];
	wcscpy_s(tileSearch, MAX_PATH, tilePath);
	wcscat_s(tileSearch, MAX_PATH, L"*.png");
	hFind = FindFirstFile(tileSearch, &ffd);

	if (hFind != INVALID_HANDLE_VALUE)
	{
		// go through all the files in the blocks directory
		do {
			pfg->fileCount += testIfTileExists( pfg, tilePath, ffd.cFileName, verbose, alternate) ? 1 : 0;
		} while (FindNextFile(hFind, &ffd) != 0);

		FindClose(hFind);
	}
	return pfg->fileCount;
}

// returns true if file exists and is usable (not a duplicate, alternate name of something already in use)
boolean testIfTileExists(FileGrid* fg, const wchar_t* tilePath, const wchar_t* origTileName, int verbose, int alternate)
{
	wchar_t tileName[MAX_PATH];

	if (verbose)
		wprintf(L"The file '%s' has been found and will be tested to see if it's needed.\n", origTileName);

	wcscpy_s(tileName, MAX_PATH, origTileName);

	if (removePNGsuffix(tileName)) {
		// has a PNG suffix, now removed, so test if it's a file name type we understand.
		int type = stripTypeSuffix(tileName, gCatSuffixes, TOTAL_CATEGORIES);
		assert(type >= 0);
		// return a negative value if tile is not found in any way
		int index = findTileIndex(tileName, alternate);
		if (index >= 0) {
			int fullIndex = type * fg->totalTiles + index;
			if (fg->fr[fullIndex].exists) {
				// duplicate, so warn and exit
				wprintf(L"WARNING: duplicate file ignored. File '%s' in directory '%s' is a different name for the same texture '%s' in '%s'.\n", origTileName, tilePath, fg->fr[fullIndex].fullFilename, fg->fr[fullIndex].path);
				return false;
			}
			else {
				// it's new and unique
				fg->types[type]++;
				fg->fr[fullIndex].rootName = _wcsdup(tileName);
				fg->fr[fullIndex].fullFilename = _wcsdup(origTileName);
				fg->fr[fullIndex].path = _wcsdup(tilePath);
				fg->fr[fullIndex].exists = true;
				return true;
			}
		}
	}
	if (verbose)
		wprintf(L"The file '%s' is one that is not needed.\n", origTileName);
	return false;
}

boolean removePNGsuffix(wchar_t* name)
{
	// check for .png suffix - note test is case insensitive
	int len = (int)wcslen(name);
	if (len > 4 && _wcsicmp(&name[len - 4], L".png") == 0)
	{
		// remove .png suffix
		name[len - 4] = 0x0;
		return true;
	}
	return false;
}

// return -1 if no suffix matches
int stripTypeSuffix(wchar_t* tileName, const wchar_t** suffixes, int numSuffixes)
{
	int type = -1;
	for (int i = 0; i < numSuffixes; i++) {
		int suffixLen = (int)wcslen(suffixes[i]);
		if (suffixLen > 0) {
			int len = (int)wcslen(tileName);
			if (_wcsicmp(&tileName[len - suffixLen], suffixes[i]) == 0) {
				// now for the annoying exceptions
				int j = 0;
				bool stripSuffix = true;
				while (wcslen(gNormalsList[j]) > 0) {
					if (_wcsicmp(tileName, gNormalsList[j]) == 0) {
						// doesn't count - the name has "_normal" at the end of it.
						stripSuffix = false;
						type = 0;	// double-triple sure
						break;
					}
					j++;
				}
				if (stripSuffix) {
					tileName[len - suffixLen] = 0x0;
				}
				type = i;
				break;
			}
		}
		else {
			// normally type of first "" suffix is 0, but just in case...
			// and that means we can return -1 if no suffix is found, by feeding in a different list of suffixes.
			type = i;
		}
	}
	return type;
}

int findTileIndex(const wchar_t* tileName, int alternate)
{
	int i;
	int index = -1;

	for (i = 0; i < TOTAL_TILES; i++)
	{
		if (_wcsicmp(tileName, gTilesTable[i].filename) == 0) {
			return i;
		}
		if (alternate && _wcsicmp(tileName, gTilesTable[i].altFilename) == 0) {
			return i;
		}
	}

	// none of those worked, so now try some more rules - good times!
	if ( alternate > 1) {
		i = 0;
		while (wcslen(gTilesAlternates[i].filename) > 0) {
			if (_wcsicmp(tileName, gTilesAlternates[i].altFilename) == 0) {
				// tricksy - search only the normal names to find the index of this alternate name
				return findTileIndex(gTilesAlternates[i].filename, 1);
			}
			i++;
		}
	}

	return index;
}

void copyFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {

	int copyTypes[] = { CATEGORY_RGBA, CATEGORY_NORMALS, CATEGORY_NORMALS_LONG, CATEGORY_METALLIC, CATEGORY_EMISSION, CATEGORY_ROUGHNESS };
	int numTypes = sizeof(copyTypes) / sizeof(int);
	for (int type = 0; type < numTypes; type++) {
		if (pfg->types[copyTypes[type]] > 0) {
			// there are files to copy
			for (int i = 0; i < pfg->totalTiles; i++) {
				int fullIndex = copyTypes[type] * pfg->totalTiles + i;
				if (pfg->fr[fullIndex].exists) {
					// file exists - copy it.
					wchar_t inputFile[MAX_PATH_AND_FILE];
					wcscpy_s(inputFile, MAX_PATH, pfg->fr[fullIndex].path);
					wcscat_s(inputFile, MAX_PATH, L"\\");
					wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].fullFilename);

					wchar_t outputFile[MAX_PATH_AND_FILE];
					wcscpy_s(outputFile, MAX_PATH, outputDirectory);
					wcscat_s(outputFile, MAX_PATH, L"\\");
					wcscat_s(outputFile, MAX_PATH, pfg->fr[fullIndex].fullFilename);

					if (CopyFile(inputFile, outputFile, true) == 0) {
						wsprintf(gErrorString, L"***** ERROR: file '%s' could not be copied to '%s'.\n", inputFile, outputFile);
						saveErrorForEnd();
						gErrorCount++;
					}
					else if (verbose) {
						wprintf(L"Texture '%s' copied to '%s'.\n", inputFile, outputFile);
					}

					if (type == CATEGORY_NORMALS_LONG) {
						int otherIndex = copyTypes[CATEGORY_NORMALS] * pfg->totalTiles + i;
						if (pfg->fr[otherIndex].exists) {
							wsprintf(gErrorString, L"WARNING: file '%s' also has a version named '%s_n.png'.\n", pfg->fr[fullIndex].fullFilename, pfg->fr[otherIndex].fullFilename);
						}
					}
				}
			}
		}
	}
}

void processSpecularFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {
	int rc;

	// SME order, inverting the red channel:
	int category[] = { CATEGORY_ROUGHNESS, CATEGORY_METALLIC, CATEGORY_EMISSION };

	for (int i = 0; i < pfg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * pfg->totalTiles + i;
		if (pfg->fr[fullIndex].exists) {
			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wchar_t inputFile[MAX_PATH_AND_FILE];
			wcscpy_s(inputFile, MAX_PATH, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH, L"\\");
			wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].fullFilename);

			// read in tile for later
			progimage_info tile;
			rc = readpng(&tile, inputFile, LCT_RGB);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
			}

			wchar_t outputFile[MAX_PATH_AND_FILE];
			if (isNearlyGrayscale(&tile)) {
				// specular only: output just the roughness channel
				// always export specular in inverted
				progimage_info* destination_ptr = allocateImage(&tile);
				copyOneChannel(destination_ptr, CHANNEL_RED, &tile);
				invertChannel(destination_ptr);

				wcscpy_s(outputFile, MAX_PATH, outputDirectory);
				wcscat_s(inputFile, MAX_PATH, L"\\");
				wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].rootName);
				wcscat_s(inputFile, MAX_PATH, gCatSuffixes[CATEGORY_ROUGHNESS]);
				wcscat_s(inputFile, MAX_PATH, L".png");

				rc = writepng(destination_ptr, 1, outputFile);
				if (rc != 0)
				{
					reportReadError(rc, outputFile);
					// quit - if we can't write one file, we're unlikely to write the rest.
					return;
				}
				writepng_cleanup(destination_ptr);
				if (verbose)
					wprintf(L"New texture '%s' created.\n", outputFile);
			}
			else {
				// SME: output all three
				for (i = 0; i < 3; i++) {
					// output the channel if it's not all black
					if (!isChannelAllBlack(&tile, i)) {
						progimage_info* destination_ptr = allocateImage(&tile);
						copyOneChannel(destination_ptr, i, &tile);
						if (i == CHANNEL_RED) {
							invertChannel(destination_ptr);
						}

						wcscpy_s(outputFile, MAX_PATH, outputDirectory);
						wcscat_s(inputFile, MAX_PATH, L"\\");
						wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].rootName);
						wcscat_s(inputFile, MAX_PATH, gCatSuffixes[category[i]]);
						wcscat_s(inputFile, MAX_PATH, L".png");

						rc = writepng(destination_ptr, 1, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit - if we can't write one file, we're unlikely to write the rest.
							return;
						}
						writepng_cleanup(destination_ptr);
						if (verbose)
							wprintf(L"New texture '%s' created.\n", outputFile);
					}
				}
			}
			readpng_cleanup(1, &tile);
		}
	}
}

progimage_info* allocateImage(progimage_info* source_ptr)
{
	// allocate output image and fill it up
	progimage_info* destination_ptr = new progimage_info();

	destination_ptr->width = source_ptr->width;
	destination_ptr->height = source_ptr->height;
	destination_ptr->image_data.resize(destination_ptr->width * destination_ptr->height * 1 * sizeof(unsigned char), 0x0);

	return destination_ptr;
}

void processMERFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {
	int rc;

	// MER order
	int category[] = { CATEGORY_METALLIC, CATEGORY_EMISSION, CATEGORY_ROUGHNESS };

	for (int i = 0; i < pfg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * pfg->totalTiles + i;
		if (pfg->fr[fullIndex].exists) {
			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wchar_t inputFile[MAX_PATH_AND_FILE];
			wcscpy_s(inputFile, MAX_PATH, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH, L"\\");
			wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].fullFilename);

			// read in tile
			progimage_info tile;
			rc = readpng(&tile, inputFile, LCT_RGB);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
			}

			wchar_t outputFile[MAX_PATH_AND_FILE];

			// MER: output all three
			for (i = 0; i < 3; i++) {
				// output the channel if it's not all black
				if (!isChannelAllBlack(&tile, i)) {
					progimage_info* destination_ptr = allocateImage(&tile);
					copyOneChannel(destination_ptr, i, &tile);

					wcscpy_s(outputFile, MAX_PATH, outputDirectory);
					wcscat_s(inputFile, MAX_PATH, L"\\");
					wcscat_s(inputFile, MAX_PATH, pfg->fr[fullIndex].rootName);
					wcscat_s(inputFile, MAX_PATH, gCatSuffixes[category[i]]);
					wcscat_s(inputFile, MAX_PATH, L".png");

					rc = writepng(destination_ptr, 1, outputFile);
					if (rc != 0)
					{
						reportReadError(rc, outputFile);
						// quit - if we can't write one file, we're unlikely to write the rest.
						return;
					}
					writepng_cleanup(destination_ptr);
					if (verbose)
						wprintf(L"New texture '%s' created.\n", outputFile);
				}
			}

			readpng_cleanup(1, &tile);
		}
	}
}

/*
int readTilesInDirectory(const wchar_t* inputDirectory, const wchar_t* inputSuffix, int& tilesFound, Tile* tiles, boolean verbose)
{
	HANDLE hFind;
	WIN32_FIND_DATA ffd;

	wchar_t tileSearch[MAX_PATH];
	wcscpy_s(tileSearch, MAX_PATH, inputDirectory);
	wcscat_s(tileSearch, MAX_PATH, L"*");
	wcscat_s(tileSearch, MAX_PATH, inputSuffix);
	wcscat_s(tileSearch, MAX_PATH, L".png");
	hFind = FindFirstFile(tileSearch, &ffd);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		wsprintf(gErrorString, L"***** ERROR: cannot find files (Windows error code # %d).\n", GetLastError());
		saveErrorForEnd();
		gErrorCount++;
		wsprintf(gErrorString, L"  No textures found in the directory '%s'.\n", inputDirectory);
		saveErrorForEnd();
		return 0;
	}
	else
	{
		// go through all the files in the blocks directory
		do {
			loadAndProcessTile(inputDirectory, inputSuffix, ffd.cFileName, tilesFound, tiles, verbose);
		} while (FindNextFile(hFind, &ffd) != 0);

		FindClose(hFind);
	}
	return 0;
}

void loadAndProcessTile(const wchar_t* inputDirectory, const wchar_t* inputSuffix, const wchar_t* inputFile, int& tilesFound, Tile *tiles, boolean verbose)
{
	wchar_t tileRootName[MAX_PATH];
	wchar_t endOfName[MAX_PATH];
	int start;

	if (verbose)
		wprintf(L"The file '%s' has been found and will be processed.\n", inputFile);

	if (tilesFound >= TOTAL_INPUT_TILES) {
		wsprintf(gErrorString, L"INTERNAL ERROR: the number of (unused) tiles is extremely high - please delete PNGs not needed and run again.\n");
		saveErrorForEnd();
		gErrorCount++;
		return;
	}

	wcscpy_s(tileRootName, MAX_PATH, inputFile);
	wcscpy_s(endOfName, MAX_PATH, inputSuffix);
	wcscat_s(endOfName, MAX_PATH, L".png");
	// check for suffix.png suffix - note test is case insensitive
	start = (int)(wcslen(tileRootName) - wcslen(endOfName));
	if (_wcsicmp(&tileRootName[start], endOfName) == 0)
	{
		// remove suffix.png suffix
		tileRootName[start] = 0x0;
		int fail_code = buildPathAndReadTile(inputDirectory, inputFile, tilesFound, tiles, tileRootName);

		if (fail_code) {
			wsprintf(gErrorString, L"ERROR: could not read file '%s' in directory '%s'.\n", inputFile, inputDirectory);
			saveErrorForEnd();
			gErrorCount++;
			return;
		}
	}
}

static int buildPathAndReadTile(const wchar_t* tilePath, const wchar_t* fileName, int& tilesFound, Tile* tiles, const wchar_t* tileRootName)
{
	int fail_code = 0;
	// read image file - build path
	wchar_t readFileName[MAX_PATH];
	wcscpy_s(readFileName, MAX_PATH, tilePath);
	wcscat_s(readFileName, MAX_PATH, fileName);
	// read in tile for later
	int rc = readpng(&tiles[tilesFound].image, readFileName, LCT_RGB);
	if (rc != 0)
	{
		reportReadError(rc, readFileName);
		fail_code = 1;
	}
	readpng_cleanup(0, &tiles[tilesFound].image);
	wcscpy_s(tiles[tilesFound].name, MAX_PATH, tileRootName);
	tilesFound++;
	return fail_code;
}
*/

/*
static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, boolean verbose)
{
	int rc = 0;
	int i;
	wchar_t tileSearch[MAX_PATH];

	// color and composite grass side overlay atop grass side
	progimage_info* grass_overlay = new progimage_info();
	progimage_info* grass_side = new progimage_info();

	wcscpy_s(tileSearch, MAX_PATH, inputDirectory);
	wcscat_s(tileSearch, MAX_PATH, L"grass_block_side_overlay.png");
	rc = readpng(grass_overlay, tileSearch, LCT_RGBA);
	if (rc == 0)
	{
		// continue, get grass underlay
		wcscpy_s(tileSearch, MAX_PATH, inputDirectory);
		wcscat_s(tileSearch, MAX_PATH, L"grass_block_side.png");
		rc = readpng(grass_side, tileSearch, LCT_RGB);
		if (rc != 0) {
			// try getting dirt instead
			wcscpy_s(tileSearch, MAX_PATH, inputDirectory);
			wcscat_s(tileSearch, MAX_PATH, L"dirt.png");
			rc = readpng(grass_side, tileSearch, LCT_RGB);
		}
		if (rc == 0) {
			// OK, color the grass and composite it.
			multiplyPNGTile(grass_overlay, 4, FOLIAGE_GREEN);
			compositePNGTiles(grass_side, grass_overlay);
			wcscpy_s(tileSearch, MAX_PATH, outputDirectory);
			wcscat_s(tileSearch, MAX_PATH, L"grass_block_side");
			wcscat_s(tileSearch, MAX_PATH, outputSuffix);
			wcscat_s(tileSearch, MAX_PATH, L".png");
			writepng(grass_side, 3, tileSearch);
			writepng_cleanup(grass_side);
			readpng_cleanup(1, grass_overlay);
			if (verbose)
				wprintf(L"New texture '%s' created.\n", tileSearch);
		}
	}

	typedef struct TypeTile {
		const wchar_t* name;
		unsigned int tint;
	} TypeTile;

#define MULT_TABLE_SIZE 27
	TypeTile tintTable[MULT_TABLE_SIZE] = {
		{ L"grass_block_top", FOLIAGE_GREEN },
		{ L"grass", FOLIAGE_GREEN },
		{ L"fern", FOLIAGE_GREEN },
		{ L"tall_grass_top", FOLIAGE_GREEN },
		{ L"tall_grass_bottom", FOLIAGE_GREEN },
		{ L"large_fern_top", FOLIAGE_GREEN },
		{ L"large_fern_bottom", FOLIAGE_GREEN },
		{ L"oak_leaves", FOLIAGE_GREEN },
		{ L"birch_leaves", FOLIAGE_GREEN },
		{ L"spruce_leaves", FOLIAGE_GREEN },
		{ L"jungle_leaves", FOLIAGE_GREEN },
		{ L"acacia_leaves", FOLIAGE_GREEN },
		{ L"dark_oak_leaves", FOLIAGE_GREEN },
		{ L"lily_pad", FOLIAGE_GREEN },
		{ L"pumpkin_stem", FOLIAGE_GREEN },
		{ L"attached_pumpkin_stem", FOLIAGE_GREEN },
		{ L"melon_stem", FOLIAGE_GREEN },
		{ L"attached_melon_stem", FOLIAGE_GREEN },
		{ L"vine", FOLIAGE_GREEN },
		{ L"water_still", WATER_BLUE },
		{ L"water_overlay", WATER_BLUE },
		{ L"water_flow", WATER_BLUE },
		{ L"water_flow", WATER_BLUE },
		{ L"water_flow", WATER_BLUE },
		{ L"redstone_dust_dot", REDSTONE_RED },
		{ L"redstone_dust_line0", REDSTONE_RED },
		{ L"redstone_dust_line1", REDSTONE_RED },
	};	// TODO: handle redstone wires, ugh

	for (i = 0; i < MULT_TABLE_SIZE; i++) {
		progimage_info* tile = new progimage_info();

		wcscpy_s(tileSearch, MAX_PATH, inputDirectory);
		wcscat_s(tileSearch, MAX_PATH, tintTable[i].name);
		wcscat_s(tileSearch, MAX_PATH, L".png");
		rc = readpng(tile, tileSearch, LCT_RGBA);
		if (rc == 0)
		{
			// OK, tint it and output it
			multiplyPNGTile(tile, 4, tintTable[i].tint);
			int channels = compressPNGTile(tile);
			wcscpy_s(tileSearch, MAX_PATH, outputDirectory);
			wcscat_s(tileSearch, MAX_PATH, tintTable[i].name);
			wcscat_s(tileSearch, MAX_PATH, outputSuffix);
			wcscat_s(tileSearch, MAX_PATH, L".png");
			writepng(tile, channels, tileSearch);
			writepng_cleanup(tile);
			if (verbose)
				wprintf(L"New texture '%s' created.\n", tileSearch);
		}
	}
}
*/

static void reportReadError(int rc, const wchar_t* filename)
{
	switch (rc) {
	case 1:
		wsprintf(gErrorString, L"***** ERROR [%s] is not a PNG file: incorrect signature.\n", filename);
		break;
	case 2:
		wsprintf(gErrorString, L"***** ERROR [%s] has bad IHDR (libpng longjmp).\n", filename);
		break;
	case 4:
		wsprintf(gErrorString, L"***** ERROR [%s] read failed - insufficient memory.\n", filename);
		break;
	case 63:
		wsprintf(gErrorString, L"***** ERROR [%s] read failed - chunk too long.\n", filename);
		break;
	case 78:
		wsprintf(gErrorString, L"***** ERROR [%s] read failed - file not found or could not be read.\n", filename);
		break;
	default:
		wsprintf(gErrorString, L"***** ERROR [%s] read failed - unknown readpng_init() error.\n", filename);
		break;
	}
	saveErrorForEnd();
	gErrorCount++;

	if (rc != 78) {
		wsprintf(gErrorString, L"Often this means the PNG file has some small bit of information that ChannelMixer cannot\n  handle. You might be able to fix this error by opening this PNG file in\n  Irfanview or other viewer and then saving it again. This has been known to clear\n  out any irregularity that ChannelMixer's somewhat-fragile PNG reader dies on.\n");
	}
	saveErrorForEnd();
}

static void saveErrorForEnd()
{
	wprintf(gErrorString);
	wcscat_s(gConcatErrorString, L"  ");
	wcscat_s(gConcatErrorString, gErrorString);
}

//================================ Image Manipulation ====================================

// meant for RGBA only
static int isChannelAllBlack(progimage_info* src, int channel)
{
	// look at all data: black?
	int row, col;
	unsigned char* src_data = &src->image_data[0] + channel;
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (*src_data != 0)
			{
				return 0;
			}
			src_data += 3;
		}
	}
	return 1;
};

static int isNearlyGrayscale(progimage_info* src)
{
	// if R = G = B +- grayEpsilon for whole image, this is a grayscale image
	int grayEpsilon = 2;

	int row, col;
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (abs(src_data[0] - src_data[1]) > grayEpsilon ||
				abs(src_data[1] - src_data[2]) > grayEpsilon )
			{
				return 0;
			}
			src_data += 3;
		}
	}
	return 1;
};

static void copyOneChannel(progimage_info* dst, int channel, progimage_info* src)
{
	int row, col;
	dst->width = src->width;
	dst->height = src->height;
	unsigned char* dst_data = &dst->image_data[0];
	unsigned char* src_data = &src->image_data[0] + channel;
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			*dst_data++ = *src_data;
			src_data += 3;
		}
	}
}

static void invertChannel(progimage_info* dst)
{
	int row, col;
	unsigned char* dst_data = &dst->image_data[0];
	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			*dst_data++ = 255 - *dst_data;
		}
	}
}
