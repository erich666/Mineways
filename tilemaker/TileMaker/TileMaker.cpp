// TileMaker : Pull in a base set of tiles and overlay all tiles found in the tiles directory. For Mineways.
//
// Step 1: Read in the terrain.png file.
// Step 2: Read all tiles in the tiles directory, get name and size of each. Check name for if it's in the known
// set of tiles.
// Step 3: make the background tile (which contains defaults) the proper size of the largest tile found in "tiles". 
// For example, if a 64x64 tile is found, the background set of tile are all expanded to this size.
// Step 4: overlay new tiles. Write out new tile set as terrain_new.png

#include "rwpng.h"
#include <assert.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#include "tiles.h"

//#define TILE_PATH	L".\\blocks\\"
#define BASE_INPUT_FILENAME			L"terrainBase.png"
#define TILE_PATH	L"blocks"
#define OUTPUT_FILENAME L"terrainExt.png"

int findTile( wchar_t *tileName, int alternate );
int findNextTile( wchar_t *tileName, int index, int alternate );
int findUnneededTile( wchar_t *tileName );
int trueWidth(int tileLoc);

static int buildPathAndReadTile(wchar_t* tilePath, wchar_t* fileName, progimage_info *pTile);
static void reportReadError( int rc, wchar_t *filename );

static void setBlackAlphaPNGTile(int chosenTile, progimage_info *src);
static int copyPNGTile(progimage_info *dst, unsigned long dst_x, unsigned long dst_y, unsigned long chosenTile, progimage_info *src,
	unsigned long dst_x_lo, unsigned long dst_y_lo, unsigned long dst_x_hi, unsigned long dst_y_hi, unsigned long src_x_lo, unsigned long src_y_lo, unsigned long flags, float zoom);
static void multPNGTileByColor(progimage_info *dst, int dst_x, int dst_y, int *color);
static void getPNGPixel(progimage_info *src, int col, int row, unsigned char *color);
static void getBrightestPNGPixel(progimage_info *src, unsigned long col, unsigned long row, unsigned long res, unsigned char *color, int *locc, int *locr);
static int computeVerticalTileOffset(progimage_info *src, int chosenTile);
static int isPNGTileEmpty( progimage_info *dst, int dst_x, int dst_y );
static void makePNGTileEmpty(progimage_info *dst, int dst_x, int dst_y);
static void makeSolidTile(progimage_info *dst, int chosenTile, int solid);

static void copyPNG(progimage_info *dst, progimage_info *src);
static void copyPNGArea(progimage_info *dst, unsigned long dst_x_min, unsigned long dst_y_min, unsigned long size_x, unsigned long size_y, progimage_info *src, int src_x_min, int src_y_min);

static int checkForCutout(progimage_info *dst);

typedef struct ChestData {
	int fromX;
	int fromY;
	int sizeX;
	int sizeY;
	int txrX;   // column and row, from upper left, of 64x64 chest tile
	int txrY;
	int toX;
	int toY;
	unsigned int flags;
} ChestData;

static ChestData gNormalChest[] = {
	//  from,    size, to tile,  starting at corner
	{  0,  0,   6,  5,   7, 26,   0, 0,  0x0 },	// MWO_chest_latch
	{ 14,  0,  14, 14,   9,  1,   1, 1,  0x0 },	// MWO_chest_top
	{  0, 14,  14,  4,  10,  1,   1, 2,  0x0 },	// top of MWO_chest_side
	{  0, 33,  14, 10,  10,  1,   1, 6,  0x0 },	// bottom of MWO_chest_side
	{ 14, 14,  14,  4,  11,  1,   1, 2,  0x0 },	// top of MWO_chest_front
	{ 14, 33,  14, 10,  11,  1,   1, 6,  0x0 },	// bottom of MWO_chest_front
};

static ChestData gNormalDoubleChest[] = {
	//  from,    size, to tile,  starting at corner
	{ 14, 14,  15,  4,   9,  2,  1, 2,  0x0 },	// MWO_double_chest_front_left top
	{ 14, 33,  15, 10,   9,  2,  1, 6,  0x0 },	// MWO_double_chest_front_left bottom
	{ 29, 14,  15,  4,  10,  2,  0, 2,  0x0 },	// MWO_double_chest_front_right top
	{ 29, 33,  15, 10,  10,  2,  0, 6,  0x0 },	// MWO_double_chest_front_right bottom
	{ 58, 14,  15,  4,   9,  3,  1, 2,  0x0 },	// MWO_double_chest_back_left top
	{ 58, 33,  15, 10,   9,  3,  1, 6,  0x0 },	// MWO_double_chest_back_left bottom
	{ 73, 14,  15,  4,  10,  3,  0, 2,  0x0 },	// MWO_double_chest_back_right top
	{ 73, 33,  15, 10,  10,  3,  0, 6,  0x0 },	// MWO_double_chest_back_right bottom
	{ 14,  0,  15, 14,   9, 14,  1, 1,  0x0 },	// MWO_double_chest_top_left
	{ 29,  0,  15, 14,  10, 14,  0, 1,  0x0 },	// MWO_double_chest_top_right
};

static ChestData gEnderChest[] = {
	//  from,    size, to tile,  starting at corner
	{  0,  0,   6,  5,   9, 13,   0, 0,  0x0 },	// MWO_ender_chest_latch
	{ 14,  0,  14, 14,  10, 13,   1, 1,  0x0 },	// MWO_ender_chest_top
	{  0, 14,  14,  4,  11, 13,   1, 2,  0x0 },	// top of MWO_ender_chest_side
	{  0, 33,  14, 10,  11, 13,   1, 6,  0x0 },	// bottom of MWO_ender_chest_side
	{ 14, 14,  14,  4,  12, 13,   1, 2,  0x0 },	// top of MWO_ender_chest_front
	{ 14, 33,  14, 10,  12, 13,   1, 6,  0x0 },	// bottom of MWO_ender_chest_front
};


// from: from upper left pixel
// size: resolution; negative means scan backwards, and compute lower right and go from there (for my sanity)
// to tile: in the grid of tiles, where it goes
// starting at corner: point to start in 16x16 tile itself
static ChestData gNormalChest115[] = {
	//  from,    size, to tile,  starting at corner
	{  1,  0,   6,  5,   7, 26,   0, 0,  0x3 },	// MWO_chest_latch
	{ 28,  0,  14, 14,   9,  1,   1, 1,  0x3 },	// MWO_chest_top
	{  0, 15,  14,  4,  10,  1,   1, 2,  0x3 },	// top of MWO_chest_side
	{  0, 33,  14, 10,  10,  1,   1, 6,  0x3 },	// bottom of MWO_chest_side
	{ 42, 15,  14,  4,  11,  1,   1, 2,  0x3 },	// top of MWO_chest_front
	{ 42, 33,  14, 10,  11,  1,   1, 6,  0x3 },	// bottom of MWO_chest_front
};

// Minecraft names these left and right, but in fact they're swapped when rendered,
// with the right chest tile's elements being put on the left, and vice versa.
static ChestData gNormalLeftChest115[] = {
	//  from,    size, to tile,  starting at corner
	{ 43, 15,  15,  4,  10,  2,  0, 2,  0x3 },	// MWO_double_chest_front_left top half
	{ 43, 33,  15, 10,  10,  2,  0, 6,  0x3 },	// MWO_double_chest_front_left bottom half
	{ 14, 15,  15,  4,  10,  3,  0, 2,  0x2 },	// MWO_double_chest_back_left top half - should really swap with RightChest, but nah
	{ 14, 33,  15, 10,  10,  3,  0, 6,  0x2 },	// MWO_double_chest_back_left bottom half - should really swap with RightChest, but nah
	{ 14, 19,  15, 14,  10, 14,  0, 1,  0x2 },	// MWO_double_chest_top_left
};

