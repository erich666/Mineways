// ChannelMixer: Read in possible Minecraft color, normal, and PBR textures, then process them into the PBR format desired

#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "readtga.h"
#include "tiles.h"
#include "tilegrid.h"

#define	VERSION_STRING	L"1.17"

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
static int gWriteProtectCount = 0;
static int gIgnoredCount = 0;
//static int gChestCount = 0;
static bool gChestDirectoryExists = false;
static bool gChestDirectoryFailed = false;
static bool gDecoratedPotDirectoryExists = false;
static bool gDecoratedPotDirectoryFailed = false;

//                                                L"", L"_n", L"_normal", L"_m", L"_e", L"_r", L"_s", L"_mer", L"_y", L"_heightmap"
static bool gUseCategory[TOTAL_CATEGORIES] = { true, true, true, true, true, true, true, true, true, true };


static wchar_t gErrorString[1000];
// 1000 errors of 100 characters each - sounds sufficient
#define CONCAT_ERROR_LENGTH	(1000*100)


static wchar_t gConcatErrorString[CONCAT_ERROR_LENGTH];

progimage_info* allocateGrayscaleImage(progimage_info* source_ptr);


static void printHelp();
//static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, bool verbose);
static void reportReadError(int rc, const wchar_t* filename);
static void saveErrorForEnd();

static int copyFiles(FileGrid* pfg, ChestGrid * pcg, DecoratedPotGrid * ppg, const wchar_t* outputDirectory, bool verbose);
static int processSpecularFiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid * ppg, const wchar_t* outputDirectory, bool outputMerged, bool verbose);
static int processMERFiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid * ppg, const wchar_t* outputDirectory, bool verbose);
static bool setChestDirectory(const wchar_t* outputDirectory, wchar_t* outputChestDirectory);
static bool setPotDirectory(const wchar_t* outputDirectory, wchar_t* outputPotDirectory);

static int isNearlyGrayscale(progimage_info* src, int channels);
static bool isAlphaSemitransparent(progimage_info * src);
static void invertChannel(progimage_info* dst);
static void StoMER(progimage_info * dst, progimage_info * src);
static bool SMEtoMER(progimage_info * dst, progimage_info * src);


int wmain(int argc, wchar_t* argv[])
{
	int argLoc = 1;
	wchar_t inputSuffix[MAX_PATH] = L"_mer";
	wchar_t inputDirectory[MAX_PATH] = L".";
	wchar_t outputDirectory[MAX_PATH] = L".";
	bool outputMerged = false;
	bool verbose = false;
	bool warnUnused = false;
	//wchar_t synthOutSuffix[MAX_PATH] = L"_y";	// _y.png is synthesized (composited) textures
	//bool createLeaves = false;

	initializeFileGrid(&gFG);
	initializeChestGrid(&gCG);
	initializeDecoratedPotGrid(&gPG);

	bool inputCalled = false;

	// two means even try the alternate name list
	int alternate = 2;

	while (argLoc < argc)
	{
		if (wcscmp(argv[argLoc], L"-i") == 0)
		{
			// input directory
			INC_AND_TEST_ARG_INDEX(argLoc);
			if (!inputCalled) {
				wcscpy_s(inputDirectory, MAX_PATH, argv[argLoc]);
				inputCalled = true;
			} else {
				wprintf(L"SERIOUS WARNING: only one input directory can be specified. Only directory %s will be used.\n", inputDirectory);
			}
		}
		else if (wcscmp(argv[argLoc], L"-o") == 0)
		{
			// output directory
			INC_AND_TEST_ARG_INDEX(argLoc);
			wcscpy_s(outputDirectory, MAX_PATH, argv[argLoc]);
		}
		else if (wcscmp(argv[argLoc], L"-m") == 0)
		{
			// also output MER file from SME
			outputMerged = true;
		}
		else if (wcscmp(argv[argLoc], L"-k") == 0)
		{
			// kill channel output
			INC_AND_TEST_ARG_INDEX(argLoc);
			if (wcschr(argv[argLoc], L'm') != NULL)
			{
				gUseCategory[CATEGORY_METALLIC] = false;
			}
			if (wcschr(argv[argLoc], L'e') != NULL)
			{
				gUseCategory[CATEGORY_EMISSION] = false;
			}
			if (wcschr(argv[argLoc], L'r') != NULL)
			{
				gUseCategory[CATEGORY_ROUGHNESS] = false;
			}
			if (wcschr(argv[argLoc], L'n') != NULL)
			{
				gUseCategory[CATEGORY_NORMALS] = false;
				gUseCategory[CATEGORY_NORMALS_LONG] = false;
				gUseCategory[CATEGORY_HEIGHTMAP] = false;
			}
		}
		else if (wcscmp(argv[argLoc], L"-u") == 0)
		{
			// warn if a PNG is found but does not match anything we would use.
			warnUnused = true;
		}
		else if (wcscmp(argv[argLoc], L"-v") == 0)
		{
			// verbose: tell when normal things happen
			verbose = true;
			warnUnused = true;
		}
		else
		{
			printHelp();
			return 1;
		}
		argLoc++;
	}

	if (verbose)
		wprintf(L"ChannelMixer version %s\n", VERSION_STRING);

	if (!dirExists(inputDirectory)) {
		wprintf(L"***** ERROR: Input directory '%s' does not exist.\n", inputDirectory);
		return 1;
	}

	// from https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	if (!(CreateDirectoryW(outputDirectory, NULL) ||
		ERROR_ALREADY_EXISTS == GetLastError())) {
		wprintf(L"***** ERROR: Output directory '%s' could not be created.\n", outputDirectory);
		return 1;
	}

	// add \ to end of directory paths
	addBackslashIfNeeded(inputDirectory);
	addBackslashIfNeeded(outputDirectory);

