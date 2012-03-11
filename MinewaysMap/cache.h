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


#ifndef __CACHE_H__
#define __CACHE_H__

typedef struct WorldBlock {
    unsigned char grid[16*16*128];  // blockid array [y+(z+x*16)*128]
    unsigned char data[16*16*64];  // additional data about each block (wool color, etc.)
    unsigned char light[16*16*64];

    unsigned char rendercache[16*16*4]; // bitmap of last render
    unsigned char heightmap[16*16]; // height of rendered block [x+z*16]

    int rendery;        // slice height for last render
    int renderopts;     // options bitmask for last render
    int renderhilitID;	// the last selected area's ID tag
    char rendermissing;  // the z-offset of a block that was missing
                        // when it was last rendered (for blocks on the
                        // left edge of the map, this might be +1)
    unsigned short colormap; //color map when this was rendered
} WorldBlock;

void Change_Cache_Size( int size );
void *Cache_Find(int bx,int bz);
void Cache_Add(int bx,int bz,void *data);
void Cache_Empty();

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

WorldBlock* block_alloc();           // allocate memory for a block
void block_free(WorldBlock* block); // release memory for a block

#endif
