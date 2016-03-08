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

static void reportReadError( int rc, wchar_t *filename );

static void setBlackAlphaPNGTile(int chosenTile, progimage_info *src);
static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, int chosenTile, progimage_info *src);
static void getPNGPixel(progimage_info *src, int col, int row, unsigned char *color);
static int computeVerticalTileOffset(progimage_info *src, int chosenTile);
static int isPNGTileEmpty( progimage_info *dst, int dst_x, int dst_y );
static void makeSolidTile(progimage_info *dst, int chosenTile, int solid);

static void copyPNGArea(progimage_info *dst, progimage_info *src);

typedef struct int2 {
	int x;
	int y;
} int2;

int wmain(int argc, wchar_t* argv[])
{
	int rc;
	progimage_info basicterrain;
	progimage_info destination;
	progimage_info *destination_ptr = &destination;

	int i;

	int tilesFound = 0;
	int tilesFoundArray[TOTAL_TILES];
	int tilesMissingSet[TOTAL_TILES];
	progimage_info tile[TOTAL_TILES];
	int baseTileSize, xTiles, baseYTiles, baseXResolution, baseYResolution;
	int outputTileSize, outputYTiles, outputXResolution, outputYResolution;

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
    int alternate = 0;
    int solid = 0;
    int solidcutout = 0;

	wcscpy_s(terrainBase, MAX_PATH, BASE_INPUT_FILENAME );
	wcscpy_s(tilePath, MAX_PATH, TILE_PATH );
	wcscpy_s(terrainExtOutput, MAX_PATH, OUTPUT_FILENAME );

	memset( tilesMissingSet, 0, 4*TOTAL_TILES );

	// usage: [-i terrainBase.png] [-d tiles] [-o terrainExt.png] [-t tileSize]
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
			// no tiles
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
        else if ( wcscmp(argv[argLoc],L"-a") == 0 )
        {
            // alternate: use names such as "blockIron" when "iron_block" is not found
            alternate = 1;
        }
        else if ( wcscmp(argv[argLoc],L"-s") == 0 )
        {
            // solid: take the average color of the incoming tile and output this solid color
            solid = 1;
        }
        else if ( wcscmp(argv[argLoc],L"-S") == 0 )
        {
            // solid cutout: as above, but preserve the cutout transparent areas
            solidcutout = 1;
        }
        else if ( wcscmp(argv[argLoc],L"-v") == 0 )
        {
            // verbose: tell when normal things happen
            verbose = 1;
        }
		else
		{
			// go to here-----------------------------------------------------------------------------|
			wprintf( L"TileMaker version 2.02\n");
			wprintf( L"usage: TileMaker [-i terrainBase.png] [-d blocks] [-o terrainExt.png]\n        [-t tileSize] [-c chosenTile] [-nb] [-nt] [-r] [-m] [-v]\n");
			wprintf( L"  -i terrainBase.png - image containing the base set of terrain blocks\n    (includes special chest tiles). Default is 'terrainBase.png'.\n");
			wprintf( L"  -d blocks - directory of block textures to overlay on top of the base.\n    Default directory is 'blocks'.\n");
			wprintf( L"  -o terrainExt.png - the resulting terrain image, used by Mineways. Default is\n    terrainExt.png.\n");
			wprintf( L"  -t tileSize - force a power of 2 tile size for the resulting terrainExt.png\n    file, e.g. 32, 128. Useful for zooming or making a 'draft quality'\n    terrainExt.png. If not set, largest tile found is used.\n");
			wprintf( L"  -c chosenTile - for tiles with multiple versions (e.g. water, lava, portal),\n    choose which tile to use. 0 means topmost, 1 second from top, 2 etc.;\n    -1 bottommost, -2 next to bottom.\n");
			wprintf( L"  -nb - no base; the base texture terrainBase.png is not read. This option is\n    good for seeing what images are in the blocks directory, as these are\n    what get put into terrainExt.png.\n");
			wprintf( L"  -nt - no tile directory; don't read in any images in the 'blocks' directory,\n    only the base image is read (and probably zoomed, otherwise this\n    option is pointless).\n");
			wprintf( L"  -r - replace (from the 'blocks' directory) only those tiles not in the base\n    texture. This is a way of extending a base texture to new versions.\n");
            wprintf( L"  -m - to report all missing tiles, ones that Mineways uses but were not in the\n    tiles directory.\n");
            wprintf( L"  -a - include alternate texture names when files are not found.\n");
            wprintf( L"  -s - take the average color of the incoming tile and output this solid color.\n");
            wprintf( L"  -S - as above, but preserve the cutout transparent areas.\n");
			wprintf( L"  -v - verbose, explain everything going on. Default: display only warnings.\n");
			return 1;
		}
		argLoc++;
	}

	// add / to tile directory path
	if ( wcscmp( &tilePath[wcslen(tilePath)-1], L"/") != 0 )
	{
		wcscat_s(tilePath, MAX_PATH, L"/" );
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
			wprintf (L"The base terrain is %s\n", terrainBase);

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

	// output should be at least as big as input base texture
	outputYTiles = baseYTiles;

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
			printf ("ERROR: FindFirstFile failed (error # %d).\n", GetLastError());
			wprintf (L"No files found - please put your new blocks in the directory %s.\n", tilePath);
			return 1;
		} 
		else 
		{
			do {
				wchar_t tileName[MAX_PATH];
				int len;

				if ( verbose )
					wprintf (L"The file found is %s\n", ffd.cFileName);

				wcscpy_s( tileName, MAX_PATH, ffd.cFileName );
				// check for .png suffix - note test is case insensitive
				len = (int)wcslen(tileName);
				if ( _wcsicmp( &tileName[len-4], L".png" ) == 0 )
				{
					int index;
					// remove .png suffix
					tileName[len-4] = 0x0;
					index = findTile(tileName, alternate);
					if ( index < 0 )
					{
                        // see if tile is on unneeded list
                        if ( findUnneededTile( ffd.cFileName ) < 0 )
						    wprintf (L"WARNING: %s is a tile name that TileMaker does not understand. Perhaps you need to rename it?\nSee https://github.com/erich666/Mineways/blob/master/tilemaker/TileMaker/tiles.h for the image file names used.\n", ffd.cFileName);
					}

					while ( index >= 0 )
					{
						// tile is one we care about.
						tilesFoundArray[tilesFound] = index;

						{
							// read image file - build path
							wchar_t readFileName[MAX_PATH];
							wcscpy_s( readFileName, MAX_PATH, tilePath );
							wcscat_s( readFileName, MAX_PATH, ffd.cFileName );
							// read in tile for later
							rc = readpng(&tile[tilesFound], readFileName);
							if ( rc != 0 )
							{
								reportReadError(rc,readFileName);
								return 1;
							}
							readpng_cleanup(0,&tile[tilesFound]);
						}

						// check for unsupported formats
						//if ( 
						//	//( tile[tilesFound].bit_depth == 8 || tile[tilesFound].bit_depth == 4 ) &&
						//	 ( tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB_ALPHA || 
						//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB || 
						//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_GRAY || 
						//	   tile[tilesFound].color_type == PNG_COLOR_TYPE_PALETTE ))
						{
							tilesMissingSet[index] = 1;	// note tile is used
							tilesFound++;

							// find maximum Y resolution of output tile
							if ( outputYTiles-1 < gTiles[index].txrY )
							{
								outputYTiles = gTiles[index].txrY + 1;
							}
						}
						//else
						//{
						//	// unknown format
						//	_tprintf (TEXT("WARNING: file %s not used because unsupported bit depth %d and color type %d\n"), ffd.cFileName, tile[tilesFound].bit_depth, tile[tilesFound].color_type );
						//}
						index = findNextTile(tileName, index, alternate);
					}
				}
			} while (FindNextFile(hFind,&ffd)!=0);

			FindClose(hFind);
		}
	}

	// look for tiles not input?
	if ( checkmissing )
	{
		for ( i = 0; i < TOTAL_TILES; i++ )
		{
			if ( tilesMissingSet[i] == 0 )
			{
				// if it starts with "MW_" or is the empty string, ignore miss
				if ( wcslen(gTiles[i].filename) > 0 && wcsncmp(gTiles[i].filename,L"MW_",3) != 0 )
					wprintf (L"This program needs a tile named %s that was not replaced.\n", gTiles[i].filename);
			}
		}
	}

	// find largest tile. Hmmm, beware of flowing lava & water, which is twice as wide.
	for ( i = 0; i < tilesFound; i++ )
	{
		if ( overlayTileSize < tile[i].width )
		{
			overlayTileSize = tile[i].width;
		}
	}

	if ( verbose )
		wprintf (L"Largest tile found is %d pixels wide.\n", overlayTileSize);

	// take the larger of the overlay and base tile sizes as the target size
	outputTileSize = ( overlayTileSize > baseTileSize ) ? overlayTileSize : baseTileSize;

	// however, if there's a forced tile size, use that:
	if ( forcedTileSize > 0 )
	{
		outputTileSize = forcedTileSize;

		if ( verbose )
			wprintf (L"Output texture is forced to have tiles %d pixels wide.\n", forcedTileSize);
	}

	// allocate output image and fill it up
	destination_ptr = new progimage_info();

	outputXResolution = xTiles * outputTileSize;
	outputYResolution = outputYTiles * outputTileSize;

	destination_ptr->width = outputXResolution;
	destination_ptr->height = outputYResolution;

	if ( nobase )
	{
		// for debug, to see just the tiles placed
		destination_ptr->image_data.resize(outputXResolution*outputYResolution*4*sizeof(unsigned char),0x0);
	}
	else
	{
		// copy base texture over
		destination_ptr->image_data.resize(outputXResolution*outputYResolution*4*sizeof(unsigned char),0x0);
		copyPNGArea(destination_ptr, &basicterrain);
		if ( verbose )
			wprintf (L"Base texture %s copied to output.\n", terrainBase);
	}

	// copy tiles found over
	for ( i = 0; i < tilesFound; i++ )
	{
		int index = tilesFoundArray[i];
		// -r option on?
		if ( onlyreplace )
		{
			if ( !isPNGTileEmpty(destination_ptr, gTiles[index].txrX, gTiles[index].txrY) )
			{
				wprintf (L"UNUSED: %s was not used because there is already a tile.\n", gTiles[index].filename);
				continue;
			}
		}
		if ( gTiles[index].flags & SBIT_BLACK_ALPHA )
		{
			setBlackAlphaPNGTile( chosenTile, &tile[i] );
		}
		copyPNGTile(destination_ptr, gTiles[index].txrX, gTiles[index].txrY, chosenTile, &tile[i]);
		if ( verbose )
			wprintf (L"File %s merged.\n", gTiles[index].filename);
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
		wprintf (L"New texture %s created.\n", terrainExtOutput);

	return 0;
}

