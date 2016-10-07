/*
Copyright (c) 2010, Sean Kasun
Parts Copyright (c) 2010, Ryan Hitchman
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


// MinewaysMap.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "biomes.h"
#include <assert.h>
#include <string.h>

static unsigned char* draw(WorldGuide *pWorldGuide,int bx,int bz,int topy,Options opts,
    ProgressCallback callback,float percent,int *hitsFound);
static void blit(unsigned char *block,unsigned char *bits,int px,int py,
    double zoom,int w,int h);
static int createBlockFromSchematic(WorldGuide *pWorldGuide, int cx, int cz, WorldBlock *block);
static void initColors();


static int gColorsInited=0;
static unsigned int gBlockColors[256*16];
static unsigned char gEmptyR,gEmptyG,gEmptyB;
static unsigned char gBlankTile[16 * 16 * 4];
static unsigned char gBlankHighlitTile[16 * 16 * 4];
static unsigned char gBlankTransitionTile[16 * 16 * 4];

static unsigned short gColormap=0;
static long long gMapSeed;

static int gBoxHighlightUsed=0;
static int gBoxMinX;
static int gBoxMinY;
static int gBoxMinZ;
static int gBoxMaxX;
static int gBoxMaxY;
static int gBoxMaxZ;
static int gDirtyBoxMinX = INT_MAX;
static int gDirtyBoxMinZ = INT_MAX;
static int gDirtyBoxMaxX = INT_MIN;
static int gDirtyBoxMaxZ = INT_MIN;

// highlight blend factor and color
static double gHalpha = 0.3;
static double gHalphaBorder = 0.8;
static int gHred = 205;
static int gHgreen = 50;
static int gHblue = 255;
static int gHighlightID=0;

// was an unknown block read in?
static int gUnknownBlock = 0;
static int gPerformUnknownBlockCheck = 1;

void SetHighlightState( int on, int minx, int miny, int minz, int maxx, int maxy, int maxz )
{
    // we don't really require one to be min or max, we take the range
    if ( minx > maxx ) swapint(minx,maxx);
    if ( miny > maxy ) swapint(miny,maxy);
    if ( minz > maxz ) swapint(minz,maxz);

    // clean up by clamping
    miny = clamp(miny,0,MAP_MAX_HEIGHT);
    maxy = clamp(maxy,0,MAP_MAX_HEIGHT);

    // has highlight state changed?
    if ( gBoxHighlightUsed != on ||
        gBoxMinX != minx ||
        gBoxMinY != miny ||
        gBoxMinZ != minz ||
        gBoxMaxX != maxx ||
        gBoxMaxY != maxy ||
        gBoxMaxZ != maxz )
    {
        // state has changed, so invalidate rendering caches by changing highlight ID
        gHighlightID++;
        gBoxHighlightUsed = on;
        gBoxMinX = minx;
        gBoxMinY = miny;
        gBoxMinZ = minz;
        gBoxMaxX = maxx;
        gBoxMaxY = maxy;
        gBoxMaxZ = maxz;
        if ( on )
        {
            // increase dirty rectangle by new bounds
            if ( gDirtyBoxMinX > minx )
                gDirtyBoxMinX = minx;
            if ( gDirtyBoxMinZ > minz )
                gDirtyBoxMinZ = minz;
            if ( gDirtyBoxMaxX < maxx )
                gDirtyBoxMaxX = maxx;
            if ( gDirtyBoxMaxZ < maxz )
                gDirtyBoxMaxZ = maxz;
        }
    }
}


//static long long randomSeed;
//static void javaRandomSetSeed(long long seed){
//  randomSeed = (seed ^ 0x5DEECE66DL) & ((1LL << 48) - 1);
//}

//static long long javaRandomNext(int bits) {
//  long long r = randomSeed;
//  r = (r * 0x5DEECE66DL + 0xBL) & ((1LL << 48) - 1);
//  return (long long)(r >> (48 - bits));
//}
//static int javaRandomNextInt(int n) {
//    long long bits,val;
//   if ((n & -n) == n)  // i.e., n is a power of 2
//       return (int)((n * (long long)javaRandomNext(31)) >> 31);
//   do {
//       bits = javaRandomNext(31);
//       val = bits % n;
//   } while(bits - val + (n-1) < 0);
//   return (int)val;
//}

//static long long getChunkSeed(int xPosition, int zPosition){
//    return (gMapSeed + (long long) (xPosition * xPosition * 0x4c1906) + (long long) (xPosition * 0x5ac0db) +
//             (long long) (zPosition * zPosition) * 0x4307a7L + (long long) (zPosition * 0x5f24f)) ^ 0x3ad8025f;
//}

//static int isSlimeChunk(int x, int z){
//    long long nextSeed = getChunkSeed(x, z);
//    javaRandomSetSeed(nextSeed);
//    return javaRandomNextInt(10)==0;
//}

void GetHighlightState( int *on, int *minx, int *miny, int *minz, int *maxx, int *maxy, int *maxz )
{
    *on = gBoxHighlightUsed;
    *minx = gBoxMinX;
    *miny = gBoxMinY;
    *minz = gBoxMinZ;
    *maxx = gBoxMaxX;
    *maxy = gBoxMaxY;
    *maxz = gBoxMaxZ;
}

//world = path to world saves
//cx = center x world
//cz = center z world
//y = start depth
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%)
//bits = byte array for output
//opts = bitmasks of render options (see MinewaysMap.h)
void DrawMap(WorldGuide *pWorldGuide, double cx, double cz, int topy, int w, int h, double zoom, unsigned char *bits, Options opts, int *hitsFound, ProgressCallback callback)
{
    /* We're converting between coordinate systems: 
    *
    * X     -world z N  -screen y
    * screen origin  |
    *                |
    *                |
    *                |
    *  -world x      |(cx,cz)   +world x
    * W--------------+----------------E
    *  -screen x     |          +screen x
    *                |
    *                | 
    *                |
    *      +world z  | +screen y
    *                S
    */

    unsigned char *blockbits;
    int z,x,px,py;
    int blockScale=(int)(16*zoom);

    // number of blocks to fill the screen (plus 2 blocks for floating point inaccuracy)
    int hBlocks=(w+blockScale*2)/blockScale;
    int vBlocks=(h+blockScale*2)/blockScale;

    // cx/cz is the center, so find the upper left corner from that
    double startx=cx-(double)w/(2*zoom);
    double startz=cz-(double)h/(2*zoom);
    // TODO: I suspect these want to be floors, not ints; int
    // rounds towards 0, floor takes -4.5 and goes to -5.
    int startxblock=(int)(startx/16);
    int startzblock=(int)(startz/16);
    int shiftx=(int)((startx-startxblock*16)*zoom);
    int shifty=(int)((startz-startzblock*16)*zoom);

    if (shiftx<0)
    {
		// essentially the floor function
        startxblock--;
        shiftx+=blockScale;
    }
    if (shifty<0)
    {
		// essentially the floor function
		startzblock--;
        shifty+=blockScale;
    }

    if (!gColorsInited)
        initColors();

    // x increases south, decreases north
    for (z=0,py=-shifty;z<=vBlocks;z++,py+=blockScale)
    {
        // z increases west, decreases east
        for (x=0,px=-shiftx;x<=hBlocks;x++,px+=blockScale)
        {
            blockbits = draw(pWorldGuide,startxblock+x,startzblock+z,topy,opts,callback,(float)(z*hBlocks+x)/(float)(vBlocks*hBlocks),hitsFound);
            blit(blockbits,bits,px,py,zoom,w,h);
        }
    }
    // clear dirty rectangle, if any
    if ( gBoxHighlightUsed )
    {
        // box is set to current rectangle
        // TODO: this isn't quite right, as if you select a large rect, scroll it offscreen
        // then select new and scroll back, you'll see the highlight.
        gDirtyBoxMinX = gBoxMinX;
        gDirtyBoxMinZ = gBoxMinZ;
        gDirtyBoxMaxX = gBoxMaxX;
        gDirtyBoxMaxZ = gBoxMaxZ;
    }
    else
    {
        // empty
		gDirtyBoxMinX = gDirtyBoxMinZ = INT_MAX;
		gDirtyBoxMaxX = gDirtyBoxMaxZ = INT_MIN;
    }
}

static struct {
    char *name;
} gExtraBlockNames[] = {
    { "Spruce Leaves" },
    { "Birch Leaves" },
    { "Jungle Leaves" },
    { "Dark Oak Leaves" },
    { "Blue Orchid" },	// flowers (poppies)
    { "Allium" },	// 5
    { "Azure Bluet" },
    { "Red Tulip" },
    { "Orange Tulip" },
    { "White Tulip" },
    { "Pink Tulip" },	// 10
    { "Oxeye Daisy" },
    { "Lilac" },	// tall flowers
    { "Double Tallgrass" },
    { "Large Fern" },
    { "Rose Bush" },	// 15
    { "Peony" },
    { "Dead Bush" },
    { "Tall Grass" },
    { "Fern" },
    { "Spruce Wood Planks" },   // 20
    { "Birch Wood Planks" },
    { "Jungle Wood Planks" },
    { "Acacia Wood Planks" },
    { "Dark Oak Wood Planks" },
    { "Granite" },              // 25
    { "Polished Granite" },
    { "Diorite" },
    { "Polished Diorite" },
    { "Andesite" },
    { "Polished Andesite" },    // 30
    { "Coarse Dirt" },
    { "Podzol" },
    { "Spruce Sapling" },
    { "Birch Sapling" },
    { "Jungle Sapling" },   // 35
    { "Acacia Sapling" },
    { "Dark Oak Sapling" },
    { "Red Sand" },
};

#define STRING_SPRUCE_LEAVES	0
#define STRING_BIRCH_LEAVES		1
#define STRING_JUNGLE_LEAVES	2
#define STRING_DARK_OAK_LEAVES	3
#define STRING_BLUE_ORCHID		4
#define STRING_ALLIUM			5
#define STRING_AZURE_BLUET		6
#define STRING_RED_TULIP		7
#define STRING_ORANGE_TULIP		8
#define STRING_WHITE_TULIP		9
#define STRING_PINK_TULIP		10
#define STRING_OXEYE_DAISY		11
#define STRING_LILAC			12
#define STRING_DOUBLE_TALLGRASS	13
#define STRING_LARGE_FERN		14
#define STRING_ROSE_BUSH		15
#define STRING_PEONY			16
#define STRING_DEAD_BUSH		17
#define STRING_TALL_GRASS		18
#define STRING_FERN				19
#define STRING_SPRUCE_WOOD_PLANKS	20
#define STRING_BIRCH_WOOD_PLANKS	21
#define STRING_JUNGLE_WOOD_PLANKS	22
#define STRING_ACACIA_WOOD_PLANKS	23
#define STRING_DARK_OAK_WOOD_PLANKS	24
#define STRING_GRANITE              25
#define STRING_POLISHED_GRANITE     26
#define STRING_DIORITE              27
#define STRING_POLISHED_DIORITE     28
#define STRING_ANDESITE             29
#define STRING_POLISHED_ANDESITE    30
#define STRING_COARSE_DIRT          31   
#define STRING_PODZOL               32
#define STRING_SPRUCE_SAPLING	    33
#define STRING_BIRCH_SAPLING	    34
#define STRING_JUNGLE_SAPLING	    35
#define STRING_ACACIA_SAPLING	    36
#define STRING_DARK_OAK_SAPLING	    37
#define STRING_RED_SAND     	    38

