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


#ifndef __MINEWAYS_MAP_H__
#define __MINEWAYS_MAP_H__

#include "blockInfo.h"
#include "biomes.h"

#ifndef WIN32
#define __declspec(a)
#define dllexport 0
#define __cdecl
#endif

#define CAVEMODE		0x0001
#define HIDEOBSCURED	0x0002
#define DEPTHSHADING	0x0004
#define LIGHTING		0x0008
#define HELL			0x0010
#define ENDER			0x0020
//#define SLIME			0x0040
#define SHOWALL         0x0080
#define BIOMES          0x0100

typedef void (*ProgressCallback)(float progress);

void SetHighlightState( int on, int minx, int miny, int minz, int maxx, int maxy, int maxz );
void GetHighlightState( int *on, int *minx, int *miny, int *minz, int *maxx, int *maxy, int *maxz );
void DrawMap(const wchar_t *world,double cx,double cz,int topy,int w,int h,double zoom,unsigned char *bits, Options opts, int hitsFound[3], ProgressCallback callback);
const char * IDBlock(int bx, int by, double cx, double cz, int w, int h, double zoom,int *ox,int *oy,int *oz,int *type,int *dataVal,int *biome);
void CloseAll();
WorldBlock * LoadBlock(wchar_t *directory,int bx,int bz);
void ClearBlockReadCheck();
int UnknownBlockRead();
void CheckUnknownBlock( int check );
int NeedToCheckUnknownBlock();
int GetSpawn(const wchar_t *world,int *x,int *y,int *z);
int GetFileVersion(const wchar_t *world,int *version);
void GetPlayer(const wchar_t *world,int *px,int *py,int *pz);
// palette should be in RGBA format, num colors in the palette
void SetMapPremultipliedColors();
void SetMapPalette(unsigned int *palette,int num);


#endif
