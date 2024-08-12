/*
Copyright (c) 2010, Sean Kasun
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

#pragma once

#include "tiles.h"
#include "blockInfo.h"
#include "biomes.h"

#ifndef WIN32
#define __declspec(a)
#define dllexport 0
#define __cdecl
#endif

#define CAVEMODE		    0x0001
#define HIDEOBSCURED	    0x0002
#define DEPTHSHADING	    0x0004
#define LIGHTING		    0x0008
#define HELL			    0x0010
#define ENDER			    0x0020
//#define SLIME			    0x0040
#define SHOWALL             0x0080
#define BIOMES              0x0100
#define TRANSPARENT_WATER	0x0200
#define MAP_GRID        	0x0400

typedef void (*ProgressCallback)(float progress, wchar_t* buf);

#define WORLD_UNLOADED_TYPE		0
#define WORLD_LEVEL_TYPE		1
#define WORLD_TEST_BLOCK_TYPE	2
#define WORLD_SCHEMATIC_TYPE	3

// nothing found in location
#define EMPTY_HEIGHT    -999

typedef struct Schematic {
    unsigned char* blocks;
    unsigned char* data;
    int width;	// X
    int height; // Y
    int length;	// Z
    int numBlocks;	// width * height * length
    bool repeat;	// should the object be repeated in the map?
} Schematic;

typedef struct WorldGuide {
    unsigned int type;
    wchar_t world[520];		// 2*MAX_PATH
    wchar_t directory[520];
    Schematic sch;
    int nbtVersion;
    bool isServerWorld;
    int minHeight;
    int maxHeight;
} WorldGuide;

typedef struct HighlightBox {
    int highlightUsed;
    int minX;
    int minY;
    int minZ;
    int maxX;
    int maxY;
    int maxZ;
} HighlightBox;

// push values
#define HIGHLIGHT_UNDO_IGNORE   0
#define HIGHLIGHT_UNDO_PUSH     1
#define HIGHLIGHT_UNDO_CLEAR    2

void SetSeparatorMap(const wchar_t* separator);
void SaveHighlightState();
bool UndoHighlightExists();
void UndoHighlight();
void SetHighlightState(int on, int minx, int miny, int minz, int maxx, int maxy, int maxz, int mapMinHeight, int mapMaxHeight, int push);
void GetHighlightState(int* on, int* minx, int* miny, int* minz, int* maxx, int* maxy, int* maxz, int mapMinHeight);
int DrawMapToArray(unsigned char* image, WorldGuide* pWorldGuide, int cx, int cz, int topy, int mapMaxY, int w, int h, int zoom, Options* pOpts, int* hitsFound, ProgressCallback callback, int mcVersion, int versionID);
int DrawMap(WorldGuide* pWorldGuide, double cx, double cz, int topy, int mapMaxY, int w, int h, double zoom, unsigned char* bits, Options* pOpts, int hitsFound[3], ProgressCallback callback, int mcVersion, int versionID);
const char* IDBlock(int bx, int by, double cx, double cz, int w, int h, int yOffset, double zoom, int* ox, int* oy, int* oz, int* type, int* dataVal, int* biome, bool schematic);
const char* RetrieveBlockSubname(int type, int dataVal); //, WorldBlock* block = NULL, int xoff = 0, int y = 0, int zoff = 0);
void CloseAll();
WorldBlock* LoadBlock(WorldGuide* pWorldGuide, int bx, int bz, int mcVersion, int versionID, int& retCode);
void GetChunkHeights(WorldGuide* pWorldGuide, int& minHeight, int& maxHeight, int mcVersion, int mx, int mz);
void ClearBlockReadCheck();
int UnknownBlockRead();
void CheckUnknownBlock(int check);
int NeedToCheckUnknownBlock();
int GetSpawn(const wchar_t* world, int* x, int* y, int* z);
int GetFileVersion(const wchar_t* world, int* version, wchar_t* fileOpened, rsize_t size);
int GetFileVersionId(const wchar_t* world, int* versionId);
// currently not used: int GetFileVersionName(const wchar_t* world, char* versionName, int stringLength);
int GetLevelName(const wchar_t* world, char* levelName, int stringLength);
int GetPlayer(const wchar_t* world, int* px, int* py, int* pz, int* dimension);
int GetSchematicWord(const wchar_t* schematic, char* field, int* word);
int GetSchematicBlocksAndData(const wchar_t* schematic, int numBlocks, unsigned char* schematicBlocks, unsigned char* schematicBlockData);
// palette should be in RGBA format, num colors in the palette
void SetMapPremultipliedColors(int start);
void SetMapPalette(unsigned int* palette, int num);
char* MapUnknownBlockName();
void ClearUnknownBlockNameString();
void SetUnknownBlockID(int val);
int GetUnknownBlockID();
void GetBadChunkLocation(int* bx, int* bz);