#ifdef _DEBUG
	// reality check - make sure data in gTilesAlternates is correct
	int i = 0;
	while (wcslen(gTilesAlternates[i].filename) > 0) {
		if (!findTileIndex(gTilesAlternates[i].filename, 1) ) {
			assert(0);
		}
		i++;
	}
#endif

	// How it works:
	// Look through all directories for files, warn of duplicates that get ignored. TODO: just have one directory now
	// Copy files over if output directory is different than input directory.
	// Split _mer and _s files into _m _e _r files.

	bool sameDir = (_wcsicmp(inputDirectory, outputDirectory) == 0);

	// look through tiles in tiles directories, see which exist.
	int filesFound = 0;
	int fileCount = searchDirectoryForTiles(&gFG, &gCG, &gPG, inputDirectory, wcslen(inputDirectory), verbose, alternate, true, warnUnused, true);
	if (fileCount < 0) {
		wsprintf(gErrorString, L"***** ERROR: cannot access the directory '%s' (Windows error code # %d). Ignoring directory.\n", inputDirectory, GetLastError());
		saveErrorForEnd();
		gErrorCount++;
	}
	else {
		filesFound += fileCount;
	}

	if (filesFound <= 0) {
		wprintf(L"***** ERROR, no files found to process - quitting.\n");
	}

	if (verbose) {
		wprintf(L"%d files found to process.\n", filesFound);
	}

	int filesProcessed = 0;

	// See what we need to do. If sameDir is false, we need to copy over all color and normals files.
	// Work we always do: look at _s files and _mer files and split out.
	// _s files might be grayscale (roughness only) or SME.
	if (!sameDir) {
		filesProcessed += copyFiles(&gFG, &gCG, &gPG, outputDirectory, verbose);
	}

	if (gFG.categories[CATEGORY_SPECULAR] > 0 || gCG.categories[CATEGORY_SPECULAR] > 0 || gPG.categories[CATEGORY_SPECULAR] > 0) {
		filesProcessed += processSpecularFiles(&gFG, &gCG, &gPG, outputDirectory, outputMerged, verbose);
	}

	if (gFG.categories[CATEGORY_MER] > 0 || gCG.categories[CATEGORY_MER] > 0 || gPG.categories[CATEGORY_MER] > 0) {
		filesProcessed += processMERFiles(&gFG, &gCG, &gPG, outputDirectory, verbose);
	}

	//if (createLeaves) {
	//	createCompositedLeaves(inputDirectory, outputDirectory, synthOutSuffix, verbose);
	//}

	// output all color textures that were NOT replaced.
	if (verbose) {
		int ifn, ifc;
		ifc = 0;
		for (ifn = 0; ifn < TOTAL_TILES; ifn++) {
			// Note we assume the first tiles in the long list are RGBAs. These are all we check.
			if (!gFG.fr[ifn].exists) {
				// RGBA not found - could it ever be found?
				if (wcslen(gTilesTable[ifn].filename) > 0 && wcsncmp(gTilesTable[ifn].filename, L"MW", 2) != 0) {
					if (ifc == 0) {
						wprintf(L"Minecraft RGBA textures that could be replaced but were not:\n");
					}
					wprintf(L"    %s.png\n", gTilesTable[ifn].filename);
					ifc++;
				}
			}
		}
		if (ifc > 0) {
			wprintf(L"Total number of RGBA textures not replaced is %d.\n", ifc);
		}
	}

	if (gWriteProtectCount > 1) {
		wprintf(L"WARNING: a total of %d files could not be written due to write protection.\n", gWriteProtectCount);
	}

	if (gErrorCount)
		wprintf(L"\nERROR SUMMARY:\n%s\n", gConcatErrorString);

	if (gErrorCount || gWarningCount)
		wprintf(L"Summary: %d error%S and %d warning%S were generated.\n", gErrorCount, (gErrorCount == 1) ? "" : "s", gWarningCount, (gWarningCount == 1) ? "" : "s");

	if (gWriteProtectCount > 0) {
		wprintf(L"ChannelMixer summary: %d relevant PNG and TGA files discovered; %d of these were used and\n    %d were read-only so could not be overwritten.\n", filesFound, filesProcessed, gWriteProtectCount);
	}
	else {
		wprintf(L"ChannelMixer summary: %d relevant PNG and TGA files discovered and %d of these were used.\n", filesFound, filesProcessed);
	}
	if (filesFound > filesProcessed + gWriteProtectCount + gIgnoredCount) {
		wprintf(L"    This difference of %d files means that some duplicate files were found and not used.\n", filesFound - filesProcessed - gWriteProtectCount);
		wprintf(L"    Look through the 'DUP WARNING's and rename or delete those files you do not want to use, clear the directory, and run again.\n");
	}

	// Note various types of files used, if more found than used. Not sure why I added this...
	if (filesFound > gFG.categories[CATEGORY_RGBA] + gCG.categories[CATEGORY_RGBA] + gWriteProtectCount + gIgnoredCount) {
		bool foundFirst = false;
		for (int category = 0; category < TOTAL_CATEGORIES; category++) {
			// does this category have any input files? If not, skip it - just a small speed-up.
			if (gFG.categories[category] + gCG.categories[category] + gPG.categories[category] > 0) {
				wprintf(L"%s %d %s%s.png",
					foundFirst ? L"," : L"    Relevant block, chest, and decorated pot files found:", gFG.categories[category] + gCG.categories[category] + gPG.categories[category],
					(category != CATEGORY_RGBA) ? L"*" : L"(RGBA)",
					(category != CATEGORY_RGBA) ? gCatSuffixes[category] : L"*");
				foundFirst = true;
			}
		}
		wprintf(L".\n");
	}


	/*
	if (verbose) {
		// count how many color textures there are that were not replaced
		int ifn, ifc;
		ifc = 0;
		for (ifn = 0; ifn < )
			if (filesProcessed < ) {
				wprintf(L"    This difference of %d files means that some duplicate files were found and not used.\n", filesFound - filesProcessed - gWriteProtectCount);
				wprintf(L"    Look through the 'DUP WARNING's and rename or delete those files you do not want to use.\n");
			}
	}
	*/
	return 0;
}