//bx = x coord of pixel
//by = y coord of pixel
//cx = center x world
//cz = center z world
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%)
//ox = world x at mouse
//oz = world z at mouse
//type is block type
//biome is biome found
const char *IDBlock(int bx, int by, double cx, double cz, int w, int h, double zoom,int *ox,int *oy,int *oz,int *type,int *dataVal, int *biome, bool schematic)
{
    //WARNING: keep this code in sync with draw()
    WorldBlock *block;
    int x,y,z,px,py,xoff,zoff;
    int blockScale=(int)(16*zoom);

    // cx/cz is the center, so find the upper left corner from that
    double startx=cx-(double)w/(2*zoom);
    double startz=cz-(double)h/(2*zoom);
    // TODO: I suspect these want to be floors, not ints; int
    // rounds towards 0, floor takes -4.5 and goes to -5.
    int startxblock=(int)(startx/16);
    int startzblock=(int)(startz/16);
    int shiftx=(int)((startx-startxblock*16)*zoom);
    int shifty=(int)((startz-startzblock*16)*zoom);
	// someone could be more than 10000 blocks from spawn, so don't assert
    //assert(cz < 10000);
    //assert(cz > -10000);

    // initialize to "not set"
    *biome = -1;
    *dataVal = 0;

    if (shiftx<0)
    {
        startxblock--;
        shiftx+=blockScale;
    }
    if (shifty<0)
    {
        startzblock--;
        shifty+=blockScale;
    }

    // Adjust bx and by so they can be negative.
    // Note that things are a bit weird with numbers here.
    // I check if the mouse location is unreasonably high, which means
    // that it's meant to be a negative number instead.
    if ( bx > 0x7000 )
        bx -= 0x8000;
    if ( by > 0x7000 )
        by -= 0x8000;

    // if off window above
    // Sean's fix, but makes the screen go empty if I scroll off top of window
    //if (by<0) return "";

    x=(bx+shiftx)/blockScale;
    px=x*blockScale-shiftx;
    z=(by+shifty)/blockScale;
    py=z*blockScale-shifty;

    xoff=(int)((bx-px)/zoom);
    zoff=(int)((by-py)/zoom);

    *ox=(startxblock+x)*16+xoff;
    *oz=(startzblock+z)*16+zoff;

    block=(WorldBlock *)Cache_Find(startxblock+x, startzblock+z);

    if (block==NULL)
    {
        *oy=-1;
        *type=BLOCK_UNKNOWN;
        return "Unknown";
    }

    y=block->heightmap[xoff+zoff*16];
    *oy=y;
    *biome = block->biome[xoff+zoff*16];

    // Note that when "hide obscured" is on, blocks can be empty because
    // they were solid from the current level on down.
    if (y == (unsigned char)-1)
    {
        *oy=-1;
		if (schematic) {
			// act like the pixel is not there, vs. a block that has an empty location
			*biome = -1;
			*type = BLOCK_UNKNOWN;
			return "Unknown";
		}
		else {
			*type = BLOCK_AIR;
			return "Empty";  // nothing was rendered here
		}
    }

    // there's a bug in the original code, sometimes xoff is negative.
    // For now, assert when I see it, and return empty - better than crashing.
    // TODO - can this still happen?
    //assert( y+(zoff+xoff*16)*128 >= 0 );
    if ( y*256+zoff*16+xoff < 0 || y*256+zoff*16+xoff >= 65536) {
        *type=BLOCK_AIR;
        *biome = -1;
        return "(off map)";
    }

    *type = block->grid[xoff+zoff*16+y*256];
    *dataVal = block->data[(xoff+zoff*16+y*256)/2];
    if ( xoff & 0x01 )
        *dataVal = (*dataVal) >> 4;
    else
        *dataVal &= 0xf;


    ///////////////////////////////////
    // give a better name if possible
    switch ( *type )
    {
    case BLOCK_LEAVES:
        switch ((*dataVal) & 0x3)
        {
        default:
            break;
        case 1:	// spruce
            return gExtraBlockNames[STRING_SPRUCE_LEAVES].name;
            break;
        case 2:	// birch
            return gExtraBlockNames[STRING_BIRCH_LEAVES].name;
            break;
        case 3:	// jungle
            return gExtraBlockNames[STRING_JUNGLE_LEAVES].name;
            break;
        }
        break;

    case BLOCK_AD_LEAVES:
        switch ((*dataVal) & 0x1)
        {
        default:
            break;
        case 1:	// dark oak
            return gExtraBlockNames[STRING_DARK_OAK_LEAVES].name;
            break;
        }
        break;

    case BLOCK_TALL_GRASS:
        switch ((*dataVal) & 0x3)
        {
        default:
        case 0: // dead bush
            return gExtraBlockNames[STRING_DEAD_BUSH].name;
            break;
        case 1:	// tall grass
            return gExtraBlockNames[STRING_TALL_GRASS].name;
            break;
        case 2:	// fern
            return gExtraBlockNames[STRING_FERN].name;
            break;
        }
        break;

    case BLOCK_POPPY:
        switch ((*dataVal))
        {
        default:	// poppy
            break;
        case 1:	// blue orchid
            return gExtraBlockNames[STRING_BLUE_ORCHID].name;
            break;
        case 2:	// allium
            return gExtraBlockNames[STRING_ALLIUM].name;
            break;
        case 3:	// azure bluet
            return gExtraBlockNames[STRING_AZURE_BLUET].name;
            break;
        case 4:	// red tulip
            return gExtraBlockNames[STRING_RED_TULIP].name;
            break;
        case 5:	// orange tulip
            return gExtraBlockNames[STRING_ORANGE_TULIP].name;
            break;
        case 6:	// white tulip
            return gExtraBlockNames[STRING_WHITE_TULIP].name;
            break;
        case 7:	// pink tulip
            return gExtraBlockNames[STRING_PINK_TULIP].name;
            break;
        case 8:	// oxeye daisy
            return gExtraBlockNames[STRING_OXEYE_DAISY].name;
            break;
        }
        break;

    case BLOCK_DOUBLE_FLOWER:
        // subtract 256, one Y level, as we need to look at the bottom of the plant to ID its type.
        *dataVal = block->data[(xoff+zoff*16+(y-1)*256)/2];
        if ( xoff & 0x01 )
            (*dataVal) = (*dataVal) >> 4;
        else
            (*dataVal) &= 0xf;
        switch ((*dataVal))
        {
        default:	// sunflower
            break;
        case 1:	// lilac
            return gExtraBlockNames[STRING_LILAC].name;
            break;
        case 2:	// double tallgrass
            return gExtraBlockNames[STRING_DOUBLE_TALLGRASS].name;
            break;
        case 3:	// large fern
            return gExtraBlockNames[STRING_LARGE_FERN].name;
            break;
        case 4:	// rose bush
            return gExtraBlockNames[STRING_ROSE_BUSH].name;
            break;
        case 5:	// peony
            return gExtraBlockNames[STRING_PEONY].name;
            break;
        }
        break;

    case BLOCK_WOODEN_PLANKS:
        switch (*dataVal)
        {
        default:
            break;
        case 1:	// spruce
            return gExtraBlockNames[STRING_SPRUCE_WOOD_PLANKS].name;
            break;
        case 2:	// birch
            return gExtraBlockNames[STRING_BIRCH_WOOD_PLANKS].name;
            break;
        case 3:	// jungle
            return gExtraBlockNames[STRING_JUNGLE_WOOD_PLANKS].name;
            break;
        case 4:	// acacia
            return gExtraBlockNames[STRING_ACACIA_WOOD_PLANKS].name;
            break;
        case 5:	// dark oak
            return gExtraBlockNames[STRING_DARK_OAK_WOOD_PLANKS].name;
            break;
        }
        break;

    case BLOCK_STONE:
        switch (*dataVal)
        {
        default:
            break;
        case 1:
            return gExtraBlockNames[STRING_GRANITE].name;
            break;
        case 2:
            return gExtraBlockNames[STRING_POLISHED_GRANITE].name;
            break;
        case 3:
            return gExtraBlockNames[STRING_DIORITE].name;
            break;
        case 4:
            return gExtraBlockNames[STRING_POLISHED_DIORITE].name;
            break;
        case 5:
            return gExtraBlockNames[STRING_ANDESITE].name;
            break;
        case 6:
            return gExtraBlockNames[STRING_POLISHED_ANDESITE].name;
            break;
        }
        break;

    case BLOCK_DIRT:
        switch (*dataVal)
        {
        default:
            break;
        case 1:
            return gExtraBlockNames[STRING_COARSE_DIRT].name;
            break;
        case 2:
            return gExtraBlockNames[STRING_PODZOL].name;
            break;
        }
        break;

    case BLOCK_SAPLING:
        switch (*dataVal)
        {
        default:
            break;
        case 1:	// spruce
            return gExtraBlockNames[STRING_SPRUCE_SAPLING].name;
            break;
        case 2:	// birch
            return gExtraBlockNames[STRING_BIRCH_SAPLING].name;
            break;
        case 3:	// jungle
            return gExtraBlockNames[STRING_JUNGLE_SAPLING].name;
            break;
        case 4:	// acacia
            return gExtraBlockNames[STRING_ACACIA_SAPLING].name;
            break;
        case 5:	// dark oak
            return gExtraBlockNames[STRING_DARK_OAK_SAPLING].name;
            break;
        }
        break;
    }

    return gBlockDefinitions[*type].name;
}

//copy block to bits at px,py at zoom.  bits is wxh
static void blit(unsigned char *block,unsigned char *bits,int px,int py,
    double zoom,int w,int h)
{
    int x,y,yofs,bitofs;
    int skipx=0,skipy=0;
    int bw=(int)(16*zoom);
    int bh=(int)(16*zoom);
    if (px<0) skipx=-px;
    if (px+bw>=w) bw=w-px;
    if (bw<=0) return;
    if (py<0) skipy=-py;
    if (py+bh>=h) bh=h-py;
    if (bh<=0) return;
    bits+=py*w*4;
    bits+=px*4;
    for (y=0;y<bh;y++,bits+=w<<2)
    {
        if (y<skipy) continue;
        yofs=((int)(y/zoom))<<6;
        bitofs=0;
        if (zoom == 1.0 && skipx == 0 && bw == 16) {
            memcpy(bits+bitofs,block+yofs,16*4);
        } else {
            for (x=0;x<bw;x++,bitofs+=4)
            {
                if (x<skipx) continue;
                memcpy(bits+bitofs,block+yofs+(((int)(x/zoom))<<2),4);
            }
        }
    }
}

void CloseAll()
{
    Cache_Empty();
}

