// ChannelMixer : Read in a 3 channel texture and separate out the layers to 3 grayscale textures.

#include "rwpng.h"
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

typedef struct Tile {
	wchar_t name[MAX_PATH];
	progimage_info image;
} Tile;



static int gErrorCount = 0;
static int gWarningCount = 0;

static wchar_t gErrorString[MAX_PATH];
// 1000 errors of 100 characters each - sounds sufficient
#define CONCAT_ERROR_LENGTH	(1000*100)
static wchar_t gConcatErrorString[CONCAT_ERROR_LENGTH];


void printHelp();
int readTilesInDirectory(const wchar_t* inputDirectory, const wchar_t* inputSuffix, int& tilesFound, Tile* tiles, boolean verbose);
void loadAndProcessTile(const wchar_t* inputDirectory, const wchar_t* inputSuffix, const wchar_t* inputFile, int& tilesFound, Tile* tiles, boolean verbose);
static int buildPathAndReadTile(const wchar_t* tilePath, const wchar_t* fileName, int& tilesFound, Tile* tiles, const wchar_t* tileRootName);
static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, boolean verbose);
static void reportReadError(int rc, const wchar_t* filename);
static void saveErrorForEnd();

static int isChannelAllBlack(progimage_info* src, int channel);
static void copyOneChannel(progimage_info* dst, int channel, progimage_info* src);
static void invertChannel(progimage_info* dst);
static void multiplyPNGTile(progimage_info* dst, int channels, unsigned int color);
static void compositePNGTiles(progimage_info* dst, progimage_info* src);
static int compressPNGTile(progimage_info* dst);


int wmain(int argc, wchar_t* argv[])
{
	int argLoc = 1;
	int i, it;
	wchar_t inputSuffix[MAX_PATH] = L"_mer";
	wchar_t inputDirectory[MAX_PATH] = L"";
	wchar_t outputDirectory[MAX_PATH] = L"";
	wchar_t synthOutSuffix[MAX_PATH] = L"_y";	// _y.png is synthesized (composited) textures
	wchar_t negateSuffix[MAX_PATH] = L"";
	static Channel outChannel[3];
	boolean verbose = false;
	boolean splitFiles = false;
	boolean createLeaves = false;
	boolean negateChannel = false;

	int tilesFound = 0;
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
		}
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

	// add \ to end of directory paths
	if (wcscmp(&inputDirectory[wcslen(inputDirectory) - 1], L"\\") != 0)
	{
		wcscat_s(inputDirectory, MAX_PATH, L"\\");
	}
	if (wcscmp(&outputDirectory[wcslen(outputDirectory) - 1], L"\\") != 0)
	{
		wcscat_s(outputDirectory, MAX_PATH, L"\\");
	}

	if (splitFiles) {

		// look through tiles in tiles directory, see which exist, find maximum Y value.
		// read textures in input directory
		if (readTilesInDirectory(inputDirectory, inputSuffix, tilesFound, tiles, verbose)) {
			return 1;
		}

		if (tilesFound == 0) {
			wprintf(L"\nERROR: No textures found in directory '%s' with a suffix of '%s'.\n", inputDirectory, inputSuffix);
			return 1;
		}

		// which channel, if any, should be inverted?
		int invertedChan = -1;
		if (negateChannel && wcslen(negateSuffix) > 0) {

			for (i = 0; i < 3; i++) {
				if (wcscmp(outChannel[i].suffix, negateSuffix) == 0) {
					invertedChan = i;
					break;
				}
			}
		}

		for (it = 0; it < tilesFound; it++) {
			for (i = 0; i < 3; i++) {
				if (outChannel[i].inUse && wcscmp(outChannel[i].suffix, L"*") != 0) {

					// output the channel if it's not all black
					if (!isChannelAllBlack(&tiles[it].image, i)) {
						wchar_t outputFile[MAX_PATH];
						wcscpy_s(outputFile, MAX_PATH, outputDirectory);
						wcscat_s(outputFile, MAX_PATH, tiles[it].name);
						wcscat_s(outputFile, MAX_PATH, outChannel[i].suffix);
						wcscat_s(outputFile, MAX_PATH, L".png");

						// allocate output image and fill it up
						progimage_info* destination_ptr = new progimage_info();

						destination_ptr->width = tiles[it].image.width;
						destination_ptr->height = tiles[it].image.height;
						destination_ptr->image_data.resize(destination_ptr->width * destination_ptr->height * 1 * sizeof(unsigned char), 0x0);

						copyOneChannel(destination_ptr, i, &tiles[it].image);

						if (i == invertedChan) {
							invertChannel(destination_ptr);
						}

						if (verbose)
							wprintf(L"Opening '%s' for output.\n", outputFile);

						// write out the result
						int rc = writepng(destination_ptr, 1, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit
							return 1;
						}
						writepng_cleanup(destination_ptr);
						if (verbose)
							wprintf(L"New texture '%s' created.\n", outputFile);
					}
				}
			}
		}
	}

	if (createLeaves) {
		createCompositedLeaves(inputDirectory, outputDirectory, synthOutSuffix, verbose);
	}

	if (gErrorCount)
		wprintf(L"\nERROR SUMMARY:\n%s\n", gConcatErrorString);

	if (gErrorCount || gWarningCount)
		wprintf(L"Summary: %d error%S and %d warning%S were generated.\n", gErrorCount, (gErrorCount == 1) ? "" : "s", gWarningCount, (gWarningCount == 1) ? "" : "s");

	return 0;
}