static void printHelp()
{
	wprintf(L"ChannelMixer version %s\n", VERSION_STRING);
	wprintf(L"usage: ChannelMixer [-v] [-i inputTexturesDirectory] [-o outputTexturesDirectory] [-m] [-k {m|e|r|n}] [-u]\n");
	wprintf(L"  -v - verbose, explain everything going on. Default: display only warnings and errors.\n");
	wprintf(L"  -i inputTexturesDirectory - directory of textures to search and process.\n        If none given, current directory.\n");
	wprintf(L"  -o outputTexturesDirectory - directory where resulting textures will go.\n        If none given, current directory.\n");
	wprintf(L"  -m - output merged '_mer' format files in addition to separate files, as found.\n");
	wprintf(L"  -k {m|e|r|n} - kill export of the metallic, emissive, roughness, and/or normals textures.\n");
	wprintf(L"  -u - show all image files encountered that are not standard Minecraft block or chest names.\n");
}

static int copyFiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid* ppg, const wchar_t* outputDirectory, bool verbose) {
	int filesRead = 0;

	int copyCategories[] = { CATEGORY_RGBA, CATEGORY_NORMALS, CATEGORY_NORMALS_LONG, CATEGORY_METALLIC, CATEGORY_EMISSION, CATEGORY_ROUGHNESS, CATEGORY_HEIGHTMAP };
	int numCats = sizeof(copyCategories) / sizeof(int);
	wchar_t outputChestDirectory[MAX_PATH];
	wchar_t outputPotDirectory[MAX_PATH];
	for (int category = 0; category < numCats; category++) {
		// does this category have any input files? If not, skip it - just a small speed-up.
		if (pfg->categories[copyCategories[category]] > 0 ) {
			if (gUseCategory[category]) {
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
								if (!gWriteProtectCount || verbose) {
									wprintf(L"WARNING: File '%s' was not copied to the output directory, as the copy there is read-only.\n    If you are running ChannelMixer again, on the same input and output directories, you can likely ignore this warning. Alternately, make your files writeable.\n", pfg->fr[fullIndex].fullFilename);
									if (!verbose)
										wprintf(L"  To avoid generating noise, this warning is shown only once (use '-v' to see them all).\n");
								}
								gWriteProtectCount++;
							}
							else {
								wsprintf(gErrorString, L"***** ERROR: file '%s' could not be copied to '%s'.\n", inputFile, outputFile);
								saveErrorForEnd();
								gErrorCount++;
							}
						}
						else {
							// file copied successfully
							filesRead++;
							if (verbose) {
								wprintf(L"Texture '%s' copied to '%s'.\n", inputFile, outputFile);
							}
						}

						if (category == CATEGORY_NORMALS_LONG || category == CATEGORY_HEIGHTMAP) {
							int otherIndex = copyCategories[CATEGORY_NORMALS] * pfg->totalTiles + i;
							if (pfg->fr[otherIndex].exists) {
								wprintf(L"WARNING: File '%s' also has a version named '%s'. Both copied over; TileMaker will ignore the '_normal.png' version.\n", pfg->fr[fullIndex].fullFilename, pfg->fr[otherIndex].fullFilename);
							}
						}
					}
				}
			}
			else {
				// don't use these files found, so decrease the count
				if (verbose) {
					wprintf(L"WARNING: %d file%s in the %s category read in but ignored\n",
						pfg->categories[copyCategories[category]],
						pfg->categories[copyCategories[category]] > 0 ? L"s" : L"",
						gCatSuffixes[category]);
				}
				gIgnoredCount += pfg->categories[copyCategories[category]];
			}
		}

		// Chests
		// does this chest category have any input files? If not, skip it - just a small speed-up.
		if (pcg->categories[copyCategories[category]] > 0 ) {
			if (gUseCategory[category]) {
				//gChestCount += pcg->categories[copyCategories[category]];
				// there are files to copy
				for (int i = 0; i < pcg->totalTiles; i++) {
					int fullIndex = copyCategories[category] * pcg->totalTiles + i;
					if (pcg->cr[fullIndex].exists) {
						// file exists - copy it.

						// first create chest subdirectory if it doesn't exist
						if (!setChestDirectory(outputDirectory, outputChestDirectory)) {
							break;
						}

						wchar_t inputFile[MAX_PATH_AND_FILE];
						wcscpy_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].path);
						wcscat_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].fullFilename);

						wchar_t outputFile[MAX_PATH_AND_FILE];
						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].fullFilename);

						// overwrite previous file
						if (CopyFile(inputFile, outputFile, false) == 0) {
							DWORD attr = GetFileAttributes(outputFile);
							int isReadOnly = 0;
							if (attr != INVALID_FILE_ATTRIBUTES) {
								isReadOnly = attr & FILE_ATTRIBUTE_READONLY;
							}
							if (isReadOnly) {
								if (!gWriteProtectCount || verbose) {
									wprintf(L"WARNING: File '%s' was not copied to the output directory, as the copy there is read-only.\n    If you are running ChannelMixer again, on the same input and output directories, you can likely ignore this warning. Alternately, make your files writeable.\n", pcg->cr[fullIndex].fullFilename);
									if (!verbose)
										wprintf(L"  To avoid generating excessive error messages, this warning is shown only once (use '-v' to see them all).\n");
								}
								gWriteProtectCount++;
							}
							else {
								wsprintf(gErrorString, L"***** ERROR: file '%s' could not be copied to '%s'.\n", inputFile, outputFile);
								saveErrorForEnd();
								gErrorCount++;
							}
						}
						else {
							// file copied successfully
							filesRead++;
							if (verbose) {
								wprintf(L"Chest texture '%s' copied to '%s'.\n", inputFile, outputFile);
							}
						}

						if (category == CATEGORY_NORMALS_LONG || category == CATEGORY_HEIGHTMAP) {
							int otherIndex = copyCategories[CATEGORY_NORMALS] * pcg->totalTiles + i;
							if (pcg->cr[otherIndex].exists) {
								wprintf(L"WARNING: Chest file '%s' also has a version named '%s'. Both copied over.\n", pcg->cr[fullIndex].fullFilename, pcg->cr[otherIndex].fullFilename);
							}
						}
					}
				}
			}
			else {
				// don't use these files found, so decrease the count
				if (verbose) {
					wprintf(L"WARNING: %d chest file%s in the %s category read in but ignored\n",
						pcg->categories[copyCategories[category]],
						pcg->categories[copyCategories[category]] > 0 ? L"s" : L"",
						gCatSuffixes[category]);
				}
				gIgnoredCount += pcg->categories[copyCategories[category]];
			}

		}

		// Decorated Pots
		// does this pot category have any input files? If not, skip it - just a small speed-up.
		if (ppg->categories[copyCategories[category]] > 0) {
			if (gUseCategory[category]) {
				//gDecoratedPotCount += ppg->categories[copyCategories[category]];
				// there are files to copy
				for (int i = 0; i < ppg->totalTiles; i++) {
					int fullIndex = copyCategories[category] * ppg->totalTiles + i;
					if (ppg->pr[fullIndex].exists) {
						// file exists - copy it.

						// first create pot subdirectory if it doesn't exist
						if (!setPotDirectory(outputDirectory, outputPotDirectory)) {
							break;
						}

						wchar_t inputFile[MAX_PATH_AND_FILE];
						wcscpy_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].path);
						wcscat_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].fullFilename);

						wchar_t outputFile[MAX_PATH_AND_FILE];
						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].fullFilename);

						// overwrite previous file
						if (CopyFile(inputFile, outputFile, false) == 0) {
							DWORD attr = GetFileAttributes(outputFile);
							int isReadOnly = 0;
							if (attr != INVALID_FILE_ATTRIBUTES) {
								isReadOnly = attr & FILE_ATTRIBUTE_READONLY;
							}
							if (isReadOnly) {
								if (!gWriteProtectCount || verbose) {
									wprintf(L"WARNING: File '%s' was not copied to the output directory, as the copy there is read-only.\n    If you are running ChannelMixer again, on the same input and output directories, you can likely ignore this warning. Alternately, make your files writeable.\n", ppg->pr[fullIndex].fullFilename);
									if (!verbose)
										wprintf(L"  To avoid generating excessive error messages, this warning is shown only once (use '-v' to see them all).\n");
								}
								gWriteProtectCount++;
							}
							else {
								wsprintf(gErrorString, L"***** ERROR: file '%s' could not be copied to '%s'.\n", inputFile, outputFile);
								saveErrorForEnd();
								gErrorCount++;
							}
						}
						else {
							// file copied successfully
							filesRead++;
							if (verbose) {
								wprintf(L"Decorated pot texture '%s' copied to '%s'.\n", inputFile, outputFile);
							}
						}

						if (category == CATEGORY_NORMALS_LONG || category == CATEGORY_HEIGHTMAP) {
							int otherIndex = copyCategories[CATEGORY_NORMALS] * ppg->totalTiles + i;
							if (ppg->pr[otherIndex].exists) {
								wprintf(L"WARNING: Decorated pot file '%s' also has a version named '%s'. Both copied over.\n", ppg->pr[fullIndex].fullFilename, ppg->pr[otherIndex].fullFilename);
							}
						}
					}
				}
			}
			else {
				// don't use these files found, so decrease the count
				if (verbose) {
					wprintf(L"WARNING: %d decorated pot file%s in the %s category read in but ignored\n",
						ppg->categories[copyCategories[category]],
						ppg->categories[copyCategories[category]] > 0 ? L"s" : L"",
						gCatSuffixes[category]);
				}
				gIgnoredCount += ppg->categories[copyCategories[category]];
			}

		}
	}
	return filesRead;
}