static unsigned int checkSpecialBlockColor( WorldBlock * block, unsigned int voxel, unsigned char type, int light, char useBiome, char useElevation )
{
    unsigned int color = 0xFFFFFF;
    unsigned int r,g,b;
    unsigned char dataVal;
    bool lightComputed = false;
    float alpha;
    bool alphaComputed = false;
    int affectedByBiome = 0;

    switch (type)
    {
    case BLOCK_WOOL:
    case BLOCK_CARPET:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch ( dataVal )
        {
            // I picked the color from the tile location 2 from the left, 3 down.
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xEEEEEE;
            color = gBlockColors[type*16+light];
            break;
        case 1:
            color = 0xDA8248;
            break;
        case 2:
            color = 0xBA5EC2;
            break;
        case 3:
            color = 0x7B96CD;
            break;
        case 4:
            color = 0xC1B52A;
            break;
        case 5:
            color = 0x46BA3A;
            break;
        case 6:
            color = 0xD597A7;
            break;
        case 7:
            color = 0x434343;
            break;
        case 8:
            color = 0xA6ACAC;
            break;
        case 9:
            color = 0x307592;
            break;
        case 10:
            color = 0x8643BF;
            break;
        case 11:
            color = 0x2E3B97;
            break;
        case 12:
            color = 0x53351F;
            break;
        case 13:
            color = 0x384B1B;
            break;
        case 14:
            color = 0xA23732;
            break;
        case 15:
            color = 0x1D1818;
            break;
        }
        break;

    case BLOCK_STAINED_CLAY:
        // from upper left corner
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch ( dataVal )
        {
            // I picked the color from the tile location 2 from the left, 3 down.
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xCEAE9E;
            color = gBlockColors[type*16+light];
            break;
        case 1:
            color = 0x9D5021;
            break;
        case 2:
            color = 0x925469;
            break;
        case 3:
            color = 0x6D6987;
            break;
        case 4:
            color = 0xB6801F;
            break;
        case 5:
            color = 0x647230;
            break;
        case 6:
            color = 0x9D4A4B;
            break;
        case 7:
            color = 0x362621;
            break;
        case 8:
            color = 0x84665D;
            break;
        case 9:
            color = 0x535758;
            break;
        case 10:
            color = 0x734253;
            break;
        case 11:
            color = 0x473858;
            break;
        case 12:
            color = 0x4A2F21;
            break;
        case 13:
            color = 0x484F27;
            break;
        case 14:
            color = 0x8B392B;
            break;
        case 15:
            color = 0x21120D;
            break;
        }
        break;

    case BLOCK_STAINED_GLASS:
    case BLOCK_STAINED_GLASS_PANE:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch ( dataVal )
        {
            // from 2 down, 2 to the right, basically upper left inside the frame
        default:
            assert(0);
        case 0:
            lightComputed = true;
            //color = 0xEFEFEF;
            color = gBlockColors[type*16+light];
            break;
        case 1:
            color = 0xDFBB9D;
            break;
        case 2:
            color = 0xCFA7DF;
            break;
        case 3:
            color = 0xB1C5DF;
            break;
        case 4:
            color = 0xE3E39D;
            break;
        case 5:
            color = 0xBBD995;
            break;
        case 6:
            color = 0xE9BBCB;
            break;
        case 7:
            color = 0xA7A7A7;
            break;
        case 8:
            color = 0xC5C5C5;
            break;
        case 9:
            color = 0xA7BBC5;
            break;
        case 10:
            color = 0xBBA1CF;
            break;
        case 11:
            color = 0x9DA7CF;
            break;
        case 12:
            color = 0xB1A79D;
            break;
        case 13:
            color = 0xB1BB9D;
            break;
        case 14:
            color = 0xC59D9D;
            break;
        case 15:
            color = 0x959595;
            break;
        }
        // now premultiply by alpha
        r=color>>16;
        g=(color>>8)&0xff;
        b=color&0xff;
        alpha = gBlockDefinitions[type].alpha;
        r=(unsigned char)(r*alpha);
        g=(unsigned char)(g*alpha);
        b=(unsigned char)(b*alpha);

        color=(r<<16)|(g<<8)|b;

        break;

    case BLOCK_WOODEN_PLANKS:
    case BLOCK_WOODEN_DOUBLE_SLAB:
    case BLOCK_WOODEN_SLAB:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
        switch (dataVal & 0x7)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// spruce
            color = 0x634C2B;
            break;
        case 2:	// birch
            color = 0xC5B477;
            break;
        case 3:	// jungle
            color = 0x9C6E47;
            break;
        case 4:	// acacia
            color = 0xAA5A2F;
            break;
        case 5:	// dark oak
            color = 0x3B260F;
            break;
        }
        break;

    case BLOCK_STONE:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// granite
            color = 0xA77562;
            break;
        case 2:	// polished granite
            color = 0x946251;
            break;
        case 3:	// diorite
            color = 0x9B9B9E;
            break;
        case 4:	// polished diorite
            color = 0xC9C9CD;
            break;
        case 5:	// andesite
            color = 0x7F7F83;
            break;
        case 6:	// polished andesite
            color = 0x7F7F84;
            break;
        }
        break;

    case BLOCK_SAND:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// red sand
            color = 0xA85420;
            break;
        }
        break;

    case BLOCK_LOG:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal & 0x3)
        {
        default:
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// spruce
            color = 0x291806;
            break;
        case 2:	// birch
            color = 0xE2E8DF;
            break;
        case 3:	// jungle
            color = 0x584419;
            break;
        }
        break;

    case BLOCK_LEAVES:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal & 0x3)
        {
        default:
        case 0:	// oak
        case 3:	// jungle
            // if the default color is in use, use something more visible
            if ( gBlockDefinitions[BLOCK_LEAVES].read_color == gBlockDefinitions[BLOCK_LEAVES].color )
            {
                // NOTE: considerably darker than what is stored:
                // the stored value is used to affect only the output color, not the map color.
                // This oak leaf color (and jungle, below) makes the trees easier to pick out.

                // jungle and oak
                color = dataVal ? 0x46AD19 : 0x3A7F1B ;
            }
            else
            {
                lightComputed = true;
                color = gBlockColors[type*16+light];
            }
            affectedByBiome = 2;
            break;
        case 1:	// spruce
            color = 0x3D623D;
            break;
        case 2:	// birch
            color = 0x6B8D46;
            break;
        }
        break;

    case BLOCK_AD_LEAVES:
        affectedByBiome = 2;
        // if the default color is in use, use something more visible
        if ( gBlockDefinitions[BLOCK_LEAVES].read_color == gBlockDefinitions[BLOCK_LEAVES].color )
        {
            // NOTE: considerably darker than what is stored:
            // the stored value is used to affect only the output color, not the map color.
            // This oak leaf color (and jungle, below) makes the trees easier to pick out.

            // acacia and 
            dataVal = block->data[voxel/2];
            if ( voxel & 0x01 )
                dataVal = dataVal >> 4;
            else
                dataVal &= 0xf;
            // dark oak and acacia
            color = dataVal ? 0x2C6F0F : 0x3D9A14 ;
        }
        else
        {
            lightComputed = true;
            color = gBlockColors[type*16+light];
        }
        affectedByBiome = 2;
        break;

    case BLOCK_TALL_GRASS:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal & 0x3)
        {
        default:
        case 0: // dead bush
            color = 0x946428;
            break;
        case 1:	// tall grass
        case 2:	// fern
            // by default, color is used for grass and ferns, which are more common
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        }
        break;

    case BLOCK_GRASS:
    case BLOCK_VINES:
        affectedByBiome = 1;
        lightComputed = true;
        color = gBlockColors[type*16+light];
        break;

    case BLOCK_AD_LOG:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal & 0x3)
        {
        default:	// acacia
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// dark oak
            color = 0x342816;
            break;
        }
        break;

    case BLOCK_DOUBLE_STONE_SLAB:
    case BLOCK_STONE_SLAB:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        alphaComputed = true;
        switch (dataVal)
        {
        default:
        case 8:	// full stone
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// sandstone
        case 9:	// full sandstone
            color = gBlockDefinitions[BLOCK_SANDSTONE].pcolor;
            break;
        case 2:	// wooden
            color = gBlockDefinitions[BLOCK_WOODEN_PLANKS].pcolor;
            break;
        case 3:	// cobblestone
        case 11:	// cobblestone
            color = gBlockDefinitions[BLOCK_COBBLESTONE].pcolor;
            break;
        case 4:	// bricks
        case 12:	// bricks
            color = gBlockDefinitions[BLOCK_BRICK].pcolor;
            break;
        case 5:	// stone brick
        case 13:	// stone brick
            color = gBlockDefinitions[BLOCK_STONE_BRICKS].pcolor;
            break;
        case 6:	// nether brick
        case 14:	// nether brick
            color = gBlockDefinitions[BLOCK_NETHER_BRICKS].pcolor;
            break;
        case 7:	// quartz
        case 15:	// quartz
            color = gBlockDefinitions[BLOCK_QUARTZ_BLOCK].pcolor;
            break;
        case 10:	// tile quartz or upper wooden slab
            color = gBlockDefinitions[(type == BLOCK_DOUBLE_STONE_SLAB) ? BLOCK_QUARTZ_BLOCK : BLOCK_WOODEN_PLANKS].pcolor;
            break;
        }
        break;

    case BLOCK_POPPY:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal)
        {
        default:	// poppy
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// blue orchid
            color = 0x26ABF8;
            break;
        case 2:	// allium
            color = 0xB562F8;
            break;
        case 3:	// azure bluet
            color = 0xE1E7EF;
            break;
        case 4:	// red tulip
            color = 0xC02905;
            break;
        case 5:	// orange tulip
            color = 0xDE6E20;
            break;
        case 6:	// white tulip
            color = 0xE4E4E4;
            break;
        case 7:	// pink tulip
            color = 0xE7BBE7;
            break;
        case 8:	// oxeye daisy
            color = 0xE7D941;
            break;
        }
        break;

    case BLOCK_DOUBLE_FLOWER:
        // subtract 256, one Y level, as we need to look at the bottom of the plant to ID its type.
        dataVal = block->data[(voxel-256)/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal)
        {
        default:
        case 0:	// sunflower
            color = 0xEAD31F;
            break;
        case 1:	// lilac
            color = 0xB79ABB;
            break;
        case 2:	// double tallgrass
            // we use color as the grass multiplier color
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 3:	// large fern
            // we use color as the grass multiplier color
            affectedByBiome = 1;
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 4:	// rose bush
            color = 0xF4210B;
            break;
        case 5:	// peony
            color = 0xE3BCF4;
            break;
        }
        break;

    case BLOCK_SPONGE:
        dataVal = block->data[voxel/2];
        if ( voxel & 0x01 )
            dataVal = dataVal >> 4;
        else
            dataVal &= 0xf;
        switch (dataVal)
        {
        default:	// sunflower
            lightComputed = true;
            color = gBlockColors[type*16+light];
            break;
        case 1:	// wet sponge
            color = 0x999829;
            break;
        }
        break;

    case BLOCK_WATER:
        color = gBlockDefinitions[BLOCK_WATER].color;
        affectedByBiome = 3;	// by swamp only
        break;

    case BLOCK_STATIONARY_WATER:
        color = gBlockDefinitions[BLOCK_STATIONARY_WATER].color;
        affectedByBiome = 3;
        break;

    default:
        // Everything else
        lightComputed = true;
        color = gBlockColors[type*16+light];
    }

    // if biome affects color, then look up color and use it
    if ( useBiome && affectedByBiome )
    {
        // get the biome
        unsigned char biome = block->biome[voxel&0xff];	// x and z location
        int elevation;
        if ( useElevation )
        {
            elevation = max( 0, (voxel >> 8) - 64 );	// y location
        }
        else
        {
            // not used
            elevation = 0;
        }

        switch ( affectedByBiome )
        {
        default:
        case 1:
            // grass
            // We'll have to compute the effect of light, alpha, etc.
            // so turn on this flag.
            lightComputed = false;
            if ( elevation )
            {
                color = ComputeBiomeColor( biome, elevation, 1 );
            }
            else
            {
                color = gBiomes[biome].grass;
            }
            break;
        case 2:
            // trees
            // We'll have to compute the effect of light, alpha, etc.
            // so turn on this flag.
            lightComputed = false;
            if ( elevation )
            {
                color = ComputeBiomeColor( biome, elevation, 0 );
            }
            else
            {
                color = gBiomes[biome].foliage;
            }
            break;
        case 3:
            // water, in swamp
            if ( (biome&0x7f) == SWAMPLAND_BIOME )
            {
                // We'll have to compute the effect of light, alpha, etc.
                // so turn on this flag.
                lightComputed = false;
                color = BiomeSwampRiverColor( color );
            }
            else
            {
                // normal water color
                lightComputed = true;
                color = gBlockColors[type*16+light];
            }
            break;
        }
    }

    // did we factor in the effect of the light? light == 15 means
    // fully lit, so nothing further needs to be done.
    if ( !lightComputed )
    {
        // alphaComputed is true if we got the color directly from the pcolor;
        // in other words, alpha is already folded in. If not, we need to multiply by alpha.
        // It is examined only when lightComputed is false; when lightComputed
        // is true, alpha is already folded in.
        if ( !alphaComputed && ( gBlockDefinitions[type].alpha != 1.0 ) )
        {
            r=(unsigned char)((color>>16)&0xff);
            g=(unsigned char)((color>>8)&0xff);
            b=(unsigned char)color&0xff;
            alpha=gBlockDefinitions[type].alpha;
            r=(unsigned char)(r*alpha); //premultiply alpha
            g=(unsigned char)(g*alpha);
            b=(unsigned char)(b*alpha);
            color=(r<<16)|(g<<8)|b;
        }
        if (light != 15)
        {
            // compute effect of light
            double y,u,v;
            r=color>>16;
            g=(color>>8)&0xff;
            b=color&0xff;
            //we'll use YUV to darken the blocks.. gives a nice even
            //coloring
            y=0.299*r+0.587*g+0.114*b;
            u=(b-y)*0.565;
            v=(r-y)*0.713;

            y*=(double)light/15.0;
            r=(unsigned int)clamp(y+1.403*v,0,255);
            g=(unsigned int)clamp(y-0.344*u-0.714*v,0,255);
            b=(unsigned int)clamp(y+1.770*u,0,255);
            color=(r<<16)|(g<<8)|b;
        }
    }

    return color;
}

