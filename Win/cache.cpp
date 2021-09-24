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
#define HASH_XDIM 64
#define HASH_ZDIM 64
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
    // mindless, but safe: empty cache and just start again.
    Cache_Empty();
    gHashMaxEntries = size;
}

void Cache_Add(int bx, int bz, void* data)
{
    int hash;
    block_entry* to_del = NULL;

    if (gBlockCache == NULL) {
        gBlockCache = (block_entry**)calloc(HASH_SIZE, sizeof(block_entry*));
        //memset(gBlockCache, 0, sizeof(block_entry*) * HASH_SIZE);
        do {
            gCacheHistory = (IPoint2*)malloc(sizeof(IPoint2) * gHashMaxEntries);
            if (gCacheHistory == NULL) {
                // ruh roh
                gHashMaxEntries /= 2;
            }
            // if we get to gHashMaxEntries then we're doomed
        } while (gCacheHistory == NULL && gHashMaxEntries > 0);
        gCacheN = 0;
    }

    hash = hash_coord(bx, bz);

    if (gCacheN >= gHashMaxEntries) {
        // we need to remove an old entry
        IPoint2 coord = gCacheHistory[gCacheN % gHashMaxEntries];
        int oldhash = hash_coord(coord.x, coord.z);

        block_entry** cur = &gBlockCache[oldhash];
        while (*cur != NULL) {
            if ((**cur).x == coord.x && (**cur).z == coord.z) {
                to_del = *cur;
                *cur = to_del->next;
                block_free(to_del->data);
                //free(to_del); // we will re-use this entry
                break;
            }
            cur = &((**cur).next);
        }
    }

    if (to_del != NULL) {
        // re-use the old entry for the new one
        to_del->next = gBlockCache[hash];
        to_del->x = bx;
        to_del->z = bz;
        to_del->data = (WorldBlock*)data;
        gBlockCache[hash] = to_del;
    }
    else {
        gBlockCache[hash] = hash_new(bx, bz, data, gBlockCache[hash]);
        if (gBlockCache[hash] == NULL) {
            // game over, out of memory
            //assert(0);
            return;
        }
    }

    gCacheHistory[gCacheN % gHashMaxEntries].x = bx;
    gCacheHistory[gCacheN % gHashMaxEntries].z = bz;
    gCacheN++;
}

void* Cache_Find(int bx, int bz)
{
    block_entry* entry;

    if (gBlockCache == NULL)
        return NULL;

    for (entry = gBlockCache[hash_coord(bx, bz)]; entry != NULL; entry = entry->next)
        if (entry->x == bx && entry->z == bz)
            return entry->data;

    return NULL;
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
** newBlock = malloc(sizeof(Block));
** cacheAdd(newBlock)
**  free(oldBlock) // same size
**
** Repeatedly. Recycling the old block can prevent the need for
** malloc and free.
**/

static WorldBlock* last_block = NULL;

WorldBlock* block_alloc(int height)
{
    WorldBlock* ret = NULL;
    // is a cached block available and is it the right size?
    if (last_block != NULL && last_block->maxHeight == height)
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
	    ret->maxHeight = height;    // for some betas of 1.17 it is 384 - change by checking versionID
	    ret->maxFilledSectionHeight = ret->maxFilledHeight = EMPTY_MAX_HEIGHT;  // not yet determined
	    return ret;
	}
}

void block_free(WorldBlock* block)
{
    // keep latest freed block available in "last_block", so free the one already there
    if (last_block != NULL)
    {
        block_force_free(last_block);
    }

    last_block = block;
}

void block_force_free(WorldBlock* block)
{
    if (block->entities != NULL) {
        free(block->entities);
        block->entities = NULL;
        block->numEntities = 0;
    }
    if (block->grid != NULL)
        free(block->grid);
    if (block->data != NULL)
        free(block->data);
    if (block->light != NULL)
        free(block->light);
    free(block);
}

// reallocs only if memory minimization is on.
void MinimizeCacheBlocks(bool min)
{
    gMinimizeBlockSize = min;
}

void block_realloc(WorldBlock* block)
{
    if (gMinimizeBlockSize) {
        if (block->maxHeight > block->maxFilledHeight + 1) {
            // we can make it smaller
            int maxHeight = block->maxFilledHeight + 1;

            unsigned char* grid = (unsigned char*)realloc(block->grid, 256 * maxHeight);
            unsigned char* data = (unsigned char*)realloc(block->data, 256 * maxHeight);
            unsigned char* light = (unsigned char*)realloc(block->light, 128 * maxHeight);
            if (grid && data && light) {
                block->grid = grid;
                block->data = data;
                block->light = light;
                block->maxHeight = maxHeight;
            }
            else {
                // we failed, but at least stop trying to realloc if we're really unable to do so.
                // By setting the maxHeight here, we signal that we at least tried
                block->maxHeight = maxHeight;
                //assert(0);
            }
        }
    }
}


