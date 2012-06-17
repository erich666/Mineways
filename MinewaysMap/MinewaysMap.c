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
#include "blockInfo.h"
#include <assert.h>
#include <string.h>

static unsigned char* draw(const wchar_t *world,int bx,int bz,int y,Options opts,
        ProgressCallback callback,float percent,int *hitsFound);
static void blit(unsigned char *block,unsigned char *bits,int px,int py,
        double zoom,int w,int h);
static void initColors();

static int gColorsInited=0;
static unsigned int gBlockColors[256*16];
static unsigned char gBlankTile[16*16*4];

static unsigned short gColormap=0;
static long long gMapSeed;

static int gBoxHighlightUsed=0;
static int gBoxMinX;
static int gBoxMinY;
static int gBoxMinZ;
static int gBoxMaxX;
static int gBoxMaxY;
static int gBoxMaxZ;
static int gDirtyBoxMinX=99999;
static int gDirtyBoxMinZ=99999;
static int gDirtyBoxMaxX=-99999;
static int gDirtyBoxMaxZ=-99999;

// highlight blend factor and color
static double gHalpha = 0.3;
static double gHalphaBorder = 0.8;
static int gHred = 205;
static int gHgreen = 50;
static int gHblue = 255;
static int gHighlightID=0;

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
            // This *can* mess up, if the selection area is off screen
            // when the dirty region is used
            if ( gDirtyBoxMinX > minx/16 )
                gDirtyBoxMinX = minx/16;
            if ( gDirtyBoxMinZ > minz/16 )
                gDirtyBoxMinZ = minz/16;
            if ( gDirtyBoxMaxX < maxx/16 )
                gDirtyBoxMaxX = maxx/16;
            if ( gDirtyBoxMaxZ < maxz/16 )
                gDirtyBoxMaxZ = maxz/16;
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
void DrawMap(const wchar_t *world,double cx,double cz,int y,int w,int h,double zoom,unsigned char *bits,Options opts, int *hitsFound, ProgressCallback callback)
{
    /* We're converting between coordinate systems, so this gets kinda ugly 
     *
     * X     -world x N  -screen y
     * screen origin  |
     *                |
     *                |
     *                |
     *  +world z      |(cz,cx)   -world z
     * W--------------+----------------E
     *  -screen x     |          +screen x
     *                |
     *                | 
     *                |
     *      +world x  | +screen y
     *                S
     */

    // Yes, yes, -x is north under most of the Beta versions of Minecraft. Feel free to revise
    // the code to make -z to be north, for the release. If you add this, it should be an option,
    // so that old players (like me) can use "old north". TODO

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
		startxblock--;
        shiftx+=blockScale;
    }
    if (shifty<0)
    {
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
            blockbits = draw(world,startxblock+x,startzblock+z,y,opts,callback,(float)(z*hBlocks+x)/(float)(vBlocks*hBlocks),hitsFound);
            blit(blockbits,bits,px,py,zoom,w,h);
        }
    }
    // clear dirty rectangle, if any
    if ( gBoxHighlightUsed )
    {
        // box is set to current rectangle
        // TODO: this isn't quite right, as if you select a large rect, scroll it offscreen
        // then select new and scroll back, you'll see the highlight.
        gDirtyBoxMinX = gBoxMinX/16;
        gDirtyBoxMinZ = gBoxMinZ/16;
        gDirtyBoxMaxX = gBoxMaxX/16;
        gDirtyBoxMaxZ = gBoxMaxZ/16;

    }
    else
    {
        // empty
        gDirtyBoxMinX=gDirtyBoxMinZ=99999;
        gDirtyBoxMaxX=gDirtyBoxMaxZ=-99999;
    }
}