// Draw a block at chunk bx,bz
// opts is a bitmask representing render options (see MinewaysMap.h)
// returns 16x16 set of block colors to use to render map.
// colors are adjusted by height, transparency, etc.
static unsigned char* draw(WorldGuide *pWorldGuide,int bx,int bz,int maxHeight,Options opts,ProgressCallback callback,float percent,int *hitsFound)
{
    WorldBlock *block, *prevblock;
    int ofs=0,prevy,prevSely,blockSolid,saveHeight;
    unsigned int voxel;
    //int hasSlime = 0;
    int x,z,i;
    unsigned int color, viewFilterFlags;
    unsigned char type, r, g, b, seenempty;
    double alpha, blend;

    char useBiome, useElevation, cavemode, showobscured, depthshading, lighting, showAll;
    unsigned char *bits;

    //    if ((opts.worldType&(HELL|ENDER|SLIME))==SLIME)
    //            hasSlime = isSlimeChunk(bx, bz);

    useBiome=!!(opts.worldType&BIOMES);
    cavemode=!!(opts.worldType&CAVEMODE);
    showobscured=!(opts.worldType&HIDEOBSCURED);
    useElevation=!!(opts.worldType&DEPTHSHADING);
    showAll=!!(opts.worldType&SHOWALL);
    // use depthshading only if biome shading is off
    //depthshading= !useBiome && useElevation;
    depthshading= useElevation;
    lighting=!!(opts.worldType&LIGHTING);
    viewFilterFlags= BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |   // what's visible
        (showAll?(BLF_FLATSIDE|BLF_SMALL_MIDDLER|BLF_SMALL_BILLBOARD):0x0);

    block=(WorldBlock *)Cache_Find(bx,bz);

	if (block == NULL)
    {
		wcsncpy_s(pWorldGuide->directory, 260, pWorldGuide->world, 260-1);
		wcscat_s(pWorldGuide->directory, 260, L"/");
        if (opts.worldType&HELL)
        {
			wcscat_s(pWorldGuide->directory, 260, L"DIM-1/");
        }
        if (opts.worldType&ENDER)
        {
			wcscat_s(pWorldGuide->directory, 260, L"DIM1/");
        }

		block = LoadBlock(pWorldGuide, bx, bz);
		if (block == NULL) //blank tile
		{
			// highlighting off, or fully outside real area? Use blank tile.
			if (!gBoxHighlightUsed ||
				(bx * 16 + 15 < gBoxMinX) || (bx * 16 > gBoxMaxX) ||
				(bz * 16 + 15 < gBoxMinZ) || (bz * 16 > gBoxMaxZ))
					return gBlankTile;

			// fully inside? Use precomputed highlit area
			static int flux = 0;
			if ((bx * 16 > gBoxMinX) && (bx * 16 + 15 < gBoxMaxX) &&
				(bz * 16 > gBoxMinZ) && (bz * 16 + 15 < gBoxMaxZ))
				return gBlankHighlitTile;

			// draw the highlighted area
			memcpy(gBlankTransitionTile, gBlankTile, 16 * 16 * 4);
			// z increases south, decreases north
			for (z = 0; z < 16; z++)
			{
				// x increases west, decreases east
				for (x = 0; x < 16; x++)
				{
					int offset = (z * 16 + x) * 4;
					// make selected area slightly red, if at right heightmap range
					if (bx * 16 + x >= gBoxMinX && bx * 16 + x <= gBoxMaxX &&
						bz * 16 + z >= gBoxMinZ && bz * 16 + z <= gBoxMaxZ)
					{
						// blend in highlight color
						blend = gHalpha;
						// are we on a border? If so, change blend factor
						if (bx * 16 + x == gBoxMinX || bx * 16 + x == gBoxMaxX ||
							bz * 16 + z == gBoxMinZ || bz * 16 + z == gBoxMaxZ)
						{
							blend = gHalphaBorder;
						}
						gBlankTransitionTile[offset++] = (unsigned char)((double)gBlankTransitionTile[offset]*(1.0 - blend) + blend*(double)gHred);
						gBlankTransitionTile[offset++] = (unsigned char)((double)gBlankTransitionTile[offset] * (1.0 - blend) + blend*(double)gHgreen);
						gBlankTransitionTile[offset] = (unsigned char)((double)gBlankTransitionTile[offset] * (1.0 - blend) + blend*(double)gHblue);
					}
				}
			}
			return gBlankTransitionTile;
		}

        //let's only update the progress bar if we're loading
        if (callback)
            callback(percent);

        Cache_Add(bx,bz,block);
    }

    // At this point the block is loaded.

	// Is the block partially or fully inside the dirty area?
	bool isOnOrInside = (bx * 16 + 15 >= gDirtyBoxMinX && bx * 16 <= gDirtyBoxMaxX &&
		bz * 16 + 15 >= gDirtyBoxMinZ && bz * 16 <= gDirtyBoxMaxZ);

    // already rendered?
    if (block->rendery==maxHeight && block->renderopts==opts.worldType && block->colormap==gColormap)
    {
        if (block->rendermissing // wait, the last render was incomplete
            && Cache_Find(bx, bz+block->rendermissing) != NULL) {
                ; // we can do a better render now that the missing block is loaded
        } else {
            // Yes, it's been rendered, but now we need to check if the highlight number is OK:
            // If the area is inside the highlighted region, renderhilitID==gHighlightID.
            // If the area is outside the hightlighted region, renderhilitID==0.
            // Else the area should be redrawn.
            // final check, is highlighting state OK?
            if ( ((block->renderhilitID==gHighlightID) && isOnOrInside) ||
                ((block->renderhilitID==0) && !isOnOrInside) )
            {
                // there's no need to re-render, use cached image already generated
                return block->rendercache;
            }
            // else re-render, to clean up previous highlight
        }
    }

    block->rendery=maxHeight;
    block->renderopts=opts.worldType;
    // if the block to be drawn is inside, note the ID, else note it's "clean" of highlighting;
    // when we come back next time, the code above will note the rendering is OK.
    block->renderhilitID= isOnOrInside ? gHighlightID : 0;
    block->rendermissing=0;
    block->colormap=gColormap;

    bits = block->rendercache;

    // find the block to the west, so we can use its heightmap for shading
    prevblock=(WorldBlock *)Cache_Find(bx-1, bz);

    if (prevblock==NULL)
        block->rendermissing=1; //note no loaded block to west
    else if (prevblock->rendery!=maxHeight || prevblock->renderopts!=opts.worldType) {
        block->rendermissing=1; //note improperly rendered block to west
        prevblock = NULL; //block was rendered at a different y level, ignore
    }
    // z increases south, decreases north
    for (z=0;z<16;z++)
    {
        // prevy is the height of the block to the left (west) of the current block, for shadowing.
        // Note it is set to the previous y height for the loop below.
        if (prevblock!=NULL)
            prevy = prevblock->heightmap[15+z*16];
        else
            prevy=-1;

        // x increases west, decreases east
        for (x=0;x<16;x++)
        {
            prevSely = -1;
            saveHeight = -1; // the "not found" value.

            voxel=((maxHeight*16+z)*16+x);
            r=gEmptyR;
            g=gEmptyG;
            b=gEmptyB;
            // if we start at the top of the world, seenempty is set to 1 (there's air above), else 0
            // The idea here is that if you're delving into cave areas, "hide obscured" will treat all
            // blocks at the topmost layer as empty, until a truly empty block is hit, at which point
            // the next solid block is then shown. If it's solid all the way down, the block will be
            // drawn as "empty"
            seenempty=(maxHeight==MAP_MAX_HEIGHT?1:0);
            alpha=0.0;
            // go from top down through all voxels, looking for the first one visible.
            for (i=maxHeight;i>=0;i--,voxel-=16*16)
            {
				type = block->grid[voxel];
                // if block is air or something very small, note it's empty and continue to next voxel
                if ( (type==BLOCK_AIR) ||
                    !(gBlockDefinitions[type].flags & viewFilterFlags ))
                {
                    seenempty=1;
                    continue;
                }

                // special selection height: we want to be able to select water
                float currentAlpha = gBlockDefinitions[type].alpha;
                blockSolid = (currentAlpha!=0.0); // ((type<NUM_BLOCKS_MAP) || (type ==255)) && 
                if ((showobscured || seenempty) && blockSolid)
                    if (prevSely==-1) 
                        prevSely=i;

                // non-flowing water does not count when finding the displayed height, so that we can reveal what is
                // underneath the water.
                if (type==BLOCK_STATIONARY_WATER)
                    seenempty=1;

                // if showobscured is on, or voxel is air or water (seenempty)
                // AND the voxel id is valid (in our array of known values)
                // AND it's not entirely transparent, then process it
                if ((showobscured || seenempty) && blockSolid)
                {
                    int light=12;
                    if (lighting)
                    {
                        if (i < MAP_MAX_HEIGHT)
                        {
                            light=block->light[voxel/2];
                            if (voxel&1) light>>=4;
                            light&=0xf;
                        } else
                        {
                            light = 0;
                        }
                    }
                    // if it's the first voxel visible (i.e., there was no block at all to the west), note this depth.
                    if (prevy==-1)
                         prevy=i;
                    else if (prevy<i)   // fully lit on west side of block?
                        light+=2;
                    else if (prevy>i)   // in shadow?
                        light-=5;
                    light=clamp(light,1,15);
                    // Here is where the color of the block is retrieved.
                    // First we check if there's a special color for this block,
                    // such as for wool, stained clay, carpet, etc. If not, then
                    // we can look the quick lookup value from the table.
                    color = checkSpecialBlockColor( block, voxel, type, light, useBiome, useElevation );

                    // is this the first block encountered?
                    if (alpha==0.0)
                    {
                        // yes; since there's no accumulated alpha, simply substitute the values into place;
                        // note that semi-transparent values already have their alpha multiplied in.
                        saveHeight = i;
                        alpha=currentAlpha;
                        r=(unsigned char)(color>>16);
                        g=(unsigned char)((color>>8)&0xff);
                        b=(unsigned char)(color&0xff);
                    }
                    else
                    {
                        // Else need to blend in this color with the previous.
                        // This is an "under" operation, putting the new color under the previous
                        // accumulated alpha
                        r+=(unsigned char)((1.0-alpha)*(color>>16));
                        g+=(unsigned char)((1.0-alpha)*((color>>8)&0xff));
                        b+=(unsigned char)((1.0-alpha)*(color&0xff));
                        alpha+=currentAlpha*(1.0-alpha);
                    }
                    // if the current block's color is fully opaque, finish.
                    if (currentAlpha==1.0f)
                        break;
                }
            }

            // The solid location (or none at all, in which case -1 is set) is saved here.
            // If everything is visible, then the height map will store the highest object found,
            // transparent or solid. This will tend to darken the water, since it will now "shadow"
            // itself. Note that prevy is used in the methods below, but for showAll it's good to have
            // this higher height for these purposes.
            prevy=showAll ? saveHeight:i;

			if (depthshading && prevy >= 0) // darken deeper blocks
            {
                // 50 kicks up the minimum darkness returned, so that it's not black.
                // Note that setting the upper height of the selection box affects this view.
                int num=prevy+50-(256-maxHeight)/5;
                int denom=maxHeight+50-(256-maxHeight)/5;

                r=(unsigned char)(r*num/denom);
                g=(unsigned char)(g*num/denom);
                b=(unsigned char)(b*num/denom);
            }

            //if (depthshading) // add contours on natural blocks
            //{
            //	if ( prevy % 5 == 0 )
            //	{
            //		alpha = 0.7;
            //		r=(unsigned char)(255.0 * alpha + r*(1.0 - alpha));
            //		g=(unsigned char)(255.0 * alpha + g*(1.0 - alpha));
            //		b=(unsigned char)(255.0 * alpha + b*(1.0 - alpha));
            //	}
            //}

            //if(hasSlime > 0){
            //    // before 0.9 Pre 5 it was 16, see http://www.minecraftwiki.net/wiki/Slime
            //    //if(maxHeight<=16){
            //    if(maxHeight<=40){
            //        g=clamp(g+20,0,MAP_MAX_HEIGHT);
            //    }else{
            //        if(x%15==0 || z%15==0){
            //            g=clamp(g+20,0,MAP_MAX_HEIGHT);
            //        }
            //    }
            //}

            if (cavemode && prevy >= 0)
            {
                seenempty=0;
                assert(voxel>=0);
                type=block->grid[voxel];

                if (type==BLOCK_LEAVES || type==BLOCK_LOG || type==BLOCK_AD_LEAVES || type==BLOCK_AD_LOG ) //special case surface trees
                    for (; i>=1; i--,voxel-=16*16,type=block->grid[voxel])
                        if (!(type==BLOCK_LOG||type==BLOCK_LEAVES||type==BLOCK_AD_LEAVES||type==BLOCK_AD_LOG||type==BLOCK_AIR))
                            break; // skip leaves, wood, air

                for (;i>=1;i--,voxel-=16*16)
                {
                    type=block->grid[voxel];
                    if (type==BLOCK_AIR)
                    {
                        seenempty=1;
                        continue;
                    }
                    if (seenempty && gBlockDefinitions[type].alpha!=0.0) // ((type<NUM_BLOCKS_MAP) || (type ==255)) &&
                    {
                        r=(unsigned char)(r*(prevy-i+10)/138);
                        g=(unsigned char)(g*(prevy-i+10)/138);
                        b=(unsigned char)(b*(prevy-i+10)/138); 
                        break;
                    }
                }
            }

            if ( gBoxHighlightUsed ) {
                // make selected area slightly red, if at right heightmap range
                if ( bx*16 + x >= gBoxMinX && bx*16 + x <= gBoxMaxX &&
                    bz*16 + z >= gBoxMinZ && bz*16 + z <= gBoxMaxZ )
                {
                    // test and save minimum height found
                    if ( prevSely >= 0 && prevSely < hitsFound[3] )
                    {
                        hitsFound[3] = prevSely;
                    }

                    // in bounds, is the height good?
					// First case is for if we hit nothing, all void, so it's black:
					// always highlight that area, just for readability.
                    if ( (prevSely == -1) || (prevSely >= gBoxMinY && prevSely <= gBoxMaxY))
                    {
                        hitsFound[1] = 1;
                        // blend in highlight color
                        blend = gHalpha;
                        // are we on a border? If so, change blend factor
                        if ( prevSely == gBoxMinY || prevSely == gBoxMaxY ||
                            bx*16 + x == gBoxMinX || bx*16 + x == gBoxMaxX ||
                            bz*16 + z == gBoxMinZ || bz*16 + z == gBoxMaxZ )
                        {
                            blend = gHalphaBorder;
                        }
                        r = (unsigned char)((double)r*(1.0-blend) + blend*(double)gHred);
                        g = (unsigned char)((double)g*(1.0-blend) + blend*(double)gHgreen);
                        b = (unsigned char)((double)b*(1.0-blend) + blend*(double)gHblue);
                    }
                    else if ( prevSely < gBoxMinY )
                    {
                        hitsFound[0] = 1;
                        // lower than selection box, so if exactly on border, dim
                        if ( bx*16 + x == gBoxMinX || bx*16 + x == gBoxMaxX ||
                            bz*16 + z == gBoxMinZ || bz*16 + z == gBoxMaxZ )
                        {
							double dim = 0.5;
                            r = (unsigned char)((double)r*dim);
                            g = (unsigned char)((double)g*dim);
                            b = (unsigned char)((double)b*dim);
                        }
                    }
                    else
                    {
                        hitsFound[2] = 1;
                        // higher than selection box, so if exactly on border, brighten
                        // - I don't think it's actually possible to hit this condition,
                        // as the area above the selection box should never be seen (the
                        // slider sets the maximum), but just in case things change...
                        if ( bx*16 + x == gBoxMinX || bx*16 + x == gBoxMaxX ||
                            bz*16 + z == gBoxMinZ || bz*16 + z == gBoxMaxZ )
                        {
							double brighten = 0.5;
                            r = (unsigned char)((double)r*(1.0-brighten) + brighten);
                            g = (unsigned char)((double)g*(1.0-brighten) + brighten);
                            b = (unsigned char)((double)b*(1.0-brighten) + brighten);
                        }
                    }
                }
			}
			
			if ((pWorldGuide->type == WORLD_SCHEMATIC_TYPE) && (prevy == -1)) {
				// empty, not highlighted, so make it background color if we're drawing a schematic
				unsigned char *clr = &gBlankTile[(x + z * 16)*4];
				r = *clr++;
				g = *clr++;
				b = *clr; // ++ if you add alpha
			}

            bits[ofs++]=r;
            bits[ofs++]=g;
            bits[ofs++]=b;
            bits[ofs++]=0xff;

            // heightmap determines what value is displayed on status and for shadowing. If "show all" is on,
            // save any semi-visible thing, else save the first solid thing (or possibly nothing == -1).
            block->heightmap[x+z*16] = (unsigned char)prevy;
        }
    }
    return bits;
}

