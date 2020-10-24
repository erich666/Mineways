// ChannelMixer: Read in possible Minecraft color, normal, and PBR textures, then process them into the PBR format desired

#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "rwpng.h"
#include "tiles.h"
#include "tilegrid.h"


// how the green and blue tiles get tinted
//#define FOLIAGE_GREEN	0x8cbd57
//#define WATER_BLUE		0x295dfe
//#define REDSTONE_RED	0xd60000

#define INC_AND_TEST_ARG_INDEX( loc )		argLoc++; \
											if (argLoc == argc) { \
												printHelp(); \
												return 1; \
											}


#define MAX_PATH_AND_FILE (2*MAX_PATH)

static int gErrorCount = 0;
static int gWarningCount = 0;
static boolean gFirstTime = true;

static wchar_t gErrorString[1000];
// 1000 errors of 100 characters each - sounds sufficient
#define CONCAT_ERROR_LENGTH	(1000*100)


static wchar_t gConcatErrorString[CONCAT_ERROR_LENGTH];

progimage_info* allocateImage(progimage_info* source_ptr);


void printHelp();
//static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, boolean verbose);
static void reportReadError(int rc, const wchar_t* filename);
static void saveErrorForEnd();

int copyFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);
int processSpecularFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);
int processMERFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose);

static int isNearlyGrayscale(progimage_info* src);
static void copyOneChannel(progimage_info* dst, int channel, progimage_info* src);
static boolean channelIsAllBlack(progimage_info * src);
static boolean channelIsAllWhite(progimage_info * src);
static void invertChannel(progimage_info* dst);


int wmain(int argc, wchar_t* argv[])
{
	int argLoc = 1;
	wchar_t inputSuffix[MAX_PATH] = L"_mer";
	wchar_t inputDirectory[MAX_PATH] = L"";
	wchar_t outputDirectory[MAX_PATH] = L"";
	boolean verbose = false;
	//wchar_t synthOutSuffix[MAX_PATH] = L"_y";	// _y.png is synthesized (composited) textures
	//boolean createLeaves = false;

	initializeFileGrid(&gFG);

	// two means even try the alternate name list
	int alternate = 2;

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
	if (!(CreateDirectoryW(outputDirectory, NULL) ||
		ERROR_ALREADY_EXISTS == GetLastError())) {
		wprintf(L"\nERROR: Output directory '%s' could not be created.\n", outputDirectory);
		return 1;
	}

	// add \ to end of directory paths
	addBackslashIfNeeded(inputDirectory);
	addBackslashIfNeeded(outputDirectory);

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

	int filesProcessed = 0;

	// See what we need to do. If sameDir is false, we need to copy over all color and normals files.
	// Work we always do: look at _s files and _mer files and split out.
	// _s files might be grayscale (roughness only) or SME.
	if (!sameDir) {
		filesProcessed += copyFiles(&gFG, outputDirectory, verbose);
	}

	if (gFG.categories[CATEGORY_SPECULAR] > 0) {
		filesProcessed += processSpecularFiles(&gFG, outputDirectory, verbose);
	}

	if (gFG.categories[CATEGORY_MER] > 0) {
		filesProcessed += processMERFiles(&gFG, outputDirectory, verbose);
	}

	//if (createLeaves) {
	//	createCompositedLeaves(inputDirectory, outputDirectory, synthOutSuffix, verbose);
	//}

	if (gErrorCount)
		wprintf(L"\nERROR SUMMARY:\n%s\n", gConcatErrorString);

	if (gErrorCount || gWarningCount)
		wprintf(L"Summary: %d error%S and %d warning%S were generated.\n", gErrorCount, (gErrorCount == 1) ? "" : "s", gWarningCount, (gWarningCount == 1) ? "" : "s");

	wprintf(L"ChannelMixer summary: %d files read in and processed.\n", filesProcessed);
	return 0;
}

void printHelp()
{
	wprintf(L"ChannelMixer version 1.00\n");  // change version below, too
	wprintf(L"usage: ChannelMixer [-i inputTexturesDirectory] [-o outputTexturesDirectory]\n"); // [-im _mer] [-oc _m _e _r] \n");
	wprintf(L"  -i inputTexturesDirectory - directory of textures to search and process.\n        If none given, current directory.\n");
	wprintf(L"  -o outputTexturesDirectory - directory where resulting textures will go.\n        If none given, current directory.\n");
	wprintf(L"  -v - verbose, explain everything going on. Default: display only warnings.\n");
}

int copyFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {
	int filesRead = 0;

	int copyCategories[] = { CATEGORY_RGBA, CATEGORY_NORMALS, CATEGORY_NORMALS_LONG, CATEGORY_METALLIC, CATEGORY_EMISSION, CATEGORY_ROUGHNESS };
	int numCats = sizeof(copyCategories) / sizeof(int);
	for (int category = 0; category < numCats; category++) {
		if (pfg->categories[copyCategories[category]] > 0) {
			// there are files to copy
			for (int i = 0; i < pfg->totalTiles; i++) {
				int fullIndex = copyCategories[category] * pfg->totalTiles + i;
				if (pfg->fr[fullIndex].exists) {
					// file exists - copy it.
					wchar_t inputFile[MAX_PATH_AND_FILE];
					wcscpy_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].path);
					wcscat_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);

					wchar_t outputFile[MAX_PATH_AND_FILE];
					wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
					wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);

					// overwrite previous file
					if (CopyFile(inputFile, outputFile, false) == 0) {
							DWORD attr = GetFileAttributes(outputFile);
							int isReadOnly = 0;
							if (attr != INVALID_FILE_ATTRIBUTES) {
								isReadOnly = attr & FILE_ATTRIBUTE_READONLY;
							}
							if (isReadOnly) {
								if (gFirstTime || verbose) {
									gFirstTime = false;
									wprintf(L"WARNING: file '%s' was not copied to the output directory, as the copy there is read-only.\n  If you are running ChannelMixer again, on the same input and output directories, you can likely ignore this warning.\n", pfg->fr[fullIndex].fullFilename);
									if (!verbose)
										wprintf(L"  To avoid generating noise, this warning is shown only once (use '-v' to see them all).\n");
								}
							}
							else {
								wsprintf(gErrorString, L"***** ERROR: file '%s' could not be copied to '%s'.\n", inputFile, outputFile);
								saveErrorForEnd();
								gErrorCount++;
							}
					}
					else if (verbose) {
						wprintf(L"Texture '%s' copied to '%s'.\n", inputFile, outputFile);
						filesRead++;
					}

					if (category == CATEGORY_NORMALS_LONG) {
						int otherIndex = copyCategories[CATEGORY_NORMALS] * pfg->totalTiles + i;
						if (pfg->fr[otherIndex].exists) {
							wprintf(gErrorString, L"WARNING: file '%s' also has a version named '%s_n.png'. Both copied over.\n", pfg->fr[fullIndex].fullFilename, pfg->fr[otherIndex].fullFilename);
						}
					}
				}
			}
		}
	}
	return filesRead;
}

int processSpecularFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {
	int rc;
	int isGrayscale = 0;
	int isSME = 0;
	int filesRead = 0;

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
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);

			// read in tile for later
			progimage_info tile;
			rc = readpng(&tile, inputFile, LCT_RGB);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
				continue;
			}
			else {
				filesRead++;

				wchar_t outputFile[MAX_PATH_AND_FILE];
				if (isNearlyGrayscale(&tile)) {
					isGrayscale++;
					// specular only: output just the roughness channel
					// always export specular in inverted
					progimage_info* destination_ptr = allocateImage(&tile);
					copyOneChannel(destination_ptr, CHANNEL_RED, &tile);
					// output the channel if it's not all black
					if (!channelIsAllBlack(destination_ptr)) {
						invertChannel(destination_ptr);

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].rootName);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_ROUGHNESS]);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

						rc = writepng(destination_ptr, 1, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit - if we can't write one file, we're unlikely to write the rest.
							return filesRead;
						}
						if (verbose) {
							wprintf(L"New texture '%s' created.\n", outputFile);
						}
					}
					writepng_cleanup(destination_ptr);
				}
				else {
					isSME++;
					// SME: output all three
					for (int channel = 0; channel < 3; channel++) {
						// output the channel if it's not all black
						progimage_info* destination_ptr = allocateImage(&tile);
						copyOneChannel(destination_ptr, channel, &tile);
						if (!channelIsAllBlack(destination_ptr)) {
							if (channel == CHANNEL_RED) {
								invertChannel(destination_ptr);
							}

							wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].rootName);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[category[channel]]);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

							rc = writepng(destination_ptr, 1, outputFile);
							if (rc != 0)
							{
								reportReadError(rc, outputFile);
								// quit - if we can't write one file, we're unlikely to write the rest.
								return filesRead;
							}
							if (verbose) {
								wprintf(L"New texture '%s' created.\n", outputFile);
							}
						}
						writepng_cleanup(destination_ptr);
					}
				}
				readpng_cleanup(1, &tile);
			}
		}
	}
	if (isGrayscale > 0 && isSME > 0) {
		wprintf(gErrorString, L"WARNING: the '*_s.png' input files seem to be of two formats: %d are specular-only, %d are specular/metallic/emissive.\n", isGrayscale, isSME);
	}
	else if (verbose) {
		wprintf(gErrorString, L"Specular input files processed: %d are specular-only, %d are specular/metallic/emissive.\n", isGrayscale, isSME);
	}
	return filesRead;
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