int findTile( wchar_t *tileName, int alternate )
{
	int i;

	for ( i = 0; i < TOTAL_TILES; i++ )
	{
		if ( wcscmp(tileName, gTiles[i].filename) == 0 )
			return i;
        if ( alternate && wcscmp(tileName, gTiles[i].altFilename) == 0 )
            return i;
	}
	return -1;
}

int findNextTile( wchar_t *tileName, int index, int alternate )
{
	int i;

	for ( i = index+1; i < TOTAL_TILES; i++ )
	{
        if ( wcscmp(tileName, gTiles[i].filename) == 0 )
            return i;
        if ( alternate && wcscmp(tileName, gTiles[i].altFilename) == 0 )
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


//====================== statics ==========================

static void reportReadError( int rc, wchar_t *filename )
{
	switch (rc) {
	case 1:
		wprintf(L"[%S] is not a PNG file: incorrect signature\n", filename);
		break;
	case 2:
		wprintf(L"[%S] has bad IHDR (libpng longjmp)\n", filename);
		break;
	case 4:
		wprintf(L"[%S] read failed - insufficient memory\n", filename);
		break;
	default:
		wprintf(L"[%S] read failed - unknown readpng_init() error\n", filename);
		break;
	}
}


//================================ Image Manipulation ====================================

// if color is black, set alpha to 0
static void setBlackAlphaPNGTile(int chosenTile, progimage_info *src)
{
	int row,col,src_start;
	unsigned char *src_data;
	int tileSize;

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

static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, int chosenTile, progimage_info *src)
{
    int row,col,src_start;
    unsigned char* dst_data;
	unsigned char color[4];
	int tileSize,zoom,zoomTileSize;
	int zoomrow,zoomcol;
	unsigned int sumR,sumG,sumB,sumA;
	int zoom2;

	if ( dst->width == src->width * 16 )
	{
		//tile matches destination tile size - copy
		tileSize = src->width;

		assert( dst_y*tileSize < dst->height );	// destination can't hold tile

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for ( row = 0; row < tileSize; row++ )
		{
			dst_data = &dst->image_data[0] + ((dst_y*tileSize + row) * dst->width + dst_x*tileSize) * 4;
			for ( col = 0; col < tileSize; col++ )
			{
				// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
				// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
				getPNGPixel( src, col, row+src_start, color );
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
	else if ( dst->width > src->width * 16 )
	{
		// magnify
		tileSize = src->width;

		// check that zoom factor is an integer (really should be a power of two)
		zoom = dst->width / (src->width*16);
		zoomTileSize = zoom * tileSize;

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for ( row = 0; row < tileSize; row++ )
		{
			for ( col = 0; col < tileSize; col++ )
			{
				// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
				// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
				getPNGPixel( src, col, row+src_start, color );
				if ( color[3] == 0 )
				{
					color[0] = color[1] = color[2] = 0;
				}
				for ( zoomrow = 0; zoomrow < zoom; zoomrow++ )
				{
					dst_data = &dst->image_data[0] + ((dst_y*zoomTileSize + row * zoom + zoomrow ) * dst->width + dst_x*zoomTileSize + col * zoom) * 4;
					for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
					{
						memcpy(dst_data,color,4);
						dst_data += 4;
					}
				}
			}
		}
	}
	else
	{
		// minify
		tileSize = dst->width/16;

		// check that zoom factor is an integer (really should be a power of two)
		zoom = src->width * 16 / dst->width;
		zoom2 = zoom * zoom;

		// which tile to use: get the bottommost
		src_start = computeVerticalTileOffset( src, chosenTile );

		for ( row = 0; row < tileSize; row++ )
		{
			for ( col = 0; col < tileSize; col++ )
			{
				sumR = sumG = sumB = sumA = 0;
				for ( zoomrow = 0; zoomrow < zoom; zoomrow++ )
				{
					for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
					{
						// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
						// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
						getPNGPixel( src, col*zoom + zoomcol, row*zoom + zoomrow, color );
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
				for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
				{
					dst_data[0] = (unsigned char)(sumR/zoom2);
					dst_data[1] = (unsigned char)(sumG/zoom2);
					dst_data[2] = (unsigned char)(sumB/zoom2);
					dst_data[3] = (unsigned char)(sumA/zoom2);
				}
			}
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

static int isPNGTileEmpty( progimage_info *dst, int dst_x, int dst_y )
{
	// look at all data: are all alphas 0?
	int tileSize = dst->width/16;
	unsigned char *dst_data;
	int row,col;

	for ( row = 0; row < tileSize; row++ )
	{
		dst_data = &dst->image_data[0] + ((dst_y * tileSize + row) * dst->width + dst_x * tileSize ) * 4;
		for ( col = 0; col < tileSize; col++ )
		{
			if ( (dst_data+col)[3] != 0 )
			{
				return 0;
			}
			dst_data++;
		}
	}
	return 1;
};

// assumes we want to match the source to fit the destination
static void copyPNGArea(progimage_info *dst, progimage_info *src)
{
	int row,col,zoomrow,zoomcol;
	unsigned char *dst_data;
	unsigned char *src_data, *src_loc;
	int zoom,zoom2;
	unsigned int sumR,sumG,sumB,sumA;

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
		assert( dst->height >= src->height*zoom );

		src_data = &src->image_data[0];
		for ( row = 0; row < src->height; row++ )
		{
			dst_data = &dst->image_data[0] + row * dst->width * zoom * 4;
			for ( col = 0; col < src->width; col++ )
			{
				for ( zoomrow = 0; zoomrow < zoom; zoomrow++ )
				{
					for ( zoomcol = 0; zoomcol < zoom; zoomcol++ )
					{
						memcpy(dst_data + (zoomrow * dst->width + zoomcol) * 4, src_data, 4);
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
		assert( dst->height*zoom >= src->height );
		zoom2 = zoom * zoom;

		dst_data = &dst->image_data[0];
		for ( row = 0; row < dst->height; row++ )
		{
			src_data = &src->image_data[0] + row * src->width * zoom * 4;
			for ( col = 0; col < dst->width; col++ )
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

static void makeSolidTile(progimage_info *dst, int chosenTile, int solid)
{
    int row,col,dst_offset;
    unsigned char* dst_data;

    unsigned char color[4];
    double dcolor[4];
    double sum_color[3],sum;
    int tileSize;

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