#define BLOCK_INDEX(x,topy,z) (  ((topy)*256)+ \
    ((z)*16) + \
    (x)  )

///////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Generate test blocks for test world.
// testBlock checks to see whether this block and data value exists and should be output,
// and whether any neighboring blocks should be output for testing. Sometimes the data value is
// used as a guide for where to put neighbors instead of as a data value, so that testing is more thorough.
void testBlock( WorldBlock *block, int type, int y, int dataVal )
{
    int bi = 0;
    int trimVal;
    int addBlock = 0;

    switch ( type )
    {
    default:
        if ( dataVal == 0 )
        {
            //block->grid[BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8)] = (unsigned char)type;
            addBlock = 1;
        }
        break;
    case BLOCK_SAND:
    case BLOCK_WOODEN_PRESSURE_PLATE:
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY:
    case BLOCK_AD_LEAVES:
    case BLOCK_SPONGE:
        // uses 0-1
        if ( dataVal < 2 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_DIRT:
    case BLOCK_TALL_GRASS:
    case BLOCK_SANDSTONE:
    case BLOCK_RED_SANDSTONE:
    case BLOCK_PRISMARINE:
        // uses 0-2
        if ( dataVal < 3 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_LEAVES:
    case BLOCK_NETHER_WART:
    case BLOCK_STONE_BRICKS:
    case BLOCK_CAULDRON:
    case BLOCK_FROSTED_ICE:
    case BLOCK_STRUCTURE_BLOCK:
        // uses 0-3
        if ( dataVal < 4 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_HEAD:
		// uses 2-5
		if (dataVal >= 2 && dataVal <= 5)
		{
			addBlock = 1;
		}
		break;
	case BLOCK_PUMPKIN:
	case BLOCK_JACK_O_LANTERN:
	case BLOCK_QUARTZ_BLOCK:
        // uses 0-4
        if ( dataVal < 5 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_WOODEN_PLANKS:
    case BLOCK_WOODEN_DOUBLE_SLAB:
    case BLOCK_SAPLING:
    case BLOCK_CAKE:
	case BLOCK_MONSTER_EGG:
	case BLOCK_END_ROD:
    case BLOCK_CHORUS_FLOWER:
	case BLOCK_OBSERVER:	// could also have top bit "fired", but no graphical effect
		// uses 0-5
        if ( dataVal < 6 )
        {
            addBlock = 1;
            // for just chorus flower, put endstone below
            if ( type == BLOCK_CHORUS_FLOWER )
            {
                block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_END_STONE;
            }
        }
        break;
    case BLOCK_DOUBLE_FLOWER:
        // uses 0-5, put flower head above it
        if ( dataVal < 6 )
        {
            addBlock = 1;
            // add flower above
            bi = BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8);
            block->grid[bi] = BLOCK_DOUBLE_FLOWER;
            // shift up the data val by 4 if on the odd value location
            // not entirely sure about this number, but 10 seems to be the norm
            block->data[(int)(bi/2)] |= (unsigned char)(10<<((bi%2)*4));
        }
        break;
    case BLOCK_STONE:
        // uses 0-6
        if ( dataVal < 7 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
    case BLOCK_OAK_WOOD_STAIRS:
    case BLOCK_COBBLESTONE_STAIRS:
    case BLOCK_BRICK_STAIRS:
    case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_NETHER_BRICK_STAIRS:
    case BLOCK_SANDSTONE_STAIRS:
    case BLOCK_SPRUCE_WOOD_STAIRS:
    case BLOCK_BIRCH_WOOD_STAIRS:
    case BLOCK_JUNGLE_WOOD_STAIRS:
    case BLOCK_QUARTZ_STAIRS:
    case BLOCK_SNOW:
	case BLOCK_END_PORTAL_FRAME:
    case BLOCK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_FARMLAND:
    case BLOCK_BREWING_STAND:
    case BLOCK_ACACIA_WOOD_STAIRS:
    case BLOCK_DARK_OAK_WOOD_STAIRS:
    case BLOCK_RED_SANDSTONE_STAIRS:
    case BLOCK_PURPUR_STAIRS:
        // uses 0-7 - we could someday add more blocks to neighbor the others, in order to show the "step block trim" feature of week 39
        if ( dataVal < 8 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_CROPS:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES:
        // uses 0-7, put farmland beneath it
        if ( dataVal < 8 )
        {
            addBlock = 1;
            // add farmland underneath
            block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_FARMLAND;
        }
        break;
    case BLOCK_BEETROOT_SEEDS:
        // uses 0-3, put farmland beneath it
        if ( dataVal < 4 )
        {
            addBlock = 1;
            // add farmland underneath
            block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_FARMLAND;
        }
        break;
    case BLOCK_POPPY:
        // uses 0-8
        if ( dataVal < 9 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_DOUBLE_STONE_SLAB:
        // uses 0-9, F (15)
        if ( dataVal < 10 || dataVal == 15 )
        {
            addBlock = 1;
        }
        break;

    case BLOCK_DOUBLE_RED_SANDSTONE_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
    case BLOCK_PURPUR_SLAB:
        // uses 0 and 8 - normal and smooth, for redstone double slab; bottom & top for slabs
        if ( dataVal == 0 || dataVal == 8 )
        {
            addBlock = 1;
        }
        break;

    case BLOCK_HUGE_BROWN_MUSHROOM:
    case BLOCK_HUGE_RED_MUSHROOM:
        // uses 0-10
        if ( dataVal < 11 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_FLOWER_POT:
		// uses 0-13 for old-style 1.7 flower pots
		if (dataVal < 14)
		{
			addBlock = 1;
		}
		break;
	case BLOCK_ANVIL:
        // uses 0-11
        if ( dataVal < 12 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_AD_LOG:
        // uses 0 and 1 for low bits
        if ( (dataVal & 0x3) < 2 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_LOG:	// really just 12, but we pay attention to directionless
    case BLOCK_STONE_SLAB:
    case BLOCK_SIGN_POST:
    case BLOCK_REDSTONE_REPEATER_OFF:
    case BLOCK_REDSTONE_REPEATER_ON:
    case BLOCK_REDSTONE_COMPARATOR:
    case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
    case BLOCK_STAINED_CLAY:
    case BLOCK_CARPET:
    case BLOCK_STAINED_GLASS:
    case BLOCK_STANDING_BANNER:
    case BLOCK_WOOL:
	case BLOCK_SHULKER_CHEST:
	case BLOCK_SHULKER_CHEST + 1:
	case BLOCK_SHULKER_CHEST + 2:
	case BLOCK_SHULKER_CHEST + 3:
	case BLOCK_SHULKER_CHEST + 4:
	case BLOCK_SHULKER_CHEST + 5:
	case BLOCK_SHULKER_CHEST + 6:
	case BLOCK_SHULKER_CHEST + 7:
	case BLOCK_SHULKER_CHEST + 8:
	case BLOCK_SHULKER_CHEST + 9:
	case BLOCK_SHULKER_CHEST + 10:
	case BLOCK_SHULKER_CHEST + 11:
	case BLOCK_SHULKER_CHEST + 12:
	case BLOCK_SHULKER_CHEST + 13:
	case BLOCK_SHULKER_CHEST + 14:
	case BLOCK_SHULKER_CHEST + 15:
		// uses all bits, 0-15
        addBlock = 1;
        break;
    case BLOCK_WATER:
    case BLOCK_STATIONARY_WATER:
    case BLOCK_LAVA:
    case BLOCK_STATIONARY_LAVA:
        // uses 0-8, with 8 giving one above
        if ( dataVal <= 8 )
        {
            addBlock = 1;

            if ( dataVal == 8 )
            {
                block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = (unsigned char)type;
            }
            else if ( dataVal > 0 )
            {
                int x = type % 2;
                int z = !x;
                block->grid[BLOCK_INDEX(x+4+(type%2)*8,y,z+4+(dataVal%2)*8)] = (unsigned char)type;
            }
        }
        break;
    case BLOCK_FURNACE:
    case BLOCK_BURNING_FURNACE:
    case BLOCK_ENDER_CHEST:
        // uses 2-5
        if ( dataVal >= 2 && dataVal <= 5 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_DISPENSER:
    case BLOCK_DROPPER:
        if ( dataVal <= 5 )
        {
            addBlock = 1;
            switch ( dataVal )
            {
            case 0:
            case 1:
                // make the block itself be up by two, so we can examine its top and bottom
                bi = BLOCK_INDEX(4+(type%2)*8,y+2,4+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
                addBlock = 0;
                break;
            }
        }
        break;

    case BLOCK_HOPPER:
        // uses 0,1, 2-5
        if ( dataVal <= 5 )
        {
            addBlock = 1;
            switch ( dataVal )
            {
            case 0:
                // make the block itself be up by two, so we can examine its top and bottom
                bi = BLOCK_INDEX(4+(type%2)*8,y+2,4+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
                addBlock = 0;
                break;
            case 1:
                // put block above
                block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 2:
                // put block to north
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to south
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to west
                block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to east
                block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            }
        }
        break;

    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        if ( dataVal >= 1 && dataVal <= 5 )
        {
            addBlock = 1;
            switch ( dataVal )
            {
            case 1:
                // put block to west
                block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to south
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            default:
                // do nothing - on ground
                break;
            }
        }
        break;
    case BLOCK_LADDER:
    case BLOCK_WALL_SIGN:
    case BLOCK_WALL_BANNER:
        if ( dataVal >= 2 && dataVal <= 5 )
        {
            addBlock = 1;
            switch ( dataVal )
            {
            case 2:
                // put block to south
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to east
                block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to west
                block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            }
        }
        break;
    case BLOCK_RAIL:
        if ( dataVal >= 6 && dataVal <= 9 )
        {
            addBlock = 1;
            break;
        }
        // test if too high - if so, ignore
        else if ( dataVal > 9 )
        {
            break;
        } // else:
        // falls through on 0 through 5, since these are handled below for all rails
    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        trimVal = dataVal & 0x7;
        if ( trimVal <= 5 )
        {
            addBlock = 1;
            switch ( trimVal )
            {
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 3:
                // put block to west
                block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 4:
                // put block to north
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            case 5:
                // put block to south
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
                break;
            default:
                // do nothing - on ground
                break;
            }
        }
        break;
    case BLOCK_LEVER:
        trimVal = dataVal & 0x7;
        addBlock = 1;
        switch ( dataVal & 0x7 )
        {
        case 1:
            // put block to west
            block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 2:
            // put block to east
            block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 3:
            // put block to north
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 4:
            // put block to south
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 7:
        case 0:
            // put block above
            block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        default:
            // do nothing - on ground
            break;
        }
        break;
    case BLOCK_WOODEN_DOOR:
    case BLOCK_IRON_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_ACACIA_DOOR:
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;
        block->data[(int)(bi/2)] = (unsigned char)((dataVal&0x7)<<((bi%2)*4));
        if ( dataVal < 8 )
        {
            bi = BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            block->data[(int)(bi/2)] = (unsigned char)(8<<((bi%2)*4));
        }
        else
        {
            // other direction door (for double doors)
            bi = BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            block->data[(int)(bi/2)] = (unsigned char)(9<<((bi%2)*4));
        }
        break;
    case BLOCK_BED:
        if ( dataVal < 8 )
        {
            addBlock = 1;
            switch ( dataVal & 0x3 )
            {
            case 0:
                // put head to south
                bi = BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)((dataVal|0x8)<<((bi%2)*4));
                break;
            case 1:
                // put head to west
                bi = BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)((dataVal|0x8)<<((bi%2)*4));
                break;
            case 2:
                // put head to north
                bi = BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)((dataVal|0x8)<<((bi%2)*4));
                break;
            case 3:
                // put head to east
                bi = BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8);
                block->grid[bi] = (unsigned char)type;
                // shift up the data val by 4 if on the odd value location
                block->data[(int)(bi/2)] |= (unsigned char)((dataVal|0x8)<<((bi%2)*4));
                break;
            }
        }
        break;
    case BLOCK_STONE_BUTTON:
    case BLOCK_WOODEN_BUTTON:
        trimVal = dataVal & 0x7;
        if ( trimVal <= 5 )
        {
            addBlock = 1;
            switch ( trimVal )
            {
            case 0:
                // put block above
                block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
                break;
            case 1:
                // put block to west
                block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
                break;
            case 2:
                // put block to east
                block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
                break;
            case 3:
                // put block to north
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
                break;
            case 4:
                // put block to south
                block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
                break;
            case 5:
                // put block below
                block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_OBSIDIAN;
            }
        }
        break;
    case BLOCK_WOODEN_SLAB:
        trimVal = dataVal & 0x7;
        if ( trimVal < 6 )
        {
            addBlock = 1;
        }
        break;
    case BLOCK_TRAPDOOR:
    case BLOCK_IRON_TRAPDOOR:
        addBlock = 1;

        trimVal = dataVal & 0x3;
        switch ( trimVal )
        {
        case 3:
            // put block to west
            block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 2:
            // put block to east
            block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 1:
            // put block to north
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        case 0:
            // put block to south
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
            break;
        }
        break;
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
        trimVal = dataVal & 0x7;
        if ( trimVal < 6 )
        {
            int bx = 0;
            int by = 0;
            int bz = 0;
            addBlock = 1;

            // is piston extended?
            if ( dataVal & 0x8 )
            {
                switch ( trimVal )
                {
                case 0: // pointing down
                    bx = 4+(type%2)*8;
                    by = y;
                    bz = 4+(dataVal%2)*8;
                    // increase y by 1 so piston is one block higher
                    y++;
                    break;
                case 1: // pointing up
                    bx = 4+(type%2)*8;
                    by = y+1;
                    bz = 4+(dataVal%2)*8;
                    break;
                case 2: // pointing north
                    bx = 4+(type%2)*8;
                    by = y;
                    bz = 3+(dataVal%2)*8;
                    break;
                case 3: // pointing south
                    bx = 4+(type%2)*8;
                    by = y;
                    bz = 5+(dataVal%2)*8;
                    break;
                case 4: // pointing west
                    bx = 3+(type%2)*8;
                    by = y;
                    bz = 4+(dataVal%2)*8;
                    break;
                case 5: // pointing east
                    bx = 5+(type%2)*8;
                    by = y;
                    bz = 4+(dataVal%2)*8;
                    break;
                default:
                    assert(0);
                    break;
                }
                bi = BLOCK_INDEX(bx,by,bz);
                block->grid[bi] = BLOCK_PISTON_HEAD;
                // sticky or not, plus direction
                block->data[(int)(bi/2)] |= (unsigned char)((trimVal | ((type == BLOCK_STICKY_PISTON) ? 0x8 : 0x0))<<((bi%2)*4));
            }
        }
        break;
    case BLOCK_PISTON_HEAD:
        // uses bits 0-5 and 8-13
        if ( (dataVal&0x7) < 6 )
        {
            if ( (dataVal&0x7) != 1 )
            {
                // add glass so that when 3D printing it's not deleted;
                // it will be deleted when pointing up.
                bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
                block->grid[bi] = BLOCK_GLASS_PANE;
            }
            // make it float above ground, to avoid asserts and to test.
            y++;
            addBlock=1;
        }
        break;
    case BLOCK_VINES:
        // uses all bits, 0-15
        // TODO: really should place vines on the sides and under stuff, but this is a pain
        if ( dataVal > 0 )
        {
            addBlock=1;
        }
        bi = BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;
        block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));

        block->grid[BLOCK_INDEX(4+(type%2)*8,y+2,4+(dataVal%2)*8)] = BLOCK_STONE;
        break;
    case BLOCK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_CHORUS_PLANT:
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;

        // put block above, too, for every fifth one, just to see it's working
        if ( (dataVal % 5) == 4 )
            block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = (unsigned char)type;

        // for just chorus plant, put endstone below
        if ( type == BLOCK_CHORUS_PLANT )
        {
            block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_END_STONE;
            // half the time put chorus flower above, instead
            if ( dataVal & 0x1 ) {
                block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = BLOCK_CHORUS_FLOWER;
            }
        }

        if ( dataVal & 0x1 )
        {
            // put block to north
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x2 )
        {
            // put block to east
            block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x4 )
        {
            // put block to south
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x8 )
        {
            // put block to west
            block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = (unsigned char)type;
        }
        break;
    case BLOCK_STAINED_GLASS_PANE:	// color AND neighbors!
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        // *and* what color to use
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;
        block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));

        if ( dataVal & 0x1 )
        {
            // alternate between wall and mossy wall - we set mossy wall if odd
            block->data[(int)(bi/2)] |= (unsigned char)(0x1<<((bi%2)*4));

            // put block to north
            bi = BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        }
        if ( dataVal & 0x2 )
        {
            // put block to east
            bi = BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        }
        if ( dataVal & 0x4 )
        {
            // put block to south
            bi = BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        }
        if ( dataVal & 0x8 )
        {
            // put block to west
            bi = BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        }
        break;
    case BLOCK_COBBLESTONE_WALL:
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;

        // put block above, too, for every seventh one, just to see it's working
        if ( (dataVal % 7) == 5 )
            block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,4+(dataVal%2)*8)] = (unsigned char)type;

        if ( dataVal & 0x1 )
        {
            // alternate between wall and mossy wall - we set mossy wall if odd
            block->data[(int)(bi/2)] |= (unsigned char)(0x1<<((bi%2)*4));

            // put block to north
            bi = BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)((dataVal%2)<<((bi%2)*4));
        }
        if ( dataVal & 0x2 )
        {
            // put block to east
            bi = BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)((dataVal%2)<<((bi%2)*4));
        }
        if ( dataVal & 0x4 )
        {
            // put block to south
            bi = BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)((dataVal%2)<<((bi%2)*4));
        }
        if ( dataVal & 0x8 )
        {
            // put block to west
            bi = BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // alternate between wall and mossy wall
            block->data[(int)(bi/2)] |= (unsigned char)((dataVal%2)<<((bi%2)*4));
        }
        break;
    case BLOCK_REDSTONE_WIRE:
        // this one is specialized: dataVal just says where to put neighbors, NSEW
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;

        if ( dataVal & 0x1 )
        {
            // put block to north
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,3+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x2 )
        {
            // put block to east
            block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(5+(type%2)*8,y+1,4+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x4 )
        {
            // put block to south
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(4+(type%2)*8,y+1,5+(dataVal%2)*8)] = (unsigned char)type;
        }
        if ( dataVal & 0x8 )
        {
            // put block to west, redstone atop it
            block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_STONE;
            block->grid[BLOCK_INDEX(3+(type%2)*8,y+1,4+(dataVal%2)*8)] = (unsigned char)type;
        }
        break;
    case BLOCK_CACTUS:
        // put on sand
        if ( dataVal == 0 )
        {
            addBlock = 1;
            // put sand below
            block->grid[BLOCK_INDEX(4+(type%2)*8,y-1,4+(dataVal%2)*8)] = BLOCK_SAND;
        }
        break;
    case BLOCK_CHEST:
    case BLOCK_TRAPPED_CHEST:
        // uses 2-5, we add an extra chest on 0x8
        trimVal = dataVal & 0x7;
        if ( trimVal >= 2 && trimVal <= 5 )
        {
            // Note that we use trimVal here, different than the norm
            bi = BLOCK_INDEX(4+(type%2)*8,y,4+(trimVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // shift up the data val by 4 if on the odd value location
            block->data[(int)(bi/2)] |= (unsigned char)(trimVal<<((bi%2)*4));
        }
        // double-chest on 0x8 (for mapping - in Minecraft chests have just 2,3,4,5)
        // - locked chests (April Fool's joke) don't really have doubles, but whatever
        switch ( dataVal )
        {
        case 0x8|2:
        case 0x8|3:
            // north/south, so put one to west (-1 X)
            bi = BLOCK_INDEX(3+(type%2)*8,y,4+(trimVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // shift up the data val by 4 if on the odd value location
            block->data[(int)(bi/2)] |= (unsigned char)(trimVal<<((bi%2)*4));
            break;
        case 0x8|4:
        case 0x8|5:
            // west/east, so put one to north (-1 Z)
            bi = BLOCK_INDEX(4+(type%2)*8,y,3+(trimVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // shift up the data val by 4 if on the odd value location
            block->data[(int)(bi/2)] |= (unsigned char)(trimVal<<((bi%2)*4));
            break;
        default:
            break;
        }
        break;
    case BLOCK_LILY_PAD:
        if ( dataVal == 0 )
        {
            int wrow, wcol;
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8)] = (unsigned char)type;
            for ( wrow = 3; wrow <= 5; wrow++ )
                for ( wcol = 3; wcol <= 5; wcol++ )
                    block->grid[BLOCK_INDEX(wrow+(type%2)*8,y-1,wcol+(dataVal%2)*8)] = BLOCK_STATIONARY_WATER;
        }
        break;
    case BLOCK_COCOA_PLANT:
        if ( dataVal < 12 )
        {
            addBlock = 1;
            switch ( dataVal & 0x3 )
            {
            case 0:
                // put block to south
                bi = BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8);
                break;
            case 1:
                // put block to west
                bi = BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8);
                break;
            case 2:
                // put block to north
                bi = BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8);
                break;
            case 3:
                // put block to east
                bi = BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8);
                break;
            }
            block->grid[bi] = BLOCK_LOG;
            block->data[(int)(bi/2)] |= 3<<((bi%2)*4);	// jungle
        }
        break;
    case BLOCK_TRIPWIRE_HOOK:
        addBlock = 1;
        switch ( dataVal & 0x3 )
        {
        case 0:
            // put block to north
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,3+(dataVal%2)*8)] = BLOCK_WOODEN_PLANKS;
            break;
        case 1:
            // put block to east
            block->grid[BLOCK_INDEX(5+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_WOODEN_PLANKS;
            break;
        case 2:
            // put block to south
            block->grid[BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8)] = BLOCK_WOODEN_PLANKS;
            break;
        case 3:
            // put block to west
            block->grid[BLOCK_INDEX(3+(type%2)*8,y,4+(dataVal%2)*8)] = BLOCK_WOODEN_PLANKS;
            break;
        }
        break;

    case BLOCK_HAY:
    case BLOCK_PURPUR_PILLAR:
	case BLOCK_BONE_BLOCK:
        // uses 0,4,8
        if ( (dataVal == 0) || (dataVal == 4) || (dataVal == 8) ) {
            addBlock = 1;
        }

    }

    // if we want to do a normal sort of thing
    if ( addBlock )
    {
        bi = BLOCK_INDEX(4+(type%2)*8,y,4+(dataVal%2)*8);
        block->grid[bi] = (unsigned char)type;
        // shift up the data val by 4 if on the odd value location
        block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        static bool extraBlock = false;
        if ( extraBlock )
        {
            // optional: put neighbor to south, so we can test for what happens at borders when splitting occurs;
            // note: this will generate two assertions with pistons. Ignore them.
            bi = BLOCK_INDEX(4+(type%2)*8,y,5+(dataVal%2)*8);
            block->grid[bi] = (unsigned char)type;
            // shift up the data val by 4 if on the odd value location
            block->data[(int)(bi/2)] |= (unsigned char)(dataVal<<((bi%2)*4));
        }
    }
}

