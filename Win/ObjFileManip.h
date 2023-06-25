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

#pragma once

#ifndef WIN32
#define __declspec(a)
#define dllexport 0
#define __cdecl
#endif

#include "vector.h"
#include "blockinfo.h"
#include "rwpng.h"

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
#define MW_TEXTURE_RESOLUTION_HIGH          		(1<<8)

// errors
#define MW_BEGIN_ERRORS                           (1<<9)

#define MW_NO_BLOCKS_FOUND                          (1<<9)
#define MW_ALL_BLOCKS_DELETED                       (1<<10)
#define MW_CANNOT_CREATE_FILE                       (1<<11)
#define MW_CANNOT_WRITE_TO_FILE                     (1<<12)
#define MW_IMAGE_WRONG_WIDTH                        (1<<13)
#define MW_NEED_16_ROWS                             (1<<14)
#define MW_DIMENSION_TOO_LARGE                      (1<<15)
#define MW_CANNOT_READ_IMPORT_FILE                  (1<<16)
#define MW_CANNOT_PARSE_IMPORT_FILE                 (1<<17)
#define MW_TEXTURE_TOO_LARGE						(1<<18)
#define MW_WORLD_EXPORT_TOO_LARGE					(1<<19)
#define MW_CANNOT_CREATE_DIRECTORY                  (1<<20)
#define MW_INTERNAL_ERROR							(1<<21)

#define MW_BEGIN_PNG_ERRORS                       (1<<22)

#define MW_CANNOT_READ_SELECTED_TERRAIN_FILE        (1<<22)
#define MW_CANNOT_CREATE_PNG_FILE                   (1<<23)

#define MW_ERRORS                                 (1<<23)

#define MW_NUM_CODES                                24

// scripts can override the various blocks with other blocks while exporting
typedef struct ChangeBlockCommand {
    // from range
    bool hasFrom;
    unsigned char simpleFromTypeBegin;
    unsigned char simpleFromTypeEnd;
    unsigned short simpleFromDataBits;
    bool useFromArray;
    // if useFromArray is true, fromDataBits contains 256 entry array of bits of blocks to change
    unsigned short* fromDataBitsArray;

    // to location - there is always only one
    bool hasInto;
    unsigned short intoType;
    unsigned char intoData;

    // location range
    bool hasLocation;	// if not set, whole export volume is used
    int minxVal;
    int minyVal;
    int minzVal;
    int maxxVal;
    int maxyVal;
    int maxzVal;
    struct ChangeBlockCommand* next;
} ChangeBlockCommand;


#define TOTAL_CATEGORIES	6
#define	CATEGORY_RGBA		0
#define	CATEGORY_NORMALS	1
#define	CATEGORY_METALLIC	2
#define	CATEGORY_EMISSION	3
#define	CATEGORY_ROUGHNESS	4
#define CATEGORY_SPECULAR   5

static int gCatChannels[TOTAL_CATEGORIES] = { 4, 3, 1, 1, 1, 1 };
static LodePNGColorType gCatFormat[TOTAL_CATEGORIES] = { LCT_RGBA, LCT_RGB, LCT_GREY, LCT_GREY, LCT_GREY, LCT_GREY };
static const wchar_t* gCatSuffixes[TOTAL_CATEGORIES] = { L"", L"_n", L"_m", L"_e", L"_r", L"_s" };
// OBJ output is assumed to be in non-wide characters - TODO I wonder if that's required by other programs?
static const char* gCatStrSuffixes[TOTAL_CATEGORIES] = { "", "_n", "_m", "_e", "_r", "_s" };

// We store a set of normals that get reused: 
// 26 predefined, plus 30 known to be needed for 196 blocks, plus another 400 for water and lava and just in case.
// Extra normals are from torches, levers, brewing stands, and sunflowers
#define NORMAL_LIST_SIZE (26+30+400)
// the UVs appears in the range [0-16],[0-16] within the 16x16 tiles.
#define NUM_UV_GRID_RESOLUTION	16
// Maximum size of a simplified area. We could crank this higher, but doing so didn't gain much:
// on a typical export, it cut 0.12% from the number of quads saved. Reducing to 16 caused 0.4% more
// quads to be created. So 32 seems reasonable.
#define SIMPLIFY_MAX_DIMENSION  32

typedef struct UVRecord
{
    float u;
    float v;
    int index;
} UVRecord;

typedef struct UVList
{
    int size;
    int count;
    UVRecord* records;
} UVList;

typedef struct UVOutput
{
    float uc;
    float vc;
    int swatchLoc;	// where this record is stored, for purposes of outputting comments and sorting
} UVOutput;