static ChestData gNormalRightChest115[] = {
	//  from,    size, to tile,  starting at corner
	{ 43, 15,  15,  4,   9,  2,  1, 2,  0x3 },	// MWO_double_chest_front_right top half
	{ 43, 33,  15, 10,   9,  2,  1, 6,  0x3 },	// MWO_double_chest_front_right bottom half
	{ 14, 15,  15,  4,   9,  3,  1, 2,  0x2 },	// MWO_double_chest_back_right top half
	{ 14, 33,  15, 10,   9,  3,  1, 6,  0x2 },	// MWO_double_chest_back_right bottom half
	{ 14, 19,  15, 14,   9, 14,  1, 1,  0x2 },	// MWO_double_chest_top_right - note we don't flip
};

static ChestData gEnderChest115[] = {
	//  from,    size, to tile,  starting at corner
	{  1,  0,   6,  5,   9, 13,   0, 0,  0x3 },	// MWO_ender_chest_latch
	{ 28,  0,  14, 14,  10, 13,   1, 1,  0x3 },	// MWO_ender_chest_top
	{  0, 15,  14,  4,  11, 13,   1, 2,  0x3 },	// top of MWO_ender_chest_side
	{  0, 33,  14, 10,  11, 13,   1, 6,  0x3 },	// bottom of MWO_ender_chest_side
	{ 42, 15,  14,  4,  12, 13,   1, 2,  0x3 },	// top of MWO_ender_chest_front
	{ 42, 33,  14, 10,  12, 13,   1, 6,  0x3 },	// bottom of MWO_ender_chest_front
};

typedef struct Chest {
	wchar_t* wname;
	int numCopies;	// number of elements we'll copy
	int defaultResX;	// how big the image is in the default set
	int defaultresY;
	ChestData* data;
} Chest;

static Chest gChest114[] = {
	{ L"normal", 6, 64, 64, NULL },
	{ L"normal_double", 10, 128, 64, NULL },
	{ L"ender", 6, 64, 64, NULL }
};

static Chest gChest115[] = {
	{ L"normal", 6, 64, 64, NULL },
	{ L"normal_left", 5, 64, 64, NULL },
	{ L"normal_right", 5, 64, 64, NULL },
	{ L"ender", 6, 64, 64, NULL }
};

// make considerably higher so that we can read in many more PNGs than we use.
#define	TOTAL_INPUT_TILES	(TOTAL_TILES*10)

// given array of tiles read in index, return gTile index
static int tilesInputToTableIndex[TOTAL_INPUT_TILES];
static progimage_info tile[TOTAL_INPUT_TILES];

static int gErrorCount = 0;
static int gWarningCount = 0;


typedef struct int2 {
	int x;
	int y;
} int2;

#define	IGNORE_TILE	99999

