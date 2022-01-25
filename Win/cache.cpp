/*
Copyright (c) 2011, Ryan Hitchman
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

#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a simple cache based on a hashtable with separate chaining */

// these must be powers of two
#ifndef MINEWAYS_X64
#define HASH_XDIM 64
#define HASH_ZDIM 64
#else
#define HASH_XDIM 128
#define HASH_ZDIM 128
#endif
#define HASH_SIZE (HASH_XDIM * HASH_ZDIM)

// arbitrary, let users tune this?
// 6000 entries translates to Mineways using ~300MB of RAM (on x64)

static int gHashMaxEntries = INITIAL_CACHE_SIZE;   // was 6000, Sean said to increase it - really should be 30000, because export memory toggle now changes it to this

typedef struct block_entry {
    int x, z;
    struct block_entry* next;
    WorldBlock* data;
} block_entry;

typedef struct {
    int x, z;
} IPoint2;

static block_entry** gBlockCache = NULL;

static IPoint2* gCacheHistory = NULL;
static int gCacheN = 0;
static bool gMinimizeBlockSize = false; // fast and memory hoggy

static int hash_coord(int x, int z) {
    return (x & (HASH_XDIM - 1)) * (HASH_ZDIM)+(z & (HASH_ZDIM - 1));
}

static block_entry* hash_new(int x, int z, void* data, block_entry* next) {
    block_entry* ret = (block_entry*)malloc(sizeof(block_entry));
    if (ret == NULL) {
        return NULL;
    }
    ret->x = x;
    ret->z = z;
    ret->data = (WorldBlock*)data;
    ret->next = next;
    return ret;
}

void Change_Cache_Size(int size)
{
    if (size == gHashMaxEntries)
    {
        // no change - why did you call?
        return;
    }
    // mindless, slow, but safe: empty cache and just start again.
    if (size < gHashMaxEntries) {
        // weird, we're making it smaller, so kind of have to start from scratch
        Cache_Empty();
    }
    else {
        // all's fine as is, the hash table of chunks stays as is, just need to make the gCacheHistory larger
        // and reset the cache LRU counter
        gCacheHistory = (IPoint2*)realloc(gCacheHistory, sizeof(IPoint2) * size);
        // We now have more room for entries in the history table, so can allocate more chunks.
        // To reflect this, set the gCacheN counter to the last free entry of the new history table.
        // Then Cache_Add will add new chunks and put their locations in this history table, until full
        // (at which point we'll cycle as usual to put new chunks into the hash table and free the old).
        if (gCacheN > gHashMaxEntries) {
            gCacheN = gHashMaxEntries;
        }
    }
    gHashMaxEntries = size;
}

// "data" here is the WorldBlock
void Cache_Add(int bx, int bz, void* data)
{
    int hash;
    block_entry* to_del = NULL;

    // Make a hash table gBlockCache that is 128*128 (16384) of entry pointers.
    // Each entry pointer will point to entries holding chunks, in a linked list.
    if (gBlockCache == NULL) {
        gBlockCache = (block_entry**)calloc(HASH_SIZE, sizeof(block_entry*));
        //memset(gBlockCache, 0, sizeof(block_entry*) * HASH_SIZE);
        do {
            // Make a history list of X,Z points of the size of the cache itself,
            // with each new chunk put in the next available spot, modulo.
            // This list is purely for knowing which entry to free next if the
            // cache gets filled.
            gCacheHistory = (IPoint2*)malloc(sizeof(IPoint2) * gHashMaxEntries);
            if (gCacheHistory == NULL) {
                // ruh roh, out of memory! We'll likely run out for reals later,
                // but might as well try to recover now.
                gHashMaxEntries /= 2;
            }
            // if we get to gHashMaxEntries then we're doomed
        } while (gCacheHistory == NULL && gHashMaxEntries > 0);
        // new list, so it's empty:
        gCacheN = 0;
    }

    // So, now we have a gCacheHistory list for coordinates, and each of those
    // coordinate's chunk data itself will go in gBlockCache, with the hash pointing
    // to the proper spot, then in a linked list from that spot.

    // find index into hash table, based on X and Z
    hash = hash_coord(bx, bz);

    // Is the list of chunks full at this point?
    if (gCacheN >= gHashMaxEntries) {
        // We need to remove an old entry.
        // Find the coordinates of some old point in the cache.
        // Note that gCacheN always increases, so as more cached
        // entries are reused, the cache is cycled through, LRU.
        IPoint2 coord = gCacheHistory[gCacheN % gHashMaxEntries];
        int oldhash = hash_coord(coord.x, coord.z);

        // Find the entry in gBlockCache
        block_entry** cur = &gBlockCache[oldhash];
        while (*cur != NULL) {
            if ((**cur).x == coord.x && (**cur).z == coord.z) {
                to_del = *cur;
                *cur = to_del->next;
                // save away the WorldBlock itself for reuse in "last_block"
                block_free(to_del->data);
                to_del->data = NULL;    // for safety's sake
                //free(to_del); // we will re-use this entry below
                break;
            }
            cur = &((**cur).next);
        }
    }

    // if we removed to_del, the entry in the cache that *points* to the WorldBlock,
    // from the cache itself, we can reuse it.
    if (to_del != NULL) {
        // re-use the old entry for the new one
        to_del->next = gBlockCache[hash];
        to_del->x = bx;
        to_del->z = bz;
        to_del->data = (WorldBlock*)data;
        gBlockCache[hash] = to_del;
    }
    else {
        // Make a new entry, attach data to it, and put it in the cache
        gBlockCache[hash] = hash_new(bx, bz, data, gBlockCache[hash]);
        if (gBlockCache[hash] == NULL) {
            // game over, out of memory
            //assert(0);
            return;
        }
    }

    // final thing: actually add the new chunk into the history table
    gCacheHistory[gCacheN % gHashMaxEntries].x = bx;
    gCacheHistory[gCacheN % gHashMaxEntries].z = bz;
    // and note a new entry is used
    gCacheN++;
}