typedef struct FaceRecord {
    int faceIndex;	// tie breaker, so that faces get near each other in location
    int vertexIndex[4];
    short materialType;	// block id; note we use negatives for some things
    unsigned short materialDataVal;	// extended data value, identifying unique block materials
    short normalIndex;    // always the same! normals all the same for the face - shared, so need only around 335
    short uvIndex[4];	// around 3800 unique values, since we use sharing.
} FaceRecord;

#define FACE_RECORD_POOL_SIZE 10000

typedef struct FaceRecordPool {
    FaceRecord fr[FACE_RECORD_POOL_SIZE];
    struct FaceRecordPool* pPrev;
    int count;
} FaceRecordPool;

typedef struct SimplifyFaceRecord {
    FaceRecord* pFace;  // original record with most of the data; the rest is really for sorting faster
    // TODO: we could cut way down on this data below and derive it on the fly each sort compare, but that sounds super-slow...
    int normalDirection;
    float normalDistance;
    // Note: these could likely be shorts (have to be a little careful of the range, as some worlds are huge), but in a test
    // of Block Test World I found it saved all of a megabyte to do so. Might be slightly faster with shorts, but leave it be.
    float xll;
    float yll;
    SimplifyFaceRecord* pXneighborSFR;
    SimplifyFaceRecord* pYneighborSFR;
} SimplifyFaceRecord;

#define SIMPLIFY_FACE_RECORD_POOL_SIZE 10000

typedef struct SimplifyFaceRecordPool {
    SimplifyFaceRecord sfr[SIMPLIFY_FACE_RECORD_POOL_SIZE];
    struct SimplifyFaceRecordPool* pNext;
    int count;
} SimplifyFaceRecordPool;

typedef struct SwatchComposite {
    int swatchLoc;
    int backgroundSwatchLoc;
    int angle;
    int compositeSwatchLoc;
    struct SwatchComposite* next;
} SwatchComposite;

typedef struct BlockInstance {
    int hash;
    int faceNumber; // first face in list of faces
    int numFaces;   // in theory not needed if we don't sort the list, but could be useful
    int startingLocation;
} BlockInstance;

typedef struct InstanceLocation {
    int index;  // index into array of BlockInstances
    float location[3];
} InstanceLocation;

typedef struct Model {
    float scale;    // size of a block, in meters
    Point center;

    // the standard normals, already rotated into position.
    // first six are the block directions
    // next four are for diagonal billboards
    // next eight are for angled tracks
    // next eight are for the angled signs and banners. Whew.
    Vector normals[NORMAL_LIST_SIZE];
    int normalListCount;

    Point* vertices;    // vertices to be output, in a given order
    // a little indirect: there is one of these for every grid *corner* location.
    // The index is therefore a block location, possibly +1 in X, Y, and Z
    // (use gFaceToVertexOffset[face][corner 0-3] to get these offsets)
    // What is returned is the index into the vertices[] array itself, where to
    // find the vertex information.
    int* vertexIndices;
    int vertexCount;    // lowest unused vertex index;
    int vertexListSize;

    // One for each SwatchLoc - each UVList potentially contains a list of UVs associated with this particular swatch.

    UVList uvSwatches[NUM_MAX_SWATCHES];
    int uvIndexCount;
    // points into uv Records actually stored at the swatch locations
    UVOutput* uvIndexList;
    // a 17 x 17 grid of all the possible UV locations in a tile
    int uvGridListCount;	// number of locations used in uvGridList
    int uvGridList[(NUM_UV_GRID_RESOLUTION + 1) * (NUM_UV_GRID_RESOLUTION + 1)];
    int uvIndexListSize;
    // For each swatchLoc there is some type (often more than one, but we save just the last one).
    // This lets us export the type as a comment.
    int uvSwatchToType[NUM_MAX_SWATCHES];

    int billboardCount;
    IBox billboardBounds;

    FaceRecord** faceList;
    int faceCount;
    int faceSize;
    int triangleCount;	// the number of true triangles output - currently just sloped rail sides

    // The absolute maximum number of materials possible. It's actually much smaller than this, as data values
    // do not usually generate sub-materials, but we're playing it safe.
#define NUM_SUBMATERIALS	2000
    unsigned int mtlList[NUM_SUBMATERIALS];
    bool tileList[TOTAL_CATEGORIES][TOTAL_TILES];
    int tileListCount; // number of tiles actually used in tileList
    bool tileEmissionNeeded[TOTAL_TILES];
    int mtlCount;

    progimage_info* pInputTerrainImage[TOTAL_CATEGORIES];
    bool terrainImageNotFound;

    int textureResolution;  // size of output texture
    float invTextureResolution; // inverse, commonly used
    int terrainWidth;   // width of input image (needed for noisy textures, when an image is not actually read in)
    int tileSize;    // number of pixels in a tile, e.g. 16 by default, could be 64, 256, or other power of two
    float resScale;     // scale factor, 16 divided by tile size
    int verticalTiles;	// number of rows of tiles, e.g. a 16x19 set of tiles gives 19
    int swatchSize;  // number of pixels in a swatch, e.g. 18
    int swatchesPerRow;  // a swatch is a tile with a +1 border (or SWATCH_BORDER) around it
    float textureUVPerSwatch; // how much a swatch spans in UV space
    float textureUVPerTile; // how much the interior tile spans in UV space
    int swatchCount;        // total number of swatches generated
    int swatchListSize;         // the absolute maximum number of swatches available
    SwatchComposite* swatchCompositeList;   // additional swatches of combinations of two types
    SwatchComposite* swatchCompositeListEnd;   // last one on list
    progimage_info* pPNGtexture;
    int usesRGB;    // 1 if the RGB (only) texture is used and so should be output
    int usesRGBA;   // 1 if the RGBA texture is used
    int usesAlpha;   // 1 if the Alpha-only texture is used
    FaceRecordPool* faceRecordPool;
    SimplifyFaceRecordPool* simplifyFaceRecordPool;
    SimplifyFaceRecordPool* headSimplifyFaceRecordPool;
    int *simplifyUVGridList;
    int simplifyFaceSavings;
    int simplifyVertexSavings;
    bool dontRandomizeRotations;    // hidden variable, for testing simplification: if set, turns off random rotations/flips. TODO: expose in script someday?
    bool alreadyAskedForComposite;
    int mcVersion;	// 12 for 1.12.2 and earlier, 13 for 1.13 and later

    Options* options;
    bool print3D;
    int exportTexture;
    int exportTiles;
    bool customMaterial;
    bool exportMDL;
    bool singleSided; // True means : "the single-sided polygon must be output twice, once reversed".
    bool emitterSingleSided;
    int blockCount;
    bool instancing; // true for USDA with instancing output enabled
    int instanceCount;
    int instanceListSize;
    BlockInstance* instance;
    int instanceLocCount;
    int instanceLocListSize;
    InstanceLocation* instanceLoc;
    int instanceChunkSize;  // what size of chunks should instances be gathered into?
    int biomeIndex;  // biome index used to color the export textures
    int groupCount;
    int groupCountSize;
    int* groupCountArray;
} Model;