//bx = x coord of pixel
//by = y coord of pixel
//cx = center x world
//cz = center z world
//w = output width
//h = output height
//zoom = zoom amount (1.0 = 100%)
//ox = world x at mouse
//oz = world z at mouse
const char *IDBlock(int bx, int by, double cx, double cz, int w, int h, double zoom,int *ox,int *oy,int *oz,int *type)
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
    assert(cz < 10000);
    assert(cz > -10000);

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

    // if off window above
    // Sean's fix, but makes the screen go empty if I scroll off top of window
    if (by<0) return "";

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
        *type=BLOCK_AIR;
        return "Unknown";
    }

    y=block->heightmap[xoff+zoff*16];
    *oy=y;

    // Note that when "hide obscured" is on, blocks can be empty because
    // they were solid from the current level on down.
    if (y == (unsigned char)-1)
    {
        *oy=-1;
        *type=BLOCK_BEDROCK;
        return "Empty";  // nothing was rendered here
    }

    // there's a bug in the original code, sometimes xoff is negative.
    // For now, assert when I see it, and return empty - better than crashing.
    //assert( y+(zoff+xoff*16)*128 >= 0 );
    if ( y*256+zoff*16+xoff < 0 || y*256+zoff*16+xoff >= 65536) {
        return "(off map)";
    }

    *type = block->grid[xoff+zoff*16+y*256];
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