int wmain(int argc, wchar_t* argv[])
{
	int rc;
	progimage_info basicterrain;
	progimage_info destination;
	progimage_info *destination_ptr = &destination;

	int i, j;
	int index;
	int width;

	int tilesFound = 0;
	int tilesTableIndexToInput[TOTAL_TILES];
	int baseTileSize, xTiles, baseYTiles, baseXResolution, baseYResolution;
	int outputTileSize, outputYTiles;
	unsigned long outputXResolution, outputYResolution;

	wchar_t terrainBase[MAX_PATH];
	wchar_t terrainExtOutput[MAX_PATH];
	wchar_t tilePath[MAX_PATH];

	int argLoc = 1;

	int overlayTileSize = 0;
	int forcedTileSize = 0;
	int chosenTile = 0;

	int nobase = 0;
	int notiles = 0;
	int onlyreplace = 0;
	int verbose = 0;
	int checkmissing = 0;
    int alternate = 1;  // always include alternate names; needed for 1.13
    int solid = 0;
    int solidcutout = 0;
	int buildnormals = 0;
	int buildspecular = 0;

    bool shulkerSide[16], shulkerBottom[16];

	wcscpy_s(terrainBase, MAX_PATH, BASE_INPUT_FILENAME);
	wcscpy_s(tilePath, MAX_PATH, TILE_PATH );
	wcscpy_s(terrainExtOutput, MAX_PATH, OUTPUT_FILENAME );

	for (i = 0; i < TOTAL_TILES; i++) {
		tilesTableIndexToInput[i] = -1;
	}
    memset(shulkerSide, 0, 16 * sizeof(bool));
    memset(shulkerBottom, 0, 16 * sizeof(bool));

	// usage: [-i terrainBase.png] [-d tiles_directory] [-o terrainExt.png] [-t forceTileSize]
	// single argument is alternate subdirectory other than "tiles"
	while (argLoc < argc)
	{
		if ( wcscmp(argv[argLoc],L"-i") == 0 )
		{
			argLoc++;
			wcscpy_s(terrainBase,MAX_PATH,argv[argLoc]);
		}
		else if ( wcscmp(argv[argLoc],L"-d") == 0 )
		{
			argLoc++;
			wcscpy_s(tilePath,MAX_PATH,argv[argLoc]);
		}
		else if ( wcscmp(argv[argLoc],L"-o") == 0 )
		{
			argLoc++;
			wcscpy_s(terrainExtOutput,MAX_PATH,argv[argLoc]);
		}
		else if ( wcscmp(argv[argLoc],L"-t") == 0 )
		{
			// force to a given tile size.
			argLoc++;
			swscanf_s( argv[argLoc], L"%d", &forcedTileSize );
		}
		else if ( wcscmp(argv[argLoc],L"-c") == 0 )
		{
			// choose which tile of multiple tiles to use.
			argLoc++;
			swscanf_s( argv[argLoc], L"%d", &chosenTile );
		}
		else if ( wcscmp(argv[argLoc],L"-nb") == 0 )
		{
			// no base background image; mostly for debug, to see which tiles we actually have ready.
			nobase = 1;
		}
		else if ( wcscmp(argv[argLoc],L"-nt") == 0 )
		{
			// no tiles
			notiles = 1;
		}
		else if ( wcscmp(argv[argLoc],L"-r") == 0 )
		{
			// replace with tiles from directory only those tiles that don't exist (i.e. base terrain wins)
			onlyreplace = 1;
		}
		else if ( wcscmp(argv[argLoc],L"-m") == 0 )
		{
			// Check for missing tiles, i.e. look for names in tiles.h that do not have a corresponding tile
			// in the tile directory. This lets people know what tiles they need to add.
			checkmissing = 1;
		}
        else if (wcscmp(argv[argLoc], L"-a") == 0)
        {
            // alternate: use names such as "blockIron" when "iron_block" is not found
            //alternate = 1;
            wprintf(L"Note: alternate names are always on now, so -a is no longer needed.\n");
        }
        else if (wcscmp(argv[argLoc], L"-na") == 0)
        {
            // turn alternate names off
			wprintf(L"Note: alternate names are needed for 1.13, so -na is no longer supported.\n");
		}
        else if (wcscmp(argv[argLoc], L"-s") == 0)
        {
            // solid: take the average color of the incoming tile and output this solid color
            solid = 1;
        }
		else if (wcscmp(argv[argLoc], L"-S") == 0)
		{
			// solid cutout: as above, but preserve the cutout transparent areas
			solidcutout = 1;
		}
		else if (wcscmp(argv[argLoc], L"-bn") == 0)
		{
			// build normal map version
			buildnormals = 1;
		}
		else if (wcscmp(argv[argLoc], L"-bs") == 0)
		{
			// build normal map version
			buildspecular = 1;
		}
		else if ( wcscmp(argv[argLoc],L"-v") == 0 )
        {
            // verbose: tell when normal things happen
            verbose = 1;
        }
		else
		{
			// go to here-----------------------------------------------------------------------------|
			wprintf( L"TileMaker version 2.12\n");  // change version below, too
			wprintf( L"usage: TileMaker [-i terrainBase.png] [-d blocks] [-o terrainExt.png]\n        [-t tileSize] [-c chosenTile] [-nb] [-nt] [-r] [-m] [-v]\n");
			wprintf( L"  -i terrainBase.png - image containing the base set of terrain blocks\n    (includes special chest tiles). Default is 'terrainBase.png'.\n");
			wprintf( L"  -d blocks - directory of block textures to overlay on top of the base.\n    Default directory is 'blocks'.\n");
			wprintf( L"  -o terrainExt.png - the resulting terrain image, used by Mineways. Default is\n    terrainExt.png.\n");
			wprintf( L"  -t tileSize - force a given (power of 2) tile size for the resulting terrainExt.png\n    file, e.g. 32, 128. Useful for zooming or making a 'draft quality'\n    terrainExt.png. If not set, largest tile found is used.\n");
			wprintf( L"  -c chosenTile - for tiles with multiple versions (e.g. water, lava, portal),\n    choose which tile to use. 0 means topmost, 1 second from top, 2 etc.;\n    -1 bottommost, -2 next to bottom.\n");
			wprintf( L"  -nb - no base; the base texture terrainBase.png is not read. This option is\n    good for seeing what images are in the blocks directory, as these are\n    what get put into terrainExt.png.\n");
			wprintf( L"  -nt - no tile directory; don't read in any images in the 'blocks' directory,\n    only the base image is read (and probably zoomed, otherwise this\n    option is pointless).\n");
			wprintf( L"  -r - replace (from the 'blocks' directory) only those tiles not in the base\n    texture. This is a way of extending a base texture to new versions.\n");
            wprintf( L"  -m - to report all missing tiles, ones that Mineways uses but were not in the\n    tiles directory.\n");
            wprintf( L"  -s - take the average color of the incoming tile and output this solid color.\n");
			wprintf( L"  -S - as above, but preserve the cutout transparent areas.\n");
			wprintf( L"  -bn - build normal map terrainExt_n.png using all *_n.png files as input.\n");
			wprintf( L"  -bs - build specular map terrainExt_n.png using all *_s.png files as input.\n");
			wprintf( L"  -v - verbose, explain everything going on. Default: display only warnings.\n");
			return 1;
		}
		argLoc++;
	}

    if ( verbose )
        wprintf(L"TileMaker version 2.12\n");  // change version above, too

	// add / to tile directory path
	if ( wcscmp( &tilePath[wcslen(tilePath)-1], L"\\") != 0 )
	{
		wcscat_s(tilePath, MAX_PATH, L"\\" );
	}

	// add ".png" to tile output name
	if ( _wcsicmp( &terrainExtOutput[wcslen(terrainExtOutput)-4], L".png" ) != 0 )
	{
		wcscat_s(terrainExtOutput, MAX_PATH, L".png" );
	}

	xTiles = 16;	// this should always be the same for all things
	if ( !nobase )
	{
		// read the base terrain file
		rc = readpng(&basicterrain, terrainBase);
		if ( rc != 0 )
		{
			reportReadError(rc,terrainBase);
			return 1;
		}
		readpng_cleanup(0,&basicterrain);
		if ( verbose )
			wprintf (L"The base terrain is '%s'\n", terrainBase);

		baseTileSize = basicterrain.width / xTiles;
		baseYTiles = basicterrain.height / baseTileSize;
	}
	else
	{
		if ( verbose )
			wprintf (L"No base terrain file is set.\n");
		// minimums
		baseTileSize = 16;
		baseYTiles = VERTICAL_TILES;
	}
	baseXResolution = xTiles * baseTileSize;
	baseYResolution = baseYTiles * baseTileSize;

	// output should be the size of the output number of tiles
	outputYTiles = VERTICAL_TILES; // used to be baseYTiles - that's no good

	// look through tiles in tiles directory, see which exist, find maximum Y value.
	if ( !notiles )
	{
		HANDLE hFind;
		WIN32_FIND_DATA ffd;

		wchar_t tileSearch[MAX_PATH];
		wcscpy_s( tileSearch, MAX_PATH, tilePath );
		wcscat_s( tileSearch, MAX_PATH, L"*.png" );
		hFind=FindFirstFile(tileSearch,&ffd);

		if (hFind == INVALID_HANDLE_VALUE) 
		{
			printf ("***** ERROR: FindFirstFile failed (error # %d).\n", GetLastError());
			wprintf (L"No files found - please put your new blocks in the directory '%s'.\n", tilePath);
			return 1;
		} 
		else 
		{
			do {
				wchar_t tileName[MAX_PATH];
				int len;

				if ( verbose )
					wprintf (L"The file '%s' has been found and will be used.\n", ffd.cFileName);

				wcscpy_s( tileName, MAX_PATH, ffd.cFileName );
				// check for .png suffix - note test is case insensitive
				len = (int)wcslen(tileName);
				if ( _wcsicmp( &tileName[len-4], L".png" ) == 0 )
				{
					// remove .png suffix
					tileName[len-4] = 0x0;
					index = findTile(tileName, alternate);
					if ( index < 0 )
					{
                        // see if tile is on unneeded list
						if (findUnneededTile(ffd.cFileName) < 0) {
							wprintf(L"WARNING: '%s' is an image name that TileMaker does not understand.\n  This means you are using a non-standard name for it.\n  See https://github.com/erich666/Mineways/blob/master/Win/tiles.h\n  for the image file names used.\n", ffd.cFileName);
							gWarningCount++;
						}
					}

					while ( index >= 0 )
					{
						int fail_code = 0;
						//wprintf(L"INDEX: %d\n", index);

						// check if already set
						if (tilesTableIndexToInput[index] < 0)
						{
							// tile is one we care about.
							fail_code = buildPathAndReadTile(tilePath, ffd.cFileName, &tile[tilesFound]);

							if (!fail_code) {
								if (fmod(log2((float)(tile[tilesFound].width)), 1.0f) != 0.0f) {
									wprintf(L"***** ERROR: file '%s' has a width that is not a power of two.\n  This will cause copying errors, so we ignore it.\n  We recommend you remove or resize this file.\n", ffd.cFileName);
									fail_code = 1;
									gErrorCount++;
								}
								if (tile[tilesFound].width > tile[tilesFound].height) {
									wprintf(L"***** ERROR: file '%s' has a height that is less than its width.\n  This will cause copying errors, so we ignore it.\n  We recommend you remove or resize this file.\n", ffd.cFileName);
									fail_code = 1;
									gErrorCount++;
								}
							}

							// check for unsupported formats
							//if ( 
							//	//( tile[tilesFound].bit_depth == 8 || tile[tilesFound].bit_depth == 4 ) &&
							//	 ( tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB_ALPHA || 
							//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB || 
							//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_GRAY || 
							//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_PALETTE ))
							if (fail_code == 0)
							{
								// check if tile has an alpha == 0; if so, it must have SBIT_DECAL or SBIT_CUTOUT_GEOMETRY set
								if (!(gTilesTable[index].flags & (SBIT_DECAL | SBIT_CUTOUT_GEOMETRY | SBIT_ALPHA_OVERLAY))) {
									// flag not set, so check for alpha == 0
									if (checkForCutout(&tile[tilesFound])) {
										wprintf(L"WARNING: file '%s' has texels that are fully transparent, but the image is not identified as having cutout geometry, being a decal, or being an overlay.\n", ffd.cFileName);
										gWarningCount++;
									}
								}

								if (tilesFound >= TOTAL_INPUT_TILES) {
									wprintf(L"INTERNAL ERROR: the number of (unused) tiles is extremely high - please delete PNGs not needed and run again.\n");
									gErrorCount++;
								}
								else {
									// The way this works:
									// tilesFound starts at 0 and is incremented every time an input tile is successfully read in.
									// gTiles is the list of tiles read in, with tilesFound being the number of tiles in this list.
									// So, tilesInputToTableIndex says, given an input file array location, what index value in tiles.h is it associated with?
									// And tilesTableIndexToInput says, given a location in the tiles.h file, which tile, if any, is associated with it? -1 means no association.
									tilesInputToTableIndex[tilesFound] = index;

									tilesTableIndexToInput[index] = tilesFound;	// note tile is used if >= 0 - currently we don't use this back-access, but someday, perhaps. Right now it's just for noting if a tile in the table has a texture.
									tilesFound++;

									// Find maximum Y resolution of output tile: expand bottom of output texture if found.
									// This is an attempt to have some compatibility as we add new texture tiles to the bottom of terrainExt.png.
									// This should never be true now (outputYTiles gets set to VERTICAL_TILES), but if VERTICAL_TILES isn't set right, this will push things up
									if (outputYTiles - 1 < gTilesTable[index].txrY)
									{
										outputYTiles = gTilesTable[index].txrY + 1;
										wprintf(L"INTERNAL WARNING: strangely, the number of images outpaces the value of 16*VERTICAL_TILES.\n  This is an internal error: update VERTICAL_TILES.\n");
										gWarningCount++;
									}
								}
							}
						}
						else {
							wprintf(L"WARNING: file '%s.png' and alternate file '%s' both found.\n  File '%s' ignored, only because it was found second.\n  To use it, remove '%s.png' from the blocks directory.\n",
								gTilesTable[index].filename, ffd.cFileName, ffd.cFileName, gTilesTable[index].filename);
							gWarningCount++;
						}
						//else
						//{
						//	// unknown format
						//	_tprintf (TEXT("WARNING: file '%s' not used because unsupported bit depth %d and color type %d\n"), ffd.cFileName, tile[tilesFound].bit_depth, tile[tilesFound].color_type );
						//}
						index = findNextTile(tileName, index, alternate);
					}
				}
			} while (FindNextFile(hFind,&ffd)!=0);

			FindClose(hFind);
		}
	}

	// if smooth_stone is missing, use stone_slab_top, and vice versa
	int smooth_stone_index = findTile(L"smooth_stone", 1);
	int stone_slab_top_index = findTile(L"stone_slab_top", 1);
	if ((tilesTableIndexToInput[smooth_stone_index] == -1) ^ (tilesTableIndexToInput[stone_slab_top_index] == -1)) {
		// found one, not the other
		if (tilesTableIndexToInput[smooth_stone_index] < 0) {
			// missing smooth_stone, so set it
			index = findTile(L"smooth_stone", 1);
			rc = buildPathAndReadTile(tilePath, L"stone_slab_top.png", &tile[tilesFound]);
		}
		else {
			// missing stone_slab_top, so set it
			index = findTile(L"stone_slab_top", 1);
			rc = buildPathAndReadTile(tilePath, L"smooth_stone.png", &tile[tilesFound]);
		}
		if (rc != 0) {
			wprintf(L"INTERNAL WARNING: a tile we just read before for smooth_stone could not be read\n  again. Please report this to erich@acm.org.\n");
			gWarningCount++;
		}
		else {
			tilesInputToTableIndex[tilesFound] = index;
			tilesTableIndexToInput[index] = tilesFound;
			tilesFound++;
		}
	}
	// similarly, if smooth_stone_slab_side is missing, use stone_slab_side, and vice versa
	int smooth_stone_slab_side_index = findTile(L"smooth_stone_slab_side", 1);
	int stone_slab_side_index = findTile(L"stone_slab_side", 1);
	if ((tilesTableIndexToInput[smooth_stone_slab_side_index] == -1) ^ (tilesTableIndexToInput[stone_slab_side_index] == -1)) {
		// found one, not the other
		if (tilesTableIndexToInput[smooth_stone_slab_side_index] < 0) {
			// missing smooth_stone_slab_side, so set it
			index = findTile(L"smooth_stone_slab_side", 1);
			rc = buildPathAndReadTile(tilePath, L"stone_slab_side.png", &tile[tilesFound]);
		}
		else {
			// missing stone_slab_side, so set it
			index = findTile(L"stone_slab_side", 1);
			rc = buildPathAndReadTile(tilePath, L"smooth_stone_slab_side.png", &tile[tilesFound]);
		}
		if (rc != 0) {
			wprintf(L"INTERNAL WARNING: a tile we just read before for smooth_stone_slab_side could not\n  be read again. Please report this to erich@acm.org.\n");
			gWarningCount++;
		}
		else {
			tilesInputToTableIndex[tilesFound] = index;
			tilesTableIndexToInput[index] = tilesFound;
			tilesFound++;
		}
	}

    // look through tiles missing: if shulker side and bottom tiles found, note they don't need to be generated;
	// these are not standard at all - shulkers now have their own entitities - but left in for simplicity.
	// TODO: someday add in shulker box reader, just like chests
    for (i = 0; i < TOTAL_TILES; i++)
    {
        if (wcsncmp(gTilesTable[i].filename, L"shulker_side_", 13) == 0) {
            if (tilesTableIndexToInput[i] < 0) {
                // it's missing, but optional, so ignore it. We mark it with a bogus index.
                tilesTableIndexToInput[i] = IGNORE_TILE;
            }
            else {
                shulkerSide[gTilesTable[i].txrX] = 1;
            }
        }
        else if (wcsncmp(gTilesTable[i].filename, L"shulker_bottom_", 15) == 0) {
            if (tilesTableIndexToInput[i] < 0) {
                // it's missing, but optional, so ignore it.
                tilesTableIndexToInput[i] = IGNORE_TILE;
            }
            else {
                shulkerBottom[gTilesTable[i].txrX] = 1;
            }
        }
    }

	// look for tiles not input?
	if ( checkmissing )
	{
		for ( i = 0; i < TOTAL_TILES; i++ )
		{
			if ( tilesTableIndexToInput[i] < 0 )
			{
				// if it starts with "MW" or is the empty string, ignore miss
				if ( wcslen(gTilesTable[i].filename) > 0 && wcsncmp(gTilesTable[i].filename,L"MW",2) != 0 )
					wprintf (L"This program needs a tile named '%s.png' that was not replaced.\n", gTilesTable[i].filename);
			}
		}
	}

	// find largest tile. Hmmm, beware of flowing lava & water, which is twice as wide.
	for ( i = 0; i < tilesFound; i++ )
	{
		// for water_flow and lava_flow, the width is twice normal, so halve it.
		width = trueWidth(i);

		if (overlayTileSize < width)
		{
			overlayTileSize = width;
		}
	}

	if ( verbose )
		wprintf (L"Largest input image found was %d pixels wide.\n", overlayTileSize);

	// take the larger of the overlay and base tile sizes as the target size
	outputTileSize = ( overlayTileSize > baseTileSize ) ? overlayTileSize : baseTileSize;

	// however, if there's a forced tile size, use that:
	if ( forcedTileSize > 0 )
	{
		outputTileSize = forcedTileSize;

		if ( verbose )
			wprintf (L"Output texture '%s' is forced to have tiles that are each %d pixels wide.\n", terrainExtOutput, outputTileSize);
	}
	else {
		wprintf(L"Output texture '%s' will have tiles that are each %d pixels wide.\n", terrainExtOutput, outputTileSize);
	}

	// warn user of large tiles
	if (outputTileSize > 256) {
		wprintf(L"WARNING: with a texture image size of %d X %d, you could be waiting a long time\n  for TileMaker to complete. Consider quitting and using the '-t tileSize'\n  option, choosing a power of two value less than this, such as 256.\n", outputTileSize, outputTileSize);
		gWarningCount++;
	}

	// allocate output image and fill it up
	destination_ptr = new progimage_info();

	outputXResolution = xTiles * outputTileSize;
	outputYResolution = outputYTiles * outputTileSize;

	destination_ptr->width = outputXResolution;
	destination_ptr->height = outputYResolution;

	// test if new image size to be allocated would be larger than 2^32, which is impossible to allocate (and the image would be unusable anyway)
	if (destination_ptr->width > 16384 ) {
		wprintf(L"***** ERROR: The tile size that is desired, %d X %d, is larger than can be allocated\n  (and likely larger than anything you would ever want to use).\n  Please run again with the '-t tileSize' option, choosing a power of two\n  value less than this, such as 256, 512, or 1024.\n",
			destination_ptr->width/16, destination_ptr->width/16);
		return 1;
	}

	if ( nobase )
	{
		// for debug, to see just the tiles placed
		destination_ptr->image_data.resize(outputXResolution*outputYResolution*4*sizeof(unsigned char),0x0);
	}
	else
	{
		// copy base texture over
		destination_ptr->image_data.resize(outputXResolution*outputYResolution*4*sizeof(unsigned char),0x0);
		copyPNG(destination_ptr, &basicterrain);
		if ( verbose )
			wprintf (L"Base texture '%s' copied to output.\n", terrainBase);
	}

	// copy tiles found over
	for ( i = 0; i < tilesFound; i++ )
	{
		index = tilesInputToTableIndex[i];
		// -r option on?
		if ( onlyreplace )
		{
			if ( !isPNGTileEmpty(destination_ptr, gTilesTable[index].txrX, gTilesTable[index].txrY) )
			{
				wprintf (L"UNUSED: '%s.png' was not used because there is already a image.\n", gTilesTable[index].filename);
				continue;
			}
		}
		if ( gTilesTable[index].flags & SBIT_BLACK_ALPHA )
		{
			setBlackAlphaPNGTile( chosenTile, &tile[i] );
		}
		if (copyPNGTile(destination_ptr, gTilesTable[index].txrX, gTilesTable[index].txrY, chosenTile, &tile[i], 0, 0, 16, 16, 0, 0, 0x0, (float)destination_ptr->width / (float)(trueWidth(i) * 16))) {
			return 1;
		}
		if ( verbose )
			wprintf (L"File '%s.png' merged.\n", gTilesTable[index].filename);
	}

    // Compute shulker box sides and bottoms, if not input
	// first, worth doing?
	bool missingSideOrBottom = false;
	for (i = 0; (i < 16) && !missingSideOrBottom; i++) {
		missingSideOrBottom |= ((shulkerSide[i] == false) || (shulkerBottom[i] == false));
	}
	if (missingSideOrBottom) {
		// Take location 2,2 on the top as the "base color". Multiply by this color, divide by the white color, and then multiply the side and bottom tile by this color. Save.
		unsigned char box_color[4];
		int neutral_color[4], mult_color[4];

		index = findTile(L"white_shulker_box", 1);
		int side_index = findTile(L"MW_SHULKER_SIDE", 1);
		int bottom_index = findTile(L"MW_SHULKER_BOTTOM", 1);
		// check that the entries are in tiles.h.
		// Note that we work from the output image file being generated, so we
		// don't actually ever read in any of the 3 images above - they're assumed
		// to be in the output image already (from terrainBase.png).
		assert(index >= 0 && side_index >= 0 && bottom_index >= 0);
		int pick_row = outputTileSize / 2;
		int pick_col = outputTileSize / 2;
		for (i = 0; i < 16; i++) {
			// compute side and bottom color
			// First, find brightest pixel
			if (i == 0) {
				getBrightestPNGPixel(destination_ptr, gTilesTable[index].txrX * outputTileSize, gTilesTable[index].txrY * outputTileSize, outputTileSize, box_color, &pick_col, &pick_row);
				for (j = 0; j < 4; j++) {
					neutral_color[j] = box_color[j];
					mult_color[j] = 255;
				}
			}
			else {
				getPNGPixel(destination_ptr, gTilesTable[index].txrX * outputTileSize + pick_col, gTilesTable[index].txrY * outputTileSize + pick_row, box_color);
				for (j = 0; j < 4; j++) {
					if (neutral_color[j] > 0) {
						mult_color[j] = (255 * (int)box_color[j] / (int)neutral_color[j]);
					}
					else {
						// avoid division by zero
						mult_color[j] = 0;
					}
				}
			}
			// we now have the multiplier color, so multiply base tile by it
			if (shulkerSide[i] == false) {
				copyPNGArea(destination_ptr, gTilesTable[index].txrX * outputTileSize, (gTilesTable[index].txrY + 4) * outputTileSize, outputTileSize, outputTileSize,
					destination_ptr, gTilesTable[side_index].txrX * outputTileSize, gTilesTable[side_index].txrY * outputTileSize);
				multPNGTileByColor(destination_ptr, gTilesTable[index].txrX, gTilesTable[index].txrY + 4, mult_color);
			}
			if (shulkerBottom[i] == false) {
				copyPNGArea(destination_ptr, gTilesTable[index].txrX * outputTileSize, (gTilesTable[index].txrY + 5) * outputTileSize, outputTileSize, outputTileSize,
					destination_ptr, gTilesTable[bottom_index].txrX * outputTileSize, gTilesTable[bottom_index].txrY * outputTileSize);
				multPNGTileByColor(destination_ptr, gTilesTable[index].txrX, gTilesTable[index].txrY + 5, mult_color);
			}
			index++;
		}
	}

	// Test if left chest exists. If so, we assume 1.15 content is being used.
	wchar_t chestFile[MAX_PATH];
	wcscpy_s(chestFile, MAX_PATH, tilePath);
	wcscat_s(chestFile, MAX_PATH, L"chest/normal_left.png");
	progimage_info testChestImage;
	rc = readpng(&testChestImage, chestFile);
	//bool using115 = (rc == 0);
	int numChests;
	Chest* chest;
	if (rc == 0) {
		readpng_cleanup(0, &testChestImage);
		numChests = 4;
		gChest115[0].data = gNormalChest115;
		gChest115[1].data = gNormalLeftChest115;
		gChest115[2].data = gNormalRightChest115;
		gChest115[3].data = gEnderChest115;
		chest = gChest115;
	}
	else {
		numChests = 3;
		gChest114[0].data = gNormalChest;
		gChest114[1].data = gNormalDoubleChest;
		gChest114[2].data = gEnderChest;
		chest = gChest114;
	}

	// Now for the chests, if any. Look for each chest image file, and use bits as found
	bool allChests = true;
	bool anyChests = false;
	for (i = 0; i < numChests; i++) {
		// single chest, double chest, ender chest in \textures\entity\chest
		Chest* pChest = &chest[i];

		// chests are normally found in \assets\minecraft\textures\entity\chest
		wcscpy_s(chestFile, MAX_PATH, tilePath);
		wcscat_s(chestFile, MAX_PATH, L"chest\\");
		wcscat_s(chestFile, MAX_PATH, pChest->wname);
		wcscat_s(chestFile, MAX_PATH, L".png");

		// note: we really do need to declare this each time, otherwise you get odd leftovers for some reason.
		progimage_info chestImage;
		rc = readpng(&chestImage, chestFile);
		if (rc != 0)
		{
			// file not found
			if (verbose) {
				wprintf(L"WARNING: The chest image file '%s' does not exist\n", chestFile);
				gWarningCount++;
			}
			allChests = false;
			// try next chest
			break;
		}
		readpng_cleanup(0, &chestImage);
		// at least one chest was found
		anyChests = true;

		if (verbose)
			wprintf(L"The chest image file '%s' exists and will be used.\n", chestFile);

		// from size figure out scaling factor from chest to terrainExt.png

		// loop through bits to copy
		for (index = 0; index < pChest->numCopies; index++) {
			// clear tile if it's a new one (don't wipe out previous copies)
			if ((index == 0) ||
				(pChest->data[index].txrX != pChest->data[index - 1].txrX) ||
				(pChest->data[index].txrY != pChest->data[index - 1].txrY)) {
				makePNGTileEmpty(destination_ptr, pChest->data[index].txrX, pChest->data[index].txrY);
			}

			// copy from area to area, scaling as needed
			copyPNGTile(destination_ptr, pChest->data[index].txrX, pChest->data[index].txrY, 0, 
				&chestImage,
				pChest->data[index].toX, pChest->data[index].toY, 
				pChest->data[index].toX + pChest->data[index].sizeX, pChest->data[index].toY + pChest->data[index].sizeY,
				pChest->data[index].fromX, pChest->data[index].fromY,
				pChest->data[index].flags,
				(float)destination_ptr->width / (256.0f * (float)chestImage.width / (float)pChest->defaultResX));	// default is 256 / 64 * 4 or 128 * 2
		}
	}


    // if solid is desired, blend final result and replace in-place
    if ( solid || solidcutout )
    {
        for ( i = 0; i < TOTAL_TILES; i++ )
        {
            makeSolidTile(destination_ptr, i, solid );
        }
    }

	// write out the result
	rc = writepng(destination_ptr, 4, terrainExtOutput);
	if ( rc != 0 )
	{
		reportReadError(rc,terrainExtOutput);
		return 1;
	}
	writepng_cleanup(destination_ptr);
	if ( verbose )
		wprintf (L"New texture '%s' created.\n", terrainExtOutput);

	// warn user that nothing was done
	// 3 is the number of MW_*.png files that come with TileMaker
	if (tilesFound <= 3 && !anyChests) {
		wprintf(L"WARNING: It's likely no real work was done. To use TileMaker, you need to put\n  all the images from your resource pack's 'assets\\minecraft\\textures'\n  block and entity\\chest directories into TileMaker's 'blocks' and\n  'blocks\\chest' directories. See http://mineways.com for more about TileMaker.\n" );
		gWarningCount++;
	}
	else if (!allChests) {
		wprintf(L"WARNING: not all relevant chest images were found in the 'blocks\\chest' directory.\n  Tilemaker worked, but you can add chest images if you like. You can provide\n  the images normal.png, normal_left.png, normal_right.png\n  (or normal_double.png for 1.14 and earlier), and ender.png.\n  Copy these texture resources from Minecraft's jar-file\n  'assets\\minecraft\\textures\\entity\\chest' directory to\n  Mineways' subdirectory blocks\\chest.\n");
		gWarningCount++;
	}

	if (gErrorCount || gWarningCount)
		wprintf(L"Summary: %d error%S and %d warning%S were generated.\n", gErrorCount, (gErrorCount == 1) ? "" : "s", gWarningCount, (gWarningCount == 1) ? "" : "s");

	return 0;
}