extern Model gModel;


// translate the world version from https://minecraft.gamepedia.com/Data_version to a version number: 12, 13, 14, 15, 16, 17, 18, 19...
#define	DATA_VERSION_TO_RELEASE_NUMBER(worldVersion) ((worldVersion) < 100 ? 8 : \
                                                     ((worldVersion) <= 184) ? 9 : \
                                                     ((worldVersion) <= 512) ? 10 : \
                                                     ((worldVersion) <= 922) ? 11 : \
                                                     ((worldVersion) <= 1343) ? 12 : \
                                                     ((worldVersion) <= 1631) ? 13 : \
                                                     ((worldVersion) <= 1976) ? 14 : \
                                                     ((worldVersion) <= 2230) ? 15 : \
                                                     ((worldVersion) <= 2586) ? 16 : \
                                                     ((worldVersion) <= 2730) ? 17 : \
                                                     ((worldVersion) <= 2975) ? 18 : \
                                                     ((worldVersion) <= 3442) ? 19 : \
                                                     20)


void SetSeparatorObj(const wchar_t* separator);
void ChangeCache(int size);
void ClearCache();

int SaveVolume(wchar_t* objFileName, int fileType, Options* options, WorldGuide* gWorldGuide, const wchar_t* curDir, int minx, int miny, int minz, int maxx, int maxy, int maxz, int mapMinHeight, int mapMaxHeight,
    ProgressCallback callback, wchar_t* terrainFileName, wchar_t* schemeSelected, FileList* outputFileList, int majorVersion, int minorVersion, int worldVersion, ChangeBlockCommand* pCBC, int instanceChunkSize,
    int& biomeIndex, int& groupCount, int groupCountSize, int* groupCountArray);

int GetMinimumSelectionHeight(WorldGuide* pWorldGuide, Options* pOptions, int minx, int minz, int maxx, int maxz, int mapMinHeight, int mapMaxHeight, bool expandByOne, bool ignoreTransparent, int maxy);

void WcharToChar(const wchar_t* inWString, char* outString, int maxlength);
void StripLastString(const wchar_t* src, wchar_t* path, wchar_t* piece);
wchar_t* RemoveGivenPath(wchar_t* src, wchar_t* path);
void EnsureSuffix(wchar_t* dst, const wchar_t* src, const wchar_t* suffix);

bool IsASubblock(int type, int dataVal);

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