static int processSpecularFiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid* ppg, const wchar_t* outputDirectory, bool outputMerged, bool verbose) {
	int rc;
	int isGrayscale = 0;
	int isSME = 0;
	int filesRead = 0;
	int imageFileType;

	// SME order, inverting the red channel:
	int category[] = { CATEGORY_ROUGHNESS, CATEGORY_METALLIC, CATEGORY_EMISSION };
	wchar_t inputFile[MAX_PATH_AND_FILE];
	LodePNGColorType readColorType = LCT_RGB;
	int numChannels = 3;

	// go through all files as a preprocess. If any have an alpha channel, then assume these are labPBR new format and
	// read the alpha as the emission channel.
	for (int i = 0; i < pfg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * pfg->totalTiles + i;
		if (pfg->fr[fullIndex].exists) {
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);
			// read tile header
			progimage_info tile;
			LodePNGColorType colortype;
			imageFileType = isImageFile(inputFile);
			rc = readImageHeader(&tile, inputFile, colortype, imageFileType);
			if (rc != 0)
			{
				// skip file - we'll report the read error below
				continue;
			}
			if (colortype == LCT_RGBA) { // and in theory: || colortype == LCT_GREY_ALPHA ) { - but this will never happen
				readColorType = LCT_RGBA;
				numChannels = 4;
				break;
			}
			else if (colortype == LCT_PALETTE ) {
				// ugh, need to read image and see if any alphas are < 255 (and > 0, just to be safe)
				rc = readImage(&tile, inputFile, LCT_RGBA, imageFileType);
				if (rc != 0)
				{
					continue;
				}
				if (isAlphaSemitransparent(&tile)) {
					readColorType = LCT_RGBA;
					numChannels = 4;
					readImage_cleanup(1, &tile);
					wprintf(L"LabPBR emission data detected; using the alpha channel for the emission map.\n");
					break;
				}
				readImage_cleanup(1, &tile);
			}
		}
	}

	for (int i = 0; i < pfg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * pfg->totalTiles + i;
		if (pfg->fr[fullIndex].exists) {
			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].fullFilename);

			// read in tile for later
			progimage_info tile;
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, readColorType, imageFileType);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
				continue;
			}
			else {
				filesRead++;

				wchar_t outputFile[MAX_PATH_AND_FILE];
				if (isNearlyGrayscale(&tile, numChannels)) {
					isGrayscale++;
					// specular only: output just the roughness channel
					// always export specular in inverted
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, CHANNEL_RED, &tile, readColorType);
					// output the channel if it's not all black
					bool allBlack = true;
					if (gUseCategory[CATEGORY_ROUGHNESS] && !channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
						allBlack = false;
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
							wprintf(L"New texture '%s' fullIndex %d created due to grayscale.\n", outputFile, fullIndex);
						}
					}
					writepng_cleanup(destination_ptr);
					if (!allBlack && outputMerged) {
						progimage_info* mer_ptr = allocateRGBImage(&tile);
						StoMER(mer_ptr, &tile);
						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].rootName);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

						rc = writepng(mer_ptr, 3, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit
							return filesRead;
						}
						if (verbose) {
							wprintf(L"New specular-only MER texture '%s' created.\n", outputFile);
						}
						writepng_cleanup(mer_ptr);
					}
				}
				else {
					isSME++;
					// SME: output all three
                    for (int channel = 0; channel < 3; channel++) {
                        if (gUseCategory[category[channel]]) {
                            // output the channel if it's not all black
                            progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
							if (channel == CHANNEL_BLUE && numChannels == 4) {
								// special treatment for labPBR
								copyOneChannel(destination_ptr, 3, &tile, readColorType);
								// if all 255, then emission is not being used
								if (channelEqualsValue(destination_ptr, 0, 1, 255, 0)) {
									continue;
								}
								// change all 255's to 0 for emission
								changeValueToValue(destination_ptr, 0, 1, 255, 0);
							}
							else {
								copyOneChannel(destination_ptr, channel, &tile, readColorType);
							}
                            // is the channel, copied over, non-zero?
                            if (!channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
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
                                    wprintf(L"New texture '%s' fullIndex %d created due to SME.\n", outputFile, fullIndex);
                                }
                            }
                            writepng_cleanup(destination_ptr);
                        }
                    }
					if (outputMerged) {
						progimage_info* smer_ptr = allocateRGBImage(&tile);
						if (SMEtoMER(smer_ptr, &tile)) {
							wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputDirectory);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, pfg->fr[fullIndex].rootName);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

							rc = writepng(smer_ptr, 3, outputFile);
							if (rc != 0)
							{
								reportReadError(rc, outputFile);
								// quit
								return filesRead;
							}
							if (verbose) {
								wprintf(L"New merged MER texture '%s' fullIndex %d created.\n", outputFile, fullIndex);
							}
						}
						else {
							wprintf(L"WARNING: No merged MER texture '%s' was generated, as SME conversion resulted in an all-black image.\n", outputFile);
						}
						writepng_cleanup(smer_ptr);
					}
				}
				readImage_cleanup(1, &tile);
			}
		}
	}

	// grotesque, lazy coding: copying all the code above, without RGBA testing, and with chest grid changes
	wchar_t outputChestDirectory[MAX_PATH];
	for (int i = 0; i < pcg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * pcg->totalTiles + i;
		if (pcg->cr[fullIndex].exists) {
			// file exists - process it.

			// first create chest subdirectory if it doesn't exist
			if (!setChestDirectory(outputDirectory, outputChestDirectory)) {
				break;
			}

			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].fullFilename);

			// read in tile for later
			progimage_info tile;
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, LCT_RGB, imageFileType);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
				continue;
			}
			else {
				filesRead++;

				wchar_t outputFile[MAX_PATH_AND_FILE];
				if (isNearlyGrayscale(&tile, 3)) {
					isGrayscale++;
					// specular only: output just the roughness channel
					// always export specular in inverted
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, CHANNEL_RED, &tile, LCT_RGB);
					// output the channel if it's not all black
					bool allBlack = true;
					if (gUseCategory[CATEGORY_ROUGHNESS] && !channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
						allBlack = false;
						invertChannel(destination_ptr);

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].rootName);
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
							wprintf(L"New chest texture '%s' created.\n", outputFile);
						}
					}
					writepng_cleanup(destination_ptr);
					if (!allBlack && outputMerged) {
						progimage_info* mer_ptr = allocateRGBImage(&tile);
						StoMER(mer_ptr, &tile);
						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].rootName);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

						rc = writepng(mer_ptr, 3, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit
							return filesRead;
						}
						if (verbose) {
							wprintf(L"New specular-only MER chest texture '%s' created.\n", outputFile);
						}
						writepng_cleanup(mer_ptr);
					}
				}
				else {
					isSME++;
					// SME: output all three
					for (int channel = 0; channel < 3; channel++) {
						if (gUseCategory[category[channel]]) {
							// output the channel if it's not all black
							progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
							copyOneChannel(destination_ptr, channel, &tile, LCT_RGB);
							// is the channel, copied over, non-zero?
							if (!channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
								if (channel == CHANNEL_RED) {
									invertChannel(destination_ptr);
								}

								wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
								wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].rootName);
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
									wprintf(L"New chest texture '%s' created.\n", outputFile);
								}
							}
							writepng_cleanup(destination_ptr);
						}
					}
					if (outputMerged) {
						progimage_info* smer_ptr = allocateRGBImage(&tile);
						if (SMEtoMER(smer_ptr, &tile)) {
							wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].rootName);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

							rc = writepng(smer_ptr, 3, outputFile);
							if (rc != 0)
							{
								reportReadError(rc, outputFile);
								// quit
								return filesRead;
							}
							if (verbose) {
								wprintf(L"New merged MER chest texture '%s' created.\n", outputFile);
							}
						}
						else {
							wprintf(L"WARNING: No merged MER chest texture '%s' was generated, as SME conversion resulted in an all-black image.\n", outputFile);
						}
						writepng_cleanup(smer_ptr);
					}
				}
				readImage_cleanup(1, &tile);
			}
		}
	}
	// grotesque-er, lazy coding: copying all the code above, without RGBA testing, and with pot grid changes
	wchar_t outputPotDirectory[MAX_PATH];
	for (int i = 0; i < ppg->totalTiles; i++) {
		int fullIndex = CATEGORY_SPECULAR * ppg->totalTiles + i;
		if (ppg->pr[fullIndex].exists) {
			// file exists - process it.

			// first create pot subdirectory if it doesn't exist
			if (!setPotDirectory(outputDirectory, outputPotDirectory)) {
				break;
			}

			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].fullFilename);

			// read in tile for later
			progimage_info tile;
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, LCT_RGB, imageFileType);
			if (rc != 0)
			{
				reportReadError(rc, inputFile);
				continue;
			}
			else {
				filesRead++;

				wchar_t outputFile[MAX_PATH_AND_FILE];
				if (isNearlyGrayscale(&tile, 3)) {
					isGrayscale++;
					// specular only: output just the roughness channel
					// always export specular in inverted
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, CHANNEL_RED, &tile, LCT_RGB);
					// output the channel if it's not all black
					bool allBlack = true;
					if (gUseCategory[CATEGORY_ROUGHNESS] && !channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
						allBlack = false;
						invertChannel(destination_ptr);

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].rootName);
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
							wprintf(L"New decorated pot texture '%s' created.\n", outputFile);
						}
					}
					writepng_cleanup(destination_ptr);
					if (!allBlack && outputMerged) {
						progimage_info* mer_ptr = allocateRGBImage(&tile);
						StoMER(mer_ptr, &tile);
						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].rootName);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

						rc = writepng(mer_ptr, 3, outputFile);
						if (rc != 0)
						{
							reportReadError(rc, outputFile);
							// quit
							return filesRead;
						}
						if (verbose) {
							wprintf(L"New specular-only MER decorated pot texture '%s' created.\n", outputFile);
						}
						writepng_cleanup(mer_ptr);
					}
				}
				else {
					isSME++;
					// SME: output all three
					for (int channel = 0; channel < 3; channel++) {
						if (gUseCategory[category[channel]]) {
							// output the channel if it's not all black
							progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
							copyOneChannel(destination_ptr, channel, &tile, LCT_RGB);
							// is the channel, copied over, non-zero?
							if (!channelEqualsValue(destination_ptr, 0, 1, 0, 0)) {
								if (channel == CHANNEL_RED) {
									invertChannel(destination_ptr);
								}

								wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
								wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].rootName);
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
									wprintf(L"New decorated pot texture '%s' created.\n", outputFile);
								}
							}
							writepng_cleanup(destination_ptr);
						}
					}
					if (outputMerged) {
						progimage_info* smer_ptr = allocateRGBImage(&tile);
						if (SMEtoMER(smer_ptr, &tile)) {
							wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].rootName);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, gCatSuffixes[CATEGORY_MER]);
							wcscat_s(outputFile, MAX_PATH_AND_FILE, L".png");

							rc = writepng(smer_ptr, 3, outputFile);
							if (rc != 0)
							{
								reportReadError(rc, outputFile);
								// quit
								return filesRead;
							}
							if (verbose) {
								wprintf(L"New merged MER decorated pot texture '%s' created.\n", outputFile);
							}
						}
						else {
							wprintf(L"WARNING: No merged MER decorated pot texture '%s' was generated, as SME conversion resulted in an all-black image.\n", outputFile);
						}
						writepng_cleanup(smer_ptr);
					}
				}
				readImage_cleanup(1, &tile);
			}
		}
	}
	if (isGrayscale > 0 && isSME > 0) {
		wprintf(L"WARNING: The input files with a suffix of '*_s.png' seem to be of two formats:\n    %d are specular-only, %d are specular/metallic/emissive.\n", isGrayscale, isSME);
	}
	else if (verbose) {
		wprintf(L"Specular input files processed: %d are specular-only, %d are specular/metallic/emissive.\n", isGrayscale, isSME);
	}
	return filesRead;
}