int findTile( wchar_t *tileName, int alternate )
{
	int i;

	for ( i = 0; i < TOTAL_TILES; i++ )
	{
		if ( wcscmp(tileName, gTilesTable[i].filename) == 0 )
			return i;
        if ( alternate && wcscmp(tileName, gTilesTable[i].altFilename) == 0 )
            return i;
	}
	return -1;
}

int findNextTile( wchar_t *tileName, int index, int alternate )
{
	int i;

	for ( i = index+1; i < TOTAL_TILES; i++ )
	{
        if ( wcscmp(tileName, gTilesTable[i].filename) == 0 )
            return i;
        if ( alternate && wcscmp(tileName, gTilesTable[i].altFilename) == 0 )
            return i;
	}
	return -1;
}

int findUnneededTile( wchar_t *tileName )
{
    int i = 0;
    size_t inlen = wcslen( tileName );
    TCHAR tileRoot[1000];
    wcscpy_s( tileRoot, 999, tileName );
    // trim off .png suffix
    tileRoot[inlen-4] = (TCHAR)0;
    while ( wcslen(gUnneeded[i]) > 0 )
    {
        if ( wcscmp(tileRoot, gUnneeded[i]) == 0 )
            return i;
        i++;
    }
    return -1;
}

int trueWidth( int tileLoc )
{
	int width = tile[tileLoc].width;
	if ((wcscmp(gTilesTable[tilesInputToTableIndex[tileLoc]].filename, L"water_flow") == 0) ||
		(wcscmp(gTilesTable[tilesInputToTableIndex[tileLoc]].filename, L"lava_flow") == 0)){
		width /= 2;
	}
	return width;
}

