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

//#define TILE_PATH	L".\\tiles\\"
#define TILE_PATH	L"tiles/"
#define OUTPUT_FILENAME L"terrainExt"

int findTile( TCHAR *tileName );
int findNextTile( TCHAR *tileName, int index );

static void reportReadError( int rc, TCHAR *filename );

static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, progimage_info *src, int tileSize);
static void copyPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, progimage_info *src, int src_x_min, int src_y_min);

int setPNGTile(progimage_info *dst, int x, int y, unsigned int value);
int multPNGTile(progimage_info *dst, int x, int y, int r, int g, int b);
int addPNGTile(progimage_info *dst, int x, int y, int r, int g, int b);

typedef struct int2 {
	int x;
	int y;
} int2;

int _tmain(int argc, _TCHAR* argv[])
{
	int rc;
	progimage_info basicterrain;
	progimage_info destination;
	progimage_info *destination_ptr = &destination;

	int i;

	int tilesFound = 0;
	int tilesFoundArray[TOTAL_TILES];
	progimage_info tile[TOTAL_TILES];
	int baseTileSize, xTiles, yTiles, xResolution, yResolution, outputYresolution;

	TCHAR tileOutput[MAX_PATH];
	TCHAR tilePath[MAX_PATH];

	wcscpy_s(tileOutput, MAX_PATH, OUTPUT_FILENAME );	// no .png yet
	wcscpy_s(tilePath, MAX_PATH, TILE_PATH );

	// single argument is alternate subdirectory other than "tiles"
	if ( argc == 2 )
	{
		wcscat_s( tileOutput, MAX_PATH, L"_" );
		wcscat_s( tileOutput, MAX_PATH, argv[1] );

		wcscpy_s( tilePath, MAX_PATH, argv[1] );
		wcscat_s( tilePath, MAX_PATH, L"/" );
	}
	else if ( argc > 2 )
	{
		wprintf( L"usage: %S [relative path to tiles directory]\n", argv[0]);
		return 1;
	}
	wcscat_s( tileOutput, MAX_PATH, L".png" );

	// read the base terrain file
	rc = readpng(&basicterrain, L"terrainBase.png");
	if ( rc != 0 ) { reportReadError(rc,L"terrainBase.png"); return 1;}
	readpng_cleanup(0,&basicterrain);

	// TODO: right now we assume tiles are well-behaved and fit nicely together.
	// Need to add code to zoom base tile to be larger, etc.
	xTiles = 16;
	baseTileSize = basicterrain.width / xTiles;
	yTiles = basicterrain.height / baseTileSize;
	xResolution = xTiles * baseTileSize;
	yResolution = yTiles * baseTileSize;

	// look through tiles in tiles directory, see which exist, find maximum Y value.
	{
		HANDLE hFind;
		WIN32_FIND_DATA ffd;

		TCHAR tileSearch[MAX_PATH];
		wcscpy_s( tileSearch, MAX_PATH, tilePath );
		wcscat_s( tileSearch, MAX_PATH, L"*.png" );
		hFind=FindFirstFile(tileSearch,&ffd);

		if (hFind == INVALID_HANDLE_VALUE) 
		{
			printf ("FindFirstFile failed (error # %d)\n", GetLastError());
			return 1;
		} 
		else 
		{
			do {
				TCHAR tileName[MAX_PATH];
				int len;

				// _tprintf (TEXT("The file found is %s\n"), ffd.cFileName);

				wcscpy_s( tileName, MAX_PATH, ffd.cFileName );
				// check for .png suffix - note test is case insensitive
				len = wcslen(tileName);
				if ( _wcsicmp( &tileName[len-4], L".png" ) == 0 )
				{
					int index;
					// remove .png suffix
					tileName[len-4] = 0x0;
					index = findTile(tileName);
					if ( index == 0 )
					{
						_tprintf (TEXT("NOTE: the file %s is not used\n"), ffd.cFileName);
					}

					while ( index >= 0 )
					{
						// tile is one we care about.
						tilesFoundArray[tilesFound] = index;
						// find maximum Y resolution of output tile
						if ( yTiles-1 < gTiles[index].txrY )
						{
							yTiles = gTiles[index].txrY + 1;
						}

						{
							// read image file - build path
							TCHAR readFileName[MAX_PATH];
							wcscpy_s( readFileName, MAX_PATH, tilePath );
							wcscat_s( readFileName, MAX_PATH, ffd.cFileName );
							// read in tile for later
							rc = readpng(&tile[tilesFound], readFileName);
							if ( rc != 0 ) { reportReadError(rc,readFileName); return 1;}
							readpng_cleanup(0,&tile[tilesFound]);
						}

						// for now, ignore any images not of the right width
						if ( (tile[tilesFound].width == 16) && 
							 ( tile[tilesFound].bit_depth == 8 ) &&
							 ( tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB_ALPHA || tile[tilesFound].color_type == PNG_COLOR_TYPE_RGB ))
						{
							tilesFound++;
						}
						else
						{
							// different size - TODO corrective action
						}
						index = findNextTile(tileName, index);
					}
				}
			} while (FindNextFile(hFind,&ffd)!=0);

			FindClose(hFind);
		}
	}

	// allocate output image and fill it up
	destination_ptr = (progimage_info *)malloc(sizeof(progimage_info));
	memset(destination_ptr,0,sizeof(progimage_info));

	outputYresolution = yTiles * baseTileSize;

	//destination_ptr->gamma = 0.0;
	destination_ptr->width = xResolution;
	destination_ptr->height = outputYresolution;
	//destination_ptr->have_time = 0;
	//destination_ptr->modtime;
	destination_ptr->color_type = PNG_COLOR_TYPE_RGB_ALPHA;	// RGBA - PNG_COLOR_TYPE_RGB_ALPHA
	////FILE *infile;
	////returns: void *png_ptr;
	////returns: void *info_ptr;
	destination_ptr->bit_depth = 8;
	//destination_ptr->interlaced = PNG_INTERLACE_NONE;
	//destination_ptr->have_bg = 0;
	//destination_ptr->bg_red;
	//destination_ptr->bg_green;
	//destination_ptr->bg_blue;
	////uch *image_data;
	destination_ptr->image_data = (unsigned char *)malloc(xResolution*outputYresolution*4*sizeof(unsigned char));
	//uch **row_pointers;
	//destination_ptr->row_pointers = (uch **)malloc(256*sizeof(uch *));
	//destination_ptr->have_text = TEXT_TITLE|TEXT_AUTHOR|TEXT_DESC;
	//destination_ptr->title = "Mineways model texture";
	//destination_ptr->author = "mineways.com";
	//destination_ptr->desc = "Mineways texture file for model, generated from base terrainBase.png";
	//destination_ptr->copyright;
	//destination_ptr->email;
	//destination_ptr->url;
	//destination_ptr->jmpbuf;

	// copy base texture over
	// TODO may need to magnify base texture someday
	copyPNGArea(destination_ptr, 0, 0, xResolution, 16*baseTileSize, &basicterrain, 0, 0);

	// clear rest (we're assuming the data is logically ordered, top to bottom)
	if ( yTiles > 16 )
		memset(destination_ptr->image_data + xResolution*(16*baseTileSize)*4,0,xResolution*(outputYresolution-16*baseTileSize)*4*sizeof(unsigned char));

	// copy tiles found over
	for ( i = 0; i < tilesFound; i++ )
	{
		int index = tilesFoundArray[i];
		copyPNGTile(destination_ptr, gTiles[index].txrX, gTiles[index].txrY, &tile[i], baseTileSize);
	}

	// write out the result
	rc = writepng(destination_ptr, tileOutput);
	if ( rc != 0 ) { reportReadError(rc,tileOutput); return 1;}
	writepng_cleanup(destination_ptr);

	return 0;
}