// Draw a block at chunk bx,bz
// opts is a bitmask representing render options (see MinewaysMap.h)
// returns 16x16 set of block colors to use to render map.
// colors are adjusted by height, transparency, etc.
static unsigned char* draw(const wchar_t *world,int bx,int bz,int maxHeight,Options opts,ProgressCallback callback,float percent,int *hitsFound)
{
    WorldBlock *block, *prevblock;
    int ofs=0,prevy,bofs,prevSely,blockSolid;
    //int hasSlime = 0;
    int x,z,i;
    unsigned int color, viewFilterFlags;
    unsigned char voxel, r, g, b, seenempty;
    double alpha, blend;

    char cavemode, showobscured, depthshading, lighting;
    unsigned char *bits;

//    if ((opts.worldType&(HELL|ENDER|SLIME))==SLIME)
//            hasSlime = isSlimeChunk(bx, bz);

    cavemode=!!(opts.worldType&CAVEMODE);
    showobscured=!(opts.worldType&HIDEOBSCURED);
    depthshading=!!(opts.worldType&DEPTHSHADING);
    lighting=!!(opts.worldType&LIGHTING);
    viewFilterFlags= BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER | BLF_BILLBOARD | BLF_PANE | BLF_FLATTOP |   // what's visible
        ((opts.worldType&SHOWALL)?(BLF_FLATSIDE|BLF_SMALL_MIDDLER|BLF_SMALL_BILLBOARD):0x0);

    block=(WorldBlock *)Cache_Find(bx,bz);

    if (block==NULL)
    {
        wchar_t directory[256];
        wcsncpy_s(directory,256,world,255);
        wcsncat_s(directory,256,L"/",1);
        if (opts.worldType&HELL)
        {
            wcsncat_s(directory,256,L"DIM-1/",6);
        }
        if (opts.worldType&ENDER)
        {
            wcsncat_s(directory,256,L"DIM1/",5);
        }

        block=LoadBlock(directory,bx,bz);
        if (block==NULL) //blank tile
            return gBlankTile;

        //let's only update the progress bar if we're loading
        if (callback)
            callback(percent);

        Cache_Add(bx,bz,block);
    }

    if (block->rendery==maxHeight && block->renderopts==opts.worldType && block->colormap==gColormap) // already rendered
    {
        // final check, has highlighting state changed
        if ( block->renderhilitID==gHighlightID )
        {
            if (block->rendermissing // wait, the last render was incomplete
                && Cache_Find(bx, bz+block->rendermissing) != NULL) {
                    ; // we can do a better render now that the missing block is loaded
            } else {
                // there's no need to re-render, use cache
                return block->rendercache;
            }
        }
        else
        {
            // check if block overlaps dirty rectangle
            if ( bx < gDirtyBoxMinX-1 || bx > gDirtyBoxMaxX ||
                bz < gDirtyBoxMinZ-1 || bz > gDirtyBoxMaxZ )
            {
                // doesn't overlap, so check cache, same as above
                if (block->rendermissing // wait, the last render was incomplete
                    && Cache_Find(bx, bz+block->rendermissing) != NULL) {
                        ; // we can do a better render now that the missing block is loaded
                } else {
                    // there's no need to re-render, use cache
                    return block->rendercache;
                }
            }
        }
    }

    block->rendery=maxHeight;
    block->renderopts=opts.worldType;
    block->renderhilitID=gHighlightID;
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
    // x increases south, decreases north
	for (z=0;z<16;z++)
    {
        if (prevblock!=NULL)
			prevy = prevblock->heightmap[15+z*16];
        else
            prevy=-1;

        // z increases (old) west, decreases (old) east
		for (x=0;x<16;x++)
        {
            prevSely = -1;

            bofs=((maxHeight*16+z)*16+x);
            color=0;
            r=g=b=0;
            // if we start at the top of the world, seenempty is set to 1 (there's air above), else 0
            // The idea here is that if you're delving into cave areas, "hide obscured" will treat all
            // blocks at the topmost layer as empty, until a truly empty block is hit, at which point
            // the next solid block is then shown. If it's solid all the way down, the block will be
            // drawn as "empty"
            seenempty=(maxHeight==MAP_MAX_HEIGHT?1:0);
            alpha=0.0;
            // go from top down through all voxels, looking for the first one visible.
			for (i=maxHeight;i>=0;i--,bofs-=16*16)
            {
                voxel=block->grid[bofs];
                // if block is air or something very small, note it's empty and continue to next voxel
                if ( (voxel==BLOCK_AIR) ||
                    !(gBlockDefinitions[voxel].flags & viewFilterFlags ))
                {
                    seenempty=1;
                    continue;
                }

                // special selection height: we want to be able to select water
                blockSolid = voxel<NUM_BLOCKS && gBlockDefinitions[voxel].alpha!=0.0;
                if ((showobscured || seenempty) && blockSolid)
                    if (prevSely==-1) 
                        prevSely=i;

                // non-flowing water does not count when finding the displayed height, so that we can reveal what is
                // underneath the water.
                if (voxel==BLOCK_STATIONARY_WATER)
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
							light=block->light[bofs/2];
							if (bofs&1) light>>=4;
                            light&=0xf;
                        } else
                        {
                            light = 0;
                        }
                    }
                    // if it's the first voxel visible, note this depth.
                    if (prevy==-1) 
                        prevy=i;
                    else if (prevy<i)
                        light+=2;
                    else if (prevy>i)
                        light-=5;
                    light=clamp(light,1,15);
                    color=gBlockColors[voxel*16+light];
                    if (alpha==0.0)
                    {
                        alpha=gBlockDefinitions[voxel].alpha;
                        r=(unsigned char)(color>>16);
                        g=(unsigned char)((color>>8)&0xff);
                        b=(unsigned char)(color&0xff);
                    }
                    else
                    {
                        r+=(unsigned char)((1.0-alpha)*(color>>16));
                        g+=(unsigned char)((1.0-alpha)*((color>>8)&0xff));
                        b+=(unsigned char)((1.0-alpha)*(color&0xff));
                        alpha+=gBlockDefinitions[voxel].alpha*(1.0-alpha);
                    }
                    // if the block is solid, break out of the loop, we're done
                    if (gBlockDefinitions[voxel].alpha==1.0)
                        break;
                }
            }

            prevy=i;

            if (depthshading) // darken deeper blocks
            {
				int num=prevy+50-(256-maxHeight)/5;
				int denom=maxHeight+50-(256-maxHeight)/5;

                r=(unsigned char)(r*num/denom);
                g=(unsigned char)(g*num/denom);
                b=(unsigned char)(b*num/denom);
            }

            //if(hasSlime > 0){
            //    // before 1.9 Pre 5 it was 16, see http://www.minecraftwiki.net/wiki/Slime
            //    //if(maxHeight<=16){
            //    if(maxHeight<=40){
            //        g=clamp(g+20,0,MAP_MAX_HEIGHT);
            //    }else{
            //        if(x%15==0 || z%15==0){
            //            g=clamp(g+20,0,MAP_MAX_HEIGHT);
            //        }
            //    }
            //}

            if (cavemode)
            {
                seenempty=0;
                voxel=block->grid[bofs];

                if (voxel==BLOCK_LEAVES || voxel==BLOCK_LOG) //special case surface trees
					for (; i>=1; i--,bofs-=16*16,voxel=block->grid[bofs])
                        if (!(voxel==BLOCK_LOG||voxel==BLOCK_LEAVES||voxel==BLOCK_AIR))
                            break; // skip leaves, wood, air

				for (;i>=1;i--,bofs-=16*16)
                {
                    voxel=block->grid[bofs];
                    if (voxel==BLOCK_AIR)
                    {
                        seenempty=1;
                        continue;
                    }
                    if (seenempty && voxel<NUM_BLOCKS && gBlockDefinitions[voxel].alpha!=0.0)
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
                    if ( prevSely >= gBoxMinY && prevSely <= gBoxMaxY )
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
                            double dim=0.5;
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
                            double brighten=0.5;
                            r = (unsigned char)((double)r*(1.0-brighten) + brighten);
                            g = (unsigned char)((double)g*(1.0-brighten) + brighten);
                            b = (unsigned char)((double)b*(1.0-brighten) + brighten);
                        }
                    }
                }
            }

            bits[ofs++]=r;
            bits[ofs++]=g;
            bits[ofs++]=b;
            bits[ofs++]=0xff;

            block->heightmap[x+z*16] = (unsigned char)prevy;
        }
    }
    return bits;
}