//====================== statics ==========================

static int buildPathAndReadTile(wchar_t* tilePath, wchar_t* fileName, progimage_info *pTile)
{
	int fail_code = 0;
	// read image file - build path
	wchar_t readFileName[MAX_PATH];
	wcscpy_s(readFileName, MAX_PATH, tilePath);
	wcscat_s(readFileName, MAX_PATH, fileName);
	// read in tile for later
	int rc = readpng(pTile, readFileName);
	if (rc != 0)
	{
		reportReadError(rc, readFileName);
		fail_code = 1;
	}
	readpng_cleanup(0, pTile);
	return fail_code;
}

static void reportReadError( int rc, wchar_t *filename )
{
	gErrorCount++;
	switch (rc) {
	case 1:
		wprintf(L"***** ERROR [%s] is not a PNG file: incorrect signature.\n", filename);
		break;
	case 2:
		wprintf(L"***** ERROR [%s] has bad IHDR (libpng longjmp).\n", filename);
		break;
    case 4:
        wprintf(L"***** ERROR [%s] read failed - insufficient memory.\n", filename);
        break;
    case 63:
        wprintf(L"***** ERROR [%s] read failed - chunk too long.\n", filename);
        break;
    default:
		wprintf(L"***** ERROR [%s] read failed - unknown readpng_init() error.", filename);
		break;
	}
    wprintf(L"Often this means the PNG file has some small bit of information that TileMaker cannot\n  handle. You might be able to fix this error by opening this PNG file in\n  Irfanview or other viewer and then saving it again. This has been known to clear\n  out any irregularity that TileMaker's somewhat-fragile PNG reader dies on.\n");
}


