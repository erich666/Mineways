/*
Copyright (c) 2011, Eric Haines
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


#ifndef __OBJ_FILE_H__
#define __OBJ_FILE_H__

#include "blockInfo.h"

#ifndef WIN32
#define __declspec(a)
#define dllexport 0
#define __cdecl
#endif

// If you change something here, you must also change gPopupInfo array in Mineways.cpp
#define MW_NO_ERROR 0
// informational
#define MW_WALLS_MIGHT_BE_THIN                      1
#define MW_SUM_OF_DIMENSIONS_IS_LOW                 (1<<1)
#define MW_TOO_MANY_POLYGONS                        (1<<2)
#define MW_MULTIPLE_GROUPS_FOUND                    (1<<3)
#define MW_AT_LEAST_ONE_DIMENSION_TOO_HIGH          (1<<4)
#define MW_UNKNOWN_BLOCK_TYPE_ENCOUNTERED           (1<<5)
#define MW_NOT_ENOUGH_ROWS                          (1<<6)

// errors
#define MW_BEGIN_ERRORS                           (1<<7)

#define MW_NO_BLOCKS_FOUND                          (1<<7)
#define MW_ALL_BLOCKS_DELETED                       (1<<8)
#define MW_CANNOT_READ_IMAGE_FILE                   (1<<9)
#define MW_CANNOT_CREATE_FILE                       (1<<10)
#define MW_CANNOT_WRITE_TO_FILE                     (1<<11)
#define MW_IMAGE_WRONG_WIDTH                        (1<<12)
#define MW_DIMENSION_TOO_LARGE                      (1<<13)
#define MW_NEED_16_ROWS                             (1<<14)
#define MW_CANNOT_READ_IMPORT_FILE                  (1<<15)
#define MW_CANNOT_PARSE_IMPORT_FILE                 (1<<16)
#define MW_ERRORS                                 (1<<16)

#define MW_NUM_CODES                                17

#ifdef __cplusplus
extern "C" {
#endif

	__declspec(dllexport) void __cdecl ChangeCache( int size );
	__declspec(dllexport) void __cdecl ClearCache();

    __declspec(dllexport) int __cdecl SaveVolume( wchar_t *objFileName, int fileType, Options *options, const wchar_t *world, const wchar_t *curDir, int minx, int miny, int minz, int maxx, int maxy, int maxz,
        ProgressCallback callback, wchar_t *terrainFileName, FileList *outputFileList, int majorVersion, int minorVersion );

    // palette should be in RGBA format, num colors in the palette
	__declspec(dllexport) void __cdecl SetExportPremultipliedColors();
    __declspec(dllexport) void __cdecl SetExportPalette(unsigned int *palette,int num);
#ifdef __cplusplus
}
#endif

#endif