int findTile( TCHAR *tileName )
{
	int i;

	for ( i = 0; i < TOTAL_TILES; i++ )
	{
		if ( wcscmp(tileName, gTiles[i].filename) == 0 )
			return i;
	}
	return -1;
}

int findNextTile( TCHAR *tileName, int index )
{
	int i;

	for ( i = index+1; i < TOTAL_TILES; i++ )
	{
		if ( wcscmp(tileName, gTiles[i].filename) == 0 )
			return i;
	}
	return -1;
}

//====================== statics ==========================

static void reportReadError( int rc, TCHAR *filename )
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

static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, progimage_info *src, int tileSize)
{
    int row;
    int dst_offset;
    int src_offset;
	// special for fire: take last element, and make white areas alpha 0 and black
	int multitile = ( src->width < src->height );

    assert( dst_x*tileSize+tileSize-1 < dst->width );	// destination can't hold tile
	assert( tileSize <= src->width );	// source does not match tile size
	assert( src->bit_depth == 8 );	// sorry, other formats not accepted. I've seen bit depth 4, in Better Than Default, TODO
	assert( src->interlaced != PNG_INTERLACE_ADAM7 );	// interlacing not supported (why do it???)

    for ( row = 0; row < tileSize; row++ )
    {
        dst_offset = ((dst_y*tileSize + row) * dst->width + dst_x*tileSize) * 4;
 		if ( src->color_type == PNG_COLOR_TYPE_RGB_ALPHA )
		{
			// use last tile
			int col;
			src_offset = ( (src->height - src->width) + row ) * src->width * 4;
			for ( col = 0; col < tileSize; col++ )
			{
				// Treat alpha == 0 as clear - nicer to set to black. This happens with fire,
				// and the flowers and double flowers have junk in the RGB channels where alpha == 0.
				if ( (src->image_data+src_offset)[3] == 0 )
				{
					*(dst->image_data+dst_offset++) = 0;
					*(dst->image_data+dst_offset++) = 0;
					*(dst->image_data+dst_offset++) = 0;
					*(dst->image_data+dst_offset++) = 0;
					src_offset += 4;
				}
				else
				{
					*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
					*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
					*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
					*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
				}
			}

			// we can do the quick copy
			//src_offset = row * src->width * 4;
			//memcpy(dst->image_data+dst_offset, src->image_data+src_offset, tileSize*4);
		}
		else if ( src->color_type == PNG_COLOR_TYPE_RGB )
		{
			// used by only lava_still, for some weird reason
			// copy colors, alpha set to 255
			int col;
			// last tile
			src_offset = ( (src->height - src->width) + row ) * src->width * 3;
			for ( col = 0; col < tileSize; col++ )
			{
				*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
				*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
				*(dst->image_data+dst_offset++) = *(src->image_data+src_offset++);
				*(dst->image_data+dst_offset++) = (unsigned char)0xff;
			}
		}
		else
		{
			// TODO could add gray and gray alpha and alpha only (what's that mean? Used by Better Than Default), not sure these are used...
			assert(0);
		}
    }
}