WorldBlock *LoadBlock(wchar_t *directory, int cx, int cz)
{
    unsigned char *pBlockID;
    WorldBlock *block=block_alloc();
    block->rendery = -1; // force redraw

    if (regionGetBlocks(directory, cx, cz, block->grid, block->data, block->light)) {
        // got block successfully
        // Major change: convert all wool found into colored wool. It's much easier
        // to simply change to a new block type, colored wool, than put special-case
        // code throughout the program. If you don't like colored wool (it costs a
        // little speed to process the wool), comment out this piece. You'll also
        // have to change numBlocks = numBlocksStandard
        //if ( convertToColoredWool )
        //{
            int i;
            pBlockID = block->grid;
            for ( i = 0; i < 16*16*256; i++, pBlockID++ )
            {
                if ( *pBlockID == BLOCK_WOOL)
                {
                    // convert to new block
                    int woolVal = block->data[i/2];
                    if ( i & 0x01 )
                        woolVal = woolVal >> 4;
                    else
                        woolVal &= 0xf;
                    *pBlockID = (unsigned char)(NUM_BLOCKS_STANDARD + woolVal);
                }
				else if ( *pBlockID >= NUM_BLOCKS_STANDARD )
				{
					// some new version of Minecraft, block ID is unrecognized;
					// turn this block into stone. dataVal will be ignored.
					assert( *pBlockID < NUM_BLOCKS_STANDARD );	// note the program needs fixing
					*pBlockID = BLOCK_STONE;
				}
            }
        //}
        return block;
    }

    block_free(block);
    return NULL;
}

int GetSpawn(const wchar_t *world,int *x,int *y,int *z)
{
	bfFile bf;
	wchar_t filename[256];
	wcsncpy_s(filename,256,world,255);
	wcsncat_s(filename,256,L"/level.dat",10);
	bf=newNBT(filename);
	if ( bf.gz == 0x0 ) return 1;
	nbtGetSpawn(bf,x,y,z);
	nbtClose(bf);
	return 0;
}
int GetFileVersion(const wchar_t *world,int *version)
{
	bfFile bf;
	wchar_t filename[256];
	wcsncpy_s(filename,256,world,255);
	wcsncat_s(filename,256,L"/level.dat",10);
	bf=newNBT(filename);
	if ( bf.gz == 0x0 ) return 1;
	nbtGetFileVersion(bf,version);
	nbtClose(bf);
	return 0;
}
//void GetRandomSeed(const wchar_t *world,long long *seed)
//{
//    bfFile bf;
//    wchar_t filename[256];
//    wcsncpy_s(filename,256,world,255);
//    wcsncat_s(filename,256,L"/level.dat",10);
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
    wcsncat_s(filename,256,L"/level.dat",10);
    bf=newNBT(filename);
    nbtGetPlayer(bf,px,py,pz);
    nbtClose(bf);
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
    unsigned r,g,b,i,shade;
    double y,u,v,delta;
    unsigned int color;
    int rx, ry;

    gColorsInited=1;
    for (i=0;i<NUM_BLOCKS;i++)
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

    // also initialize the "missing tile" graphic

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
            gBlankTile[off+3] = (unsigned char)128;
        }
    }
}