void printHelp()
{
	wprintf(L"ChannelMixer version 1.00\n");  // change version below, too
	wprintf(L"usage: ChannelMixer [-i inputTexturesDirectory] [-o outputTexturesDirectory]\n        [-im _mer] [-oc _m _e _r]\n");
	wprintf(L"  -i inputTexturesDirectory - directory of textures to search and process.\n        If none given, current directory.\n");
	wprintf(L"  -o outputTexturesDirectory - directory where resulting textures will go.\n        If none given, current directory.\n");
	wprintf(L"  -y - create composite colored tiles, such as grass block sides, for reuse.\n      Suffix of output files is '*_y.png'.\n");
	wprintf(L"  -im _mer - the merged input file suffix, e.g., 'anvil_mer.png'.\n");
	wprintf(L"  -oc _m _e _r - the output channel file suffixes, e.g., 'anvil_m.png',\n        'anvil_e.png', 'anvil_r.png' are produced from the RGB input channels.\n");
	wprintf(L"  -n _r - iNvert the output _r channel (making it shininess, or vice versa).\n");
	wprintf(L"  -oy _y - create composite colored tiles, with given suffix ('_y' is default).\n");
	wprintf(L"  -v - verbose, explain everything going on. Default: display only warnings.\n");
}

int readTilesInDirectory(const wchar_t* inputDirectory, const wchar_t* inputSuffix, int &tilesFound, Tile *tiles, boolean verbose)
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
	unsigned char *src_data = &src->image_data[0] + channel;
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


static void multiplyPNGTile(progimage_info* dst, int channels, unsigned int color)
{
	int row, col, ic;
	unsigned int colArray[3];
	colArray[0] = color >> 16;
	colArray[1] = (color >> 8) & 0xff;
	colArray[2] = color & 0xff;
	assert(channels == 3 || channels == 4);
	boolean fourChannels = channels == 4;

	unsigned char* duc = ((unsigned char*)(&dst->image_data[0]));
	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			for (ic = 0; ic < 3; ic++) {
				*duc++ = (unsigned char)((unsigned int)*duc * colArray[ic] / 255);
			}
			// leave alpha unchanged
			if (fourChannels)
				duc++;
		}
	}
}

// input 4 channels, squish data to 3 (without realloc) 
static void compositePNGTiles(progimage_info* dst, progimage_info *src)
{
	int row, col, ic;
	unsigned char* suc = ((unsigned char*)(&src->image_data[0]));
	unsigned char* duc = ((unsigned char*)(&dst->image_data[0]));

	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			unsigned int alpha = suc[3];
			unsigned int oneMinusAlpha = 255 - alpha;
			for (ic = 0; ic < 3; ic++) {
				*duc++ = (unsigned char)((oneMinusAlpha * (unsigned int)*duc + alpha * (unsigned int)*suc++) / 255);
			}
			suc++;
		}
	}
}

// input 4 channels, squish data to 3 (without realloc)
// This is quite sleazy, doing it in place and not reallocing, but
// we know we're going to write and quit immediately after anyway
// so we don't bother reallocing, etc.
static int compressPNGTile(progimage_info* dst)
{
	int row, col, ic;
	unsigned char* duc = ((unsigned char*)(&dst->image_data[0])) + 3;
	boolean compress = true;

	// should we compress?
	for (row = 0; row < dst->height && compress; row++)
	{
		for (col = 0; col < dst->width && compress; col++)
		{
			if (*duc < 255) {
				compress = false;
				break;
			}
			duc += 4;
		}
	}
	if (compress) {
		unsigned char* suc = duc = ((unsigned char*)(&dst->image_data[0]));
		for (row = 0; row < dst->height; row++)
		{
			for (col = 0; col < dst->width; col++)
			{
				for (ic = 0; ic < 3; ic++) {
					*duc++ = *suc++;
				}
				suc++;
			}
		}
		return 3;
	}
	else {
		return 4;
	}
}

