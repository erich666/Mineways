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

enum {BF_BUFFER, BF_GZIP};

// wraps gzFile and memory buffers with a consistent interface
typedef struct {
    int type;
    unsigned char *buf;
    int *offset;
    int _offset;
    gzFile gz;
} bfFile;

typedef struct BlockEntity {
	unsigned char type;
	unsigned char zx;
	unsigned char y;
	unsigned char data;	// major and minor data in one byte
} BlockEntity;

bfFile newNBT(const wchar_t *filename);
int nbtGetBlocks(bfFile bf, unsigned char *buff, unsigned char *data, unsigned char *blockLight, unsigned char *biome, BlockEntity *entities, int *numEntities);
void nbtGetSpawn(bfFile bf,int *x,int *y,int *z);
void nbtGetFileVersion(bfFile bf, int *version);
void nbtGetFileVersionId(bfFile bf, int *versionId);
void nbtGetFileVersionName(bfFile bf, char *versionName, int stringLength);
void nbtGetLevelName(bfFile bf, char *levelName, int stringLength);
void nbtGetPlayer(bfFile bf,int *px,int *py,int *pz);
//void nbtGetRandomSeed(bfFile bf,long long *seed);
int nbtGetSchematicWord(bfFile bf, char *field, int *value);
int nbtGetSchematicBlocksAndData(bfFile bf, int numBlocks, unsigned char *schematicBlocks, unsigned char *schematicBlockData);
void nbtClose(bfFile bf);

#endif