int processMERFiles(FileGrid* pfg, const wchar_t* outputDirectory, int verbose) {
	int rc;
	int isMER = 0;
	int filesRead = 0;

	// MER order
	int category[] = { CATEGORY_METALLIC, CATEGORY_EMISSION, CATEGORY_ROUGHNESS };

	for (int i = 0; i < pfg->totalTiles; i++) {
		int fullIndex = CATEGORY_MER * pfg->totalTiles + i;
		if (pfg->fr[fullIndex].exists) {
			isMER++;

			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wchar_t inputFile[MAX_PATH_AND_FILE];
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);

			// read in tile
			progimage_info tile;
			rc = readpng(&tile, inputFile, LCT_RGB);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
			}
			else {
				filesRead++;

				wchar_t outputFile[MAX_PATH_AND_FILE];

				// MER: output all three
				for (int channel = 0; channel < 3; channel++) {
					// output the channel if it's not all black
					// output the channel if it's not all black - actually, we do want to output it;
					// that's a valid value set, which we should deal with in TileMaker
					//if (!isChannelAllBlack(&tile, channel)) {
					progimage_info* destination_ptr = allocateImage(&tile);
					copyOneChannel(destination_ptr, channel, &tile);
					// output the channel if it's not all black (or white, for roughness
					if ((channel == 2) ? !channelIsAllWhite(destination_ptr) : !channelIsAllBlack(destination_ptr)) {

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].rootName);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[category[channel]]);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

						rc = writepng(destination_ptr, 1, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit - if we can't write one file, we're unlikely to write the rest.
							return filesRead;
						}
						if (verbose)
							wprintf(L"New texture '%s' created.\n", outputFile);
						//}
					}
					writepng_cleanup(destination_ptr);
				}

				readpng_cleanup(1, &tile);
			}
		}
	}
	if (verbose) {
		wprintf(gErrorString, L"%d MER input files processed.\n", isMER);
	}

	return filesRead;
}

/*
static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, boolean verbose)
{
	int rc = 0;
	int i;
	wchar_t tileSearch[MAX_PATH_AND_FILE];

	// color and composite grass side overlay atop grass side
	progimage_info* grass_overlay = new progimage_info();
	progimage_info* grass_side = new progimage_info();

	wcscpy_s(tileSearch, MAX_PATH_AND_FILE, inputDirectory);
	wcscat_s(tileSearch, MAX_PATH_AND_FILE, L"grass_block_side_overlay.png");
	rc = readpng(grass_overlay, tileSearch, LCT_RGBA);
	if (rc == 0)
	{
		// continue, get grass underlay
		wcscpy_s(tileSearch, MAX_PATH_AND_FILE, inputDirectory);
		wcscat_s(tileSearch, MAX_PATH_AND_FILE, L"grass_block_side.png");
		rc = readpng(grass_side, tileSearch, LCT_RGB);
		if (rc != 0) {
			// try getting dirt instead
			wcscpy_s(tileSearch, MAX_PATH_AND_FILE, inputDirectory);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, L"dirt.png");
			rc = readpng(grass_side, tileSearch, LCT_RGB);
		}
		if (rc == 0) {
			// OK, color the grass and composite it.
			multiplyPNGTile(grass_overlay, 4, FOLIAGE_GREEN);
			compositePNGTiles(grass_side, grass_overlay);
			wcscpy_s(tileSearch, MAX_PATH_AND_FILE, outputDirectory);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, L"grass_block_side");
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, outputSuffix);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, L".png");
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

		wcscpy_s(tileSearch, MAX_PATH_AND_FILE, inputDirectory);
		wcscat_s(tileSearch, MAX_PATH_AND_FILE, tintTable[i].name);
		wcscat_s(tileSearch, MAX_PATH_AND_FILE, L".png");
		rc = readpng(tile, tileSearch, LCT_RGBA);
		if (rc == 0)
		{
			// OK, tint it and output it
			multiplyPNGTile(tile, 4, tintTable[i].tint);
			int channels = compressPNGTile(tile);
			wcscpy_s(tileSearch, MAX_PATH_AND_FILE, outputDirectory);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, tintTable[i].name);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, outputSuffix);
			wcscat_s(tileSearch, MAX_PATH_AND_FILE, L".png");
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

static boolean channelIsAllBlack(progimage_info* src)
{
	// look at all data: black?
	int row, col;
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (*src_data++ != 0)
			{
				return false;
			}
		}
	}
	return true;
}

static boolean channelIsAllWhite(progimage_info* src)
{
	// look at all data: black?
	int row, col;
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (*src_data++ != 255)
			{
				return false;
			}
		}
	}
	return true;
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