static void copyPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, progimage_info *src, int src_x_min, int src_y_min)
{
	int row;
	int dst_offset,src_offset;

	for ( row = 0; row < size_y; row++ )
	{
		dst_offset = ((dst_y_min+row)*dst->width + dst_x_min) * 4;
		src_offset = ((src_y_min+row)*src->width + src_x_min) * 4;
		memcpy(dst->image_data+dst_offset, src->image_data+src_offset, size_x*4);
	}
}

int setPNGTile(progimage_info *dst, int x, int y, unsigned int value)
{
	int row, col;
	unsigned int *di;

	assert( x*16+15 < dst->width );

	for ( row = 0; row < 16; row++ )
	{
		di = ((unsigned int *)dst->image_data) + ((y*16 + row) * dst->width + x*16);
		for ( col = 0; col < 16; col++ )
		{
			*di++ = value;
		}
	}
	return 0;
}

int multPNGTile(progimage_info *dst, int x, int y, int r, int g, int b)
{
	unsigned char *dc;
	int row, col;
	assert( x*16+15 < dst->width );

	for ( row = 0; row < 16; row++ )
	{
		dc = dst->image_data + ((y*16 + row) * dst->width + x*16)*4;
		for ( col = 0; col < 16; col++ )
		{
			*dc++ = (unsigned char)(((int)*dc * r)/255);
			*dc++ = (unsigned char)(((int)*dc * g)/255);
			*dc++ = (unsigned char)(((int)*dc * b)/255);
			dc++;    // alpha untouched
		}
	}
	return 0;
}

int addPNGTile(progimage_info *dst, int x, int y, int r, int g, int b)
{
	unsigned char *dc;
	int row, col;
	assert( x*16+15 < dst->width );

	for ( row = 0; row < 16; row++ )
	{
		dc = dst->image_data + ((y*16 + row) * dst->width + x*16)*4;
		for ( col = 0; col < 16; col++ )
		{
			*dc++ = (unsigned char)(((int)*dc + r));
			*dc++ = (unsigned char)(((int)*dc + g));
			*dc++ = (unsigned char)(((int)*dc + b));
			dc++;    // alpha untouched
		}
	}
	return 0;
}

int upscalepng_2x2(progimage_info *dst, progimage_info *src)
{
	int row, col;
	unsigned int *di, *si;
	//di = (unsigned int *)dst->image_data;

	//for ( row = 0; row < dst->height; row++ )
	//{
	//	for ( col = 0; col < dst->width; col++ )
	//	{
	//		si = (unsigned int *)src->image_data;
	//		si += ((int)(row/2)) * src->width + (int)(col/2);
	//		*di++ = *si;
	//	}
	//}
	si = (unsigned int *)src->image_data;
	for ( row = 0; row < src->height; row++ )
	{
		for ( col = 0; col < src->width; col++ )
		{
			di = (unsigned int *)dst->image_data + (row*2)*dst->width + col*2;
			*di++ = *si;
			*di++ = *si;
			di = (unsigned int *)dst->image_data + (row*2+1)*dst->width + col*2;
			*di++ = *si;
			*di++ = *si++;
		}
	}
	return 0;
}

int cleanImage(progimage_info *dst)
{
	int row, col;
	unsigned char *dc;
	dc = dst->image_data;

	for ( row = 0; row < dst->height; row++ )
	{
		for ( col = 0; col < dst->width; col++ )
		{
			if ( dc[3] == 0 )
			{
				*dc++ = 0;
				*dc++ = 0;
				*dc++ = 0;
				dc++;
			}
			else
			{
				dc += 4;
			}
		}
	}
	return 0;
}