//================================ Image Manipulation ====================================

// if color is black, set alpha to 0
static void setBlackAlphaPNGTile(int chosenTile, progimage_info *src)
{
	unsigned long row,col,src_start;
	unsigned char *src_data;
	unsigned long tileSize;

	//tile matches destination tile size - copy
	tileSize = src->width;

	// which tile to use: get the bottommost
	src_start = computeVerticalTileOffset( src, chosenTile );
	src_data = &src->image_data[0] + ( src_start * src->width ) * 4;

	for ( row = 0; row < tileSize; row++ )
	{
		for ( col = 0; col < tileSize; col++ )
		{
			// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
			// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
			if ( src_data[0] == 0 && src_data[1] == 0 && src_data[2] == 0 )
			{
				src_data[3] = 0;
			}
			src_data += 4;
		}
	}
}

static int copyPNGTile(progimage_info *dst, unsigned long dst_x, unsigned long dst_y, unsigned long chosenTile, progimage_info *src,
	unsigned long dst_x_lo, unsigned long dst_y_lo, unsigned long dst_x_hi, unsigned long dst_y_hi, unsigned long src_x_lo, unsigned long src_y_lo, unsigned long flags, float zoom )
{
	unsigned long row,col,src_start;
    unsigned char* dst_data;
	unsigned char color[4];
	unsigned long tileSize,zoomTileSize;
	unsigned long zoomrow,zoomcol,izoom;
	unsigned int sumR,sumG,sumB,sumA;
	unsigned long zoom2;

	if ( zoom == 1.0f ) // dst->width == src->width * 16 )
	{
		//tile matches destination tile size - copy
		tileSize = dst->width / 16;

		// 16x16 is assumed, so scale up all our lo and hi values if not the case
		if (tileSize != 16) {
			int rescale = tileSize / 16;
			dst_x_lo *= rescale;
			dst_y_lo *= rescale;
			dst_x_hi *= rescale;
			dst_y_hi *= rescale;
			src_x_lo *= rescale;
			src_y_lo *= rescale;
		}

		assert( dst_y*tileSize < (unsigned long)dst->height );	// destination can't hold tile

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for ( row = dst_y_lo; row < dst_y_hi; row++ )
		{
			dst_data = &dst->image_data[0] + ((dst_y*tileSize + row) * dst->width + dst_x*tileSize + dst_x_lo) * 4;
			for ( col = dst_x_lo; col < dst_x_hi; col++ )
			{
				// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
				// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
				// negate column and row, as Minecraft stores these reversed (left and right chest, basically)
				getPNGPixel(src, 
					src_x_lo + ((flags & 0x1) ? (dst_x_hi - col - 1) : (col - dst_x_lo)),
					src_start + src_y_lo + ((flags & 0x2) ? (dst_y_hi - row - 1) : (row - dst_y_lo)), color);
				if ( color[3] == 0 )
				{
					memset(dst_data,0,4);
				}
				else
				{
					memcpy(dst_data, color, 4);
				}
				dst_data += 4;
			}
		}
	}
	else if ( zoom > 1.0f ) // dst->width > src->width * 16 )
	{
		// magnify
		tileSize = (int)((float)dst->width / zoom)/16;

		if (tileSize <= 0) {
			wprintf(L"***** ERROR: somehow, the largest tile size is computed to be %d - this needs to be a positive number.\n", tileSize);
			gErrorCount++;
			return 1;
		}

		// 16x16 is assumed, so scale up all our lo and hi values if not the case
		if (tileSize != 16) {
			float rescale = (float)tileSize / 16;
			dst_x_lo = (int)((float)dst_x_lo*rescale);
			dst_y_lo = (int)((float)dst_y_lo*rescale);
			dst_x_hi = (int)((float)dst_x_hi*rescale);
			dst_y_hi = (int)((float)dst_y_hi*rescale);
			src_x_lo = (int)((float)src_x_lo*rescale);
			src_y_lo = (int)((float)src_y_lo*rescale);
		}

		// could check that zoom factor is an integer (really should be a power of two)
		izoom = (int)zoom;
		zoomTileSize = izoom * tileSize;

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for (row = dst_y_lo; row < dst_y_hi; row++)
		{
			for (col = dst_x_lo; col < dst_x_hi; col++)
			{
				// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
				// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
				getPNGPixel(src,
					src_x_lo + ((flags & 0x1) ? (dst_x_hi - col - 1) : (col - dst_x_lo)),
					src_start + src_y_lo + ((flags & 0x2) ? (dst_y_hi - row - 1) : (row - dst_y_lo)), color);
				if ( color[3] == 0 )
				{
					color[0] = color[1] = color[2] = 0;
				}
				for ( zoomrow = 0; zoomrow < izoom; zoomrow++ )
				{
					dst_data = &dst->image_data[0] + ((dst_y*zoomTileSize + row * izoom + zoomrow ) * (unsigned long)dst->width + dst_x*zoomTileSize + col * izoom) * 4;
					for ( zoomcol = 0; zoomcol < izoom; zoomcol++ )
					{
						memcpy(dst_data,color,4);
						dst_data += 4;
					}
				}
			}
		}
	}
	else // zoom < 1.0f
	{
		// minify
		tileSize = dst->width/16;

		// 16x16 is assumed, so scale up all our lo and hi values if not the case
		if (tileSize != 16) {
			int rescale = tileSize / 16;
			dst_x_lo *= rescale;
			dst_y_lo *= rescale;
			dst_x_hi *= rescale;
			dst_y_hi *= rescale;
			src_x_lo *= rescale;
			src_y_lo *= rescale;
		}

		// check that zoom factor is an integer (really should be a power of two)
		izoom = (int)(1.0f / zoom);	// src->width * 16 / dst->width;
		zoom2 = izoom * izoom;

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for (row = dst_y_lo; row < dst_y_hi; row++)
		{
			for (col = dst_x_lo; col < dst_x_hi; col++)
			{
				sumR = sumG = sumB = sumA = 0;
				for ( zoomrow = 0; zoomrow < izoom; zoomrow++ )
				{
					for ( zoomcol = 0; zoomcol < izoom; zoomcol++ )
					{
						// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
						// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
						getPNGPixel(src, (col + src_x_lo - dst_x_lo)*izoom + zoomcol, (row + src_y_lo - dst_y_lo)*izoom + zoomrow, color);
						if ( color[3] == 0 )
						{
							color[0] = color[1] = color[2] = 0;
						}
						sumR += (unsigned int)color[0];
						sumG += (unsigned int)color[1];
						sumB += (unsigned int)color[2];
						sumA += (unsigned int)color[3];
					}
				}
				dst_data = &dst->image_data[0] + ((dst_y * tileSize + row) * dst->width + dst_x * tileSize + col) * 4;
				for ( zoomcol = 0; zoomcol < izoom; zoomcol++ )
				{
					dst_data[0] = (unsigned char)(sumR/zoom2);
					dst_data[1] = (unsigned char)(sumG/zoom2);
					dst_data[2] = (unsigned char)(sumB/zoom2);
					dst_data[3] = (unsigned char)(sumA/zoom2);
				}
			}
		}
	}
	return 0;
}

