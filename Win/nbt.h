/*
Copyright (c) 2010, Sean Kasun
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


#ifndef __NBT_H__
#define __NBT_H__

#define ZLIB_WINAPI
#include "zlib.h"
#include <stdio.h>

enum { BF_BUFFER, BF_GZIP };

// for another 256 block types, this bit gets set in the dataVal field (we're out of bits with block IDs)
// Note tthe next-to-highest bit is used for "waterlogged" so should be avoided if that property is part of the block's description
#define BIT_8 0x08
#define SNOWY_BIT 0x08
#define BIT_16 0x10
#define BIT_32 0x20
#define WATERLOGGED_BIT 0x40
#define HIGH_BIT 0x80

// old data values mixed with new. This works because 0 means empty under both systems, and the high bits (0xff00) are set for all new-style flowers,
// so the old data values 1-13 don't overlap the new ones, which are 16 and higher.
// See nbt for the loop minecraft:red_flower etc. that sets these values.
#define RED_FLOWER_FIELD		0x10
#define YELLOW_FLOWER_FIELD		0x20
#define RED_MUSHROOM_FIELD		0x30
#define BROWN_MUSHROOM_FIELD	0x40
#define SAPLING_FIELD			0x50
#define DEADBUSH_FIELD			0x60
#define TALLGRASS_FIELD			0x70
#define CACTUS_FIELD			0x80
#define BAMBOO_FIELD			0x90

// TODO: also add error codes for true return errors, below, the -1, -2, etc.
#define		NBT_WARNING_NAME_NOT_FOUND	0x4

// wraps gzFile and memory buffers with a consistent interface
typedef struct {
    int type;
    unsigned char* buf;
    int* offset;
    int _offset;
    gzFile gz;
    FILE* fptr;
} bfFile;

typedef struct BlockEntity {
    unsigned char type;
    unsigned char zx;
    unsigned char y;
    unsigned char data;	// major and minor data in one byte
} BlockEntity;

bfFile newNBT(const wchar_t* filename, int* err);
int nbtGetBlocks(bfFile* pbf, unsigned char* buff, unsigned char* data, unsigned char* blockLight, unsigned char* biome, BlockEntity* entities, int* numEntities, int mcversion);
int nbtGetSpawn(bfFile* pbf, int* x, int* y, int* z);
int nbtGetFileVersion(bfFile* pbf, int* version);
int nbtGetFileVersionId(bfFile* pbf, int* versionId);
// currently not used: int nbtGetFileVersionName(bfFile* pbf, char* versionName, int stringLength);
int nbtGetLevelName(bfFile* pbf, char* levelName, int stringLength);
int nbtGetPlayer(bfFile* pbf, int* px, int* py, int* pz);
//void nbtGetRandomSeed(bfFile *pbf,long long *seed);
int nbtGetSchematicWord(bfFile* pbf, char* field, int* value);
int nbtGetSchematicBlocksAndData(bfFile* pbf, int numBlocks, unsigned char* schematicBlocks, unsigned char* schematicBlockData);
void nbtClose(bfFile* pbf);

#endif
