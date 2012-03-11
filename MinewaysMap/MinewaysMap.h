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

#ifndef WIN32
#define __declspec(a)
#define dllexport 0
#define __cdecl
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CAVEMODE		0x01
#define HIDEOBSCURED	0x02
#define DEPTHSHADING	0x04
#define LIGHTING		0x08
#define HELL			0x10
#define ENDER			0x20
#define SLIME			0x40
#define SHOWALL         0x80

typedef void (*ProgressCallback)(float progress);

    __declspec(dllexport) void __cdecl SetHighlightState( int on, int minx, int miny, int minz, int maxx, int maxy, int maxz );
    __declspec(dllexport) void __cdecl GetHighlightState( int *on, int *minx, int *miny, int *minz, int *maxx, int *maxy, int *maxz );
    __declspec(dllexport) void __cdecl DrawMap(const wchar_t *world,double cx,double cz,int y,int w,int h,double zoom,unsigned char *bits, Options opts, int hitsFound[3], ProgressCallback callback);
    __declspec(dllexport) const char * __cdecl IDBlock(int bx, int by, double cx, double cz, int w, int h, double zoom,int *ox,int *oy,int *oz,int *type);
    __declspec(dllexport) void __cdecl CloseAll();
    __declspec(dllexport) WorldBlock * __cdecl LoadBlock(wchar_t *directory,int bx,int bz);
    __declspec(dllexport) int __cdecl GetSpawn(const wchar_t *world,int *x,int *y,int *z);
    __declspec(dllexport) void __cdecl GetPlayer(const wchar_t *world,int *px,int *py,int *pz);
    __declspec(dllexport) void __cdecl GetRandomSeed(const wchar_t *world,long long* pseed);
    
    // palette should be in RGBA format, num colors in the palette
    __declspec(dllexport) void __cdecl SetMapPalette(unsigned int *palette,int num);
#ifdef __cplusplus
}
#endif

#endif
