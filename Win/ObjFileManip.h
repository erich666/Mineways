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
#define MW_CHANGE_BLOCK_COMMAND_OUT_OF_BOUNDS		(1<<7)

// errors
#define MW_BEGIN_ERRORS                           (1<<8)

#define MW_NO_BLOCKS_FOUND                          (1<<8)
#define MW_ALL_BLOCKS_DELETED                       (1<<9)
#define MW_CANNOT_CREATE_FILE                       (1<<10)
#define MW_CANNOT_WRITE_TO_FILE                     (1<<11)
#define MW_IMAGE_WRONG_WIDTH                        (1<<12)
#define MW_NEED_16_ROWS                             (1<<13)
#define MW_DIMENSION_TOO_LARGE                      (1<<14)
#define MW_CANNOT_READ_IMPORT_FILE                  (1<<15)
#define MW_CANNOT_PARSE_IMPORT_FILE                 (1<<16)
#define MW_TEXTURE_TOO_LARGE						(1<<17)
#define MW_WORLD_EXPORT_TOO_LARGE					(1<<18)
#define MW_INTERNAL_ERROR							(1<<19)

#define MW_BEGIN_PNG_ERRORS                        (1<<20)

#define MW_CANNOT_READ_SELECTED_TERRAIN_FILE        (1<<20)
#define MW_CANNOT_CREATE_PNG_FILE                   (1<<21)

#define MW_ERRORS                                 (1<<21)

#define MW_NUM_CODES                                22

// scripts can override the various blocks with other blocks while exporting
typedef struct ChangeBlockCommand {
    // from range
    bool hasFrom;
    unsigned char simpleFromTypeBegin;
    unsigned char simpleFromTypeEnd;
    unsigned short simpleFromDataBits;
    bool useFromArray;
    // if useFromArray is true, fromDataBits contains 256 entry array of bits of blocks to change
    unsigned short *fromDataBitsArray;

    // to location - there is always only one
    bool hasInto;
    unsigned char intoType;
    unsigned char intoData;

    // location range
    bool hasLocation;	// if not set, whole export volume is used
    int minxVal;
    int minyVal;
    int minzVal;
    int maxxVal;
    int maxyVal;
    int maxzVal;
    struct ChangeBlockCommand *next;
} ChangeBlockCommand;


// translate the world version from https://minecraft.gamepedia.com/Data_version to a version number: 12, 13, 14
#define	DATA_VERSION_TO_RELEASE_NUMBER(worldVersion) ((worldVersion) <= 1343 ? 12 : ((worldVersion) <= 1631) ? 13 : 14)


void SetSeparatorObj(const wchar_t *separator);
void ChangeCache(int size);
void ClearCache();

int SaveVolume(wchar_t *objFileName, int fileType, Options *options, WorldGuide *gWorldGuide, const wchar_t *curDir, int minx, int miny, int minz, int maxx, int maxy, int maxz,
    ProgressCallback callback, wchar_t *terrainFileName, wchar_t *schemeSelected, FileList *outputFileList, int majorVersion, int minorVersion, int worldVersion, ChangeBlockCommand *pCBC);
int GetMinimumSelectionHeight(WorldGuide *pWorldGuide, Options *pOptions, int minx, int minz, int maxx, int maxz, bool expandByOne, bool ignoreTransparent, int maxy);

void WcharToChar(const wchar_t *inWString, char *outString, int maxlength);


//
//
//#ifdef __cplusplus
//extern "C" {
//#endif
//
//	__declspec(dllexport) void __cdecl ChangeCache( int size );
//	__declspec(dllexport) void __cdecl ClearCache();
//
//    __declspec(dllexport) int __cdecl SaveVolume( wchar_t *objFileName, int fileType, Options *options, const wchar_t *world, const wchar_t *curDir, int minx, int miny, int minz, int maxx, int maxy, int maxz,
//        ProgressCallback callback, wchar_t *terrainFileName, FileList *outputFileList, int majorVersion, int minorVersion );
//#ifdef __cplusplus
//}
//#endif

#endif