static int processMERFiles(FileGrid* pfg, ChestGrid* pcg, DecoratedPotGrid* ppg, const wchar_t* outputDirectory, bool verbose) {
	int rc;
	int isMER = 0;
	int filesRead = 0;
	int imageFileType;

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
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, LCT_RGB, imageFileType);
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
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, channel, &tile, LCT_RGB);
					// output the channel if it's not all black (or white, for roughness)
					if (gUseCategory[category[channel]] && 
						((channel == 2) ? !channelEqualsValue(destination_ptr, 0, 1, 255, 0) : !channelEqualsValue(destination_ptr, 0, 1, 0, 0))) {

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
							wprintf(L"New texture '%s' fullIndex %d created due to MER.\n", outputFile, fullIndex);
						//}
					}
					writepng_cleanup(destination_ptr);
				}

				readImage_cleanup(1, &tile);
			}
		}
	}

	// lame code copying of code above for chests
	wchar_t outputChestDirectory[MAX_PATH];
	for (int i = 0; i < pcg->totalTiles; i++) {
		int fullIndex = CATEGORY_MER * pcg->totalTiles + i;
		if (pcg->cr[fullIndex].exists) {
			// first create chest subdirectory if it doesn't exist
			if (!setChestDirectory(outputDirectory, outputChestDirectory)) {
				break;
			}

			isMER++;

			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wchar_t inputFile[MAX_PATH_AND_FILE];
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].fullFilename);

			// read in tile
			progimage_info tile;
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, LCT_RGB, imageFileType);
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
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, channel, &tile, LCT_RGB);
					// output the channel if it's not all black (or white, for roughness)
					if (gUseCategory[category[channel]] &&
						((channel == 2) ? !channelEqualsValue(destination_ptr, 0, 1, 255, 0) : !channelEqualsValue(destination_ptr, 0, 1, 0, 0))) {

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputChestDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, pcg->cr[fullIndex].rootName);
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
							wprintf(L"New chest texture '%s' created.\n", outputFile);
						//}
					}
					writepng_cleanup(destination_ptr);
				}

				readImage_cleanup(1, &tile);
			}
		}
	}
	// lamer code copying of code above for decorated pots
	wchar_t outputPotDirectory[MAX_PATH];
	for (int i = 0; i < ppg->totalTiles; i++) {
		int fullIndex = CATEGORY_MER * ppg->totalTiles + i;
		if (ppg->pr[fullIndex].exists) {
			// first create pot subdirectory if it doesn't exist
			if (!setPotDirectory(outputDirectory, outputPotDirectory)) {
				break;
			}

			isMER++;

			// file exists - process it.
			// Read file's contents.
			// Check if file's channels are all quite close to one another:
			// If so, then this is just a roughness map.
			// Else, this is an SME texture, so output each.
			wchar_t inputFile[MAX_PATH_AND_FILE];
			wcscpy_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].path);
			wcscat_s(inputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].fullFilename);

			// read in tile
			progimage_info tile;
			imageFileType = isImageFile(inputFile);
			rc = readImage(&tile, inputFile, LCT_RGB, imageFileType);
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
					progimage_info* destination_ptr = allocateGrayscaleImage(&tile);
					copyOneChannel(destination_ptr, channel, &tile, LCT_RGB);
					// output the channel if it's not all black (or white, for roughness)
					if (gUseCategory[category[channel]] &&
						((channel == 2) ? !channelEqualsValue(destination_ptr, 0, 1, 255, 0) : !channelEqualsValue(destination_ptr, 0, 1, 0, 0))) {

						wcscpy_s(outputFile, MAX_PATH_AND_FILE, outputPotDirectory);
						wcscat_s(outputFile, MAX_PATH_AND_FILE, ppg->pr[fullIndex].rootName);
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
							wprintf(L"New decorated pot texture '%s' created.\n", outputFile);
						//}
					}
					writepng_cleanup(destination_ptr);
				}

				readImage_cleanup(1, &tile);
			}
		}
	}
	if (verbose) {
		wprintf(L"%d MER input files processed.\n", isMER);
	}

	return filesRead;
}