void testNumeral( WorldBlock *block, int type, int y, int digitPlace, int outType )
{
    int i;
    int shiftedNumeral = type;
    int numeral;

    i = digitPlace;
    while ( i > 0 )
    {
        shiftedNumeral /= 10;
        i--;
    }
    numeral = shiftedNumeral % 10;
    if ( (type < NUM_BLOCKS_DEFINED) && shiftedNumeral > 0 )
    {
        int dots[50][2];
        int doti = 0;
        switch ( numeral )
        {
        default:
        case 0:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 1:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 1;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 4;
            dots[doti][0] = 2; dots[doti++][1] = 4;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 2:
            dots[doti][0] = 0; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 1;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 3:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 4:
            dots[doti][0] = 3; dots[doti++][1] = 0;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 2;
            dots[doti][0] = 4; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 4;
            break;
        case 5:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 6:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 0; dots[doti++][1] = 3;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 7:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 1; dots[doti++][1] = 1;
            dots[doti][0] = 1; dots[doti++][1] = 2;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 0; dots[doti++][1] = 5;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            dots[doti][0] = 3; dots[doti++][1] = 5;
            break;
        case 8:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 0; dots[doti++][1] = 2;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        case 9:
            dots[doti][0] = 1; dots[doti++][1] = 0;
            dots[doti][0] = 2; dots[doti++][1] = 0;
            dots[doti][0] = 0; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 1;
            dots[doti][0] = 3; dots[doti++][1] = 2;
            dots[doti][0] = 1; dots[doti++][1] = 3;
            dots[doti][0] = 2; dots[doti++][1] = 3;
            dots[doti][0] = 3; dots[doti++][1] = 3;
            dots[doti][0] = 0; dots[doti++][1] = 4;
            dots[doti][0] = 3; dots[doti++][1] = 4;
            dots[doti][0] = 1; dots[doti++][1] = 5;
            dots[doti][0] = 2; dots[doti++][1] = 5;
            break;
        }
        for ( i = 0; i < doti; i++ )
        {
            block->grid[BLOCK_INDEX(2+dots[i][0]+(type%2)*8,y-1,6-dots[i][1]+((digitPlace+1)%2)*8)] = (unsigned char)outType;
        }
    }
}