bool Cache_Find(int bx, int bz, void** data)
{
    block_entry* entry;
    // in case we assume the block will be found and are not checking the return code
    *data = NULL;

    if (gBlockCache == NULL)
        return false;

    // Find the head of the list for this hash and go through the block cache to find it.
    for (entry = gBlockCache[hash_coord(bx, bz)]; entry != NULL; entry = entry->next) {
        if (entry->x == bx && entry->z == bz) {
            *data = (void*)entry->data;
            return true;
        }
    }

    return false;
}

void Cache_Empty()
{
    int hash;
    block_entry* entry, * next;

    if (gBlockCache == NULL)
        return;

    for (hash = 0; hash < HASH_SIZE; hash++) {
        entry = gBlockCache[hash];
        while (entry != NULL) {
            next = entry->next;
            // so hacky
            if (entry->data != NULL) {
                block_force_free(entry->data);
                entry->data = NULL;
            }
            free(entry);
            entry = next;
        }
    }

    free(gBlockCache);
    free(gCacheHistory);
    gBlockCache = NULL;
    gCacheHistory = NULL;
}


/* a simple malloc wrapper, based on the observation that a common
** behavior pattern for Mineways when the cache is at max capacity
** is something like:
**
**   newBlock = malloc(sizeof(Block));
**   cacheAdd(newBlock)
**   free(oldBlock) // same size
**
** repeatedly. Recycling the old block can prevent the need for
** malloc and free.
**/

static WorldBlock* last_block = NULL;

WorldBlock* block_alloc(int minHeight, int maxHeight)
{
    int height = maxHeight - minHeight + 1;
    WorldBlock* ret = NULL;
    // is a cached block available and is it the right size?
    if (last_block != NULL && last_block->heightAlloc == height)
    {
        // use this cached block (clearing out just the optional entity storage first)
        ret = last_block;
        if (ret->entities != NULL) {
            free(ret->entities);
            ret->entities = NULL;
            ret->numEntities = 0;
        }
        last_block = NULL;
        return ret;
    }
    else {
        // new block needed
		if (last_block != NULL) {
			// saved block was wrong size - clear it out, since it's unlikely to be used
			block_force_free(last_block);
			last_block = NULL;
		}
		// allocate normally
	    ret = (WorldBlock*)malloc(sizeof(WorldBlock));
	    if (ret == NULL)
	        return NULL;
	    ret->grid = (unsigned char*)malloc(16 * 16 * height * sizeof(unsigned char));
	    if (ret->grid == NULL)
	        return NULL;
	    ret->data = (unsigned char*)malloc(16 * 16 * height * sizeof(unsigned char));
	    if (ret->data == NULL)
	        return NULL;
	    ret->light = (unsigned char*)malloc(16 * 16 * height * sizeof(unsigned char) / 2);
	    if (ret->light == NULL)
	        return NULL;
	    ret->entities = NULL;
	    ret->numEntities = 0;
	    ret->heightAlloc = height;    // for some betas of 1.17 it is 384 - change by checking versionID
        ret->minHeight = minHeight;
        ret->maxHeight = maxHeight;
	    ret->maxFilledSectionHeight = ret->maxFilledHeight = EMPTY_MAX_HEIGHT;  // not yet determined
	    return ret;
	}
}

// Given a WorldBlock that is no longer cached, save it away as "last_block" for immediate reuse if possible.
// If there's already a last_block, get rid of that one and save this one instead (TODO: is there a better tie break?)
void block_free(WorldBlock* block)
{
    // don't bother "caching" an empty WorldBlock - keep the old one in last_block for return some other time
    if (block == NULL)
        return;

    // keep latest freed block available in "last_block", so free the one already there
    if (last_block != NULL && last_block != block)
    {
        block_force_free(last_block);
    }

    last_block = block;
}

// Really free the block, period
void block_force_free(WorldBlock* block)
{
    if (block == NULL)
        return;

    if (block->entities != NULL) {
        free(block->entities);
        block->entities = NULL;
        block->numEntities = 0;
    }
    if (block->grid != NULL) {
        free(block->grid);
        // should be unnecessary, but just in case there's a double free, somehow
        block->grid = NULL;
    }
    if (block->data != NULL) {
        free(block->data);
        block->data = NULL;
    }
    if (block->light != NULL) {
        free(block->light);
        block->light = NULL;
    }
    free(block);
}

// reallocs only if memory minimization is on.
void MinimizeCacheBlocks(bool min)
{
    gMinimizeBlockSize = min;
}

void block_realloc(WorldBlock* block)
{
    if (block == NULL)
        return;

    if (gMinimizeBlockSize) {
        if (block->heightAlloc > block->maxFilledHeight + 1) {
            // we can make it smaller
            int heightAlloc = block->maxFilledHeight + 1;

            unsigned char* grid = (unsigned char*)realloc(block->grid, 256 * heightAlloc);
            unsigned char* data = (unsigned char*)realloc(block->data, 256 * heightAlloc);
            unsigned char* light = (unsigned char*)realloc(block->light, 128 * heightAlloc);
            if (grid && data && light) {
                block->grid = grid;
                block->data = data;
                block->light = light;
                block->heightAlloc = heightAlloc;
            }
            else {
                // we failed, but at least stop trying to realloc if we're really unable to do so.
                // By setting the maxHeight here, we signal that we at least tried
                block->heightAlloc = heightAlloc;
                //assert(0);
            }
        }
    }
}


