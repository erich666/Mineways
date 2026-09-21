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


#pragma once

#define ZLIB_WINAPI
#include "zlib.h"
#include <stdio.h>
#include <stdint.h>

#define NUM_TRANS 1230

// versionID: what format is the data in https://minecraft.wiki/w/Data_version#List_of_data_versions
// This 2685 version of 1.17 beta, 21w06a, went to a height of 384; went back to normal with 2709, 21w15a.
// Almost future proof for 1.18, which will increase height again. Really, when 1.18 is out, DATA_VERSION_TO_RELEASE_NUMBER macro needs to be updated
//#define MAX_ARRAY_HEIGHT(versionID, mcVersion)   (((versionID >= 2685 && versionID < 2709)||mcVersion>=18) ? 384 : 256)
#define MAX_WORLD_HEIGHT(versionID, mcVersion)   (((versionID >= 2685 && versionID < 2709)||mcVersion>=18) ? 319 : 255)
#define ZERO_WORLD_HEIGHT(versionID, mcVersion)   (((versionID >= 2685 && versionID < 2709)||mcVersion>=18) ? -64 : 0)
//#define MAX_ARRAY_HEIGHT(versionID)   ((versionID >= 2685) ? 384 : 256)
//#define MAX_WORLD_HEIGHT(versionID)   ((versionID >= 2685) ? 319 : 255)
//#define ZERO_WORLD_HEIGHT(versionID)   ((versionID >= 2685) ? -64 : 0)


enum { BF_BUFFER, BF_GZIP };

// for another 256 block types, this bit gets set in the dataVal field (we're out of bits with block IDs)
// Note the next-to-highest bit is used for "waterlogged" so should be avoided if that property is part of the block's description
#define BIT_8 0x08
#define SNOWY_BIT 0x08
#define BIT_16 0x10
#define BIT_32 0x20
#define WATERLOGGED_BIT 0x40
#define TYPE_HIGH_BIT1 0x80

// WorldBlock.data[] / BoxCell.data are unsigned short: the low 12 bits are dataVal (subtype/state,
// 0-4095), the top 4 bits are the high bits (8-11) of the block's type. grid[] (or BoxCell.type's
// low byte) holds the low 8 bits of type, giving a combined 12-bit type (0-4095). This replaces the
// old scheme where TYPE_HIGH_BIT1 (0x80) in an 8-bit dataVal byte doubled as a "type >= 256" promotion
// flag (with BLOCK_HEAD/BLOCK_FLOWER_POT carved out, since they used bit 0x80 as real data) - with
// promotion moved to its own bit range (12-15), disjoint from all real dataVal bits (0-11, which
// still include bit 7 / TYPE_HIGH_BIT1), no carve-out is needed anymore.
#define DATAVAL_MASK    0x0FFF
#define TYPE_EXT_MASK   0xF000
// OR this into a data[] value to promote its block's type by exactly 256 (nibble bit 0 of the
// type-extension field) - the direct replacement for the old "OR in TYPE_HIGH_BIT1" idiom, used by the
// synthetic test-world writer (MinewaysMap.cpp testBlock()) wherever a neighbor cell's type is
// hardcoded to be > 255 rather than computed from an origType (see typeHighBit for that case).
#define TYPE_PROMOTE_256 0x1000

// Combine grid[]/BoxCell.type's low byte with data's type-extension nibble into a full type.
static inline unsigned short BLOCK_TYPE_FROM_GRID_DATA(int gridVal, int dataVal16) {
    return (gridVal & 0xFF) | ((dataVal16 & TYPE_EXT_MASK) >> 4);
}
// Extract just the dataVal (subtype/state) bits from a widened data value.
static inline unsigned short BLOCK_DATAVAL_FROM_DATA(int dataVal16) {
    return dataVal16 & DATAVAL_MASK;
}
// Pack a (possibly promoted) type's high bits and a dataVal back into a data[] value.
static inline unsigned short PACK_TYPE_EXT_AND_DATAVAL(int type, int dataVal) {
    return (unsigned short)((dataVal & DATAVAL_MASK) | (((type >> 8) & 0xF) << 12));
}

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
#define AZALEA_FIELD            0xA0

// TODO: also add error codes for true return errors, below, the -1, -2, etc.
// Really these should be in cache.h, I guess, since blockType is defined there. Oh well.
#define     NBT_VALID_BUT_EMPTY         0
#define     NBT_VALID_BLOCK             1
#define     NBT_NO_SECTIONS             2
#define		NBT_WARNING_NAME_NOT_FOUND	0x4
#define		NBT_WARNING_DIRECTORY_NOT_FOUND	0x8

// Errors
#define ERROR_GET_FILE_VERSION_DATA     -101
#define ERROR_GET_FILE_VERSION_VERSION  -102