// return NULL if no block loaded.
WorldBlock *LoadBlock(WorldGuide *pWorldGuide, int cx, int cz)
{
	// if there's no world, simply return
	if (pWorldGuide->type == WORLD_UNLOADED_TYPE)
		return NULL;

	WorldBlock *block = block_alloc();

    // out of memory? If so, clear cache and cross fingers
    if ( block == NULL )
    {
        Cache_Empty();
        block=block_alloc();
    }
	// always set
	block->rendery = -1; // force redraw

	if ( pWorldGuide->type == WORLD_TEST_BLOCK_TYPE )
    {
        int type = cx*2;
        // if directory starts with /, this is [Block Test World], a synthetic test world
        // made by the testBlock() method.
        int x, y, z;
        int bedrockHeight = 60;
        int grassHeight = 62;
        int blockHeight = 63;

        memset(block->grid, 0, 16*16*256);
        memset(block->data, 0, 16*16*128);
        memset(block->biome, 1, 16*16);
        memset(block->light, 0xff, 16*16*128);
        block->renderhilitID = 0;

        if ( type >= 0 && type < NUM_BLOCKS_DEFINED && cz >= 0 && cz < 8)
        {
            // grass base
            for ( x = 0; x < 16; x++ )
            {
                for ( z = 0; z < 16; z++ )
                {
                    // make the grass two blocks thick, and then "impenetrable" below, for test border code
                    block->grid[BLOCK_INDEX(x,bedrockHeight,z)] = BLOCK_BEDROCK;
                    for ( y = bedrockHeight+1; y < grassHeight; y++ )
                        block->grid[BLOCK_INDEX(x,y,z)] = BLOCK_DIRT;
                    block->grid[BLOCK_INDEX(x,grassHeight,z)] = BLOCK_GRASS;
                }
            }

            // blocks: each 16x16 area has 4 blocks, every 8 spaces, so we call this to get 4 blocks.
            testBlock(block,type,blockHeight,cz*2);
            testBlock(block,type,blockHeight,cz*2+1);
            if ( type+1 < NUM_BLOCKS_DEFINED )
            {
                testBlock(block,type+1,blockHeight,cz*2);
                testBlock(block,type+1,blockHeight,cz*2+1);
            }
            return block;
        }
        // tick marks
        else if ( type >= 0 && type < NUM_BLOCKS_DEFINED && (cz == -1 || cz == 8) )
        {
            int i, j;

            // stone edge
            for ( x = 0; x < 16; x++ )
            {
                for ( z = 0; z < 16; z++ )
                {
                    block->grid[BLOCK_INDEX(x,grassHeight,z)] = (cz > 0 ) ? (unsigned char)BLOCK_WOODEN_PLANKS : (unsigned char)BLOCK_STONE;
                }
            }

            // blocks
            for ( i = 0; i < 2; i++ )
            {
                if ( ((type+i) % 10) == 0 )
                {
                    if ( type+i < NUM_BLOCKS_DEFINED )
                    {
                        for ( j = 0; j <= (int)(cx/8); j++ )
                            block->grid[BLOCK_INDEX(4+(i%2)*8,grassHeight,j)] = (((type+i)%50) == 0) ? (unsigned char)BLOCK_WATER : (unsigned char)BLOCK_LAVA;
                    }
                }
            }
            return block;
        }
        // numbers (yes, I'm insane)
        else if ( type >= 0 && type < NUM_BLOCKS_DEFINED && (cz <= -2 && cz >= -3) )
        {
            int letterType = BLOCK_OBSIDIAN;
            if ( (type >= NUM_BLOCKS_STANDARD) && (type != BLOCK_STRUCTURE_BLOCK) )
            {
                // for unknown block, put a different font
                letterType = BLOCK_LAVA;
            }

            // white wool
            for ( x = 0; x < 16; x++ )
            {
                for ( z = 0; z < 16; z++ )
                {
                    block->grid[BLOCK_INDEX(x,grassHeight,z)] = BLOCK_WOOL;
                }
            }
            // blocks
            testNumeral(block,type,blockHeight,-cz*2-3, letterType);
            testNumeral(block,type,blockHeight,-cz*2-1-3, letterType);

            // second number on 16x16 tile
            letterType = BLOCK_OBSIDIAN;
            if ( type+1 < NUM_BLOCKS_DEFINED )
            {
                if ( (type+1 >= NUM_BLOCKS_STANDARD) && (type+1 != BLOCK_STRUCTURE_BLOCK) )
                {
                    letterType = BLOCK_LAVA;
                }
                testNumeral(block,type+1,blockHeight,-cz*2-3, letterType);
                testNumeral(block,type+1,blockHeight,-cz*2-1-3, letterType);
            }
            return block;
        }
        else
        {
            block_free(block);
            return NULL;
        }
	}
	else {
		// it's a real world or schematic or no world is loaded
		int gotBlock = 0;

		if (pWorldGuide->type == WORLD_LEVEL_TYPE) {
			BlockEntity blockEntities[16 * 16 * 256];

			gotBlock = regionGetBlocks(pWorldGuide->directory, cx, cz, block->grid, block->data, block->light, block->biome, blockEntities, &block->numEntities);
			// got block successfully?

			if (gotBlock && block->numEntities > 0) {
				// transfer the relevant part of the BlockEntity array to permanent block storage
				block->entities = (BlockEntity *)malloc(block->numEntities*sizeof(BlockEntity));

				if (block->entities)
					memcpy(block->entities, blockEntities, block->numEntities*sizeof(BlockEntity));
				else
					// couldn't alloc data
					return NULL;
			}
		}
		else {
			assert(pWorldGuide->type == WORLD_SCHEMATIC_TYPE);
			gotBlock = createBlockFromSchematic(pWorldGuide, cx, cz, block);
		}

		if ( gotBlock ) {

			int i;
			unsigned char *pBlockID = block->grid;
			for (i = 0; i < 16 * 16 * 256; i++, pBlockID++)
			{
				if ((*pBlockID >= NUM_BLOCKS_STANDARD) && (*pBlockID != BLOCK_STRUCTURE_BLOCK))
				{
					// some new version of Minecraft, block ID is unrecognized;
					// turn this block into stone. dataVal will be ignored.
					// flag assert only once
					assert((gUnknownBlock == 1) || (gPerformUnknownBlockCheck == 0));	// note the program needs fixing
					*pBlockID = BLOCK_UNKNOWN;
					// note that we always clean up bad blocks;
					// whether we flag that a bad block was found is optional.
					// This gets turned off once the user has been warned, once, that his map has some funky data.
					if (gPerformUnknownBlockCheck)
						gUnknownBlock = 1;
				}
			}
			return block;
		}
	}

    block_free(block);
    return NULL;
}