static void multPNGTileByColor(progimage_info *dst, int dst_x, int dst_y, int *color)
{
	unsigned long row, col, i;
    unsigned char* dst_data;

	unsigned long tileSize = dst->width / 16;

    for (row = 0; row < tileSize; row++)
    {
        dst_data = &dst->image_data[0] + ((dst_y*tileSize + row) * dst->width + dst_x*tileSize) * 4;
        for (col = 0; col < tileSize; col++)
        {
            for (i = 0; i < 3; i++) {
                int val = color[i] * (int)*dst_data / 255;
                *dst_data++ = (val > 255) ? 255 : (unsigned char)val;
            }
            // ignore alpha - assumed solid, and that we wouldn't want to multiply anyway
            dst_data++;
        }
    }
}

static int computeVerticalTileOffset(progimage_info *src, int chosenTile)
{
	int offset = 0;
	if ( chosenTile >= 0 )
		offset = src->width * chosenTile;
	else
		offset = src->height + chosenTile*src->width;

	if ( offset < 0 )
	{
		offset = 0;
	}
	else if ( offset >= src->height )
	{
		offset = src->height - src->width;
	}

	return offset;
}

static void getPNGPixel(progimage_info *src, int col, int row, unsigned char *color)
{
	unsigned char *src_data;

	//if ( ( src->color_type == PNG_COLOR_TYPE_RGB_ALPHA ) || ( src->color_type == PNG_COLOR_TYPE_PALETTE ) || ( src->color_type == PNG_COLOR_TYPE_GRAY_ALPHA ) )
	//if ( src->channels == 4 )
	//{

	// LodePNG does all the work for us, going to RGBA by default:
	src_data = &src->image_data[0] + ( row * src->width + col ) * 4;
	memcpy(color,src_data,4);

	//}
	//else if ( ( src->color_type == PNG_COLOR_TYPE_RGB ) || (src->color_type == PNG_COLOR_TYPE_GRAY) )
	//else if ( src->channels == 3 )
	//{
	//	src_data = &src->image_data[0] + ( row * src->width + col ) * 3;
	//	memcpy(color,src_data,3);
	//	color[3] = 255;	// alpha always 1.0
	//}
	//else if ( src->channels == 2 )
	//{
	//	// just a guess
	//	src_data = &src->image_data[0] + ( row * src->width + col ) * 2;
	//	color[0] = color[1] = color[2] = *src_data++;
	//	color[3] = *src_data;
	//}
	//else if ( src->channels == 1 )
	//{
	//	// just a guess
	//	src_data = &src->image_data[0] + ( row * src->width + col );
	//	color[0] = color[1] = color[2] = *src_data;
	//	color[3] = 255;	// alpha always 1.0
	//}
	////else if ( src->color_type == PNG_COLOR_TYPE_GRAY )
	////{
	////	// I'm guessing there's just one channel...
	////	src_data = &src->image_data[0] + row * src->width + col;
	////	color[0] = color[1] = color[2] = *src_data;
	////	color[3] = 255;	// alpha always 1.0
	////}
	//else
	//{
	//	// unknown type
	//	assert(0);
	//}
}

static void getBrightestPNGPixel(progimage_info *src, unsigned long col, unsigned long row, unsigned long res, unsigned char *color, int *locc, int *locr)
{
	unsigned long r, c;
    unsigned char testColor[4];
	int maxSum, testSum;
    maxSum = -1;
    color[3] = 255;
    for (r = 0; r < res; r++) {
        for (c = 0; c < res; c++) {
            getPNGPixel(src, col + c, row + r, testColor);
            testSum = (int)testColor[0] + (int)testColor[1] + (int)testColor[2];
            if (testSum > maxSum) {
                maxSum = testSum;
                *locr = r;
                *locc = c;
                color[0] = testColor[0];
                color[1] = testColor[1];
                color[2] = testColor[2];
            }
        }
    }
}