// true for success, false for there was a serious error
static bool setChestDirectory(const wchar_t* outputDirectory, wchar_t* outputChestDirectory)
{
	// copy directory over and add "\chest" - repetitive, and should really just test another way, but
	// this operation is not done much.
	wcscpy_s(outputChestDirectory, MAX_PATH, outputDirectory);
	// Note: the \\ is put at the end here, so we don't have to do it elsewhere
	wcscat_s(outputChestDirectory, MAX_PATH, L"chest\\");
	
	// lazy global - check if we've done this operation before
	if (!gChestDirectoryExists && !gChestDirectoryFailed) {
		gChestDirectoryExists = true;
		if (!createDir(outputChestDirectory)) {
			// does not exist and could not create it
			wsprintf(gErrorString, L"***** ERROR: Output chest directory %s cannot be accessed. No chest tiles will be saved.\n", outputChestDirectory);
			saveErrorForEnd();
			gErrorCount++;
			gChestDirectoryFailed = true;
		}
	}
	return !gChestDirectoryFailed;
}

// true for success, false for there was a serious error
static bool setPotDirectory(const wchar_t* outputDirectory, wchar_t* outputPotDirectory)
{
	// copy directory over and add "\decorated_pot" - repetitive, and should really just test another way, but
	// this operation is not done much.
	wcscpy_s(outputPotDirectory, MAX_PATH, outputDirectory);
	// Note: the \\ is put at the end here, so we don't have to do it elsewhere
	wcscat_s(outputPotDirectory, MAX_PATH, L"decorated_pot\\");

	// lazy global - check if we've done this operation before
	if (!gDecoratedPotDirectoryExists && !gDecoratedPotDirectoryFailed) {
		gDecoratedPotDirectoryExists = true;
		if (!createDir(outputPotDirectory)) {
			// does not exist and could not create it
			wsprintf(gErrorString, L"***** ERROR: Output decorated pot directory %s cannot be accessed. No decorated pot tiles will be saved.\n", outputPotDirectory);
			saveErrorForEnd();
			gErrorCount++;
			gDecoratedPotDirectoryFailed = true;
		}
	}
	return !gDecoratedPotDirectoryFailed;
}