// cx and cz are the chunk location - multiply by 16 to get the starting world location
// return 1 if real data is found, 0 if all empty
int createBlockFromSchematic(WorldGuide *pWorldGuide, int cx, int cz, WorldBlock *block)
{
	if (!pWorldGuide->sch.repeat) {
		// does block overlap the schematic?
		if ((cx * 16 > pWorldGuide->sch.width) || (cx < 0))
			return 0;
		if ((cz * 16 > pWorldGuide->sch.length) || (cz < 0))
			return 0;
	}

	// no biome, so that's easy
	memset(block->biome, 0, 16 * 16);
	// no light, so that's also easy
	memset(block->light, 0, 16 * 16 * 128);

	// clear the rest, so we fill these in as found
	memset(block->grid, 0, 16 * 16 * 256);
	memset(block->data, 0, 16 * 16 * 128);

	static int border = 1;
	if (pWorldGuide->sch.repeat) {
		// loop through local block locations, 0-15,0-15
		for (int y = 0; y < pWorldGuide->sch.height; y++) {
			int zWorld = cz * 16;
			for (int z = 0; z < 16; z++, zWorld++) {
				int zMod = zWorld % (pWorldGuide->sch.length + border);
				zMod = (zMod + pWorldGuide->sch.length + border) % (pWorldGuide->sch.length + border);
				// leave a border
				if (zMod < pWorldGuide->sch.length) {
					int index = (y * 16 + z) * 16;
					int xWorld = cx * 16;
					for (int x = 0; x < 16; x++, index++, xWorld++) {
						int xMod = xWorld % (pWorldGuide->sch.width + border);
						xMod = (xMod + pWorldGuide->sch.width + border) % (pWorldGuide->sch.width + border);
						if (xMod < pWorldGuide->sch.width) {
							int schIndex = (y * pWorldGuide->sch.length + zMod) * pWorldGuide->sch.width + xMod;
							assert(schIndex >= 0 && schIndex < pWorldGuide->sch.numBlocks);
							block->grid[index] = pWorldGuide->sch.blocks[schIndex];
							block->data[index / 2] |= pWorldGuide->sch.data[schIndex] << ((index % 2) * 4);
						}
					}
				}
			}
		}
	}
	else {
		// find the valid data's bounds inside the 16x16 area
		int xlength = (cx * 16 + 16 > pWorldGuide->sch.width) ? pWorldGuide->sch.width - cx * 16 : 16;
		int ylength = pWorldGuide->sch.height;
		int zlength = (cz * 16 + 16 > pWorldGuide->sch.length) ? pWorldGuide->sch.length - cz * 16 : 16;
		// the offset is how many 16x16 tiles into the schematic data itself we need to offset
		int offset = 16 * cz * pWorldGuide->sch.width + 16 * cx;

		// loop through local block locations, 0-15,0-15
		for (int y = 0; y < ylength; y++) {
			for (int z = 0; z < zlength; z++) {
				int index = (y * 16 + z) * 16;
				int schIndex = (y * pWorldGuide->sch.length + z) * pWorldGuide->sch.width + offset;
				assert(schIndex >= 0 && schIndex < pWorldGuide->sch.numBlocks);
				for (int x = 0; x < xlength; x++, index++, schIndex++) {

					block->grid[index] = pWorldGuide->sch.blocks[schIndex];
					block->data[index / 2] |= pWorldGuide->sch.data[schIndex] << ((index % 2) * 4);
				}
			}
		}
	}
	return 1;
}

// Clear that an unknown block was encountered. Good to do when loading a new world.
void ClearBlockReadCheck()
{
    gUnknownBlock = 0;
}

// was an unknown block found during mapping? Will be set true only if CheckUnknownBlock is true (which it is by default)
int UnknownBlockRead()
{
    return gUnknownBlock;
}

// should we check for unknown blocks? If turned off, we won't assert and won't flag (but will still clean up) any unknown blocks found.
// Normally always on, could turn it off for debugging situations.
void CheckUnknownBlock( int check )
{
    gPerformUnknownBlockCheck = check;
}

// should not be needed, but there's some timing thread weirdness
int NeedToCheckUnknownBlock()
{
    return gPerformUnknownBlockCheck;
}

int GetSpawn(const wchar_t *world,int *x,int *y,int *z)
{
    bfFile bf;
    wchar_t filename[256];
    wcsncpy_s(filename,256,world,255);
    wcscat_s(filename,256,L"/level.dat");
    bf=newNBT(filename);
    if ( bf.gz == 0x0 ) return 1;
    nbtGetSpawn(bf,x,y,z);
    nbtClose(bf);
    return 0;
}
int GetFileVersion(const wchar_t *world, int *version)
{
	bfFile bf;
	wchar_t filename[256];
	wcsncpy_s(filename, 256, world, 255);
	wcscat_s(filename, 256, L"/level.dat");
	bf = newNBT(filename);
	if (bf.gz == 0x0) return 1;
	nbtGetFileVersion(bf, version);
	nbtClose(bf);
	return 0;
}
int GetFileVersionId(const wchar_t *world, int *versionId)
{
	bfFile bf;
	wchar_t filename[256];
	wcsncpy_s(filename, 256, world, 255);
	wcscat_s(filename, 256, L"/level.dat");
	bf = newNBT(filename);
	if (bf.gz == 0x0) return 1;
	nbtGetFileVersionId(bf, versionId);
	nbtClose(bf);
	return 0;
}
int GetFileVersionName(const wchar_t *world, char *versionName)
{
	bfFile bf;
	wchar_t filename[256];
	wcsncpy_s(filename, 256, world, 255);
	wcscat_s(filename, 256, L"/level.dat");
	bf = newNBT(filename);
	if (bf.gz == 0x0) return 1;
	nbtGetFileVersionName(bf, versionName);
	nbtClose(bf);
	return 0;
}
int GetLevelName(const wchar_t *world, char *levelName)
{
    bfFile bf;
    wchar_t filename[256];
    wcsncpy_s(filename,256,world,255);
    wcscat_s(filename,256,L"/level.dat");
    bf=newNBT(filename);
    if ( bf.gz == 0x0 ) return 1;
    nbtGetLevelName(bf,levelName);
    nbtClose(bf);
    return 0;
}
//void GetRandomSeed(const wchar_t *world,long long *seed)
//{
//    bfFile bf;
//    wchar_t filename[256];
//    wcsncpy_s(filename,256,world,255);
//    wcscat_s(filename,256,L"/level.dat");
//    bf=newNBT(filename);
//    nbtGetRandomSeed(bf,seed);
//    gMapSeed = *seed;
//    nbtClose(bf);
//
//}
void GetPlayer(const wchar_t *world,int *px,int *py,int *pz)
{
    bfFile bf;
    wchar_t filename[256];
    wcsncpy_s(filename,256,world,255);
    wcscat_s(filename,256,L"/level.dat");
    bf=newNBT(filename);
    nbtGetPlayer(bf,px,py,pz);
    nbtClose(bf);
}

/////////////////////////// Schematic read file
// return 0 for OK, 1 for failure
int GetSchematicWord(const wchar_t *schematic, char *field, int *value)
{
	bfFile bf;
	bf = newNBT(schematic);
	if (bf.gz == 0x0) return 1;
	int retval = nbtGetSchematicWord(bf, field, value);
	nbtClose(bf);
	return retval;
}

int GetSchematicBlocksAndData(const wchar_t *schematic, int numBlocks, unsigned char *schematicBlocks, unsigned char *schematicBlockData)
{
	bfFile bf;
	bf = newNBT(schematic);
	if (bf.gz == 0x0) return 1;
	int retval = nbtGetSchematicBlocksAndData(bf, numBlocks, schematicBlocks, schematicBlockData);
	nbtClose(bf);
	return retval;
}


//Sets the pcolor, the premultiplied colors, as these are a pain to precompute and put in the table.
void SetMapPremultipliedColors()
{
    unsigned int color;
    unsigned char r,g,b;
    unsigned char ra,ga,ba;
    float a;
    int i;

    for (i=0;i<NUM_BLOCKS_DEFINED;i++)
    {
        color = gBlockDefinitions[i].color = gBlockDefinitions[i].read_color;
        r=(unsigned char)((color>>16)&0xff);
        g=(unsigned char)((color>>8)&0xff);
        b=(unsigned char)color&0xff;
        a= gBlockDefinitions[i].alpha = gBlockDefinitions[i].read_alpha;
        ra=(unsigned char)(r*a); //premultiply alpha
        ga=(unsigned char)(g*a);
        ba=(unsigned char)(b*a);
        gBlockDefinitions[i].pcolor=(ra<<16)|(ga<<8)|ba;

        // reality check: every block except the first one (air) should have *some* sort of "it exists" flag set for it.
        assert( gBlockDefinitions[i].flags & BLF_CLASS_SET );
    }
}

//Sets the colors used.
//palette should be in RGBA format
void SetMapPalette(unsigned int *palette,int num)
{
    unsigned char r,g,b;
    unsigned char ra,ga,ba;
    float a;
    int i;

    gColormap++;
    for (i=0;i<num;i++)
    {
        r=(unsigned char)(palette[i]>>24);
        g=(unsigned char)(palette[i]>>16);
        b=(unsigned char)(palette[i]>>8);
        a=((float)(palette[i]&0xff))/255.0f;
        ra=(unsigned char)(r*a); //premultiply alpha
        ga=(unsigned char)(g*a);
        ba=(unsigned char)(b*a);
        gBlockDefinitions[i].color=(r<<16)|(g<<8)|b;
        gBlockDefinitions[i].pcolor=(ra<<16)|(ga<<8)|ba;
        gBlockDefinitions[i].alpha=a;
    }
    initColors();
}

// for each block color, calculate light levels 0-15
static void initColors()
{
    unsigned int r,g,b,i,shade;
    double y,u,v,delta;
    unsigned int color;
    int rx, ry;

    gColorsInited=1;
    for (i=0;i<NUM_BLOCKS_DEFINED;i++)
    {
        color=gBlockDefinitions[i].pcolor;
        r=color>>16;
        g=(color>>8)&0xff;
        b=color&0xff;
        //we'll use YUV to darken the blocks.. gives a nice even
        //coloring
        y=0.299*r+0.587*g+0.114*b;
        u=(b-y)*0.565;
        v=(r-y)*0.713;
        delta=y/15;

        for (shade=0;shade<16;shade++)
        {
            y=shade*delta;
            r=(unsigned int)clamp(y+1.403*v,0,255);
            g=(unsigned int)clamp(y-0.344*u-0.714*v,0,255);
            b=(unsigned int)clamp(y+1.770*u,0,255);
            gBlockColors[i*16+shade]=(r<<16)|(g<<8)|b;
        }
    }

    // set the Empty color, for initialization
    gEmptyR = (unsigned char)(gBlockColors[15]>>16);
    gEmptyG = (unsigned char)((gBlockColors[15]>>8)&0xff);
    gEmptyB = (unsigned char)(gBlockColors[15]&0xff);

    // also initialize the "missing tile" unknown graphic, a gray and black checkerboard
    for (rx = 0; rx < 16; ++rx)
    {
        for (ry = 0; ry < 16; ++ry)
        {
            int off = (rx+ry*16)*4;
            int tone = 150;
            if ((rx/4)%2 ^ (ry/4)%2)
                tone=140;
            gBlankTile[off] = (unsigned char)tone;
            gBlankTile[off+1] = (unsigned char)tone;
            gBlankTile[off+2] = (unsigned char)tone;
            gBlankTile[off+3] = (unsigned char)255;	// was 128 - why?

			// fully inside highlight box
			gBlankHighlitTile[off] = (unsigned char)((double)gBlankTile[off] * (1.0 - gHalpha) + gHalpha*(double)gHred);
			gBlankHighlitTile[off + 1] = (unsigned char)((double)gBlankTile[off + 1] * (1.0 - gHalpha) + gHalpha*(double)gHgreen);
			gBlankHighlitTile[off + 2] = (unsigned char)((double)gBlankTile[off + 2] * (1.0 - gHalpha) + gHalpha*(double)gHblue);
			gBlankHighlitTile[off + 3] = (unsigned char)255;
		}
    }
}