// upper limit on these
#define     NUM_BLOCK_ENTITIES  (16 * 16 * 384)

// wraps gzFile and memory buffers with a consistent interface
typedef struct {
    int type;
    unsigned char* buf;
    int buflen;     // total size of buf (for BF_BUFFER bounds checking)
    int* offset;
    int _offset;
    gzFile gz;
    FILE* fptr;
    uint64_t skipBytesRemaining;
} bfFile;

typedef struct BlockEntity {
    unsigned char type;
    unsigned char zx;
    unsigned char y;
    unsigned char data;	// major and minor data in one byte
} BlockEntity;

typedef struct TranslationTuple {
    char* name;
    int type;
    bool useData;
    struct TranslationTuple* next;
} TranslationTuple;

bfFile newNBT(const wchar_t* filename, int* err);
int nbtGetBlocks(bfFile* pbf, unsigned char* buff, unsigned short* data, unsigned char* blockLight, unsigned char* biome, BlockEntity* entities, int* numEntities, int mcVersion, int minHeight, int maxHeight, int& mfsHeight, char* unknownBlock, int unknownBlockID);
int nbtGetHeights(bfFile* pbf, int & minHeight, int & maxHeight, int mcVersion);
int nbtGetSpawn(bfFile* pbf, int* x, int* y, int* z);
int nbtGetFileVersion(bfFile* pbf, int* version);
int nbtGetFileVersionId(bfFile* pbf, int* versionId);
// currently not used: int nbtGetFileVersionName(bfFile* pbf, char* versionName, int stringLength);
int nbtGetLevelName(bfFile* pbf, char* levelName, int stringLength);
int nbtGetPlayer(bfFile* pbf, int* px, int* py, int* pz);
int nbtGetPlayerDirect(bfFile* pbf, int* px, int* py, int* pz);
int nbtGetDimension(bfFile* pbf, int* dimension);
int nbtGetDimensionDirect(bfFile* pbf, int* dimension);
//void nbtGetRandomSeed(bfFile *pbf,long long *seed);
int nbtGetSchematicWord(bfFile* pbf, char* field, int* value);
int nbtGetSchematicBlocksAndData(bfFile* pbf, int numBlocks, unsigned char* schematicBlocks, unsigned short* schematicBlockData);
bool nbtGetValidatedSchematicVolume(int width, int height, int length, int* numBlocks);

// Read a Sponge Schematic v3 (.schem) file. Returns 1 on success, 0 on parse failure.
// On success, *outBlocks and *outData are malloc'd with `(*outWidth) * (*outHeight) * (*outLength)`
// entries each (*outBlocks: 1 byte/entry, *outData: 2 bytes/entry). The arrays use the same
// in-memory format as WorldBlock's grid[]/data[]: block ID's low 8 bits in *outBlocks, dataVal in
// the low 12 bits of *outData, and the block ID's bits 8-11 in *outData's top 4 bits (see
// BLOCK_TYPE_FROM_GRID_DATA). State-string properties (axis, facing, …) are currently ignored —
// only the base block name is recovered. Issue #40.
int nbtGetSpongeSchematic(bfFile* pbf,
    int* outWidth, int* outHeight, int* outLength,
    unsigned char** outBlocks, unsigned short** outData);
void nbtClose(bfFile* pbf);

int SlowFindIndexFromName(char* name);
void SetModTranslations(TranslationTuple* mt);

// Sponge Schematic v3 export (issue #40): build the canonical Minecraft block-state string
// for a Mineways internal (type, dataVal). Writes "minecraft:name[prop=val,...]" into `out`.
// Properties are emitted in alphabetical order as required by the v2 spec. Returns the
// number of bytes written (excluding the NUL), or -1 if the buffer was too small.
int spongeBuildBlockStateString(int type, int dataVal, char* out, int outSize);

// ---- Small public surface over BlockTranslations[] for the Culling Schemes feature ----
// The Culling editor needs to walk every Minecraft block-state name Mineways understands
// (one row per entry in BlockTranslations[]) and check / uncheck them by index. The runtime
// cull lookup needs to map (type, dataVal) → BlockTranslations index. We expose just those
// two pieces, so callers don't depend on the BlockTranslator struct layout.

// Returns the number of populated rows in BlockTranslations[] (i.e., the cap on valid indices).
int blockTransCount(void);

// Fills *outName with the row's name (ASCII, NOT-null pointer into a static C string).
// Returns true on success; false if idx is out of range or the row has no name.
bool blockTransNameAt(int idx, const char** outName);

// Maps a runtime (type, dataVal) pair to a BlockTranslations index, or -1 if no row matches.
// Thin wrapper over the existing findSpongeTranslator() reverse lookup.
int blockTransIndexFor(int type, int dataVal);
