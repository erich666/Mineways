/*
Copyright (c) 2010, Sean Kasun
Parts Copyright (c) 2010-2011, Ryan Hitchman
All rights reserved.

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

#pragma once

#include "nbt.h"

// For 64 bits we can increase the amount of map memory we can hold at one time considerably.
#ifndef MINEWAYS_X64
// 32 bits can run out of memory pretty quickly
#define INITIAL_CACHE_SIZE 6000
#else
// this is about the number of chunks visible on a 4K screen.
//#define INITIAL_CACHE_SIZE 30000
#define INITIAL_CACHE_SIZE 6000
#endif

// we track maximum height per chunk. Start at this value; if value found later, chunk is empty
#define EMPTY_MAX_HEIGHT -1

typedef struct WorldBlock {
    int minHeight;      // 0 for world < 1.17, -64 for some beta 1.17 and for 1.18
    int maxHeight;      // 255 for world < 1.17, 319 for some beta 1.17 and for 1.18
    int heightAlloc;      // 256 for worlds < 1.17, 384 for 1.17 and beyond - basically, maxHeight - minHeight + 1, the total number of levels we store
    // NOTE: maxFilled*Height values will always (once set) be lower than maxHeight, even if chunk is filled. These values are the maximum level of the grid with stuff in it,
    // e.g., if maxHeight is 384, maxFilledHeight is 383 for a filled chunk, as the levels are 0-383. This is important for reallocing and for testing ranges
    int maxFilledSectionHeight;    // set to EMPTY_MAX_HEIGHT if not yet determined. Gives the height for the first non-zero content found, by 16-height sections. The value could be lower, so use:
    int maxFilledHeight;    // set to EMPTY_MAX_HEIGHT if not yet determined. Gives the height for the first non-zero content found. Lowest value is 0 (not -96 i.e. -gMinHeight for 1.17)
    unsigned char *grid;  // blockid array [y+(z+x*16)*256] -> [16 * 16 * 384]
    // someday we'll need the top four bits field when > 256 blocks
    // unsigned char add[16*16*128];   // the Add tag - see http://www.minecraftwiki.net/wiki/Anvil_file_format
    unsigned char *data;  // half-byte additional data about each block, i.e., subtype such as log type, etc. -> [16 * 16 * 384]
    unsigned char *light; // half-byte lighting data -> [16 * 16 * 384/2]

    unsigned char rendercache[16 * 16 * 4]; // bitmap of last render
    short heightmap[16 * 16]; // height of rendered block [x+z*16]
    unsigned char biome[16 * 16];
    BlockEntity* entities;	// block entities, http://minecraft.wiki/w/Chunk_format#Block_entity_format
    int numEntities;	// number in the list, maximum of 16x16x256
    // a waste to do per block, but so be it.
    int mcVersion;		// type of block opened: 12 for 1.12 and earlier, 13 for 1.13 and on
    int versionID;      // exact version ID, https://minecraft.wiki/w/Data_version

    int rendery;        // slice height for last render
    int renderopts;     // options bitmask for last render
    int renderhilitID;	// the last selected area's ID tag
    char rendermissing;  // the z-offset of a block that was missing
    // when it was last rendered (for blocks on the
    // left edge of the map, this might be +1)
    unsigned short colormap; //color map when this was rendered
    int blockType;		// 1 = normal, 2 = entirely empty; see nbt.h for the definitions
} WorldBlock;

void Change_Cache_Size(int size);
bool Cache_Find(int bx, int bz, void** data);
void Cache_Add(int bx, int bz, void* data);
void Cache_Empty();
void MinimizeCacheBlocks(bool min);

/* a simple malloc wrapper, based on the observation that a common
* behavior pattern for Mineways when the cache is at max capacity
* is something like:
*
* newBlock = malloc(sizeof(Block));
* cacheAdd(newBlock)
*  free(oldBlock) // same size
*
* Repeated over and over. Recycling the most recently freed block
* prevents expensive reallocations.
*/

WorldBlock* block_alloc(int minHeight, int maxHeight);           // allocate memory for a block
void block_free(WorldBlock* block); // release memory for a block
void block_force_free(WorldBlock* block); // no single block cache test - clears the cache, too
void block_realloc(WorldBlock* block);   // realloc and copy over