static int isPNGTileEmpty(progimage_info *dst, int dst_x, int dst_y)
{
	// look at all data: are all alphas 0?
	unsigned long tileSize = dst->width / 16;
	unsigned char *dst_data;
	unsigned long row, col;

	for (row = 0; row < tileSize; row++)
	{
		dst_data = &dst->image_data[0] + ((dst_y * tileSize + row) * dst->width + dst_x * tileSize) * 4;
		for (col = 0; col < tileSize; col++)
		{
			if (dst_data[3] != 0)
			{
				return 0;
			}
			dst_data += 4;
		}
	}
	return 1;
};

static void makePNGTileEmpty(progimage_info *dst, int dst_x, int dst_y)
{
	// look at all data: are all alphas 0?
	unsigned long tileSize = dst->width / 16;
	unsigned int *dst_data;
	unsigned long row, col;

	for (row = 0; row < tileSize; row++)
	{
		dst_data = ((unsigned int *)&dst->image_data[0]) + ((dst_y * tileSize + row) * dst->width + dst_x * tileSize);
		for (col = 0; col < tileSize; col++)
		{
			*dst_data++ = 0x0;
		}
	}
};

// assumes we want to match the source to fit the destination
static void copyPNG(progimage_info *dst, progimage_info *src)
{
	unsigned long row,col,zoomrow,zoomcol;
	unsigned char *dst_data;
	unsigned char *src_data, *src_loc;
	unsigned long zoom,zoom2;
	unsigned int sumR,sumG,sumB,sumA;
	unsigned long numrow,numcol;

	if ( dst->width == src->width )
	{
		memcpy(&dst->image_data[0], &src->image_data[0], src->width*src->height*4);
	}
	else if ( dst->width > src->width )
	{
		// magnify

		// check that zoom factor is an integer (really should be a power of two)
		assert( (dst->width / src->width) == (float)((int)(dst->width / src->width)));
		zoom = dst->width / src->width;
		assert( (unsigned long)dst->height >= src->height*zoom );

		src_data = &src->image_data[0];
		numrow = (unsigned long)src->height;
		numcol = (unsigned long)src->width;
		for ( row = 0; row < numrow; row++ )
		{
			dst_data = &dst->image_data[0] + row * (unsigned long)dst->width * zoom * 4;
			for ( col = 0; col < numcol; col++ )
			{
				for ( zoomrow = 0; zoomrow < zoom; zoomrow++ )
				{
					for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
					{
						memcpy(dst_data + (zoomrow * (unsigned long)dst->width + zoomcol) * 4, src_data, 4);
					}
				}
				dst_data += zoom*4;	// move to next column
				src_data += 4;
			}
		}
	}
	else
	{
		// minify: squish source into destination

		// check that zoom factor is an integer (really should be a power of two)
		assert( (src->width / dst->width) == (float)((int)(src->width / dst->width)));
		zoom = src->width / dst->width;
		assert( (unsigned long)dst->height*zoom >= (unsigned long)src->height );
		zoom2 = zoom * zoom;

		dst_data = &dst->image_data[0];
		numrow = (unsigned long)dst->height;
		numcol = (unsigned long)dst->width;
		for ( row = 0; row < numrow; row++ )
		{
			src_data = &src->image_data[0] + row * src->width * zoom * 4;
			for ( col = 0; col < numcol; col++ )
			{
				sumR = sumG = sumB = sumA = 0;
				for ( zoomrow = 0; zoomrow < zoom; zoomrow++ )
				{
					for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
					{
						src_loc = src_data + (zoomrow * src->width + zoomcol) * 4;
						sumR += (unsigned int)*src_loc++;
						sumG += (unsigned int)*src_loc++;
						sumB += (unsigned int)*src_loc++;
						sumA += (unsigned int)*src_loc++;
					}
				}
				*dst_data++ = (unsigned char)(sumR/zoom2);
				*dst_data++ = (unsigned char)(sumG/zoom2);
				*dst_data++ = (unsigned char)(sumB/zoom2);
				*dst_data++ = (unsigned char)(sumA/zoom2);
				// move to next column
				src_data += zoom*4;
			}
		}
	}
}

static void copyPNGArea(progimage_info *dst, unsigned long dst_x_min, unsigned long dst_y_min, unsigned long size_x, unsigned long size_y, progimage_info *src, int src_x_min, int src_y_min)
{
	unsigned long row;
	unsigned long dst_offset, src_offset;

    for (row = 0; row < size_y; row++)
    {
        dst_offset = ((dst_y_min + row)*dst->width + dst_x_min) * 4;
        src_offset = ((src_y_min + row)*src->width + src_x_min) * 4;
        memcpy(&dst->image_data[dst_offset], &src->image_data[src_offset], size_x * 4);
    }
}


static void makeSolidTile(progimage_info *dst, int chosenTile, int solid)
{
	unsigned long row,col,dst_offset;
    unsigned char* dst_data;

    unsigned char color[4];
    double dcolor[4];
    double sum_color[3],sum;
	unsigned long tileSize;

    tileSize = dst->width / 16;

    dst_offset = ((chosenTile % 16)*tileSize + (int)(chosenTile/16)*tileSize*dst->width)*4;

    sum_color[0] = sum_color[1] = sum_color[2] = sum = 0;

    for ( row = 0; row < tileSize; row++ )
    {
        dst_data = &dst->image_data[0] + dst_offset + row*dst->width*4;
        for ( col = 0; col < tileSize; col++ )
        {
            // linearize; really we should use sRGB conversions, but this is close enough
            dcolor[0] = pow(*dst_data++/255.0,2.2);
            dcolor[1] = pow(*dst_data++/255.0,2.2);
            dcolor[2] = pow(*dst_data++/255.0,2.2);
            dcolor[3] = *dst_data++/255.0;
            sum_color[0] += dcolor[0]*dcolor[3];
            sum_color[1] += dcolor[1]*dcolor[3];
            sum_color[2] += dcolor[2]*dcolor[3];
            sum += dcolor[3];
        }
    }
    if ( sum > 0 ) {
        // gamma correct and then unassociate for PNG storage
        color[0] = (unsigned char)(0.5 + 255.0 * pow((sum_color[0] / sum),1/2.2));
        color[1] = (unsigned char)(0.5 + 255.0 * pow((sum_color[1] / sum),1/2.2));
        color[2] = (unsigned char)(0.5 + 255.0 * pow((sum_color[2] / sum),1/2.2));
        color[3] = 255;
        for ( row = 0; row < tileSize; row++ )
        {
            dst_data = &dst->image_data[0] + dst_offset + row*dst->width*4;
            for ( col = 0; col < tileSize; col++ )
            {
                // if we want solid blocks (not cutouts), or we do want solid cutouts
                // and the alpha is not fully transparent, then save new color.
                if ( solid || (dst_data[3] == 255) ) {
                    // solid, or cutout is fully opaque
                    *dst_data++ = color[0];
                    *dst_data++ = color[1];
                    *dst_data++ = color[2];
                    *dst_data++ = color[3];
                } else if ( !solid || (dst_data[3] != 255) ) {
                    // cutout mode, and partial alpha
                    *dst_data++ = color[0];
                    *dst_data++ = color[1];
                    *dst_data++ = color[2];
                    // don't touch alpha, leave it unassociated
                    dst_data++;
                } else {
                    // skip pixel, as it's fully transparent
                    dst_data += 4;
                }
            }
        }
    }
}

// does any pixel have an alpha of 0?
static int checkForCutout(progimage_info *dst)
{
	unsigned char *dst_data = &dst->image_data[0];
	int row, col;

	for (row = 0; row < dst->height; row++)
	{
		for (col = 0; col < dst->width; col++)
		{
			if (dst_data[3] == 0)
			{
				return 1;
			}
			dst_data += 4;
		}
	}
	return 0;
};