/*
static void createCompositedLeaves(const wchar_t* inputDirectory, const wchar_t* outputDirectory, const wchar_t* outputSuffix, bool verbose)
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

	delete grass_overlay;
	delete grass_side;

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
		progimage_info tile_template;
		progimage_info* tile = &tile_template;

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
	case 102:
		wsprintf(gErrorString, L"***** ERROR [%s] - could not read Targa TGA file header.\n", filename);
		break;
	case 103:
		wsprintf(gErrorString, L"***** ERROR [%s] - could not read Targa TGA file data.\n", filename);
		break;
	case 104:
		wsprintf(gErrorString, L"***** ERROR [%s] - unsupported Targa TGA file type.\n", filename);
		break;
	case 999:
		wsprintf(gErrorString, L"***** ERROR [%s] - unknown image file type.\n", filename);
		break;
	default:
		wsprintf(gErrorString, L"***** ERROR [%s] read failed - unknown readpng_init() error.\n", filename);
		break;
	}
	saveErrorForEnd();
	gErrorCount++;

	if (rc != 78 && rc < 100) {
		wsprintf(gErrorString, L"Often this means the PNG file has some small bit of information that ChannelMixer cannot\n    handle. You might be able to fix this error by opening this PNG file in\n    Irfanview or other viewer and then saving it again. This has been known to clear\n    out any irregularity that ChannelMixer's somewhat-fragile PNG reader dies on.\n");
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

static int isNearlyGrayscale(progimage_info* src, int channels)
{
	// if R = G = B +- grayEpsilon for whole image, this is a grayscale image
	int grayEpsilon = 14;	// number found to work for Absolution, which is 13 max

	int row, col;
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (abs(src_data[0] - src_data[1]) > grayEpsilon ||
				// if we're reading four channels, ignore emission channel for comparison
				((channels != 4) && (abs(src_data[1] - src_data[2]) > grayEpsilon)))
			{
				return 0;
			}
			src_data += channels;
		}
	}
	return 1;
}

// for RGBA files
static bool isAlphaSemitransparent(progimage_info* src)
{
	int row, col;
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < src->height; row++)
	{
		for (col = 0; col < src->width; col++)
		{
			if (src_data[3] < 255 && src_data[3] > 0 )
			{
				return true;
			}
			src_data += 4;
		}
	}
	return false;
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

// true if there is a non-black pixel
static void StoMER(progimage_info* dst, progimage_info* src)
{
	int row, col;
	unsigned char* dst_data = &dst->image_data[0];
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			// M
			*dst_data++ = 0;
			// E
			*dst_data++ = 0;
			// R - invert S
			*dst_data++ = 255 - src_data[0];
			src_data += 3;
		}
	}
}

// true if there is a non-black pixel
static bool SMEtoMER(progimage_info* dst, progimage_info* src)
{
	bool allBlack = true;
	int row, col;
	unsigned char* dst_data = &dst->image_data[0];
	unsigned char* src_data = &src->image_data[0];
	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			// if all channels are black, the default values, then don't output the MER texture,
			// as this texture would be ignored by Mineways anyway.
			if (allBlack && (src_data[0] != 0 || src_data[1] != 0 || src_data[2] != 0)) {
				allBlack = false;
			}
			// M
			*dst_data++ = src_data[1];
			// E
			*dst_data++ = src_data[2];
			// R - invert S
			*dst_data++ = 255 - src_data[0];
			src_data += 3;
		}
	}
	return !allBlack;
}

