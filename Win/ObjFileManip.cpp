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


// ObjFileManip.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "tiles.h"
#include "rwpng.h"
#include "vector.h"
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <vector>

// Set to a tiny number to have front and back faces of billboards be separated a bit.
// TODO: currently works only for those billboards made by using the various multitile calls,
// not by the traditional billboard calls.
//#define STOP_Z_FIGHTING	(0.0002f/2.0f)
// feature currently disabled, G3D was fixed so this is no longer an issue.
#define STOP_Z_FIGHTING	0.0f

// If the model is going to undergo a transform, we don't know the normal of the surface,
// so figure out the normal at the end of the run.
#define COMPUTE_NORMAL  -1

static PORTAFILE gModelFile;
static PORTAFILE gMtlFile;
static PORTAFILE gPngFile;  // for terrainExt.png input (not texture output)

#define MINECRAFT_SINGLE_MATERIAL "MC_material"

#define NO_GROUP_SET 0
#define BOUNDARY_AIR_GROUP 1

#define GENERIC_MATERIAL -1

typedef struct BoxCell {
    int group;	// for 3D printing, what connected group a block is part of
    unsigned char type;
    unsigned char origType;
    unsigned char flatFlags;	// pointer to which origType to use for face output, for "merged" snow, redstone, etc. in cell above
    unsigned char data;     // extra data for block (wool color, etc.); note that four top bits are not used
} BoxCell;

typedef struct BoxGroup 
{
    int groupID;	// which group number am I? Always matches index of gGroupInfo array
    int population;	// how many in the group?
    int solid;		// solid or air group?
    IBox bounds;	// the box that this group occupies. Not valid if population is 0 (merged)
} BoxGroup;

static BoxCell *gBoxData = NULL;
static unsigned char *gBiome = NULL;
static IPoint gBoxSize;
static int gBoxSizeYZ = -999;
static int gBoxSizeXYZ = -999;
// the box bounds of gBoxData that has something in it, before processing
static IBox gSolidBox;
// the box bounds of gBoxData that has something in it, +1 in all directions for air
// Basically, gSolidBox + 1 in all directions, but generated once for readability
static IBox gAirBox;
// Dimensions of what truly has stuff in it, after all processing is done and we're ready to write
static Point gFilledBoxSize;    // in centimeters

static IBox gSolidWorldBox;  // area of solid box in world coordinates
static IPoint gWorld2BoxOffset;

typedef struct FaceRecord {
    int type;	// block id
    int faceIndex;	// tie breaker, so that faces get near each other in location
    int vertexIndex[4];
    int normalIndex;    // always the same! normals all the same for the face
    int uvIndex[4];
} FaceRecord;

#define FACE_RECORD_POOL_SIZE 10000

typedef struct FaceRecordPool {
    FaceRecord fr[FACE_RECORD_POOL_SIZE];
    struct FaceRecordPool *pPrev;
    int count;
} FaceRecordPool;

typedef struct SwatchComposite {
    int swatchLoc;
    int backgroundSwatchLoc;
    int angle;
    int compositeSwatchLoc;
    struct SwatchComposite *next;
} SwatchComposite;

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
    UVRecord *records;
} UVList;

typedef struct UVOutput
{
    float uc;
    float vc;
    int swatchLoc;	// where this record is stored, just for purposes of outputting comments
} UVOutput;

// 26 predefined, plus 30 known to be needed for 196 blocks, plus another 400 for water and lava and just in case.
// extra normals are from torches, levers, brewing stands, and sunflowers
#define NORMAL_LIST_SIZE (26+30+400)

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

    Point *vertices;    // vertices to be output, in a given order
    // a little indirect: there is one of these for every grid *corner* location.
    // The index is therefore a block location, possibly +1 in X, Y, and Z
    // (use gFaceToVertexOffset[face][corner 0-3] to get these offsets)
    // What is returned is the index into the vertices[] array itself, where to
    // find the vertex information.
    int *vertexIndices;
    int vertexCount;    // lowest unused vertex index;
    int vertexListSize;

    // One for each SwatchLoc - each UVList potentially contains a list of UVs associated with this particular swatch.
    // During output of the 
    UVList uvSwatches[NUM_MAX_SWATCHES];
    int uvIndexCount;
    // points into uv Records actually stored at the swatch locations
    UVOutput *uvIndexList;
    int uvIndexListSize;
    // For each swatchLoc there is some type (often more than one, but we save just the last one).
    // This lets us export the type as a comment.
    int uvSwatchToType[NUM_MAX_SWATCHES];

    int billboardCount;
    IBox billboardBounds;

    FaceRecord **faceList;
    int faceCount;
    int faceSize;
    int triangleCount;	// the number of true triangles output - currently just sloped rail sides

    int mtlList[NUM_BLOCKS];
    int mtlCount;

    progimage_info *pInputTerrainImage;

    int textureResolution;  // size of output texture
    float invTextureResolution; // inverse, commonly used
    int tileSize;    // number of pixels in a tile, e.g. 16
    int verticalTiles;	// number of rows of tiles, e.g. a 16x19 set of tiles gives 19
    int swatchSize;  // number of pixels in a swatch, e.g. 18
    int swatchesPerRow;  // a swatch is a tile with a +1 border (or SWATCH_BORDER) around it
    float textureUVPerSwatch; // how much a swatch spans in UV space
    float textureUVPerTile; // how much the interior tile spans in UV space
    int swatchCount;        // total number of swatches generated
    int swatchListSize;         // the absolute maximum number of swatches available
    SwatchComposite *swatchCompositeList;   // additional swatches of combinations of two types
    SwatchComposite *swatchCompositeListEnd;   // last one on list
    progimage_info *pPNGtexture;
    int usesRGB;    // 1 if the RGB (only) texture is used and so should be output
    int usesRGBA;   // 1 if the RGBA texture is used
    int usesAlpha;   // 1 if the Alpha-only texture is used
    FaceRecordPool *faceRecordPool;
    bool alreadyAskedForComposite;
} Model;

static Model gModel;

typedef struct CompositeSwatchPreset
{
    int cutoutSwatch;
    int backgroundSwatch;
} CompositeSwatchPreset;

typedef struct FillAlpha {
    int cutout;
    int underlay;
} FillAlpha;

// The size of the border added to a tile to make a swatch. So a 16x16 tile makes an 18x18 swatch.
// The whole point here is simple: bilinear interpolation is done on swatches when rendering textures
// (unless your renderer is cool enough to allow you to turn it off and so get the block Minecraft look).
// To avoid bleeding colors from one tile to another, we have to add a 1 pixel border around the tile
// to make a swatch. Note that this border does not help if mipmapping is on: far enough away, the swatches
// will bleed through to each other. Increase the swatch border to avoid this problem. The downside is that
// a larger border will use up more swatches, so you could run out of swatches made on the fly for composites.
// A composite is something like, say, a ladder or torch put on top of a background swatch, like grass or
// stone. We make these on the fly as needed and add them to the end of the texture, until we run out.
// NOTE! This is not really supported to increase to 2 or more. There needs to be additional code to clamp,
// to run a loop that copies a particular row or column again and again along the edge. Search for SWATCH_BORDER
// and you'll see where the clamps occur.
#define SWATCH_BORDER 1

#define SWATCH_TO_COL_ROW( s, c, r ) \
    (c) = (s) % gModel.swatchesPerRow; \
    (r) = (s) / gModel.swatchesPerRow; \

static int gSolidGroups = -999;
static int gAirGroups = -999;

static int gGroupListSize = 0;
static BoxGroup *gGroupList = NULL;
static int gGroupCount = -999;

// offsets in box coordinates to the neighboring faces
static int gFaceOffset[6];

static ProgressCallback *gpCallback;

static Options *gOptions;

static FileList *gOutputFileList;

static int gExportTexture=0;
// whether we're outputting a print or render
static int gPrint3D=0;

static int gPhysMtl;

static float gUnitsScale=1.0f;

static int gExportBillboards=0;

static int gMajorVersion=0;
static int gMinorVersion=0;

static int gBadBlocksInModel=0;

// If set, the current faces being output will (probably) be transformed later.
// This is important to know for merging faces: if faces are to later be rotated, etc.,
// then their geometric coordinates cannot be used for seeing if the face should be removed
// because it is neighboring something that covers it.
// This is a global hack, but I didn't want to add this variable *everywhere* when it's usually 0.
// Basically, if you are going to transform geometry being created to a new spot, set this to true
// before creating the geometry, then false after.
static int gUsingTransform=0;

// extra face directions, for normals
#define DIRECTION_LO_X_LO_Z 6
#define DIRECTION_LO_X_HI_Z 7
#define DIRECTION_HI_X_LO_Z 8
#define DIRECTION_HI_X_HI_Z 9

// for rails
#define DIRECTION_LO_X_LO_Y 10
#define DIRECTION_LO_Z_LO_Y 11
#define DIRECTION_HI_X_LO_Y 12
#define DIRECTION_HI_Z_LO_Y 13
#define DIRECTION_LO_X_HI_Y 14
#define DIRECTION_LO_Z_HI_Y 15
#define DIRECTION_HI_X_HI_Y 16
#define DIRECTION_HI_Z_HI_Y 17

// for rotUV, flips LO face
#define FLIP_X_FACE_VERTICALLY	0x01
#define FLIP_Z_FACE_VERTICALLY	0x02
#define ROTATE_TOP_AND_BOTTOM	0x04

#define OSQRT2 0.707106781f
#define OCOS22P5DEG 0.92387953251f
#define OSIN22P5DEG 0.38268343236f

// types of billboards
#define BB_FULL_CROSS   1
#define BB_GRID         2
#define BB_TORCH        3
#define BB_RAILS        4
#define BB_FIRE         5
#define BB_SIDE			6


#define SWATCH_INDEX( col, row ) (NUM_BLOCKS + (col) + (row)*16)

// these are swatches that we will use for other things;
// The swatches reused are the "breaking block" animations, which we'll never need
#define TORCH_TOP               SWATCH_INDEX( 0,15 )
#define RS_TORCH_TOP_ON         SWATCH_INDEX( 1,15 )
#define RS_TORCH_TOP_OFF        SWATCH_INDEX( 2,15 )
#define REDSTONE_WIRE_DOT		SWATCH_INDEX( 4,11 )
#define REDSTONE_WIRE_ANGLED_2  SWATCH_INDEX( 3,15 )
#define REDSTONE_WIRE_3         SWATCH_INDEX( 4,15 )
// these spots are used for compositing, as temporary places to put swatches to edit
// TODO - make separate hunks of memory that don't get output.
#define SWATCH_WORKSPACE        SWATCH_INDEX( 8, 2 )


wchar_t gOutputFilePath[MAX_PATH];
wchar_t gOutputFileRoot[MAX_PATH];
wchar_t gOutputFileRootClean[MAX_PATH]; // used by all files that are referenced inside text files
char gOutputFileRootCleanChar[MAX_PATH];

// how many blocks are needed to make a thick enough wall
static int gWallBlockThickness = -999;
// how many is the user specifying for hollow walls
static int gHollowBlockThickness = -999;

static int gBlockCount = -999;

static int gMinorBlockCount = -999;

static int gDebugTransparentType = -999;

static long gMySeed = 12345;


// these should not be relied on for much of anything during processing,
// the fields are filled out until writing.
typedef struct ExportStatistics {
    int numBlocks;
    int numGroups;
    int numSolidGroups;
    int numAirGroups;
    int bubblesFound;
    int solidGroupsMerged;
    int numberManifoldPasses;
    int nonManifoldEdgesFound;
    int blocksManifoldWelded;
    int blocksCornertipWelded;
    int blocksHollowed;
    int blocksSuperHollowed;
    int floaterGroupsDeleted;
    int blocksFloaterDeleted;
    float density;  // value 0 to 1, number of blocks filled.
} ExportStatistics;

static ExportStatistics gStats;

typedef struct TypeTile {
    int type;	// block id
    int col;    // location on terrainExt.png
    int row;
    unsigned char colorReplace[3]; // if not 0,0,0, then use this color to replace any lookup color (spruce and birch leaves, basically)
} TypeTile;


// given a face direction and a vertex number 0-3, give the relative vertex location (offset of 0 or 1) for X, Y, Z
static int gFaceToVertexOffset[6][4][3] =
{
    {
        {0,0,0},{0,0,1},{0,1,1},{0,1,0}	// -X
    },
    {
        {1,0,1},{0,0,1},{0,0,0},{1,0,0}	// -Y
    },
    {
        {1,0,0},{0,0,0},{0,1,0},{1,1,0}	// -Z
    },
    {
        {1,0,1},{1,0,0},{1,1,0},{1,1,1}	// +X.
    },
    {
        {0,1,1},{1,1,1},{1,1,0},{0,1,0}	// +Y
    },
    {
        {0,0,1},{1,0,1},{1,1,1},{0,1,1}	// +Z
    }
};

static int gFaceDirectionVector[6][3] =
{
    {-1,0,0},{0,-1,0},{0,0,-1},
    { 1,0,0},{0, 1,0},{0,0, 1}
};

#define WERROR(x) if(x) { assert(0); PortaClose(gModelFile); return MW_CANNOT_WRITE_TO_FILE; }


// feed world coordinate in to get box index: in our coordinate system, X is dominant, Z is next, Y is weakest.
// So to go up by 1 in Y, simply add 1. To go up 1 in Z, add gBoxSize[Y]. To go up 1 in X, add gBoxSizeYZ.
#define WORLD_TO_BOX_INDEX(x,y,z) (((x)+gWorld2BoxOffset[X])*gBoxSizeYZ + ((z)+gWorld2BoxOffset[Z])*gBoxSize[Y] + (y)+gWorld2BoxOffset[Y])

// feed relative XYZ indices inside box to get index number
#define BOX_INDEXV(pt)	((pt)[X]*gBoxSizeYZ + (pt)[Z]*gBoxSize[Y] + (pt)[Y])
#define BOX_INDEX(x,y,z)	((x)*gBoxSizeYZ + (z)*gBoxSize[Y] + (y))

// feed chunk number and location to get index inside chunk's data
//#define CHUNK_INDEX(bx,bz,x,y,z) (  (y)+ \
//											(((z)-(bz)*16)+ \
//											((x)-(bx)*16)*16)*128)
#define CHUNK_INDEX(bx,bz,x,y,z) (  ((y)*256)+ \
    (((z)-(bz)*16)*16) + \
    ((x)-(bx)*16)  )

#define UPDATE_PROGRESS(p)		if (*gpCallback){ (*gpCallback)((float)(p));}

#define AREA_IN_CM2     (gModel.faceCount * gModel.scale * gModel.scale * METERS_TO_CM * METERS_TO_CM)

// when should output be part of the progress bar? That is, 0.80 means 80% done when we start outputting the file
// number is how far along we are when that part begins, approx.
#define PG_MAKE_FACES 0.15f
#define PG_OUTPUT 0.20f
#define PG_TEXTURE 0.75f
#define PG_CLEANUP 0.85f
// leave some time for zipping files
#define PG_END 0.90f

#define NO_INDEX_SET 0xffffffff

// alpha for group debug mode
#define DEBUG_DISPLAY_ALPHA 0.2f

// type for weld edge debug display
#define DEBUG_EDGE_TOUCH_TYPE BLOCK_LAVA
#define DEBUG_CORNER_TOUCH_TYPE 129

// For super-hollow, we identify hollowed areas with group 0
#define HOLLOW_AIR_GROUP 0
// The surround air group is all the air around the model (in all 6 directions, since there's a border of air)
#define SURROUND_AIR_GROUP 1


typedef struct TouchCell {
    unsigned short connections;	// bit field showing connections to edges
    unsigned char count;		// number of connections (up to 12)
    unsigned char obscurity;	// how many directions have something blocking it from visibility (up to 6). More hidden air cells get filled first
} TouchCell;

TouchCell *gTouchGrid;

static int gTouchSize;

typedef struct TouchRecord {
    int boxIndex;
    float distance;   // really, distance squared
    unsigned char count;
    unsigned char obscurity;
} TouchRecord;

// read these as: given my location, and I'm an air block, TOUCH_MX_MY means
// "in the X=-1 and Y=-1 (and Z=0) direction there is another air block"
#define TOUCH_MX_MY 0x001
#define TOUCH_MX_MZ 0x002
#define TOUCH_MX_PY 0x004
#define TOUCH_MX_PZ 0x008

#define TOUCH_MY_MZ 0x010
#define TOUCH_MY_PZ 0x020

#define TOUCH_PX_MY 0x040
#define TOUCH_PX_MZ 0x080
#define TOUCH_PX_PY 0x100
#define TOUCH_PX_PZ 0x200

#define TOUCH_PY_MZ 0x400
#define TOUCH_PY_PZ 0x800


// anything flattened (made into a decal put on a neighboring solid block) marks the flatFlags in the block itself,
// so that the block knows to look that direction for the flattened decal to apply.
#define FLAT_FACE_LO_X  0x01
#define FLAT_FACE_HI_X  0x02
#define FLAT_FACE_LO_Z  0x04
#define FLAT_FACE_HI_Z  0x08
#define FLAT_FACE_ABOVE 0x10
// vines only:
#define FLAT_FACE_BELOW 0x20

// suffixes for OBJ files' material textures
#define PNG_RGB_SUFFIXCHAR "-RGB"
#define PNG_RGBA_SUFFIXCHAR "-RGBA"
#define PNG_ALPHA_SUFFIXCHAR "-Alpha"
#define PNG_RGB_SUFFIX L"-RGB"
#define PNG_RGBA_SUFFIX L"-RGBA"
#define PNG_ALPHA_SUFFIX L"-Alpha"

// For clearing border blocks
#define EDIT_MODE_CLEAR_TYPE                    0
#define EDIT_MODE_CLEAR_ALL                     1
#define EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES      2


static void initializeWorldData( IBox *worldBox, int xmin, int ymin, int zmin, int xmax, int ymax, int zmax );
static int initializeModelData();

static int readTerrainPNG( const wchar_t *curDir, progimage_info *pII, wchar_t *terrainFileName );

static int populateBox(const wchar_t *world, IBox *box);
static void findChunkBounds(const wchar_t *world, int bx, int bz, IBox *worldBox );
static void extractChunk(const wchar_t *world, int bx, int bz, IBox *box );
static void modifySides( int editMode );
static void modifySlab(int by, int editMode);
static void editBlock( int x, int y, int z, int editMode );

static int filterBox();
static int computeFlatFlags( int boxIndex );
static int firstFaceModifier( int isFirst, int faceIndex );
static int saveBillboardOrGeometry( int boxIndex, int type );
static int saveTriangleGeometry( int type, int dataVal, int boxIndex, int typeBelow, int dataValBelow, int boxIndexBelow, int choppedSide );
static void setDefaultUVs( Point2 uvs[3], int skip );
static FaceRecord * allocFaceRecordFromPool();
static int saveTriangleFace( int boxIndex, int swatchLoc, int type, int faceDirection, int startVertexIndex, int vindex[3], Point2 uvs[3] );
static void saveBlockGeometry( int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static void saveBoxGeometry( int boxIndex, int type, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static void saveBoxTileGeometry( int boxIndex, int type, int swatchLoc, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static void saveBoxMultitileGeometry( int boxIndex, int type, int topSwatchLoc, int sideSwatchLoc, int bottomSwatchLoc, int markFirstFace, int faceMask,
    int rotUVs, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static void saveBoxReuseGeometry( int boxIndex, int type, int swatchLoc, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static int saveBoxAlltileGeometry( int boxIndex, int type, int swatchLocSet[6], int markFirstFace, int faceMask, int rotUVs, int reuseVerts,
    int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static int findFaceDimensions( int rect[4], int faceDirection, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ );
static int lesserNeighborCoversRectangle( int faceDirection, int boxIndex, int rect[4] );
static int getFaceRect( int faceDirection, int boxIndex, int view3D, int faceRect[4] );
static int saveBoxFace( int swatchLoc, int type, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int reverseLoop,
    int rotUVs, float minu, float maxu, float minv, float maxv );
static int saveBoxFaceUVs( int type, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int uvIndices[4] );
static int saveBillboardFaces( int boxIndex, int type, int billboardType );
static int saveBillboardFacesExtraData( int boxIndex, int type, int billboardType, int dataVal, int firstFace );
static int checkGroupListSize();
static int checkVertexListSize();
static int checkFaceListSize();

static int findGroups();
static void addVolumeToGroup( int groupID, int minx, int miny, int minz, int maxx, int maxy, int maxz );
static void propagateSeed(IPoint point, BoxGroup *groupInfo, IPoint **seedStack, int *seedSize, int *seedCount);
static int getNeighbor( int faceDirection, IPoint newPoint );
static void getNeighborUnsafe( int faceDirection, IPoint newPoint );

static void checkAndRemoveBubbles();
static void findNeighboringGroups( IBox *bounds, int groupID, int *neighborGroups );

//static void establishGroupBounds();

static void fillGroups( IBox *bounds, int masterGroupID, int solid, int fillType, int *targetGroupIDs );

static void addBounds( IPoint loc, IBox *bounds );
static void addBoundsToBounds( IBox inBounds, IBox *bounds );

static int connectCornerTips();
static int checkForCorner(int boxIndex, int offx, int offz);

static int fixTouchingEdges();
static int touchRecordCompare( void *context, const void *str1, const void *str2);
static void checkForTouchingEdge(int boxIndex, int offx, int offy, int offz);
static int computeObscurity( int boxIndex );
static void decrementNeighbors( int boxIndex );
static float computeHidingDistance( Point loc1, Point loc2, float norm );
static void boxIndexToLoc( IPoint loc, int boxIndex );

static void deleteFloatingGroups();
static int determineScaleAndHollowAndMelt();
static void scaleByCost();
static void hollowBottomOfModel();
static void meltSnow();
static void hollowSeed( int x, int y, int z, IPoint **seedList, int *seedSize, int *seedCount );

static int generateBlockDataAndStatistics();
static int faceIdCompare( void *context, const void *str1, const void *str2);

static int getDimensionsAndCount( Point dimensions );
static void rotateLocation( Point pt );
static int checkAndCreateFaces( int boxIndex, IPoint loc );
static int checkMakeFace( int type, int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex );
static int neighborMayCoverFace( int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex );
static int lesserBlockCoversWholeFace( int faceDirection, int neighborBoxIndex, int view3D );
static int isFluidBlockFull( int type, int boxIndex );
static int cornerHeights( int type, int boxIndex, float heights[4] );
static float computeUpperCornerHeight( int type, int boxIndex, int x, int z );
static float getFluidHeightPercent( int dataVal );
static int sameFluid( int fluidType, int type );
static int saveSpecialVertices( int boxIndex, int faceDirection, IPoint loc, float heights[4], int heightIndices[4] );
static int saveVertices( int boxIndex, int faceDirection, IPoint loc );
static int saveFaceLoop( int boxIndex, int faceDirection, float heights[4], int heightIndex[4] );
static int getMaterialUsingGroup( int groupID );
static int retrieveWoolSwatch( int dataVal );
static int getSwatch( int type, int dataVal, int faceDirection, int backgroundIndex, int uvIndices[4] );
static int getCompositeSwatch( int swatchLoc, int backgroundIndex, int faceDirection, int angle );
static int createCompositeSwatch( int swatchLoc, int backgroundSwatchLoc, int angle );

static void flipIndicesLeftRight( int localIndices[4] );
static void rotateIndices( int localIndices[4], int angle );
static void saveTextureCorners( int swatchLoc, int type, int uvIndices[4] );
static void saveRectangleTextureUVs( int swatchLoc, int type, float minu, float maxu, float minv, float maxv, int uvIndices[4] );
static int saveTextureUV( int swatchLoc, int type, float u, float v );

static void freeModel( Model *pModel );

static int writeAsciiSTLBox( const wchar_t *world, IBox *box );
static int writeBinarySTLBox( const wchar_t *world, IBox *box );
static int writeOBJBox( const wchar_t *world, IBox *worldBox, const wchar_t *curDir, const wchar_t *terrainFileName );
static int writeOBJTextureUV( float u, float v, int addComment, int swatchLoc );
static int writeOBJMtlFile();

static int writeVRML2Box( const wchar_t *world, IBox *box );
static int writeVRMLAttributeShapeSplit( int type, char *mtlName, char *textureOutputString );
static int writeVRMLTextureUV( float u, float v, int addComment, int swatchLoc );

static int writeSchematicBox();
static int schematicWriteCompoundTag( gzFile gz, char *tag );
static int schematicWriteShortTag( gzFile gz, char *tag, short value );
static int schematicWriteEmptyListTag( gzFile gz, char *tag );
static int schematicWriteString( gzFile gz, char *tag, char *field );
static int schematicWriteByteArrayTag( gzFile gz, char *tag, unsigned char *byteData, int totalSize );

static int schematicWriteTagValue( gzFile gz, unsigned char tagValue, char *tag );
static int schematicWriteUnsignedCharValue( gzFile gz, unsigned char charValue );
static int schematicWriteUnsignedShortValue( gzFile gz, unsigned short shortValue );
static int schematicWriteShortValue( gzFile gz, short shortValue );
static int schematicWriteIntValue( gzFile gz, int intValue );
static int schematicWriteStringValue( gzFile gz, char *stringValue );


static int writeLines( HANDLE file, char **textLines, int lines );

static int writeStatistics( HANDLE fh, const char *justWorldFileName, IBox *worldBox );

static float computeMaterialCost( int printMaterialType, float blockEdgeSize, int numBlocks, int numMinorBlocks );
static int finalModelChecks();

static void addOutputFilenameToList(wchar_t *filename);

static void spacesToUnderlines( wchar_t *targetString );
static void spacesToUnderlinesChar( char *targetString );

static int createBaseMaterialTexture();

static void copyPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, progimage_info *src, int src_x_min, int src_y_min);
static int tileIsSemitransparent(progimage_info *src, int col, int row);
// for jungle determination - no longer needed: static int tileIsCutout(progimage_info *src, int col, int row);
static int tileIsOpaque(progimage_info *src, int col, int row);
static void setColorPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, unsigned int value);
static void stretchSwatchToTop(progimage_info *dst, int swatchIndex, float startStretch);
static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, int tileSize, progimage_info *src, int src_x, int src_y);
#ifdef _DEBUG
static void drawPNGTileLetterR( progimage_info *dst, int x, int y, int tileSize );
#endif
static void setColorPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned int value);
static void addNoisePNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a, float noise );
static void multiplyPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a );
//static void grayZeroAlphasPNG(progimage_info *dst, unsigned char r, unsigned char g, unsigned char b ); TODO remove?
//static void fillZeroAlphasPNGSwatch(progimage_info *dst, int destSwatch, int sourceSwatch, int swatchSize, int swatchesPerRow );
static void rotatePNGTile(progimage_info *dst, int dcol, int drow, int scol, int srow, int angle, int swatchSize );
static void blendTwoSwatches( progimage_info *dst, int txrSwatch, int solidSwatch, float blend, unsigned char alpha );
static void bleedPNGSwatch(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha );
static void setAlphaPNGSwatch(progimage_info *dst, int dstSwatch, int swatchSize, int swatchesPerRow, unsigned char alpha );
static void compositePNGSwatches(progimage_info *dst, int dstSwatch, int overSwatch, int underSwatch, int swatchSize, int swatchesPerRow, int forceSolid );
static int convertRGBAtoRGBandWrite(progimage_info *src, wchar_t *filename);
static void convertAlphaToGrayscale( progimage_info *dst );

static void ensureSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix );
static void removeSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix );
//static const wchar_t *removePath( const wchar_t *src );
static const char *removePathChar( const char *src );

static void wcharToChar( const wchar_t *inWString, char *outString );
static void charToWchar( char *inString, wchar_t *outWString );
static void getPathAndRoot( const wchar_t *src, int fileType, wchar_t *path, wchar_t *root );
static void concatFileName2(wchar_t *dst, const wchar_t *src1, const wchar_t *src2);
static void concatFileName3(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3);
static void concatFileName4(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3, const wchar_t *src4);
static void wcharCleanse( wchar_t *wstring );

static void myseedrand( long seed );
static double myrand();


void ChangeCache( int size )
{
    Change_Cache_Size(size);
}

void ClearCache()
{
    Cache_Empty();
}
////////////////////////////////////////////////////////
//
// Main code begins

//world = path to world saves
//min* = minimum block location of box to save
//max* = maximum block location of box to save

// Return 0 if all is well, errcode if something went wrong.
// User should be warned if nothing found to export.
int SaveVolume( wchar_t *saveFileName, int fileType, Options *options, const wchar_t *world, const wchar_t *curDir, int xmin, int ymin, int zmin, int xmax, int ymax, int zmax, 
    ProgressCallback callback, wchar_t *terrainFileName, FileList *outputFileList, int majorVersion, int minorVersion )
{
    //#ifdef WIN32
    //    DWORD br;
    //#endif

    // * Read the texture for the materials.
    // * populateBox:
    //  ** Read through the chunks once, only the data within the box specified. Keep track of the "solid box", the bounds where objects actually exist.
    //  ** Allocate and clear the "air box", which includes the solid box plus a 1 cell border for ease of neighbor testing.
    //  ** Read through the chunks again, this time increasing the solid box by 1 in X and Z for rendering if not boxing off, and 1 in Y for all.
    // * Create output texture image data array.
    // * filterBox: output all minor geometry objects that are not full blocks. Billboards, small geometry, and snow and wires are flattened onto the face below. The "type" of these is all set to be empty immediately after they're processed, so origType is used if we want to find if anything was in the cell.
    //  ** Fill, connect, hollow, melt: various operations to add or subtract blocks, mostly needed for 3D printing.

    IBox worldBox;
    int retCode = MW_NO_ERROR;
    int needDifferentTextures = 0;

    // set up a bunch of globals
    gpCallback = &callback;
    // initial "quick" progress just so progress bar moves a bit.
    UPDATE_PROGRESS(0.20f*PG_MAKE_FACES);

    gMajorVersion = majorVersion;
    gMinorVersion = minorVersion;

    memset(&gStats,0,sizeof(ExportStatistics));
    // clear all of gModel to zeroes
    memset(&gModel,0,sizeof(Model));
    gOptions = options;
    gOptions->totalBlocks = 0;
    gOptions->cost = 0.0f;

    gExportTexture = (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE) ? 1 : 0;

    gPrint3D = (gOptions->exportFlags & EXPT_3DPRINT) ? 1 : 0;
    gModel.pInputTerrainImage = NULL;

    // Billboards and true geometry to be output?
    // True only if we're exporting all geometry.
    // Must be set now, as this influences whether we stretch textures.
    gExportBillboards =
        //(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) &&
        gOptions->pEFD->chkExportAll;

    gPhysMtl = gOptions->pEFD->comboPhysicalMaterial[gOptions->pEFD->fileType];

    gUnitsScale = gUnitTypeTable[gOptions->pEFD->comboModelUnits[gOptions->pEFD->fileType]].unitsPerMeter;

    gBoxData = NULL;
    gBiome = NULL;

    gMinorBlockCount = 0;

    // get path name and root of output file name as separate globals. Also have a
    // "clean" (no extended characters, spaces turns to _) version of the output name, for the files
    // that are referenced, such as material and texture files. We will
    // use these elements to then build up the output names.
    getPathAndRoot( saveFileName, fileType, gOutputFilePath, gOutputFileRoot );
    wcscpy_s(gOutputFileRootClean,MAX_PATH,gOutputFileRoot);
    wcharCleanse(gOutputFileRootClean);
    spacesToUnderlines(gOutputFileRootClean);
    wcharToChar(gOutputFileRootClean, gOutputFileRootCleanChar);


    // start exporting for real

    if ( options->moreExportMemory )
    {
        // clear the cache before export - this lets us export larger worlds.
        // This could be made optional, but map reload is pretty fast, so let's always do this.
        ClearCache();
    }

    // we might someday reload when reading the data that will actually be exported;
    // Right now, any bad data encountered will flag the problem.
    //ClearBlockReadCheck();
    gBadBlocksInModel = 0;

    // reset random number seed
    myseedrand(12345);

    gOutputFileList = outputFileList;

    UPDATE_PROGRESS(0.30f*PG_MAKE_FACES);

    // first things very first: if full texturing is wanted, check if the texture is readable
    if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
    {
        gModel.pInputTerrainImage = new progimage_info();

        // note that any failure in readTerrainPNG will cause the "sub-error code" (in the shifted bits MW_NUM_CODES)
        // to give a value > MW_BEGIN_ERRORS. This is fine, as any read PNG error is a real error.
        retCode |= readTerrainPNG(curDir,gModel.pInputTerrainImage,terrainFileName);
        if ( retCode >= MW_BEGIN_ERRORS )
        {
            // couldn't read terrain image
            goto Exit;
        }

        // check if height of texture is sufficient.
        if ( gModel.pInputTerrainImage->height / (gModel.pInputTerrainImage->width/16) < 16 )
        {
            // image does not have the minimum 16 rows, something's really wrong
            retCode |= MW_NEED_16_ROWS;
            goto Exit;
        }
        if ( gModel.pInputTerrainImage->height / (gModel.pInputTerrainImage->width/16) < VERTICAL_TILES )
        {
            // fix image, expanding the image with white. Warn user.
            int tileSize = gModel.pInputTerrainImage->width/16;
            // set empty area to all 1's
            gModel.pInputTerrainImage->image_data.resize(VERTICAL_TILES*tileSize*gModel.pInputTerrainImage->width*4, 0xff);
            retCode |= MW_NOT_ENOUGH_ROWS;
        }
    }

    UPDATE_PROGRESS(0.40f*PG_MAKE_FACES);

    initializeWorldData( &worldBox, xmin, ymin, zmin, xmax, ymax, zmax );

    // note that worldBox will come back with the "solid" bounds, of where data was actually found
    retCode |= populateBox(world, &worldBox);
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // nothing in box, so end.
        goto Exit;
    }

    // prepare to write texture, if needed
    if (gExportTexture)
    {
        // make it twice as large if we're outputting image textures, too- we need the space
        if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
        {
            // use true textures
            gModel.textureResolution = 2*gModel.pInputTerrainImage->width;
        }
        else
        {
            // Use "noisy" colors, fixed 512 x 512 - we could actually make this texture quite small
            // Note this used to be 256 x 256, but that's only 14*14 = 196 materials, and we're now
            // at 198 or so...
            gModel.textureResolution = 512;
            // This number determines number of swatches per row. Make it 256, even though there's
            // no incoming image. This then ensures there's room for enough solid color images.
            gModel.pInputTerrainImage->width = 256;    // really, no image, but act like there is
        }
        // there are always 16 tiles wide in terrainExt.png, so we divide by this.
        gModel.tileSize = gModel.pInputTerrainImage->width/16;
        gModel.swatchSize = 2 + gModel.tileSize;
        gModel.invTextureResolution = 1.0f / (float)gModel.textureResolution;
        gModel.swatchesPerRow = (int)(gModel.textureResolution / gModel.swatchSize);
        gModel.textureUVPerSwatch = (float)gModel.swatchSize / (float)gModel.textureResolution; // e.g. 18 / 256
        gModel.textureUVPerTile = (float)gModel.tileSize / (float)gModel.textureResolution; // e.g. 16 / 256
        gModel.swatchListSize = gModel.swatchesPerRow*gModel.swatchesPerRow;

        retCode |= createBaseMaterialTexture();
    }

    // all done with base input texture, free up its memory.
    if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
    {
        readpng_cleanup(1,gModel.pInputTerrainImage);
        delete gModel.pInputTerrainImage;
        gModel.pInputTerrainImage = NULL;
    }

    // were there errors?
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // texture out of memory or some other read error.
        goto Exit;
    }

    UPDATE_PROGRESS(0.50f*PG_MAKE_FACES);
    retCode |=  initializeModelData();
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        goto Exit;
    }

    UPDATE_PROGRESS(0.60f*PG_MAKE_FACES);

    // process all billboards and "minor geometry"
    retCode |= filterBox();
    // always return the worst error
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // problem found
        goto Exit;
    }
    UPDATE_PROGRESS(0.90f*PG_MAKE_FACES);


    // at this point all data is read in and filtered. At this point check if we're outputting a
    // non-polygonal output format, like schematic. If so, do that and be done.
    if ( fileType == FILE_TYPE_SCHEMATIC )
    {
        retCode |= writeSchematicBox();
        goto Exit;
    }

    retCode |= determineScaleAndHollowAndMelt();
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // problem found
        goto Exit;
    }
    UPDATE_PROGRESS(PG_MAKE_FACES);

    // TODO idea: not sure it's needed, but we could provide a "melt" function, in which all objects of
    // a given ID are removed from the final model before output. This gives the user a way to connect
    // hollowed areas with interiors and let the building material out of "escape holes".

    // create database and compute statistics for output
    retCode |= generateBlockDataAndStatistics();
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    UPDATE_PROGRESS(PG_OUTPUT);

    switch ( fileType )
    {
    case FILE_TYPE_WAVEFRONT_REL_OBJ:
    case FILE_TYPE_WAVEFRONT_ABS_OBJ:
        // for OBJ, we may use more than one texture
        needDifferentTextures = 1;
        retCode |= writeOBJBox( world, &worldBox, curDir, terrainFileName );
        break;
    case FILE_TYPE_BINARY_MAGICS_STL:
    case FILE_TYPE_BINARY_VISCAM_STL:
        retCode |= writeBinarySTLBox( world, &worldBox );
        break;
    case FILE_TYPE_ASCII_STL:
        retCode |= writeAsciiSTLBox( world, &worldBox );
        break;
    case FILE_TYPE_VRML2:
        retCode |= writeVRML2Box( world, &worldBox );
        break;
    //case FILE_TYPE_SETTINGS:
        //retCode |= writeSettings( world, &worldBox );
        //break;
    default:
        assert(0);
        break;
    }

    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // problem found
        goto Exit;
    }

    retCode |= finalModelChecks();

    // done!
Exit:

    // write out texture file, if any input data
    // 45%
    UPDATE_PROGRESS(PG_TEXTURE);

    // if there were major errors, don't bother
    if ( retCode < MW_BEGIN_ERRORS )
    {
        if ( gModel.pPNGtexture != NULL )
        {
            // if we're rendering all blocks, don't fill in cauldrons, beds, etc. as we want these cutouts for rendering; else use offset:
#define FA_TABLE__RENDER_BLOCK_START 10
#define FA_TABLE__VIEW_SIZE 18
#define FA_TABLE_SIZE 71
            static FillAlpha faTable[FA_TABLE_SIZE] =
            {
                // stuff filled only if lesser (i.e. all blocks) is off for rendering, so that the cauldron is rendered as a solid block
                { SWATCH_INDEX( 10, 8 ), BLOCK_OBSIDIAN }, // cauldron
                { SWATCH_INDEX( 10, 9 ), BLOCK_OBSIDIAN }, // cauldron
                { SWATCH_INDEX( 11, 9 ), BLOCK_OBSIDIAN }, // cauldron

                { SWATCH_INDEX( 5, 9 ), BLOCK_OBSIDIAN }, // bed
                { SWATCH_INDEX( 6, 9 ), BLOCK_OBSIDIAN }, // bed
                { SWATCH_INDEX( 7, 9 ), BLOCK_OBSIDIAN }, // bed
                { SWATCH_INDEX( 8, 9 ), BLOCK_OBSIDIAN }, // bed

                { SWATCH_INDEX( 5, 4 ), BLOCK_CACTUS }, // cactus
                { SWATCH_INDEX( 6, 4 ), BLOCK_CACTUS }, // cactus
                { SWATCH_INDEX( 7, 4 ), BLOCK_CACTUS }, // cactus

                /////////////////////////////////// always do:
                // stuff that is put in always, fringes that need to be filled
                { SWATCH_INDEX( 9, 7 ), BLOCK_CAKE }, // cake
                { SWATCH_INDEX( 10, 7 ), BLOCK_CAKE }, // cake
                { SWATCH_INDEX( 11, 7 ), BLOCK_CAKE }, // cake
                { SWATCH_INDEX( 12, 7 ), BLOCK_CAKE }, // cake
                { SWATCH_INDEX( 15, 9 ), SWATCH_INDEX(15,10) }, // ender portal (should be filled, but just in case, due to stretch)
                { SWATCH_INDEX( 15, 14 ), BLOCK_LAVA }, // lava, in case it's not filled
                { SWATCH_INDEX( 15, 15 ), BLOCK_STATIONARY_LAVA }, // stationary lava, in case it's not present
                { SWATCH_INDEX( 10, 11 ), BLOCK_FLOWER_POT }, // flower pot

                // count is FA_TABLE__VIEW_SIZE up to this point

                // stuff filled in for 3D printing only
                { SWATCH_INDEX( 11, 0 ), SWATCH_INDEX( 6, 3 ) }, // spiderweb over stone block
                { SWATCH_INDEX( 12, 0 ), SWATCH_INDEX( 0, 0 ) }, // red flower over grass
                { SWATCH_INDEX( 13, 0 ), SWATCH_INDEX( 0, 0 ) }, // yellow flower over grass
                { SWATCH_INDEX( 15, 0 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX( 12, 1 ), SWATCH_INDEX( 0, 0 ) }, // red mushroom over grass
                { SWATCH_INDEX( 13, 1 ), SWATCH_INDEX( 0, 0 ) }, // brown mushroom over grass
                { SWATCH_INDEX(  1, 3 ), BLOCK_GLASS }, // glass over glass color
                { SWATCH_INDEX(  4, 3 ), BLOCK_AIR }, // transparent leaves over air (black) - doesn't really matter, not used when printing anyway
                { SWATCH_INDEX(  7, 3 ), SWATCH_INDEX( 2, 1 ) }, // dead bush over grass
                { SWATCH_INDEX(  8, 3 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX(  1, 4 ), SWATCH_INDEX( 1, 0 ) }, // spawner over stone
                { SWATCH_INDEX(  9, 4 ), SWATCH_INDEX( 0, 0 ) }, // reeds over grass
                { SWATCH_INDEX( 15, 4 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX( 14, 1 ), SWATCH_INDEX( 0, 0 ) }, // jungle sapling over grass
                { SWATCH_INDEX(  1, 5 ), SWATCH_INDEX( 6, 0 ) }, // wooden door top over slab top
                { SWATCH_INDEX(  2, 5 ), SWATCH_INDEX( 6, 0 ) }, // door top over slab top
                { SWATCH_INDEX(  5, 5 ), SWATCH_INDEX( 6, 3 ) }, // iron bars over stone block
                { SWATCH_INDEX(  8, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX(  9, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 10, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 11, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 12, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 13, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 14, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX( 15, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
                { SWATCH_INDEX(  0, 6 ), SWATCH_INDEX( 1, 0 ) }, // lever over stone
                { SWATCH_INDEX( 15, 6 ), SWATCH_INDEX( 6, 5 ) }, // melon stem over farmland
                { SWATCH_INDEX( 15, 7 ), SWATCH_INDEX( 6, 5 ) }, // mature stem over farmland
                { SWATCH_INDEX(  4, 8 ), BLOCK_AIR }, // leaves over air (black) - doesn't really matter, not used
                { SWATCH_INDEX( 10, 8 ), BLOCK_AIR }, // cauldron over air (black)
                { SWATCH_INDEX( 12, 8 ), BLOCK_AIR }, // cake over air (black) - what's this for, anyway?
                { SWATCH_INDEX(  4, 9 ), BLOCK_GLASS }, // glass pane over glass color (more interesting than stone, and lets you choose)
                { SWATCH_INDEX( 13, 9 ), SWATCH_INDEX( 1, 0 ) }, // brewing stand over stone
                { SWATCH_INDEX( 15,10 ), SWATCH_INDEX( 1, 0 ) }, // end portal thingy (unused) over stone
                { SWATCH_INDEX( 14,11 ), SWATCH_INDEX( 6, 5 ) }, // pumpkin stem over farmland
                { SWATCH_INDEX( 15,11 ), SWATCH_INDEX( 6, 5 ) }, // mature stem over farmland
                { SWATCH_INDEX(  4,12 ), BLOCK_AIR }, // jungle leaves over air (black) - doesn't really matter, not used
                { SWATCH_INDEX(  2,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX(  3,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX(  4,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX( 11,14 ), BLOCK_WOOL }, // beacon over white color - TODO!!! change to 9,2 once we're using terrain properly
                { SWATCH_INDEX(  8,10 ), BLOCK_COCOA_PLANT }, // cocoa, so preview looks semi-OK
                { SWATCH_INDEX(  9,10 ), BLOCK_COCOA_PLANT }, // cocoa, so preview looks OK
                { SWATCH_INDEX( 10,10 ), BLOCK_COCOA_PLANT }, // cocoa, so preview looks OK
                { SWATCH_INDEX(  8,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX(  9,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 10,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 11,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 12,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 13,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 14,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 15,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 15, 1 ), BLOCK_AIR }, // fire over air (black)
            };

            int rc;

            // do only if true textures used
            if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
            {
                int i;
                int faTableCount;

                // fill in all alphas that 3D export wants filled; always fill in cactus, cake, and bed fringes, for example;
                // For printing we also then composite over other backgrounds as the defaults.
                faTableCount = gPrint3D ? FA_TABLE_SIZE : FA_TABLE__VIEW_SIZE;
                // start at solid rendering vs. leave it transparent for true cutaway rendering;
                // that is, go from 0 if printing or if we're rendering & not exporting true geometry (lesser)
                for ( i = ( gPrint3D || !gOptions->pEFD->chkExportAll ) ? 0 : FA_TABLE__RENDER_BLOCK_START; i < faTableCount; i++ )
                {
                    compositePNGSwatches( gModel.pPNGtexture,
                        faTable[i].cutout, faTable[i].cutout, faTable[i].underlay,
                        gModel.swatchSize, gModel.swatchesPerRow, 0 );
                }

                // final swatch cleanup if textures are used and we're doing 3D printing
                if ( gPrint3D )
                {


#define FA_FINAL_TABLE_SIZE 20
                    static FillAlpha faFinalTable[] =
                    {
                        { SWATCH_INDEX( 0, 8 ), SWATCH_INDEX( 1, 0 ) }, // rail over stone
                        { SWATCH_INDEX( 0, 7 ), SWATCH_INDEX( 1, 0 ) }, // curved rail over stone
                        { SWATCH_INDEX( 0, 5 ), SWATCH_INDEX( 1, 0 ) }, // torch over stone
                        { SWATCH_INDEX( 4, 10 ), SWATCH_INDEX( 1, 0 ) }, // wire over stone
                        { SWATCH_INDEX( 3, 5 ), SWATCH_INDEX( 1, 0 ) }, // ladder over stone
                        { SWATCH_INDEX( 3, 11 ), SWATCH_INDEX( 1, 0 ) }, // powered rail over stone
                        { SWATCH_INDEX( 3, 10 ), SWATCH_INDEX( 1, 0 ) }, // unpowered rail over stone
                        { SWATCH_INDEX( 3, 12 ), SWATCH_INDEX( 1, 0 ) }, // detector rail over stone
                        { SWATCH_INDEX( 3, 6 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch on over stone
                        { SWATCH_INDEX( 3, 7 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch off over stone
                        { SWATCH_INDEX( 12, 4 ), SWATCH_INDEX( 15, 13 ) }, // lily pad over water
                        { SWATCH_INDEX( 4, 5 ), SWATCH_INDEX( 1, 0 ) }, // trapdoor over stone
                        { SWATCH_INDEX( 15, 8 ), SWATCH_INDEX( 0, 0 ) }, // vines over grass

                        // stuff we don't use, so don't need defaults for
                        { SWATCH_INDEX( 5, 10 ), SWATCH_INDEX( 1, 0 ) }, // wire over stone
                        { SWATCH_INDEX( 4, 11 ), SWATCH_INDEX( 1, 0 ) }, // wire over stone
                        { SWATCH_INDEX( 5, 11 ), SWATCH_INDEX( 1, 0 ) }, // wire over stone

                        { SWATCH_INDEX( 0, 15 ), SWATCH_INDEX( 1, 0 ) }, // torch top
                        { SWATCH_INDEX( 1, 15 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch on top
                        { SWATCH_INDEX( 2, 15 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch off top
                        { SWATCH_INDEX( 2, 22 ), SWATCH_INDEX( 1, 0 ) }, // iron trapdoor over stone
                    };

                    // now that we're totally done, fill in the main pieces which were left empty as templates;
                    // these probably won't get used (and really, we could use them as the initial composited swatches, which we created - silly duplication TODO)
                    for ( i = 0; i < FA_FINAL_TABLE_SIZE; i++ )
                    {
                        compositePNGSwatches( gModel.pPNGtexture,
                            faFinalTable[i].cutout, faFinalTable[i].cutout, faFinalTable[i].underlay,
                            gModel.swatchSize, gModel.swatchesPerRow, 0 );
                    }
                }
            }

            // if we're debugging groups, make the largest group transparent and set it to red
            if ( (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS) &&
                gExportTexture )
            {
                unsigned char a = (unsigned char)(DEBUG_DISPLAY_ALPHA * 255);
                unsigned char r = 0xff;
                unsigned char g = 0x00;
                unsigned char b = 0x00;
                unsigned int color = (a<<24)|(b<<16)|(g<<8)|r;
                int col, row;

                SWATCH_TO_COL_ROW( gDebugTransparentType, col, row );
                setColorPNGTile( gModel.pPNGtexture, col, row, gModel.swatchSize, color );
            }

            UPDATE_PROGRESS(PG_TEXTURE+0.05f);

            // do we need three textures, or just the one RGBA texture?
            if ( needDifferentTextures )
            {
                // need all three
                wchar_t textureRGB[MAX_PATH];
                wchar_t textureRGBA[MAX_PATH];
                wchar_t textureAlpha[MAX_PATH];

                // Write them out! We need three texture file names: -RGB, -RGBA, -Alpha.
                // The RGB/RGBA split is needed for fast previewers like G3D to gain additional speed
                // The all-alpha image is needed for various renderers to properly read cutouts
                concatFileName4(textureRGB, gOutputFilePath, gOutputFileRootClean, PNG_RGB_SUFFIX, L".png");
                concatFileName4(textureRGBA, gOutputFilePath, gOutputFileRootClean, PNG_RGBA_SUFFIX, L".png");
                concatFileName4(textureAlpha, gOutputFilePath, gOutputFileRootClean, PNG_ALPHA_SUFFIX, L".png");

                if ( gModel.usesRGBA )
                {
                    // output RGBA version
                    rc = writepng(gModel.pPNGtexture,4,textureRGBA);
                    addOutputFilenameToList(textureRGBA);
                    assert(rc == 0);
                    retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }

                if ( gModel.usesRGB )
                {
                    // output RGB version
                    rc = convertRGBAtoRGBandWrite(gModel.pPNGtexture,textureRGB);
                    assert(rc == 0);
                    retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }

                if ( gModel.usesAlpha )
                {
                    // output Alpha version, which is actually RGBA, to make 3DS MAX happy
                    convertAlphaToGrayscale( gModel.pPNGtexture );
                    rc = writepng(gModel.pPNGtexture,4,textureAlpha);
                    addOutputFilenameToList(textureAlpha);
                    assert(rc == 0);
                    retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }
            }
            else
            {
                // just the one (VRML). If we're printing, and not debugging (debugging needs transparency), we can convert this one down to RGB
                wchar_t textureFileName[MAX_PATH];
                concatFileName3(textureFileName,gOutputFilePath,gOutputFileRootClean,L".png");
                if ( gPrint3D && !(gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS) )
                {
                    rc = convertRGBAtoRGBandWrite(gModel.pPNGtexture,textureFileName);
                }
                else
                {
                    rc = writepng(gModel.pPNGtexture,4,textureFileName);
                    addOutputFilenameToList(textureFileName);
                }
                assert(rc == 0);
                retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
            }

            writepng_cleanup(gModel.pPNGtexture);
        }
    }

    UPDATE_PROGRESS(PG_CLEANUP);

    freeModel( &gModel );

    if ( gBoxData )
        free(gBoxData);
    gBoxData = NULL;

    if ( gBiome )
        free(gBiome);
    gBiome = NULL;


    // 90%
    UPDATE_PROGRESS(PG_END);

    if ( gBadBlocksInModel )
        // if ( UnknownBlockRead() && gBadBlocksInModel )
    {
        retCode |= MW_UNKNOWN_BLOCK_TYPE_ENCOUNTERED;
        // flag this just the first time found, then turn off error
        //CheckUnknownBlock( 0 );
    }

    return retCode;
}

static void initializeWorldData( IBox *worldBox, int xmin, int ymin, int zmin, int xmax, int ymax, int zmax )
{
    // clean up a bit: make sure max>=min, and limit Y
    ymin = clamp(ymin,0,MAP_MAX_HEIGHT);
    ymax = clamp(ymax,0,MAP_MAX_HEIGHT);

    // we don't really require one to be min or max, we take the range
    if ( xmin > xmax ) swapint(xmin,xmax);
    if ( ymin > ymax ) swapint(ymin,ymax);
    if ( zmin > zmax ) swapint(zmin,zmax);

    // add an air border of 1 block around the whole box
    gBoxSize[X] = xmax - xmin + 3;
    gBoxSize[Y] = ymax - ymin + 3;
    gBoxSize[Z] = zmax - zmin + 3;
    // scale for X index value
    gBoxSizeYZ = gBoxSize[Y] * gBoxSize[Z];
    // this will be the size of gBoxData
    gBoxSizeXYZ = gBoxSize[X] * gBoxSizeYZ;

    gFaceOffset[0] = -gBoxSizeYZ;	// -X
    gFaceOffset[1] = -1;			// -Y
    gFaceOffset[2] = -gBoxSize[Y];	// -Z
    gFaceOffset[3] =  gBoxSizeYZ;	// +X
    gFaceOffset[4] =  1;			// +Y
    gFaceOffset[5] =  gBoxSize[Y];	// +Z

    // what to add to a world coordinate to get a box coordinate;
    // the -1 is the "air" border of one block
    gWorld2BoxOffset[X] = 1 - xmin;
    gWorld2BoxOffset[Y] = 1 - ymin;
    gWorld2BoxOffset[Z] = 1 - zmin;

    Vec3Scalar( worldBox->min, =, xmin, ymin, zmin );
    Vec3Scalar( worldBox->max, =, xmax, ymax, zmax );
}

static int initializeModelData()
{
    int i, x,y,z, boxIndex, faceDirection;

    // allocate vertex index array for box (we can ignore all the outer edge
    // box cells, which is why this array is one smaller).
    // For convenience, we make it the same size anyway. So, index 0 is
    // the -x/-y/-z corner of box 0, which will never be used.

    // Who knows how many is a good starting number? We don't want to realloc
    // all the time, but too large and the program dies.
    int startNumVerts = 6*gBoxSize[X]*gBoxSize[Z];
    // let's not start with more than a million vertices
    if ( startNumVerts > 1000000 )
    {
        startNumVerts = 1000000;
    }
    // There is an index location for each grid cell. It gets filled in as vertices are found to exist.
    // Each location is set with the vertex index in the list of vertices output. Not memory efficient...
    gModel.vertexIndices = (int*)malloc(gBoxSizeXYZ*sizeof(int));   // this one never needs realloc
    // These may be reallocated as we go.
    gModel.vertexListSize = startNumVerts;
    gModel.vertices = (Point*)malloc(startNumVerts*sizeof(Point));
    if ( (gModel.vertexIndices == NULL ) || ( gModel.vertices == NULL ) )
    {
        return MW_WORLD_EXPORT_TOO_LARGE;
    }

    gModel.faceRecordPool = (FaceRecordPool *)malloc(sizeof(FaceRecordPool));
    gModel.faceRecordPool->count = 0;
    gModel.faceRecordPool->pPrev = NULL;

    VecScalar( gModel.billboardBounds.min, =,  999999);
    VecScalar( gModel.billboardBounds.max, =, -999999);

    // NO_INDEX_SET means vertex is not used
    for ( i = 0; i < gBoxSizeXYZ; i++ )
        gModel.vertexIndices[i] = NO_INDEX_SET;

    // count about how many faces we'll need to store and sort for output; this code will probably have to change
    // as we get more involved faces (welds, etc.)
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
                {
                    if ( gBoxData[boxIndex].type > BLOCK_AIR ) 
                    {
                        if ( gBoxData[boxIndex + gFaceOffset[faceDirection]].type <= BLOCK_AIR )
                            gModel.faceSize++;
                    }
                }
            }
        }
    }
    // increase the face list size
    // - it can sometimes get even higher, with foliage + billboards
    gModel.faceSize = (int)(gModel.faceSize*1.4 + 1);
    gModel.faceList = (FaceRecord**)malloc(gModel.faceSize*sizeof(FaceRecord*));

    memset(gModel.uvSwatches,0,NUM_MAX_SWATCHES*sizeof(UVList));
    gModel.uvIndexListSize = 200;	// 50 blocks' worth of UVs, often enough
    gModel.uvIndexList = (UVOutput*)malloc(gModel.uvIndexListSize*sizeof(UVOutput));
    if ( (gModel.faceList == NULL ) || ( gModel.uvIndexList == NULL ) )
    {
        return MW_WORLD_EXPORT_TOO_LARGE;
    }

    return MW_NO_ERROR;
}

static int readTerrainPNG( const wchar_t *curDir, progimage_info *pITI, wchar_t *selectedTerrainFileName )
{
    // file should be in same directory as .exe, sort of
    wchar_t defaultTerrainFileName[MAX_PATH];
    int rc=0;

    if ( wcslen(selectedTerrainFileName) > 0 )
    {
        rc = readpng(pITI,selectedTerrainFileName);
    }
    else
    {
        // Really, we shouldn't ever hit this branch, as the terrain file name is now always set
        // at the start. Left just in case...
        concatFileName2(defaultTerrainFileName,curDir,L"\\terrainExt.png");
        rc = readpng(pITI,defaultTerrainFileName);
    }

    // test if terrainExt.png file does not exist
    if ( rc == 48 )
    {
        //FILE DOESN'T EXIST - read memory file, setting all fields
        pITI->width = gTerrainExtWidth;
        pITI->height = gTerrainExtHeight;
        pITI->image_data.insert(pITI->image_data.end(), &gTerrainExt[0], &gTerrainExt[gTerrainExtWidth*gTerrainExtHeight*4]);
    }
    else if ( rc )
    {
        // Some other error, so we need to quit. We *could* read the internal memory terrainExt.png instead,
        // but that would hide any problems encountered with the terrainExt.png that does exist but is invalid. 
        return (MW_CANNOT_READ_SELECTED_TERRAIN_FILE|(rc<<MW_NUM_CODES));
    }

    if ( pITI->width > pITI->height )
        return MW_NEED_16_ROWS;

    // is width >= 16 and evenly dividable by 16?
    if ( (pITI->width < 16) || ((pITI->width % 16) > 0) )
        return MW_IMAGE_WRONG_WIDTH;

    // check that height is divisible by tile size
    if ( (pITI->height % (pITI->width / 16)) != 0 )
        return MW_IMAGE_WRONG_WIDTH;

    gModel.tileSize = gModel.pInputTerrainImage->width/16;
    // note vertical tile limit for texture. We will save all these tiles away.
    gModel.verticalTiles = gModel.pInputTerrainImage->height/gModel.tileSize;

#ifdef _DEBUG
    static int markTiles = 0;	// to put R on each tile for debugging, set to 1
    if ( markTiles )
    {
        int row, col;
        for ( row = 0; row < gModel.verticalTiles; row++ )
        {
            for ( col = 0; col < 16; col++ )
            {
                drawPNGTileLetterR( pITI, row, col, pITI->width/16 );
            }
        }
    }

    // If true, dump terrainExt.png file to code files terrainExt.h and terrainExt.cpp.
    // Meant to run once whenever a new TerrainExt.png is made.
    static bool outputPNG = false;
    if (outputPNG)
    {
#ifdef WIN32
        DWORD br;
#endif
        char outputString[MAX_PATH];
        int size = 4 * pITI->width * pITI->height;

        wchar_t codeFileNameWithSuffix[MAX_PATH];
        concatFileName3(codeFileNameWithSuffix,gOutputFilePath,gOutputFileRoot,L".h");

        // create the Wavefront OBJ file
        //DeleteFile(codeFileNameWithSuffix);
        static PORTAFILE outFile=PortaCreate(codeFileNameWithSuffix);
        if (outFile != INVALID_HANDLE_VALUE)
        {
            sprintf_s(outputString,256,"#ifndef __TERRAINEXTDATA_H__\n#define __TERRAINEXTDATA_H__\n\n" );
            WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
            sprintf_s(outputString,256,"extern int gTerrainExtWidth;\nextern int gTerrainExtHeight;\nextern unsigned char gTerrainExt[%d];\n\n#endif\n", size );
            WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
            PortaClose(outFile);

            concatFileName3(codeFileNameWithSuffix,gOutputFilePath,gOutputFileRoot,L".cpp");
            static PORTAFILE outFile=PortaCreate(codeFileNameWithSuffix);
            if (outFile != INVALID_HANDLE_VALUE)
            {
                sprintf_s(outputString,256,"#include \"stdafx.h\"\n\nint gTerrainExtWidth = %d;\nint gTerrainExtHeight = %d;\n\nunsigned char gTerrainExt[] = {\n", pITI->width, pITI->height );
                WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));

                // dump the data
                for ( int i = 0; i < size/16; i++ )
                {
                    unsigned char *p = &pITI->image_data[i*16];
                    if ( i < (size/16)-1 )
                    {
                        // comma at end
                        sprintf_s(outputString,256,"%d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d,\n",
                            *p, *(p+1), *(p+2), *(p+3),
                            *(p+4), *(p+5), *(p+6), *(p+7),
                            *(p+8), *(p+9), *(p+10), *(p+11),
                            *(p+12), *(p+13), *(p+14), *(p+15)
                            );
                    }
                    else
                    {
                        // no comma, since it's the last line.
                        sprintf_s(outputString,256,"%d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d, %d,%d,%d,%d\n",
                            *p, *(p+1), *(p+2), *(p+3),
                            *(p+4), *(p+5), *(p+6), *(p+7),
                            *(p+8), *(p+9), *(p+10), *(p+11),
                            *(p+12), *(p+13), *(p+14), *(p+15)
                            );
                    }
                    WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
                }

                sprintf_s(outputString,256,"};\n" );
                WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
                PortaClose(outFile);
            }
        }
    }
#endif

    return MW_NO_ERROR;
}

static int populateBox(const wchar_t *world, IBox *worldBox)
{
    int startxblock, startzblock;
    int endxblock, endzblock;
    int blockX, blockZ;
    IBox originalWorldBox = *worldBox;

    // grab the data block needed, with a border of "air", 0, around the set
    startxblock=(int)floor((float)worldBox->min[X]/16.0f);
    startzblock=(int)floor((float)worldBox->min[Z]/16.0f);
    endxblock=(int)floor((float)worldBox->max[X]/16.0f);
    endzblock=(int)floor((float)worldBox->max[Z]/16.0f);

    // get bounds on Y coordinates, since top part of box is usually air
    VecScalar( gSolidWorldBox.min, =,  999999 );
    VecScalar( gSolidWorldBox.max, =, -999999 );

    // We now extract twice: first time is just to get bounds of solid stuff we'll actually output.
    // Results of this first pass are put in gSolidWorldBox.
    for ( blockX=startxblock; blockX<=endxblock; blockX++ )
    {
        //UPDATE_PROGRESS( 0.1f*(blockX-startxblock+1)/(endxblock-startxblock+1) );
        // z increases west, decreases east
        for ( blockZ=startzblock; blockZ<=endzblock; blockZ++ )
        {
            // this method sets gSolidWorldBox
            findChunkBounds(world,blockX,blockZ,worldBox);
        }
    }
    if (gSolidWorldBox.min[Y] > gSolidWorldBox.max[Y])
    {
        // nothing to do: there is nothing in the box
        return MW_NO_BLOCKS_FOUND;
    }

    // done with reading chunk for export, so free memory
    if ( gOptions->moreExportMemory )
    {
        ClearCache();
    }

    // have to reinitialize to get right globals for gSolidWorldBox.
    initializeWorldData( worldBox, gSolidWorldBox.min[X], gSolidWorldBox.min[Y], gSolidWorldBox.min[Z], gSolidWorldBox.max[X], gSolidWorldBox.max[Y], gSolidWorldBox.max[Z] );

    gBoxData = (BoxCell*)malloc(gBoxSizeXYZ*sizeof(BoxCell));
    if ( gBoxData == NULL )
    {
        return MW_WORLD_EXPORT_TOO_LARGE;
    }

    // set all values to "air", 0, etc.
    memset(gBoxData,0x0,gBoxSizeXYZ*sizeof(BoxCell));

    if ( gOptions->exportFlags & EXPT_BIOME )
    {
        gBiome = (unsigned char *)malloc(gBoxSize[X] * gBoxSize[Z] * sizeof(unsigned char));
        if ( gBiome == NULL )
        {
            return MW_WORLD_EXPORT_TOO_LARGE;
        }

        // set all biome values to "plains"
        memset(gBiome,0x1,gBoxSize[X] * gBoxSize[Z] * sizeof(unsigned char));
    }

    // Now actually copy the relevant data over to the newly-allocated box data grid.
    // x increases east, decreases west.

    int edgestartxblock=(int)floor(((float)worldBox->min[X]-1)/16.0f);
    int edgestartzblock=(int)floor(((float)worldBox->min[Z]-1)/16.0f);
    int edgeendxblock=(int)floor(((float)worldBox->max[X]+1)/16.0f);
    int edgeendzblock=(int)floor(((float)worldBox->max[Z]+1)/16.0f);

    // If we are not generating borders (where everything outside is "air"), expand out by 1
    // for what we read from the data files. Later on we will set these border blocks so that
    // only the original type is set, with the regular type being air.
    IBox edgeWorldBox = *worldBox;
    if ( !gOptions->pEFD->chkBlockFacesAtBorders )
    {
        // These are useful for knowing which faces to output for "no sides and bottom" mode.
        assert(!gPrint3D);

        // The test: we've shrunk the worldBox (copied to edgeWorldBox) to the solid stuff in the scene.
        // If the solid stuff border is at the original world box bounds, then there may be stuff 1 block
        // outside the scene. So, expand the volume to be read in by 1.
        if ( originalWorldBox.min[X] == gSolidWorldBox.min[X] )
        {
            edgeWorldBox.min[X]--;
        }
        if ( originalWorldBox.max[X] == gSolidWorldBox.max[X] )
        {
            edgeWorldBox.max[X]++;
        }
        if ( originalWorldBox.min[Z] == gSolidWorldBox.min[Z] )
        {
            edgeWorldBox.min[Z]--;
        }
        if ( originalWorldBox.max[Z] == gSolidWorldBox.max[Z] )
        {
            edgeWorldBox.max[Z]++;
        }
    }

    // Here we actually always read in the upper and lower edge blocks in Y, even though we won't output them.
    // It is useful to know what is above or below a block because of how neighbors can affect 2-high objects,
    // such as doors and sunflowers. See http://minecraft.gamepedia.com/Data_values#Door for example.
    // if we're not at the lower limit, and there's actually some solid stuff on that bottom level, get the stuff below
    if ( edgeWorldBox.min[Y] > 0 && ( originalWorldBox.min[Y] == gSolidWorldBox.min[Y] ) )
    {
        edgeWorldBox.min[Y]--;
    }
    // if we not at the upper limit, get the stuff above
    if ( edgeWorldBox.max[Y] < 255 && ( originalWorldBox.max[Y] == gSolidWorldBox.max[Y] ))
    {
        edgeWorldBox.max[Y]++;
    }
    // Later in this method we clear all these blocks, leaving only the original type, if we do generate faces at the borders.

    for ( blockX=edgestartxblock; blockX<=edgeendxblock; blockX++ )
    {
        //UPDATE_PROGRESS( 0.1f*(blockX-edgestartxblock+1)/(edgeendxblock-edgestartxblock+1) );
        // z increases south, decreases north
        for ( blockZ=edgestartzblock; blockZ<=edgeendzblock; blockZ++ )
        {
            extractChunk(world,blockX,blockZ,&edgeWorldBox);

            // done with reading chunk for export, so free memory
            if ( gOptions->moreExportMemory )
            {
                ClearCache();
            }
        }
    }

    // done with reading chunk for export, so free memory.
    // should all be freed, but just in case...
    if ( gOptions->moreExportMemory )
    {
        ClearCache();
    }

    // convert to solid relative box (0 through boxSize-1)
    Vec3Op( gSolidBox.min, =, gSolidWorldBox.min, +, gWorld2BoxOffset );
    Vec3Op( gSolidBox.max, =, gSolidWorldBox.max, +, gWorld2BoxOffset );

    // adjust to box coordinates, and add 1 air fringe
    Vec2Op( gAirBox.min, =, -1 + gSolidBox.min );
    Vec2Op( gAirBox.max, =,  1 + gSolidBox.max );
    assert( (gAirBox.min[Y] >= 0) && (gAirBox.max[Y] < gBoxSize[Y]) );

    if ( gPrint3D )
    {
        // Clear slabs' type data, so that snow covering will not export if there is air below it.
        // Original types are left set so that minor objects can export properly.
        modifySlab(gAirBox.min[Y],EDIT_MODE_CLEAR_TYPE);
        modifySlab(gAirBox.max[Y],EDIT_MODE_CLEAR_TYPE);
    }

    return MW_NO_ERROR;
}

// test relevant part of a given chunk to find its size
static void findChunkBounds(const wchar_t *world, int bx, int bz, IBox *worldBox )
{
    int chunkX, chunkZ;

    int loopXmin, loopZmin;
    int loopXmax, loopZmax;
    int x,y,z;

    int chunkIndex, boxIndex;
    int blockID;

    //unsigned char dataVal;

    WorldBlock *block;
    block=(WorldBlock *)Cache_Find(bx,bz);

    if (block==NULL)
    {
        wchar_t directory[256];
        wcsncpy_s(directory,256,world,255);
        wcscat_s(directory,256,L"/");
        if (gOptions->worldType&HELL)
        {
            wcscat_s(directory,256,L"DIM-1/");
        }
        if (gOptions->worldType&ENDER)
        {
            wcscat_s(directory,256,L"DIM1/");
        }

        block=LoadBlock(directory,bx,bz);
        if (block==NULL) //blank tile, nothing to do
            return;

        Cache_Add(bx,bz,block);
    }

    // loop through area of box that overlaps with this chunk
    chunkX = bx * 16;
    chunkZ = bz * 16;

    loopXmin = max(worldBox->min[X],chunkX);
    loopZmin = max(worldBox->min[Z],chunkZ);

    loopXmax = min(worldBox->max[X],chunkX+15);
    loopZmax = min(worldBox->max[Z],chunkZ+15);

    for ( x = loopXmin; x <= loopXmax; x++ ) {
        for ( z = loopZmin; z <= loopZmax; z++ ) {
            boxIndex = WORLD_TO_BOX_INDEX(x,worldBox->min[Y],z);
            chunkIndex = CHUNK_INDEX(bx,bz,x,worldBox->min[Y],z);
            for ( y = worldBox->min[Y]; y <= worldBox->max[Y]; y++, boxIndex++ ) {
                blockID = block->grid[chunkIndex];

                // For Anvil, Y goes up by 256 (in 1.1 and earlier, it was just ++)
                chunkIndex += 256;

                // get bounds on y searches:
                // search for air, and if not found then
                // add to vertical bounds.
                if ( blockID > BLOCK_AIR )
                {
                    // Also check that object is not culled out because it's filtered (see filterBox).
                    int flags = gBlockDefinitions[blockID].flags;

                    // check if it's something not to be filtered out: in the output list and alpha > 0
                    if ( (flags & gOptions->saveFilterFlags) &&
                        (gBlockDefinitions[blockID].alpha > 0.0) ) {
                            IPoint loc;
                            Vec3Scalar( loc, =, x,y,z );
                            addBounds(loc,&gSolidWorldBox);
                    }
                }
            }
        }
    }
}

// copy relevant part of a given chunk to the box data grid
static void extractChunk(const wchar_t *world, int bx, int bz, IBox *edgeWorldBox )
{
    int chunkX, chunkZ;

    int loopXmin, loopZmin;
    int loopXmax, loopZmax;
    int x,y,z;

    int chunkIndex, boxIndex;
    int blockID;

    int notSchematic = (gOptions->pEFD->fileType != FILE_TYPE_SCHEMATIC);

    //IPoint loc;
    //unsigned char dataVal;

    WorldBlock *block;
    block=(WorldBlock *)Cache_Find(bx,bz);

    if (block==NULL)
    {
        wchar_t directory[256];
        wcsncpy_s(directory,256,world,255);
        wcscat_s(directory,256,L"/");
        if (gOptions->worldType&HELL)
        {
            wcscat_s(directory,256,L"DIM-1/");
        }
        if (gOptions->worldType&ENDER)
        {
            wcscat_s(directory,256,L"DIM1/");
        }

        block=LoadBlock(directory,bx,bz);
        if (block==NULL) //blank tile, nothing to do
            return;

        Cache_Add(bx,bz,block);
    }

    // loop through area of box that overlaps with this chunk
    chunkX = bx * 16;
    chunkZ = bz * 16;

    loopXmin = max(edgeWorldBox->min[X],chunkX);
    loopZmin = max(edgeWorldBox->min[Z],chunkZ);

    loopXmax = min(edgeWorldBox->max[X],chunkX+15);
    loopZmax = min(edgeWorldBox->max[Z],chunkZ+15);

    int useBiomes = ( gOptions->exportFlags & EXPT_BIOME );

    for ( x = loopXmin; x <= loopXmax; x++ ) {
        for ( z = loopZmin; z <= loopZmax; z++ ) {
            boxIndex = WORLD_TO_BOX_INDEX(x,edgeWorldBox->min[Y],z);
            chunkIndex = CHUNK_INDEX(bx,bz,x,edgeWorldBox->min[Y],z);
            if (useBiomes)
            {
                // X and Z location index is stored in the low-order bits; mask off Y location
                int biomeIdx = (x+gWorld2BoxOffset[X])*gBoxSize[Z] + z + gWorld2BoxOffset[Z];
                gBiome[biomeIdx] = block->biome[chunkIndex&0xff];
            }

            for ( y = edgeWorldBox->min[Y]; y <= edgeWorldBox->max[Y]; y++, boxIndex++ ) {
                // Get the extra values (orientation, type) for the blocks
                unsigned char dataVal = block->data[chunkIndex/2];
                if ( chunkIndex & 0x01 )
                    dataVal = dataVal >> 4;
                else
                    dataVal &= 0xf;
                gBoxData[boxIndex].data = dataVal;
                blockID = gBoxData[boxIndex].origType = 
                    gBoxData[boxIndex].type = block->grid[chunkIndex];

                // For Anvil, Y goes up by 256 (in 1.1 and earlier, it was just ++)
                chunkIndex += 256;

                // get bounds on y searches:
                // search for air, and if not found then
                // add to vertical bounds.
                // now done by findChunkBounds:
                //if ( blockID > BLOCK_AIR )
                //{
                //Vec3Scalar( loc, =, x,y,z );
                //addBounds(loc,&gSolidWorldBox);

                // special: if it's a wire, clear the data value. We use this later for
                // how the wires actually connect to each other.
                // TODO: We could still save the value, then use the high 4 bits for the
                // connection values. The only headache: need a new "wire off" set of tiles.
                if ( (blockID == BLOCK_REDSTONE_WIRE) && notSchematic )
                {
                    gBoxData[boxIndex].data = 0x0;
                }
                else if ( blockID == BLOCK_UNKNOWN )
                {
                    gBadBlocksInModel++;
                }
            }
        }
    }
}

static void modifySides( int editMode )
{
    // do X min and max sides first
    for ( int x = gAirBox.min[X]; x <= gAirBox.max[X]; x += gAirBox.max[X]-gAirBox.min[X] )
    {
        for ( int z = gAirBox.min[Z]; z <= gAirBox.max[Z]; z++ )
        {
            for ( int y = gAirBox.min[Y]; y <= gAirBox.max[Y]; y++ )
            {
                editBlock( x, y, z, editMode );
            }
        }
    }
    // now clear Z faces; note we don't have to clear vertical edges so can move X in by 1.
    for ( int x = gAirBox.min[X]+1; x < gAirBox.max[X]; x++ )
    {
        for ( int z = gAirBox.min[Z]; z <= gAirBox.max[Z]; z += gAirBox.max[Z]-gAirBox.min[Z] )
        {
            for ( int y = gAirBox.min[Y]; y <= gAirBox.max[Y]; y++ )
            {
                editBlock( x, y, z, editMode );
            }
        }
    }
}

static void modifySlab( int y, int editMode )
{
    for ( int x = gAirBox.min[X]; x <= gAirBox.max[X]; x++ )
    {
        for ( int z = gAirBox.min[Z]; z <= gAirBox.max[Z]; z++ )
        {
            editBlock( x, y, z, editMode );
        }
    }
}

static void editBlock( int x, int y, int z, int editMode )
{
    int boxIndex = BOX_INDEX(x,y,z);

    switch ( editMode )
    {
    case EDIT_MODE_CLEAR_TYPE:
        gBoxData[boxIndex].type = BLOCK_AIR;
        break;
    case EDIT_MODE_CLEAR_ALL:
        gBoxData[boxIndex].type = gBoxData[boxIndex].origType = BLOCK_AIR;
        // just to be safe, probably not necessary, but do it anyway:
        gBoxData[boxIndex].data = 0x0;
        break;
    case EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES:
        // if type is an entrance, clear it fully: done so seed propagation along borders happens properly
        if ( gBlockDefinitions[gBoxData[boxIndex].origType].flags & BLF_ENTRANCE )
        {
            gBoxData[boxIndex].origType = BLOCK_AIR;
        }
        gBoxData[boxIndex].type = BLOCK_AIR;
        break;
    default:
        assert(0);
        break;
    }
}

// remove snow blocks and anything else not desired
static int filterBox()
{
    int boxIndex;
    int x,y,z;
    // Push flattop onto block below
    int flatten = gOptions->pEFD->chkMergeFlattop;

    int retCode = MW_NO_ERROR;
    int foundBlock = 0;

    int outputFlags, retVal;

    // Filter out all stuff that is not to be rendered. Done before anything, as these blocks simply
    // should not exist for all operations beyond.
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                // sorry, air is never allowed to turn solid
                if ( gBoxData[boxIndex].type != BLOCK_AIR )
                {
                    int flags = gBlockDefinitions[gBoxData[boxIndex].type].flags;

                    // check if it's something to be filtered out: not in the output list or alpha is 0
                    if ( !(flags & gOptions->saveFilterFlags) ||
                        gBlockDefinitions[gBoxData[boxIndex].type].alpha <= 0.0 ) {
                            // things that should not be saved should be gone, gone, gone
                            gBoxData[boxIndex].type = gBoxData[boxIndex].origType = BLOCK_AIR;
                            gBoxData[boxIndex].data = 0x0;
                    }
                }
            }
        }
    }
    // what should we output? Only 3D bits (no billboards) if printing or if textures are off
    if ( gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
    {
        outputFlags = BLF_3D_BIT;
    }
    else
    {
        outputFlags = (BLF_BILLBOARD|BLF_SMALL_BILLBOARD|BLF_TRUE_GEOMETRY);
    }
    // check for billboards and lesser geometry - immediately output. Flatten that which should be flattened. 
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                // sorry, air is never allowed to turn solid
                if ( gBoxData[boxIndex].type != BLOCK_AIR )
                {
                    int flags = gBlockDefinitions[gBoxData[boxIndex].type].flags;
                    // check: is it a billboard we can export? Clear it out if so.
                    int blockProcessed = 0;
                    if ( gExportBillboards )
                    {
                        // If we're 3d printing, or rendering without textures, then export 3D printable bits,
                        // on the assumption that the software can merge the data properly with the solid model.
                        // TODO: Should any blocks that are bits get used to note connected objects,
                        // so that floaters are not deleted? Probably... but we don't try to test.
                        if ( flags & outputFlags )
                        {
                            // tricksy code, because I'm lazy: if the return value > 1, then it's an error
                            // and should be treated as such.
                            retVal = saveBillboardOrGeometry( boxIndex, gBoxData[boxIndex].type );
                            if ( retVal == 1 )
                            {
                                // this block is then cleared out, since it's been processed.
                                gBoxData[boxIndex].type = BLOCK_AIR;
                                foundBlock = 1;
                                blockProcessed = 1;
                            }
                            else if ( retVal >= MW_BEGIN_ERRORS )
                            {
                                return retVal;
                            }
                        }
                    }

                    // not filtered out by the basics or billboard
                    if ( !blockProcessed && flatten && ( flags & (BLF_FLATTOP|BLF_FLATSIDE) ) )
                    {
                        // this block is redstone, a rail, a ladder, etc. - shove its face to the top of the next cell down,
                        // or to its neighbor, or both (depends on dataval),
                        // instead of rendering a block for it.

                        // was: gBoxData[boxIndex-1].flatFlags = gBoxData[boxIndex].type;
                        // if object was indeed flattened, set it to air
                        if ( computeFlatFlags( boxIndex ) )
                        {
                            gBoxData[boxIndex].type = BLOCK_AIR;
                        }
                    }
                    // note that we found any sort of block that was valid (flats don't count, whatever
                    // they're pushed against needs to exist, too)
                    foundBlock |= (gBoxData[boxIndex].type > BLOCK_AIR);
                }
            }
        }
    }
    // 1%
    UPDATE_PROGRESS(0.70f*PG_MAKE_FACES);
    if ( foundBlock == 0 )
        // everything got filtered out!
        return retCode|MW_NO_BLOCKS_FOUND;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Filling, connecting, hollowing, melting.
    //
    // If we'd doing 3D printing, there are lots of things to worry about. Here are some:
    // 1) Hollow spots in the model that can't get cleared out of material. You need to make fair-size holes so that
    //    the unsolidified material inside can be removed. Fix by filling in these "bubbles".
    // 2) Non-manifold edges. For Minecraft, this always means "where two cubes touch at only an edge", i.e.
    //    diagonal (along exactly two axes - touching at a corner is fine). Fix by adding "weld blocks" next to them.
    // 3) Hanging objects. Tree tops, for example, commonly will sit in the air at the boundaries of the selection.
    //    Fix by deleting any groups of objects smaller than 16 blocks that don't touch the ground. 16 is chosen as
    //    16 blocks is the minimum to make a chain link at the end of a chain (a 5x5 square minus the 3x3 middle),
    //    which *is* legal.
    // 4) Unattached objects. While it's perfectly legal to have blocks touch at corners, the object might fall apart
    //    when created. Option: weld such blocks together if they're part of different groups.
    // 5) Wasted material. While bubbles are illegal, it's fine to hollow out the base of any object, working from the
    //    bottom up. Option: delete a block at the base if it has neighboring blocks in all 8 positions on this level
    //    and all 9 positions on the level above. This may be overconservative in some cases, but is safe. Mark all
    //    these positions, working up the object, then delete.

    gSolidGroups = gAirGroups = 0;

    if ( gOptions->pEFD->chkBlockFacesAtBorders )
    {
        // Clear slabs (sides are not set, so we don't need to clear them). We don't need the slab data for here on in
        // if borders are being output.
        modifySlab(gAirBox.min[Y],EDIT_MODE_CLEAR_ALL);
        modifySlab(gAirBox.max[Y],EDIT_MODE_CLEAR_ALL);
    }

    // do we need groups and neighbors?
    if ( gOptions->exportFlags & (EXPT_FILL_BUBBLES|EXPT_CONNECT_PARTS|EXPT_DELETE_FLOATING_OBJECTS|EXPT_DEBUG_SHOW_GROUPS) )
    {
        // If we're modifying blocks, we need to stash the border blocks away and clear them.
        if ( !gOptions->pEFD->chkBlockFacesAtBorders )
        {
            // not putting borders in final output, so need to do two things:
            // The types in the borders must all be cleared. The original type should be left intact for output, *except*:
            // any entrance blocks need to be fully cleared so that they don't mess with the seed propagation process.
            // This is the only use of "originalType" during this block editing process below.
            modifySides(EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
            modifySlab(gAirBox.min[Y],EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
            modifySlab(gAirBox.max[Y],EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
        }

        int foundTouching = 0;
        gGroupListSize = 200;
        gGroupList = (BoxGroup *)malloc(gGroupListSize*sizeof(BoxGroup));
        if ( gGroupList == NULL )
        {
            return retCode|MW_WORLD_EXPORT_TOO_LARGE;
        }

        memset(gGroupList,0,gGroupListSize*sizeof(BoxGroup));
        gGroupCount = 0;

        retCode |= findGroups();
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        // We now know a bit about how the blocks are grouped. The first group is always air, the second
        // group is always solid. If there are any more air groups, these are bubbles surround by one or more
        // solid groups and must be filled (and the solid groups merged). If there are corner tips that touch,
        // and the solid groups are different, and one of the solid groups is hanging in space, we should weld.
        // If there are objects that hang in the air (we tighten the Y bounds, so there will always be at least
        // one object that doesn't), if the group is small (16 or less), then delete it.

        // Note that if there is only one solid group, we don't need further processing: this group is what will
        // be cleaned up (manifold), hollowed, and exported.

        // Now that we have the groups and their bounds, start processing them.

        // 1) Hollow spots in the model that can't get cleared out of material. You need to make fair-size holes so that
        //    the unsolidified material inside can be removed. Fix by filling in these "bubbles".
        // Really, we could start the count at 2, since the first group is air,
        // the second group is something solid, and is a group at the base (minimum Y).
        // If we're deleting floaters, we need to merge groups first. This is done by merging bubbles but not changing from AIR.
        if ( gOptions->exportFlags & (EXPT_FILL_BUBBLES|EXPT_DELETE_FLOATING_OBJECTS) )
        {
            if ( gAirGroups > 1 )
            {
                checkAndRemoveBubbles();
            }
        }
        // 2%
        UPDATE_PROGRESS(0.80f*PG_MAKE_FACES);

        // 2) Non-manifold edges. For Minecraft, this always means "where two cubes touch at only an edge", i.e.
        //    diagonal (along exactly two axes - touching at a corner is fine). Fix by adding "weld blocks" next to them.
        // These are good to do next, as we want to build up all material before carving away.
        // If two different groups are welded together, add them to the same meta group.
        // Note that a single group can have non-manifold geometry, so this test must always be done (if flagged)
        // Also, check if when a block is next to be added, would it *make* another manifold edge? If so, pass it by until
        // we're desperate. If any like this are added, need to run manifold again (but do this later, after joining tips
        // below).
        do
        {
            if ( gOptions->exportFlags & EXPT_CONNECT_PARTS )
            {
                foundTouching = fixTouchingEdges();
            }

            // 4) Unattached corners. While it's perfectly legal to have blocks touch at corner tips, the object might fall apart
            //    when created. Option: weld such blocks together if they're part of different groups.
            // This is why we need metagroups, because holes could get filled and groups get joined.
            // If there is only one solid original group, we don't have to do this test.
            // We do this after doing edge part connections: our pass here puts in one block that will then get joined to
            // other parts out there, by edge welding. We add *after* because we want parts to get connected "normally" by
            // connecting edges.
            if ( gOptions->exportFlags & EXPT_CONNECT_CORNER_TIPS )
            {
                assert( gOptions->exportFlags & EXPT_CONNECT_PARTS );
                if ( gSolidGroups > 1 )
                {
                    foundTouching |= connectCornerTips();
                }
            }

            // run again if any non-manifold made manifold.
            // we must process until we find *no* non-manifold edges that are fixed. Fixing can create
            // more non-manifold (touching) edges.
        } while ( foundTouching );

        // 3.5%
        UPDATE_PROGRESS(0.85f*PG_MAKE_FACES);


        // 3) Hanging objects. Tree tops, for example, commonly will sit in the air at the boundaries of the selection.
        //    Fix by deleting any groups of objects smaller than 16 blocks that don't touch the ground. 16 is chosen as
        //    16 blocks is the minimum to make a chain link at the end of a chain (a 5x5 square minus the 3x3 middle),
        //    which *is* legal.
        // Now delete any tiny objects, unless there's only one group
        // Make this an option, as a person could be making "charms" and having
        // a bunch of little objects in a single order.
        if ( gOptions->exportFlags & EXPT_DELETE_FLOATING_OBJECTS )
        {
            // delete only if there's more than one solid group. One solid group means this is the object to output.
            if ( gSolidGroups > 1 )
            {
                // delete only groups that have a min Y > the base gMinY+1 level, i.e. aren't at ground level
                // OR delete tree (even at ground level - who wants a tree that will fall over?).
                deleteFloatingGroups();
            }

            // it's possible that all groups are deleted
            if ( gSolidGroups == 0 )
            {
                retCode |= MW_ALL_BLOCKS_DELETED;
                goto Exit;
            }
        }

        // if debug for groups is on, and materials are being output, then
        // change the alpha for the largest group to be semitransparent. In
        // this way you can see the small groups left over much more easily.
        // Set this while gGroupList is still around.
        if ( (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS) &&
            (gOptions->exportFlags & EXPT_OUTPUT_MATERIALS) )
        {
            int groupMaxID=-1;
            int maxPop = -1;
            int i;
            for ( i = 0; i <= gGroupCount; i++ )
            {
                if ( gGroupList[i].population > maxPop && gGroupList[i].solid )
                {
                    groupMaxID = gGroupList[i].groupID;
                    maxPop = gGroupList[i].population;
                }
            }
            assert(groupMaxID>=0);
            // now we know which group is the largest. Set its
            // alpha to semitransparent when material and texture are
            // output
            gDebugTransparentType = getMaterialUsingGroup(groupMaxID);
        }

Exit:
        free(gGroupList);
    }
    return retCode;
}

static int computeFlatFlags( int boxIndex )
{
    // for this box's contents, mark the neighbor(s) that should receive
    // its flatness
    IPoint loc;

    switch ( gBoxData[boxIndex].type )
    {
        // easy ones: flattops
    case BLOCK_RAIL:
        if ( gBoxData[boxIndex].data >= 6 )
        {
            // curved rail bit, it's always just flat
            gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
            break;
        }
        // NOTE: if curve test failed, needed only for basic rails, continue on through tilted track tests
        // SO - don't dare put a break here, we need to flow through, to avoid repeating all this code
    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        // only pay attention to sloped rails, as these mark sides;
        // remove top bit, as that's whether it's powered
        switch ( gBoxData[boxIndex].data & 0x7 )
        {
        case 2: // new east, +X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 3:
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        case 4:
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 5:
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        default:
            // don't do anything, this rail is not sloped; continue on down to mark top face
            break;
        }
        gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
        break;

        // the block below this one, if solid, gets marked
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_WOODEN_PRESSURE_PLATE:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY:
    case BLOCK_SNOW:
    case BLOCK_CARPET:
    case BLOCK_REDSTONE_REPEATER_OFF:
    case BLOCK_REDSTONE_REPEATER_ON:
    case BLOCK_REDSTONE_COMPARATOR_INACTIVE:
    case BLOCK_REDSTONE_COMPARATOR_ACTIVE:
    case BLOCK_LILY_PAD:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_BROWN_MUSHROOM:
    case BLOCK_RED_MUSHROOM:
    case BLOCK_SAPLING:
    case BLOCK_TALL_GRASS:
    case BLOCK_DEAD_BUSH:
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
    case BLOCK_DAYLIGHT_SENSOR:
    case BLOCK_INVERTED_DAYLIGHT_SENSOR:
    case BLOCK_DOUBLE_FLOWER:
        gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
        break;

    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        switch ( gBoxData[boxIndex].data )
        {
        case 1: // new east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        case 2:
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 3:
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 4:
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 5:
            gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
            break;
        default:
            // don't do anything, this torch is not touching a side
            break;
        }
        break;

    case BLOCK_REDSTONE_WIRE: // 0x37
        gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
        // look to see whether there is wire neighboring and above: if so, run this wire
        // up the sides of the blocks

        // first, is the block above the redstone wire not a whole block, or is a whole block and is glass on the outside or a piston?
        // If so, then wires can run up the sides; whole blocks that are not glass cut redstone wires.
        if ( !(gBlockDefinitions[gBoxData[boxIndex+1].origType].flags & BLF_WHOLE) ||
            (gBoxData[boxIndex+1].origType == BLOCK_PISTON) ||
            (gBoxData[boxIndex+1].origType == BLOCK_GLASS) ||
            (gBoxData[boxIndex+1].origType == BLOCK_STAINED_GLASS))
        {
            // first hurdle passed - now check each in turn: is block above wire. If so,
            // then these will connect. Note we must check again origType, as wires get culled out
            // as we go through the blocks.
            if ( gBoxData[boxIndex+1+gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE )
            {
                gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
                gBoxData[boxIndex+1+gBoxSizeYZ].data |= FLAT_FACE_LO_X;
                gBoxData[boxIndex].data |= FLAT_FACE_HI_X;
            }
            if ( gBoxData[boxIndex+1-gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE )
            {
                gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
                gBoxData[boxIndex+1-gBoxSizeYZ].data |= FLAT_FACE_HI_X;
                gBoxData[boxIndex].data |= FLAT_FACE_LO_X;
            }
            if ( gBoxData[boxIndex+1+gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE )
            {
                gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
                gBoxData[boxIndex+1+gBoxSize[Y]].data |= FLAT_FACE_LO_Z;
                gBoxData[boxIndex].data |= FLAT_FACE_HI_Z;
            }
            if ( gBoxData[boxIndex+1-gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE )
            {
                gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
                gBoxData[boxIndex+1-gBoxSize[Y]].data |= FLAT_FACE_HI_Z;
                gBoxData[boxIndex].data |= FLAT_FACE_LO_Z;
            }
        }
        // finally, check the +X and +Z neighbors on this level: if wire, connect them.
        // Note that the other wires in the world will pick up the other 6 possibilities:
        // -X and -Z on this level (by these same tests below) and the 4 "wires down a level"
        // possibilities (by these same tests above).
        // Test *all* things that redstone connects to. This could be a table, for speed.
        if ( (gBlockDefinitions[gBoxData[boxIndex+gBoxSizeYZ].origType].flags & BLF_CONNECTS_REDSTONE) ||
            // repeaters attach only at their ends, so test the direction they're at
            (gBoxData[boxIndex+gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_OFF && (gBoxData[boxIndex+gBoxSizeYZ].data & 0x1)) ||
            (gBoxData[boxIndex+gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_ON && (gBoxData[boxIndex+gBoxSizeYZ].data & 0x1))
            )
        {
            if ( gBoxData[boxIndex+gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE )
                gBoxData[boxIndex+gBoxSizeYZ].data |= FLAT_FACE_LO_X;
            gBoxData[boxIndex].data |= FLAT_FACE_HI_X;
        }
        if ( (gBlockDefinitions[gBoxData[boxIndex+gBoxSize[Y]].origType].flags & BLF_CONNECTS_REDSTONE) ||
            // repeaters attach only at their ends, so test the direction they're at
            (gBoxData[boxIndex+gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_OFF && !(gBoxData[boxIndex+gBoxSize[Y]].data & 0x1)) ||
            (gBoxData[boxIndex+gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_ON && !(gBoxData[boxIndex+gBoxSize[Y]].data & 0x1))
            )
        {
            if ( gBoxData[boxIndex+gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE )
                gBoxData[boxIndex+gBoxSize[Y]].data |= FLAT_FACE_LO_Z;
            gBoxData[boxIndex].data |= FLAT_FACE_HI_Z;
        }
        // catch redstone torches at the -X and -Z faces
        if ( (gBlockDefinitions[gBoxData[boxIndex-gBoxSizeYZ].origType].flags & BLF_CONNECTS_REDSTONE) ||
            // repeaters attach only at their ends, so test the direction they're at
            (gBoxData[boxIndex-gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_OFF && (gBoxData[boxIndex-gBoxSizeYZ].data & 0x1)) ||
            (gBoxData[boxIndex-gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_ON && (gBoxData[boxIndex-gBoxSizeYZ].data & 0x1))
            )
        {
            gBoxData[boxIndex].data |= FLAT_FACE_LO_X;
        }
        if ( (gBlockDefinitions[gBoxData[boxIndex-gBoxSize[Y]].origType].flags & BLF_CONNECTS_REDSTONE) ||
            // repeaters attach only at their ends, so test the direction they're at
            (gBoxData[boxIndex-gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_OFF && !(gBoxData[boxIndex-gBoxSize[Y]].data & 0x1)) ||
            (gBoxData[boxIndex-gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_ON && !(gBoxData[boxIndex-gBoxSize[Y]].data & 0x1))
            )
        {
            gBoxData[boxIndex].data |= FLAT_FACE_LO_Z;
        }

        // NOTE: even after all this the wiring won't perfectly match Minecraft's. For example:
        // http://i.imgur.com/Al7JI.png - upside-down steps don't block wires.

        break;

    case BLOCK_LADDER:
    case BLOCK_WALL_SIGN:
    case BLOCK_WALL_BANNER:
        switch ( gBoxData[boxIndex].data)
        {
        case 2: // new north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // new south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 4: // new west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 5: // new east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_LEVER:
        switch ( gBoxData[boxIndex].data & 0x7 )
        {
        case 1: // new east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        case 2:
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 3:
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 4:
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 5:
        case 6:
            gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
            break;
            // added in 1.3:
        case 7:	// pointing south
        case 0:	// pointing east
            gBoxData[boxIndex+1].flatFlags |= FLAT_FACE_BELOW;
            break;
        default:
            assert(0);
            return 0;
        }
        break;
    case BLOCK_STONE_BUTTON:
    case BLOCK_WOODEN_BUTTON:
        switch ( gBoxData[boxIndex].data & 0x7 )
        {
        case 4: // new north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // new south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 2: // new west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 1: // new east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_TRIPWIRE_HOOK:
        // 0x4 means "tripwire connected"
        // 0x8 means "tripwire tripped"
        switch ( gBoxData[boxIndex].data & 0x3 )
        {
        case 0: // new south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 1: // new west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 2: // new north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // new east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_TRAPDOOR:
    case BLOCK_IRON_TRAPDOOR:
        if ( gBoxData[boxIndex].data & 0x4 )
        {
            // trapdoor is open, so is against a wall
            switch ( gBoxData[boxIndex].data & 0x3 )
            {
            case 0: // new north, -Z
                gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
                break;
            case 1: // new south, +Z
                gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
                break;
            case 2: // new west, -X
                gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
                break;
            case 3: // new east, +X
                gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
                break;
            default:
                assert(0);
                return 0;
            }
        }
        else
        {
            // Not open, so connected to floor (if any!) or "roof". Very special case:
            // attached to roof?
            if ( gBoxData[boxIndex].data & 0x8 )
            {
                // Roof: don't need to do anything, should show up as full block.'
                return 0;
            }
            else
            {
                // On floor
                // if there's nothing below trapdoor, block below is set to trapdoor, if
                // Y is not too low
                if ( gBoxData[boxIndex-1].origType == BLOCK_AIR )
                {
                    boxIndexToLoc(loc, boxIndex);
                    if ( loc[Y] > gSolidBox.min[Y] )
                    {
                        gBoxData[boxIndex-1].origType = BLOCK_TRAPDOOR;
                    }
                }
                else
                {
                    // mark the solid box, as usual
                    gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
                }
            }
        }
        break;

    case BLOCK_VINES:
        // first, if this block was not originally a vine, then forget it - this block was generated
        // by a vine spreading to its neighbor - see below.
        if ( gBoxData[boxIndex].origType != BLOCK_VINES )
        {
            return 0;
        }
        // the rules: vines can cover up to four sides, or if no bits set, top of overhanging block.
        // The overhanging block stops side faces from appearing, essentially.
        // If billboarding is on and we're not printing, then we've already exported everything else of the vine, so remove it.
        if ( gBoxData[boxIndex].data == 0 || ( gExportBillboards && !gPrint3D) )
        {
            // top face, flatten to bottom of block above, if the neighbor exists. If it doesn't,
            // something odd is going on (this shouldn't happen).
            if ( gBoxData[boxIndex+1].origType != BLOCK_AIR )
            {
                gBoxData[boxIndex+1].flatFlags |= FLAT_FACE_BELOW;
            }
            else
            {
                // fill the block with vine, since we can't flatten it above for some reason
                // (it could well be that 
                return 0;
            }
        }
        // side faces: if it can be flattened to neighbor, great, do it, else actually *put*
        // a vine block in the empty neighbor space. Might work...
        else
        {
            // if a block is above a vine, there's always a below
            if ( gBoxData[boxIndex+1].origType != BLOCK_AIR )
            {
                gBoxData[boxIndex+1].flatFlags |= FLAT_FACE_BELOW;
            }
            if ( gBoxData[boxIndex].data & 0x1 )
            {
                // south face (+Z)
                // is there a neighbor large enough to composite a vine onto?
                // Right now we keep vines solid if they are next to leaves, so that the vines hanging down in air below these
                // will continue to look OK, i.e. fully connected as vines.
                // TODO shift the "air vines" inwards, as shown in the "else" statement. However, this code here is not
                // really the place to do it - vines could extend past the border, and if "seal tunnels" etc. is done things go
                // very wrong.
                if ( gBlockDefinitions[gBoxData[boxIndex+gBoxSize[Y]].type].flags & (BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF) &&
                    gBoxData[boxIndex+gBoxSize[Y]].type != BLOCK_LEAVES )
                {
                    // neighbor's a whole block, so shove the vine onto it
                    gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
                }
                else
                {
                    // force the block to become a vine - could be weird if there was something else here.
                    // This is not quite legal, first of all because we might set a location to solid that's outside the border
                    //gBoxData[boxIndex+gBoxSize[Y]].type = BLOCK_VINES;
                    //gBoxData[boxIndex+gBoxSize[Y]].data = 0x0;
                    return 0;
                }
            }
            if ( gBoxData[boxIndex].data & 0x2 )
            {
                // west face (-X)
                // is there a neighbor?
                if ( gBlockDefinitions[gBoxData[boxIndex-gBoxSizeYZ].type].flags & (BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF) &&
                    gBoxData[boxIndex-gBoxSizeYZ].type != BLOCK_LEAVES )
                {
                    // neighbor's a whole block, so shove the vine onto it
                    gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
                }
                else
                {
                    // force the block to become a vine - could be weird if there was something else here.
                    //gBoxData[boxIndex-gBoxSizeYZ].type = BLOCK_VINES;
                    //gBoxData[boxIndex-gBoxSizeYZ].data = 0x0;
                    return 0;
                }
            }
            if ( gBoxData[boxIndex].data & 0x4 )
            {
                // north face (-Z)
                // is there a neighbor?
                if ( gBlockDefinitions[gBoxData[boxIndex-gBoxSize[Y]].type].flags & (BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF) &&
                    gBoxData[boxIndex-gBoxSize[Y]].type != BLOCK_LEAVES )
                {
                    // neighbor's a real-live whole block, so shove the vine onto it
                    gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
                }
                else
                {
                    // TODO for rendering export, we really want vines to always be offset billboards, I believe
                    // force the block to become a vine - could be weird if there was something else here.
                    //gBoxData[boxIndex-gBoxSize[Y]].type = BLOCK_VINES;
                    //gBoxData[boxIndex-gBoxSize[Y]].data = 0x0;
                    return 0;
                }
            }
            if ( gBoxData[boxIndex].data & 0x8 )
            {
                // east face (+X)
                // is there a neighbor?
                if ( gBlockDefinitions[gBoxData[boxIndex+gBoxSizeYZ].type].flags & (BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF) &&
                    gBoxData[boxIndex+gBoxSizeYZ].type != BLOCK_LEAVES )
                {
                    // neighbor's a whole block, so shove the vine onto it
                    gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
                }
                else
                {
                    // force the block to become a vine - could be weird if there was something else here.
                    //gBoxData[boxIndex+gBoxSizeYZ].type = BLOCK_VINES;
                    //gBoxData[boxIndex+gBoxSizeYZ].data = 0x0;
                    return 0;
                }
            }
        }
        break;

    default:
        // something needs to be added to the cases above!
        assert(0);
        return 0;

        // TODO: we *could* mark doors like this, checking if the door is next to a wall.
        // But, that's pretty rare, I think: doors are usually closed and in air, so we
        // always render them as blocks (user can always delete doors if they don't like it).
        // If we *did* flatten doors, we'd have to return a code saying the door did get flattened
        // and should be removed.
    }

    return 1;
}

static void identityMtx(float mtx[4][4])
{
    int i,j;
    for ( i = 0; i < 4; i++ )
        for ( j = 0; j < 4; j++ )
            mtx[i][j] = (float)(i == j);
}
// multiply mtx1 by mtx2, put result in mtx1
static void multiplyMtx(float mtx1[4][4],float mtx2[4][4])
{
    int i,j,k;
    float mtx[4][4];
    for ( i = 0; i < 4; i++ )
    {
        for ( j = 0; j < 4; j++ )
        {
            mtx[i][j] = 0.0f;
            for (k=0; k<4; k++)
                mtx[i][j] += mtx1[i][k] * mtx2[k][j];
        }
    }

    // could really just use memcpy
    for ( i = 0; i < 4; i++ )
        for ( j = 0; j < 4; j++ )
            mtx1[i][j] = mtx[i][j];
}
static void translateMtx(float mtx[4][4], float tx, float ty, float tz)
{
    float tmtx[4][4];
    identityMtx( tmtx );
    Vec3Scalar( tmtx[3], =, tx, ty, tz );
    multiplyMtx( mtx, tmtx );
}
// translates center of block to origin
static void translateToOriginMtx(float mtx[4][4], int boxIndex)
{
    IPoint anchor;
    boxIndexToLoc( anchor, boxIndex );
    translateMtx( mtx, VecList(-0.5f-(float)anchor) );
}
static void translateFromOriginMtx(float mtx[4][4], int boxIndex)
{
    IPoint anchor;
    boxIndexToLoc( anchor, boxIndex );
    translateMtx( mtx, VecList(0.5f+(float)anchor) );
}
static void scaleMtx(float mtx[4][4], float sx, float sy, float sz)
{
    float smtx[4][4];
    identityMtx( smtx );
    smtx[0][0] = sx;
    smtx[1][1] = sy;
    smtx[2][2] = sz;
    multiplyMtx( mtx, smtx );
}
#define DEGREES_TO_RADIANS (3.14159265358979323846/180.0)
static void rotateMtx(float mtx[4][4], float xAngle, float yAngle, float zAngle )
{
    Point angle;
    int i;
    float rmtx[4][4];

    Vec3Scalar( angle, =, xAngle, yAngle, zAngle );
    for ( i = 0; i < 3; i++ )
    {
        if ( angle[i] != 0.0f && angle[i] != 360.0f )
        {
            int u,v;
            float cosAngle,sinAngle;

            identityMtx(rmtx);
            u = (i+1)%3;
            v = (i+2)%3;

            // check for perfect rotations (common!)
            if ( angle[i] == 90.0f )
            {
                cosAngle = 0.0f;
                sinAngle = 1.0f;
            }
            else if ( angle[i] == 180.0f )
            {
                cosAngle = -1.0f;
                sinAngle = 0.0f;
            }
            else if ( angle[i] == 270.0f )
            {
                cosAngle = 0.0f;
                sinAngle = -1.0f;
            }
            else
            {
                cosAngle = (float)cos((double)angle[i]*DEGREES_TO_RADIANS);
                sinAngle = (float)sin((double)angle[i]*DEGREES_TO_RADIANS);
            }
            rmtx[u][u] = rmtx[v][v] = cosAngle;
            rmtx[u][v] = -sinAngle;
            rmtx[v][u] = sinAngle;

            multiplyMtx(mtx,rmtx);
        }
    }
}
static void shearMtx(float mtx[4][4], float shx, float shy)
{
    float shmtx[4][4];
    identityMtx( shmtx );
    shmtx[0][2] = shx;
    shmtx[1][2] = shy;
    multiplyMtx( mtx, shmtx );
}
static void transformVertices(int count,float mtx[4][4])
{
    int i,j,vert;
    for ( vert = gModel.vertexCount-count; vert < gModel.vertexCount; vert++ )
    {
        Point resVertex;
        for ( i = 0; i < 3; i++ )
        {
            resVertex[i] = mtx[3][i];   // translation
            for ( j = 0; j < 3; j++ )
            {
                resVertex[i] += gModel.vertices[vert][j] * mtx[j][i];
            }
        }
        Vec2Op( gModel.vertices[vert], =, resVertex );
    }
}

// silly sleaze: if we're exporting individual blocks, set the first face index for a block being output to be negative.
// Saves us from having to allocate a boolean just to keep track of this. Note that 0 is always treated as the "first face"
// of a block anyway (so negative zero is not needed).
static int firstFaceModifier( int isFirst, int faceIndex )
{
    if ( (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK) && isFirst )
        return -faceIndex;
    else
        return faceIndex;
}

// return 1 if block processed as a billboard or true geometry
static int saveBillboardOrGeometry( int boxIndex, int type )
{
    int dataVal, minx, maxx, miny, maxy, minz, maxz, faceMask, tbFaceMask, bitAdd, dir;
    int swatchLoc, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc;
    int topDataVal, bottomDataVal, shiftVal, neighborType, neighborDataVal;
    int i, hasPost, firstFace, totalVertexCount, littleTotalVertexCount, uberTotalVertexCount, typeBelow, dataValBelow, useInsidesAndBottom, filled;
    float xrot, yrot, zrot;
    float mtx[4][4], angle, hingeAngle, signMult;
    int swatchLocSet[6];
    // how much to add to dimension when fattening
    int fatten = (gOptions->pEFD->chkFatten) ? 2 : 0;
    int retCode = MW_NO_ERROR;
    int transNeighbor,boxIndexBelow;
    int groupByBlock;
    int waterHeight;


    dataVal = gBoxData[boxIndex].data;

    // Add to minor count if this object has some heft. This is approximate, but better than nothing.
    if ( gBlockDefinitions[type].flags & (BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF|BLF_MIDDLER|BLF_PANE))
    {
        gMinorBlockCount++;
    }

    switch ( type )
    {
    case BLOCK_SAPLING:
    case BLOCK_COBWEB:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_RED_MUSHROOM:
    case BLOCK_BROWN_MUSHROOM:
    case BLOCK_TALL_GRASS:
    case BLOCK_DEAD_BUSH:
    case BLOCK_SUGAR_CANE:
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
    case BLOCK_DOUBLE_FLOWER:
        return saveBillboardFaces( boxIndex, type, BB_FULL_CROSS );

    case BLOCK_CROPS:
    case BLOCK_NETHER_WART:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES:
        return saveBillboardFaces( boxIndex, type, BB_GRID );
        break;
    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        return saveBillboardFaces( boxIndex, type, BB_TORCH );
        break;
    case BLOCK_RAIL:
    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            return saveBillboardFaces( boxIndex, type, BB_RAILS );
        }
        else
        {
            int modDataVal = dataVal;
            // for printing, angled pieces get triangle blocks.
            // first check dataVal to see if it's a triangle, and remove top bit if so.
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            switch ( type )
            {
            case BLOCK_POWERED_RAIL:
                if ( !(modDataVal & 0x8) )
                {
                    // unpowered rail
                    swatchLoc = SWATCH_INDEX( 3, 10 );
                }
                // if not a normal rail, there are no curve bits, so mask off upper bit, which is
                // whether the rail is powered or not.
                modDataVal &= 0x7;
                break;
            case BLOCK_DETECTOR_RAIL:
                // by default, the detector rail is in its undetected state
                if ( modDataVal & 0x8 )
                {
                    // rail detector activated (same tile in basic game)
                    swatchLoc = SWATCH_INDEX( 11,17 );
                }
                // if not a normal rail, there are no curve bits, so mask off upper bit, which is
                // whether the rail is powered or not.
                modDataVal &= 0x7;
                break;
            case BLOCK_ACTIVATOR_RAIL:
                if ( modDataVal & 0x8 )
                {
                    // activated rail
                    swatchLoc = SWATCH_INDEX( 9,17 );
                }
                // if not a normal rail, there are no curve bits, so mask off upper bit, which is
                // whether the rail is powered or not.
                modDataVal &= 0x7;
                break;
            }
            // do for all rails
            switch ( modDataVal )
            {
            case 2:
            case 3:
            case 4:
            case 5:
                // sloping, so continue
                break;
            default:
                // it's a flat
                gMinorBlockCount--;
                return 0;
            }

            // it's sloping, so check if object below it is not air
            typeBelow = gBoxData[boxIndex-1].origType;
            if ( typeBelow == BLOCK_AIR )
            {
                // air below, which means this rail's at the bottom level, descending.
                // Since we have no idea what's below, then, we need to look at the
                // track's neighbor for a material, as a guess.
                // TODO: if we really wanted to, we could store the bottom level of stuff
                // in the database, then clear it after this pass.
                transNeighbor = boxIndex;
                switch ( modDataVal )
                {
                case 2:
                    transNeighbor = DIRECTION_BLOCK_SIDE_HI_X;
                    break;
                case 3:
                    transNeighbor = DIRECTION_BLOCK_SIDE_LO_X;
                    break;
                case 4:
                    transNeighbor = DIRECTION_BLOCK_SIDE_LO_Z;
                    break;
                case 5:
                    transNeighbor = DIRECTION_BLOCK_SIDE_HI_Z;
                    break;
                default:
                    // huh?
                    assert(0);
                }
                boxIndexBelow = boxIndex+gFaceOffset[transNeighbor];
                typeBelow = gBoxData[boxIndex+gFaceOffset[transNeighbor]].origType;
                // make sure the block to the side is something valid for a rail to be on
                if ( gBlockDefinitions[typeBelow].flags & BLF_WHOLE )
                {
                    dataValBelow = gBoxData[boxIndexBelow].data;
                }
                else
                {
                    // couldn't find the whole-block neighbor, how weird - just pick cobblestone
                    assert(0);
                    typeBelow = BLOCK_COBBLESTONE;
                    dataValBelow = 0;
                }
            }
            else
            {
                boxIndexBelow = boxIndex-1;
                dataValBelow = gBoxData[boxIndexBelow].data;
            }

            // brute force the four cases: always draw bottom of block as the thing, use top of block for decal,
            // use sides for triangles. Really, we'll just use the top for everything for now (TODO), as it's kind of
            // a bogus object anyway (no right answer).
            switch ( modDataVal )
            {
            case 2: // ascend east +x
                retCode |= saveTriangleGeometry( type, dataVal, boxIndex, typeBelow, dataValBelow, boxIndexBelow, DIR_LO_X_BIT );
                break;
            case 3: // ascend west -x
                retCode |= saveTriangleGeometry( type, dataVal, boxIndex, typeBelow, dataValBelow, boxIndexBelow, DIR_HI_X_BIT );
                break;
            case 4: // ascend north -z
                retCode |= saveTriangleGeometry( type, dataVal, boxIndex, typeBelow, dataValBelow, boxIndexBelow, DIR_HI_Z_BIT );
                break;
            case 5: // ascend south +z
                retCode |= saveTriangleGeometry( type, dataVal, boxIndex, typeBelow, dataValBelow, boxIndexBelow, DIR_LO_Z_BIT );
                break;
            default:
                // it's a flat, so flatten
                return(0);
            }

            if ( retCode >= MW_BEGIN_ERRORS ) return retCode;
        }
        break;


    case BLOCK_FIRE:
        return saveBillboardFaces( boxIndex, type, BB_FIRE );

    case BLOCK_VINES:
        return saveBillboardFaces( boxIndex, type, BB_SIDE );


        // real-live output, baby
    case BLOCK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
        groupByBlock = (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK);
        // if fence is to be fattened, instead make it like a brick wall - stronger
        if ( fatten )
        {
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            hasPost = 0;
            // if there's *anything* above the wall, put the post
            if ( gBoxData[boxIndex+1].origType != 0 )
            {
                hasPost = 1;
            }
            else
            {
                // else, test if there are neighbors and not across from one another.
                int xCount = 0;
                int zCount = 0;
                neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
                if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
                {
                    xCount++;
                }
                neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
                if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
                {
                    xCount++;
                }
                neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
                if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
                {
                    zCount++;
                }
                neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
                if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
                {
                    zCount++;
                }

                // for cobblestone walls, if the count is anything but 2, put the post
                if ( (xCount != 2) && (zCount != 2) )
                {
                    // cobblestone post
                    hasPost = 1;
                }
            }

            if ( hasPost )
            {
                saveBoxTileGeometry( boxIndex, type, swatchLoc, 1, 0x0, 4,12, 0,16, 4,12 );
                firstFace = 0;
            }
            else
            {
                firstFace = 1;
            }

            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                // - if we're doing 3D printing, neighbor type must exactly match for the face to be removed
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_HI_X_BIT)|(transNeighbor?0x0:DIR_LO_X_BIT), 0,8-hasPost*4,  0,13,  5,11 );
                firstFace = 0;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_LO_X_BIT)|(transNeighbor?0x0:DIR_HI_X_BIT), 8+hasPost*4,16,  0,13,  5,11 );
                firstFace = 0;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_HI_Z_BIT)|(transNeighbor?0x0:DIR_LO_Z_BIT), 5,11,  0,13,  0,8-hasPost*4 );
                firstFace = 0;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_HI_Z_BIT)|(transNeighbor?0x0:DIR_HI_Z_BIT), 5,11,  0,13,  8+hasPost*4,16 );
                firstFace = 0;
            }
        }
        else
        {
            // don't really need to "fatten", as it's now done above, but keeping fatten around is good to know.

            // post, always output
            saveBoxGeometry( boxIndex, type, 1, 0x0, 6-fatten,10+fatten, 0,16, 6-fatten,10+fatten);
            // which posts are needed: NSEW. Brute-force it.

            // since we erase "billboard" objects as we go, we need to test against origType.
            // Note that if a render export chops through a fence, the fence will not join.
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_HI_X_BIT)|(transNeighbor?0x0:DIR_LO_X_BIT), 0,6-fatten, 6,9,  7-fatten,9+fatten );
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_HI_X_BIT)|(transNeighbor?0x0:DIR_LO_X_BIT), 0,6-fatten, 12,15,  7-fatten,9+fatten );
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_LO_X_BIT)|(transNeighbor?0x0:DIR_HI_X_BIT), 10+fatten,16, 6,9,  7-fatten,9+fatten );
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_LO_X_BIT)|(transNeighbor?0x0:DIR_HI_X_BIT), 10+fatten,16, 12,15,  7-fatten,9+fatten );
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_HI_Z_BIT)|(transNeighbor?0x0:DIR_LO_Z_BIT), 7-fatten,9+fatten, 6,9,  0,6-fatten );
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_HI_Z_BIT)|(transNeighbor?0x0:DIR_LO_Z_BIT), 7-fatten,9+fatten, 12,15,  0,6-fatten );
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_LO_Z_BIT)|(transNeighbor?0x0:DIR_HI_Z_BIT), 7-fatten,9+fatten, 6,9,  10+fatten,16 );
                saveBoxGeometry( boxIndex, type, 0, (gPrint3D?0x0:DIR_LO_Z_BIT)|(transNeighbor?0x0:DIR_HI_Z_BIT), 7-fatten,9+fatten, 12,15,  10+fatten,16 );
            }
        }
        break;

    case BLOCK_COBBLESTONE_WALL:
        groupByBlock = (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK);
        // which posts are needed: NSEW. Brute-force it.

        // TODO: get more subtle, like glass panes, and generate only the faces needed. Right now there's overlap at corners, for example.

        // since we erase "billboard" objects as we go, we need to test against origType.
        // Note that if a render export chops through a fence, the fence will not join.
        swatchLoc = ( dataVal == 0x1 ) ? SWATCH_INDEX( gBlockDefinitions[BLOCK_MOSS_STONE].txrX, gBlockDefinitions[BLOCK_MOSS_STONE].txrY ) :
            SWATCH_INDEX( gBlockDefinitions[BLOCK_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_COBBLESTONE].txrY );

        hasPost = 0;
        // if there's *anything* above the wall, put the post
        if ( gBoxData[boxIndex+1].origType != 0 )
        {
            hasPost = 1;
        }
        else
        {
            // else, test if there are neighbors and not across from one another.
            int xCount = 0;
            int zCount = 0;
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                xCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                xCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                zCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
            {
                zCount++;
            }

            // for cobblestone walls, if the count is anything but 2, put the post
            if ( (xCount != 2) && (zCount != 2) )
            {
                // cobblestone post
                hasPost = 1;
            }
        }

        // test texturing with a 2x2 grid
        //saveBoxTileGeometry( boxIndex, type, swatchLoc, 1, 0x0, 0,7, 16,16, 0,7 );
        //saveBoxTileGeometry( boxIndex, type, swatchLoc, 0, 0x0, 0,7, 16,16, 9,16 );
        //saveBoxTileGeometry( boxIndex, type, swatchLoc, 0, 0x0, 9,16, 16,16, 0,7 );
        //saveBoxTileGeometry( boxIndex, type, swatchLoc, 0, 0x0, 9,16, 16,16, 9,16 );

        if ( hasPost )
        {
            saveBoxTileGeometry( boxIndex, type, swatchLoc, 1, 0x0, 4,12, 0,16, 4,12 );
            firstFace = 0;
        }
        else
        {
            firstFace = 1;
        }

        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
            saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_HI_X_BIT)|(transNeighbor?0x0:DIR_LO_X_BIT), 0,8-hasPost*4,  0,13,  5,11 );
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
            saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_LO_X_BIT)|(transNeighbor?0x0:DIR_HI_X_BIT), 8+hasPost*4,16,  0,13,  5,11 );
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
            saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_HI_Z_BIT)|(transNeighbor?0x0:DIR_LO_Z_BIT), 5,11,  0,13,  0,8-hasPost*4 );
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) )
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
            saveBoxTileGeometry( boxIndex, type, swatchLoc, firstFace, (gPrint3D?0x0:DIR_LO_Z_BIT)|(transNeighbor?0x0:DIR_HI_Z_BIT), 5,11,  0,13,  8+hasPost*4,16 );
            firstFace = 0;
        }
        break;

    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_WOODEN_PRESSURE_PLATE:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY:
        // if printing and the location below the plate is empty, then don't make plate (it'll be too thin)
        if ( gPrint3D &&
            ( gBoxData[boxIndex-1].origType == BLOCK_AIR ) )
        {
            gMinorBlockCount--;
            return 0;
        }
        // the only reason I fatten here is because plates get used for table tops sometimes...
        // note we don't use gUsingTransform here, because if bottom of plate can match, remove it
        saveBoxGeometry( boxIndex, type, 1, 0x0, 1,15, 0,1+fatten, 1,15);
        if ( dataVal & 0x1 )
        {
            // pressed, kick it down half a pixel
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, -0.5f/16.0f, 0.5/16.0f);
            transformVertices(8,mtx);
        }
        break;

    case BLOCK_CARPET:
        // if printing and the location below the carpet is empty, then don't make carpet (it'll be too thin)
        if ( gPrint3D &&
            ( gBoxData[boxIndex-1].origType == BLOCK_AIR ) )
        {
            gMinorBlockCount--;
            return 0;
        }
        // yes, we fall through to wool here
    case BLOCK_WOOL:
        // use dataVal to retrieve location. These are scattered all over.
        swatchLoc = retrieveWoolSwatch(dataVal);
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0,16, 0,1, 0,16 );
        break;

    case BLOCK_OAK_WOOD_STAIRS:
    case BLOCK_COBBLESTONE_STAIRS:
    case BLOCK_BRICK_STAIRS:
    case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_NETHER_BRICK_STAIRS:
    case BLOCK_SANDSTONE_STAIRS:
    case BLOCK_SPRUCE_WOOD_STAIRS:
    case BLOCK_BIRCH_WOOD_STAIRS:
    case BLOCK_JUNGLE_WOOD_STAIRS:
    case BLOCK_ACACIA_WOOD_STAIRS:
    case BLOCK_DARK_OAK_WOOD_STAIRS:
    case BLOCK_QUARTZ_STAIRS:
    case BLOCK_RED_SANDSTONE_STAIRS:

        // set up textures
        switch ( type )
        {
        default:
            assert(0);
        case BLOCK_OAK_WOOD_STAIRS:
        case BLOCK_COBBLESTONE_STAIRS:
        case BLOCK_BRICK_STAIRS:
        case BLOCK_STONE_BRICK_STAIRS:
        case BLOCK_NETHER_BRICK_STAIRS:
        case BLOCK_SPRUCE_WOOD_STAIRS:
        case BLOCK_BIRCH_WOOD_STAIRS:
        case BLOCK_JUNGLE_WOOD_STAIRS:
        case BLOCK_ACACIA_WOOD_STAIRS:
        case BLOCK_DARK_OAK_WOOD_STAIRS:
        case BLOCK_QUARTZ_STAIRS:
            topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            break;
        case BLOCK_SANDSTONE_STAIRS:
            topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY );
            sideSwatchLoc = SWATCH_INDEX( 0,12 );
            bottomSwatchLoc = SWATCH_INDEX( 0,13 );
            break;
        case BLOCK_RED_SANDSTONE_STAIRS:
            topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_RED_SANDSTONE_STAIRS].txrX, gBlockDefinitions[BLOCK_RED_SANDSTONE_STAIRS].txrY );
            sideSwatchLoc = SWATCH_INDEX( 14,13 );
            bottomSwatchLoc = SWATCH_INDEX( 5,8 );
            break;
        }

        {
            // The stairs block has a full level (a full slab) on one level. Our task is to find which of
            // the four boxes are filled on the other level: it could be 1, 2, or 3. Normal stairs have
            // 2 filled, but as they get next to other stairs, they can change, losing or adding a block.
            // The rules are as follows:
            // If the neighbor stair behind our stair block turns to left or right compared to the stair,
            // then subtract the block not touched by the neighboring stair, but only if there is no
            // neighbor stair to the left or right with the same orientation.
            // Else, if the neighbor stair in front of our stair block turns left or right, then add
            // a block to left or right, but only if there is no neighbor stair to the right or left
            // (note the reversal from the subtraction case) with the same orientation.
            // Else, the stair block remains as is.
            //
            // This is mostly table-driven.
            // The mask bits are as follows
            //
            //        lo hi
            //    lo   0  1
            //    hi   2  3
            //
            // The value sets the 1<<bit in the mask. So, bits 2 and 3 would give 4 + 8 = 12.
            // Table is oriented along world lines, and recall that North is -Z, East is +X.
            typedef struct StairsTable {
                unsigned int mask;  // which of the four blocks is set
                int backDir;
                unsigned int behind[4];
                int sideDir[4];     // which side neighbor is important for overcoming the subtractive effect of the stair behind
                unsigned int front[4];
            } StairsTable;
            const StairsTable stairs[4]={
                { 10, DIRECTION_BLOCK_SIDE_HI_X,  {0,0,8,2}, {0,0,DIRECTION_BLOCK_SIDE_LO_Z,DIRECTION_BLOCK_SIDE_HI_Z}, {0,0,14,11} },
                {  5, DIRECTION_BLOCK_SIDE_LO_X,  {0,0,4,1}, {0,0,DIRECTION_BLOCK_SIDE_LO_Z,DIRECTION_BLOCK_SIDE_HI_Z}, {0,0,13,7} },
                { 12, DIRECTION_BLOCK_SIDE_HI_Z,  {8,4,0,0}, {DIRECTION_BLOCK_SIDE_LO_X,DIRECTION_BLOCK_SIDE_HI_X,0,0}, {14,13,0,0} },
                {  3, DIRECTION_BLOCK_SIDE_LO_Z,  {2,1,0,0}, {DIRECTION_BLOCK_SIDE_LO_X,DIRECTION_BLOCK_SIDE_HI_X,0,0}, {11,7,0,0} }
            };

            // The bottom 2 bits is direction of step.
            int stepDir = (dataVal & 0x3);
            int stepLevel = (dataVal & 0x4);
            unsigned int stepMask = stairs[stepDir].mask;
            bool sideNeighbor;
            unsigned int newMask;

            int neighborIndex = boxIndex+gFaceOffset[stairs[stepDir].backDir];
            neighborType = gBoxData[neighborIndex].origType;
            // is there a stairs behind us that subtracted a block?
            bool subtractedBlock = false;
            if ( gBlockDefinitions[neighborType].flags & BLF_STAIRS )
            {
                // get the data value and check it
                neighborDataVal = gBoxData[neighborIndex].data;

                // first, are slabs on same level?
                if ( (neighborDataVal & 0x4) == stepLevel )
                {
                    // On the same level. Is neighbor value one of the values that can affect this block?
                    newMask = stairs[stepDir].behind[(neighborDataVal & 0x3)];
                    if ( newMask != 0x0 )
                    {
                        // The behind value indeed affects the step. Now we need to check if the corresponding
                        // neighbor to the side is a stairs, at the same level, and has the same orientation.
                        // If so, we ignore subtraction, else allow it. Basically, steps next to steps keep
                        // the step's upper step "in place" without subtraction.
                        sideNeighbor = false;
                        neighborIndex = boxIndex+gFaceOffset[stairs[stepDir].sideDir[(neighborDataVal & 0x3)]];
                        assert( neighborIndex != boxIndex );
                        neighborType = gBoxData[neighborIndex].origType;
                        // is there a stairs to the key side of us?
                        if ( gBlockDefinitions[neighborType].flags & BLF_STAIRS )
                        {
                            // get the data value and check it
                            neighborDataVal = gBoxData[neighborIndex].data;

                            // first, are slabs on same level?
                            if ( (neighborDataVal & 0x4) == stepLevel )
                            {
                                // On the same level. Is neighbor value the same as the block's value, i.e. are the
                                // stairs facing the same direction?
                                if ( (neighborDataVal & 0x3) == stepDir )
                                {
                                    // so, this stairs hold the stair step in place, no subtraction.
                                    sideNeighbor = true;
                                }
                            }
                        }

                        if ( !sideNeighbor )
                        {
                            // No side neighbor holding the step in place, so set this as the new mask, and we're done;
                            // subtraction takes precedence over addition.
                            stepMask = newMask;
                            subtractedBlock = true;
                        }
                    }
                }
            }

            // if a subtraction didn't happen, then we can test for an addition
            if ( !subtractedBlock )
            {
                // now check the neighbor in front, in a similar manner.
                neighborIndex = boxIndex+gFaceOffset[(stairs[stepDir].backDir+3)%6];
                neighborType = gBoxData[neighborIndex].origType;
                // is there a stairs in front of us?
                if ( gBlockDefinitions[neighborType].flags & BLF_STAIRS )
                {
                    // get the data value and check it
                    neighborDataVal = gBoxData[neighborIndex].data;

                    // first, are slabs on same level?
                    if ( (neighborDataVal & 0x4) == stepLevel )
                    {
                        // On the same level. Is neighbor value one of the values that can affect this block?
                        newMask = stairs[stepDir].front[(neighborDataVal & 0x3)];
                        if ( newMask != 0x0 )
                        {
                            // The front value indeed affects the step. Now we need to check if the corresponding
                            // neighbor to the side is a stairs, at the same level, and has the same orientation.
                            // If so, we ignore addition, else allow it. Basically, steps next to steps keep
                            // the step's upper step "in place" without addition.
                            sideNeighbor = false;
                            neighborIndex = boxIndex+gFaceOffset[(stairs[stepDir].sideDir[(neighborDataVal & 0x3)]+3)%6];
                            assert( neighborIndex != boxIndex );
                            neighborType = gBoxData[neighborIndex].origType;
                            // is there a stairs to the key side of us?
                            if ( gBlockDefinitions[neighborType].flags & BLF_STAIRS )
                            {
                                // get the data value and check it
                                neighborDataVal = gBoxData[neighborIndex].data;

                                // first, are slabs on same level?
                                if ( (neighborDataVal & 0x4) == stepLevel )
                                {
                                    // On the same level. Is neighbor value the same as the block's value, i.e. are the
                                    // stairs facing the same direction?
                                    if ( (neighborDataVal & 0x3) == stepDir )
                                    {
                                        // so, this stairs hold the stair step in place, no subtraction.
                                        sideNeighbor = true;
                                    }
                                }
                            }

                            if ( !sideNeighbor )
                            {
                                // No side neighbor holding the step in place, so set this as the new mask, and we're done;
                                // subtraction takes precedence over addition.
                                stepMask = newMask;
                            }
                        }
                    }
                }
            }

            // We now have the mask in stepMask of what blocks to output.

            // Use the level bit for creating the slab itself
            // The 0x4 bit is about whether the bottom of the stairs is in the top half or bottom half (used to always be bottom half).
            // See http://www.minecraftwiki.net/wiki/Block_ids#Stairs
            if ( stepLevel )
            {
                // upper slab
                miny = 8;
                maxy = 16;
            }
            else
            {
                // lower slab
                miny = 0;
                maxy = 8;
            }
            saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0,16, miny,maxy, 0,16);

            // Now create the larger 2x1 box, if found
            minx = minz = 0;
            maxx = maxz = 16;
            assert( (stepMask != 0) && (stepMask != 0xff) );
            bool outputStep = false;
            if ( (stepMask & 0x3) == 0x3 )
            {
                // north step covered
                outputStep = true;
                maxz = 8;
                stepMask &= ~0x3;
            }
            else if ( (stepMask & 0xC) == 0xC )
            {
                // south step covered
                outputStep = true;
                minz = 8;
                stepMask &= ~0xC;
            }
            else if ( (stepMask & 0x5) == 0x5 )
            {
                // west step covered
                outputStep = true;
                maxx = 8;
                stepMask &= ~0x5;
            }
            else if ( (stepMask & 0xA) == 0xA )
            {
                // east step covered
                outputStep = true;
                minx = 8;
                stepMask &= ~0xA;
            }

            if ( outputStep )
            {
                if ( stepLevel )
                {
                    // lower step (stairs is upside down)
                    miny = 0;
                    maxy = 8;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_TOP_BIT;
                }
                else
                {
                    // upper step
                    miny = 8;
                    maxy = 16;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_BOTTOM_BIT;
                }
                saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, faceMask, 0, minx,maxx, miny,maxy, minz,maxz);
            }

            // anything left? output that little box
            if ( stepMask != 0x0 )
            {
                // TODO: we could get mad fancy here: we could check the direction of the 2x1 step (if it exists at all) and mask another side face from this little
                // box if we wanted to do so. A lot of work...
                if ( stepLevel )
                {
                    // lower step (stairs is upside down)
                    miny = 0;
                    maxy = 8;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_TOP_BIT;
                }
                else
                {
                    // upper step
                    miny = 8;
                    maxy = 16;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_BOTTOM_BIT;
                }
                minx = minz = 0;
                maxx = maxz = 16;
                switch ( stepMask )
                {
                case 0x1:
                    // upper left (remember that upper is negative direction)
                    maxx = 8;
                    maxz = 8;
                    break;
                case 0x2:
                    // upper right
                    minx = 8;
                    maxz = 8;
                    break;
                case 0x4:
                    // lower left
                    maxx = 8;
                    minz = 8;
                    break;
                case 0x8:
                    // lower right
                    minx = 8;
                    minz = 8;
                    break;
                default:
                    // should never get here
                    assert(0);
                }
                saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, faceMask, 0, minx,maxx, miny,maxy, minz,maxz);
            }
        }

        break;

    case BLOCK_STONE_SLAB:
    case BLOCK_WOODEN_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
        switch ( type )
        {
        default:
            assert(0);
        case BLOCK_STONE_SLAB:
            switch ( dataVal & 0x7 )
            {
            default:
                assert(0);
            case 0:
                // 
                topSwatchLoc = bottomSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
                sideSwatchLoc = SWATCH_INDEX( 5,0 );
                break;
            case 1:
                // sandstone
                topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY );
                sideSwatchLoc = SWATCH_INDEX( 0,12 );
                bottomSwatchLoc = SWATCH_INDEX( 0,13 );
                break;
            case 2:
                // wooden
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrX, gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrY );
                break;
            case 3:
                // cobblestone
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_COBBLESTONE].txrY );
                break;
            case 4:
                // brick
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_BRICK].txrX, gBlockDefinitions[BLOCK_BRICK].txrY );
                break;
            case 5:
                // stone brick
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_STONE_BRICKS].txrX, gBlockDefinitions[BLOCK_STONE_BRICKS].txrY );
                break;
            case 6:
                // nether brick
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_NETHER_BRICKS].txrX, gBlockDefinitions[BLOCK_NETHER_BRICKS].txrY );
                break;
            case 7:
                // quartz with distinctive sides and bottom
                topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrX, gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrY );
                sideSwatchLoc = SWATCH_INDEX( 6,17 );
                bottomSwatchLoc = SWATCH_INDEX( 1,17 );
                break;
            }
            break;

        case BLOCK_WOODEN_SLAB:
            switch ( dataVal & 0x7 )
            {
            default: // normal log
                assert(0);
            case 0:
                // no change, default plank is fine
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
                break;
            case 1: // spruce (dark)
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( 6,12 );
                break;
            case 2: // birch
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( 6,13 );
                break;
            case 3: // jungle
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( 7,12 );
                break;
            case 4: // acacia
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( 0,22 );
                break;
            case 5: // dark oak
                topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( 1,22 );
                break;
            }
            break;

        case BLOCK_RED_SANDSTONE_SLAB:
            // normal, for both dataVal == 0 and == 8 
            topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            sideSwatchLoc = SWATCH_INDEX( 14,13 );
            bottomSwatchLoc = SWATCH_INDEX( 5,8 ); 
            break;
        }

        // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
        // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
        if ( dataVal & 0x8 )
        {
            // upper slab
            miny = 8;
            maxy = 16;
        }
        else
        {
            // lower slab
            miny = 0;
            maxy = 8;
        }
        saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0,16, miny,maxy, 0,16);
        break;

    case BLOCK_STONE_BUTTON:
    case BLOCK_WOODEN_BUTTON:
        // The bottom 3 bits is direction of button. Top bit is whether it's pressed.
        bitAdd = (dataVal & 0x8) ? 1 : 0;
        switch (dataVal & 0x7)
        {
        default:    // make compiler happy
        case 1: // east
            minx = 0;
            maxx = 2 - bitAdd;
            minz = 5;
            maxz = 12;
            break;
        case 2: // west
            minx = 14 + bitAdd;
            maxx = 16;
            minz = 5;
            maxz = 12;
            break;
        case 3: // south
            minx = 5;
            maxx = 12;
            minz = 0;
            maxz = 2 - bitAdd;
            break;
        case 4: // north
            minx = 5;
            maxx = 12;
            minz = 14 + bitAdd;
            maxz = 16;
            break;
        }
        // we *could* save one face of the stone button, the one facing the object, but don't:
        // the thing holding the stone button could be missing, due to export limits.
        saveBoxGeometry( boxIndex, type, 1, 0x0, minx,maxx, 6,10, minz,maxz );
        break;

    case BLOCK_WALL_SIGN:
        switch (dataVal)
        {
        default:    // make compiler happy
        case 2: // north
            angle = 0.0f;
            break;
        case 3: // south
            angle = 180.0f;
            break;
        case 4: // west
            angle = 270.0f;
            break;
        case 5: // east
            angle = 90.0f;
            break;
        }
        // TODO: sign is actually a bit thick here - it should be more like 1.5 and move 0.25 out.
        // To do this we would need to pass in floats, which might be fine.
        gUsingTransform = 1;
        saveBoxGeometry( boxIndex, type, 1, 0x0, 0,16, 0,12, 14,16 );
        gUsingTransform = 0;
        // scale sign down, move slightly away from wall
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        // this moves block up so that bottom of sign is at Y=0
        // also move a bit away from wall if we're not doing 3d printing
        translateMtx(mtx, 0.0f, 0.5f, gPrint3D ? 0.0f : -0.25f/16.0f);
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        scaleMtx(mtx, 1.0f, 8.0f/12.0f, 1.0f);
        // undo translation
        translateMtx(mtx, 0.0f, -0.5f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 4.5f/16.0f, 0.0f);
        transformVertices(8,mtx);
        break;

    case BLOCK_WALL_BANNER:
        switch (dataVal)
        {
        default:    // make compiler happy
        case 2: // north
            angle = 0.0f;
            break;
        case 3: // south
            angle = 180.0f;
            break;
        case 4: // west
            angle = 270.0f;
            break;
        case 5: // east
            angle = 90.0f;
            break;
        }
        // Banner: three pieces: horizontal brace, top half, bottom half
        gUsingTransform = 1;
        totalVertexCount = gModel.vertexCount;

        if ( gPrint3D ) 
        {
            // 3d printing

            // upper banner
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1,15, 0,14, 2,5 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, 0.0f, -4.0f/16.0f);
            transformVertices(8,mtx);

            // lower banner
            swatchLoc++;
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1,15, 3,16, 10,13 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -1.0f, 4.0f/16.0f);
            transformVertices(8,mtx);

            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, 0.0f, -7.0f/16.0f);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, angle+180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        else
        {
            // rendering

            // crossbar
            saveBoxGeometry( boxIndex, BLOCK_PISTON, 1, 0x0, 1,15, 14,16, 0,2);
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, -2.0f/16.0f, 7.0f/16.0f);
            transformVertices(8,mtx);

            // upper banner
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1,15, 0,14, 2,3 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, 0.0f, -4.0f/16.0f);
            transformVertices(8,mtx);

            // lower banner
            swatchLoc++;
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1,15, 3,16, 12,13 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -1.0f, 6.0f/16.0f);
            transformVertices(8,mtx);

            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, 0.0f, -7.0f/16.0f);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, angle+180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        gUsingTransform = 0;

        break;

    case BLOCK_TRAPDOOR:
    case BLOCK_IRON_TRAPDOOR:
        // On second thought, in testing it worked fine.
        //if ( gPrint3D && !(dataVal & 0x4) )
        //{
        //	// if printing, and door is down, check if there's air below.
        //	// if so, don't print it! Too thin.
        //	if ( gBoxData[boxIndex-1].type == BLOCK_AIR)
        //		return 0;
        //}
        gUsingTransform = 1;
        saveBoxGeometry( boxIndex, type, 1, 0x0, 0,16, 0,3, 0,16);
        gUsingTransform = 0;
        // rotate as needed
        if (dataVal & 0x4 )
        {
            switch (dataVal & 0x3)
            {
            default:    // make compiler happy
            case 0: // south
                angle = 180.0f;
                break;
            case 1: // north
                angle = 0.0f;
                break;
            case 2: // east
                angle = 90.0f;
                break;
            case 3: // west
                angle = 270.0f;
                break;
            }
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            translateMtx(mtx,0.0f, 0.5f-1.5f/16.0f, 6.5f/16.0f);
            rotateMtx(mtx, 90.0f, 0.0f, 0.0f);
            translateMtx(mtx, 0.0f, -0.5f+1.5f/16.0f, -6.5f/16.0f);
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            // undo translation
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(8,mtx);
        }
        else if ( dataVal & 0x8 )
        {
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, 13.0f/16.0f, 0.0f);
            transformVertices(8,mtx);
        }
        break;

    case BLOCK_SIGN_POST:
        // sign is two parts:
        // bottom output first, which saves one translation
        topSwatchLoc = bottomSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_LOG].txrX, gBlockDefinitions[BLOCK_LOG].txrY );
        sideSwatchLoc = SWATCH_INDEX( 4,1 );    // log
        gUsingTransform = 1;
        // if printing, seal the top of the post
        saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, gPrint3D ? 0x0 : DIR_TOP_BIT, 0, 7-fatten,9+fatten, 0,14, 7-fatten,9+fatten);
        gUsingTransform = 0;
        // scale sign down, move slightly away from wall
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 0.5f, 0.0f);
        rotateMtx(mtx, 0.0f, dataVal*22.5f, 0.0f);
        scaleMtx(mtx, 1.0f, 16.0f/24.0f, 1.0f);
        // undo translation
        translateMtx(mtx, 0.0f, -0.5f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        // top is 12 high, extends 2 blocks above. Scale down by 16/24 and move up by 14/24
        gUsingTransform = 1;
        saveBoxGeometry( boxIndex, type, 0, 0x0, 0,16, 0,12, 7-fatten,9+fatten );
        gUsingTransform = 0;
        translateMtx(mtx, 0.0f, 14.0f/24.0f, 0.0f);
        transformVertices(8,mtx);

        break;

    case BLOCK_STANDING_BANNER:
        // Banner: five pieces: vertical sticks, horizontal brace, top half, bottom half
        gUsingTransform = 1;
        totalVertexCount = gModel.vertexCount;

        // first the pole, two parts, rotate to position, made from piston head texture
        saveBoxGeometry( boxIndex, BLOCK_PISTON_HEAD, 1, DIR_HI_X_BIT, 0,12, 11,13, 3,5);
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -4.0f/16.0f, 0.0f);
        rotateMtx(mtx, 0.0f, 0.0f, 90.0f);
        translateMtx(mtx, 0.0f, 12.0f/16.0f, 4.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        saveBoxGeometry( boxIndex, BLOCK_PISTON_HEAD, 0, DIR_LO_X_BIT, 0,16, 11,13, 3,5);
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -4.0f/16.0f, 0.0f);
        rotateMtx(mtx, 0.0f, 0.0f, 90.0f);
        translateMtx(mtx, 0.0f, 0.0f/16.0f, 4.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        // crossbar
        saveBoxGeometry( boxIndex, BLOCK_PISTON, 0, 0x0, 1,15, 14,16, 0,2);
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 14.0f/16.0f, 7.0f/16.0f);
        transformVertices(8,mtx);

        // upper banner
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1,15, 0,14, 2,3 );
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 1.0f, -4.0f/16.0f);
        transformVertices(8,mtx);

        // lower banner
        swatchLoc++;
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1,15, 3,16, 12,13 );
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 0.0f, 6.0f/16.0f);
        transformVertices(8,mtx);

        totalVertexCount = gModel.vertexCount - totalVertexCount;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, dataVal*22.5f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);

        gUsingTransform = 0;

        break;

    case BLOCK_WOODEN_DOOR:
    case BLOCK_IRON_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_ACACIA_DOOR:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // at top of door, so get bottom swatch loc, as we use this for the top and bottom faces
        if ( type == BLOCK_WOODEN_DOOR || type == BLOCK_IRON_DOOR )
        {
            // top happens to be +16 further on
            bottomSwatchLoc = swatchLoc + 16;
        }
        else
        {
            // bottom happens to be -1 back
            bottomSwatchLoc = swatchLoc-1;
        }

        // is this the top or bottom of the door?
        if ( dataVal & 0x8 )
        {
            // top of door
            // get bottom dataVal - if bottom of door is cut off, this will be 0 and door will be wrong
            // (who cares, it's half a door)
            topDataVal = dataVal;
            bottomDataVal = gBoxData[boxIndex-1].data;
        }
        else
        {
            swatchLoc = bottomSwatchLoc;
            topDataVal = gBoxData[boxIndex+1].data;
            bottomDataVal = dataVal;
        }

        // door facing direction
        switch (bottomDataVal & 0x3)
        {
        default:    // make compiler happy
        case 0: // west
            angle = 90.0f;
            break;
        case 1: // north
            angle = 180.0f;
            break;
        case 2: // east
            angle = 270.0f;
            break;
        case 3: // south
            angle = 0.0f;
            break;
        }
#define HINGE_ANGLE 90.f
        // hinge move
        // is hinge on right or left?
        if ( topDataVal & 0x1 )
        {
            // reverse hinge - hinge is on the left
            angle += (topDataVal & 0x1) ? 180.0f : 0.0f;
            hingeAngle = (bottomDataVal & 0x4) ? HINGE_ANGLE : 0.0f;
        }
        else
        {
            hingeAngle = (bottomDataVal & 0x4) ? 360.0f - HINGE_ANGLE : 0.0f;
        }

        // one of the only uses of rotUVs other than beds - rotate the UV coordinates by 2, i.e. 180 degrees, for the LO Z face
        // TODO: note that Minecraft does not generate its doors like this. The difference is in the top (and bottom) of the door.
        // Their doors are oriented and so use different pieces of the texture for the tops and bottoms, depending on which direction
        // the door faces (and maybe open/closed). Minecraft appears to grab a strip from the left edge of the bottom tile, or something.
        gUsingTransform = 1;
        saveBoxMultitileGeometry( boxIndex, type, bottomSwatchLoc, swatchLoc, bottomSwatchLoc, 1, 0x0, FLIP_Z_FACE_VERTICALLY, 0,16, 0,16, 13-fatten, 16);
        gUsingTransform = 0;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        // is hinge on left or right?
        if ( topDataVal & 0x1 )
        {
            // hinge is on left, so give it a different translation
            static float offx = 0.0f;
            translateMtx(mtx, 0.0f + offx/16.0f, 0.0f, ((float)(13-fatten)/16.0f) * ((bottomDataVal & 0x4) ? 1.0f : -1.0f) );
            signMult = -1.0f;
        }
        else
        {
            signMult = 1.0f;
        }
        if ( hingeAngle > 0.0f )
        {
            // turn door on hinge location: translate hinge to origin, rotate, translate back
            float halfDepth = (float)(13-fatten)/2.0f;
            translateMtx(mtx, halfDepth/16.0f*signMult, 0.0f, -halfDepth/16.0f );
            rotateMtx(mtx, 0.0f, hingeAngle, 0.0f);
            translateMtx(mtx, -halfDepth/16.0f*signMult, 0.0f, halfDepth/16.0f );
        }
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        // undo translation
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);
        break;

    case BLOCK_SNOW:
        // if printing and the location below the snow is empty, then don't make geometric snow (it'll be too thin).
        // This should only happen if the snow is at the lowest level, which means that the object below would
        // not exist. In this case, we check "type" and not "origType", as origType may well exist due to reading it in.
        if ( gPrint3D &&
            ( ( gBoxData[boxIndex-1].origType == BLOCK_AIR ) || (gBoxData[boxIndex-1].type == BLOCK_AIR) ) )
        {
            gMinorBlockCount--;
            return 0;
        }
        // change height as needed
        saveBoxGeometry( boxIndex, type, 1, 0x0, 0,16, 0, 2 * (1 + (dataVal&0x7)), 0,16);
        break;

    case BLOCK_END_PORTAL_FRAME:
        topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        sideSwatchLoc = SWATCH_INDEX( 15,9 );
        bottomSwatchLoc = SWATCH_INDEX( 15,10 );
        saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0,16, 0,13, 0,16);
        break;

        // top, sides, and bottom, and don't stretch the sides if output here
    case BLOCK_CAKE:
        swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = SWATCH_INDEX( 12,7 );
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = dataVal ? SWATCH_INDEX( 11,7 ) : SWATCH_INDEX( 10,7 );
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX( 10,7 );
        saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, 0x0, 0, 0, 1+(dataVal&0x7)*2,15, 0,8, 1,15);
        break;

    case BLOCK_FARMLAND:
        if ( gPrint3D )
        {
            // if we're print, and there is something above this farmland, don't shift the farmland down (it would just make a gap)
            if ( gBoxData[boxIndex+1].origType != BLOCK_AIR )
            {
                gMinorBlockCount--;
                return 0;
            }
        }
        // TODO: change color by wetness
        topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        sideSwatchLoc = SWATCH_INDEX( 2,0 );
        bottomSwatchLoc = SWATCH_INDEX( 2,0 );
        saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0,16, 0,15, 0,16);
        break;

    case BLOCK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
        // Check if open
        if ( dataVal & 0x4 )
        {
            // open
            if ( dataVal & 0x1 )
            {
                // open west/east
                saveBoxGeometry( boxIndex, type, 1, 0x0, 7,9, 5,16,  0, 2+fatten);
                saveBoxGeometry( boxIndex, type, 0, 0x0, 7,9, 5,16, 14-fatten,16);
                if ( dataVal & 0x2 )
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16,  6, 9,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16, 12,15,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16,  6, 9, 14-fatten,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16, 12,15, 14-fatten,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14,16, 9,12,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14,16, 9,12, 14-fatten,16 );
                }
                else
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7,  6, 9,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7, 12,15,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7,  6, 9, 14-fatten,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7, 12,15, 14-fatten,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 0,2, 9,12,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 0,2, 9,12, 14-fatten,16 );
                }
            }
            else
            {
                // open north/south - hinge posts:
                saveBoxGeometry( boxIndex, type, 1, 0x0,  0, 2+fatten, 5,16, 7,9);
                saveBoxGeometry( boxIndex, type, 0, 0x0, 14-fatten,16, 5,16, 7,9);
                if ( dataVal & 0x2 )	// north
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 0, 2+fatten,  6, 9,  0,7 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 0, 2+fatten, 12,15,  0,7 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 14-fatten,16,  6, 9, 0,7 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 14-fatten,16, 12,15, 0,7 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT),  0, 2+fatten, 9,12, 0,2 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14-fatten,16, 9,12, 0,2 );
                }
                else
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT),  0, 2+fatten,  6, 9, 9,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT),  0, 2+fatten, 12,15, 9,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT), 14-fatten,16,  6, 9, 9,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT), 14-fatten,16, 12,15, 9,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT),  0, 2+fatten, 9,12, 14,16 );
                    saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14-fatten,16, 9,12, 14,16 );
                }
            }
        }
        else
        {
            // closed
            if ( dataVal & 0x1 )
            {
                // open west/east
                saveBoxGeometry( boxIndex, type, 1, 0x0, 7-fatten,9+fatten, 5,16,  0, 2);
                saveBoxGeometry( boxIndex, type, 0, 0x0, 7-fatten,9+fatten, 5,16, 14,16);
                // side pieces
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT|DIR_HI_Z_BIT), 7-fatten,9+fatten,  6, 9, 2,14 );
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT|DIR_HI_Z_BIT), 7-fatten,9+fatten, 12,15, 2,14 );
                // gate center
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 7-fatten,9+fatten, 9,12, 6,10 );
            }
            else
            {
                // open north/south
                saveBoxGeometry( boxIndex, type, 1, 0x0,  0, 2, 5,16, 7-fatten,9+fatten);
                saveBoxGeometry( boxIndex, type, 0, 0x0, 14,16, 5,16, 7-fatten,9+fatten);
                // side pieces
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT|DIR_HI_X_BIT), 2,14,  6, 9,  7-fatten,9+fatten );
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT|DIR_HI_X_BIT), 2,14, 12,15,  7-fatten,9+fatten );
                // gate center
                saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 6,10, 9,12,  7-fatten,9+fatten);
            }
        }
        break;

    case BLOCK_COCOA_PLANT:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        shiftVal = 0;
        gUsingTransform = 1;
        switch ( dataVal >> 2 )
        {
        case 0:
            // small
            swatchLoc += 2;
            // note all six sides are used, but with different texture coordinates
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0,  11,15,  7,12,  11,15);
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_Z_BIT,  1,5,  7,12,  1,5);
            // top and bottom
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0,4,  0,4,  0,4);
            shiftVal = 5;
            break;
        case 1:
            // medium
            swatchLoc++;
            // note all six sides are used, but with different texture coordinates
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0,  9,15,  5,12,  9,15);
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_Z_BIT,  1,7,  5,12,  1,7);
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0,6,  0,6, 0,6);
            shiftVal = 4;
            break;
        case 2:
        default:
            // large
            // already right swatch
            // note all six sides are used, but with different texture coordinates
            // sides:
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0, 7,15, 3,12, 7,15);
            // it should really be 3,12, but Minecraft has a bug where their sides are wrong and are 3,10
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_Z_BIT,  1,9,  3,10,  1,9);
            // top and bottom:
            saveBoxReuseGeometry( boxIndex, type, swatchLoc, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0,7, 0,7, 0,7);
            shiftVal = 3;
            break;
        }
        // -X (west) is the "base" position for the cocoa plant pod
        identityMtx(mtx);
        // push fruit against tree if printing
        translateMtx(mtx, (float)gPrint3D/16.0f, 0.0f, (float)-shiftVal/16.0f);
        transformVertices(8,mtx);

        bitAdd = 8;

        // add stem if not printing and images in use
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0,  DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 12,16, 12,16, 8,8);
            // transform the stem and the block, below, so add in these vertices
            bitAdd = 16;
        }

        // rotate whole thing
        switch ( dataVal & 0x3 )
        {
        default:
        case 0:
            angle = 90;
            break;
        case 1:
            angle = 180;
            break;
        case 2:
            angle = 270;
            break;
        case 3:
            angle = 0;
            break;
        }
        if ( angle != 0 )
        {
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, (float)angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(bitAdd,mtx);
        }
        gUsingTransform = 0;
        break;

    case BLOCK_CAULDRON:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // if printing, we seal the cauldron against the water height (possibly empty), else for rendering we make the walls go to the bottom
        waterHeight = gPrint3D ? (6+dataVal*3) : 6;
        // outsides and bottom
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 1, DIR_TOP_BIT, 0, 0, 16, 0, 16, 0, 16 );
        // top as 4 small faces, and corresponding inside faces
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            14, 16, waterHeight, 16, 2, 14 );	// top and lo_x
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            0, 2, waterHeight, 16, 2, 14 );	// top and hi_x
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT, 0, 
            2, 14, waterHeight, 16, 14, 16 );	// top and lo_z
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0, 
            2, 14, waterHeight, 16, 0, 2 );	// top and hi_z
        // four tiny corners, just tops
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            0, 2, 16, 16, 0, 2 );	// top
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            0, 2, 16, 16, 14, 16 );	// top
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            14, 16, 16, 16, 0, 2 );	// top
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+16, swatchLoc+17, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
            14, 16, 16, 16, 14, 16 );	// top
        // inside bottom
        // outside bottom
        if ( !gPrint3D || (dataVal == 0x0) )
        {
            // show smaller inside bottom if cauldron is empty
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc+1, swatchLoc+16, swatchLoc+1, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 
                2, 14, 3, 6, 2, 14 );

            if ( !gPrint3D ) {
                // outside bottom, above the cutout legs - only when not printing, as otherwise it's invisible and not needed
                saveBoxMultitileGeometry( boxIndex, type, swatchLoc+1, swatchLoc+16, swatchLoc+1, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0, 
                    0, 16, 3, 6, 0, 16 );
            }
        }

        if ( dataVal > 0 && dataVal < 4 )
        {
            // water level
            saveBoxGeometry( boxIndex, BLOCK_STATIONARY_WATER, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 
                2, 14, 6+dataVal*3, 6+dataVal*3, 2, 14 );
        }

        break;

    case BLOCK_DRAGON_EGG:
        // top to bottom
        saveBoxGeometry( boxIndex, type, 1, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 6,10, 15,16, 6,10);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 5,11, 14,15, 5,11);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 4,12, 13,14, 4,12);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 3,13, 11,13, 3,13);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 2,14,  8,11, 2,14);
        saveBoxGeometry( boxIndex, type, 0, 0x0, 1,15, 3,8, 1,15);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 2,14, 1,3, 2,14);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 5,11, 0,1, 5,11);
        break;

    case BLOCK_ANVIL:
        // top to bottom
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        if ( dataVal < 4)
        {
            // undamaged
            topSwatchLoc = swatchLoc+16;
        }
        else if ( dataVal < 8 )
        {
            // damaged 1
            topSwatchLoc = swatchLoc+1;
        }
        else
        {
            // damaged 2
            topSwatchLoc = swatchLoc+17;
        }
        gUsingTransform = 1;
        totalVertexCount = gModel.vertexCount;
        saveBoxMultitileGeometry( boxIndex, type, topSwatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 
            3,13, 10,16, 0,16 );
        saveBoxGeometry( boxIndex, type, 0, 0x0, 3,13, 10,16, 0,16);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 6,10, 5,10, 4,12);
        saveBoxGeometry( boxIndex, type, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 4,12, 4,5, 3,13);
        saveBoxGeometry( boxIndex, type, 0, 0x0, 2,14, 0,4, 2,14);
        totalVertexCount = gModel.vertexCount - totalVertexCount;

        if ( dataVal & 0x3 )
        {
            // rotate
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 90.0f*((dataVal & 0x3)%4), 0.0f);
            // undo translation
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        gUsingTransform = 0;
        break;

    case BLOCK_FLOWER_POT:
        // note that in version 1.7 on flower pots rely on the tile entity to give the data value. Pots before then have the value.
        useInsidesAndBottom = (dataVal != 9);	// cactus
        firstFace = 1;

        if ( gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            // printing or not using images: only geometry we can add is a cactus
            if ( dataVal == 9 )
            {
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_CACTUS].txrX, gBlockDefinitions[BLOCK_CACTUS].txrY );
                saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, firstFace, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 0, 6,10, 6,16, 6,10 );
                firstFace = 0;
            }
        }
        else
        {
            int performMatrixOps = 1;
            int typeB = 0;
            int dataValB = 0;
            int billboardType = BB_FULL_CROSS;
            float scale = 1.0f;
            // rendering
            switch ( dataVal )
            {
            case 0:
                // nothing!
                performMatrixOps = 0;
                break;
            case 1:
                // rose
                typeB = BLOCK_POPPY;
                break;
            case 2:
                // dandelion
                typeB = BLOCK_DANDELION;
                break;
            case 3:
                // sapling (oak)
                typeB = BLOCK_SAPLING;
                scale = 0.75f;
                break;
            case 4:
                // spruce sapling flower - todo ACACIA uses this, maybe uses tile entity?
                typeB = BLOCK_SAPLING;
                dataValB = 1;
                scale = 0.75f;
                break;
            case 5:
                // birch sapling - todo DARK OAK uses this, maybe uses tile entity?
                typeB = BLOCK_SAPLING;
                dataValB = 2;
                scale = 0.75f;
                break;
            case 6:
                // jungle sapling
                typeB = BLOCK_SAPLING;
                dataValB = 3;
                scale = 0.75f;
                break;
            case 7:
                // red mushroom
                typeB = BLOCK_RED_MUSHROOM;
                break;
            case 8:
                // brown mushroom
                typeB = BLOCK_BROWN_MUSHROOM;
                break;
            case 9:
                // cactus (note we're definitely not 3D printing, so no face test)
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_CACTUS].txrX, gBlockDefinitions[BLOCK_CACTUS].txrY );
                saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, firstFace, DIR_BOTTOM_BIT, 0, 6,10, 6,16, 6,10 );
                firstFace = 0;
                performMatrixOps = 0;
                break;
            case 10:
                // dead bush
                typeB = BLOCK_TALL_GRASS;
                scale = 0.75f;
                break;
            case 11:
                // fern
                typeB = BLOCK_TALL_GRASS;
                dataValB = 2;
                scale = 0.75f;
                break;
            default:
                assert(0);
                performMatrixOps = 0;
                break;
            }

            if ( performMatrixOps )
            {
                totalVertexCount = gModel.vertexCount;
                gUsingTransform = 1;
                saveBillboardFacesExtraData( boxIndex, typeB, billboardType, dataValB, firstFace );
                gUsingTransform = 0;
                totalVertexCount = gModel.vertexCount - totalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                // put bottom of billboard at Y==0
                translateMtx(mtx, 0.0f, 0.5f, 0.0f );
                scaleMtx(mtx, scale, scale, scale );
                // move to bottom of pot
                translateMtx(mtx, 0.0f, -0.25f, 0.0f );
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(totalVertexCount,mtx);

                firstFace = 0;
            }
        }

        // the four outside walls
        saveBoxGeometry( boxIndex, type, firstFace, DIR_TOP_BIT|DIR_BOTTOM_BIT, 5,11, 0,6, 5,11 );

        // 4 top edge faces and insides, if visible
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_LO_X_BIT), 
            10,11,  4,6,   6,10 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_HI_X_BIT),
            5,6,    4,6,   6,10 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_LO_Z_BIT),
            6,10,   4,6,  10,11 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|(useInsidesAndBottom?0x0:DIR_HI_Z_BIT),
            6,10,   4,6,   5,6 );
        // top corners
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            5,6,  4,6,  5,6 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            5,6,  4,6,  10,11 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            10,11,  4,6,  5,6 );
        saveBoxGeometry( boxIndex, type, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            10,11,  4,6, 10,11 );

        // inside bottom
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_DIRT].txrX, gBlockDefinitions[BLOCK_DIRT].txrY );
        if ( useInsidesAndBottom )
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 6,10,  0,4,  6,10);
        // outside bottom - in theory never seen, so we make it dirt, since the flowerpot texture itself has a hole in it at these coordinates
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0,  5,11,  0,4,  5,11);

        break;

    case BLOCK_BED:
        // side of bed - head or foot?
        gUsingTransform = 1;
        if ( dataVal & 0x8 )
        {
            // head of bed.
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX( 7,9 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX( 7,9 );
            swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX( 7,8 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX( 5,9 );  // should normally get removed by neighbor tester code
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX( 8,9 );
            // Note: for rendering we might print an open-ended bed - could test neighbor to see if other half of bed is there.
            // For 3D printing we can't risk it, so cap the middle of the bed.
            saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_BOTTOM_BIT|(gPrint3D?0x0:DIR_LO_X_BIT), FLIP_Z_FACE_VERTICALLY, 0,
                0,16, 0,9, 0,16 );
        }
        else
        {
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX( 6,9 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX( 6,9 );
            swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX( 6,8 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX( 5,9 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX( 8,9 );  // should normally get removed by neighbor tester code
            saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_BOTTOM_BIT|(gPrint3D?0x0:DIR_HI_X_BIT), FLIP_Z_FACE_VERTICALLY, 0,
                0,16, 0,9, 0,16 );
        }
        gUsingTransform = 0;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 90.0f*(((dataVal & 0x3)+1)%4), 0.0f);
        // undo translation
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        // add bottom at bottom, just in case bed is open to world
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrX, gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrY );
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0, 0,16,
            (gPrint3D? 0:3),(gPrint3D? 0:3), 0,16);
        break;

    case BLOCK_CACTUS:
        // are top and bottom needed?
        groupByBlock = (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK);

        faceMask = 0x0;
        if ( (gBoxData[boxIndex+1].origType == BLOCK_CACTUS) && !groupByBlock )
            faceMask |= DIR_TOP_BIT;
        if ( (gBoxData[boxIndex-1].origType == BLOCK_CACTUS) && !groupByBlock )
            faceMask |= DIR_BOTTOM_BIT;
        // remember that this gives the top of the block:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // for textured rendering, make a billboard-like object, for printing or solid rendering, pull in the edges
        if ( gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, 1, faceMask, 0, 1,15, 0,16, 1,15 );
        }
        else
        {
            // could use billboard, but this gives two-sided faces, and Minecraft actually uses one-sided faces for thorns
            //saveBillboardFaces( boxIndex, type, BB_GRID );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_TOP_BIT|DIR_BOTTOM_BIT, 0, 0,16, 0,16, 1,15 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|DIR_BOTTOM_BIT, 0, 1,15, 0,16, 0,16 );
            if ( faceMask != (DIR_TOP_BIT|DIR_BOTTOM_BIT))
            {
                // top and bottom
                saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc+1, swatchLoc+2, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|faceMask, 0, 1,15, 0,16, 1,15 );
            }
        }
        break;

    case BLOCK_ENDER_CHEST:
        // fake TODO!
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = swatchLoc+14;	// front
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = swatchLoc+13;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = swatchLoc+13;
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc+13;
        // TODO! For tile itself, Y data was centered instead of put at bottom, so note we have to shift it down
        gUsingTransform = 1;
        saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, 0x0, 0, 0, 1, 15, 1, 15, 1, 15 );
        gUsingTransform = 0;
        switch ( dataVal )
        {
        default:
        case 2: // facing north
            angle = 90;
            break;
        case 3: // facing south
            angle = 270;
            break;
        case 4: // facing west
            // no change
            angle = 0;
            break;
        case 5: // facing east
            angle = 180;
            break;
        }
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -1.0f/16.0f, 0.0f );
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);
        break;

    case BLOCK_REDSTONE_REPEATER_OFF:
    case BLOCK_REDSTONE_REPEATER_ON:
        swatchLoc = SWATCH_INDEX( 3, 8 + (type == BLOCK_REDSTONE_REPEATER_ON) );
        angle = 90.0f*(float)(dataVal&0x3);
        gUsingTransform = 1;
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0,16, 14,16, 0,16 );
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -14.0f/16.0f, 0.0f );
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            // TODO: we don't actually chop off the bottom of the torch - at least 3 (really, 5) pixels should be chopped from bottom). Normally doesn't
            // matter, because no one ever sees the bottom of a repeater.

            // the slideable one, based on dataVal
            totalVertexCount = gModel.vertexCount;
            saveBillboardFacesExtraData( boxIndex, (type==BLOCK_REDSTONE_REPEATER_OFF)?BLOCK_REDSTONE_TORCH_OFF:BLOCK_REDSTONE_TORCH_ON, BB_TORCH, 0x5, 0 );
            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -3.0f/16.0f, -1.0f/16.0f + (float)(dataVal>>2)*2.0f/16.0f );
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);

            // the one shifted 5 sideways
            totalVertexCount = gModel.vertexCount;
            saveBillboardFacesExtraData( boxIndex, (type==BLOCK_REDSTONE_REPEATER_OFF)?BLOCK_REDSTONE_TORCH_OFF:BLOCK_REDSTONE_TORCH_ON, BB_TORCH, 0x5, 0 );
            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -3.0f/16.0f, -5.0f/16.0f );
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        gUsingTransform = 0;
        break;

    case BLOCK_REDSTONE_COMPARATOR_INACTIVE:
    case BLOCK_REDSTONE_COMPARATOR_ACTIVE:
        // in 1.5, comparator active is used for top bit
        // in 1.6, comparator active is not used, it depends on dataVal
        {
            int in_powered = ((type == BLOCK_REDSTONE_COMPARATOR_ACTIVE) || (dataVal >= 8));
            int out_powered = (dataVal & 0x4) == 0x4;
            swatchLoc = SWATCH_INDEX( 14 + in_powered,14 );
            angle = 90.0f*(float)(dataVal&0x3);
            gUsingTransform = 1;
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0,16, 14,16, 0,16 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -14.0f/16.0f, 0.0f );
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(8,mtx);
            if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
            {
                // TODO: we don't actually chop off the bottom of the torch - at least 3 (really, 5) pixels should be chopped from bottom). Normally doesn't
                // matter, because no one ever sees the bottom of a repeater.

                // the left one shifted 5 sideways
                totalVertexCount = gModel.vertexCount;
                saveBillboardFacesExtraData( boxIndex, in_powered?BLOCK_REDSTONE_TORCH_ON:BLOCK_REDSTONE_TORCH_OFF, BB_TORCH, 0x5, 0 );
                totalVertexCount = gModel.vertexCount - totalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                translateMtx(mtx, -3.0f/16.0f, -3.0f/16.0f, 4.0f/16.0f );
                rotateMtx(mtx, 0.0f, angle, 0.0f);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(totalVertexCount,mtx);

                // the right one shifted 5 sideways
                totalVertexCount = gModel.vertexCount;
                saveBillboardFacesExtraData( boxIndex, in_powered?BLOCK_REDSTONE_TORCH_ON:BLOCK_REDSTONE_TORCH_OFF, BB_TORCH, 0x5, 0 );
                totalVertexCount = gModel.vertexCount - totalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                translateMtx(mtx, 3.0f/16.0f, -3.0f/16.0f, 4.0f/16.0f );
                rotateMtx(mtx, 0.0f, angle, 0.0f);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(totalVertexCount,mtx);

                // the inner one moved 3 more down if not powered
                totalVertexCount = gModel.vertexCount;
                saveBillboardFacesExtraData( boxIndex, out_powered?BLOCK_REDSTONE_TORCH_ON:BLOCK_REDSTONE_TORCH_OFF, BB_TORCH, 0x5, 0 );
                totalVertexCount = gModel.vertexCount - totalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                translateMtx(mtx, 0.0f, out_powered ? -3.0f/16.0f : -6.0f/16.0f, -5.0f/16.0f );
                rotateMtx(mtx, 0.0f, angle, 0.0f);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(totalVertexCount,mtx);
            }
            gUsingTransform = 0;
        }
        break;

    case BLOCK_BEACON:
        saveBoxGeometry( boxIndex, BLOCK_GLASS, 1, 0x0, 0,16, 0,16, 0,16);
        if (!gPrint3D)
        {
            // chewy interior
            saveBoxGeometry( boxIndex, BLOCK_BEACON, 0, DIR_BOTTOM_BIT, 3,13, 3,13, 3,13);
            saveBoxGeometry( boxIndex, BLOCK_OBSIDIAN, 0, 0x0, 2,14, 0,3, 2,14);
        }
        break;

    case BLOCK_SLIME:
        saveBoxGeometry( boxIndex, BLOCK_SLIME, 1, 0x0, 0,16, 0,16, 0,16);
        if (!gPrint3D)
        {
            // tasty slime center
            saveBoxGeometry( boxIndex, BLOCK_SLIME, 0, 0x0, 3,13, 3,13, 3,13);
        }
        break;

    case BLOCK_BREWING_STAND:
        // brewing stand exports as an ugly block for 3D printing - too delicate to print. Check that we're not printing
        assert( !gPrint3D );
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // post
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0,  7,  9,  0, 14,  7,  9 );
        // go through the three bottle locations
        for ( i = 0; i < 3; i++ )
        {
            // is the bottle filled or not?
            filled = ((0x1<<i) & dataVal) ? 1 : 0;

            angle = i * 120.0f + filled * 180.0f;

            totalVertexCount = gModel.vertexCount;
            // vertical billboards
            // The bottom three bits are bit flags for which bottle slots actually contain bottles. The actual bottle contents (and the reagent at the top) are stored in a TileEntity for this block, not in the data field.
            // 0x1: The slot pointing east
            // 0x2: The slot pointing southwest
            // 0x4: The slot pointing northwest
            // Set angle and whether there is a bottle.
            // We don't look at (or have!) TileEntity data at this point. TODO
            gUsingTransform = 1;
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY,  9-filled*9, 16-filled*9,  0, 16,  8,  8 );
            gUsingTransform = 0;
            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            //translateMtx(mtx, 0.0f, out_powered ? -3.0f/16.0f : -6.0f/16.0f, -5.0f/16.0f );
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }

        // base
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX-1, gBlockDefinitions[type].txrY );
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  2,  8,   0,  2,   1,  7 );
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  2,  8,   0,  2,   9, 15 );
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  9, 15,   0,  2,   5, 11 );
        break;

    case BLOCK_LEVER:
        // make the lever on the ground, facing east, then move it into position
        uberTotalVertexCount = gModel.vertexCount;
        totalVertexCount = gModel.vertexCount;
        littleTotalVertexCount = gModel.vertexCount;
        // tip - move over by 1
        gUsingTransform = 1;
        saveBoxGeometry( boxIndex, BLOCK_LEVER, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT,  7,9,  10,10,  6,8);
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 0.0f, 1.0f/16.0f );
        transformVertices(littleTotalVertexCount,mtx);

        // add lever - always mask top face, which is output above. Use bottom face if 3D printing, to make object watertight.
        // That said, levers are not currently exported when 3D printing, but just in case we ever do...
        saveBoxGeometry( boxIndex, BLOCK_LEVER, 0, (gPrint3D ? 0x0 : DIR_BOTTOM_BIT)|DIR_TOP_BIT,  7,9,  0,10,  7,9);
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 9.5f/16.0f, 0.0f );
        // tips of levers almost touch
        rotateMtx(mtx, 0.0f, 0.0f, 38.8f);
        translateMtx(mtx, 0.0f, -8.0f/16.0f, 0.0f );
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);

        saveBoxGeometry( boxIndex, BLOCK_COBBLESTONE, 0, 0x0,  4,12,  0,3,  5,11);

        uberTotalVertexCount = gModel.vertexCount - uberTotalVertexCount;
        // transform lever as a whole
        yrot = (dataVal & 0x8 ) ? 180.0f : 0.0f;
        xrot = zrot = 0.0f;
        switch ( dataVal & 0x7 )
        {
        case 1:	// facing east
            yrot += 180.0f;
            zrot = 90.0f;
            break;
        case 2:	// facing west
            yrot += 0.0f;
            zrot = -90.0f;
            break;
        case 3:	// facing south
            yrot += 270.0f;
            xrot = -90.0f;
            break;
        case 4:	// facing north
            yrot += 90.0f;
            xrot = 90.0f;
            break;
        case 5:	// ground south off
            yrot += 90.0f;
            break;
        case 6:	// ground east off
            // no change
            break;
        case 7:	// ceiling south off
            yrot += 90.0f;
            zrot = 180.0f;
            break;
        case 0:	// ceiling east off
            yrot += 180.0f;
            zrot = 180.0f;
            break;
        }
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, yrot, 0.0f);
        rotateMtx(mtx, xrot, 0.0f, zrot);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(uberTotalVertexCount,mtx);
        gUsingTransform = 0;
        break;

    case BLOCK_DAYLIGHT_SENSOR:
    case BLOCK_INVERTED_DAYLIGHT_SENSOR:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLoc;	// 6,15 or 13,22
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] =  SWATCH_INDEX( 5,15 );
        // TODO! For tile itself, Y data was centered instead of put at bottom, so note we have to shift it down
        saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, 0x0, 0, 0, 0,16, 0,6, 0,16 );
        break;

    case BLOCK_STICKY_PISTON:
    case BLOCK_PISTON:
        // TODO: should really add fattening someday, but the textures don't work properly for that
        // is the piston head extended?
        if ( !(dataVal & 0x8) )
        {
            // If the piston head is not extended, this is a full block and we can return and
            // use the more efficient full-block output
            gMinorBlockCount--;
            return 0;
        }

        // We know at this point that the piston is extended.
        // 10,6 sticky head, 11,6 head, 12,6 side, 13,6 bottom, 14,6 extended top
        // we form the head pointing up, so the up direction needs no transform
        bottomDataVal = dataVal & 0x7;
        // compute two rotations, and piston nubbin visibility.
        // Piston nubbin visibility:
        // the top face of the piston is output only in the rare case that the neighboring voxel is empty;
        // normally the extended piston head is in the next voxel. If we see *anything* else, assert, as
        // something's probably wrong with the code.
        yrot = zrot = 0.0f;
        dir = DIRECTION_BLOCK_TOP;
        switch ( bottomDataVal )
        {
        case 0: // pointing down
            dir = DIRECTION_BLOCK_BOTTOM;
            zrot = 180.0f;
            break;
        case 1: // pointing up
            dir = DIRECTION_BLOCK_TOP;
            break;
        case 2: // pointing north
            dir = DIRECTION_BLOCK_SIDE_LO_Z;
            zrot = 90.0f;
            yrot = 270.0f;
            break;
        case 3: // pointing south
            dir = DIRECTION_BLOCK_SIDE_HI_Z;
            zrot = 90.0f;
            yrot = 90.0f;
            break;
        case 4: // pointing west
            dir = DIRECTION_BLOCK_SIDE_LO_X;
            zrot = 90.0f;
            yrot = 180.0f;
            break;
        case 5: // pointing east
            dir = DIRECTION_BLOCK_SIDE_HI_X;
            zrot = 90.0f;
            break;
        default:
            assert(0);
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[dir]].origType;
        assert((neighborType == BLOCK_PISTON_HEAD) || (neighborType == BLOCK_AIR));

        totalVertexCount = gModel.vertexCount;
        littleTotalVertexCount = gModel.vertexCount;

        // we definitely do move the piston shaft into place, always
        gUsingTransform = 1;
        // side of piston body:
        swatchLoc = SWATCH_INDEX( 12, 6 );
        // form the piston itself sideways, just the small connecting bit, then we rotate upwards
        saveBoxTileGeometry( boxIndex, type, swatchLoc, 1, ((neighborType == BLOCK_PISTON_HEAD)?DIR_HI_X_BIT:0x0)|(gPrint3D ? 0x0 : DIR_LO_X_BIT),  0,4,   12,16,  0,4 );
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -90.0f);
        translateMtx(mtx, 6.0f/16.0f, 12.0f/16.0f, 6.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(littleTotalVertexCount,mtx);

        // piston body
        gUsingTransform = (bottomDataVal != 1);
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc+2, swatchLoc, swatchLoc+1, 0, 0x0,         0, 0,16,   0,12,  0,16 );
        if ( (zrot != 0.0) || (yrot != 0.0) )
        {
            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0, 0.0f, zrot);
            rotateMtx(mtx, 0.0f, yrot, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        gUsingTransform = 0;
        break;

    case BLOCK_PISTON_HEAD:
        // 10,6 sticky head, 11,6 head, 12,6 side, 13,6 bottom, 14,6 extended top
        // we form the head pointing up, so the up direction needs no transform
        bottomDataVal = dataVal & 0x7;
        // compute two rotations, and piston nubbin visibility.
        // Piston nubbin visibility:
        // the bottom face of the piston is output only in the rare case that the neighboring piston voxel is empty;
        // normally the piston body is in the next voxel. If we see *anything* else, assert, as
        // something's probably wrong with the code.
        yrot = zrot = 0.0f;
        dir = DIRECTION_BLOCK_TOP;
        switch ( bottomDataVal )
        {
        case 0: // pointing down
            dir = DIRECTION_BLOCK_TOP;
            zrot = 180.0f;
            break;
        case 1: // pointing up
            dir = DIRECTION_BLOCK_BOTTOM;
            break;
        case 2: // pointing north
            dir = DIRECTION_BLOCK_SIDE_HI_Z;
            zrot = 90.0f;
            yrot = 270.0f;
            break;
        case 3: // pointing south
            dir = DIRECTION_BLOCK_SIDE_LO_Z;
            zrot = 90.0f;
            yrot = 90.0f;
            break;
        case 4: // pointing west
            dir = DIRECTION_BLOCK_SIDE_HI_X;
            zrot = 90.0f;
            yrot = 180.0f;
            break;
        case 5: // pointing east
            dir = DIRECTION_BLOCK_SIDE_LO_X;
            zrot = 90.0f;
            break;
        default:
            assert(0);
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[dir]].origType;
        assert((neighborType == BLOCK_PISTON) || (neighborType == BLOCK_STICKY_PISTON) || (neighborType == BLOCK_AIR));

        totalVertexCount = gModel.vertexCount;
        littleTotalVertexCount = gModel.vertexCount;

        // we definitely do move the piston shaft into place, always
        gUsingTransform = 1;
        // side of piston body:
        swatchLoc = SWATCH_INDEX( 12, 6 );
        // form the piston itself sideways, just the small bit, then we rotate upwards
        saveBoxTileGeometry( boxIndex, type, swatchLoc, 1, (((neighborType == BLOCK_PISTON) || (neighborType == BLOCK_STICKY_PISTON))?DIR_LO_X_BIT:0x0)||(gPrint3D ? 0x0 : DIR_HI_X_BIT),  4,16,   12,16,  0,4 );
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -90.0f);
        translateMtx(mtx, 6.0f/16.0f, -4.0f/16.0f, 6.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(littleTotalVertexCount,mtx);

        // piston body
        saveBoxMultitileGeometry( boxIndex, type, (dataVal & 0x8)?(swatchLoc-2):(swatchLoc-1), swatchLoc, swatchLoc-1, 0, 0x0,         0, 0,16,   12,16,  0,16 );
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0, 90.0f, zrot);
        rotateMtx(mtx, 0.0f, yrot, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);
        gUsingTransform = 0;
        break;

    case BLOCK_HOPPER:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // outsides and bottom
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc-1, swatchLoc-1, swatchLoc-1, 1, DIR_TOP_BIT, 0, 0,16,  10,16,  0,16 );
        // next level down outsides and bottom
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc-1, swatchLoc-1, swatchLoc-1, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 0, 4,12,   4,10,  4,12 );
        // bottom level cube - move to position based on dataVal
        totalVertexCount = gModel.vertexCount;
        gUsingTransform = (dataVal > 1);
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc-1, swatchLoc-1, swatchLoc-1, 0, 0x0,         0, 6,10,   0, 4,  6,10 );
        gUsingTransform = 0;
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        if ( dataVal > 1 )
        {
            float xShift = 0.0f;
            float zShift = 0.0f;
            identityMtx(mtx);
            // move 4x4x4 box up and over
            switch ( dataVal )
            {
            case 2:
                zShift = -6.0/16.0f;
                break;
            case 3:
                zShift = 6.0/16.0f;
                break;
            case 4:
                xShift = -6.0/16.0f;
                break;
            case 5:
                xShift = 6.0/16.0f;
                break;
            }
            translateMtx(mtx, xShift, 4.0f/16.0f, zShift);
            transformVertices(totalVertexCount,mtx);
        }

        if ( fatten )
        {
            // top as a single flat face - really won't print well otherwise, as surface is infinitely thin at level Y=10.
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 
                0, 16,   16, 16,   0, 16 );
        }
        else
        {
            // top as 4 small faces, and corresponding inside faces
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16,  10+gPrint3D*2, 16,   2, 14 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2,  10+gPrint3D*2, 16,   2, 14 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT, 0, 
                2, 14,  10+gPrint3D*2, 16,  14, 16 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0, 
                2, 14,  10+gPrint3D*2, 16,   0,  2 );

            // top corners
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2,  16, 16,  0,  2 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2, 16, 16,  14, 16 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16, 16, 16,  0,  2 );
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16, 16, 16, 14, 16 );

            // inside bottom
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc-2, swatchLoc-2, swatchLoc-2, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 
                2, 14,  10+gPrint3D*2, 10+gPrint3D*2,   2, 14 );
        }
        break;

    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_STAINED_GLASS_PANE:
        // dataVal applies only to stained_glass, for which swatch to use
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        switch (type)
        {
        default:
            assert(0);
        case BLOCK_GLASS_PANE:
            topSwatchLoc = SWATCH_INDEX( 4, 9 );
            break;
        case BLOCK_STAINED_GLASS_PANE:
            // get colored swatch and edge above it.
            swatchLoc += dataVal;
            topSwatchLoc = swatchLoc + 16;
            break;
        case BLOCK_IRON_BARS:
            topSwatchLoc = swatchLoc;	// same
            break;
        }

        tbFaceMask = 0x0;
        filled = 0x0;
        faceMask = 0x0;

        // which neighboring blocks have something that attaches to a glass pane? Things that attach:
        // whole blocks, glass panes, iron bars
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            filled |= 0x1;
            faceMask |= DIR_LO_Z_BIT;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            filled |= 0x2;
            faceMask |= DIR_HI_X_BIT;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            filled |= 0x4;
            faceMask |= DIR_HI_Z_BIT;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            filled |= 0x8;
            faceMask |= DIR_LO_X_BIT;
        }

        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_BOTTOM]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            // neighbor above, turn off edge
            faceMask |= DIR_BOTTOM_BIT;
            tbFaceMask |= DIR_BOTTOM_BIT;
        }

        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_TOP]].origType;
        if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
            (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        {
            // neighbor below, turn off edge
            faceMask |= DIR_TOP_BIT;
            tbFaceMask |= DIR_TOP_BIT;
        }

        // after all that, if we're 3D printing details (and so need a perfect seal) or if we're doing per-block output, then output all faces
        if ( gPrint3D || (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK) )
        {
            faceMask = tbFaceMask = 0x0;
        }

        // make everything an edge, substitute in as needed.
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_TOP] = topSwatchLoc;
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = topSwatchLoc;

        if ( (type == BLOCK_IRON_BARS) && !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) )
        {
            // for rendering iron bars, we just need one side of each wall - easier
            switch (filled)
            {
            case 0:
            case 15:
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 9,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 9,16 );

                // bottom & top of east-west wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // north and south ends
                //saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                //	7, 9, 0,16, 0,16 );

                // east and west ends
                //saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                //	7, 9, 0,0, 0,16 );

                // north-south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );
                // east-west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 1:
                // north wall only, just south edge as border
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,8 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,8 );

                // south end
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7, 9, 0,16, 8,8 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );
                break;
            case 2:
                // east wall only
                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 16,16, 7, 9 );

                // west end
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 3:
                // north and east: build west face of north wall, plus top and bottom
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,8 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,8 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 4:
                // south wall only
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 8,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 8,16 );

                // south end
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7, 9, 0,16, 8,8 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );
                break;
            case 5:
                // north and south - easy!
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // north-south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );
                break;
            case 6:
                // east and south
                // south and east: build west face of north wall, plus top and bottom
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 8,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 8,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 7:
                // north, east, and south - 5 faces horizontally
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );

                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    9,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    9,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 8:
                // west wall only
                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 16,16, 7, 9 );

                // west end
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 9:
                // north and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,8 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,8 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 16,16, 7, 9 );

                // west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 10:
                // east and west - have to mess with top and bottom being rotated
                // bottom & top of wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 11:
                // north, east, and west
                // north top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,7 );
                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // east-west bottom & top
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 12:
                // south and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 8,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 8,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // bottom & top of west wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 16,16, 7, 9 );

                // west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 13:
                // north, south, and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );

                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,7, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,7, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 14:
                // east, south, and west
                // south top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 9,16 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 9,16 );
                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // east-west bottom & top
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            }
        }
        else
        {
            // for printing, we make solids as possible:
            // fatten tops and bottoms by stretching
            // CHECK end caps of N, E, S, W walls - do they have the right orientation?
            switch (filled)
            {
            case 0:
                // top & bottom of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );

                // east and west faces of north wall, shorter at south, plus north end
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,7-fatten );
                // east and west faces of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north and south face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // north and south face of east wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 15:
                // all four; 15 has no outside edges.
                // top & bottom of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );

                // east and west faces of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,7-fatten );
                // east and west faces of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north and south face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // north and south face of east wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 1:
                // north wall only, just south edge as border
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                break;
            case 2:
                // east wall only
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 3:
                // north and east: build west face of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                // east face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 4:
                // south wall only
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                break;
            case 5:
                // north and south - easy!
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                break;
            case 6:
                // east and south
                // build west face of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 7-fatten,16 );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north face of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 7:
                // north, east, and south - 5 faces horizontally
                // west face of north-south wall, and top & bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                // east face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north and south face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                break;
            case 8:
                // west wall only
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 9+fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 9:
                // north and west
                // build east face of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                // west face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // south face of west wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 9+fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 10:
                // east and west - have to mess with top and bottom being rotated
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 11:
                // north, east, and west
                // east and west faces of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of west wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // north face of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of west-east wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 12:
                // south and west
                // build east face of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 7-fatten,16 );
                // west face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north face of west wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,9+fatten, 0,16, 7-fatten, 9+fatten );
                // south face of west wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,7-fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 13:
                // north, south, and west
                // east face of north-south wall, and top & bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                // west face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north and south face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,7-fatten, 0,16, 7-fatten, 9+fatten );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                break;
            case 14:
                // east, south, and west
                // east and west faces of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 1, DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // south face of west wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // south face of east wall
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // north face of west-east wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            }
        }
        break;

    default:
        // something tagged as billboard or geometry, but no case here!
        assert(0);
        gMinorBlockCount--;
        return 0;
    }

    return 1;
}
/* ===========================
 * Code structure for output of blocks:

 Partial blocks:

 saveBoxTileGeometry
 saveBoxGeometry
   saveBoxMultitileGeometry
   saveBoxReuseGeometry
     saveBoxAlltileGeometry
       (face saved if block is transformed, as we don't know where it really is)
       !findFaceDimensions - face saved if face doesn't touch voxel face (side)
       !lesserNeighborCoversRectangle - or, face saved if it touches but isn't properly covered
           (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK)
         lesserBlockCoversWholeFace
         getFaceRect - gets partial neighbor's face rect (for a few blocks) for testing for coverage
       then saveBoxFace - indeed, really do it


 Solid blocks:

 checkAndCreateFaces - called for all solid blocks (water and lava are 'solid' but treated specially)
   checkMakeFace
     neighborMayCoverFace
       lesserBlockCoversWholeFace - check if lesser block fully covers particular face (since it's a block)
    OR (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK)
   if lava or water, save special with heights


 Triangles (sides of sloping rails):

 saveTriangleGeometry
   saveTriangleFace
     !findFaceDimensions - face saved if face doesn't touch voxel face (side)
     !lesserNeighborCoversRectangle - or, face saved if it touches but isn't properly covered
         (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK)
*/
static int saveTriangleGeometry( int type, int dataVal, int boxIndex, int typeBelow, int dataValBelow, int boxIndexBelow, int choppedSide )
{
    int swatchLoc;
    int vindex[4];
    int uvSlopeIndices[4];
    Point2 uvs[3];
    int i, startVertexIndex;
    IPoint anchor;
    Point *vertices;
    int retCode = MW_NO_ERROR;

    vertices = &gModel.vertices[gModel.vertexCount];
    startVertexIndex = gModel.vertexCount;
    boxIndexToLoc( anchor, boxIndex );

    for ( i = 0; i < 8; i++ )
    {
        float *pt;
        Point cornerVertex;
        Vec3Scalar( cornerVertex, =, (i&0x4)?1.0f:0.0f, (i&0x2)?1.0f:0.0f, (i&0x1)?1.0f:0.0f );

        // get vertex and store
        retCode |= checkVertexListSize();
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        pt = (float *)gModel.vertices[gModel.vertexCount];

        pt[X] = (float)anchor[X] + cornerVertex[X];
        pt[Y] = (float)anchor[Y] + cornerVertex[Y];
        pt[Z] = (float)anchor[Z] + cornerVertex[Z];

        gModel.vertexCount++;
        assert( gModel.vertexCount <= gModel.vertexListSize );
    }

    // top UVs are always the same
    vindex[0] = 0x2|0x4;		// xmax, zmin
    vindex[1] = 0x2;			// xmin, zmin 
    vindex[2] = 0x2|0x1;		// xmin, zmax
    vindex[3] = 0x2|0x4|0x1;	// xmax, zmax

    // Make decal tile and put it here for swatchLoc - note we don't need the swatch loc itself, the uvSlopeIndices are all we need
    if ( gExportTexture )
    {
        getSwatch( type, dataVal, DIRECTION_BLOCK_TOP, boxIndexBelow, uvSlopeIndices );
    }

    switch ( choppedSide )
    {
    case DIR_LO_X_BIT:
        retCode |= saveBoxFaceUVs( typeBelow, DIRECTION_LO_X_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        // We could choose to not use the chopped off vertices, or simply lower them and not think too hard. We lower them.
        // So we then set index X == 0, Y == 1, Z == 0/1 should be set to have the Y value set to 0.0f
        vertices[0x0|0x2|0x0][Y] -= 1.0f;	// xmin, ymax, zmin -> xmin, ymin, zmin
        vertices[0x0|0x2|0x1][Y] -= 1.0f;	// xmin, ymax, zmax -> xmin, ymin, zmax

        // bottom and side
        saveBlockGeometry( boxIndex, typeBelow, dataValBelow, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0,16, 0,16, 0,16 );

        vindex[0] = 0x4;		// xmax, ymin, zmin
        vindex[1] = 0x0;		// xmin, ymin, zmin
        vindex[2] = 0x4|0x2;	// xmax, ymax, zmin
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex, swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_LO_Z, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x1;		    // xmin, ymin, zmax
        vindex[1] = 0x1|0x4;		// xmax, ymin, zmax
        vindex[2] = 0x1|0x4|0x2;	// xmax, ymax, zmax
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_HI_Z, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_HI_X_BIT:
        retCode |= saveBoxFaceUVs( typeBelow, DIRECTION_HI_X_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        // We could choose to not use the chopped off vertices, or simply lower them and not think too hard. We lower them.
        // So we then set index X == 1, Y == 1, Z == 0/1 should be set to have the Y value set to 0.0f
        vertices[0x4|0x2|0x0][Y] -= 1.0f;	// xmax, ymax, zmin -> xmax, ymin, zmin
        vertices[0x4|0x2|0x1][Y] -= 1.0f;	// xmax, ymax, zmax -> xmax, ymin, zmax

        // bottom and side
        saveBlockGeometry( boxIndex, typeBelow, dataValBelow, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0,16, 0,16, 0,16 );

        vindex[0] = 0x4;		// xmax, ymin, zmin
        vindex[1] = 0x0;		// xmin, ymin, zmin
        vindex[2] = 0x2;	    // xmin, ymax, zmin
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_LO_Z, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x1;		    // xmin, ymin, zmax
        vindex[1] = 0x1|0x4;		// xmax, ymin, zmax
        vindex[2] = 0x1|0x2;	    // xmin, ymax, zmax
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_HI_Z, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_LO_Z_BIT:	// north side, going from north low to south high (i.e. sloping up to south)
        retCode |= saveBoxFaceUVs( typeBelow, DIRECTION_LO_Z_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        // We could choose to not use the chopped off vertices, or simply lower them and not think too hard. We lower them.
        // So we then set index X == 0/1, Y == 1, Z == 0 should be set to have the Y value set to 0.0f
        vertices[0x0|0x2|0x0][Y] -= 1.0f;	// xmin, ymax, zmin -> xmin, ymin, zmin
        vertices[0x4|0x2|0x0][Y] -= 1.0f;	// xmin, ymax, zmax -> xmin, ymin, zmax

        // bottom and side
        saveBlockGeometry( boxIndex, typeBelow, dataValBelow, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_TOP_BIT, 0,16, 0,16, 0,16 );

        vindex[0] = 0x0;		// zmin, ymin, xmin
        vindex[1] = 0x1;		// zmax, ymin, xmin
        vindex[2] = 0x1|0x2;	// zmax, ymax, xmin
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_LO_X, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x4|0x1;		// zmax, ymin, xmax
        vindex[1] = 0x4;		    // zmin, ymin, xmax
        vindex[2] = 0x4|0x1|0x2;	// zmax, ymax, xmax
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_HI_X, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_HI_Z_BIT:
        retCode |= saveBoxFaceUVs( typeBelow, DIRECTION_HI_Z_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        // We could choose to not use the chopped off vertices, or simply lower them and not think too hard. We lower them.
        // So we then set index X == 0/1, Y == 1, Z == 1 should be set to have the Y value set to 0.0f
        vertices[0x0|0x2|0x1][Y] -= 1.0f;	// xmin, ymax, zmin -> xmin, ymin, zmin
        vertices[0x4|0x2|0x1][Y] -= 1.0f;	// xmin, ymax, zmax -> xmin, ymin, zmax

        // bottom and side
        saveBlockGeometry( boxIndex, typeBelow, dataValBelow, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0,16, 0,16, 0,16 );

        vindex[0] = 0x0;		// zmin, ymin, xmin
        vindex[1] = 0x1;		// zmax, ymin, xmin
        vindex[2] = 0x2;	    // zmin, ymax, xmin
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_LO_X, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x4|0x1;		// zmax, ymin, xmax
        vindex[1] = 0x4;		    // zmin, ymin, xmax
        vindex[2] = 0x4|0x2;	    // zmin, ymax, xmax
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace( boxIndex,  swatchLoc, typeBelow, DIRECTION_BLOCK_SIDE_HI_X, startVertexIndex, vindex, uvs );
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    default:
        assert(0);
    }

    gModel.triangleCount += 2;

    return retCode;
}

//   3---2
//   |   |
//   |   |
//   0---1
static void setDefaultUVs( Point2 uvs[3], int skip )
{
    int index = 0;
    if ( skip != 0 )
    {
        uvs[index][X] = 0.0f;
        uvs[index++][Y] = 0.0f;
    }
    if ( skip != 1 )
    {
        uvs[index][X] = 1.0f;
        uvs[index++][Y] = 0.0f;
    }
    if ( skip != 2 )
    {
        uvs[index][X] = 1.0f;
        uvs[index++][Y] = 1.0f;
    }
    if ( skip != 3 )
    {
        uvs[index][X] = 0.0f;
        uvs[index++][Y] = 1.0f;
    }
}

static FaceRecord * allocFaceRecordFromPool()
{
    if ( gModel.faceRecordPool->count >= FACE_RECORD_POOL_SIZE )
    {
        // allocate new pool
        FaceRecordPool *pFRP = (FaceRecordPool *)malloc(sizeof(FaceRecordPool));
        pFRP->count = 0;
        pFRP->pPrev = gModel.faceRecordPool;
        gModel.faceRecordPool = pFRP;
    }
    return &(gModel.faceRecordPool->fr[gModel.faceRecordPool->count++]);
}

// Output the face of triangle slope element
static int saveTriangleFace( int boxIndex, int swatchLoc, int type, int faceDirection, int startVertexIndex, int vindex[3], Point2 uvs[3] )
{
    FaceRecord *face;
    int j;
    int uvIndices[3];
    int retCode = MW_NO_ERROR;
    int rect[4];

    // If we're doing a 3D print, we must always output the triangle - we don't currently
    // check for an exact neighbor match (which is going to very rare anyway).
    // Or, if we're using a transform, we can't know about face coverage so must output it.
    // Or, if the face is not touching the voxel's face, it must be output (note - for this
    // method, the face *always* touches the voxel's face, since it's a full block, but
    // it's still good to test, as it sets rect and also this method may someday change).
    // Or, if a lesser neighbor doesn't cover this face fully, then output it.
    if ( gPrint3D || gUsingTransform || !findFaceDimensions( rect, faceDirection, 0, 16, 0, 16, 0, 16 ) ||
        !lesserNeighborCoversRectangle(faceDirection, boxIndex, rect) )
    {
        // output the triangle
        if ( gExportTexture )
        {
            // get the three UV texture vertices, stored by swatch type
            uvIndices[0] = saveTextureUV( swatchLoc, type, uvs[0][X], uvs[0][Y] );
            uvIndices[1] = saveTextureUV( swatchLoc, type, uvs[1][X], uvs[1][Y] );
            uvIndices[2] = saveTextureUV( swatchLoc, type, uvs[2][X], uvs[2][Y] );
        }

        face = allocFaceRecordFromPool();
        //face = allocFaceRecordFromPool();
        if ( face == NULL )
        {
            return retCode|MW_WORLD_EXPORT_TOO_LARGE;
        }

        // if we sort, we want to keep faces in the order generated, which is
        // generally cache-coherent (and also just easier to view in the file)
        //face->faceIndex = firstFaceModifier( 0, gModel.faceCount );
        // never a first face, we know how we're using it currently (as sides for rails, and these simply aren't the first)
        face->faceIndex = gModel.faceCount;
        face->type = type;

        // always the same normal, which directly corresponds to the normals[] array in gModel
        face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : faceDirection;

        // get three face indices for the three corners of the triangular face, and always create each
        for ( j = 0; j < 3; j++ )
        {
            face->vertexIndex[j] = startVertexIndex + vindex[j];
            if (gExportTexture)
                face->uvIndex[j] = uvIndices[j];
        }
        // double last point - we normally store four points in face records, so this tips us off that it's a triangle
        face->vertexIndex[3] = face->vertexIndex[2];
        if (gExportTexture)
            face->uvIndex[3] = face->uvIndex[2];

        // all set, so save it away
        retCode |= checkFaceListSize();
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        gModel.faceList[gModel.faceCount++] = face;
    }

    return retCode;
}

// save the proper faces for the given block type and data value
static void saveBlockGeometry( int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    int swatchLocSet[6];
    int i;
    for ( i = 0; i < 6; i++ )
    {
        swatchLocSet[i] = getSwatch(type,dataVal,i,boxIndex,NULL);
    }
    saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, markFirstFace, faceMask, 0, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}

static void saveBoxGeometry( int boxIndex, int type, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    int swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

    saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, markFirstFace, faceMask, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}

// With this one we specify the swatch location explicitly, vs. get it from the type - e.g., cobblestone vs. moss stone for cobblestone walls
static void saveBoxTileGeometry( int boxIndex, int type, int swatchLoc, int markFirstFace, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, markFirstFace, faceMask, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}


static void saveBoxMultitileGeometry( int boxIndex, int type, int topSwatchLoc, int sideSwatchLoc, int bottomSwatchLoc, int markFirstFace, int faceMask, int rotUVs, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    int swatchLocSet[6];
    swatchLocSet[DIRECTION_BLOCK_TOP] = topSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_BOTTOM] = bottomSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = sideSwatchLoc;
    saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, markFirstFace, faceMask, rotUVs, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}

static void saveBoxReuseGeometry( int boxIndex, int type, int swatchLoc, int faceMask, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    int swatchLocSet[6];
    swatchLocSet[DIRECTION_BLOCK_TOP] =
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc;
    saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, faceMask, 0, 1, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}

// rotUVs == FLIP_X_FACE_VERTICALLY means vertically flip X face
// rotUVs == FLIP_Z_FACE_VERTICALLY means vertically flip Z face
// rotUVs == ROTATE_TOP_AND_BOTTOM means rotate top and bottom tile 90 degrees; for glass panes.
static int saveBoxAlltileGeometry( int boxIndex, int type, int swatchLocSet[6], int markFirstFace, int faceMask, int rotUVs, int reuseVerts, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    int i;
    int swatchLoc;
    int faceDirection;
    IPoint anchor;
    int vindex[4];
    float minu, maxu, minv, maxv;
    float fminx, fmaxx, fminy, fmaxy, fminz, fmaxz;
    int startVertexIndex;
    int retCode = MW_NO_ERROR;

    // test if no faces are output (it could happen with a glass pane post)
    if ( faceMask == DIR_ALL_BITS )
    {
        return retCode;
    }

    if ( reuseVerts )
    {
        // reuse the 8 vertices made previously
        startVertexIndex = gModel.vertexCount-8;
    }
    else
    {
        // normal case: make vertices
        startVertexIndex = gModel.vertexCount;

        boxIndexToLoc( anchor, boxIndex );

        // create the eight corner locations: xmin, ymin, zmin; xmin, ymin, zmax; etc.
        fminx = (float)minPixX / 16.0f;
        fmaxx = (float)maxPixX / 16.0f;
        fminy = (float)minPixY / 16.0f;
        fmaxy = (float)maxPixY / 16.0f;
        fminz = (float)minPixZ / 16.0f;
        fmaxz = (float)maxPixZ / 16.0f;
        // If the two values match, and there are real faces on both sides,
        // add an epsilon to avoid z-fighting
        if ( minPixX == maxPixX && !(faceMask & (DIR_LO_X_BIT|DIR_HI_X_BIT)))
        {
            fminx -= STOP_Z_FIGHTING;
            fmaxx += STOP_Z_FIGHTING;
        }
        if ( minPixY == maxPixY && !(faceMask & (DIR_BOTTOM_BIT|DIR_TOP_BIT)))
        {
            fminy -= STOP_Z_FIGHTING;
            fmaxy += STOP_Z_FIGHTING;
        }
        if ( minPixZ == maxPixZ && !(faceMask & (DIR_LO_Z_BIT|DIR_HI_Z_BIT)))
        {
            fminz -= STOP_Z_FIGHTING;
            fmaxz += STOP_Z_FIGHTING;
        }
        for ( i = 0; i < 8; i++ )
        {
            float *pt;
            Point cornerVertex;
            Vec3Scalar( cornerVertex, =, (i&0x4)?fmaxx:fminx, (i&0x2)?fmaxy:fminy, (i&0x1)?fmaxz:fminz );

            // get vertex and store
            retCode |= checkVertexListSize();
            if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

            pt = (float *)gModel.vertices[gModel.vertexCount];

            pt[X] = (float)anchor[X] + cornerVertex[X];
            pt[Y] = (float)anchor[Y] + cornerVertex[Y];
            pt[Z] = (float)anchor[Z] + cornerVertex[Z];

            gModel.vertexCount++;
            assert( gModel.vertexCount <= gModel.vertexListSize );
        }
    }

    // index these eight corners for each face and make texture UVs on the fly, storing the UV locations.
    for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
    {
        // if bit is not set in mask, output face
        if ( !((1<<faceDirection) & faceMask) )
        {
            // see if neighbor covers this face
            int rect[4];
            // if it is going to be transformed (so we can't really know what it's next to), 
            // or doesn't touch the voxel's face, 
            // or it touches the voxel's face and its neighbor doesn't cover the geometry in rect,
            // then output the face. Whew!
            if ( gUsingTransform || !findFaceDimensions( rect, faceDirection, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ ) ||
                !lesserNeighborCoversRectangle(faceDirection, boxIndex, rect) )
            {

                int reverseLoop = 0;
                // rotUVs == 1 means vertically flip X face
                // rotUVs == 2 means vertically flip Z face
                int useRotUVs = 0;

                swatchLoc = swatchLocSet[faceDirection];
                switch (faceDirection)
                {
                default:
                case DIRECTION_BLOCK_SIDE_LO_X:	// CCW
                    vindex[0] = 0x2|0x1;	// ymax, zmax
                    vindex[1] = 0x2;		// ymax, zmin
                    vindex[2] = 0;			// ymin, zmin
                    vindex[3] = 0x1;		// ymin, zmax
                    minu = (float)minPixZ/16.0f;
                    maxu = (float)maxPixZ/16.0f;
                    minv = (float)minPixY/16.0f;
                    maxv = (float)maxPixY/16.0f;
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X:	// CCW
                    vindex[0] = 0x4|0x2;		// ymax, zmin
                    vindex[1] = 0x4|0x2|0x1;	// ymax, zmax
                    vindex[2] = 0x4|0x1;		// ymin, zmax
                    vindex[3] = 0x4;			// ymin, zmin
                    // rotate 90 or 180 - used for glass
                    // when rotUVs are used currently, also reverse the loop
                    if ( rotUVs&FLIP_X_FACE_VERTICALLY )
                    {
                        // to mirror the face, use the same coordinates as the LO_X face
                        minu = (float)minPixZ/16.0f;
                        maxu = (float)maxPixZ/16.0f;
                        minv = (float)minPixY/16.0f;
                        maxv = (float)maxPixY/16.0f;
                        useRotUVs = 2;
                        reverseLoop = 1;
                    }
                    else
                    {
                        // normal case
                        // On the hi X face, the Z direction is negative, so we negate
                        minu = (float)(16.0f-maxPixZ)/16.0f;
                        maxu = (float)(16.0f-minPixZ)/16.0f;
                        minv = (float)minPixY/16.0f;
                        maxv = (float)maxPixY/16.0f;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:	
                    vindex[0] = 0x2;		// xmin, ymax
                    vindex[1] = 0x4|0x2;	// xmax, ymax
                    vindex[2] = 0x4;		// xmax, ymin
                    vindex[3] = 0;			// xmin, ymin 
                    // here for the sides of beds and for doors, and for glass, so one mirrors the other
                    // rotate 180 and flip
                    // when rotUVs are used currently, also reverse the loop
                    if ( rotUVs&FLIP_Z_FACE_VERTICALLY )
                    {
                        // use same coordinates as HI_Z, so that the faces mirror one another
                        minu = (float)minPixX/16.0f;
                        maxu = (float)maxPixX/16.0f;
                        minv = (float)minPixY/16.0f;
                        maxv = (float)maxPixY/16.0f;
                        useRotUVs = 2;
                        reverseLoop = 1;
                    }
                    else
                    {
                        // say we're looking at the left half of a tile;
                        // on the low Z face the X direction is negative, so we have to negate here
                        minu = (float)(16.0f-maxPixX)/16.0f;
                        maxu = (float)(16.0f-minPixX)/16.0f;
                        minv = (float)minPixY/16.0f;
                        maxv = (float)maxPixY/16.0f;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    vindex[0] = 0x1|0x4|0x2;	// xmax, ymax
                    vindex[1] = 0x1|0x2;		// xmin, ymax
                    vindex[2] = 0x1;			// xmin, ymin 
                    vindex[3] = 0x1|0x4;		// xmax, ymin
                    minu = (float)minPixX/16.0f;
                    maxu = (float)maxPixX/16.0f;
                    minv = (float)minPixY/16.0f;
                    maxv = (float)maxPixY/16.0f;
                    break;
                case DIRECTION_BLOCK_BOTTOM:
                    vindex[0] = 0x4|0x1;	// xmax, zmax
                    vindex[1] = 0x1;		// xmin, zmax
                    vindex[2] = 0;			// xmin, zmin 
                    vindex[3] = 0x4;		// xmax, zmin
                    // rotate bottom 90 and rotate bounds to match
                    useRotUVs = (rotUVs&ROTATE_TOP_AND_BOTTOM) ? 1 : 0;
                    if ( useRotUVs )
                    {
                        // rotate 90 as far as bounds go
                        // used for glass pane and nothing else.
                        // NOTE: may need some "(16.0f-" adjustment
                        // similar to below if the object rendered
                        // is NOT symmetric around the center 8,8
                        minv = (float)minPixX/16.0f;
                        maxv = (float)maxPixX/16.0f;
                        minu = (float)minPixZ/16.0f;
                        maxu = (float)maxPixZ/16.0f;
                    }
                    else
                    {
                        // normal case
                        minu = (float)minPixX/16.0f;
                        maxu = (float)maxPixX/16.0f;
                        minv = (float)(16.0f-maxPixZ)/16.0f;
                        maxv = (float)(16.0f-minPixZ)/16.0f;
                    }
                    reverseLoop = 1;
                    break;
                case DIRECTION_BLOCK_TOP:
                    vindex[0] = 0x2|0x4;		// xmax, zmin
                    vindex[1] = 0x2;			// xmin, zmin 
                    vindex[2] = 0x2|0x1;		// xmin, zmax
                    vindex[3] = 0x2|0x4|0x1;	// xmax, zmax
                    // rotate top 90, and rotate bounds
                    useRotUVs = (rotUVs&ROTATE_TOP_AND_BOTTOM) ? 1 : 0;
                    if ( useRotUVs )
                    {
                        // rotate 90 as far as bounds go
                        // used for glass pane and nothing else.
                        // NOTE: may need some "(16.0f-" adjustment
                        // similar to below if the object rendered
                        // is NOT symmetric around the center 8,8
                        minv = (float)minPixX/16.0f;
                        maxv = (float)maxPixX/16.0f;
                        minu = (float)minPixZ/16.0f;
                        maxu = (float)maxPixZ/16.0f;
                    }
                    else
                    {
                        // normal case
                        minu = (float)minPixX/16.0f;
                        maxu = (float)maxPixX/16.0f;
                        minv = (float)(16.0f-maxPixZ)/16.0f;
                        maxv = (float)(16.0f-minPixZ)/16.0f;
                    }
                    break;
                }

                // mark the first face if calling routine wants it, and this is the first face of six
                retCode |= saveBoxFace( swatchLoc, type, faceDirection, markFirstFace, startVertexIndex, vindex, reverseLoop, useRotUVs, minu, maxu, minv, maxv );
                if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                // face output, so don't need to mark first face
                markFirstFace = 0;
            }
        }
    }

    if ( !reuseVerts )
    {
        // note the box's bounds (not the exact bounds, just that the voxel is occupied)
        addBounds( anchor, &gModel.billboardBounds );
        VecScalar( anchor, +=, 1 );
        addBounds( anchor, &gModel.billboardBounds );
    }

    gModel.billboardCount++;

    return retCode;
}

// find if the specified face touches its voxel's face (i.e., is up against the voxel), and get the dimensions found.
// Return 0 if the face is not against the voxel (and so cannot possibly be culled).
static int findFaceDimensions( int rect[4], int faceDirection, int minPixX, int maxPixX, int minPixY, int maxPixY, int minPixZ, int maxPixZ )
{
    switch ( faceDirection )
    {
    default:
    case DIRECTION_BLOCK_SIDE_LO_X:
        if ( minPixX > 0 )
            return 0;
        rect[0] = minPixZ;
        rect[1] = maxPixZ;
        rect[2] = minPixY;
        rect[3] = maxPixY;
        break;
    case DIRECTION_BLOCK_SIDE_HI_X:
        if ( maxPixX < 16 )
            return 0;
        rect[0] = minPixZ;
        rect[1] = maxPixZ;
        rect[2] = minPixY;
        rect[3] = maxPixY;
        break;
    case DIRECTION_BLOCK_SIDE_LO_Z:	
        if ( minPixZ > 0 )
            return 0;
        rect[0] = minPixX;
        rect[1] = maxPixX;
        rect[2] = minPixY;
        rect[3] = maxPixY;
        break;
    case DIRECTION_BLOCK_SIDE_HI_Z:	
        if ( maxPixZ < 16 )
            return 0;
        rect[0] = minPixX;
        rect[1] = maxPixX;
        rect[2] = minPixY;
        rect[3] = maxPixY;
        break;
    case DIRECTION_BLOCK_BOTTOM:
        if ( minPixY > 0 )
            return 0;
        rect[0] = minPixX;
        rect[1] = maxPixX;
        rect[2] = minPixZ;
        rect[3] = maxPixZ;
        break;
    case DIRECTION_BLOCK_TOP:
        if ( maxPixY < 16 )
            return 0;
        rect[0] = minPixX;
        rect[1] = maxPixX;
        rect[2] = minPixZ;
        rect[3] = maxPixZ;
        break;
    }
    // does touch face, so check if it can be deleted
    return 1;
}

// Test if lesser neighbor covers rectangle defining this lesser block's face that touches the voxel.
// It is assumed lesser block is on, as that's the only one that calls this method.
static int lesserNeighborCoversRectangle( int faceDirection, int boxIndex, int rect[4] )
{
    int type, neighborType, neighborBoxIndex;
    int neighborRect[4];

    if ( gOptions->exportFlags & EXPT_GROUP_BY_BLOCK )
    {
        // mode where every block is output regardless of neighbors, so return false
        return 0;
    }

    // check for easy case: if neighbor is a full block, neighbor covers all, so return 1
    // (or, for printing, return 1 if the block being covered *exactly* matches)
    type = gBoxData[boxIndex].type;
    neighborBoxIndex = boxIndex + gFaceOffset[faceDirection];
    neighborType = gBoxData[neighborBoxIndex].type;
    if ( gBlockDefinitions[neighborType].flags & BLF_WHOLE )
    {
        // special cases for viewing (rendering), having to do with semitransparency or cutouts
        if ( gPrint3D )
        {
            // hide only if the incoming rectangle is exactly filling the space;
            // we want to removing only exact matches so that the 3D print slicer works correctly (I hope...)
            if ( rect[0] != 0 || rect[1] != 16 || rect[2] != 0 || rect[3] != 16 )
                return 0;
        }
        else
        {
            // check if the neighbor is semitransparent (glass, water, etc.)
            if (gBlockDefinitions[neighborType].alpha < 1.0f)
            {
                // and this object is a different
                // type - including ice on water, glass next to water, etc. - then output face
                if ( neighborType != type )
                {
                    return 0;
                }
            }
            // look for blocks with cutouts next to them - only for rendering
            if ((gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) &&
                (gBlockDefinitions[neighborType].flags & BLF_CUTOUTS) )
            {
                //(neighborType == BLOCK_LEAVES) ||
                //(neighborType == BLOCK_SUGAR_CANE) ||
                //(neighborType == BLOCK_CROPS) ||
                //(neighborType == BLOCK_NETHER_WART) ||
                //(neighborType == BLOCK_MONSTER_SPAWNER)||
                //(neighborType == BLOCK_COBWEB) ||
                //((neighborType == BLOCK_IRON_BARS) && (type != BLOCK_IRON_BARS)) ||
                //((neighborType == BLOCK_VINES) && (type != BLOCK_VINES) ) )

                if ( !((neighborType == BLOCK_GLASS || neighborType == BLOCK_STAINED_GLASS) && (type == BLOCK_GLASS || type == BLOCK_STAINED_GLASS_PANE)) &&
                    !((neighborType == BLOCK_GLASS_PANE || neighborType == BLOCK_STAINED_GLASS_PANE) && (type == BLOCK_GLASS_PANE || type == BLOCK_STAINED_GLASS_PANE)) &&
                    !((neighborType == BLOCK_VINES) && (type == BLOCK_VINES)) &&
                    !((neighborType == BLOCK_IRON_BARS) && (type == BLOCK_IRON_BARS)) )
                {
                    // neighbor does not cover face, so output the face by returning 0
                    return 0;
                }
            }
        }
        // neighbor covers face, as it's whole, not semitransparent, and not a cutout, so don't output the face by returning 1
        return 1;
    }

    // check if neighboring lesser block fully covers face.
    if ( gPrint3D )
    {
        // We're 3d printing; in this case, delete only if an exact removal is possible.
        // Bounds of lesser block itself must exactly match full face to even do this test
        if ( ( rect[0] == 0 ) &&
            ( rect[1] == 16 ) &&
            ( rect[2] == 0 ) &&
            ( rect[3] == 16 ) )
        {
            // lesser block covers full face - does its neighbor?
            if ( lesserBlockCoversWholeFace( faceDirection, neighborBoxIndex, !gPrint3D ) )
            {
                // exactly match, so can be removed.
                return 1;
            }
            else
            {
                // doesn't exactly match, so can quit
                return 0;
            }
        }
        // else lesser block covers partial, so should continue testing with partial
    }
    else
    {
        // rendering, so test if neighbor fully covers face
        if ( lesserBlockCoversWholeFace( faceDirection, neighborBoxIndex, !gPrint3D ) )
        {
            // fully covered, so done
            return 1;
        }
    }

    // full face check fails; check if neighboring lesser block covers the rectangle passed in
    if ( getFaceRect( (faceDirection+3)%6, neighborBoxIndex, !gPrint3D, neighborRect ) )
    {
        if ( gPrint3D )
        {
            // We're 3d printing; in this case, delete only if an exact removal is possible.
            // Bounds must exactly match.
            if ( ( rect[0] == neighborRect[0] ) &&
                ( rect[1] == neighborRect[1] ) &&
                ( rect[2] == neighborRect[2] ) &&
                ( rect[3] == neighborRect[3] ) )
            {
                // exactly match, so can be removed.
                return 1;
            }
        }
        else
        {
            // 3D rendering - we're happy to delete any hidden faces we can
            // see if our rectangle is inside neighborRect
            if ( ( rect[0] >= neighborRect[0] ) &&
                ( rect[1] <= neighborRect[1] ) &&
                ( rect[2] >= neighborRect[2] ) &&
                ( rect[3] <= neighborRect[3] ) )
            {
                // inside, so is covered.
                return 1;
            }
        }
    }

    return 0;
}

// Find partial rectangle covered by face. You should first call lesserBlockCoversWholeFace
// to check full face coverage (easy out), then this for partial coverage, ONLY. It will not
// test for full coverage.
// faceDirection is relative to the neighbor itself.
// Exact is set to 1 (true) if the face rect being returned is precisely covering the area, not
// covering more than that.
static int getFaceRect( int faceDirection, int boxIndex, int view3D, int faceRect[4] )
{
    // Do this test only for sides, not top or bottom.
    // TODO:
    // Note how non-comprehensive this method is right now; many objects' face bounds are not precisely
    // computed.
    if ( ( faceDirection != DIRECTION_BLOCK_BOTTOM ) &&
        ( faceDirection != DIRECTION_BLOCK_TOP ) )
    {
        // we have partial blocks possible. Check if neighbor's original type exists at all
        int origType = gBoxData[boxIndex].origType;
        // not air?
        if ( origType > BLOCK_AIR )
        {
            int dataVal = gBoxData[boxIndex].data;
            int setBottom = 0;
            // The idea here is that setTop is set
            int setTop = 0;
            // a minor block exists, so check its coverage given the face direction
            switch ( origType )
            {
            case BLOCK_STONE_SLAB:
            case BLOCK_WOODEN_SLAB:

            case BLOCK_OAK_WOOD_STAIRS:
            case BLOCK_COBBLESTONE_STAIRS:
            case BLOCK_BRICK_STAIRS:
            case BLOCK_STONE_BRICK_STAIRS:
            case BLOCK_NETHER_BRICK_STAIRS:
            case BLOCK_SANDSTONE_STAIRS:
            case BLOCK_SPRUCE_WOOD_STAIRS:
            case BLOCK_BIRCH_WOOD_STAIRS:
            case BLOCK_JUNGLE_WOOD_STAIRS:
            case BLOCK_ACACIA_WOOD_STAIRS:
            case BLOCK_DARK_OAK_WOOD_STAIRS:
            case BLOCK_QUARTZ_STAIRS:
            case BLOCK_RED_SANDSTONE_STAIRS:
                // TODO: Right now stairs are dumb: only the large rectangle of the base is returned.
                // Returning the little block, which can further be trimmed to a cube, is a PAIN.
                // This does mean the little stair block sides won't be deleted. Ah well.
                // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
                // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
                
                if ( dataVal & 0x8 )
                {
                    // upper slab
                    setBottom = 8;
                    setTop = 16;
                }
                else
                {
                    // lower slab
                    setBottom = 0;
                    setTop = 8;
                }
                break;

            case BLOCK_BED:
                setTop = 9;
                if ( view3D )
                {
                    // when rendering, we can see under the bed
                    setBottom = 3;
                }
                break;

            case BLOCK_CARPET:
                setTop = 1;
                break;

            case BLOCK_END_PORTAL_FRAME:
                setTop = 13;
                break;

            case BLOCK_FARMLAND:
                setTop = 15;
                break;

            case BLOCK_SNOW:
                setTop = 2 * (1 + (dataVal&0x7));
                break;

            case BLOCK_CAULDRON:
                setTop = 16;
                if ( view3D )
                {
                    // can see between legs when rendering
                    setBottom = 3;
                }
                break;

            case BLOCK_REDSTONE_REPEATER_OFF:
            case BLOCK_REDSTONE_REPEATER_ON:
            case BLOCK_REDSTONE_COMPARATOR_INACTIVE:
            case BLOCK_REDSTONE_COMPARATOR_ACTIVE:
                // annoyingly, repeaters undergo transforms, so repeaters next to each other won't clear each other...
                setTop = 2;
                break;

            case BLOCK_DAYLIGHT_SENSOR:
            case BLOCK_INVERTED_DAYLIGHT_SENSOR:
                setTop = 6;
                break;

            case BLOCK_HOPPER:
                // blocks bottom
                setTop = 16;
                setBottom = 10;
                break;

            case BLOCK_TRAPDOOR:
            case BLOCK_IRON_TRAPDOOR:
                if ( !(dataVal & 0x4) )
                {
                    // trapdoor is flat on ground
                    setTop = 3;
                }
                break;

            default:
                // not in list, so won't cover anything
                break;
            }

            // did we find any side bounds? setTop is 0 if none are set
            if ( setTop > 0 )
            {
                // sides
                faceRect[0] = 0;
                faceRect[1] = 16;
                faceRect[2] = setBottom;
                faceRect[3] = setTop;
                return 1;
            }
        }
    }

    // no side-bounds used, or rectangle found - done
    return 0;
}

// rotUVs rotates the face: 0 - no rotation, 1 - 90 degrees (I forget if it's CCW or CW), 2 - 180, 3 - 270
static int saveBoxFace( int swatchLoc, int type, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int reverseLoop, int rotUVs, float minu, float maxu, float minv, float maxv )
{
    int j;
    int uvIndices[4];
    int orderedUvIndices[4];
    int retCode = MW_NO_ERROR;

    if ( gExportTexture )
    {
        // output each face
        // get the four UV texture vertices, stored by swatch type
        saveRectangleTextureUVs( swatchLoc, type, minu, maxu, minv, maxv, uvIndices );

        // get four face indices for the four corners of the face, and always create each
        for ( j = 0; j < 4; j++ )
        {
            if ( reverseLoop )
            {
                // 13 is really like "1", just higher so that %4 will be positive (and not confusing)
                orderedUvIndices[j] = uvIndices[(13 - j - rotUVs)%4];
            }
            else
            {
                // rotUVs
                orderedUvIndices[j] = uvIndices[(j+2+rotUVs)%4];
            }
        }
    }

    retCode |= saveBoxFaceUVs( type, faceDirection, markFirstFace, startVertexIndex, vindex, orderedUvIndices );
    return retCode;
}

static int saveBoxFaceUVs( int type, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int uvIndices[4] )
{
    FaceRecord *face;
    int j;
    int retCode = MW_NO_ERROR;

    // output each face
    face = allocFaceRecordFromPool();
    if ( face == NULL )
    {
        return retCode|MW_WORLD_EXPORT_TOO_LARGE;
    }

    // if we sort, we want to keep faces in the order generated, which is
    // generally cache-coherent (and also just easier to view in the file)
    face->faceIndex = firstFaceModifier( markFirstFace, gModel.faceCount );
    face->type = type;

    // always the same normal, which directly corresponds to the normals[] array in gModel
    face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : faceDirection;

    // get four face indices for the four corners of the face, and always create each
    for ( j = 0; j < 4; j++ )
    {
        face->vertexIndex[j] = startVertexIndex + vindex[j];
        if ( gExportTexture )
            face->uvIndex[j] = uvIndices[j];
    }

    // all set, so save it away
    retCode |= checkFaceListSize();
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    gModel.faceList[gModel.faceCount++] = face;

    return retCode;
}

// What we need to do here:
// 1) add two faces for given angle
// 2) save the vertex locations once for both faces
// 3) for each face, set the loop, the vertex indices, the normal indices (really, just face direction), and the texture indices
static int saveBillboardFaces( int boxIndex, int type, int billboardType )
{
    return saveBillboardFacesExtraData( boxIndex, type, billboardType, gBoxData[boxIndex].data, 1 );
}

static int saveBillboardFacesExtraData( int boxIndex, int type, int billboardType, int dataVal, int firstFace )
{
    int i, j, fc, swatchLoc;
    FaceRecord *face;
    int faceDir[8];
    Point vertexOffsets[4][4];
    IPoint anchor;
    int faceCount = 0;
    int startVertexCount = 0;   // doesn't really need initialization, but makes compilers happy
    int totalVertexCount;
    int doubleSided = 1;
    float height = 0.0f;
    int uvIndices[4];
    int foundSunflowerTop = 0;
    int swatchLocSet[6];
    int retCode = MW_NO_ERROR;

    assert(!gPrint3D);

    swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

    // some types use data values for which billboard to use
    switch ( type )
    {
    case BLOCK_SAPLING:
        switch ( dataVal & 0x3 )
        {
        default:
        case 0: // OAK sapling
            // set OK already
            break;
        case 1:
            // spruce sapling
            swatchLoc = SWATCH_INDEX(15,3);
            break;
        case 2:
            // birch sapling
            swatchLoc = SWATCH_INDEX(15,4);
            break;
        case 3:
            // jungle sapling
            swatchLoc = SWATCH_INDEX(14,1);
            break;
        case 4:
            // acacia sapling
            swatchLoc = SWATCH_INDEX(14,18);
            break;
        case 5:
            // dark oak sapling
            swatchLoc = SWATCH_INDEX(15,18);
            break;
        }
        break;
    case BLOCK_TALL_GRASS:
        switch ( dataVal )
        {
        case 0:
            // dead bush appearance
            swatchLoc = SWATCH_INDEX(7,3);
            break;
        case 1:
        default:
            // set OK already
            break;
        case 2:
            // fern
            swatchLoc = SWATCH_INDEX(8,3);
            break;
        }
        break;
    case BLOCK_TORCH:
        // redstone torches stick out a bit, so need billboards
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        //case BLOCK_TRIPWIRE:
        // is torch not standing up?
        //if ( dataVal != 5 )
        //{
        //    // it'll get flattened instead
        //    return 0;
        //}
        break;
    case BLOCK_CROPS:
        // adjust for growth
        // undocumented: village-grown wheat appears to have
        // the 0x8 bit set, which doesn't seem to matter. Mask it out.
        swatchLoc += ( (dataVal & 0x7) - 7 );
        break;
    case BLOCK_CARROTS:
        // mask out high bit just in case
        swatchLoc += ( (dataVal & 0x7)/2 - 3 );
        break;
    case BLOCK_POTATOES:
        // mask out high bit just in case
        swatchLoc += ( (dataVal & 0x7)/2 - 3 );
        break;
    case BLOCK_NETHER_WART:
        if ( dataVal == 0 )
        {
            swatchLoc -= 2;
        }
        else if ( dataVal <= 2 )
        {
            swatchLoc--;
        }
        break;
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
        // offset about height of stem - 1 extra down for farmland shift
        height = ((float)(dataVal&0x7)*2.0f-15.0f)/16.0f;
        // TODO: the tricky bit is rotating the stem to a reasonable pumpkin
        //if ( dataVal&0x7 == 0x7 )
        //{
        //    // fully mature, use other stem if next to pumpkin or melon
        //    //swatchLoc += 16;
        //}
        break;

    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:	// TODO - should be powered
    case BLOCK_ACTIVATOR_RAIL:	// TODO - should be powered
        switch ( type )
        {
        case BLOCK_POWERED_RAIL:
            if ( !(dataVal & 0x8) )
            {
                // unpowered rail
                swatchLoc = SWATCH_INDEX( 3, 10 );
            }
            break;
        case BLOCK_DETECTOR_RAIL:
            // by default, the detector rail is in its undetected state
            if ( dataVal & 0x8 )
            {
                // rail detector activated (same tile in basic game)
                swatchLoc = SWATCH_INDEX( 11,17 );
            }
            break;
        case BLOCK_ACTIVATOR_RAIL:
            // by default, unactivated
            if ( dataVal & 0x8 )
            {
                // activated rail
                swatchLoc = SWATCH_INDEX( 9,17 );
            }
            break;
        }
        // if not a normal rail, there are no curve bits, so mask off upper bit, which is
        // whether the rail is powered or not.
        dataVal &= 0x7;
        // fall through:
    case BLOCK_RAIL:
        switch ( dataVal )
        {
        case 2:
        case 3:
        case 4:
        case 5:
            // sloping, so continue
            break;
        default:
            // it's a flat
            return(0);
        }
        break;
    case BLOCK_VINES:
        if ( dataVal == 0 )
            // it's sitting underneath a block, so flat
            return(0);
        break;
    case BLOCK_CACTUS:
        // side faces are one higher
        swatchLoc++;
        break;
    case BLOCK_POPPY:
        if ( dataVal > 0 )
        {
            // row 20 has these flowers; else rose (12,0) is used
            swatchLoc = SWATCH_INDEX( dataVal-1,19 );
        }
        break;
    case BLOCK_DOUBLE_FLOWER:
        if ( dataVal >= 8 )
        {
            // top half of plant, so need data value of block below
            // to know which sort of plant
            // (could be zero if block is missing, in which case it'll be a sunflower, which is fine)
            // row 19 (#18) has these
            swatchLoc = SWATCH_INDEX( gBoxData[boxIndex-1].data*2+3,18 );
            if ( gBoxData[boxIndex-1].data == 0 )
            {
                foundSunflowerTop = 1;
            }
        }
        else
        {
            // bottom half of plant, from row 19
            swatchLoc = SWATCH_INDEX( dataVal*2+2,18 );
        }
        break;
    default:
        // perfectly fine to hit here, the billboard is generic
        break;
    }

    // given billboardType, set up number of faces, normals for each, coordinate offsets for each;
    // the UV indices should always be the same for each, so we don't need to touch those
    switch ( billboardType )
    {
    case BB_FULL_CROSS:
        {
            float texelWidth,texelLow,texelHigh;

            // approximate width of billboard across block, eyeballing it.
            texelWidth = 14.5f/16.0f;
            texelLow = (1.0f - texelWidth)/2.0f;
            texelHigh = (1.0f + texelWidth)/2.0f;
            faceCount = 4;
            // two paired billboards
            faceDir[0] = DIRECTION_LO_X_HI_Z;
            faceDir[1] = DIRECTION_HI_X_LO_Z;
            faceDir[2] = DIRECTION_HI_X_HI_Z;
            faceDir[3] = DIRECTION_LO_X_LO_Z;

            Vec3Scalar( vertexOffsets[0][0], =, texelLow,height,texelLow );
            Vec3Scalar( vertexOffsets[0][1], =, texelHigh,height,texelHigh );
            Vec3Scalar( vertexOffsets[0][2], =, texelHigh,1+height,texelHigh );
            Vec3Scalar( vertexOffsets[0][3], =, texelLow,1+height,texelLow );

            Vec3Scalar( vertexOffsets[1][0], =, texelLow,height,texelHigh );
            Vec3Scalar( vertexOffsets[1][1], =, texelHigh,height,texelLow );
            Vec3Scalar( vertexOffsets[1][2], =, texelHigh,1+height,texelLow );
            Vec3Scalar( vertexOffsets[1][3], =, texelLow,1+height,texelHigh );
        }
        break;
    case BB_GRID:
    case BB_FIRE:
        {
            float texelWidth,texelLow,texelHigh;

            // shift down for crops
            float vertShift = -1.0f/16.0f;
            if ( type == BLOCK_NETHER_WART || type == BLOCK_CACTUS || billboardType == BB_FIRE )
                vertShift = 0.0f;

            // width is space between parallel billboards
            // Fire goes to almost the very edge of the block (by not going to the very edge, this stops z-fighting with neighboring blocks).
            texelWidth = (billboardType == BB_FIRE) ? 0.995f : 0.5f;
            // NOTE: we don't actually use billboards for cactus, but if we did, this is what we'd do.
            // We don't use it because thorns should show up on front faces only.
            if ( type == BLOCK_CACTUS )
                texelWidth = 14.0f/16.0f;
            texelLow = (1.0f - texelWidth)/2.0f;
            texelHigh = (1.0f + texelWidth)/2.0f;
            faceCount = 8;  // really, 4, single-sided, for torches
            // two paired billboards
            faceDir[0] = DIRECTION_BLOCK_SIDE_LO_X;
            faceDir[1] = DIRECTION_BLOCK_SIDE_HI_X;
            faceDir[2] = DIRECTION_BLOCK_SIDE_LO_X;
            faceDir[3] = DIRECTION_BLOCK_SIDE_HI_X;
            faceDir[4] = DIRECTION_BLOCK_SIDE_HI_Z;
            faceDir[5] = DIRECTION_BLOCK_SIDE_LO_Z;
            faceDir[6] = DIRECTION_BLOCK_SIDE_HI_Z;
            faceDir[7] = DIRECTION_BLOCK_SIDE_LO_Z;

            Vec3Scalar( vertexOffsets[0][0], =, texelLow,vertShift,0 );
            Vec3Scalar( vertexOffsets[0][1], =, texelLow,vertShift,1 );
            Vec3Scalar( vertexOffsets[0][2], =, texelLow,1+vertShift,1 );
            Vec3Scalar( vertexOffsets[0][3], =, texelLow,1+vertShift,0 );

            Vec3Scalar( vertexOffsets[1][0], =, texelHigh,vertShift,0 );
            Vec3Scalar( vertexOffsets[1][1], =, texelHigh,vertShift,1 );
            Vec3Scalar( vertexOffsets[1][2], =, texelHigh,1+vertShift,1 );
            Vec3Scalar( vertexOffsets[1][3], =, texelHigh,1+vertShift,0 );

            Vec3Scalar( vertexOffsets[2][0], =, 0,vertShift,texelLow );
            Vec3Scalar( vertexOffsets[2][1], =, 1,vertShift,texelLow );
            Vec3Scalar( vertexOffsets[2][2], =, 1,1+vertShift,texelLow );
            Vec3Scalar( vertexOffsets[2][3], =, 0,1+vertShift,texelLow );

            Vec3Scalar( vertexOffsets[3][0], =, 0,vertShift,texelHigh );
            Vec3Scalar( vertexOffsets[3][1], =, 1,vertShift,texelHigh );
            Vec3Scalar( vertexOffsets[3][2], =, 1,1+vertShift,texelHigh );
            Vec3Scalar( vertexOffsets[3][3], =, 0,1+vertShift,texelHigh );
        }
        break;
    case BB_TORCH:
        {
            float texelWidth,texelLow,texelHigh;

            // width is space between parallel billboards
            // Annoyingly, it all depends on how you interpolate your textures. Torches will show
            // gaps if you use bilinear interpolation (so don't!), but the size of the gap depends on the texture resolution;
            // higher is narrower. So compromise by pulling in the width a bit more. Since torches are not beautiful
            // anyway, I'm fine with this.
            texelWidth = 0.125f;
            // This version gives a bigger fringe, but I don't think it's really needed
            //texelWidth = (float)(gModel.tileSize/8 - 1)/(float)gModel.tileSize;
            // torches are single-sided, all pointing out (note torch has no tip currently)
            doubleSided = 0;
            texelLow = (1.0f - texelWidth)/2.0f;
            texelHigh = (1.0f + texelWidth)/2.0f;
            faceCount = 8;  // really, 4, single-sided, for torches
            // two paired billboards
            faceDir[0] = DIRECTION_BLOCK_SIDE_LO_X;
            faceDir[1] = DIRECTION_BLOCK_SIDE_LO_X; // lame doubling of unused data
            faceDir[2] = DIRECTION_BLOCK_SIDE_HI_X;
            faceDir[3] = DIRECTION_BLOCK_SIDE_HI_X;
            faceDir[4] = DIRECTION_BLOCK_SIDE_LO_Z;
            faceDir[5] = DIRECTION_BLOCK_SIDE_LO_Z;
            faceDir[6] = DIRECTION_BLOCK_SIDE_HI_Z;
            faceDir[7] = DIRECTION_BLOCK_SIDE_HI_Z;

            Vec3Scalar( vertexOffsets[0][0], =, texelLow,0,0 );
            Vec3Scalar( vertexOffsets[0][1], =, texelLow,0,1 );
            Vec3Scalar( vertexOffsets[0][2], =, texelLow,1,1 );
            Vec3Scalar( vertexOffsets[0][3], =, texelLow,1,0 );

            Vec3Scalar( vertexOffsets[1][0], =, texelHigh,0,1 );
            Vec3Scalar( vertexOffsets[1][1], =, texelHigh,0,0 );
            Vec3Scalar( vertexOffsets[1][2], =, texelHigh,1,0 );
            Vec3Scalar( vertexOffsets[1][3], =, texelHigh,1,1 );

            Vec3Scalar( vertexOffsets[2][0], =, 1,0,texelLow );
            Vec3Scalar( vertexOffsets[2][1], =, 0,0,texelLow );
            Vec3Scalar( vertexOffsets[2][2], =, 0,1,texelLow );
            Vec3Scalar( vertexOffsets[2][3], =, 1,1,texelLow );

            Vec3Scalar( vertexOffsets[3][0], =, 0,0,texelHigh );
            Vec3Scalar( vertexOffsets[3][1], =, 1,0,texelHigh );
            Vec3Scalar( vertexOffsets[3][2], =, 1,1,texelHigh );
            Vec3Scalar( vertexOffsets[3][3], =, 0,1,texelHigh );
        }
        break;
    case BB_RAILS:
        {
            float texelWidth,texelLow,texelHigh;

            // approximate width of billboard across block, eyeballing it.
            texelWidth = 1.0f;
            texelLow = (1.0f - texelWidth)/2.0f;
            texelHigh = (1.0f + texelWidth)/2.0f;
            faceCount = 2;

            // note that by the time we get here the upper bit has been masked off (on/off) for powered/detector rails
            switch ( dataVal )
            {
            case 2: // ascend east +x
                // two paired billboards
                faceDir[0] = DIRECTION_LO_X_HI_Y;
                faceDir[1] = DIRECTION_HI_X_LO_Y;

                Vec3Scalar( vertexOffsets[0][0], =, texelHigh,texelHigh,1 );
                Vec3Scalar( vertexOffsets[0][1], =, texelHigh,texelHigh,0 );
                Vec3Scalar( vertexOffsets[0][2], =, texelLow,texelLow,0 );
                Vec3Scalar( vertexOffsets[0][3], =, texelLow,texelLow,1 );
                break;
            case 3: // ascend west -x
                faceDir[0] = DIRECTION_HI_X_HI_Y;
                faceDir[1] = DIRECTION_LO_X_LO_Y;

                Vec3Scalar( vertexOffsets[0][0], =, texelLow,texelHigh,0 );
                Vec3Scalar( vertexOffsets[0][1], =, texelLow,texelHigh,1 );
                Vec3Scalar( vertexOffsets[0][2], =, texelHigh,texelLow,1 );
                Vec3Scalar( vertexOffsets[0][3], =, texelHigh,texelLow,0 );
                break;
            case 4: // ascend north -z
                faceDir[0] = DIRECTION_HI_Z_HI_Y;
                faceDir[1] = DIRECTION_LO_Z_LO_Y;

                Vec3Scalar( vertexOffsets[0][0], =, 0,texelLow,texelHigh );
                Vec3Scalar( vertexOffsets[0][1], =, 1,texelLow,texelHigh );
                Vec3Scalar( vertexOffsets[0][2], =, 1,texelHigh,texelLow );
                Vec3Scalar( vertexOffsets[0][3], =, 0,texelHigh,texelLow );
                break;
            case 5: // ascend south +z
                faceDir[0] = DIRECTION_LO_Z_HI_Y;
                faceDir[1] = DIRECTION_HI_Z_LO_Y;

                Vec3Scalar( vertexOffsets[0][0], =, 0,texelHigh,texelHigh );
                Vec3Scalar( vertexOffsets[0][1], =, 1,texelHigh,texelHigh );
                Vec3Scalar( vertexOffsets[0][2], =, 1,texelLow,texelLow );
                Vec3Scalar( vertexOffsets[0][3], =, 0,texelLow,texelLow );
                break;
            default:
                // it's a flat
                return(0);
            }
        }
        break;

    case BB_SIDE:
        {
            // vines

            // simply billboard all the vines and be done with it.
            int sideCount = 0;
            int inPositive = 0;
            int inZdirection = 0;
            int billCount = 0;
            float texelDist = 0.0f;

            while ( dataVal )
            {
                if ( dataVal & 0x1 )
                {
                    // side has a vine, so put out billboard
                    switch ( sideCount )
                    {
                    case 0:
                        // south face +Z
                        inZdirection = 1;
                        inPositive = 1;
                        texelDist = 15.0f/16.0f;
                        break;
                    case 1:
                        // west face -X
                        inZdirection = 0;
                        inPositive = 0;
                        texelDist = 1.0f/16.0f;
                        break;
                    case 2:
                        // north face -Z
                        inZdirection = 1;
                        inPositive = 0;
                        texelDist = 1.0f/16.0f;
                        break;
                    case 3:
                        // east face +X
                        inZdirection = 0;
                        inPositive = 1;
                        texelDist = 15.0f/16.0f;
                        break;
                    default:
                        assert(0);
                    }
                    if ( inPositive )
                    {
                        faceDir[faceCount] = inZdirection ? DIRECTION_BLOCK_SIDE_LO_Z : DIRECTION_BLOCK_SIDE_LO_X;
                        faceDir[faceCount+1] = inZdirection ? DIRECTION_BLOCK_SIDE_HI_Z : DIRECTION_BLOCK_SIDE_HI_X;
                    }
                    else
                    {
                        faceDir[faceCount] = inZdirection ? DIRECTION_BLOCK_SIDE_HI_Z : DIRECTION_BLOCK_SIDE_HI_X;
                        faceDir[faceCount+1] = inZdirection ? DIRECTION_BLOCK_SIDE_LO_Z : DIRECTION_BLOCK_SIDE_LO_X;
                    }

                    faceCount += 2;

                    switch ( sideCount )
                    {
                    case 0:
                        // south face +Z
                        Vec3Scalar( vertexOffsets[billCount][0], =, 1,0,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][1], =, 0,0,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][2], =, 0,1,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][3], =, 1,1,texelDist );
                        break;
                    case 1:
                        // west face -X
                        Vec3Scalar( vertexOffsets[billCount][0], =, texelDist,0,1 );
                        Vec3Scalar( vertexOffsets[billCount][1], =, texelDist,0,0 );
                        Vec3Scalar( vertexOffsets[billCount][2], =, texelDist,1,0 );
                        Vec3Scalar( vertexOffsets[billCount][3], =, texelDist,1,1 );
                        break;
                    case 2:
                        // north face -Z
                        Vec3Scalar( vertexOffsets[billCount][0], =, 0,0,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][1], =, 1,0,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][2], =, 1,1,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][3], =, 0,1,texelDist );
                        break;
                    case 3:
                        // east face +X
                        Vec3Scalar( vertexOffsets[billCount][0], =, texelDist,0,0 );
                        Vec3Scalar( vertexOffsets[billCount][1], =, texelDist,0,1 );
                        Vec3Scalar( vertexOffsets[billCount][2], =, texelDist,1,1 );
                        Vec3Scalar( vertexOffsets[billCount][3], =, texelDist,1,0 );
                        break;
                    default:
                        assert(0);
                    }
                    billCount++;
                }

                sideCount++;
                dataVal = dataVal>>1;
            }
        }
        break;
    default:
        assert(0);
        return 0;
    }


    // box value
    boxIndexToLoc( anchor, boxIndex );

    // get the four UV texture vertices, based on type of block
    saveTextureCorners( swatchLoc, type, uvIndices );

    bool normalUnknown = ( ( billboardType == BB_TORCH ) || ( billboardType == BB_FIRE ) || foundSunflowerTop );

    totalVertexCount = gModel.vertexCount;
    for ( i = 0; i < faceCount; i++ )
    {
        // torches are 4 sides facing out: don't output 8 sides
        if ( doubleSided || (i % 2 == 0))
        {
            face = allocFaceRecordFromPool();
            if ( face == NULL )
            {
                return retCode|MW_WORLD_EXPORT_TOO_LARGE;
            }

            // if  we sort, we want to keep faces in the order generated, which is
            // generally cache-coherent (and also just easier to view in the file)
            face->faceIndex = firstFaceModifier( (i == 0)&&firstFace, gModel.faceCount );
            face->type = type;

            // if no transform happens, then use faceDir index for normal index
            face->normalIndex = normalUnknown ? COMPUTE_NORMAL : faceDir[i];

            // two faces have the same vertices and uv's, just the normal is reversed.
            fc = i/2;

            // first or second half of billboard (we assume polygons are one-sided)
            if ( i % 2 == 0 )
            {
                // first half of billboard
                startVertexCount = gModel.vertexCount;

                // get four face indices for the four corners of the billboard, and always create each
                for ( j = 0; j < 4; j++ )
                {
                    // get vertex location and store location
                    float *pt;
                    retCode |= checkVertexListSize();
                    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                    pt = (float *)gModel.vertices[gModel.vertexCount];

                    pt[X] = (float)(anchor[X] + vertexOffsets[fc][j][X]);
                    pt[Y] = (float)(anchor[Y] + vertexOffsets[fc][j][Y]);
                    pt[Z] = (float)(anchor[Z] + vertexOffsets[fc][j][Z]);

                    face->vertexIndex[j] = startVertexCount + j;
                    if (gExportTexture)
                        face->uvIndex[j] = uvIndices[j];

                    gModel.vertexCount++;
                    assert( gModel.vertexCount <= gModel.vertexListSize );
                }
            }
            else
            {
                // second half of billboard
                // vertices already created by code above, for previous billboard
                // reverse both loops

                // use startVertexCount for the locations of the four vertices;
                // these are always the same
                for ( j = 0; j < 4; j++ )
                {
                    face->vertexIndex[3-j] = startVertexCount + j;
                    if (gExportTexture)
                        face->uvIndex[3-j] = uvIndices[j];
                }
            }

            // all set, so save it away
            retCode |= checkFaceListSize();
            if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

            gModel.faceList[gModel.faceCount++] = face;
        }
    }

    // if torch, transform at end if needed
    if ( billboardType == BB_TORCH )
    {
        float mtx[4][4];
        int torchVertexCount = gModel.vertexCount;
        // add a tip to the torch, shift it one texel in X
        gUsingTransform = 1;
        saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 7,9, 10,10, 6,8);
        gUsingTransform = 0;
        torchVertexCount = gModel.vertexCount - torchVertexCount;
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 0.0f, 1.0f/16.0f);
        transformVertices(torchVertexCount,mtx);

        if ( dataVal != 5 )
        {
            static float shr = -0.8f/2.0f;
            static float trans = 8.0f/16.0f;
            float yAngle;
            switch (dataVal)
            {
            default:
            case 1:
                yAngle = 90.0f;
                break;
            case 2:
                yAngle = 270.0f;
                break;
            case 3:
                yAngle = 180.0f;
                break;
            case 4:
                yAngle = 0.0f;
                break;
            }

            totalVertexCount = gModel.vertexCount - totalVertexCount;
            // time to rotate to place. Minecraft shears the torch, we're just going to rotate it.
            // scale sign down, move slightly away from wall
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            // this moves block up so that bottom of torch is at Y=0
            // also move to wall
            translateMtx(mtx, 0.0f, 0.5f, 0.0f );
            //rotateMtx(mtx, 20.0f, 0.0f, 0.0f);
            shearMtx(mtx, 0.0f, shr);
            translateMtx(mtx, 0.0f, 0.0f, trans );
            rotateMtx(mtx, 0.0f, yAngle, 0.0f);
            // undo translation, and kick it up the wall a bit
            translateMtx(mtx, 0.0f, -0.5f + 3.8f/16.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
    }
    else if ( billboardType == BB_FIRE )
    {
        int face;
        for ( face = 0; face < 4; face++ )
        {
            // add sheared "cross flames" inside, 8 in all
            float mtx[4][4];
            int fireVertexCount = gModel.vertexCount;
            gUsingTransform = 1;
            saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, 
                DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT,
                FLIP_Z_FACE_VERTICALLY, 0,16, 0,16, 16,16);
            gUsingTransform = 0;
            fireVertexCount = gModel.vertexCount - fireVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            shearMtx(mtx, 0.0f, 3.2f/8.0f );
            translateMtx(mtx, 0.0f, 0.0f, -8.0f/16.0f);
            switch ( face )
            {
            default:
                assert(0);
            case 0:
                break;
            case 1:
                rotateMtx(mtx, 0.0f, 90.0f, 0.0f);
                break;
            case 2:
                rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
                break;
            case 3:
                rotateMtx(mtx, 0.0f, 270.0f, 0.0f);
                break;
            }
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(fireVertexCount,mtx);
        }
    }
    // check for sunflower
    else if ( foundSunflowerTop )
    {
        // add sunflower head
        float mtx[4][4];
        totalVertexCount = gModel.vertexCount;

        // front of sunflower
        swatchLocSet[DIRECTION_BLOCK_TOP] =
            swatchLocSet[DIRECTION_BLOCK_BOTTOM] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] =  SWATCH_INDEX( gBlockDefinitions[type].txrX+1, gBlockDefinitions[type].txrY );
        // back of sunflower is before front
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X]--; 

        gUsingTransform = 1;
        retCode |= saveBoxAlltileGeometry( boxIndex, type, swatchLocSet, 0, 
            DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT,
            FLIP_X_FACE_VERTICALLY, 0,  8,8, 0,16, 0,16);
        if ( retCode > MW_BEGIN_ERRORS ) return retCode;

        gUsingTransform = 0;
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -20.0f);
        rotateMtx(mtx, 0.0f, -10.0f, 0.0f);
        translateMtx(mtx, 1.8f/16.0f, 0.0f, 0.2f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);
    }
    // if you add anything here, make sure normalUnknown also gets set appropriately.

    //identityMtx(mtx);
    //translateToOriginMtx(mtx, boxIndex);
    //// this moves block up so that bottom of sign is at Y=0
    //// also move a bit away from wall
    //translateMtx(mtx, 0.0f, -0.5f, 0.0f );
    //rotateMtx(mtx, 30.0f, yAngle, 0.0f);
    //// undo translation, and kick it up the wall a bit
    //translateMtx(mtx, 0.0f, 0.5f+2.0f/16.0f, 0.0f);
    //translateFromOriginMtx(mtx, boxIndex);
    //transformVertices(totalVertexCount,mtx);

    gModel.billboardCount++;

    addBounds( anchor, &gModel.billboardBounds );
    VecScalar( anchor, +=, 1 );
    addBounds( anchor, &gModel.billboardBounds );

    // special case:
    // for vines, return 0 (flatten to face) if there is a block above it
    if ( (billboardType == BB_SIDE) && (gBlockDefinitions[gBoxData[boxIndex+1].type].flags & BLF_WHOLE) )
    {
        return 0;
    }

    return 1;
}

static int checkGroupListSize()
{
    // there's some play with the group count, e.g. group 0 is not used, so
    // resize should happen a bit earlier. 3 for safety.
    assert(gGroupCount <= gGroupListSize);
    if (gGroupCount == gGroupListSize)
    {
        BoxGroup *groups;
        gGroupListSize = (int)(gGroupListSize * 1.4 + 1);
        groups = (BoxGroup*)malloc(gGroupListSize*sizeof(BoxGroup));
        if ( groups == NULL )
        {
            return MW_WORLD_EXPORT_TOO_LARGE;
        }
        memcpy( groups, gGroupList, gGroupCount*sizeof(BoxGroup));
        free( gGroupList );
        gGroupList = groups;
    }
    return MW_NO_ERROR;
}
static int checkVertexListSize()
{
    assert(gModel.vertexCount <= gModel.vertexListSize);
    if (gModel.vertexCount == gModel.vertexListSize)
    {
        Point *vertices;
        gModel.vertexListSize = (int)(gModel.vertexListSize * 1.4 + 1);
        vertices = (Point*)malloc(gModel.vertexListSize*sizeof(Point));
        if ( vertices == NULL )
        {
            return MW_WORLD_EXPORT_TOO_LARGE;
        }
        memcpy( vertices, gModel.vertices, gModel.vertexCount*sizeof(Point));
        free( gModel.vertices );
        gModel.vertices = vertices;
    }
    return MW_NO_ERROR;
}
static int checkFaceListSize()
{
    assert(gModel.faceCount <= gModel.faceSize);
    if (gModel.faceCount == gModel.faceSize)
    {
        FaceRecord **faceList;
        gModel.faceSize = (int)(gModel.faceSize * 1.4 + 1);
        faceList = (FaceRecord**)malloc(gModel.faceSize*sizeof(FaceRecord*));
        if ( faceList == NULL )
        {
            return MW_WORLD_EXPORT_TOO_LARGE;
        }
        memcpy( faceList, gModel.faceList, gModel.faceCount*sizeof(FaceRecord*));
        free( gModel.faceList );
        gModel.faceList = faceList;
    }
    return MW_NO_ERROR;
}

static int findGroups()
{
    int boxIndex;
    IPoint loc;
    IPoint seedLoc;
    int seedSize = 1000;
    IPoint *seedStack = (IPoint *)malloc(seedSize*sizeof(IPoint));
    int seedCount = 0;
    BoxGroup *pGroup;
    int retCode = MW_NO_ERROR;

    gGroupList[SURROUND_AIR_GROUP].population = 0;

    // check if we're sealing off tunnels. If so, all we need to do is add the sides and bottom to the air group,
    // the first real group. The "top of the box" air is then used as the seed area, and the rest of the first
    // "outside" air group is finished filling in, as normal. In this way the sides and bottom are "sealed", the
    // air touching the sides and bottom will be put in their own groups (if they don't attach to the outside air
    // group via some tunnel) and will get filled in later as bubbles. You with me?
    // TODO: note in documentation that sealing the side tunnels actually fills them in only if filling air bubbles is done.
    // This process below simply makes sure that the air groups are separate from the outside air group, not that they're filled.
    if ( gOptions->exportFlags & EXPT_SEAL_SIDE_TUNNELS )
    {
        // Note that we actually seal the originally-specified box, so that we don't seal the shrink-wrapped area around the box.
        // -Y face (i.e. bottom)
        addVolumeToGroup( SURROUND_AIR_GROUP, 0, 0, 0, gBoxSize[X]-1, 0, gBoxSize[Z]-1 );
        // -Z face - vary X & Y; note we don't do bottom, since it's already done by -Y
        addVolumeToGroup( SURROUND_AIR_GROUP, 0, 1, 0, gBoxSize[X]-1, gBoxSize[Y]-1, 0 );
        // +Z face
        addVolumeToGroup( SURROUND_AIR_GROUP, 0, 1, gBoxSize[Z]-1, gBoxSize[X]-1, gBoxSize[Y]-1, gBoxSize[Z]-1 );
        // -X face - vary Y & Z; note we don't do columns at corners, since it's already done by -Z and +Z
        addVolumeToGroup( SURROUND_AIR_GROUP, 0, 1, 1,  0, gBoxSize[Y]-1, gBoxSize[Z]-2 );
        // +X face
        addVolumeToGroup( SURROUND_AIR_GROUP, gBoxSize[X]-1, 1, 1, gBoxSize[X]-1, gBoxSize[Y]-1, gBoxSize[Z]-2 );
    }

    // We test *all* cells, including surrounding air, for connectivity to others. The idea here is to have the surrounding air group
    // be created first, and find all connecting air. Note that the tunnel sealing, above, will stop this percolation of air along the sides and
    // bottom, as those cells will already have a group.
    for ( loc[X] = gAirBox.min[X]; loc[X] <= gAirBox.max[X]; loc[X]++ )
    {
        for ( loc[Z] = gAirBox.min[Z]; loc[Z] <= gAirBox.max[Z]; loc[Z]++ )
        {
            // IMPORTANT: Note that we start at the top and work down, as we want to ensure that outside air is the top group.
            boxIndex = BOX_INDEX(loc[X],gAirBox.max[Y],loc[Z]);
            for ( loc[Y] = gAirBox.max[Y]; loc[Y] >= gAirBox.min[Y]; loc[Y]--, boxIndex-- )
            {
                // check if the object has no group
                if ( gBoxData[boxIndex].group == NO_GROUP_SET )
                {
                    gGroupCount++;
                    retCode |= checkGroupListSize();
                    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                    // no group, so make a new seed.
                    // Note that group index 0 is not used at all.
                    pGroup = &gGroupList[gGroupCount];
                    pGroup->groupID = gGroupCount;
                    pGroup->population = (gGroupCount == SURROUND_AIR_GROUP) ? pGroup->population+1 : 1;   // the solid air group might already exist with a population
                    // the solid air group will need to have its bounds fixed at the end if tunnel sealing is going on
                    Vec2Op( pGroup->bounds.min, =, loc );
                    Vec2Op( pGroup->bounds.max, =, loc );
                    pGroup->solid = (gBoxData[boxIndex].type > BLOCK_AIR);

                    gBoxData[boxIndex].group = gGroupCount;
                    if ( pGroup->solid )
                        gSolidGroups++;
                    else
                        gAirGroups++;
                    // propagate seed: make neighbors with same type (solid or air) and no group to be this group.
                    propagateSeed(loc,pGroup,&seedStack,&seedSize,&seedCount);

                    // When this is done, seedStack has a list of seeds that had no ID before and now have one.
                    // Each of these seeds' neighbors needs to be tested .

                    // while seedCount > 0, propagate
                    while ( seedCount > 0 )
                    {
                        // copy test point over, so we don't trample it when seedStack gets increased
                        seedCount--;
                        Vec2Op( seedLoc, =, seedStack[seedCount]);
                        propagateSeed(seedLoc,pGroup,&seedStack,&seedSize,&seedCount);
                    }
                }
            }
        }
    }

    if ( gOptions->exportFlags & EXPT_SEAL_SIDE_TUNNELS )
    {
        // fix solid group, make sure its bounds are the world
        addBoundsToBounds( gAirBox, &gGroupList[SURROUND_AIR_GROUP].bounds );
    }

    free(seedStack);

    return retCode;
}


// Add a volume of space to a group. Group is assumed to exist, have solidity assigned, blocks are assumed to not be in a group already.
static void addVolumeToGroup( int groupID, int minx, int miny, int minz, int maxx, int maxy, int maxz )
{
    int boxIndex;
    IPoint loc;
    BoxGroup *pGroup = &gGroupList[groupID];
    // actually meaningless for our particular use of this method right now,
    // as (a) the bounds are set when the group is created (which is later),
    // and (b) the group's bounds at this point are all zeros (which is OK in this case,
    // but normally is not what bounds should have).
    //IBox bounds;
    //Vec3Scalar( bounds.min, =, minx, miny, minz );
    //Vec3Scalar( bounds.max, =, maxx, maxy, maxz ); 
    //addBoundsToBounds( bounds, &pGroup->bounds );

    for ( loc[X] = minx; loc[X] <= maxx; loc[X]++ )
    {
        for ( loc[Z] = minz; loc[Z] <= maxz; loc[Z]++ )
        {
            boxIndex = BOX_INDEX(loc[X],miny,loc[Z]);
            // Note that we start at the top and work down, as we want to ensure that outside air is the top group.
            for ( loc[Y] = miny; loc[Y] <= maxy; loc[Y]++, boxIndex++ )
            {
                assert(gBoxData[boxIndex].group == NO_GROUP_SET);

                pGroup->population++;   // the solid air group might already exist with a population
                gBoxData[boxIndex].group = groupID;
            }
        }
    }
}


static void propagateSeed(IPoint point, BoxGroup *pGroup, IPoint **pSeedStack, int *seedSize, int *seedCount)
{
    int faceDirection;
    int boxIndex,newBoxIndex;
    IPoint newPt;
    int sindex;
    IPoint *seedStack;

    // check that there's not enough room for seedStack to grow by 6 points
    if ( *seedSize <= *seedCount + 6 )
    {
        IPoint *seeds;
        *seedSize += 6;
        *seedSize = (int)(*seedSize * 1.4 + 1);
        seeds = (IPoint*)malloc(*seedSize*sizeof(IPoint));
        memcpy( seeds, (*pSeedStack), *seedCount*sizeof(IPoint));
        free( (*pSeedStack) );
        (*pSeedStack) = seeds;
    }

    seedStack = *pSeedStack;

    // special test: if the seed is air, and the original type of this block was solid, not air (was: was an entrance), then
    // don't propagate the seed. This is useful for structures, where you don't necessarily want to see
    // the ladder or whatever that closes the building off from the world but you do want the ladder
    // to stop propagation. Note that this means two air groups can be next to each other, i.e., neighbors,
    // so we need to check for this case elsewhere.
    if ( (gOptions->exportFlags & EXPT_SEAL_ENTRANCES) && !pGroup->solid )
    {
        boxIndex = BOX_INDEXV(point);
        if ( gBlockDefinitions[gBoxData[boxIndex].origType].flags & BLF_ENTRANCE )
            // In this way, you can use things like snow blocks set to display an alpha of 0 to seal off entrances,
            // and the hole will be visible at the end.  TODO: document - removed, too obscure!!!
            //if ( gBoxData[boxIndex].origType > BLOCK_AIR )
        {
            // This air block was actually something (like a ladder) that got culled out early on. Use it to seal the entrance.
            // Old code: This air block is actually an entrance, so don't propagate it further.
            return;
        }
    }

    // check the six neighbors for if they're not in a group already and have the same type, filled or unfilled
    for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
    {
        // check that new location is valid for testing
        Vec2Op( newPt, =, point );
        if ( getNeighbor( faceDirection, newPt ) )
        {
            newBoxIndex = BOX_INDEXV(newPt);
            // is neighbor not in a group, and the same sort of thing as our seed (solid or not)?
            if ( (gBoxData[newBoxIndex].group == NO_GROUP_SET) &&
                ((gBoxData[newBoxIndex].type > BLOCK_AIR) == pGroup->solid) ) {

                    // note the block is a part of this group now
                    gBoxData[newBoxIndex].group = pGroup->groupID;
                    // update the group's population, and check if this one touches a side.
                    pGroup->population++;
                    addBounds(newPt,&pGroup->bounds);

                    // and add it to the list of potential seeds to continue to propagate
                    sindex = *seedCount;
                    Vec2Op(seedStack[sindex], =, newPt);
                    (*seedCount)++;
                    // it's possible that we could run out, I think: each seed could add its six neighbors, etc.?
                    assert((*seedCount)<gBoxSizeXYZ);
            }
        }
    }
}

// return 1 if there's a valid neighbor, which is put in newx, etc.
// NOTE: it's important that "point" be copied over here, not as a point. It is changed locally.
static int getNeighbor( int faceDirection, IPoint newPoint )
{
    switch (faceDirection)
    {
    case DIRECTION_BLOCK_SIDE_LO_X: // -X
        newPoint[X]--;
        if ( newPoint[X] < gAirBox.min[X] ) return 0;
        break;
    case DIRECTION_BLOCK_BOTTOM: // -Y
        newPoint[Y]--;
        if ( newPoint[Y] < gAirBox.min[Y] ) return 0;
        break;
    case DIRECTION_BLOCK_SIDE_LO_Z: // -Z
        newPoint[Z]--;
        if ( newPoint[Z] < gAirBox.min[Z] ) return 0;
        break;
    case DIRECTION_BLOCK_SIDE_HI_X: // +X
        newPoint[X]++;
        if ( newPoint[X] > gAirBox.max[X] ) return 0;
        break;
    case DIRECTION_BLOCK_TOP: // +Y
        newPoint[Y]++;
        if ( newPoint[Y] > gAirBox.max[Y] ) return 0;
        break;
    case DIRECTION_BLOCK_SIDE_HI_Z: // +Z
        newPoint[Z]++;
        if ( newPoint[Z] > gAirBox.max[Z] ) return 0;
        break;
    }
    return 1;
}

// return 1 if there's a valid neighbor, which is put in newx, etc.
// Use only if you know that the block is not on a face
// NOTE: it's important that "point" be copied over here, passed directly and not as a pointer. It is changed locally.
static void getNeighborUnsafe( int faceDirection, IPoint newPoint )
{
    switch (faceDirection)
    {
    case DIRECTION_BLOCK_SIDE_LO_X: // -X
        newPoint[X]--;
        break;
    case DIRECTION_BLOCK_BOTTOM: // -Y
        newPoint[Y]--;
        break;
    case DIRECTION_BLOCK_SIDE_LO_Z: // -Z
        newPoint[Z]--;
        break;
    case DIRECTION_BLOCK_SIDE_HI_X: // +X
        newPoint[X]++;
        break;
    case DIRECTION_BLOCK_TOP: // +Y
        newPoint[Y]++;
        break;
    case DIRECTION_BLOCK_SIDE_HI_Z: // +Z
        newPoint[Z]++;
        break;
    }
}

static void checkAndRemoveBubbles()
{
    int i, groupID, maxPop, masterGroupID;
    IBox bounds;

    int *neighborGroups = (int *)malloc((gGroupCount+1)*sizeof(int));

    // if we are simply merging groups, then air bubbles are left as air and merely act to merge groups (I hope...)
    int fillType = (gOptions->exportFlags & EXPT_FILL_BUBBLES) ? BLOCK_GLASS : BLOCK_AIR;

    // only the very first group, which is air, will not be a bubble. All other air groups will be.
    for ( i = SURROUND_AIR_GROUP+1; i <= gGroupCount; i++ )
    {
        // is this group air?
        if ( !gGroupList[i].solid )
        {
            // a bubble is found, so merge it, along with all solid groups that border it, into one solid group.
            memset(neighborGroups,0,(gGroupCount+1)*sizeof(int));

            // the first group should always be the outside air group, the second should be solid. Just in case,
            // see if this assumption is wrong. Really, we could probably change i = 2 as the starting condition.
            assert(i>=2);

            // Find which solid groups are neighbors of this group i.
            // Search the given bounds.
            neighborGroups[gGroupList[i].groupID] = 1;
            findNeighboringGroups( &gGroupList[i].bounds, gGroupList[i].groupID, neighborGroups );

            // find the highest population solid group on the list: this will be the one we merge to.
            // Turn this group off in this list.
            maxPop = 0;
            masterGroupID = -1;
            // we loop through all groups here, as initial air group should get turned off from list
            for ( groupID = SURROUND_AIR_GROUP; groupID <= gGroupCount; groupID++ )
            {
                // check if the group is a neighbor
                if ( neighborGroups[groupID] > 0 )
                {
                    // neighboring group to air *could* also be air (and the original
                    // group itself is air); check it's solid
                    if ( gGroupList[groupID].solid)
                    {
                        // solid: find if it's larger than any previously-examined solid group
                        if ( gGroupList[groupID].population > maxPop )
                        {
                            assert(gGroupList[groupID].solid == 1);
                            maxPop = gGroupList[groupID].population;
                            masterGroupID = groupID;
                        }
                        // at the same time, decrement count by how many groups will get merged
                        // Really, at this point, all neighborGroups should be solid, but someday
                        // I want to allow air groups to border each other, and for bubbles to
                        // get formed inside of buildings, etc.
                        gStats.solidGroupsMerged++;
                        gSolidGroups--;
                        assert(gSolidGroups >= 0);
                    }
                    else
                    {
                        // since the neighbor is air, simply don't fill it, clear it from the list
                        neighborGroups[groupID] = 0;
                    }
                }
            }
            // add one back in for the master solid group that won't get merged
            gStats.solidGroupsMerged--;
            gSolidGroups++;
            assert(gSolidGroups >= 1);

            // it can be the case that there *are* no solid neighbors next to the bubble: for example,
            // six torches can surround a single bubble of air. In this case, just make the bubble solid
            // and as other bubbles are processed they'll merge with this solid (or not).
            if ( masterGroupID >= 0 )
            {
                // A solid group was found next to the bubble

                // Take the biggest group out of the list, as it's the one everyone merges to.
                neighborGroups[masterGroupID] = 0;
                // Get the union of the bounds of all groups found to merge.
                VecScalar( bounds.min, =,  999999);
                VecScalar( bounds.max, =, -999999);
                // Turn the air bubble itself back on, as it will also be filled
                neighborGroups[i] = 1;
                for ( groupID = SURROUND_AIR_GROUP+1; groupID <= gGroupCount; groupID++ )
                {
                    if ( neighborGroups[groupID] > 0 )
                    {
                        addBoundsToBounds( gGroupList[groupID].bounds, &bounds );
                    }
                }
                assert( bounds.max[Y] >= bounds.min[Y]);

                // Go through this sub-box and change all groups tagged to the new fill group.
                // We can fill air with whatever we want, since it won't be visible.
                fillGroups( &bounds, masterGroupID, 1, fillType, neighborGroups );
                // delete the air group from the count
                gStats.bubblesFound++;
                gAirGroups--;
                assert(gAirGroups>=1);
            }
            else
            {
                // no solid group found, so make only the bubble itself solid

                // Get the union of the bounds of all groups found to merge.
                bounds = gGroupList[i].bounds;
                // Turn the air bubble itself on, as it will be (the only thing) filled
                neighborGroups[i] = 1;

                // Go through this sub-box and change all groups tagged to the new fill group.
                // We can fill air with whatever we want, since it won't be visible.
                fillGroups( &bounds, i, 1, fillType, neighborGroups );

                // delete the air group from the count
                gStats.bubblesFound++;
                gAirGroups--;
                assert(gAirGroups>=1);
            }
        }
    }
    free(neighborGroups);
}

// go through box. Whichever blocks have same group, mark all neighbors into neighborGroups array.
// Note that the groupID neighborGroup entry is likely to be set, also.
static void findNeighboringGroups( IBox *bounds, int groupID, int *neighborGroups )
{
    int x,y,z;
    int boxIndex, faceDirection;

    // if you hit this assert, the bounds for the group is too large (overlaps the exterior).
    assert(bounds->min[X]>0);
    assert(bounds->min[Y]>0);
    assert(bounds->min[Z]>0);
    assert(bounds->max[X]<gBoxSize[X]-1);
    assert(bounds->max[Y]<gBoxSize[Y]-1);
    assert(bounds->max[Z]<gBoxSize[Z]-1);

    for ( x = bounds->min[X]; x <= bounds->max[X]; x++ )
    {
        for ( z = bounds->min[Z]; z <= bounds->max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,bounds->min[Y],z);
            for ( y = bounds->min[Y]; y <= bounds->max[Y]; y++, boxIndex++ )
            {
                if ( gBoxData[boxIndex].group == groupID )
                {
                    // mark the neighbors
                    for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
                    {
                        // get the neighbor's box index, to get the neighbor box's group,
                        // and set this as a group that touches the group specified. Simply set them
                        // all, again and again, brute force.
                        // Note that we don't have to check if a neighbor block location is valid! They're all inside air.
                        neighborGroups[gBoxData[boxIndex + gFaceOffset[faceDirection]].group] = 1;
                    }
                }
            }
        }
    }
}


// set the bounds for all groups, from the box data's assigned groups.
//static void establishGroupBounds()
//{
//	int i, boxIndex, groupIndex;
//	IPoint loc;
//	// get the bounds for each group. This tells us whether each group touches the full box faces,
//	// and gives us a limited volume to search when a group is merged with another group.
//
//	// first initialize bounds, except group 1, surrounding air, which is known at this point.
//	Vec2Op( gGroupList[SURROUND_AIR_GROUP].bounds.min, =, gAirBox.min );
//	Vec2Op( gGroupList[SURROUND_AIR_GROUP].bounds.max, =, gAirBox.max );
//
//	for ( i = SURROUND_AIR_GROUP+1; i <= gGroupCount; i++ )
//	{
//		VecScalar( gGroupList[i].bounds.min, =,  999999);
//		VecScalar( gGroupList[i].bounds.max, =, -999999);
//	}
//
//	// look at all locations and set. We can not bother with the air coating (group 1), as that one's done anyways
//    for ( loc[X] = gSolidBox.min[X]; loc[X] <= gSolidBox.max[X]; loc[X]++ )
//    {
//        for ( loc[Z] = gSolidBox.min[Z]; loc[Z] <= gSolidBox.max[Z]; loc[Z]++ )
//        {
//            boxIndex = BOX_INDEX(loc[X],gSolidBox.min[Y],loc[Z]);
//            for ( loc[Y] = gSolidBox.min[Y]; loc[Y] <= gSolidBox.max[Y]; loc[Y]++, boxIndex++ )
//            {
//				// get the group of the block
//				groupIndex = gBoxData[boxIndex].group;
//				assert(groupIndex >= SURROUND_AIR_GROUP );
//				if ( groupIndex > SURROUND_AIR_GROUP )
//				{
//					// group is not the surrounding air group (which is always first and whose bounds are known),
//					// so update its bounds with the location.
//					addBounds( loc, &gGroupList[groupIndex].bounds );
//				}
//			}
//		}
//	}
//}


// fill the group given with whatever is specified, if the group is of different
// solidity than the master group. So, if the master group is solid, air blocks in
// the target list will be turned to fillType. If the master group is air, solid blocks
// in the target list will be turned to fillType (usually air).
// Brute force it - we could be more clever, but that's OK
static void fillGroups( IBox *bounds, int masterGroupID, int solid, int fillType, int *targetGroupIDs )
{
    int x,y,z;
    int boxIndex;

    // if solid, we can not bother with the fringe of the box
    IPoint loc;
    BoxGroup *pGroup;

    BoxGroup *pMasterGroup = &gGroupList[masterGroupID];

    for ( x = bounds->min[X]; x <= bounds->max[X]; x++ )
    {
        for ( z = bounds->min[Z]; z <= bounds->max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,bounds->min[Y],z);
            for ( y = bounds->min[Y]; y <= bounds->max[Y]; y++, boxIndex++ )
            {
                // is this group one that should get filled by the master group?
                if ( targetGroupIDs[gBoxData[boxIndex].group] > 0 )
                {
                    // found one to fill, transfer it to master group
                    pGroup = &gGroupList[gBoxData[boxIndex].group];
                    if ( pGroup->solid != solid )
                    {
                        // target and master differ in solidity
                        // target is not solid, and master is solid, so needs to be made solid OR
                        // target is solid, and master is not solid, so need to be made air (fillType is
                        // assumed to be set correctly, to air.
                        // Note that we don't bother setting the "data" to anything;
                        // we assume the master fill type doesn't have a sub-field.

                        // special test: if a location is surrounded by trees and leaves and air, fill in with a leaf
                        if ( solid && (( fillType == BLOCK_GLASS ) || (fillType == BLOCK_STAINED_GLASS)) )
                        {
                            int i;
                            int leafFound = 0;
                            int woodSearch = 1;
                            unsigned char leafData = 0;
                            for ( i = 0; i < 6 && woodSearch; i++ )
                            {
                                int index = boxIndex+gFaceOffset[i];
                                // leaf found?
                                if ( gBlockDefinitions[gBoxData[index].type].flags & BLF_LEAF_PART )
                                {
                                    leafFound = 1;
                                    leafData = gBoxData[index].data;
                                }
                                else if ( !(gBlockDefinitions[gBoxData[index].type].flags & BLF_TREE_PART) && gBoxData[index].type != BLOCK_AIR )
                                {
                                    // not a leaf, log, or air, so we won't fill it in.
                                    woodSearch = 0;
                                }
                            }
                            if ( woodSearch && leafFound )
                            {
                                // leaf fill
                                gBoxData[boxIndex].type = BLOCK_LEAVES;
                                gBoxData[boxIndex].data = leafData;
                            }
                            else
                            {
                                // normal fill
                                gBoxData[boxIndex].type = (unsigned char)fillType;
                            }
                        }
                        else
                        {
                            gBoxData[boxIndex].type = (unsigned char)fillType;
                        }
                        gBoxData[boxIndex].data = 0x0;
                    }
                    // transfer to master group
                    gBoxData[boxIndex].group = masterGroupID;

                    // note that this will make this group's bounds invalid,
                    // but since the group is going away, it doesn't matter
                    pGroup->population--;
                    pMasterGroup->population++;
                    Vec3Scalar( loc, =, x,y,z);
                    addBounds( loc, &pMasterGroup->bounds );
                }
            }
        }
    }
}

static void addBounds( IPoint loc, IBox *bounds )
{
    if ( loc[X] < bounds->min[X] )
        bounds->min[X] = loc[X];
    if ( loc[X] > bounds->max[X] )
        bounds->max[X] = loc[X];
    if ( loc[Y] < bounds->min[Y] )
        bounds->min[Y] = loc[Y];
    if ( loc[Y] > bounds->max[Y] )
        bounds->max[Y] = loc[Y];
    if ( loc[Z] < bounds->min[Z] )
        bounds->min[Z] = loc[Z];
    if ( loc[Z] > bounds->max[Z] )
        bounds->max[Z] = loc[Z];
}

// Add the first bounds to the seconds bounds
static void addBoundsToBounds( IBox inBounds, IBox *bounds )
{
    if ( inBounds.min[X] < bounds->min[X] )
        bounds->min[X] = inBounds.min[X];
    if ( inBounds.max[X] > bounds->max[X] )
        bounds->max[X] = inBounds.max[X];
    if ( inBounds.min[Y] < bounds->min[Y] )
        bounds->min[Y] = inBounds.min[Y];
    if ( inBounds.max[Y] > bounds->max[Y] )
        bounds->max[Y] = inBounds.max[Y];
    if ( inBounds.min[Z] < bounds->min[Z] )
        bounds->min[Z] = inBounds.min[Z];
    if ( inBounds.max[Z] > bounds->max[Z] )
        bounds->max[Z] = inBounds.max[Z];
}

static int connectCornerTips()
{
    int boxIndex;
    int x,y,z;
    int filledTip=0;

    // Find solid cells to test for tips to connect. Note we test in +Y order,
    // looking below at the previous Y plane for connectors. In this way we will
    // avoid interactions of new blocks getting added and causing some cascade
    // effect of connections with other tips now touched.

    // Note that we *always* add below for now. A more elaborate algorithm would
    // be to look at the 6 possible boxes to add to and use the one with the
    // largest obscurity factor. Too involved!
    for ( y = gSolidBox.min[Y]+1; y <= gSolidBox.max[Y]; y++ )
    {
        for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
        {
            for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
            {
                boxIndex = BOX_INDEX(x,y,z);

                // check if it's solid - if so, we'll check if the spot below is air
                if ( gBoxData[boxIndex].type > BLOCK_AIR )
                {
                    // quick out: if -Y cell is air, then continue checking, else we're done!
                    if ( gBoxData[boxIndex-1].type == BLOCK_AIR)
                    {
                        int hasCorner = checkForCorner(boxIndex,-1,-1);
                        if (!hasCorner)
                            hasCorner = checkForCorner(boxIndex,-1, 1);
                        if (!hasCorner)
                            hasCorner = checkForCorner(boxIndex, 1,-1);
                        if (!hasCorner)
                            hasCorner = checkForCorner(boxIndex, 1, 1);

                        // did we find at least one diagonal tip?
                        if ( hasCorner )
                        {
                            // add cell to group above
                            IPoint loc;
                            int airBoxIndex = boxIndex-1;
                            assert(gBoxData[airBoxIndex].type == BLOCK_AIR );
                            if ( gOptions->exportFlags & EXPT_DEBUG_SHOW_WELDS )
                            {
                                gBoxData[airBoxIndex].type = DEBUG_CORNER_TOUCH_TYPE;
                            }
                            else
                            {
                                // Here is where we actually add the block, by using the
                                // data of the boxIndex.
                                // copy this over, unless true geometry has been created
                                // and the original block was already output as true connector geometry.
                                // Basically, we're crossing fingers that the original block can connect
                                // the blocks together. TODO...?
                                gBoxData[airBoxIndex].type = gBoxData[boxIndex].type;
                                gBoxData[airBoxIndex].data = gBoxData[boxIndex].data;
                            }
                            gStats.blocksCornertipWelded++;

                            // we don't know which item on the group list is the air block's
                            // group, so can't easily subtract one from its population. But, we
                            // don't really care about the air group populations, ever.
                            gBoxData[airBoxIndex].group = gBoxData[boxIndex].group;
                            assert(gGroupList[gBoxData[boxIndex].group].solid);
                            gGroupList[gBoxData[boxIndex].group].population++;
                            Vec3Scalar( loc, =, x, y-1, z );
                            addBounds( loc, &gGroupList[gBoxData[boxIndex].group].bounds );

                            filledTip = 1;
                        }
                    }
                }
            }
        }
    }

    return filledTip;
}

static int checkForCorner(int boxIndex, int offx, int offz)
{
    int tipCornerIndex = boxIndex + offx*gBoxSizeYZ - 1 + offz*gBoxSize[Y];
    // check diagonally opposite corner in -Y direction to see if it's solid.
    // If so, check if the groups do not match (meaning they are disconnected parts, like
    // a balloon string).
    // If so, continue search, as these two could get joined.
    if ( (gBoxData[tipCornerIndex].type != BLOCK_AIR) &&
        (gBoxData[tipCornerIndex].group != gBoxData[boxIndex].group) )
    {
        // solid, so now check 2x2x2 to see if there are just two filled cells (which must be the original
        // and the diagonal ones).
        int i;
        // cleverness: by avoid index 0 (where x, y, and z are all zero) and index 1 (where they're all one)
        // we can entirely avoid testing the original and diagonal blocks!
        for ( i = 1; i < 7; i++ )
        {
            int x = (i>=4);
            int y = ((i%4)>=2);
            int z = i%2;

            if ( gBoxData[ boxIndex + x*offx*gBoxSizeYZ - y + z*offz*gBoxSize[Y] ].type != BLOCK_AIR )
                // one of the six is not air - return
                return 0;
        }
        return 1;
    }
    else
    {
        // air, or groups match,
        return 0;
    }
}

// return 1 if any fixes occurred.
static int fixTouchingEdges()
{
    // strategy: walk through solid box. Look at 6 of 12 edges if cell is solid:
    // does 2x2x1 have the right pattern of two diagonally-opposite solids?
    // If so, mark the two empty cells with a count and with flags pointing to the
    // neighboring cell.

    // Once all empty cells are marked like this, go through them and make records for sorting:
    // record has box index, count, and distance from center of model (computed in first pass,
    // as a by-product). Sort these records, first by count, then distance as a tie-breaker.
    // Fill in this list of empties: note that you have to decrement the count of the *grid box*
    // of the empty counts, since each edge getting repaired has +2 count, so both counts must
    // be subtracted. Some records will therefore have a count of zero when they're found on the
    // sorted list, as previous elements processed will have decremented their counts.
    //
    // When filling a location, do a merge of groups if needed. Choose material for new cell as
    // that of the +Y object first, then X and Z, then -Y last. Update group population and bounds.
    int boxIndex;
    int x,y,z;
    int touchCount,i;
    Point avgLoc,floc;
    int solidBlocks=0;
    TouchRecord *touchList;
    //int maxVal;

    // big allocation, not much to be done about it.
    gTouchGrid = (TouchCell*)malloc(gBoxSizeXYZ*sizeof(TouchCell));
    memset(gTouchGrid,0,gBoxSizeXYZ*sizeof(TouchCell));

    gTouchSize = 0;

    VecScalar(avgLoc, =, 0.0f);

    // Find solid cells to test for edges to connect
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                // check if it's solid - if so, add to average center computations,
                // and then see if there are any edges that touch
                if ( gBoxData[boxIndex].type > BLOCK_AIR )
                {
                    // TODO: this just averages all solid blocks purely by position.
                    // Some other weighting from center of air space might be better?
                    Vec3Scalar( avgLoc, +=, x, y, z);
                    solidBlocks++;

                    // Test the following edges:
                    // +X face and the four edges there
                    // +Z+Y and +Z-Y
                    // This order guarantees us that we'll always count forward into grid,
                    // which I'm not sure is important (I once thought it was), i.e. that
                    // we will never examine cells for solidity that have already been touched.

                    // quick out: if +X cell is air, +X face edges are processed, else all can be ignored
                    if ( gBoxData[boxIndex+gBoxSizeYZ].type == BLOCK_AIR)
                    {
                        checkForTouchingEdge(boxIndex,1,-1, 0);
                        checkForTouchingEdge(boxIndex,1, 0,-1);
                        checkForTouchingEdge(boxIndex,1, 1, 0);
                        checkForTouchingEdge(boxIndex,1, 0, 1);
                    }
                    // quick out, if +Z cell is air, the two +Z face edges are processed, else all can be ignored
                    if ( gBoxData[boxIndex+gBoxSize[Y]].type == BLOCK_AIR)
                    {
                        checkForTouchingEdge(boxIndex,0,-1,1);
                        checkForTouchingEdge(boxIndex,0, 1,1);
                    }
                }
            }
        }
    }
    assert( solidBlocks );

    // were no touching edges found? Then return!
    if ( gTouchSize == 0 )
        return 0;

    // get average location
    VecScalar( avgLoc, /=, solidBlocks );

    // now find largest of three, X,Y,Z, and subtract it from Y center. We want to bias towards putting
    // blocks *under* other blocks, since we tend to look down on terrain.
    //maxVal = ( gSolidBox.max[X]-gSolidBox.min[X] > gSolidBox.max[Z]-gSolidBox.min[Z] ) ? gSolidBox.max[X]-gSolidBox.min[X]+1 : gSolidBox.max[Z]-gSolidBox.min[Z]+1;
    //maxVal = ( maxVal > gSolidBox.max[Y]-gSolidBox.min[Y] ) ? maxVal : gSolidBox.max[Y]-gSolidBox.min[Y]+1;
    //avgLoc[Y] -= maxVal;

    // allocate space for touched air cells
    touchList = (TouchRecord *)malloc(gTouchSize*sizeof(TouchRecord));
    touchCount = 0;

    // what is the distance from corner to corner of the solid box? We choose this distance because
    // avgLoc could be skewed, way over to some other location, due to an imbalance in where stuff is
    // located. We want to ensure that the Y location always dominates over the XZ distance value, so
    // that lower blocks are favored over upper ones. At least, that's today's theory of what's good to add
    // in for a block that connects two edges.
    float norm = (float)sqrt((float)gBoxSize[X] * (float)gBoxSize[X] + (float)gBoxSize[Z] * (float)gBoxSize[Z]);

    // go through the grid, collecting up the locations needing processing
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                // find potential fill locations and put them in a list
                if ( gTouchGrid[boxIndex].count > 0 )
                {
                    // here's a possible manifold-fill location, an air block that if
                    // we fill it in we will get rid of a manifold edge.
                    // So add a record.
                    touchList[touchCount].obscurity = gTouchGrid[boxIndex].obscurity;
                    touchList[touchCount].count = gTouchGrid[boxIndex].count;
                    touchList[touchCount].boxIndex = boxIndex;
                    assert(gBoxData[boxIndex].type == BLOCK_AIR );

                    Vec3Scalar(floc, = (float), x,y,z);
                    touchList[touchCount].distance = computeHidingDistance(floc, avgLoc, norm);
                    touchCount++;
                }
            }
        }
    }

    assert( touchCount == gTouchSize );

    // we have the list of touched air cells that could be filled - sort it!
    qsort_s(touchList,touchCount,sizeof(TouchRecord),touchRecordCompare,NULL);

    // Now we have a sorted list of places to fill in, in importance order.
    // Check if the count *of the original touch grid cell* is positive.
    // Fill each in, decrement the neighboring air cells associated with each edge.
    // Also make sure to decrement the neighbor flags of the neighboring air cells (PITA).
    //
    // Then make the air cell into a solid thing. When filling:
    // First, choose the material: whatever is at +Y should be used, then XZXZ, then -Y
    // - good as any strategy
    // Next, see if all neighbors are in same group. Find the largest of the groups.
    // Add the new cell, and its bounds, to this largest group.
    // Merge the other groups to the largest group.

    for ( i = 0; i < touchCount; i++ )
    {
        // note that the ONLY thing we should be accessing from the touchList is the boxIndex.
        // We know the order is sorted now, so the other two bits of data (*especially* the count)
        // are useless. We're going to decrement the count on the gTouchGrid and its neighbors,
        // which is why this count is the one to access.
        boxIndex = touchList[i].boxIndex;

        // Any edges left to fix in the touch grid cell on the sorted list? Previous operations
        // might have decremented its count to 0.

        if ( gTouchGrid[boxIndex].count > 0 )
        {
            int boxMtlIndex=-999;
            int foundBlock=0;
            int faceGroupIDCount=0;
            int faceGroupID[6];
            int masterGroupID;
            int maxPop;
            int i;
            IPoint loc;

            // decrement this cell's neighbors' counts and pointers to it, each by one
            decrementNeighbors( boxIndex );

            // choose material for fill: +Y neighbor, on down to -Y - use an array to test
            // also get groups of solid blocks found

            // find 6 neighboring solids, if they exist (at least two will)
            for ( i = 0; i < 6; i++ )
            {
                int index = boxIndex+gFaceOffset[i];
                foundBlock = (gBoxData[index].type > BLOCK_AIR);
                if ( foundBlock )
                {
                    int j;
                    int foundGroup=0;
                    int groupID = gBoxData[index].group;
                    if ( boxMtlIndex < 0)
                        // store away the index of the first material found
                        boxMtlIndex = index;

                    for ( j = 0; j < faceGroupIDCount && !foundGroup; j++ )
                    {
                        if ( faceGroupID[j] == groupID )
                        {
                            foundGroup = 1;
                        }
                    }
                    if ( !foundGroup )
                    {
                        // add a new group to list
                        faceGroupID[faceGroupIDCount] = groupID;
                        faceGroupIDCount++;
                    }
                }
            }
            assert(faceGroupIDCount>0);

            // Now time for fun with the group list:
            // Which group is the largest? That one will be used to merge the rest. The new block also
            // gets this group ID, adds to pop.
            masterGroupID = faceGroupID[0];
            maxPop = gGroupList[faceGroupID[0]].population;
            for ( i = 1; i < faceGroupIDCount; i++ )
            {
                if ( gGroupList[faceGroupID[i]].population > maxPop )
                {
                    assert(gGroupList[faceGroupID[i]].solid);
                    masterGroupID = faceGroupID[i];
                    maxPop = gGroupList[masterGroupID].population;
                }
            }

            // tada! The actual work: the air block is now filled
            // if weld debugging is going on, we should make these some special color - what?
            assert(gBoxData[boxIndex].type == BLOCK_AIR );
            if ( gOptions->exportFlags & EXPT_DEBUG_SHOW_WELDS )
            {
                gBoxData[boxIndex].type = DEBUG_EDGE_TOUCH_TYPE;
            }
            else
            {
                // copy this over, unless true geometry has been created
                // and the original block was already output as true connector geometry.
                // Basically, we're crossing fingers that the original block can connect
                // the blocks together. TODO...?
                gBoxData[boxIndex].type = gBoxData[boxMtlIndex].type;
                gBoxData[boxIndex].data = gBoxData[boxMtlIndex].data;
            }
            gStats.blocksManifoldWelded++;

            // we don't know which item on the group list is the air block's
            // group, so can't easily subtract one from its population. But, we
            // don't really care about the air group populations, ever.
            gBoxData[boxIndex].group = masterGroupID;
            gGroupList[masterGroupID].population++;
            boxIndexToLoc( loc, boxIndex );
            addBounds( loc, &gGroupList[masterGroupID].bounds );
            assert(gGroupList[masterGroupID].solid);

            // If multiple groups found, then we must merge all the groups on neighborList.
            if ( faceGroupIDCount > 1 )
            {
                IBox bounds;
                int *neighborGroups = (int *)malloc((gGroupCount+1)*sizeof(int));
                memset(neighborGroups,0,(gGroupCount+1)*sizeof(int));

                gStats.solidGroupsMerged += (faceGroupIDCount-1);
                gSolidGroups -= (faceGroupIDCount-1);
                assert(gSolidGroups >= 1);

                // Get the union of the bounds of all groups found to merge.
                VecScalar( bounds.min, =,  999999);
                VecScalar( bounds.max, =, -999999);
                // mark all the neighbor groups that are to merge
                for ( i = 0; i < faceGroupIDCount; i++ )
                {
                    if ( faceGroupID[i] != masterGroupID )
                    {
                        neighborGroups[faceGroupID[i]] = 1;
                        addBoundsToBounds( gGroupList[faceGroupID[i]].bounds, &bounds );
                    }
                }

                assert( bounds.max[Y] >= bounds.min[Y]);

                // Go through this sub-box and change all groups tagged to the new fill group.
                // We can fill air with whatever we want, since it won't be visible.
                // We don't actually want to fill with bedrock, but put that in for debugging -
                // really, groups just get merged. Also, note that the method below adds to
                // the master group's bounds.
                fillGroups( &bounds, masterGroupID, 1, BLOCK_LAVA, neighborGroups );

                free(neighborGroups);
            }

            // Note that we do not check if this air block getting filled gives a new
            // manifold edge somewhere or not, as some later block filling in might
            // make it go away, or change things some other way. TODO We could actually
            // check here, but it's not a huge deal to check everything again, I hope...
        }
    }

    free(touchList);
    free(gTouchGrid);
    gStats.numberManifoldPasses++;

    return touchCount ? 1 : 0;
}

static int touchRecordCompare( void* context, const void *str1, const void *str2)
{
    TouchRecord *t1;
    TouchRecord *t2;
    t1 = (TouchRecord*)str1;
    t2 = (TouchRecord*)str2;
    context;    // make a useless reference to the unused variable, to avoid C4100 warning
    // Blocks that had something in them originally (e.g. rails, redstone, or other things that got flattened)
    // are more significant than blocks of air, so the air should get covered up first so the rails aren't covered.
    // if the blocks are both air, or were both solid, then we need a different thing to test on.
    if ( (gBoxData[t1->boxIndex].origType == BLOCK_AIR) == (gBoxData[t2->boxIndex].origType == BLOCK_AIR) )
    {
        // both elements are air or both are not air
        // elements that are in more of a crevice (more faces covered by solid neighbors) get filled first
        if ( t1->obscurity == t2->obscurity )
        {
            // elements fixing multiple manifold problems are filled before those fixing just one
            if ( t1->count == t2->count )
            {
                // elements closer to the center of the model (or center-bottom) should be filled in first, over those farther away
                return ( (t1->distance < t2->distance) ? -1 : ((t1->distance == t2->distance)) ? 0 : 1 );
            }
            else return ( (t1->count > t2->count) ? -1 : 1 );
        }
        else return ( (t1->obscurity > t2->obscurity) ? -1 : 1 );
    }
    // one element is air, so favor filling it first
    else return ( (gBoxData[t1->boxIndex].origType < gBoxData[t2->boxIndex].origType ) ? -1 : 1 );
}


static void checkForTouchingEdge(int boxIndex, int offx, int offy, int offz)
{
    // we assume the location itself is solid. Check if diagonal is solid
    int otherSolidIndex = boxIndex + offx*gBoxSizeYZ + offy + offz*gBoxSize[Y];
    if ( gBoxData[otherSolidIndex].type > BLOCK_AIR )
    {
        // so far so good, both are solid, so we have two diagonally-opposite blocks;
        // do we want to connect all diagonals (usually a bad option), or do the groups differ?
        if ( (gOptions->exportFlags & EXPT_CONNECT_ALL_EDGES) ||
            ( gBoxData[boxIndex].group != gBoxData[otherSolidIndex].group ) )
        {
            // groups differ (or all edges should be connected)
            int n1index=-999;
            int n2index=-999;
            int n1neighbor=-999;
            int n2neighbor=-999;
            int foundPair = 0;
            // is this the +X face or +Z face
            if ( offx == 1 )
            {
                // we're checking the +X face. The calling method has already tested +X and knows it's air.
                // So just use the other two offsets to check if the other direction is air.

                // so begins the brute force. There's probably some clever way to do this...
                if ( gBoxData[boxIndex + offy + offz*gBoxSize[Y]].type == BLOCK_AIR )
                {
                    // manifold found! So, mark the two air blocks, +X and y/z offset, and put the proper
                    // TOUCH_ flags in the touch grid.
                    foundPair = 1;
                    gStats.nonManifoldEdgesFound++;
                    n1index = boxIndex+gBoxSizeYZ;
                    if ( offy > 0 )
                    {
                        // +X+Y
                        n2index = boxIndex+1;
                        n1neighbor = TOUCH_MX_PY;
                        n2neighbor = TOUCH_PX_MY;
                    }
                    else if ( offy < 0 )
                    {
                        // +X-Y
                        n2index = boxIndex-1;
                        n1neighbor = TOUCH_MX_MY;
                        n2neighbor = TOUCH_PX_PY;
                    }
                    else if ( offz > 0)
                    {
                        // +X+Z
                        n2index = boxIndex+gBoxSize[Y];
                        n1neighbor = TOUCH_MX_PZ;
                        n2neighbor = TOUCH_PX_MZ;
                    }
                    else
                    {
                        // +X-Z
                        n2index = boxIndex-gBoxSize[Y];
                        n1neighbor = TOUCH_MX_MZ;
                        n2neighbor = TOUCH_PX_PZ;
                    }
                }
            }
            else
            {
                // we're on the +Z face, just need to test the Y offset for AIR
                assert(offz == 1);
                if ( gBoxData[boxIndex + offy].type == BLOCK_AIR )
                {
                    foundPair = 1;
                    assert(offx == 0);
                    gStats.nonManifoldEdgesFound++;
                    n1index = boxIndex+gBoxSize[Y];
                    // we know Z face is touched, now is it +Y or -Y?
                    if ( offy > 0)
                    {
                        // +Z+Y is the solid neighbor, so the +Z air neighbors +Y and vice versa:
                        n2index = boxIndex+1;
                        n1neighbor = TOUCH_PY_MZ;
                        n2neighbor = TOUCH_MY_PZ;
                    }
                    else
                    {
                        // +Z-Y is the solid neighbor
                        assert(offy == -1);
                        n2index = boxIndex-1;
                        n1neighbor = TOUCH_MY_MZ;
                        n2neighbor = TOUCH_PY_PZ;
                    }
                }
            }
            if ( foundPair )
            {
                // a pair is found. Compute obscurity for each (how much the block is blocked),
                // and whichever has the highest obscurity win
                int n1obscurity,n2obscurity,obscurityMatches;
                assert(n1index>=0);
                assert(n2index>=0);
                n1obscurity = computeObscurity(n1index);
                n2obscurity = computeObscurity(n2index);
                obscurityMatches = (n1obscurity == n2obscurity);

                // We check if one obscurity is larger than the other;
                // if it is, it wins, and the other won't need to get added.
                // Note that the count in a cell, by this code, then does *not* match
                // how many neighbors it will subtract from others when it is filled.
                if ( n1obscurity >= n2obscurity)
                {
                    // adding a new cell? Note it
                    if ( gTouchGrid[n1index].count == 0 )
                        gTouchSize++;
                    // add the fact that this cell touches one manifold edge
                    gTouchGrid[n1index].count++;
                    // note which neighbor it connects to
                    gTouchGrid[n1index].connections |=  obscurityMatches ? n1neighbor : 0x0;
                }
                if ( n2obscurity >= n1obscurity)
                {
                    if ( gTouchGrid[n2index].count == 0 )
                        gTouchSize++;
                    gTouchGrid[n2index].count++;
                    gTouchGrid[n2index].connections |= obscurityMatches ? n2neighbor : 0x0;
                }

                //// adding a new cell? Note it
                //if ( gTouchGrid[n1index].count == 0 )
                //    gTouchSize++;
                //// add the fact that this cell touches one manifold edge
                //gTouchGrid[n1index].count++;
                //// note which neighbor it connects to
                //gTouchGrid[n1index].connections |= n1neighbor;

                //if ( gTouchGrid[n2index].count == 0 )
                //    gTouchSize++;
                //gTouchGrid[n2index].count++;
                //gTouchGrid[n2index].connections |= n2neighbor;
            }
        }
    }
}

// count how many of the six directions for this cell are blocked by something solid
static int computeObscurity( int boxIndex )
{
    int obscurity = gTouchGrid[boxIndex].obscurity;

    // we know that obscurity must be 2 or more; so 0 means "not set"
    if ( obscurity == 0 )
    {
        int faceDirection;
        IPoint loc;
        boxIndexToLoc( loc, boxIndex );

        for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
        {
            // we want to get the start & ending locations and increment for the loop to
            // check obscurity.
            int start = boxIndex;
            int incr=-999;
            int hit=0;
            int i, cellIndex, cellsToLoop;
            int axis = faceDirection % 3;

            switch (axis)
            {
            case X:
                incr=gBoxSizeYZ;
                break;
            case Y:
                incr=1;
                break;
            case Z:
                incr=gBoxSize[Y];
                break;
            default:
                assert(0);
            }

            if ( faceDirection < 3 )
            {
                // traveling negative
                incr = -incr;
                cellsToLoop = loc[axis] - gSolidBox.min[axis];
            }
            else
            {
                // traveling positive
                cellsToLoop = gSolidBox.max[axis] - loc[axis];
            }
            // get past first cell (where we started)
            start += incr;

            // now check the stretch of cells in the given direction
            for ( i = 0, cellIndex = start; i < cellsToLoop && !hit; i++, cellIndex += incr )
            {
                if ( gBoxData[cellIndex].type > BLOCK_AIR )
                    hit = 1;
            }
            obscurity += hit;
        }

    }
    return obscurity;
}

// given location, turn off neighbors from grid
static void decrementNeighbors( int boxIndex )
{
    int nc = 0;
    int offset;
    int connections = gTouchGrid[boxIndex].connections;

    if ( connections & TOUCH_MX_MY )
    {
        nc++;
        offset = boxIndex - gBoxSizeYZ - 1;
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PX_PY;
    }
    if ( connections & TOUCH_MX_MZ )
    {
        nc++;
        offset = boxIndex - gBoxSizeYZ - gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PX_PZ;
    }
    if ( connections & TOUCH_MY_MZ )
    {
        nc++;
        offset = boxIndex - 1 - gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PY_PZ;
    }

    if ( connections & TOUCH_MX_PY )
    {
        nc++;
        offset = boxIndex - gBoxSizeYZ + 1;
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PX_MY;
    }
    if ( connections & TOUCH_MX_PZ )
    {
        nc++;
        offset = boxIndex - gBoxSizeYZ + gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PX_MZ;
    }
    if ( connections & TOUCH_MY_PZ )
    {
        nc++;
        offset = boxIndex - 1 + gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_PY_MZ;
    }

    if ( connections & TOUCH_PX_MY )
    {
        nc++;
        offset = boxIndex + gBoxSizeYZ - 1;
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MX_PY;
    }
    if ( connections & TOUCH_PX_MZ )
    {
        nc++;
        offset = boxIndex + gBoxSizeYZ - gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MX_PZ;
    }
    if ( connections & TOUCH_PY_MZ )
    {
        nc++;
        offset = boxIndex + 1 - gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MY_PZ;
    }

    if ( connections & TOUCH_PX_PY )
    {
        nc++;
        offset = boxIndex + gBoxSizeYZ + 1;
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MX_MY;
    }
    if ( connections & TOUCH_PX_PZ )
    {
        nc++;
        offset = boxIndex + gBoxSizeYZ + gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MX_MZ;
    }
    if ( connections & TOUCH_PY_PZ )
    {
        nc++;
        offset = boxIndex + 1 + gBoxSize[Y];
        assert(gTouchGrid[offset].count>0);
        gTouchGrid[offset].count--;
        gTouchGrid[offset].connections &= ~TOUCH_MY_MZ;
    }
    // we should have cleared as many as we had in the cell
    // Well, no longer true: we can now have a count > nc,
    // since we now use obscurity to win early on.
    assert(nc <= gTouchGrid[boxIndex].count);

    // clear cell itself
    gTouchGrid[boxIndex].connections = 0;
    gTouchGrid[boxIndex].count = 0;
}

// norm is half the distance from the gBox corner to center, in XZ plane, squared.
// The idea here is that the Y location should dominate: if two Y's match, then the
// distance from the center on that plane is used. This distance is the fractional
// part, e.g. 24.182321 means 24 high, 18% of the way from the center to the corner (squared).
static float computeHidingDistance( Point loc1, Point loc2, float norm )
{
    Vector vec;
    // a little wasteful; really just need X and Z squared
    Vec3Op( vec, =, loc1, -, loc2 );
    Vec2Op( vec, *=, vec );
    return ( loc1[Y] + sqrt( vec[X] + vec[Z] ) / norm );
}

static void boxIndexToLoc( IPoint loc, int boxIndex )
{
    int yzLeft;
    loc[X] = boxIndex / gBoxSizeYZ;
    yzLeft = boxIndex % gBoxSizeYZ;
    loc[Z] = yzLeft / gBoxSize[Y];
    loc[Y] = yzLeft % gBoxSize[Y];
}


static void deleteFloatingGroups()
{
    BoxGroup *pGroup;
    int deleteGroup;
    int i,x,y,z;
    int boxIndex;
    int treeParts;
    int *neighborGroups;

    // Find largest group and don't delete it, so there's always something left.
    // If there's a tie on size, lower Y bounds is the tie breaker
    // [don't need to look at air group]
    int survivorGroup = -999;
    int maxPop = -999;
    int minY=999;
    for ( i = SURROUND_AIR_GROUP+1; i <= gGroupCount; i++ )
    {
        pGroup = &gGroupList[i];
        // is this group solid
        if ( pGroup->solid )
        {
            // if this group is larger than any other group, or equal to another
            // group but lower down (nearer to ground), make sure to save it by
            // making it designated as the largest group.
            if ( (pGroup->population > maxPop) ||
                ( (pGroup->population == maxPop) && (pGroup->bounds.min[Y] < minY)))
            {
                maxPop = pGroup->population;
                survivorGroup = i;
                minY = pGroup->bounds.min[Y];
            }
        }
    }
    assert(maxPop > 0);

    // [don't need to look at air group]
    for ( i = SURROUND_AIR_GROUP+1; i <= gGroupCount; i++ )
    {
        pGroup = &gGroupList[i];
        // is this group solid and does it have anything in it (not already merged)?
        if ( pGroup->solid && (pGroup->population > 0) && i != survivorGroup )
        {
            deleteGroup = 0;
            // is it too small and is it not touching ground level?
            if ( pGroup->population < gOptions->pEFD->floaterCountVal )
                // this is the "touching ground level" test, but it's too annoying - leaves tiny clumps at the base
                // && (pGroup->bounds.min[Y] > gSolidBox.min[Y]) )
            {
                deleteGroup = 1;
            }
            else
            {
                // If not too small, is it all tree?
                // Test all objects in group - are they all tree parts?

                // TODO?: a better rule might be "if any group is hanging and is also touching the solid box X or Z faces, delete it"
                // This would certainly be good for trees, though I think deleting hanging trees always is a safe thing.

                // assume delete unless we find something non-wood
                deleteGroup = 1;
                treeParts = 0x0;
                for ( x = pGroup->bounds.min[X]; deleteGroup && x <= pGroup->bounds.max[X]; x++ )
                {
                    for ( z = pGroup->bounds.min[Z]; deleteGroup && z <= pGroup->bounds.max[Z]; z++ )
                    {
                        boxIndex = BOX_INDEX(x,pGroup->bounds.min[Y],z);
                        for ( y = pGroup->bounds.min[Y]; deleteGroup && y <= pGroup->bounds.max[Y]; y++, boxIndex++ )
                        {
                            // is this group one that should get filled by the master group?
                            if ( gBoxData[boxIndex].group == i)
                            {
                                // group matches: is it a tree part? Or is it a glass bubble that is
                                // is surrounded by tree bits? (this can happen, some trees grow funny)
                                if ( (gBlockDefinitions[gBoxData[boxIndex].type].flags & BLF_TREE_PART) ||
                                    (gBoxData[boxIndex].origType == BLOCK_AIR) || (gBoxData[boxIndex].origType == BLOCK_VINES) )
                                {
                                    // tree part, mark which parts
                                    treeParts |= gBlockDefinitions[gBoxData[boxIndex].type].flags;
                                }
                                else
                                {
                                    deleteGroup = 0;
                                }
                            }
                        }
                    }
                }
                if ( deleteGroup )
                {
                    // final test; were there any leaves? If no leaves in leaves & trunk, then just trunk,
                    // which could be fine: totem pole, house, etc.
                    if ( !(treeParts & BLF_LEAF_PART) )
                    {
                        // no leaves, so don't delete
                        deleteGroup = 0;
                    }
                }
            }
            // ok, tests done: delete?
            if ( deleteGroup )
            {
                assert( i == pGroup->groupID );
                neighborGroups = (int *)malloc((gGroupCount+1)*sizeof(int));
                memset(neighborGroups,0,(gGroupCount+1)*sizeof(int));
                neighborGroups[i] = 1;
                gStats.blocksFloaterDeleted += pGroup->population;
                fillGroups( &(pGroup->bounds), SURROUND_AIR_GROUP, 0, BLOCK_AIR, neighborGroups );
                gStats.floaterGroupsDeleted++;
                gSolidGroups--;
                assert(gSolidGroups >= 0);
                free(neighborGroups);
            }
        }
    }
}

static int determineScaleAndHollowAndMelt()
{
    // get these for statistics and for autoscaling.
    // If returns 0, nothing in box. Abort output.
    gBlockCount = getDimensionsAndCount(gFilledBoxSize);
    if ( gBlockCount + gModel.billboardCount == 0 )
    {
        // there really should be something in the box
        assert(0);
        return MW_NO_BLOCKS_FOUND;
    }

    // fill in statistics - the density is actually used for cost scaling
    gStats.numBlocks = gBlockCount;
    gStats.density = (float)gStats.numBlocks / (float)(gFilledBoxSize[X]*gFilledBoxSize[Y]*gFilledBoxSize[Z]);
    gStats.numGroups = gAirGroups + gSolidGroups;
    gStats.numAirGroups = gAirGroups;
    gStats.numSolidGroups = gSolidGroups;

    // determine scale for vertices at this point.    
    if ( gOptions->pEFD->radioScaleToHeight )
    {
        // scale to desired height
        gModel.scale = gOptions->pEFD->modelHeightVal*CM_TO_METERS / gFilledBoxSize[Y];
    }
    else if ( gOptions->pEFD->radioScaleToMaterial )
    {
        // autoscale to cheapest size - good for test objects
        // You can make them smaller still, but have to be careful about wall thickness

        // in meters - internally we use meters throughout
        float minWall = gMtlCostTable[gPhysMtl].minWall;

        // check that dimensions are big enough
        float sum = gFilledBoxSize[X] + gFilledBoxSize[Y] + gFilledBoxSize[Z];

        // for colored sandstone, sum must be > 65 mm, which is 0.065 - this is often the limiter
        gModel.scale = gMtlCostTable[gPhysMtl].minDimensionSum / sum;

        // minimum wall thickness trumps minimum dimension sum
        if ( gModel.scale < minWall )
        {
            gModel.scale = minWall;
        }
    }
    else if ( gOptions->pEFD->radioScaleByBlock )
    {
        // simple straight up block size
        gModel.scale = gOptions->pEFD->blockSizeVal[gOptions->pEFD->fileType]*MM_TO_METERS;
    }
    else
    {
        assert(gOptions->pEFD->radioScaleByCost);
        scaleByCost();
    }

    // now compute how many blocks are needed for a minimally-thick wall
    gWallBlockThickness = (int)(ceil(gMtlCostTable[gPhysMtl].minWall/gModel.scale));

    // Remove unneeded material. While bubbles are illegal, it's fine to hollow out the base of any object, working from the
    //    bottom up. Option: delete a block at the base if it has neighboring blocks in all 8 positions on this level
    //    and all 9 positions on the level above. This may be overconservative in some cases, but is safe. Mark all
    //    these positions, working up the object, then delete.
    // Now hollow - should be the last thing done, as we always want to build up before deleting
    if ( gOptions->exportFlags & EXPT_HOLLOW_BOTTOM )
    {
        // Check each solid block on the bottom layer (so we start +2 in from X & Z edges)
        // If the 9 neighbors above and 8 neighbors on this level are all solid, we can clear
        // this location. Save the location in a list and move on (since this location will
        // affect other picks).
        // [We could try to share neighbors samples from location to location, but that's messy.]
        hollowBottomOfModel();
    }

    if ( gOptions->pEFD->chkMeltSnow )
    {
        // Simple: remove any snow blocks found. This is useful to use in conjunction with hollowing: the hollow
        // operation clears the base, then you melt snow on the floor of your building, just above this hollow
        // area. Now your building's interior is connected to the hollow area below and so will clear of material.
        meltSnow();
    }

    // If we're scaling by cost, we now have the *real* block count after hollowing. We needed stats
    // before for things like wall thickness, which hollowing uses. If we hollowed at all (check stats), we need to
    // rescale to a new (larger) block size, since we can now afford more material. This is actually good, in that
    // blocks will never get smaller (walls won't get thinner), but it might make for slightly thicker walls than
    // needed, if gWallBlockThickness could have been decreased.
    if ( gOptions->pEFD->radioScaleByCost )
    {
        scaleByCost();
    }

    return MW_NO_ERROR;
}

// this gives the vertical 
static float computeMachineVolume()
{
    int x,y,z,boxIndex;
    int blockSum = 0;
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            int minSolid = 999;
            int maxSolid = -999;
            // check the 3x3 in the column for its max and min heights
            for ( int xi = -1; xi <= 1; xi++ )
            {
                for ( int zi = -1; zi <= 1; zi++ )
                {
                    boxIndex = BOX_INDEX(x+xi,gSolidBox.min[Y],z+zi);
                    for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
                    {
                        // The melting option melts away snow built as supports or whatever
                        if ( gBoxData[boxIndex].origType != BLOCK_AIR )
                        {
                            if ( y < minSolid )
                            {
                                minSolid = y;
                            }
                            if ( y > maxSolid )
                            {
                                maxSolid = y;
                            }
                        }
                    }
                }
            }
            if ( minSolid <= maxSolid )
            {
                // add in difference in blocks
                blockSum += maxSolid - minSolid + 1;
            }
        }
    }
    // add in a top and bottom layer of about 0.5 mm - assumes blocks are around 1 mm.
    return (float)blockSum + 0.25f*(gFilledBoxSize[X] * gFilledBoxSize[Z]);
    // Possible addition: can't quite add the 0.5 mm fraction around the surface, but we can add a little.
    // The 0.25 assumes blocks are 1 mm high, which is probably about right for plastic. Also assumes object fills space...
    // + 0.25f*(gFilledBoxSize[X] * gFilledBoxSize[Z]) + 0.5f*(gFilledBoxSize[X] * gFilledBoxSize[Y]) + 0.5f*(gFilledBoxSize[Y] * gFilledBoxSize[Z]);
}

// find model scale, given cost
static void scaleByCost()
{
    float materialBudget,budgetPerBlock;

    // scale by cost
    assert(gOptions->pEFD->radioScaleByCost);

    // white gets tricky because of the discount - won't affect anything else:
    // http://www.shapeways.com/blog/archives/490-Significant-price-reduction-on-dense-models.html

    materialBudget = gOptions->pEFD->costVal - gMtlCostTable[gPhysMtl].costHandling;
    // the dialog should not allow this to happen:
    assert(materialBudget > 0.0f);

    // compute volume for materials that have a machine bed volume cost
    float volumeBlockCount = 0.0f;
    if ( gMtlCostTable[gPhysMtl].costPerMachineCC )
    {
        volumeBlockCount = computeMachineVolume();
        float maxVolume = gFilledBoxSize[X] * gFilledBoxSize[Y] * gFilledBoxSize[Z];
        if ( volumeBlockCount > maxVolume )
        {
            assert(volumeBlockCount <= maxVolume);
            volumeBlockCount = maxVolume;
        }
    }

    // how much money we can spend on each block?
    // Think of it this way: what the cost 
    budgetPerBlock = materialBudget / (((float)gBlockCount * gMtlCostTable[gPhysMtl].costPerCubicCentimeter) + volumeBlockCount * gMtlCostTable[gPhysMtl].costPerMachineCC);

    // so a cubic centimeter costs say 75 cents
    // budgetPerBlock / COST_COLOR_CCM - how much we can spend for each block.
    // Take the cube root of this to get the size in cm,
    // convert to meters to get size in meters
    gModel.scale = (float)pow((double)budgetPerBlock,1.0/3.0) * CM_TO_METERS;

    // fill in statistics:
    // if density > 10%, and the cubic centimeters of material over 20, then discount applies.
    //if ( (gStats.density > mtlCostTable[gPhysMtl].costDiscountDensityLevel) && (pow((double)(gModel.scale*METERS_TO_CM),3.0)*gBlockCount > mtlCostTable[gPhysMtl].costDiscountCCMLevel ) )
    //{
    //        // We wimp out here by simply incrementing the scale by 0.1 mm (which is smaller than the minimum detail) until the cost is reached
    //    while ( gOptions->pEFD->costVal > computeMaterialCost( gPhysMtl, gModel.scale, gBlockCount, gMinorBlockCount ))
    //    {
    //        gModel.scale += 0.1f*MM_TO_METERS;
    //    }
    //}
}

static void hollowBottomOfModel()
{
    int x,y,z,boxIndex;
    int listCount,addPost;
    int listFound=1;
    int survived,dir,neighborCount,neighborIndex;
    IPoint loc;
    // we could actually allocate less, but don't want to risk allocing 0 (or less than 0!)
    int *listToChange=(int *)malloc((gSolidBox.max[X]-gSolidBox.min[X]+1)*(gSolidBox.max[Z]-gSolidBox.min[Z]+1)*sizeof(int));

    int hollowListSize = gBoxSize[X]*gBoxSize[Z]*sizeof(unsigned char);
    unsigned char *hollowDone = (unsigned char *)malloc(hollowListSize);
    memset(hollowDone,0,hollowListSize);

    assert(gOptions->pEFD->hollowThicknessVal[gOptions->pEFD->fileType] > 0.0f);
    gHollowBlockThickness = (int)(ceil(gOptions->pEFD->hollowThicknessVal[gOptions->pEFD->fileType]*MM_TO_METERS/gModel.scale));

    // an arbitrary guess: if the box is long in one dimension or the other, add posts for support
    addPost = ( gSolidBox.max[X] > gSolidBox.min[X] + 8 + 2*gHollowBlockThickness ) || ( gSolidBox.max[Z] > gSolidBox.min[Z] + 8 + 2*gHollowBlockThickness );

    // note that we test to the maximum, as we want the upper limit to be hit and so set the hollowDone for every location.
    for ( y = gSolidBox.min[Y]; (y <= gSolidBox.max[Y]) && listFound; y++ )
    {
        listCount = 0;
        // we know the walls have to be at least gHollowBlockThickness thick, so don't bother checking those.
        for ( x = gSolidBox.min[X]+gHollowBlockThickness; x <= gSolidBox.max[X]-gHollowBlockThickness; x++ )
        {
            for ( z = gSolidBox.min[Z]+gHollowBlockThickness; z <= gSolidBox.max[Z]-gHollowBlockThickness; z++ )
            {
                boxIndex = BOX_INDEX(x,y,z);
                // First check that we can continue to hollow things upward from base, then
                // check 9 above and 8 on sides.
                if ( !hollowDone[x*gBoxSize[Z]+z] )
                {
                    // check if the block is in a corner: if so, don't hollow it, in order to
                    // give some support to the walls being formed. TODO with Shapeways tests: is this necessary? I
                    // like to think it makes for a more solid model.
                    // This test is a little wasteful, it normally fails just 4 times and then the hollowDone
                    // is set for those post positions. However, we need to make sure the posts get their own 0
                    // group in the "else".
                    if ( !addPost || ( (x > gSolidBox.min[X]+gHollowBlockThickness) && (x < gSolidBox.max[X]-gHollowBlockThickness) )
                        || ( (z > gSolidBox.min[Z]+gHollowBlockThickness) && (z < gSolidBox.max[Z]-gHollowBlockThickness) ) )
                    {
                        survived = 0;
                        // brute force the 3x3 above and 3x3 in the middle layer: all solid?
                        if ( gBoxData[boxIndex-1].type == BLOCK_AIR &&    // if block below is air
                            gBoxData[boxIndex].type != BLOCK_AIR &&    // if block is solid
                            gBoxData[boxIndex+1].type != BLOCK_AIR &&   // +Y
                            gBoxData[boxIndex-gBoxSizeYZ].type != BLOCK_AIR &&  // -X
                            gBoxData[boxIndex+gBoxSizeYZ].type != BLOCK_AIR &&  // +X
                            gBoxData[boxIndex-gBoxSize[Y]].type != BLOCK_AIR &&  // -Z
                            gBoxData[boxIndex+gBoxSize[Y]].type != BLOCK_AIR &&  // +Z
                            gBoxData[boxIndex-gBoxSizeYZ-gBoxSize[Y]].type != BLOCK_AIR &&  // -X-Z
                            gBoxData[boxIndex+gBoxSizeYZ-gBoxSize[Y]].type != BLOCK_AIR &&  // +X-Z
                            gBoxData[boxIndex-gBoxSizeYZ+gBoxSize[Y]].type != BLOCK_AIR &&  // -X+Z
                            gBoxData[boxIndex+gBoxSizeYZ+gBoxSize[Y]].type != BLOCK_AIR &&  // +X+Z
                            gBoxData[boxIndex-gBoxSizeYZ+1].type != BLOCK_AIR &&  // -X+Y
                            gBoxData[boxIndex+gBoxSizeYZ+1].type != BLOCK_AIR &&  // +X+Y
                            gBoxData[boxIndex-gBoxSize[Y]+1].type != BLOCK_AIR &&  // -Z+Y
                            gBoxData[boxIndex+gBoxSize[Y]+1].type != BLOCK_AIR &&  // +Z+Y
                            gBoxData[boxIndex-gBoxSizeYZ-gBoxSize[Y]+1].type != BLOCK_AIR &&  // -X-Z+Y
                            gBoxData[boxIndex+gBoxSizeYZ-gBoxSize[Y]+1].type != BLOCK_AIR &&  // +X-Z+Y
                            gBoxData[boxIndex-gBoxSizeYZ+gBoxSize[Y]+1].type != BLOCK_AIR &&  // -X+Z+Y
                            gBoxData[boxIndex+gBoxSizeYZ+gBoxSize[Y]+1].type != BLOCK_AIR )  // +X+Z+Y
                        {
                            survived = 1;
                            // OK, this one can be deleted. Now check extra width, if any
                            if ( gHollowBlockThickness > 1 )
                            {
                                // check neighbors in the 5 directions to see if all solid
                                for ( dir = 0; survived && (dir < 6); dir++ )
                                {
                                    if ( dir != DIRECTION_BLOCK_BOTTOM)
                                    {
                                        Vec3Scalar( loc, =, x,y,z);
                                        neighborCount = gHollowBlockThickness-1;
                                        // get the next neighbor over, which we already checked above
                                        getNeighborUnsafe( dir, loc );
                                        // and get the next neighbor of *it* - note that getNeighbor changes the loc
                                        while ( survived && getNeighbor( dir, loc ) && neighborCount > 0 )
                                        {
                                            neighborIndex = BOX_INDEXV(loc);
                                            // is neighbor not in a group, and the same sort of thing as our seed (solid or not)?
                                            if ( gBoxData[neighborIndex].type == BLOCK_AIR )
                                            {
                                                survived = 0;
                                            }
                                            neighborCount--;
                                        }
                                    }
                                }
                            }
                        }
                        if ( survived )
                        {
                            // area all around is thick enough, so we can delete it
                            listToChange[listCount++] = boxIndex;
                        }
                        else
                        {
                            // We have reached the limits of hollowing upwards, so stop hollowing
                            // hollowDone is now an array showing the maximum height reached, +1
                            hollowDone[x*gBoxSize[Z]+z] = (unsigned char)y;
                        }
                    }
                    else
                    {
                        // corner post hit - mark it as a post so it doesn't get super-hollowed later
                        // do this to only solid objects. This is done until we hit air.
                        // TODO: when we hit air we could continue, not sure that helps...
                        if ( !hollowDone[x*gBoxSize[Z]+z] )
                            if (gBoxData[boxIndex].type > BLOCK_AIR)
                                gBoxData[boxIndex].group = HOLLOW_AIR_GROUP;
                            else
                                // stop making a post if we hit air. This OK? TODO
                                hollowDone[x*gBoxSize[Z]+z] = (unsigned char)y;
                    }
                }
            }
        }
        // delete any found. If none found, we're done.
        listFound = listCount;  // note if a list was found
        if ( listCount )
        {
            while ( listCount > 0 )
            {
                listCount--;
                assert(listToChange[listCount]>=0 && listToChange[listCount]<gBoxSizeXYZ);
                // note at this point we're not messing with populations, since hollow is the very last operation.
                // If this changes, need to decrement and add to populations here, and we'd need to get the new bounds
                // for any groups that lost anything (and gained anything), etc.
                gBoxData[listToChange[listCount]].type = BLOCK_AIR;
                // must track block count now, as it's been computed
                gBlockCount--;
                // special use of group 0 - for hollow
                gBoxData[listToChange[listCount]].group = HOLLOW_AIR_GROUP;
                gStats.blocksHollowed++;
            }
        }
    }
    free(listToChange);

    if ( gOptions->exportFlags & EXPT_SUPER_HOLLOW_BOTTOM )
    {
        // Now do superhollowing:
        // Check all solids next to the hollow areas: can the hollows be expanded outward?
        // We use the hollowDone "heightmap" to generate seeds: if the height of an XZ position is lower or equal to the height
        // of its four neighboring hollowDones then the elements at its height to the maximum of the other 4 heights - 1 should be checked
        // as possible seeds.
        int seedSize = 1000;
        IPoint *seedStack = (IPoint *)malloc(seedSize*sizeof(IPoint));
        int seedCount = 0;

        gStats.blocksSuperHollowed = 0;
        for ( x = gSolidBox.min[X]+gHollowBlockThickness; x <= gSolidBox.max[X]-gHollowBlockThickness; x++ )
        {
            for ( z = gSolidBox.min[Z]+gHollowBlockThickness; z <= gSolidBox.max[Z]-gHollowBlockThickness; z++ )
            {
                // we know we can't extend upwards beyond this height, at least not directly, so see if we can expand sideways.
                int hollowHeight = hollowDone[x*gBoxSize[Z]+z];

                // find if any neighboring column is higher than this column's max height;
                // if so, then items in this column above this hollow height level could be things to hollow out
                int maxNeighborHeight = hollowDone[(x-1)*gBoxSize[Z]+z];
                int nextNeighborHeight = hollowDone[(x+1)*gBoxSize[Z]+z];
                if ( nextNeighborHeight > maxNeighborHeight )
                    maxNeighborHeight = nextNeighborHeight;
                nextNeighborHeight = hollowDone[(x+1)*gBoxSize[Z]+z];
                if ( nextNeighborHeight > maxNeighborHeight )
                    maxNeighborHeight = nextNeighborHeight;
                nextNeighborHeight = hollowDone[(x+1)*gBoxSize[Z]+z];
                if ( nextNeighborHeight > maxNeighborHeight )
                    maxNeighborHeight = nextNeighborHeight;

                // Examine all from hollowHeight+gHollowBlockThickness to max height - 1, if any. The idea
                // here is that we've found that this column goes to a certain height, but its neighbors may
                // have higher hollow heights. In other words, those neighbors' hollowed areas could cause
                // voxels in this column to be hollowed out by percolation (super-hollowing). We start testing
                // from hollowHeight itself (which could not be hollowed - we just tested above) plus the
                // block thickness (moving us above any set of blocks that could not be hollowed, to the first
                // one that actually can be hollowed).
                // Note that hollowHeight+gHollowBlockThickness can definitely be >= max neighbor height, so
                // this loop may not run at all (and in fact normally won't).
                for ( y = hollowHeight+gHollowBlockThickness; y < maxNeighborHeight; y++ )
                {
                    // propagate seed: make neighbors with same type (solid or air) and no group to be this group.
                    hollowSeed( x, y, z, &seedStack,&seedSize,&seedCount);

                    // When this is done, seedStack has a list of seeds that had no ID before and now have one.
                    // Each of these seeds' neighbors needs to be tested.

                    // while seedCount > 0, propagate
                    while ( seedCount > 0 )
                    {
                        // copy test point over, so we don't trample it when seedStack gets increased
                        seedCount--;
                        hollowSeed(seedStack[seedCount][X],seedStack[seedCount][Y],seedStack[seedCount][Z],&seedStack,&seedSize,&seedCount);
                    }
                }
            }
        }
        free(seedStack);
    }

    free(hollowDone);
}

static void hollowSeed( int x, int y, int z, IPoint **pSeedList, int *seedSize, int *seedCount )
{
    // recursively check this seed location and all neighbors for whether this location can be hollowed out
    int boxIndex = BOX_INDEX(x,y,z);

    // first, is it already empty? or marked as part of hollow (as the posts are)?
    if ( gBoxData[boxIndex].type != BLOCK_AIR && gBoxData[boxIndex].group != HOLLOW_AIR_GROUP )
    {
        // OK, it can be tested and could spawn more seeds
        int neighborBoxIndex,dir;
        IPoint loc;

        // first test to see if the 26 neighbors are all either solid, or have group 0 (hollow).
        // We do not want to tunnel through any walls and reach air that's not part of the hollow group.
        int ok = 1;
        for ( loc[X] = x-1; ok && loc[X] <= x+1; loc[X]++ )
        {
            for ( loc[Z] = z-1; ok && loc[Z] <= z+1; loc[Z]++ )
            {
                neighborBoxIndex = BOX_INDEX(loc[X],y-1,loc[Z]);
                for ( loc[Y] = y-1; ok && loc[Y] <= y+1; loc[Y]++, neighborBoxIndex++ )
                {
                    if ( gBoxData[neighborBoxIndex].type == BLOCK_AIR &&
                        gBoxData[neighborBoxIndex].group != HOLLOW_AIR_GROUP )
                    {
                        // outside air found, so can't grow that direction
                        ok = 0;
                    }
                }
            }
        }

        // survived initial test. If desired wall thickness is > 1, check the surrounding solid
        // blocks in the 6 directions.
        // This test is a little "dangerous", in that if the wall thickness is very high, it should
        // really be testing a large block of neighbors to make sure it's large enough. This is just
        // a quick and dirty version. We could instead have the loop above go from -gHollowBlockThickness
        // to +gHollowBlockThickness. This quick and dirty is a little more aggressive, though, cleaning
        // out as much as it can.
        if ( ok && gHollowBlockThickness > 1 )
        {
            for ( dir = 0; ok && (dir < 6); dir++ )
            {
                int neighborCount = gHollowBlockThickness-1;
                Vec3Scalar( loc, =, x,y,z);
                // get the next neighbor over, which we already checked above
                getNeighborUnsafe( dir, loc );
                // and get the next neighbor of *it*. Note that getNeighbor changes the loc
                while ( ok && getNeighbor( dir, loc ) && neighborCount > 0 )
                {
                    neighborBoxIndex = BOX_INDEXV(loc);
                    // is neighbor not in a group, and the same sort of thing as our seed (solid or not)?
                    if ( gBoxData[neighborBoxIndex].type == BLOCK_AIR &&
                        gBoxData[neighborBoxIndex].group != HOLLOW_AIR_GROUP )
                    {
                        ok = 0;
                    }
                    neighborCount--;
                }
            }
        }

        if ( ok )
        {
            // survived! Turn it hollow, and check the six seeds. Recursion, here we come.
            IPoint *seedList;

            // check that there's not enough room for seedStack to grow by 6 points
            if ( *seedSize <= *seedCount + 6 )
            {
                IPoint *seeds;
                *seedSize += 6;
                *seedSize = (int)(*seedSize * 1.4 + 1);
                seeds = (IPoint*)malloc(*seedSize*sizeof(IPoint));
                memcpy( seeds, (*pSeedList), *seedCount*sizeof(IPoint));
                free( (*pSeedList) );
                (*pSeedList) = seeds;
            }

            seedList = *pSeedList;

            gBoxData[boxIndex].type = BLOCK_AIR;
            gBoxData[boxIndex].group = HOLLOW_AIR_GROUP;
            gStats.blocksSuperHollowed++;
            // must track block count now, as it's been computed
            gBlockCount--;

            for ( dir = 0; dir < 6; dir++ )
            {
                int sindex = *seedCount;
                Vec3Scalar( seedList[sindex], =, x+gFaceDirectionVector[dir][X], y+gFaceDirectionVector[dir][Y], z+gFaceDirectionVector[dir][Z] );
                (*seedCount)++;
                // it's possible that we could run out, I think: each seed could add its six neighbors, etc.?
                assert((*seedCount)<gBoxSizeXYZ);
            }
        }
    }
}

static void meltSnow()
{
    int x,y,z,boxIndex;
    for ( x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++ )
    {
        for ( z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++ )
        {
            boxIndex = BOX_INDEX(x,gSolidBox.min[Y],z);
            for ( y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++ )
            {
                // The melting option melts away snow built as supports or whatever
                if ( gBoxData[boxIndex].type == BLOCK_SNOW_BLOCK )
                {
                    // melting time
                    gBoxData[boxIndex].type = BLOCK_AIR;
                    // We don't know if it's true that this is the right air group, but who cares,
                    // it's the last operation before exporting the model itself. Still, give it some
                    // group, just in case...
                    gBoxData[boxIndex].group = SURROUND_AIR_GROUP;
                    gStats.blocksHollowed++;
                }
            }
        }
    }
}

static int generateBlockDataAndStatistics()
{
    int i, boxIndex;
    IPoint loc;
    float pgFaceStart,pgFaceOffset;

    int retCode = MW_NO_ERROR;

    // Undefine and normals are output

    // Minecraft's great, just six normals does it (mostly)
    // Billboards have an additional 4 at this point, rails 8 (top and bottom)
    // Banners and signs give another 8
    // Torches on walls give another 16, but are not listed here, they're added on the fly
    Point normals[] = {{-1,0,0},{0,-1,0},{0,0,-1},{1,0,0},{0,1,0},{0,0,1},
    {-OSQRT2,0,-OSQRT2},    // DIRECTION_LO_X_LO_Z
    {-OSQRT2,0, OSQRT2},    // DIRECTION_LO_X_HI_Z
    { OSQRT2,0,-OSQRT2},    // DIRECTION_HI_X_LO_Z
    { OSQRT2,0, OSQRT2},    // DIRECTION_HI_X_HI_Z

    {-OSQRT2,-OSQRT2,      0},  // DIRECTION_LO_X_LO_Y
    {      0,-OSQRT2,-OSQRT2},  // DIRECTION_LO_Z_LO_Y
    { OSQRT2,-OSQRT2,      0},  // DIRECTION_HI_X_LO_Y
    {      0,-OSQRT2, OSQRT2},  // DIRECTION_HI_Z_LO_Y
    {-OSQRT2, OSQRT2,      0},  // DIRECTION_LO_X_HI_Y
    {      0, OSQRT2,-OSQRT2},  // DIRECTION_LO_Z_HI_Y
    { OSQRT2, OSQRT2,      0},  // DIRECTION_HI_X_HI_Y
    {      0, OSQRT2, OSQRT2},  // DIRECTION_HI_Z_HI_Y

    // need to add standing banner and sign angles, another 8 - these don't have
    // correspondences with any values
    {-OCOS22P5DEG,0,-OSIN22P5DEG},
    {-OCOS22P5DEG,0, OSIN22P5DEG},
    { OCOS22P5DEG,0,-OSIN22P5DEG},
    { OCOS22P5DEG,0, OSIN22P5DEG},

    {-OSIN22P5DEG,0,-OCOS22P5DEG},
    {-OSIN22P5DEG,0, OCOS22P5DEG},
    { OSIN22P5DEG,0,-OCOS22P5DEG},
    { OSIN22P5DEG,0, OCOS22P5DEG},
    };

    // Add the following to this, everyone does it
    if ( gOptions->pEFD->chkCenterModel )
    {
        // Compute center for output. This could be more elaborate, but our goal
        // here is simply to approximately center in X and Z and put min.Y at 0 level

        gModel.center[Y] = (float)gSolidBox.min[Y];
        // Note that we don't perfectly center all the time, but rather try to make blocks
        // align with whole numbers. If you output in meters, you get no fractions, so get
        // a smaller OBJ file.
        gModel.center[X] = (float)floor((float)(gSolidBox.max[X]+gSolidBox.min[X]+1)/2.0f);
        gModel.center[Z] = (float)floor((float)(gSolidBox.max[Z]+gSolidBox.min[Z]+1)/2.0f);
    }
    else
    {
        // keep world coordinates (may get rescaled, of course)
        Vec2Op(gModel.center, =, (float)gWorld2BoxOffset);
    }

    // get the normals into their proper orientations.
    // We need only 6 when only blocks are exported.
    gModel.normalListCount = gExportBillboards ? 26 : 6;
    for ( i = 0; i < gModel.normalListCount; i++ )
    {
        rotateLocation(normals[i]);
        Vec2Op(gModel.normals[i], =, normals[i]);
    }

    pgFaceStart = PG_MAKE_FACES+0.01f;
    // 6%
    UPDATE_PROGRESS(pgFaceStart);
    pgFaceOffset = PG_OUTPUT - PG_MAKE_FACES - 0.01f;   // save 0.01 for sorting

    // At this point all partial blocks have been output, and their type set to BLOCK_AIR. Now output the fully solid blocks.
    // Go through blocks and see which is solid; output these solid blocks.
    for ( loc[X] = gSolidBox.min[X]; loc[X] <= gSolidBox.max[X]; loc[X]++ )
    {
        // update on each row of X
        UPDATE_PROGRESS( pgFaceStart + pgFaceOffset*((float)(loc[X]-gSolidBox.min[X]+1)/(float)(gSolidBox.max[X]-gSolidBox.min[X]+1)));
        for ( loc[Z] = gSolidBox.min[Z]; loc[Z] <= gSolidBox.max[Z]; loc[Z]++ )
        {
            boxIndex = BOX_INDEX(loc[X],gSolidBox.min[Y],loc[Z]);
            for ( loc[Y] = gSolidBox.min[Y]; loc[Y] <= gSolidBox.max[Y]; loc[Y]++, boxIndex++ )
            {
                // if it's not air (everything too small has been turned into air)
                // then output it
                if ( gBoxData[boxIndex].type > BLOCK_AIR ) 
                {
                    // block is solid, may need to output some faces.
                    retCode |= checkAndCreateFaces(boxIndex,loc);
                    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;
                }
            }
        }
    }

    UPDATE_PROGRESS(pgFaceStart + pgFaceOffset);

    // now that we have the scale and world offset, and all vertices are now generated, transform all points to their proper locations
    for ( i = 0; i < gModel.vertexCount; i++ )
    {
        float *pt = (float *)gModel.vertices[i];
        float anchor[3];
        Vec2Op( anchor, =, gModel.vertices[i] );
        pt[X] = (float)(anchor[X] - gModel.center[X])*gModel.scale*gUnitsScale;
        pt[Y] = (float)(anchor[Y] - gModel.center[Y])*gModel.scale*gUnitsScale;
        pt[Z] = (float)(anchor[Z] - gModel.center[Z])*gModel.scale*gUnitsScale;

        // rotate location as needed
        rotateLocation( pt );
    }

    // If we are grouping by material (e.g., STL does not need this), then we need to sort by material
    if ( gOptions->exportFlags & EXPT_GROUP_BY_MATERIAL )
    {
        qsort_s(gModel.faceList,gModel.faceCount,sizeof(FaceRecord*),faceIdCompare,NULL);
    }

    return retCode;
}
static int faceIdCompare( void* context, const void *str1, const void *str2)
{
    FaceRecord *f1;
    FaceRecord *f2;
    f1 = *(FaceRecord**)str1;
    f2 = *(FaceRecord**)str2;
    context;    // make a useless reference to the unused variable, to avoid C4100 warning
    if ( f1->type == f2->type )
    {
        // Not necessary, but...
        // Tie break is face loop starting vertex, so that data is
        // output with some coherence. May help mesh caching and memory access.
        // Also, the data just looks more tidy in the file.
        return ( (f1->faceIndex < f2->faceIndex) ? -1 : ((f1->faceIndex == f2->faceIndex)) ? 0 : 1 );
    }
    else return ( (f1->type < f2->type) ? -1 : 1 );
}


// return 0 if nothing solid in box
// Note that the dimensions are returned in floats, for later use for statistics
static int getDimensionsAndCount( Point dimensions )
{
    IPoint loc;
    int boxIndex;
    IBox bounds;
    int count = 0;
    VecScalar( bounds.min, =,  999999);
    VecScalar( bounds.max, =, -999999);

    // do full grid here, in case supports or other stuff gets added at the end
    //for ( loc[X] = 0; loc[X] < gBoxSize[X]; loc[X]++ )
    //{
    //    for ( loc[Z] = 0; loc[Z] < gBoxSize[Z]; loc[Z]++ )
    //    {
    //        boxIndex = BOX_INDEX(loc[X],0,loc[Z]);
    //        for ( loc[Y] = 0; loc[Y] < gBoxSize[Y]; loc[Y]++, boxIndex++ )

    // search air block, in case something got added around fringe, or some subtraction
    // pulled box in.
    for ( loc[X] = gAirBox.min[X]; loc[X] <= gAirBox.max[X]; loc[X]++ )
    {
        for ( loc[Z] = gAirBox.min[Z]; loc[Z] <= gAirBox.max[Z]; loc[Z]++ )
        {
            boxIndex = BOX_INDEX(loc[X],gAirBox.min[Y],loc[Z]);
            for ( loc[Y] = gAirBox.min[Y]; loc[Y] <= gAirBox.max[Y]; loc[Y]++, boxIndex++ )
            {
                // if it's not air, then it's valid - update bounds
                if ( gBoxData[boxIndex].type > BLOCK_AIR) 
                {
                    // block is solid, may need to output some faces.
                    addBounds( loc, &bounds );
                    count++;
                }
            }
        }
    }

    if ( gExportBillboards )
    {
        // add in billboard/geometry object count and bounds
        addBoundsToBounds( gModel.billboardBounds, &bounds );
    }

    // anything in the box?
    if ( bounds.min[X] > bounds.max[X] )
        return 0;

    // note conversion from int to float here
    Vec3Op( dimensions, =, 1.0f + (float)bounds.max, -, (float)bounds.min);
    return count;
}


static void rotateLocation( Point pt )
{
    float tempf;

    // check if rotation around an axis
    if ( !gOptions->pEFD->radioRotate0 )
    {
        // rotation will occur
        if ( gOptions->pEFD->radioRotate90 )
        {
            tempf = pt[X];
            pt[X] = -pt[Z];
            pt[Z] = tempf;
        }
        else if ( gOptions->pEFD->radioRotate180 )
        {
            pt[X] = -pt[X];
            pt[Z] = -pt[Z];
        }
        else
        {
            tempf = -pt[X];
            pt[X] = pt[Z];
            pt[Z] = tempf;
        }
    }

    // check if we rotate Y axis to be Z axis as up
    if ( gOptions->pEFD->chkMakeZUp[gOptions->pEFD->fileType] )
    {
        // Rotate +Y to +Z
        tempf = pt[Y];
        pt[Y] = -pt[Z];
        pt[Z] = tempf;
    }
}

// check if a solid block is next to something that causes a face to be created
static int checkAndCreateFaces( int boxIndex, IPoint loc )
{
    int faceDirection;
    int neighborType;
    int type = gBoxData[boxIndex].type;
    int computeHeights = 1;
    int isFullBlock = 0;	// to make compiler happy
    float heights[4];
    int heightIndices[4];
    int testPartial = gOptions->pEFD->chkExportAll;
    int retCode = MW_NO_ERROR;

    // only solid blocks should be passed in here.
    assert(type != BLOCK_AIR);

    for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
    {
        int neighborBoxIndex = boxIndex + gFaceOffset[faceDirection];
        neighborType = gBoxData[neighborBoxIndex].type;

        // If neighbor is air, or if we're outputting a model for viewing
        // (not printing) and it is transparent and our object is not transparent,
        // then output a face. This latter condition gives lakes bottoms.
        // TODO: do we care if two transparent objects are touching each other? (Ice & water?)
        // Right now water and ice touching will generate no faces, which I think is fine.
        // so, create a face?
        if ( checkMakeFace( type, neighborType, !gPrint3D, testPartial, faceDirection, neighborBoxIndex ) )
        {
            // Air (or water, or portal) found next to solid block: time to write it out.
            // First write out any vertices that are needed (this may do nothing, if they're
            // already written out by previous faces doing output of the same vertices).

            // check if we're rendering (always), or 3D printing & lesser, and exporting a fluid block.
            if ( ( !gPrint3D || testPartial ) && 
                (type>=BLOCK_WATER) && (type<=BLOCK_STATIONARY_LAVA) &&
                (faceDirection != DIRECTION_BLOCK_BOTTOM ) )
            {
                if ( computeHeights )
                {
                    computeHeights = 0;
                    isFullBlock = cornerHeights( type, boxIndex, heights );
                    heightIndices[0] = heightIndices[1] = heightIndices[2] = heightIndices[3] = NO_INDEX_SET;
                }
                // are all heights 1.0, so that this is a full block?
                if ( isFullBlock )
                {
                    // full block, so save vertices as-is
                    goto SaveFullBlock;
                }
                else
                {
                    // save partial block for water and lava
                    retCode |= saveSpecialVertices( boxIndex, faceDirection, loc, heights, heightIndices );
                    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                    // note that there's a "transform", which really is just that the
                    // top of the block is different.
                    gUsingTransform = 1;
                    saveFaceLoop( boxIndex, faceDirection, heights, heightIndices );
                    gUsingTransform = 0;
                }
            }
            else
            {
SaveFullBlock:
                // normal face save: not fluid, etc.
                retCode |= saveVertices( boxIndex, faceDirection, loc );
                if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                saveFaceLoop( boxIndex, faceDirection, NULL, NULL );
            }
        }
    }
    return retCode;
}

// Called for lava and water faces, and for 
// Assumes the following: billboards and lesser stuff has been output and their blocks made into air -
//   this then means that if any neighbor is found, it must be a full block and so will cover the face.
// Check if we should make a face: return 1 if face should be output
// type is type of face of currect voxel,
// neighborType is neighboring type,
// faceDirection is which way things connect
// boxIndex and neighborBoxIndex is real locations, in case more info is needed
static int checkMakeFace( int type, int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex )
{
    // Our whole goal here is to determine if this face is visible. For most solid
    // blocks, if the neighbor covers the face and is not semitransparent, then the block
    // face is hidden and can be discarded instead of output.

    // if neighboring face does not cover block type fully, or if individual (i.e., all) blocks are being output as separate entities, then output the face.
    if ( !neighborMayCoverFace( neighborType, view3D, testPartial, faceDirection, neighborBoxIndex) || (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK) )
    {
        // Face is visible for sure, it is not fully covered by the neighbor, so return.
        // A return value of 0 means our block is exposed to air and so is fully visible or neighbor is not a full block and doesn't cover face,
        // or exporting all blocks, so display. Note that just because a neighbor does cover a full block we still do more tests below.
        return 1;
    }

    // At this point we know that the neighbor *could* cover the face - it's been found to fully cover the face geometrically, now we have to test its properties.
    // Lesser blocks really do fully cover the face, that's known at this point, it's just the full blocks that are questionable

    // Do additional testing for the case where neighbors are 'full size' but may be transparent or fluids
    if (neighborType > BLOCK_AIR)
    {
        // neighbor is indeed a full block so needs further testing.
        // special cases for viewing (rendering), having to do with semitransparency or cutouts
        if ( view3D )
        {
            // check if the neighbor is semitransparent (glass, water, etc.)
            if (gBlockDefinitions[neighborType].alpha < 1.0f)
            {
                // and this object is a different
                // type - including ice on water, glass next to water, etc. - then output face
                if ( neighborType != type )
                {
                    // special check: if water next to stationary water, or (rare, user-defined semitransparent) lava next to stationary lava, that doesn't
                    // make a face
                    if ( ( type < BLOCK_WATER ) || ( type > BLOCK_STATIONARY_LAVA ) || !sameFluid(type, neighborType) )
                    {
                        // semitransparent neighbor of a different type reveals face.
                        return 1;
                    }
                }
            }
            // look for blocks with cutouts next to them - only for rendering
            if ((gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) &&
                (gBlockDefinitions[neighborType].flags & BLF_CUTOUTS) )
            {
                // Special case: if leaves are neighbors and "make leaves solid" is on, then don't output face.
                if ( gOptions->pEFD->chkLeavesSolid && (gBlockDefinitions[neighborType].flags & BLF_LEAF_PART) )
                {
                    // "solid" leaves to minimize output, don't output adjoining leaves.
                    return 0;
                }
                //(neighborType == BLOCK_LEAVES) ||
                //(neighborType == BLOCK_SUGAR_CANE) ||
                //(neighborType == BLOCK_CROPS) ||
                //(neighborType == BLOCK_NETHER_WART) ||
                //(neighborType == BLOCK_MONSTER_SPAWNER)||
                //(neighborType == BLOCK_COBWEB) ||
                //((neighborType == BLOCK_IRON_BARS) && (type != BLOCK_IRON_BARS)) ||
                //((neighborType == BLOCK_VINES) && (type != BLOCK_VINES) ) )

                // Exporting textures, and neighbor has cutouts. If the block is glass and the neighbor is glass, or both are vines, or both are iron bars, then
                // we know the objects are "joined" and don't need a face between them. Otherwise, return 1, that the face should be output.
                if ( !((neighborType == BLOCK_GLASS || neighborType == BLOCK_STAINED_GLASS) && (type == BLOCK_GLASS || type == BLOCK_STAINED_GLASS_PANE)) &&
                    !((neighborType == BLOCK_GLASS_PANE || neighborType == BLOCK_STAINED_GLASS_PANE) && (type == BLOCK_GLASS_PANE || type == BLOCK_STAINED_GLASS_PANE)) &&
                    !((neighborType == BLOCK_VINES) && (type == BLOCK_VINES)) &&
                    !((neighborType == BLOCK_IRON_BARS) && (type == BLOCK_IRON_BARS)) )
                {
                    // anything neighboring a leaf block should be created, since leaves can be seen through. This does
                    // include other leaf blocks. Do similar for iron bars and vines, but neighbors that are also iron bars
                    // or vines don't generate faces in between (since we're faking bars & vines by putting them on blocks).
                    // TODO if vines actually border some solid surface, vs. hanging down, they should get flatsided:
                    // add as a composite, like ladder pretty much.

                    // neighbor does not cover face, so output it.
                    return 1;
                }
                else
                {
                    // don't output face: adjoining glass or panes will attach to one another; same with vines and bars.
                    return 0;
                }
            }
        }
    }

    // Checks for when the neighbor or the block itself is lava or water, when outputting fluid levels.
    // Do test if we're rendering, or if all "lesser" blocks are output for printing.
    // Under these circumstances, for water and lava, a few special tests must be done.
    if ( view3D || testPartial )
    {
        // Check fluids.
        // Is our block a fluid?
        if ( type >= BLOCK_WATER && type <= BLOCK_STATIONARY_LAVA )
        {
            // Check if rendering
            if ( view3D )
            {
                // At this point we know that the face is "covered" by the neighbor in some way.
                // Side and bottom block faces should not be output. All that's left is the top.
                // For rendering, if we're not outputting full blocks and the top of the fluid is
                // a partial thing and doesn't fill the block, then definitely output it.
                if ( testPartial && (faceDirection == DIRECTION_BLOCK_TOP) && !sameFluid(type, neighborType) )
                {
                    // output the top face, as the fluid block doesn't fill its volume.
                    return 1;
                }
            }
            else
            {
                assert( testPartial );
                // we're 3D printing and we're outputting partial, so we want to output all liquid faces
                // so that the fluid volume is watertight (as it were...). In this case we simply need to
                // test neighbors: if it isn't the same fluid, then the face must be output.
                if ( !sameFluid(type, neighborType) )
                {
                    return 1;
                }
            }
        }
        // Faces that are left to test at this point: full block faces, and partial faces for rendered fluids.
        // These are assumed to be fully hidden by the neighbor *unless* the neighbor is lava or water, in which case
        // additional testing is needed.
        if ( neighborType >= BLOCK_WATER && neighborType <= BLOCK_STATIONARY_LAVA )
        {
            // If the neighbor is water/lava, and this block is *not* water/lava, and this block face is not a TOP (which is
            // always covered), and partial rendering is happening, and the neighbor block is not full, then the neighbor
            // is not guaranteed to cover this block (the water/lava level might not be high enough, or it's a top), so output it.
            if ( !sameFluid(neighborType, type) )
            {
                // neighbor is e.g. lava, this block is not, and the lava block is not full (though lava will always cover the top)
                if ( ( faceDirection != DIRECTION_BLOCK_TOP) && testPartial && !isFluidBlockFull( neighborType, neighborBoxIndex ))
                {
                    return 1;
                }
            }
        }
    }

    // don't make the face, it appears to be fully covered by its neighbor
    return 0;
}

// For testing full blocks (along with water and lava)
// Check if the neighbor is a full block, or if the partial face would cover this entire full block face.
static int neighborMayCoverFace( int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex )
{
    // If the neighbor type is defined at this point (not a partial block, which are set to zero at this point),
    // then it WILL cover the face, barring transparency or it being lava or water.
    if ( neighborType > BLOCK_AIR )
    {
        // neighbor is a full block of some sort.
        // note that this is only a quick check - if the neighbor does cover the face, we may still decide to output
        // the face because the neighbor is water, semitransparent, etc.
        return 1;
    }
    // else, covered by air or a partial block;
    // see if partial objects are being output. If not, we're done here with geometric coverage, as it's only blocks or air.
    else if ( !testPartial )
    {
        // outputting only blocks, there are no partials, so the face is definitely visible, since it's covered by air
        return 0;
    }
    else
    {
        // neighbor is a partial block, return 1 if it fully covers the face
        return lesserBlockCoversWholeFace( faceDirection, neighborBoxIndex, view3D );
    }
}

// check if lesser block neighboring this face fully covers (0-16) this face's contents
static int lesserBlockCoversWholeFace( int faceDirection, int neighborBoxIndex, int view3D )
{
    // we have partial blocks possible. Check if neighbor's original type exists at all
    int origType = gBoxData[neighborBoxIndex].origType;
    // not air?
    if ( origType > BLOCK_AIR )
    {
        int neighborDataVal = gBoxData[neighborBoxIndex].data;
        // a minor block exists, so check its coverage given the face direction
        switch ( origType )
        {
        case BLOCK_OAK_WOOD_STAIRS:
        case BLOCK_COBBLESTONE_STAIRS:
        case BLOCK_BRICK_STAIRS:
        case BLOCK_STONE_BRICK_STAIRS:
        case BLOCK_NETHER_BRICK_STAIRS:
        case BLOCK_SANDSTONE_STAIRS:
        case BLOCK_SPRUCE_WOOD_STAIRS:
        case BLOCK_BIRCH_WOOD_STAIRS:
        case BLOCK_JUNGLE_WOOD_STAIRS:
        case BLOCK_ACACIA_WOOD_STAIRS:
        case BLOCK_DARK_OAK_WOOD_STAIRS:
        case BLOCK_QUARTZ_STAIRS:
        case BLOCK_RED_SANDSTONE_STAIRS:
            switch (neighborDataVal & 0x3)
            {
            default:    // make compiler happy
            case 0: // ascending east
                if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X)
                    return 1;
                break;
            case 1: // ascending west
                if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
                    return 1;
                break;
            case 2: // ascending south
                if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z)
                    return 1;
                break;
            case 3: // ascending north
                if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z)
                    return 1;
                break;
            }
            // The 0x4 bit is about whether the bottom of the stairs is in the top half or bottom half (used to always be bottom half).
            // See http://www.minecraftwiki.net/wiki/Block_ids#Stairs
            if ( neighborDataVal & 0x4 )
            {
                // upper slab
                return (faceDirection == DIRECTION_BLOCK_BOTTOM);
            }
            else
            {
                // lower slab
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }

        case BLOCK_STONE_SLAB:
        case BLOCK_WOODEN_SLAB:
        case BLOCK_RED_SANDSTONE_SLAB:
            // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
            // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
            if ( neighborDataVal & 0x8 )
            {
                // upper slab
                return (faceDirection == DIRECTION_BLOCK_BOTTOM);
            }
            else
            {
                // lower slab
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }

        case BLOCK_BED:
            // top
            if ( !view3D )
            {
                // only the print version actually covers the top of the neighboring block
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }
            break;

        case BLOCK_CARPET:
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_END_PORTAL_FRAME:
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_FARMLAND:
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_TRAPDOOR:
        case BLOCK_IRON_TRAPDOOR:
            if ( !view3D )
            {
                // rotate as needed
                if (neighborDataVal & 0x4 )
                {
                    switch (neighborDataVal & 0x3)
                    {
                    default:    // make compiler happy
                    case 0: // south
                        return (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z);
                    case 1: // north
                        return (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z);
                    case 2: // east
                        return (faceDirection == DIRECTION_BLOCK_SIDE_HI_X);
                    case 3: // west
                        return (faceDirection == DIRECTION_BLOCK_SIDE_LO_X);
                    }
                }
                else
                {
                    if ( neighborDataVal & 0x8 )
                    {
                        // trapdoor is up, have it block below.
                        return (faceDirection == DIRECTION_BLOCK_BOTTOM);
                    }
                    else
                    {
                        // trapdoor is down, have it block above.
                        return (faceDirection == DIRECTION_BLOCK_TOP);
                    }
                }
            }
            break;

        case BLOCK_SNOW:
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_CAULDRON:
            if ( !view3D )
            {
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }
            break;

        case BLOCK_REDSTONE_REPEATER_OFF:
        case BLOCK_REDSTONE_REPEATER_ON:
        case BLOCK_REDSTONE_COMPARATOR_INACTIVE:
        case BLOCK_REDSTONE_COMPARATOR_ACTIVE:
        case BLOCK_DAYLIGHT_SENSOR:
        case BLOCK_INVERTED_DAYLIGHT_SENSOR:
            // blocks top
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_HOPPER:
            // blocks bottom
            return (faceDirection == DIRECTION_BLOCK_BOTTOM);

        case BLOCK_RAIL:
        case BLOCK_POWERED_RAIL:
        case BLOCK_DETECTOR_RAIL:
        case BLOCK_ACTIVATOR_RAIL:
            if ( !view3D )
            {
                int modDataVal = neighborDataVal;
                // for printing, angled pieces get triangle blocks.
                // first check dataVal to see if it's a triangle, and remove top bit if so.
                switch ( origType )
                {
                case BLOCK_POWERED_RAIL:
                case BLOCK_DETECTOR_RAIL:
                case BLOCK_ACTIVATOR_RAIL:
                    // if not a normal rail, there are no curve bits, so mask off upper bit, which is
                    // whether the rail is powered or not.
                    modDataVal &= 0x7;
                    break;
                }
                // do for all rails
                switch ( modDataVal )
                {
                case 2:
                case 3:
                case 4:
                case 5:
                    // sloping, so continue
                    break;
                default:
                    // it's a flat piece, return that it doesn't cover
                    return 0;
                }

                // it's sloping - covers top
                if (faceDirection == DIRECTION_BLOCK_TOP)
                    return 1;

                // brute force the four cases: always draw bottom of block as the thing, use top of block for decal,
                // use sides for triangles. Really, we'll just use the top for everything for now (TODO), as it's kind of
                // a bogus object anyway (no right answer).
                switch ( modDataVal )
                {
                case 2: // ascending east
                    if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X)
                        return 1;
                    break;
                case 3: // ascending west
                    if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
                        return 1;
                    break;
                case 4: // ascending north
                    if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z)
                        return 1;
                    break;
                case 5: // ascending south
                    if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z)
                        return 1;
                    break;
                default:
                    // it's a flat, so flatten
                    return 0;
                }
            }
            break;

        default:
            // not in list, so won't cover anything
            break;
        }
    }

    // no cover found, so note that the face is exposed and not covered
    return 0;
}

static int isFluidBlockFull( int type, int boxIndex )
{
    float heights[4];
    return cornerHeights( type, boxIndex, heights );
}

// find heights at the four corners, x lo/z lo, x lo/z hi, etc.
static int cornerHeights( int type, int boxIndex, float heights[4] )
{
    // if block above is same fluid, all heights are 1.0 - quick out.
    if ( sameFluid(type,gBoxData[boxIndex+1].type) )
    {
        return 1;
    }
    else
    {
        // OK, compute heights.
        int i;
        int dataHeight = gBoxData[boxIndex].data;
        if ( dataHeight >= 8 )
        {
            dataHeight = 0;
        }
        for ( i = 0; i < 4; i++ )
        {
            heights[i] = computeUpperCornerHeight(type, boxIndex, i>>1, i%2);
        }

        return ( ( heights[0] >= 1.0f ) && ( heights[1] >= 1.0f ) && ( heights[2] >= 1.0f ) && ( heights[3] >= 1.0f ) );
    }
}

static float computeUpperCornerHeight( int type, int boxIndex, int x, int z )
{
    // if any location above this corner is same fluid, height is 1.0
    int i;
    int neighbor[4];
    float heightSum = 0.0f;
    int weight = 0;

    IPoint loc;
    boxIndexToLoc(loc, boxIndex);

    for ( i = 0; i < 4; i++ )
    {
        // Because we can now look at the corners of "border" blocks, we might access a block
        // off the edge of the border. Don't allow that! Clamp, instead. Doesn't matter along the
        // borders if the "on border" neighbor height is wrong.
        int offx = x-1 + (i >> 1);
        int x = clamp( loc[X]+offx, gAirBox.min[X], gAirBox.max[X] );
        int offz = z-1 + (i%2);
        int z = clamp( loc[Z]+offz, gAirBox.min[Z], gAirBox.max[Z] );
        neighbor[i] = BOX_INDEX(x, loc[Y], z);
        // walk through neighbor above this corner
        if ( sameFluid(type, gBoxData[neighbor[i] + 1].type) )
            return 1.0f;
    }

    // look at neighbors and blend them in.
    for ( i = 0; i < 4; i++ )
    {
        // is neighbor same fluid?
        int neighborType = gBoxData[neighbor[i]].type;
        if ( sameFluid(type, neighborType) )
        {	
            // matches, so get neighbor's stored height
            int neighborDataVal = gBoxData[neighbor[i]].data;

            // if height is "full", add it times 10
            if (neighborDataVal >= 8 || neighborDataVal == 0)
            {
                // full height: by adding it in 10 times, you get a more rounded look.
                heightSum += getFluidHeightPercent(neighborDataVal) * 10.0f;
                // i is normalizer. Basically, if you get here, this value is 10x more important in average
                weight += 10;
            }

            // (par0 + 1) / 9F is fluid height percent formula.
            // so 0 means 1/9, 7 means 8/9 - 0 is the highest level, 7 is the lowest!

            // always just add it in, whatever height the water is
            heightSum += getFluidHeightPercent(neighborDataVal);
            weight++;
        }
        // if neighbor is not considered solid, add one more
        else if ( (gBoxData[neighbor[i]].origType == BLOCK_AIR) || (gBlockDefinitions[gBoxData[neighbor[i]].origType].flags & BLF_DNE_FLUID) )
        {
            heightSum += 1.0f;
            weight++;
        }
    }

    if ( weight == 0 )
    {
        // in theory should never reach here...
        assert(weight);
        return 1.0f;
    }

    // now get the weighted average of the height for this corner
    return 1.0F - heightSum / (float)weight;
}

// name is misleading. More like "depth", as the value returned is essentially how far to move *down* from the corner.
// 0 means full height water, 1/9th returned; 7 means shallow as possible, return 8/9ths
static float getFluidHeightPercent( int dataVal )
{
    if (dataVal >= 8)
    {
        dataVal = 0;
    }

    return (float)(dataVal + 1) / 9.0f;
}

// note: fluidType must be known to be either lava or water type
static int sameFluid( int fluidType, int type )
{
    if ( (fluidType == BLOCK_WATER) || (fluidType == BLOCK_STATIONARY_WATER) )
    {
        return (type == BLOCK_WATER) || (type == BLOCK_STATIONARY_WATER);
    }
    else
    {
        assert( (fluidType == BLOCK_LAVA) || (fluidType == BLOCK_STATIONARY_LAVA) );
        return (type == BLOCK_LAVA) || (type == BLOCK_STATIONARY_LAVA);
    }
}

// check if each face vertex has an index;
// if it doesn't, give it one and save out the vertex location itself
static int saveSpecialVertices( int boxIndex, int faceDirection, IPoint loc, float heights[4], int heightIndices[4] )
{
    int vertexIndex;
    int i;
    IPoint offset;
    float *pt;
    int retCode = MW_NO_ERROR;

    // four vertices to output, check that they exist
    for ( i = 0; i < 4; i++ )
    {
        Vec2Op( offset, =, gFaceToVertexOffset[faceDirection][i]);
        // gFaceToVertexOffset[6][4][3] gives the X,Y,Z offsets to the
        // vertex to be written for this box
        vertexIndex = boxIndex +
            offset[X] * gBoxSizeYZ +
            offset[Y] +
            offset[Z] * gBoxSize[Y];

        // just to feel super-safe, check we're OK - should not be needed...
        if ( vertexIndex < 0 || vertexIndex > gBoxSizeXYZ )
        {
            assert(0);
            return retCode|MW_INTERNAL_ERROR;
        }

        if ( offset[Y] == 1 )
        {
            // an upper vertex
            int heightLoc = 2*offset[X] + offset[Z];
            if ( heights[heightLoc] >= 1.0f )
            {
                goto UseGridLoc;
            }
            else if ( heightIndices[heightLoc] == NO_INDEX_SET )
            {
                // save the vertex for this location.
                // need to give an index and write out vertex location
                retCode |= checkVertexListSize();
                if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                heightIndices[heightLoc] = gModel.vertexCount;
                pt = (float *)gModel.vertices[gModel.vertexCount];

                pt[X] = (float)(loc[X] + offset[X]);
                pt[Y] = (float)loc[Y] + heights[heightLoc];
                pt[Z] = (float)(loc[Z] + offset[Z]);

                gModel.vertexCount++;
                assert( gModel.vertexCount <= gModel.vertexListSize );
            }
        }
        else
        {
UseGridLoc:
            if ( gModel.vertexIndices[vertexIndex] == NO_INDEX_SET )
            {
                // need to give an index and write out vertex location
                retCode |= checkVertexListSize();
                if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                gModel.vertexIndices[vertexIndex] = gModel.vertexCount;
                pt = (float *)gModel.vertices[gModel.vertexCount];

                // for now, we use exactly the same coordinates as Minecraft does.
                //xOut = (float)(1-gWorld2BoxOffset[X] + xloc + xoff);
                //yOut = (float)(1-gWorld2BoxOffset[Y] + yloc + yoff);
                //zOut = (float)(1-gWorld2BoxOffset[Z] + zloc + zoff);
                // centered on origin, good for Blender import. I put Y==0, X & Z centered
                pt[X] = (float)(loc[X] + offset[X]);
                pt[Y] = (float)(loc[Y] + offset[Y]);
                pt[Z] = (float)(loc[Z] + offset[Z]);

                gModel.vertexCount++;
                assert( gModel.vertexCount <= gModel.vertexListSize );
            }
        }
    }
    return retCode;
}

// check if each face vertex has an index;
// if it doesn't, give it one and save out the vertex location itself
static int saveVertices( int boxIndex, int faceDirection, IPoint loc )
{
    int vertexIndex;
    int i;
    IPoint offset;
    float *pt;
    int retCode = MW_NO_ERROR;

    // four vertices to output, check that they exist
    for ( i = 0; i < 4; i++ )
    {
        Vec2Op( offset, =, gFaceToVertexOffset[faceDirection][i]);
        // gFaceToVertexOffset[6][4][3] gives the X,Y,Z offsets to the
        // vertex to be written for this box
        vertexIndex = boxIndex +
            offset[X] * gBoxSizeYZ +
            offset[Y] +
            offset[Z] * gBoxSize[Y];

        // just to feel super-safe, check we're OK - should not be needed...
        if ( vertexIndex < 0 || vertexIndex > gBoxSizeXYZ )
        {
            assert(0);
            return retCode|MW_INTERNAL_ERROR;
        }

        if ( gModel.vertexIndices[vertexIndex] == NO_INDEX_SET )
        {
            // need to give an index and write out vertex location
            retCode |= checkVertexListSize();
            if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

            gModel.vertexIndices[vertexIndex] = gModel.vertexCount;
            pt = (float *)gModel.vertices[gModel.vertexCount];

            // for now, we use exactly the same coordinates as Minecraft does.
            //xOut = (float)(1-gWorld2BoxOffset[X] + xloc + xoff);
            //yOut = (float)(1-gWorld2BoxOffset[Y] + yloc + yoff);
            //zOut = (float)(1-gWorld2BoxOffset[Z] + zloc + zoff);
            // centered on origin, good for Blender import. I put Y==0, X & Z centered
            pt[X] = (float)(loc[X] + offset[X]);
            pt[Y] = (float)(loc[Y] + offset[Y]);
            pt[Z] = (float)(loc[Z] + offset[Z]);

            gModel.vertexCount++;
            assert( gModel.vertexCount <= gModel.vertexListSize );
        }
    }
    return retCode;
}

static int saveFaceLoop( int boxIndex, int faceDirection, float heights[4], int heightIndices[4] )
{
    int i;
    FaceRecord *face;
    int dataVal = 0;
    unsigned char originalType = gBoxData[boxIndex].type;
    int computedSpecialUVs = 0;
    int specialUVindices[4];
    int retCode = MW_NO_ERROR;

    face = allocFaceRecordFromPool();

    // if  we sort, we want to keep faces in the order generated, which is
    // generally cache-coherent (and also just easier to view in the file)
    face->faceIndex = firstFaceModifier( faceDirection == 0, gModel.faceCount );

    // always the same normal, which directly corresponds to the normals[6] array in gModel
    face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : faceDirection;

    // get four face indices for the four corners
    for ( i = 0; i < 4; i++ )
    {
        IPoint offset;
        int vertexIndex;

        Vec2Op( offset, =, gFaceToVertexOffset[faceDirection][i]);

        // heights in use, and Y value is top of fluid
        if ( heights && offset[Y] == 1 )
        {
            // if height is not equal to 1 at this point, use its index
            int heightLoc = 2*offset[X] + offset[Z];
            if ( heights[heightLoc] >= 1.0f )
            {
                goto UseGridLoc;
            }
            else
            {
                // use the vertex added in for this location
                assert(heightIndices[heightLoc] != NO_INDEX_SET);
                face->vertexIndex[i] = heightIndices[heightLoc];

                // Since we're saving a special location, we also need a special UV index
                // to go along with it and use later.
                // Check the direction - top and bottom don't need these, sides do.
                if ( gExportTexture && !computedSpecialUVs && (faceDirection != DIRECTION_BLOCK_BOTTOM) && (faceDirection != DIRECTION_BLOCK_TOP) )
                {
                    int j;
                    computedSpecialUVs = 1;

                    // Add the new UV here, and save its index in an array that is then used
                    // to replace the regular UV index array location.
                    for ( j = 0; j < 4; j++ )
                    {
                        int type, swatchLoc;
                        float u = ((j == 1) || (j == 2)) ? 1.0f : 0.0f;
                        float v;
                        if ( (j == 2) || (j == 3) )
                        {
                            switch ( faceDirection )
                            {
                            case DIRECTION_BLOCK_SIDE_LO_X:
                                v = ( u == 0.0f ) ? heights[0] : heights[1];
                                break;
                            case DIRECTION_BLOCK_SIDE_HI_X:
                                v = ( u == 0.0f ) ? heights[3] : heights[2];
                                break;
                            case DIRECTION_BLOCK_SIDE_LO_Z:
                                v = ( u == 0.0f ) ? heights[2] : heights[0];
                                break;
                            case DIRECTION_BLOCK_SIDE_HI_Z:
                                v = ( u == 0.0f ) ? heights[1] : heights[3];
                                break;
                            default:
                                v = 0.0f;
                                assert(0);
                            }
                        }
                        else
                        {
                            // bottom of fluid is always 0.0
                            v = 0.0f;
                        }

                        type = gBoxData[boxIndex].type;
                        if ( (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES) || 
                            !( gBlockDefinitions[type].flags & BLF_IMAGE_TEXTURE) )
                        {
                            // use a solid color
                            swatchLoc = type;
                        }
                        else
                        {
                            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
                        }
                        specialUVindices[j] = saveTextureUV( swatchLoc, type, u, v );
                    }
                }
            }
        }
        else
        {
UseGridLoc:
            vertexIndex = boxIndex +
                offset[X] * gBoxSizeYZ +
                offset[Y] +
                offset[Z] * gBoxSize[Y];

            face->vertexIndex[i] = gModel.vertexIndices[vertexIndex];
        }
    }

    if (gOptions->exportFlags & (EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE))
    {
        // for debugging: instead of outputting material, output group ID
        // as the material
        if (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS)
        {
            face->type = getMaterialUsingGroup(gBoxData[boxIndex].group);
        }
        else
        {
            // if we're doing FLATTOP compression, the topId for the block will
            // have been set to what is above the block before now (in filter).
            // If the value is not 0 (air), use that material instead
            int special = 0;
            if ( gBoxData[boxIndex].flatFlags )
            {
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_TOP:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_ABOVE )
                    {
                        face->type = gBoxData[boxIndex+1].origType;
                        dataVal = gBoxData[boxIndex+1].data;    // this should still be intact, even if neighbor block is cleared to air
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_BOTTOM:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_BELOW )
                    {
                        face->type = gBoxData[boxIndex-1].origType;
                        dataVal = gBoxData[boxIndex-1].data;    // this should still be intact, even if neighbor block is cleared to air
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_X )
                    {
                        face->type = gBoxData[boxIndex-gBoxSizeYZ].origType;
                        dataVal = gBoxData[boxIndex-gBoxSizeYZ].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_X )
                    {
                        face->type = gBoxData[boxIndex+gBoxSizeYZ].origType;
                        dataVal = gBoxData[boxIndex+gBoxSizeYZ].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_Z )
                    {
                        face->type = gBoxData[boxIndex-gBoxSize[Y]].origType;
                        dataVal = gBoxData[boxIndex-gBoxSize[Y]].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_Z )
                    {
                        face->type = gBoxData[boxIndex+gBoxSize[Y]].origType;
                        dataVal = gBoxData[boxIndex+gBoxSize[Y]].data;
                        special = 1;
                    }
                    break;
                default:
                    // only direction left is down, and nothing gets merged with those faces
                    break;
                }
            }
            if ( !special )
            {
                face->type = originalType;
                dataVal = gBoxData[boxIndex].data;
            }
            else
            {
                // A flattening has happened.
                // Test just in case something's wedged
                if ( face->type == BLOCK_AIR )
                {
                    assert(0);
                    face->type = originalType;
                    return retCode|MW_INTERNAL_ERROR;
                }
            }
        }

        assert(face->type);
    }
    // else no material, so type is not needed

    if ( gExportTexture )
    {
        // I guess we really don't need the swatch location returned; it's
        // main effect is to set the proper indices in the texture map itself
        // and note that the swatch is being used
        (int)getSwatch( face->type, dataVal, faceDirection, boxIndex, face->uvIndex );

        if ( computedSpecialUVs )
        {
            for ( i = 0; i < 4; i++ )
            {
                face->uvIndex[i] = specialUVindices[i];
            }
        }

        // if we're exporting the full texture, then a composite was used:
        // we might then actually want to use the original type
        //if ( Options.exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
        //{
        //    face->type = originalType;
        //}
    }

    retCode |= checkFaceListSize();
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    gModel.faceList[gModel.faceCount++] = face;
    // make sure we're not running off the edge, out of memory.
    // We don't need this memory when not writing out materials, as we instantly write out the faces

    return retCode;
}


static int getMaterialUsingGroup( int groupID )
{
    // material 11 is the second lava color swatch. Counting up from here is a colorful
    // stretch of different materials.
    int type = (11 + groupID) % NUM_BLOCKS_MAP;
    return type;
}

static int retrieveWoolSwatch( int dataVal )
{
    int swatchLoc;
    // use dataVal to retrieve location. These are scattered all over.
    switch (dataVal )
    {
    default:
        assert(0);
    case 0:
        swatchLoc = SWATCH_INDEX( 0, 4 );
        break;
    case 1:
        swatchLoc = SWATCH_INDEX( 2, 13 );
        break;
    case 2:
        swatchLoc = SWATCH_INDEX( 2, 12 );
        break;
    case 3:
        swatchLoc = SWATCH_INDEX( 2, 11 );
        break;
    case 4:
        swatchLoc = SWATCH_INDEX( 2, 10 );
        break;
    case 5:
        swatchLoc = SWATCH_INDEX( 2, 9 );
        break;
    case 6:
        swatchLoc = SWATCH_INDEX( 2, 8 );
        break;
    case 7:
        swatchLoc = SWATCH_INDEX( 2, 7 );
        break;
    case 8:
        swatchLoc = SWATCH_INDEX( 1, 14 );
        break;
    case 9:
        swatchLoc = SWATCH_INDEX( 1, 13 );
        break;
    case 10:
        swatchLoc = SWATCH_INDEX( 1, 12 );
        break;
    case 11:
        swatchLoc = SWATCH_INDEX( 1, 11 );
        break;
    case 12:
        swatchLoc = SWATCH_INDEX( 1, 10 );
        break;
    case 13:
        swatchLoc = SWATCH_INDEX( 1, 9 );
        break;
    case 14:
        swatchLoc = SWATCH_INDEX( 1, 8 );
        break;
    case 15:
        swatchLoc = SWATCH_INDEX( 1, 7 );
        break;
    }
    return swatchLoc;
}

#define SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, sx,sy, bx,by ) \
    if ( (faceDirection) == DIRECTION_BLOCK_BOTTOM )          \
    swatchLoc = SWATCH_XY_TO_INDEX( (bx), (by) );         \
else if ( (faceDirection) != DIRECTION_BLOCK_TOP )        \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );

#define SWATCH_SWITCH_SIDE( faceDirection, sx,sy )      \
    if ( ((faceDirection) != DIRECTION_BLOCK_BOTTOM) && ((faceDirection) != DIRECTION_BLOCK_TOP))      \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );

#define SWATCH_SWITCH_SIDE_VERTICAL( faceDirection, sx,sy, vx,vy ) \
    if ( (faceDirection) == DIRECTION_BLOCK_TOP || (faceDirection) == DIRECTION_BLOCK_BOTTOM )          \
    swatchLoc = SWATCH_XY_TO_INDEX( (vx), (vy) );             \
else													  \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );

// note that, for flattops and sides, the dataVal passed in is indeed the data value of the neighboring flattop being merged
static int getSwatch( int type, int dataVal, int faceDirection, int backgroundIndex, int uvIndices[4] )
{
    int swatchLoc;
    int localIndices[4] = { 0, 1, 2, 3 };

    // outputting swatches, or this block doesn't have a good official texture?
    if ( (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES) || 
        !( gBlockDefinitions[type].flags & BLF_IMAGE_TEXTURE) )
    {
        // use a solid color - we could add carpet and stairs here, among others.
        // TODO: note that this is not as fleshed out (e.g. I don't know if snow works) as full textures, which is the popular mode.
        swatchLoc = type;
        switch ( type )
        {
        case BLOCK_DOUBLE_STONE_SLAB:
        case BLOCK_STONE_SLAB:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:	// no change, default stone "cement" BLOCK_STONE_SLAB
            case 8:
                // smooth stone slab (double slab only)
                // same as stone, AFAIK.
                break;
            case 1:	// sandstone
            case 9:
                // smooth sandstone (double slab only)
                swatchLoc = BLOCK_SANDSTONE;
                break;
            case 2:
                // wooden
                swatchLoc = BLOCK_WOODEN_PLANKS;
                break;
            case 3:
            case 11:
                // cobblestone
                swatchLoc = BLOCK_COBBLESTONE;
                break;
            case 4:
            case 12:
                // brick
                swatchLoc = BLOCK_BRICK;
                break;
            case 5:
            case 13:
                // stone brick
                swatchLoc = BLOCK_STONE_BRICKS;
                break;
            case 6:
            case 14:
                // nether brick
                swatchLoc = BLOCK_NETHER_BRICKS;
                break;
            case 7:	// quartz
            case 15:	// quartz
                // quartz (normally same quartz on all faces? See http://minecraft.gamepedia.com/Data_values)
                swatchLoc = BLOCK_QUARTZ_BLOCK;
                break;
            case 10:
                // quartz (normally same quartz on all faces? See http://minecraft.gamepedia.com/Data_values)
                swatchLoc = (type == BLOCK_DOUBLE_STONE_SLAB) ? BLOCK_QUARTZ_BLOCK : BLOCK_WOODEN_PLANKS;
                break;
            }
            break;
        }
    }
    else
    {
        int head,bottom,angle,inside,outside,newFaceDirection,flip;
        int xoff,xstart,dir,dirBit,frontLoc,trimVal;
        // north is 0, east is 1, south is 2, west is 3
        int faceRot[6] = { 0, 0, 1, 2, 0, 3 };

        // use the textures:
        // go past the NUM_BLOCKS solid colors, use the txrX and txrY to find which to go to.
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

        // now do anything special needed for the particular type, data, and face direction
        switch ( type )
        {
        case BLOCK_GRASS:
            //SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 3, 0,  2, 0 );
            // now use the manufactured grass block at 6,2
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6, 2,  2, 0 );
            if ( faceDirection != DIRECTION_BLOCK_TOP && faceDirection != DIRECTION_BLOCK_BOTTOM )
            {
                // check if block above is snow; if so, use snow side tile; note we
                // check against the original type, since the snow block is likely to be flattened
                if ( gBoxData[backgroundIndex+1].origType == BLOCK_SNOW )
                {
                    swatchLoc = SWATCH_INDEX( 4, 4 );
                }
            }
            break;
        case BLOCK_DIRT:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                break;
            case 1: // coarse dirt, never takes grass - new tile in 1.8
                swatchLoc = SWATCH_INDEX( 5, 3 );
                break;
            case 2: // podzol
                swatchLoc = SWATCH_INDEX( 15,17 );
                SWATCH_SWITCH_SIDE( faceDirection, 14,17 );
                break;
            }
            break;
        case BLOCK_DOUBLE_STONE_SLAB:
        case BLOCK_STONE_SLAB:
            // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
            // Since we're exporting full blocks, we don't care, and so mask off this 0x8 bit.
            // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
            // high order bit for double slabs means something else
            trimVal = ( type == BLOCK_STONE_SLAB ) ? (dataVal & 0x7) : dataVal;
            switch ( trimVal )
            {
            default:
                assert(0);
            case 0:
                // smooth stone, two slabs
                // use stone side pattern
                SWATCH_SWITCH_SIDE( faceDirection, 5, 0 );
                break;
            case 1:
                // sandstone
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY );
                SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12,  0,13 );
                break;
            case 2:
                // wooden
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrX, gBlockDefinitions[BLOCK_WOODEN_PLANKS].txrY );
                break;
            case 3:
                // cobblestone
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_COBBLESTONE].txrY );
                break;
            case 4:
                // brick
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_BRICK].txrX, gBlockDefinitions[BLOCK_BRICK].txrY );
                break;
            case 5:
                // stone brick
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_STONE_BRICKS].txrX, gBlockDefinitions[BLOCK_STONE_BRICKS].txrY );
                break;
            case 6:
                // nether brick
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_NETHER_BRICKS].txrX, gBlockDefinitions[BLOCK_NETHER_BRICKS].txrY );
                break;
            case 7:
                // quartz with distinctive sides and bottom
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrX, gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrY );
                SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6,17,  1,17 );
                break;
            case 8:
                // smooth stone slab (double slab only)
                // don't touch a thing, as top should be used on all sides
                break;
            case 9:
                // smooth sandstone (double slab only); top used on the sides
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY );
                break;
            case 15:
                // quartz same on all faces
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrX, gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrY );
                break;
            }
            break;
        case BLOCK_DOUBLE_RED_SANDSTONE_SLAB:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                // normal block
                SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 14,13, 5,8 );
                break;
            case 8:
                // smooth all over, so do nothing - just use top everywhere
                break;
            }
            break;
        case BLOCK_RED_SANDSTONE_SLAB:
            // normal block
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 14,13, 5,8 );
            break;
        case BLOCK_SANDSTONE_STAIRS:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12,  0,13 );
            break;
        case BLOCK_RED_SANDSTONE_STAIRS:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 14,13,  5,8 );
            break;
        case BLOCK_ENDER_CHEST:
            // fake TODO!
            frontLoc = 1;
            angle = 0;	// to make compiler happy
            switch ( faceDirection )
            {
            default:
            case DIRECTION_BLOCK_SIDE_LO_X:
                angle = 0;
                break;
            case DIRECTION_BLOCK_SIDE_HI_X:
                angle = 180;
                break;
            case DIRECTION_BLOCK_SIDE_LO_Z:
                angle = 90;
                break;
            case DIRECTION_BLOCK_SIDE_HI_Z:
                angle = 270;
                break;
            case DIRECTION_BLOCK_BOTTOM:
                // fine as is
                frontLoc = 0;
                break;
            case DIRECTION_BLOCK_TOP:
                // fine as is
                frontLoc = 0;
                break;
            }
            if ( frontLoc )
            {
                switch ( dataVal )
                {
                default:
                case 2: // facing north
                    angle += 270;
                    break;
                case 3: // facing south
                    angle += 90;
                    break;
                case 4: // facing west
                    // no change
                    break;
                case 5: // facing east
                    angle += 180;
                    break;
                }
                if ( (angle % 360) == 0 )
                {
                    swatchLoc = SWATCH_INDEX( 10,14 );
                }
                else
                {
                    swatchLoc = SWATCH_INDEX( 9,14 );
                }
            }
            break;
        case BLOCK_LOG:
        case BLOCK_AD_LOG:
            // bit tricksy: rotate by rotating face direction itself
            newFaceDirection = faceDirection;
            angle = 0;
            flip = 0;
            switch ( dataVal & 0xC )
            {
            default:
            case 0x0:
                // as above: newFaceDirection = faceDirection;
                break;
            case 0x4:
                switch ( faceDirection )
                {
                default:
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X:
                    newFaceDirection = DIRECTION_BLOCK_TOP;
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                    angle = 270;
                    flip = 1;
                    break;
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    angle = 90;
                    break;
                case DIRECTION_BLOCK_BOTTOM:
                    angle = 270;
                    newFaceDirection = DIRECTION_BLOCK_SIDE_LO_X;
                    break;
                case DIRECTION_BLOCK_TOP:
                    angle = 90;
                    newFaceDirection = DIRECTION_BLOCK_SIDE_LO_X;
                    break;
                }
                break;
            case 0x8:
                switch ( faceDirection )
                {
                default:
                case DIRECTION_BLOCK_SIDE_LO_X:
                    angle = 90;
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X:
                    angle = 270;
                    flip = 1;
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    newFaceDirection = DIRECTION_BLOCK_TOP;
                    break;
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP:
                    newFaceDirection = DIRECTION_BLOCK_SIDE_LO_X;
                    break;
                }

                break;
            case 0xC:
                // all faces are sides
                newFaceDirection = DIRECTION_BLOCK_SIDE_LO_Z;
                break;
            }
            // use data to figure out which side
            if ( type == BLOCK_LOG )
            {
                switch ( dataVal & 0x3 )
                {
                case 1: // spruce (dark)
                    SWATCH_SWITCH_SIDE_VERTICAL( newFaceDirection,  4, 7,  12,11 );
                    break;
                case 2: // birch
                    SWATCH_SWITCH_SIDE_VERTICAL( newFaceDirection,  5, 7,  11,11 );
                    break;
                case 3: // jungle
                    SWATCH_SWITCH_SIDE_VERTICAL( newFaceDirection,  9, 9,  13,11 );
                    break;
                default: // normal log
                    SWATCH_SWITCH_SIDE( newFaceDirection, 4,1 );
                }
            }
            else
            {
                assert( type == BLOCK_AD_LOG );
                switch ( dataVal & 0x3 )
                {
                default: // normal log
                case 0: // acacia
                    SWATCH_SWITCH_SIDE_VERTICAL( newFaceDirection,  5,11,  13,19 );
                    break;
                case 1: // dark oak
                    SWATCH_SWITCH_SIDE_VERTICAL( newFaceDirection, 14,19,  15,19 );
                    break;
                }
            }
            if ( angle != 0 && uvIndices )
                rotateIndices( localIndices, angle );
            if ( flip && uvIndices )
                flipIndicesLeftRight( localIndices );
            break;
        case BLOCK_WOODEN_PLANKS:
        case BLOCK_WOODEN_DOUBLE_SLAB:
        case BLOCK_WOODEN_SLAB:
            // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
            // Since we're exporting full blocks, we don't care, and so mask off this 0x8 bit.
            // This bit is unused for WOODEN_PLANKS, so masking doesn't hurt - we can share code here.
            // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
            switch ( dataVal & 0x7 )
            {
            default: // normal log
                assert(0);
            case 0:
                // no change, default plank is fine
                break;
            case 1: // spruce (dark)
                swatchLoc = SWATCH_INDEX( 6,12 );
                break;
            case 2: // birch
                swatchLoc = SWATCH_INDEX( 6,13 );
                break;
            case 3: // jungle
                swatchLoc = SWATCH_INDEX( 7,12 );
                break;
            case 4: // acacia
                swatchLoc = SWATCH_INDEX( 0,22 );
                break;
            case 5: // dark oak
                swatchLoc = SWATCH_INDEX( 1,22 );
                break;
            }
            break;
        case BLOCK_STONE:
            switch ( dataVal )
            {
            default: // normal stone
                assert(0);
            case 0:
                // no change, default stone is fine
                break;
            case 1: // granite
                swatchLoc = SWATCH_INDEX( 8,22 );
                break;
            case 2: // polished granite
                swatchLoc = SWATCH_INDEX( 9,22 );
                break;
            case 3: // diorite
                swatchLoc = SWATCH_INDEX( 6,22 );
                break;
            case 4: // polished diorite
                swatchLoc = SWATCH_INDEX( 7,22 );
                break;
            case 5: // andesite
                swatchLoc = SWATCH_INDEX( 4,22 );
                break;
            case 6: // polished andesite
                swatchLoc = SWATCH_INDEX( 5,22 );
                break;
            }
            break;
        case BLOCK_LEAVES:
            // if we're using print export, go with the non-fancy leaves (not transparent)
            // was, when we had opaque leaves: col = gPrint3D ? 5 : 4;
            switch ( dataVal & 0x3 )
            {
            default:
            case 0: // normal tree (oak)
                swatchLoc = SWATCH_INDEX( 4, 3 );
                break;
            case 1: // Spruce leaves
                swatchLoc = SWATCH_INDEX( 4, 8 );
                break;
            case 2: // birch - shaded differently in tile code, same as normal tree otherwise - now separate
                swatchLoc = SWATCH_INDEX( 13, 13 );
                break;
            case 3: // jungle
                swatchLoc = SWATCH_INDEX( 4, 12 );
                break;
            }
            break;
        case BLOCK_AD_LEAVES:
            switch ( dataVal & 0x3 )
            {
            case 0: // normal tree (acacia)
            default:
                swatchLoc = SWATCH_INDEX( 9, 19 );
                break;
            case 1: // dark oak
                swatchLoc = SWATCH_INDEX( 11, 19 );
                break;
            }
            break;
        case BLOCK_SAND:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                // no change, default sand is fine
                break;
            case 1: // red sand
                swatchLoc = SWATCH_INDEX( 13,17 );
                break;
            }
            break;
        case BLOCK_DISPENSER:
        case BLOCK_DROPPER:
        case BLOCK_FURNACE:
        case BLOCK_BURNING_FURNACE:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 13, 2,  14, 3 );
            if ( (faceDirection != DIRECTION_BLOCK_TOP) && (faceDirection != DIRECTION_BLOCK_BOTTOM) )
            {
                switch ( type )
                {
                case BLOCK_DISPENSER:
                    frontLoc = SWATCH_INDEX( 14, 2 );
                    break;
                case BLOCK_DROPPER:
                    frontLoc = SWATCH_INDEX(  7,15 );
                    break;
                case BLOCK_FURNACE:
                    frontLoc = SWATCH_INDEX( 12, 2 );
                    break;
                case BLOCK_BURNING_FURNACE:
                default:
                    frontLoc = SWATCH_INDEX( 13, 3 );
                    break;
                }
                switch ( dataVal & 0x7 )
                {
                case 0:	// dispenser/dropper facing down, can't be anything else
                    swatchLoc = SWATCH_INDEX( 14, 3 );
                    break;
                case 1: // dispenser/dropper facing up, can't be anything else
                    swatchLoc = SWATCH_INDEX( 14, 3 );
                    break;
                case 2: // North
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                        swatchLoc = frontLoc;
                    break;
                case 3: // South, which is really the west side, i.e. HI_Z
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                        swatchLoc = frontLoc;
                    break;
                case 4: // West
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                        swatchLoc = frontLoc;
                    break;
                case 5: // East
                default:
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                        swatchLoc = frontLoc;
                    break;
                }
            }
            else
            {
                // top or bottom face: is it a dispenser or dropper?
                if ( ( type == BLOCK_DISPENSER ) || ( type == BLOCK_DROPPER ) )
                {
                    switch ( dataVal )
                    {
                    case 0:	// dispenser/dropper facing down, can't be anything else
                        if (faceDirection == DIRECTION_BLOCK_BOTTOM)
                        {
                            swatchLoc = ( type == BLOCK_DISPENSER ) ? SWATCH_INDEX( 15, 2 ) : SWATCH_INDEX( 8,15 ) ;
                        }
                        else
                        {
                            swatchLoc = SWATCH_INDEX( 14, 3 );
                        }
                        break;
                    case 1: // dispenser/dropper facing up, can't be anything else
                        if (faceDirection == DIRECTION_BLOCK_TOP)
                        {
                            swatchLoc = ( type == BLOCK_DISPENSER ) ? SWATCH_INDEX( 15, 2 ) : SWATCH_INDEX( 8,15 ) ;
                        }
                        else
                        {
                            swatchLoc = SWATCH_INDEX( 14, 3 );
                        }
                        break;
                    }
                }
            }
            break;
        case BLOCK_POWERED_RAIL:
        case BLOCK_DETECTOR_RAIL:
        case BLOCK_ACTIVATOR_RAIL:
            switch ( type )
            {
            case BLOCK_POWERED_RAIL:
                if ( !(dataVal & 0x8) )
                {
                    // unpowered rail
                    swatchLoc = SWATCH_INDEX( 3, 10 );
                }
                break;
            case BLOCK_DETECTOR_RAIL:
                // by default, the detector rail is in its undetected state
                if ( dataVal & 0x8 )
                {
                    // rail detector activated (same tile in basic game)
                    swatchLoc = SWATCH_INDEX( 11,17 );
                }
                break;
            case BLOCK_ACTIVATOR_RAIL:
                // by default, unactivated
                if ( dataVal & 0x8 )
                {
                    // activated rail
                    swatchLoc = SWATCH_INDEX( 9,17 );
                }
                break;
            }
            // if not a normal rail, there are no curve bits, so mask off upper bit, which is
            // whether the rail is powered or not.
            dataVal &= 0x7;
            // fall through:
        case BLOCK_RAIL:
            // get data of track itself
            switch ( dataVal )
            {
            case 0:
            case 4:
            case 5:
                // horizontal straight (plus sloping)
                break;

            case 2:
            case 3:
                // sloping: if side, don't rotate!
                if ( faceDirection != DIRECTION_BLOCK_TOP )
                    break;
                // else continue through and rotate indices, it's on the ground
            case 1:
                // vertical straight (plus sloping)
                if ( uvIndices )
                    rotateIndices( localIndices, 90 );
                break;

            case 6:
            case 7:
            case 8:
            case 9:
                // curved piece
                assert(type == BLOCK_RAIL );
                swatchLoc = SWATCH_INDEX( 0, 7 );
                if ( uvIndices )
                    rotateIndices( localIndices, 90*(dataVal-6));
                break;
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_SANDSTONE:
            // top is always sandy, just leave it be
            if ( faceDirection != DIRECTION_BLOCK_TOP )
            {
                // something must be done
                // use data to figure out which type of sandstone
                switch ( dataVal )
                {
                default:
                    assert(0);
                case 0:
                    // normal sandstone, bottom does get changed
                    SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12, 0,13 );
                    break;
                case 1: // creeper - bottom unchanged
                    SWATCH_SWITCH_SIDE( faceDirection, 5,14 );
                    break;
                case 2: // smooth - bottom unchanged
                    SWATCH_SWITCH_SIDE( faceDirection, 6,14 );
                    break;
                }
            }
            break;
        case BLOCK_RED_SANDSTONE:
            // top is always sandy, just leave it be
            if ( faceDirection != DIRECTION_BLOCK_TOP )
            {
                // something must be done
                // use data to figure out which type of sandstone
                switch ( dataVal )
                {
                default:
                    assert(0);
                case 0:
                    // bottom does get changed
                    SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 14,13, 5,8 );
                    break;
                case 1: // chiseled - bottom smooth like top
                    SWATCH_SWITCH_SIDE( faceDirection, 5,12 );
                    break;
                case 2: // smooth - bottom smooth like top
                    SWATCH_SWITCH_SIDE( faceDirection, 10,19 );
                    break;
                }
            }
            break;
        case BLOCK_NOTEBLOCK:
            SWATCH_SWITCH_SIDE( faceDirection, 12, 8 );	// was 10, 4 jukebox side, now is separate at 
            break;
        case BLOCK_BED:
            if ( (faceDirection != DIRECTION_BLOCK_TOP) && (faceDirection != DIRECTION_BLOCK_BOTTOM) )
            {
                // side of bed - head or foot?
                if ( dataVal & 0x8 )
                {
                    // head of bed.
                    xoff = 1;
                    xstart = 7;
                }
                else
                {
                    xoff = -1;
                    xstart = 6;
                }

                // dataVal gives which way it points 7,9 and 8,9 vs. 5,9, 6,9
                switch ( ((dataVal & 0x3) - faceRot[faceDirection] + 4) % 4 )
                {
                case 0: // south
                    swatchLoc = SWATCH_INDEX( xstart, 9 );
                    break;
                case 1:
                    swatchLoc = SWATCH_INDEX( xoff+xstart, 9 );
                    if ( uvIndices )
                        flipIndicesLeftRight( localIndices );
                    break;
                case 2:
                    swatchLoc = SWATCH_INDEX( xstart, 9 );
                    if ( uvIndices )
                        flipIndicesLeftRight( localIndices );
                    break;
                case 3:
                    swatchLoc = SWATCH_INDEX( xoff + xstart, 9 );
                    break;
                default:
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                        swatchLoc = SWATCH_INDEX( 14, 2 );
                    break;
                }
            }
            else if (faceDirection == DIRECTION_BLOCK_TOP)
            {
                // top surface of bed
                // head or foot?
                if ( dataVal & 0x8 )
                {
                    // head
                    swatchLoc = SWATCH_INDEX( 7, 8 );
                }
                if ( uvIndices )
                {
                    // rotate as needed (head and foot rotate the same)
                    switch ( dataVal & 0x3 )
                    {
                    case 0: // south
                        rotateIndices( localIndices, 90 );
                        break;
                    case 1: // west
                        rotateIndices( localIndices, 180 );
                        break;
                    case 2: // north
                        rotateIndices( localIndices, 270 );
                        break;
                    case 3: // east
                        rotateIndices( localIndices, 0 );
                        break;
                    }
                }
            }
            else
            {
                // bottom of bed is always wood
                swatchLoc = SWATCH_INDEX( 4, 0 );
            }
            break;
        case BLOCK_STICKY_PISTON:
        case BLOCK_PISTON:
            // 10,6 sticky head, 11,6 head, 12,6 side, 13,6 bottom, 14,6 extended top
            head = bottom = 0;
            dir = dataVal & 7;
            angle = 0;
            switch ( dir )
            {
            case 0: // pointing down
            case 1: // pointing up
                if ( faceDirection == DIRECTION_BLOCK_BOTTOM )
                {
                    head = 1 - dir;
                    bottom = dir;
                }
                else if ( faceDirection == DIRECTION_BLOCK_TOP )
                {
                    head = dir;
                    bottom = 1 - dir;
                }
                // else it's a side, and since sides are aligned, rotate all 180 or none
                else
                {
                    angle=180*(1-dir);
                }
                break;
            case 2: // pointing east (new north)
            case 3: // pointing west (new south)
                dirBit = dir - 2;
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    head = 1 - dirBit;
                    bottom = dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    head = dirBit;
                    bottom = 1 - dirBit;
                }
                // else it's a side
                if ( ( faceDirection == DIRECTION_BLOCK_BOTTOM ) ||
                    ( faceDirection == DIRECTION_BLOCK_TOP ) )
                {
                    angle = dirBit*180;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    angle = 90 + dirBit*180;
                }
                else
                {
                    angle = 270 + dirBit*180;
                }
                break;
            case 4: // pointing north (new west)
            case 5: // pointing south (new east)
                dirBit = dir - 4;
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                {
                    head = 1 - dirBit;
                    bottom = dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    head = dirBit;
                    bottom = 1 - dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    angle = 270 + dirBit*180;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    angle = 90 + dirBit*180;
                }
                else
                {
                    angle = 270 + dirBit*180;
                }
                break;
            }
            // ok, now we know head vs. bottom vs. side & angle
            if ( head )
            {
                // sticky, extended, or regular?
                if ( dataVal & 8 )
                {
                    // extended, and somewhat irrelevant, since it's covered by the piston head,
                    // but I guess the head could be outside the model bounds.
                    swatchLoc = SWATCH_INDEX( 14, 6 );
                }
                else
                {
                    if ( type == BLOCK_STICKY_PISTON )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 6 );
                    }
                    else
                    {
                        swatchLoc = SWATCH_INDEX( 11, 6 );
                    }
                }
            }
            else if ( bottom )
            {
                // easy!
                swatchLoc = SWATCH_INDEX( 13, 6 );
            }
            else
            {
                // side
                swatchLoc = SWATCH_INDEX( 12, 6 );
                if ( uvIndices )
                    rotateIndices( localIndices, angle );
            }
            break;
        case BLOCK_PISTON_HEAD:
            // overkill copy of PISTON above
            // 10,6 sticky head, 11,6 head, 12,6 side, 13,6 bottom, 14,6 extended top
            head = bottom = 0;
            dir = dataVal & 7;
            angle = 0;
            switch ( dir )
            {
            case 0: // pointing down
            case 1: // pointing up
                if ( faceDirection == DIRECTION_BLOCK_BOTTOM )
                {
                    head = 1 - dir;
                    bottom = dir;
                }
                else if ( faceDirection == DIRECTION_BLOCK_TOP )
                {
                    head = dir;
                    bottom = 1 - dir;
                }
                // else it's a side, and since sides are aligned, rotate all 180 or none
                else
                {
                    angle=180*(1-dir);
                }
                break;
            case 2: // pointing east (new north)
            case 3: // pointing west (new south)
                dirBit = dir - 2;
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    head = 1 - dirBit;
                    bottom = dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    head = dirBit;
                    bottom = 1 - dirBit;
                }
                // else it's a side
                if ( ( faceDirection == DIRECTION_BLOCK_BOTTOM ) ||
                    ( faceDirection == DIRECTION_BLOCK_TOP ) )
                {
                    angle = dirBit*180;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    angle = 90 + dirBit*180;
                }
                else
                {
                    angle = 270 + dirBit*180;
                }
                break;
            case 4: // pointing north (new west)
            case 5: // pointing south (new east)
                dirBit = dir - 4;
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                {
                    head = 1 - dirBit;
                    bottom = dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    head = dirBit;
                    bottom = 1 - dirBit;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    angle = 270 + dirBit*180;
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    angle = 90 + dirBit*180;
                }
                else
                {
                    angle = 270 + dirBit*180;
                }
                break;
            }
            // ok, now we know head vs. bottom vs. side & angle
            if ( head )
            {
                // sticky, extended, or regular?
                if ( dataVal & 8 )
                {
                    // extended, and somewhat irrelevant, since it's covered by the piston head,
                    // but I guess the head could be outside the model bounds.
                    swatchLoc = SWATCH_INDEX( 10, 6 );
                }
                else
                {
                    swatchLoc = SWATCH_INDEX( 11, 6 );
                }
            }
            else if ( bottom )
            {
                // easy!
                swatchLoc = SWATCH_INDEX( 11, 6 );
            }
            else
            {
                // side
                swatchLoc = SWATCH_INDEX( 11, 6 );
                if ( uvIndices )
                    rotateIndices( localIndices, angle );
            }
            break;
        case BLOCK_TNT:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 8, 0,  10, 0 );
            break;
        case BLOCK_BOOKSHELF:
            SWATCH_SWITCH_SIDE( faceDirection, 3, 2 );
            break;
        case BLOCK_WOODEN_DOOR:
        case BLOCK_IRON_DOOR:
        case BLOCK_SPRUCE_DOOR:
        case BLOCK_BIRCH_DOOR:
        case BLOCK_JUNGLE_DOOR:
        case BLOCK_DARK_OAK_DOOR:
        case BLOCK_ACACIA_DOOR:
            // top half is default
            if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                (faceDirection == DIRECTION_BLOCK_BOTTOM) )
            {
                // full block "door" - on top or bottom make it full wood or iron
                switch ( type ) {
                default:
                    assert(0);
                case BLOCK_WOODEN_DOOR:
                    swatchLoc = SWATCH_INDEX( 4, 0 );
                    break;
                case BLOCK_IRON_DOOR:
                    swatchLoc = SWATCH_INDEX( 6, 1 );
                    break;
                case BLOCK_SPRUCE_DOOR:
                    swatchLoc = SWATCH_INDEX( 6,12 );
                    break;
                case BLOCK_BIRCH_DOOR:
                    swatchLoc = SWATCH_INDEX( 6,13 );
                    break;
                case BLOCK_JUNGLE_DOOR:
                    swatchLoc = SWATCH_INDEX( 7,12 );
                    break;
                case BLOCK_DARK_OAK_DOOR:
                    swatchLoc = SWATCH_INDEX( 1,22 );
                    break;
                case BLOCK_ACACIA_DOOR:
                    swatchLoc = SWATCH_INDEX( 0,22 );
                    break;
                }
            }
            else if ( !(dataVal & 0x8) )
            {
                // switch from top to bottom half of door
                switch ( type ) {
                default:
                    assert(0);
                case BLOCK_WOODEN_DOOR:
                case BLOCK_IRON_DOOR:
                    swatchLoc += 16;
                    break;
                case BLOCK_SPRUCE_DOOR:
                case BLOCK_BIRCH_DOOR:
                case BLOCK_JUNGLE_DOOR:
                case BLOCK_DARK_OAK_DOOR:
                case BLOCK_ACACIA_DOOR:
                    swatchLoc--;
                    break;
                }
            }
            break;

        case BLOCK_TORCH:
        case BLOCK_REDSTONE_TORCH_ON:
        case BLOCK_REDSTONE_TORCH_OFF:
            // is torch in middle of block?
            if ( dataVal == 5 )
            {
                // use the "from above" torch
                if ( type == BLOCK_TORCH )
                    swatchLoc = TORCH_TOP;
                else if ( type == BLOCK_REDSTONE_TORCH_ON )
                    swatchLoc = RS_TORCH_TOP_ON;
                if ( type == BLOCK_REDSTONE_TORCH_OFF )
                    swatchLoc = RS_TORCH_TOP_OFF;
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_LEVER:
            // TODO in a perfect world, we'd move the lever up a bit when compositing, so its bottom was more centered.
            angle = (dataVal & 0x8 ) ? 180 : 0;
            if ( ((dataVal & 0x7) == 5) || ((dataVal & 0x7) == 7) )
                angle += 180;
            else if ( ((dataVal & 0x7) == 6) || ((dataVal & 0x7) == 0) )
                angle += 90;
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, angle );
            break;
        case BLOCK_TRIPWIRE_HOOK:
            // currently we don't adjust the tripwire hook
            angle = 0;
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, angle );
            break;
        case BLOCK_CHEST:
        case BLOCK_TRAPPED_CHEST:
            // set side of chest as default
            SWATCH_SWITCH_SIDE( faceDirection, 10, 1 );
            switch ( dataVal )
            {
            case 3: // new south
                if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z ) // south
                {
                    // front of chest, on possibly long face
                    swatchLoc = SWATCH_INDEX( 11, 1 );	// front
                    // is neighbor to east also a chest?
                    if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                    // else, is neighbor to west also a chest?
                    else if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z ) // north
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                    else if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                }
                // TODO: we still haven't fixed chest tops - should be a separate tile or two, shoved into tiles.h
                //else if ( faceDirection == DIRECTION_BLOCK_TOP  || faceDirection == DIRECTION_BLOCK_BOTTOM ) // top or bottom
                break;
            case 4: // west
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X ) // west
                {
                    swatchLoc = SWATCH_INDEX( 11, 1 );
                    if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X ) // east
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                }
                break;
            case 2: // north
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    swatchLoc = SWATCH_INDEX( 11, 1 );
                    if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                }
                break;
            case 5: // new east
                if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    swatchLoc = SWATCH_INDEX( 11, 1 );
                    if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                    else if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                    else if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                }
                break;
            case 0:
                // old bad data, so this code matches it.
                // In reality, in 1.8 such chests just disappear! The data's still
                // in the world, but you can't see or interact with the chests.
                if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                {
                    if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if ( gBoxData[backgroundIndex-gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                    else if ( gBoxData[backgroundIndex+gBoxSizeYZ].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                {
                    if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 2 );
                    }
                    else if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 2 );
                    }
                }
                else if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if ( gBoxData[backgroundIndex+gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 9, 3 );
                    }
                    else if ( gBoxData[backgroundIndex-gBoxSize[Y]].type == type )
                    {
                        swatchLoc = SWATCH_INDEX( 10, 3 );
                    }
                }
                break;
            default:
                assert(0);
                break;
            }
            break;
        case BLOCK_CRAFTING_TABLE:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 11, 3,  4, 0 );
            if ( ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X ) || ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z ) )
            {
                SWATCH_SWITCH_SIDE( faceDirection, 12, 3 );
            }
            break;
        case BLOCK_CACTUS:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6, 4,  7, 4 );
            break;
        case BLOCK_PUMPKIN:
        case BLOCK_JACK_O_LANTERN:
        case BLOCK_HEAD:	// definitely wrong for heads, TODO - need tile entity daa
            SWATCH_SWITCH_SIDE( faceDirection, 6, 7 );
            xoff = ( type == BLOCK_PUMPKIN ) ? 7 : 8;
            if ( ( faceDirection != DIRECTION_BLOCK_TOP ) && ( faceDirection != DIRECTION_BLOCK_BOTTOM ) )
            {
                switch ( dataVal )
                {
                case 0: // south
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_Z )
                    {
                        swatchLoc = SWATCH_INDEX( xoff, 7 );
                    }
                    break;
                case 1: // west
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_X )
                    {
                        swatchLoc = SWATCH_INDEX( xoff, 7 );
                    }
                    break;
                case 2: // north
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_LO_Z )
                    {
                        swatchLoc = SWATCH_INDEX( xoff, 7 );
                    }
                    break;
                case 3: // east
                    if ( faceDirection == DIRECTION_BLOCK_SIDE_HI_X )
                    {
                        swatchLoc = SWATCH_INDEX( xoff, 7 );
                    }
                    break;
                }
            }
            break;
        case BLOCK_JUKEBOX:
            SWATCH_SWITCH_SIDE( faceDirection, 10, 4 );
            break;
        case BLOCK_CAKE:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 10, 7,  12, 7 );
            break;
        case BLOCK_FARMLAND:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 2, 0,  2, 0 );
            break;
        case BLOCK_REDSTONE_REPEATER_OFF:
        case BLOCK_REDSTONE_REPEATER_ON:
            swatchLoc = SWATCH_INDEX( 3, 8 + (type == BLOCK_REDSTONE_REPEATER_ON) );
            if ( uvIndices )
                rotateIndices( localIndices, 90*(dataVal&0x3));
            break;
        case BLOCK_REDSTONE_COMPARATOR_INACTIVE:
        case BLOCK_REDSTONE_COMPARATOR_ACTIVE:
            // in 1.5, comparator active is used for top bit
            // in 1.6, comparator active is not used, it depends on dataVal
            {
                int in_powered = ((type == BLOCK_REDSTONE_COMPARATOR_ACTIVE) || (dataVal >= 8));
                swatchLoc = SWATCH_INDEX( 14 + in_powered,14 );
            }
            if ( uvIndices )
                rotateIndices( localIndices, 90*(dataVal&0x3));
            break;
        case BLOCK_REDSTONE_WIRE:
            angle = 0;
            if ( faceDirection == DIRECTION_BLOCK_TOP )
            {
                // flat wire
                switch( dataVal )
                {
                case 0x0:
                    // no connections, just a dot
                    swatchLoc = REDSTONE_WIRE_DOT; // SWATCH_INDEX( 4,11);
                    break;

                case FLAT_FACE_LO_X:
                case FLAT_FACE_HI_X:
                    // one node, but it's a two-way in Minecraft: no single branch
                case FLAT_FACE_LO_X|FLAT_FACE_HI_X:
                    swatchLoc = SWATCH_INDEX( 5,10 );
                    break;
                case FLAT_FACE_LO_Z:
                case FLAT_FACE_HI_Z:
                case FLAT_FACE_LO_Z|FLAT_FACE_HI_Z:
                    angle = 90;
                    swatchLoc = SWATCH_INDEX( 5,10 );
                    break;

                    // angled 2 wire:
                case FLAT_FACE_LO_X|FLAT_FACE_LO_Z:
                    angle = 270;
                    swatchLoc = REDSTONE_WIRE_ANGLED_2;
                    break;
                case FLAT_FACE_LO_Z|FLAT_FACE_HI_X:
                    angle = 0;
                    swatchLoc = REDSTONE_WIRE_ANGLED_2;
                    break;
                case FLAT_FACE_HI_X|FLAT_FACE_HI_Z:
                    angle = 90;
                    swatchLoc = REDSTONE_WIRE_ANGLED_2;
                    break;
                case FLAT_FACE_HI_Z|FLAT_FACE_LO_X:
                    angle = 180;
                    swatchLoc = REDSTONE_WIRE_ANGLED_2;
                    break;

                    // 3 wire
                case FLAT_FACE_LO_X|FLAT_FACE_LO_Z|FLAT_FACE_HI_X:
                    angle = 270;
                    swatchLoc = REDSTONE_WIRE_3;
                    break;
                case FLAT_FACE_LO_Z|FLAT_FACE_HI_X|FLAT_FACE_HI_Z:
                    angle = 0;
                    swatchLoc = REDSTONE_WIRE_3;
                    break;
                case FLAT_FACE_HI_X|FLAT_FACE_HI_Z|FLAT_FACE_LO_X:
                    angle = 90;
                    swatchLoc = REDSTONE_WIRE_3;
                    break;
                case FLAT_FACE_HI_Z|FLAT_FACE_LO_X|FLAT_FACE_LO_Z:
                    angle = 180;
                    swatchLoc = REDSTONE_WIRE_3;
                    break;

                default:
                    assert(0);
                case FLAT_FACE_LO_X|FLAT_FACE_LO_Z|FLAT_FACE_HI_X|FLAT_FACE_HI_Z:
                    // easy: 4 way, it's already done
                    break;
                }
            }
            else
            {
                // vertical wire
                SWATCH_SWITCH_SIDE( faceDirection, 5,10 );
                angle = 270;
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, angle );
            break;
        case BLOCK_STONE_BRICKS:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                // no change
                break;
            case 1:
                // mossy
                swatchLoc = SWATCH_INDEX( 4, 6 );
                break;
            case 2:
                // cracked
                swatchLoc = SWATCH_INDEX( 5, 6 );
                break;
            case 3:
                // circle - added in 1.2.4
                swatchLoc = SWATCH_INDEX( 5,13 );
                break;
            }
            break;
        case BLOCK_TRAPDOOR:
        case BLOCK_IRON_TRAPDOOR:
        case BLOCK_DAYLIGHT_SENSOR:
        case BLOCK_INVERTED_DAYLIGHT_SENSOR:
        case BLOCK_LADDER:
        case BLOCK_LILY_PAD:
        case BLOCK_DANDELION:
        case BLOCK_BROWN_MUSHROOM:
        case BLOCK_RED_MUSHROOM:
        case BLOCK_DEAD_BUSH:
        case BLOCK_PUMPKIN_STEM:
        case BLOCK_MELON_STEM:
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_POPPY:
            if ( dataVal > 0 )
            {
                // row 20 has these flowers; else rose (12,0) is used
                swatchLoc = SWATCH_INDEX( dataVal-1,19 );
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_DOUBLE_FLOWER:
            if ( dataVal < 8 )
            {
                // bottom half of plant
                if ( dataVal == 0 )
                {
                    // sunflower head
                    swatchLoc = SWATCH_INDEX( 1,18 );
                }
                else
                {
                    swatchLoc = SWATCH_INDEX( dataVal*2+3,18 );
                }
            } // else it's an unknown flower or flower top (weird), so just keep the default swatchLoc
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_SAPLING:
            switch ( dataVal & 0x3 )
            {
            case 0: // OAK
                // set OK already
                break;
            case 1:
                // spruce
                swatchLoc = SWATCH_INDEX(15,3);
                break;
            case 2:
                // birch
                swatchLoc = SWATCH_INDEX(15,4);
                break;
            default:
            case 3:
                swatchLoc = SWATCH_INDEX(14,1);
                break;
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_TALL_GRASS:
            switch ( dataVal )
            {
            case 0:
                // dead bush appearance
                swatchLoc = SWATCH_INDEX(7,3);
                break;
            case 1:
            default:
                // set OK already
                break;
            case 2:
                // fern
                swatchLoc = SWATCH_INDEX(8,3);
                break;
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_VINES:
            // special case (and I'm still not sure about this), if background is air, then
            // just use the default vine, whatever it is
            if ( gBoxData[backgroundIndex].type == BLOCK_AIR || gBoxData[backgroundIndex].type == BLOCK_VINES )
            {
                swatchLoc = SWATCH_INDEX( 15, 8 );
            }
            else
            {
                swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            }
            break;
        case BLOCK_HIDDEN_SILVERFISH:
            switch ( dataVal )
            {
            case 0:
            default:
                // default
                break;
            case 1: // cobblestone
                swatchLoc = SWATCH_INDEX( 0, 1 );
                break;
            case 2: // stone brick
                swatchLoc = SWATCH_INDEX( 6, 3 );
                break;
            }
            break;
        case BLOCK_HUGE_BROWN_MUSHROOM:
        case BLOCK_HUGE_RED_MUSHROOM:
            inside = SWATCH_INDEX( 14, 8 );
            outside = SWATCH_INDEX( (type == BLOCK_HUGE_BROWN_MUSHROOM) ? 14 : 13, 7 );
            swatchLoc = inside;
            switch ( dataVal )
            {
            case 0: //0	 Fleshy piece	 Pores on all sides
                // done above, it's the default: swatchLoc = inside;
                break;
            case 1: //1	 Corner piece	 Cap texture on top, West and North
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_X) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 2: //2	 Side piece	 Cap texture on top and North
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 3: //3	 Corner piece	 Cap texture on top, North and East
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_X) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 4: //4	 Side piece	 Cap texture on top and West
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_X) )
                {
                    swatchLoc = outside;
                }
                break;
            case 5: //5	 Top piece	 Cap texture on top
                if ( faceDirection == DIRECTION_BLOCK_TOP )
                {
                    swatchLoc = outside;
                }
                break;
            case 6: //6	 Side piece	 Cap texture on top and East
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_X) )
                {
                    swatchLoc = outside;
                }
                break;
            case 7: //7	 Corner piece	 Cap texture on top, South and West
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_LO_X) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 8: //8	 Side piece	 Cap texture on top and South
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 9: //9	 Corner piece	 Cap texture on top, East and South
                if ( (faceDirection == DIRECTION_BLOCK_TOP) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_X) ||
                    (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z) )
                {
                    swatchLoc = outside;
                }
                break;
            case 10: //10	 Stem piece	 Stem texture on all four sides, pores on top and bottom
                swatchLoc = inside;
                SWATCH_SWITCH_SIDE( faceDirection, 13, 8 );
                break;
            }
            break;
        case BLOCK_MELON:
            SWATCH_SWITCH_SIDE( faceDirection, 8, 8 );
            break;
        case BLOCK_MYCELIUM:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 13, 4,  2, 0 );
            break;
        case BLOCK_ENCHANTMENT_TABLE:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6,11,  7,11 );
            break;
        case BLOCK_BREWING_STAND:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 13, 9,  12, 9 );
            break;
        case BLOCK_CAULDRON:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 10, 9,  11, 9 );
            break;
        case BLOCK_END_PORTAL_FRAME:
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 15, 9,  15,10 );
            break;
        case BLOCK_COBBLESTONE_WALL:
            if ( dataVal == 0x1 )
            {
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_MOSS_STONE].txrX, gBlockDefinitions[BLOCK_MOSS_STONE].txrY );
            }
            break;
        case BLOCK_CARPET:
        case BLOCK_WOOL:
            swatchLoc = retrieveWoolSwatch( dataVal );
            break;
        case BLOCK_STAINED_GLASS:
        case BLOCK_STAINED_GLASS_PANE:
            // add data value to retrieve proper texture
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY ) + dataVal;
            break;
        case BLOCK_STAINED_CLAY:
            swatchLoc += dataVal;
            break;
        case BLOCK_HAY:
            SWATCH_SWITCH_SIDE( faceDirection, 9,15 );
            break;
        case BLOCK_HOPPER:
            // full block version - inside gets used for bottom; a little goofy, but smoother, I think.
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 12,15,  11,15 );
            break;
        case BLOCK_QUARTZ_BLOCK:
            // use data to figure out which type of quartz
            switch ( dataVal )
            {
            case 0:
                SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6,17, 1,17 );
                break;
            case 1:	// chiseled quartz block
                SWATCH_SWITCH_SIDE_VERTICAL( faceDirection, 3,17, 2,17 );
                break;
            case 2: // pillar quartz block (vertical)
                SWATCH_SWITCH_SIDE_VERTICAL( faceDirection, 5,17, 4,17 );
                break;
            case 3: // pillar quartz block (north-south)
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP:
                    swatchLoc = SWATCH_INDEX( 5,17 );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    swatchLoc = SWATCH_INDEX( 5,17 );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X:
                    swatchLoc = SWATCH_INDEX( 4,17 );
                    break;
                }
                break;
            case 4: // pillar quartz block (east-west)
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP:
                    swatchLoc = SWATCH_INDEX( 5,17 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X:
                    swatchLoc = SWATCH_INDEX( 5,17 );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    swatchLoc = SWATCH_INDEX( 4,17 );
                    break;
                }
                break;
            }
            break;
        case BLOCK_PRISMARINE:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                break;
            case 1: // bricks
                swatchLoc = SWATCH_INDEX( 10,22 );
                break;
            case 2: // dark
                swatchLoc = SWATCH_INDEX( 11,22 );
                break;
            }
            break;
        case BLOCK_SPONGE:
            switch ( dataVal )
            {
            default:
                assert(0);
            case 0:
                break;
            case 1: // wet
                swatchLoc = SWATCH_INDEX( 15,22 );
                break;
            }
            break;
        }
    }

    // flag UVs as being used for this swatch, so that the actual four UV values
    // are saved.

    if ( uvIndices && gExportTexture )
    {
        int standardCorners[4];
        // get four UV texture vertices, based on type of block
        saveTextureCorners( swatchLoc, type, standardCorners );

        // let the adjustments begin!
        if ( faceDirection == DIRECTION_BLOCK_BOTTOM )
        {
            // -Y is unique: the textures are actually flipped! 2,1,4,3
            uvIndices[0] = standardCorners[localIndices[1]];
            uvIndices[1] = standardCorners[localIndices[0]];
            uvIndices[2] = standardCorners[localIndices[3]];
            uvIndices[3] = standardCorners[localIndices[2]];
        }
        else
        {
            // Normal case (note that pistons go through this, too, but we compensate
            // earlier - easier than testing for this special case here)
            uvIndices[0] = standardCorners[localIndices[0]];
            uvIndices[1] = standardCorners[localIndices[1]];
            uvIndices[2] = standardCorners[localIndices[2]];
            uvIndices[3] = standardCorners[localIndices[3]];
        }
    }

    return swatchLoc;
}

// if we have a cutout swatch, we need to also add the swatch to the library
// Note that currently we don't rotate the background texture properly compared to the swatch itself - tough.
static int getCompositeSwatch( int swatchLoc, int backgroundIndex, int faceDirection, int angle )
{
    // does library have type/backgroundType desired?
    SwatchComposite *pSwatch = gModel.swatchCompositeList;
    int backgroundSwatchLoc;
    if ( gModel.alreadyAskedForComposite )
    {
        // The data coming in is probably modded and invalid. To avoid an infinite regression (getCompositeSwatch calling itself),
        // we pull the plug and give back cobblestone, end of story. We should also flag an error, but that's not done -
        // the user's already been warned his data's bad.
        backgroundSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_STONE].txrX, gBlockDefinitions[BLOCK_STONE].txrY );
    }
    else
    {
        gModel.alreadyAskedForComposite = true;
        backgroundSwatchLoc = getSwatch( gBoxData[backgroundIndex].type, gBoxData[backgroundIndex].data, faceDirection, backgroundIndex, NULL );
    }

    while ( pSwatch )
    {
        if ( (pSwatch->swatchLoc == swatchLoc) && 
            (pSwatch->angle == angle) &&
            (pSwatch->backgroundSwatchLoc == backgroundSwatchLoc) )
        {
            // done with reentry check, we have something valid
            gModel.alreadyAskedForComposite = false;
            return pSwatch->compositeSwatchLoc;
        }

        pSwatch = pSwatch->next;
    }

    // can't find swatch, so see if we can make it
    if ( gModel.swatchCount >= gModel.swatchListSize )
    {
        // no room for more swatches. Plan B: find the default swatch for this type
        pSwatch = gModel.swatchCompositeList;
        while ( pSwatch )
        {
            // any port in a storm: use the first match found (there should always be one)
            if ( pSwatch->swatchLoc == swatchLoc )
                return pSwatch->compositeSwatchLoc;

            pSwatch = pSwatch->next;
        }
        assert(0);  // you need to create a default type/backgroundType swatch for this type
        return 0;
    }

    // done with reentry check
    gModel.alreadyAskedForComposite = false;

    // make it
    return createCompositeSwatch( swatchLoc, backgroundSwatchLoc, angle );
}

// take the cutout at swatchLoc, with given type and subtype, and put it over the background at background index, for faceDirection
static int createCompositeSwatch( int swatchLoc, int backgroundSwatchLoc, int angle )
{
    SwatchComposite *pSwatch = (SwatchComposite *)malloc(sizeof(SwatchComposite));
    pSwatch->swatchLoc = swatchLoc;
    pSwatch->angle = angle;
    pSwatch->backgroundSwatchLoc = backgroundSwatchLoc;
    pSwatch->next = NULL;

    // The real work: take the swatch and composite it over the background swatch - this should never recurse! If it does, then a background is a composite, too,
    // which should not happen

    if ( angle != 0 )
    {
        int scol,srow,dcol,drow;
        // rotate swatch by angle, then apply to background.
        SWATCH_TO_COL_ROW( swatchLoc, scol, srow );
        // set to new, temporary location for the swatch, for compositing
        swatchLoc = SWATCH_WORKSPACE;
        SWATCH_TO_COL_ROW( swatchLoc, dcol, drow );
        rotatePNGTile(gModel.pPNGtexture, dcol, drow, scol, srow, angle, gModel.swatchSize );
    }

    // where result goes, over, under
    compositePNGSwatches( gModel.pPNGtexture, gModel.swatchCount, swatchLoc, backgroundSwatchLoc, gModel.swatchSize, gModel.swatchesPerRow, 0 );
    pSwatch->compositeSwatchLoc = gModel.swatchCount++;

    // link it up
    if ( gModel.swatchCompositeListEnd != NULL )
    {
        gModel.swatchCompositeListEnd->next = pSwatch;
        gModel.swatchCompositeListEnd = pSwatch;
    }
    else
    {
        // first swatch ever added to list
        gModel.swatchCompositeList = gModel.swatchCompositeListEnd = pSwatch;
    }

    return pSwatch->compositeSwatchLoc;
}


// 0,0 1,0 1,1 0,1 is 0,1,2,3 
static void flipIndicesLeftRight( int localIndices[4] )
{
    int tmp;
    tmp = localIndices[0];
    localIndices[0] = localIndices[1];
    localIndices[1] = tmp;
    tmp = localIndices[2];
    localIndices[2] = localIndices[3];
    localIndices[3] = tmp;
}

static void rotateIndices( int localIndices[4], int angle )
{
    switch ( (angle+360)%360 )
    {
    case 0: // do nothing
        break;
    case 90: // rotate 90 clockwise - you rotate the UV's counterclockwise
        localIndices[0] = 1;
        localIndices[1] = 2;
        localIndices[2] = 3;
        localIndices[3] = 0;
        break;
    case 180:
        localIndices[0] = 2;
        localIndices[1] = 3;
        localIndices[2] = 0;
        localIndices[3] = 1;
        break;
    case 270:
        localIndices[0] = 3;
        localIndices[1] = 0;
        localIndices[2] = 1;
        localIndices[3] = 2;
        break;
    }
}

static void saveTextureCorners( int swatchLoc, int type, int uvIndices[4] )
{
    // add the four corner positions
    saveRectangleTextureUVs( swatchLoc, type, 0.0f, 1.0f, 0.0f, 1.0f, uvIndices );
}

// returns first UV coordinate location of four
// minx,
static void saveRectangleTextureUVs( int swatchLoc, int type, float minu, float maxu, float minv, float maxv, int uvIndices[4] )
{
    uvIndices[0] = saveTextureUV( swatchLoc, type, minu, minv );
    uvIndices[1] = saveTextureUV( swatchLoc, type, maxu, minv );
    uvIndices[2] = saveTextureUV( swatchLoc, type, maxu, maxv );
    uvIndices[3] = saveTextureUV( swatchLoc, type, minu, maxv );
}


static int saveTextureUV( int swatchLoc, int type, float u, float v )
{
    int i;
    int count = gModel.uvSwatches[swatchLoc].count;
    UVRecord *uvr = gModel.uvSwatches[swatchLoc].records;

    int col, row;

    assert(gExportTexture);

    for ( i = 0; i < count; i++ )
    {
        if ( (uvr->u == u) && (uvr->v == v) )
        {
            // match found, return the index
            return uvr->index;
        }
        uvr++;
    }

    // didn't find a match, so add it
    if ( count == gModel.uvSwatches[swatchLoc].size )
    {
        // need to alloc or realloc the list
        if ( count == 0 )
        {
            // alloc
            gModel.uvSwatches[swatchLoc].size = 4;
            gModel.uvSwatches[swatchLoc].records = (UVRecord *)malloc(gModel.uvSwatches[swatchLoc].size*sizeof(UVRecord));
        }
        else
        {
            // realloc
            UVRecord *records;
            int newSize = gModel.uvSwatches[swatchLoc].size * 3;	// I forget why I triple it - I think it's "floodgates are open, we need a lot more"
            records = (UVRecord*)malloc(newSize*sizeof(UVRecord));
            memcpy( records, gModel.uvSwatches[swatchLoc].records, count*sizeof(UVRecord));
            free( gModel.uvSwatches[swatchLoc].records );
            gModel.uvSwatches[swatchLoc].records = records;
            gModel.uvSwatches[swatchLoc].size = newSize;
        }
    }

    // OK, save the new pair and return the index
    gModel.uvSwatches[swatchLoc].count++;
    uvr = &gModel.uvSwatches[swatchLoc].records[count];
    uvr->u = u;
    uvr->v = v;
    uvr->index = gModel.uvIndexCount;

    // now save it in the master list, which is what actually gets output
    if ( gModel.uvIndexCount == gModel.uvIndexListSize )
    {
        // resize time
        UVOutput *output;
        int newSize = (int)(gModel.uvIndexListSize * 1.4 + 1);
        output = (UVOutput*)malloc(newSize*sizeof(UVOutput));
        memcpy( output, gModel.uvIndexList, gModel.uvIndexCount*sizeof(UVOutput));
        free( gModel.uvIndexList );
        gModel.uvIndexList = output;
        gModel.uvIndexListSize = newSize;
    }
    // convert to stored uv's
    SWATCH_TO_COL_ROW( swatchLoc, col, row );

    gModel.uvIndexList[gModel.uvIndexCount].uc = (float)col * gModel.textureUVPerSwatch + u * gModel.textureUVPerTile + gModel.invTextureResolution;
    gModel.uvIndexList[gModel.uvIndexCount].vc = 1.0f - ((float)row * gModel.textureUVPerSwatch + (1.0f-v) * gModel.textureUVPerTile + gModel.invTextureResolution);
    gModel.uvIndexList[gModel.uvIndexCount].swatchLoc = swatchLoc;
    gModel.uvIndexCount++;

    // also save what type is associated with this swatchLoc, to allow output of name in comments.
    // Multiple types can be associated with the same swatchLoc, we just save the last one (often the most
    // visible one) here.
    gModel.uvSwatchToType[swatchLoc] = type;

    return uvr->index;
}


static void freeModel(Model *pModel)
{
    if ( pModel->vertices )
    {
        free(pModel->vertices);
        pModel->vertices = NULL;
    }
    if ( pModel->vertexIndices )
    {
        free(pModel->vertexIndices);
        pModel->vertexIndices = NULL;
    }
    if ( pModel->faceList )
    {
        FaceRecordPool *pPool = gModel.faceRecordPool;
        int i = 0;
        while (pPool)
        {
            UPDATE_PROGRESS( PG_CLEANUP + 0.9f*(PG_END-PG_CLEANUP)*((float)i/(float)pModel->faceCount));
            i += FACE_RECORD_POOL_SIZE;
            FaceRecordPool *pPrev = pPool->pPrev;
            free( pPool );
            pPool = pPrev;
        }
        gModel.faceRecordPool = NULL;

        // about a hundred times slower, without a pool:
        //int i;
        //for ( i = 0; i < pModel->faceCount; i++ )
        //{
        //    if ( i % 10000 == 0 )
        //        UPDATE_PROGRESS( PG_CLEANUP + 0.9f*(PG_END-PG_CLEANUP)*((float)i/(float)pModel->faceCount));
        //    assert( pModel->faceList[i] );
        //    free( pModel->faceList[i] );
        //}
        free(pModel->faceList);
        pModel->faceList = NULL;
        pModel->faceSize = 0;
    }

    if ( pModel->uvIndexList )
    {
        int i;
        free(pModel->uvIndexList);
        pModel->uvIndexList = NULL;

        // free all per-swatch UVRecord lists
        for ( i = 0; i < NUM_MAX_SWATCHES; i++ )
        {
            free(pModel->uvSwatches[i].records);
            pModel->uvSwatches[i].records = NULL;
        }
    }

    if (pModel->pInputTerrainImage)
    {
        delete pModel->pInputTerrainImage;
        pModel->pInputTerrainImage = NULL;
    }

    if (pModel->pPNGtexture)
    {
        delete pModel->pPNGtexture;
        pModel->pPNGtexture = NULL;
    }
}

static int findMatchingNormal( FaceRecord *pFace, Vector normal, Vector *normalList, int normalListCount )
{
    // compute the normal for a face
    Vector edge1, edge2;
    int index0 = pFace->vertexIndex[0];
    Vec3Op( edge1, =, gModel.vertices[index0], -, gModel.vertices[pFace->vertexIndex[1]] );
    Vec3Op( edge2, =, gModel.vertices[index0], -, gModel.vertices[pFace->vertexIndex[2]] );
    VecCross( normal, =, edge1, X, edge2 );
    float vecdot = VecDot( normal, normal );
    if ( vecdot == 0.0f )
    {
        // the triangle has no area; assert, return index of 0 since it doesn't matter
        assert(0);
        return 0;
    }
    vecdot = 1.0f/sqrt(vecdot);
    VecScalar( normal, *=, vecdot );
    
    // find a close match for the normal
    for ( int i = 0; i < normalListCount; i++ )
    {
        vecdot = VecDot( normal, normalList[i] );
        if ( vecdot > 0.999f )
        {
            // good match! See how much tolerance is needed
            static float minMatch = 1.0f;
            if ( vecdot < minMatch )
                minMatch = vecdot;

            return i;
        }
    }

    // signal no match found
    return COMPUTE_NORMAL;
}

static int addNormalToList( Vector normal, Vector *normalList, int *normalListCount, int normalListSize )
{
    if ( *normalListCount == normalListSize )
    {
        // out of room! make normalList longer
        assert(0);
        // we return the last normal, since normally (hah) we'd run out only due to really complex water
        // and lava top patterns. The last normal computed is probably fine for everything else...
        return (*normalListCount)-1;
    }
    // add to end of list
    Vec2Op( normalList[*normalListCount], =, normal );
    (*normalListCount)++;

    return (*normalListCount)-1;
}

static void resolveFaceNormals()
{
    for ( int i = 0; i < gModel.faceCount; i++ )
    {
        // output the actual face
        FaceRecord *pFace = gModel.faceList[i];
        if ( pFace->normalIndex == COMPUTE_NORMAL )
        {
            // Object may have undergone a transform that changes its normal, so we need to compute
            // the normal now. First try to retrieve it from the list, then add a new one if not found.
            Vector normal;
            pFace->normalIndex = findMatchingNormal( pFace, normal, gModel.normals, gModel.normalListCount );

            // Check if a good match was not found; need to save a new normal if not
            if ( pFace->normalIndex == COMPUTE_NORMAL )
            {
                pFace->normalIndex = addNormalToList( normal, gModel.normals, &gModel.normalListCount, NORMAL_LIST_SIZE );
            }
            assert( pFace->normalIndex != COMPUTE_NORMAL );
        }
// to test if the normal direction saved by the code is in agreement with the normal direction computed.
// We could simply take out the normalIndex == COMPUTE_NORMAL test at the start and have every normal get computed and
// checked, but that would slow things down a bit (probably not all that much...). Anyway, all blocks check out.
//#ifdef _DEBUG
//        else
//        {
//            Vector tnormal;
//            int index = findMatchingNormal( pFace, tnormal, gModel.normals, gModel.normalListCount );
//            assert( pFace->normalIndex == index );
//        }
//#endif
    }
}

// return 0 if no write
static int writeOBJBox( const wchar_t *world, IBox *worldBox, const wchar_t *curDir, const wchar_t *terrainFileName )
{
    // set to 1 if you want absolute (positive) indices used in the faces
    int absoluteIndices = (gOptions->exportFlags & EXPT_OUTPUT_OBJ_REL_COORDINATES) ? 0 : 1;

#ifdef WIN32
    DWORD br;
#endif
    wchar_t objFileNameWithSuffix[MAX_PATH];

    char outputString[MAX_PATH];
    char mtlName[MAX_PATH];
    const char *justWorldFileName;
    char justMtlFileName[MAX_PATH];

    int i, groupCount;

    unsigned char outputMaterial[NUM_BLOCKS];

    int exportMaterials;

    int retCode = MW_NO_ERROR;

    char worldNameUnderlined[256];
    int prevType;

    FaceRecord *pFace;

    char worldChar[MAX_PATH];
    char outChar[MAX_PATH];

#define OUTPUT_NORMALS
#ifdef OUTPUT_NORMALS
    int outputFaceDirection;
#endif

    exportMaterials = gOptions->exportFlags & EXPT_OUTPUT_MATERIALS;

    concatFileName3(objFileNameWithSuffix,gOutputFilePath,gOutputFileRoot,L".obj");

    // create the Wavefront OBJ file
    //DeleteFile(objFileNameWithSuffix);
    gModelFile=PortaCreate(objFileNameWithSuffix);
    addOutputFilenameToList(objFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return retCode|MW_CANNOT_CREATE_FILE;

    wcharToChar(world,worldChar);	// don't touch worldChar after this, as justWorldFileName depends on it
    justWorldFileName = removePathChar(worldChar);

    sprintf_s(outputString,256,"# Wavefront OBJ file made by Mineways version %d.%d, http://mineways.com\n", gMajorVersion, gMinorVersion );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    retCode |= writeStatistics( gModelFile, justWorldFileName, worldBox );
    if ( retCode >= MW_BEGIN_ERRORS )
        goto Exit;

    // Debug info, to figure out Mac paths:
    sprintf_s(outputString,256,"\n# Full world path: %s\n", worldChar );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    wcharToChar(terrainFileName,outChar);
    sprintf_s(outputString,256,"# Full terrainExt.png path: %s\n", outChar );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    wcharToChar(curDir,outChar);
    sprintf_s(outputString,256,"# Full current path: %s\n", outChar );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));


    // If we use materials, say where the file is
    if ( exportMaterials )
    {
        sprintf_s(justMtlFileName,MAX_PATH,"%s.mtl",gOutputFileRootCleanChar);

        sprintf_s(outputString,256,"\nmtllib %s\n", justMtlFileName );
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
    }

    // replace spaces with underscores for world name output
    strcpy_s(worldNameUnderlined,256,justWorldFileName);
    spacesToUnderlinesChar(worldNameUnderlined);

    // Object name
    sprintf_s(outputString,256,"\no %s__%d_%d_%d_to_%d_%d_%d\n", worldNameUnderlined,
        worldBox->min[X], worldBox->min[Y], worldBox->min[Z],
        worldBox->max[X], worldBox->max[Y], worldBox->max[Z] );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

#ifdef OUTPUT_NORMALS
    resolveFaceNormals();

    // write out normals, texture coordinates, vertices, and then faces grouped by material
    for ( i = 0; i < gModel.normalListCount; i++ )
    {
        sprintf_s(outputString,256,"vn %g %g %g\n", gModel.normals[i][0], gModel.normals[i][1], gModel.normals[i][2]);
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
    }
#endif

    if ( gExportTexture )
    {
        int prevSwatch = -1;
        for ( i = 0; i < gModel.uvIndexCount; i++ )
        {
            retCode |= writeOBJTextureUV(gModel.uvIndexList[i].uc, gModel.uvIndexList[i].vc, prevSwatch!=gModel.uvIndexList[i].swatchLoc, gModel.uvIndexList[i].swatchLoc);
            prevSwatch = gModel.uvIndexList[i].swatchLoc;
            if (retCode >= MW_BEGIN_ERRORS)
                goto Exit;
        }
    }

    for ( i = 0; i < gModel.vertexCount; i++ )
    {
        if ( i % 1000 == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + 0.5f*(PG_TEXTURE-PG_OUTPUT)*((float)i/(float)gModel.vertexCount));

        sprintf_s(outputString,256,"v %g %g %g\n", gModel.vertices[i][X], gModel.vertices[i][Y], gModel.vertices[i][Z] );
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
    }

    //if ( exportMaterials && (gOptions->exportFlags & EXPT_OUTPUT_NEUTRAL_MATERIAL) )
    //{
    //    sprintf_s(outputString,256,"\ng world\nusemtl object_material\n");
    //    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
    //}

    prevType = -1;
    groupCount = 0;
    // outputMaterial notes when a material is used for the first time;
    // should only be needed for when objects are not sorted by material (grouped by block).
    memset(outputMaterial,0,sizeof(outputMaterial));

    // test for a single material output. If so, do it now and reset materials in general
    if ( exportMaterials )
    {
        // should there be just one single material in this OBJ file?
        if ( !(gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE) )
        {
            sprintf_s(outputString,256,"\nusemtl %s\n", MINECRAFT_SINGLE_MATERIAL);
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
        }
    }

    for ( i = 0; i < gModel.faceCount; i++ )
    {
        if ( i % 1000 == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + 0.5f*(PG_TEXTURE-PG_OUTPUT) + 0.5f*(PG_TEXTURE-PG_OUTPUT)*((float)i/(float)gModel.faceCount));

        if ( exportMaterials )
        {
            // should there be more than one material or group output in this OBJ file?
            if ( gOptions->exportFlags & (EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE|EXPT_OUTPUT_OBJ_GROUPS) )
            {
                // did we reach a new material?
                if ( prevType != gModel.faceList[i]->type )
                {
                    prevType = gModel.faceList[i]->type;
                    // new ID encountered, so output it: material name, and group
                    // group isn't really required, but can be useful.
                    // Output group only if we're not already using it for individual blocks
                    strcpy_s(mtlName,256,gBlockDefinitions[prevType].name);

                    // substitute ' ' to '_'
                    spacesToUnderlinesChar( mtlName );
                    // usemtl materialName
                    if ( gOptions->exportFlags & EXPT_GROUP_BY_BLOCK )
                    {
                        sprintf_s(outputString,256,"\nusemtl %s\n", mtlName);
                        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
                        // note which material is to be output, if not output already
                        if ( outputMaterial[prevType] == 0 )
                        {
                            gModel.mtlList[gModel.mtlCount++] = prevType;
                            outputMaterial[prevType] = 1;
                        }
                    }
                    else
                    {
                        strcpy_s(outputString,256,"\n");
                        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

                        if ( gOptions->exportFlags & EXPT_OUTPUT_OBJ_GROUPS )
                        {
                            sprintf_s(outputString,256,"g %s\n", mtlName);
                            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
                        }
                        if ( gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE )
                        {
                            sprintf_s(outputString,256,"usemtl %s\n", mtlName);
                            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
                            gModel.mtlList[gModel.mtlCount++] = prevType;
                        }
                        // else don't output material
                    }
                }
            }
        }

        // output the actual face
        pFace = gModel.faceList[i];

        // if we're outputting each individual block, set a unique group name here.
        if ( (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK) && pFace->faceIndex <= 0 )
        {
            sprintf_s(outputString,256,"\ng block_%05d\n", ++groupCount);
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
        }

#ifdef OUTPUT_NORMALS
        if ( absoluteIndices )
        {
            outputFaceDirection = pFace->normalIndex+1;
        }
        else
        {
            outputFaceDirection = pFace->normalIndex-gModel.normalListCount;
        }
#endif

        // first, are there texture coordinates?
        if ( gExportTexture )
        {
#ifdef OUTPUT_NORMALS
            // with normals - not really needed by most renderers, but good to include;
            // GLC, for example, does smoothing if normals are not present.
            // Check if last two vertices match - if so, output a triangle instead 
            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // triangle
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]+1, pFace->uvIndex[0]+1, outputFaceDirection,
                        pFace->vertexIndex[1]+1, pFace->uvIndex[1]+1, outputFaceDirection,
                        pFace->vertexIndex[2]+1, pFace->uvIndex[2]+1, outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, pFace->uvIndex[0]-gModel.uvIndexCount, outputFaceDirection,
                        pFace->vertexIndex[1]-gModel.vertexCount, pFace->uvIndex[1]-gModel.uvIndexCount, outputFaceDirection,
                        pFace->vertexIndex[2]-gModel.vertexCount, pFace->uvIndex[2]-gModel.uvIndexCount, outputFaceDirection
                        );
                }
            }
            else
            {
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]+1, pFace->uvIndex[0]+1, outputFaceDirection,
                        pFace->vertexIndex[1]+1, pFace->uvIndex[1]+1, outputFaceDirection,
                        pFace->vertexIndex[2]+1, pFace->uvIndex[2]+1, outputFaceDirection,
                        pFace->vertexIndex[3]+1, pFace->uvIndex[3]+1, outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, pFace->uvIndex[0]-gModel.uvIndexCount, outputFaceDirection,
                        pFace->vertexIndex[1]-gModel.vertexCount, pFace->uvIndex[1]-gModel.uvIndexCount, outputFaceDirection,
                        pFace->vertexIndex[2]-gModel.vertexCount, pFace->uvIndex[2]-gModel.uvIndexCount, outputFaceDirection,
                        pFace->vertexIndex[3]-gModel.vertexCount, pFace->uvIndex[3]-gModel.uvIndexCount, outputFaceDirection
                        );
                }
            }
#else
            // check if last two vertices match - if so, output a triangle instead 
            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // triangle
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]+1, pFace->uvIndex[0]+1,
                        pFace->vertexIndex[1]+1, pFace->uvIndex[1]+1,
                        pFace->vertexIndex[2]+1, pFace->uvIndex[2]+1
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, pFace->uvIndex[0]-gModel.uvIndexCount,
                        pFace->vertexIndex[1]-gModel.vertexCount, pFace->uvIndex[1]-gModel.uvIndexCount,
                        pFace->vertexIndex[2]-gModel.vertexCount, pFace->uvIndex[2]-gModel.uvIndexCount
                        );
                }
            }
            else
            {
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]+1, pFace->uvIndex[0]+1,
                        pFace->vertexIndex[1]+1, pFace->uvIndex[1]+1,
                        pFace->vertexIndex[2]+1, pFace->uvIndex[2]+1,
                        pFace->vertexIndex[3]+1, pFace->uvIndex[3]+1
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, pFace->uvIndex[0]-gModel.uvIndexCount,
                        pFace->vertexIndex[1]-gModel.vertexCount, pFace->uvIndex[1]-gModel.uvIndexCount,
                        pFace->vertexIndex[2]-gModel.vertexCount, pFace->uvIndex[2]-gModel.uvIndexCount,
                        pFace->vertexIndex[3]-gModel.vertexCount, pFace->uvIndex[3]-gModel.uvIndexCount
                        );
                }
            }
#endif
        }
        else
        {
#ifdef OUTPUT_NORMALS
            // check if last two vertices match - if so, output a triangle instead 
            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // triangle
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[0]+1, outputFaceDirection,
                        pFace->vertexIndex[1]+1, outputFaceDirection,
                        pFace->vertexIndex[2]+1, outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[1]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[2]-gModel.vertexCount, outputFaceDirection
                        );
                }
            }
            else
            {
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[0]+1, outputFaceDirection,
                        pFace->vertexIndex[1]+1, outputFaceDirection,
                        pFace->vertexIndex[2]+1, outputFaceDirection,
                        pFace->vertexIndex[3]+1, outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[1]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[2]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[3]-gModel.vertexCount, outputFaceDirection
                        );
                }
            }
#else
            // check if last two vertices match - if so, output a triangle instead 
            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // triangle
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d %d %d\n",
                        pFace->vertexIndex[0]+1,
                        pFace->vertexIndex[1]+1,
                        pFace->vertexIndex[2]+1
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d %d %d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount,
                        pFace->vertexIndex[1]-gModel.vertexCount,
                        pFace->vertexIndex[2]-gModel.vertexCount
                        );
                }
            }
            else
            {
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d %d %d %d\n",
                        pFace->vertexIndex[0]+1,
                        pFace->vertexIndex[1]+1,
                        pFace->vertexIndex[2]+1,
                        pFace->vertexIndex[3]+1
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d %d %d %d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount,
                        pFace->vertexIndex[1]-gModel.vertexCount,
                        pFace->vertexIndex[2]-gModel.vertexCount,
                        pFace->vertexIndex[3]-gModel.vertexCount
                        );
                }
            }
#endif
        }
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
    }

Exit:
    PortaClose(gModelFile);

    // should we call it a day here?
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    // write materials file
    if ( exportMaterials )
    {
        // write material file
        retCode |= writeOBJMtlFile();
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;
    }

    return retCode;
}


static int writeOBJTextureUV( float u, float v, int addComment, int swatchLoc )
{
#ifdef WIN32
    DWORD br;
#endif
    char outputString[1024];

    if ( addComment )
    {
        sprintf_s(outputString,1024,"# %s\nvt %g %g\n",
            gBlockDefinitions[gModel.uvSwatchToType[swatchLoc]].name,
            u, v );
    }
    else
    {
        sprintf_s(outputString,1024,"vt %g %g\n",
            u, v );
    }
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    return MW_NO_ERROR;
}


static int writeOBJMtlFile()
{
#ifdef WIN32
    DWORD br;
#endif
    wchar_t mtlFileName[MAX_PATH];
    char outputString[1024];

    char textureRGB[MAX_PATH];
    char textureRGBA[MAX_PATH];
    char textureAlpha[MAX_PATH];

    concatFileName3(mtlFileName, gOutputFilePath, gOutputFileRootClean, L".mtl");

    gMtlFile=PortaCreate(mtlFileName);
    addOutputFilenameToList(mtlFileName);
    if (gMtlFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

    sprintf_s(outputString,1024,"Wavefront OBJ material file\n# Contains %d materials\n",
        (gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE) ? gModel.mtlCount : 1 );
    WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString) ));

    if (gExportTexture )
    {
        // Write them out! We need three texture file names: -RGB, -RGBA, -Alpha.
        // The RGB/RGBA split is needed for fast previewers like G3D to gain additional speed
        // The all-alpha image is needed for various renderers to properly read cutouts, since map_d is poorly defined
        sprintf_s(textureRGB,MAX_PATH,"%s%s.png",gOutputFileRootCleanChar,PNG_RGB_SUFFIXCHAR);
        sprintf_s(textureRGBA,MAX_PATH,"%s%s.png",gOutputFileRootCleanChar,PNG_RGBA_SUFFIXCHAR);
        sprintf_s(textureAlpha,MAX_PATH,"%s%s.png",gOutputFileRootCleanChar,PNG_ALPHA_SUFFIXCHAR);
    }

    if ( !(gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE) )
    {
        // output a single material
        if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
        {
            if ( gPrint3D )
            {
                gModel.usesRGB = 1;
            }
            else
            {
                // cutouts or alpha
                // we could be more clever here and go through all the materials to see which are needed,
                // but we'd basically have to copy the code further below for who has what alpha, etc.
                gModel.usesRGBA = 1;
                gModel.usesAlpha = 1;
            }

            sprintf_s(outputString,1024,
                "\nnewmtl %s\n"
                "Kd 1 1 1\n"
                "Ks 0 0 0\n"
                "map_Kd %s\n"
                "map_d %s\n"
                ,
                MINECRAFT_SINGLE_MATERIAL,
                textureRGBA,
                textureAlpha );
        }
        else
        {
            sprintf_s(outputString,1024,
                "\nnewmtl %s\n"
                "Kd 1 1 1\n"
                "Ks 0 0 0\n"
                ,
                MINECRAFT_SINGLE_MATERIAL );
        }
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    }
    else
    {
        // output materials
        int i;
        for ( i = 0; i < gModel.mtlCount; i++ )
        {
            int type;
            char tfString[256];
            char mapdString[256];
            char mapKeString[256];
            char keString[256];
            char mtlName[MAX_PATH];
            char *typeTextureFileName;
            char fullMtl[256];
            double alpha;
            double fRed,fGreen,fBlue;
            double ka, kd;

            type = gModel.mtlList[i];

            if ( gOptions->exportFlags & EXPT_OUTPUT_OBJ_FULL_MATERIAL )
            {
                // use full material description, and include illumination model.
                // Really currently tailored for G3D, and things like Tr are commented out always.
                strcpy_s(fullMtl,256,"");
            }
            else
            {
                // don't use full material, comment it out, just output the basics
                strcpy_s(fullMtl,256,"# ");
            }

            // print header: material name
            strcpy_s(mtlName,256,gBlockDefinitions[type].name);
            spacesToUnderlinesChar(mtlName);

            // if we want a neutral material, set to white
            // was: if (gOptions->exportFlags & EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL)
            // In fact, texture should be multiplied by color, according to the spec: http://paulbourke.net/dataformats/mtl/
            // So use a white color if we're outputting a texture, since it could get multiplied. This
            // will make Blender previewing look all-white, but people should turn on texturing anyway.
            if (gOptions->exportFlags & (EXPT_OUTPUT_TEXTURE_IMAGES|EXPT_OUTPUT_TEXTURE_SWATCHES))
            {
                fRed = fGreen = fBlue = 1.0f;
            }
            else
            {
                // use color in file, which is nice for previewing. Blender and 3DS MAX like this, for example. G3D does not.
                fRed = (gBlockDefinitions[type].color >> 16)/255.0f;
                fGreen = ((gBlockDefinitions[type].color >> 8) & 0xff)/255.0f;
                fBlue = (gBlockDefinitions[type].color & 0xff)/255.0f;
            }

            // good for blender:
            ka = 0.2;
            kd = 1.0;
            // 3d printers cannot print semitransparent surfaces, so set alpha to 1.0 so what you preview
            // is what you get. TODO Should we turn off alpha for textures, as the textures themselves will have alpha in them - this is
            // in case the model viewer tries to multiply alpha by texture; also, VRML just has one material for textures, generic.
            // Really, we could have no colors at all when textures are output, but the colors are useful for previewers that
            // do not support textures (e.g. Blender).
            //alpha = ( gPrint3D || (gExportTexture)) ? 1.0f : gBlockDefinitions[type].alpha;
            // Well, hmmm, alpha is useful in previewing (no textures displayed), at least for OBJ files
            // alpha = gPrint3D ? 1.0f : gBlockDefinitions[type].alpha;
            alpha = gBlockDefinitions[type].alpha;
            if (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS)
            {
                // if showing groups, make the alpha of the largest group transparent
                if ( gDebugTransparentType == type )
                {
                    alpha = DEBUG_DISPLAY_ALPHA;
                }
                else
                {
                    alpha = 1.0f;
                }
            }
            else if ( gPrint3D )
            {
                // for 3d printing, alpha is always 1.0
                alpha = 1.0f;
            }
            // if semitransparent, and a truly transparent thing, then alpha is used; otherwise it's probably a cutout and the overall alpha should be 1.0f for export
            // (this is true for glass panes, but stained glass pains are semitransparent)
            if ( alpha < 1.0f && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) && !(gBlockDefinitions[type].flags & BLF_TRANSPARENT) )
            {
                alpha = 1.0f;
            }

            if ( alpha < 1.0f )
            {
                // semitransparent block, such as water
                gModel.usesRGBA = 1;
                gModel.usesAlpha = 1;
                sprintf_s(tfString,256,"%sTf %g %g %g\n", fullMtl, 1.0f-(float)(fRed*alpha), 1.0f-(float)(fGreen*alpha), 1.0f-(float)(fBlue*alpha) );
            }
            else
            {
                tfString[0] = '\0';
            }

            // export map_d only if CUTOUTS.
            if (!gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) && (alpha < 1.0 || (gBlockDefinitions[type].flags & BLF_CUTOUTS)) 
                && !( gOptions->pEFD->chkLeavesSolid && (gBlockDefinitions[type].flags & BLF_LEAF_PART) ) )
            {
                // cutouts or alpha
                gModel.usesRGBA = 1;
                gModel.usesAlpha = 1;
                typeTextureFileName = textureRGBA;
                sprintf_s(mapdString,256,"map_d %s\n", textureAlpha );
            }
            else
            {
                gModel.usesRGB = 1;
                if ( gExportTexture )
                {
                    typeTextureFileName = textureRGB;
                }
                else
                {
                    typeTextureFileName = '\0'; 
                }
                mapdString[0] = '\0';
            }
            if (!gPrint3D && (gBlockDefinitions[type].flags & BLF_EMITTER) )
            {
                // emitter: for G3D, make it 2x as bright to get bloom effect
                if ( gOptions->exportFlags & EXPT_OUTPUT_OBJ_FULL_MATERIAL )
                {
                    sprintf_s(keString,256,"Ke 2 2 2\n" );
                }
                else
                {
                    sprintf_s(keString,256,"Ke 1 1 1\n" );
                }
                if ( gExportTexture )
                {
                    sprintf_s(mapKeString,256,"map_Ke %s\n", typeTextureFileName );
                }
                else
                {
                    mapKeString[0] = '\0';
                }
            }
            else
            {
                keString[0] = '\0';
                mapKeString[0] = '\0';
            }

            if (gExportTexture)
            {
                sprintf_s(outputString,1024,
                    "\nnewmtl %s\n"
                    "%sNs 0\n"	// specular highlight power
                    "%sKa %g %g %g\n"
                    "Kd %g %g %g\n"
                    "Ks 0 0 0\n"
                    "%s" // emissive
                    "%smap_Ka %s\n"
                    "map_Kd %s\n"
                    "%s" // map_d, if there's a cutout
                    "%s"	// map_Ke
                    // "Ni 1.0\n" - Blender likes to output this - no idea what it is
                    "%sillum %d\n"
                    "# d %g\n"	// some use Tr here - Blender likes "d"
                    "# Tr %g\n"	// we put both, in hopes of helping both types of importer; comment out one, as 3DS MAX doesn't like it
                    "%s"	//Tf, if needed
                    ,
                    // colors are premultiplied by alpha, Wavefront OBJ doesn't want that
                    mtlName,
                    fullMtl,
                    fullMtl,(float)(fRed*ka), (float)(fGreen*ka), (float)(fBlue*ka), 
                    (float)(fRed*kd), (float)(fGreen*kd), (float)(fBlue*kd),
                    keString,
                    fullMtl,typeTextureFileName,
                    typeTextureFileName,
                    mapdString,
                    mapKeString,
                    fullMtl,(alpha < 1.0f ? 4 : 2), // ray trace if transparent overall, e.g. water
                    (float)(alpha),
                    (float)(alpha),
                    tfString);
            }
            else
            {
                sprintf_s(outputString,1024,
                    "\nnewmtl %s\n"
                    "%sNs 0\n"	// specular highlight power
                    "%sKa %g %g %g\n"
                    "Kd %g %g %g\n"
                    "Ks 0 0 0\n"
                    "%s%s" // emissive
                    // "Ni 1.0\n" - Blender likes to output this - no idea what it is
                    "%sillum %d\n"
                    "d %g\n"	// some use Tr here - Blender likes "d"
                    "Tr %g\n"	// we put both, in hopes of helping both types of importer; comment out one, as 3DS MAX doesn't like it
                    "%s%s\n"	//Tf, if needed
                    ,
                    // colors are premultiplied by alpha, Wavefront OBJ doesn't want that
                    mtlName,
                    fullMtl,
                    fullMtl,(float)(fRed*ka), (float)(fGreen*ka), (float)(fBlue*ka), 
                    (float)(fRed*kd), (float)(fGreen*kd), (float)(fBlue*kd),
                    fullMtl,keString,
                    fullMtl,(alpha < 1.0f ? 4 : 2), // ray trace if transparent overall, e.g. water
                    (float)(alpha),
                    (float)(alpha),
                    fullMtl,tfString);
            }
            WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString) ));
        }
    }

    PortaClose(gMtlFile);

    return MW_NO_ERROR;
}


// all the blocks that need premultiplication by a color.
// See http://www.minecraftwiki.net/wiki/File:TerrainGuide.png
#define MULT_TABLE_SIZE 24
#define MULT_TABLE_NUM_GRASS	8
#define MULT_TABLE_NUM_FOLIAGE	(MULT_TABLE_NUM_GRASS+4)
#define MULT_TABLE_NUM_WATER	(MULT_TABLE_NUM_FOLIAGE+1)
static TypeTile multTable[MULT_TABLE_SIZE] = {
    { BLOCK_GRASS /* grass */, 0,0, {0,0,0} },
    { BLOCK_GRASS /* side grass overlay */, 6, 2, {0,0,0} },
    //{ BLOCK_GRASS /* unused grass, now a workspace */, 8, 2, {0,0,0} },
    { BLOCK_TALL_GRASS /* tall grass */, 7, 2, {0,0,0} },
    { BLOCK_TALL_GRASS /* fern */, 8, 3, {0,0,0} },
    { BLOCK_DOUBLE_FLOWER /* double flower, tallgrass bottom */, 6,18, {0,0,0} },
    { BLOCK_DOUBLE_FLOWER /* double flower, tallgrass top */, 7,18, {0,0,0} },
    { BLOCK_DOUBLE_FLOWER /* double flower, fern bottom */, 8,18, {0,0,0} },
    { BLOCK_DOUBLE_FLOWER /* double flower, fern top */, 9,18, {0,0,0} },

    // affected by foliage biome
    { BLOCK_LEAVES /* leaves, fancy */, 4, 3, {0,0,0} },
    { BLOCK_LEAVES /* jungle leaves, fancy */, 4,12, {0,0,0} },
    { BLOCK_AD_LEAVES /* acacia leaves, fancy */,  9,19, {0,0,0} },
    { BLOCK_AD_LEAVES /* dark oak leaves, fancy */, 11,19, {0,0,0} },

    // water - possibly affected by swampland
    { BLOCK_WATER /* water */, 15,13, {0,0,0} },

    /////////////////////////////
    // not affected by biomes

    // These two have fixed colors, unchangeable in Minecraft (and there are no controls in Mineways, because of this)
    { BLOCK_LEAVES /* spruce leaves fancy */, 4, 8, {61,98,61} },	// 0x3D623D
    { BLOCK_LEAVES /* birch leaves, fancy */, 13,13, {107,141,70} },	// 0x6B8D46

    { BLOCK_LILY_PAD /* lily pad */, 12, 4, {0,0,0} },
    { BLOCK_MELON_STEM /* melon stem */, 14,11, {0,0,0} },
    { BLOCK_MELON_STEM /* melon stem, matured */, 15,11, {0,0,0} }, /* TODO: probably want a different color, a yellow? */
    { BLOCK_PUMPKIN_STEM /* pumpkin stem */, 15, 6, {0,0,0} },
    { BLOCK_PUMPKIN_STEM /* pumpkin stem, matured */, 15, 7, {0,0,0} }, /* TODO: probably want a different color, a yellow? */
    { BLOCK_VINES /* vines */, 15, 8, {0,0,0} },
    { BLOCK_REDSTONE_WIRE /* redstone wire */, 4,10, {0,0,0} },
    { BLOCK_REDSTONE_WIRE /* redstone wire */, 5,10, {0,0,0} },
    { BLOCK_REDSTONE_TORCH_ON /* redstone */, 4,11, {0,0,0} },	// manufactured redstone dot REDSTONE_WIRE_DOT
};

// the blocks that should be solid if valid water tile is not found
int solidCount = 5;
static int solidTable[] = { BLOCK_WATER, BLOCK_STATIONARY_WATER, BLOCK_LAVA, BLOCK_STATIONARY_LAVA, BLOCK_FIRE };

// Create basic composite files for flattops and flatsides with alpha cutouts in them
// ladder, trapdoor, three torches, two rails, powered rail, detector rail, lily, wire
#define COMPOSITE_TABLE_SIZE 23
CompositeSwatchPreset compositeTable[COMPOSITE_TABLE_SIZE] =
{ 
    /* MUST BE FIRST IN LIST, code rotates this one - rotated */ { SWATCH_INDEX( 5, 10 ), /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // wire over stone, vertical
    { SWATCH_INDEX( 0, 8 ), /* BLOCK_RAILS */ SWATCH_INDEX( 1, 0 ) }, // rail over stone
    { SWATCH_INDEX( 0, 7 ), /* BLOCK_RAILS */ SWATCH_INDEX( 1, 0 ) }, // curved rail over stone
    { SWATCH_INDEX( 0, 5 ), /* BLOCK_TORCH */ SWATCH_INDEX( 1, 0 ) }, // torch over stone
    { TORCH_TOP, /* BLOCK_TORCH */ SWATCH_INDEX( 1, 0 ) }, // torch top over stone
    { SWATCH_INDEX( 4, 10 ), /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // wire over stone
    { SWATCH_INDEX( 5, 10 ), /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // 2-wire straight over stone
    { SWATCH_INDEX( 4, 11 ), /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // no-wire dot over stone (some texture packs don't use this?)
    { REDSTONE_WIRE_ANGLED_2, /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // 2-wire angled over stone
    { REDSTONE_WIRE_3, /* BLOCK_REDSTONE_WIRE */ SWATCH_INDEX( 1, 0 ) }, // 3-wire over stone
    { SWATCH_INDEX( 3, 5 ), /* BLOCK_LADDER */ SWATCH_INDEX( 1, 0 ) }, // ladder over stone
    { SWATCH_INDEX( 3, 11 ), /* BLOCK_POWERED_RAIL */ SWATCH_INDEX( 1, 0 ) }, // powered rail over stone
    { SWATCH_INDEX( 3, 10 ), /* BLOCK_POWERED_RAIL */ SWATCH_INDEX( 1, 0 ) }, // unpowered rail over stone
    { SWATCH_INDEX( 3, 12 ), /* BLOCK_DETECTOR_RAIL */ SWATCH_INDEX( 1, 0 ) }, // detector rail over stone - TODO add BLOCK_ACTIVATOR_RAIL, and add activated vs. non-activated
    { SWATCH_INDEX( 3, 6 ), /* BLOCK_REDSTONE_TORCH_ON */ SWATCH_INDEX( 1, 0 ) }, // redstone torch on over stone
    { RS_TORCH_TOP_ON, /* BLOCK_REDSTONE_TORCH_ON */ SWATCH_INDEX( 1, 0 ) }, // redstone torch on over stone
    { SWATCH_INDEX( 3, 7 ), /* BLOCK_REDSTONE_TORCH_OFF */ SWATCH_INDEX( 1, 0 ) }, // redstone torch off over stone
    { RS_TORCH_TOP_OFF, /* BLOCK_REDSTONE_TORCH_OFF */ SWATCH_INDEX( 1, 0 ) }, // redstone torch off over stone
    { SWATCH_INDEX( 0, 6 ), /* BLOCK_LEVER */ SWATCH_INDEX( 1, 0 ) }, // lever over stone
    { SWATCH_INDEX( 12, 4 ), /* BLOCK_LILY_PAD */ SWATCH_INDEX( 15,13 ) }, // lily pad over water
    { SWATCH_INDEX( 4, 5 ), /* BLOCK_TRAPDOOR */ SWATCH_INDEX( 1, 0 ) }, // trapdoor over stone
    { SWATCH_INDEX( 2, 22 ), /* BLOCK_IRON_TRAPDOOR */ SWATCH_INDEX( 1, 0 ) }, // iron trapdoor over stone
    { SWATCH_INDEX( 15, 8 ), /* BLOCK_VINES */ SWATCH_INDEX( 1, 0 ) }, // vines over stone
};

//#define FA_TABLE_SIZE 55
//#define FA_TABLE__VIEW_SIZE 17
//    static FillAlpha faTable[FA_TABLE_SIZE] =
//    {
//        // stuff that is put in always, fringes that need to be filled
//        { SWATCH_INDEX( 5, 9 ), BLOCK_OBSIDIAN }, // bed
//        { SWATCH_INDEX( 6, 9 ), BLOCK_OBSIDIAN }, // bed
//        { SWATCH_INDEX( 7, 9 ), BLOCK_OBSIDIAN }, // bed
//        { SWATCH_INDEX( 8, 9 ), BLOCK_OBSIDIAN }, // bed
//        { SWATCH_INDEX( 5, 4 ), BLOCK_CACTUS }, // cactus
//        { SWATCH_INDEX( 6, 4 ), BLOCK_CACTUS }, // cactus
//        { SWATCH_INDEX( 7, 4 ), BLOCK_CACTUS }, // cactus
//        { SWATCH_INDEX( 9, 7 ), BLOCK_CAKE }, // cake
//        { SWATCH_INDEX( 10, 7 ), BLOCK_CAKE }, // cake
//        { SWATCH_INDEX( 11, 7 ), BLOCK_CAKE }, // cake
//        { SWATCH_INDEX( 12, 7 ), BLOCK_CAKE }, // cake
//        { SWATCH_INDEX( 10, 8 ), BLOCK_OBSIDIAN }, // cauldron
//        { SWATCH_INDEX( 10, 9 ), BLOCK_OBSIDIAN }, // cauldron
//        { SWATCH_INDEX( 11, 9 ), BLOCK_OBSIDIAN }, // cauldron
//        { SWATCH_INDEX( 15, 9 ), SWATCH_INDEX(15,10) }, // ender portal (should be filled, but just in case, due to stretch)
//        { SWATCH_INDEX( 15, 14 ), BLOCK_LAVA }, // lava, in case it's not filled
//        { SWATCH_INDEX( 15, 15 ), BLOCK_STATIONARY_LAVA }, // stationary lava, in case it's not present
//
//
//        // stuff filled in for 3D printing only
//        { SWATCH_INDEX( 11, 0 ), SWATCH_INDEX( 6, 3 ) }, // spiderweb over stone block
//        { SWATCH_INDEX( 12, 0 ), SWATCH_INDEX( 0, 0 ) }, // red flower over grass
//        { SWATCH_INDEX( 13, 0 ), SWATCH_INDEX( 0, 0 ) }, // yellow flower over grass
//        { SWATCH_INDEX( 15, 0 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
//        { SWATCH_INDEX( 12, 1 ), SWATCH_INDEX( 0, 0 ) }, // red mushroom over grass
//        { SWATCH_INDEX( 13, 1 ), SWATCH_INDEX( 0, 0 ) }, // brown mushroom over grass
//        { SWATCH_INDEX( 1, 3 ), BLOCK_GLASS }, // glass over glass color
//        { SWATCH_INDEX( 4, 3 ), BLOCK_AIR }, // transparent leaves over air (black) - doesn't really matter, not used when printing anyway
//        { SWATCH_INDEX( 7, 3 ), SWATCH_INDEX( 2, 1 ) }, // dead bush over grass
//        { SWATCH_INDEX( 8, 3 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
//        { SWATCH_INDEX( 1, 4 ), SWATCH_INDEX( 1, 0 ) }, // spawner over stone
//        { SWATCH_INDEX( 9, 4 ), SWATCH_INDEX( 0, 0 ) }, // reeds over grass
//        { SWATCH_INDEX( 15, 4 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
//        { SWATCH_INDEX( 1, 5 ), SWATCH_INDEX( 6, 0 ) }, // wooden door top over slab top
//        { SWATCH_INDEX( 2, 5 ), SWATCH_INDEX( 6, 0 ) }, // door top over slab top
//        { SWATCH_INDEX( 5, 5 ), SWATCH_INDEX( 6, 3 ) }, // iron bars over stone block
//        { SWATCH_INDEX( 8, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 9, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 10, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 11, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 12, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 13, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 14, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 15, 5 ), SWATCH_INDEX( 6, 5 ) }, // crops over farmland
//        { SWATCH_INDEX( 0, 6 ), SWATCH_INDEX( 1, 0 ) }, // lever over stone
//        { SWATCH_INDEX( 15, 6 ), SWATCH_INDEX( 6, 5 ) }, // stem over farmland
//        { SWATCH_INDEX( 15, 7 ), SWATCH_INDEX( 6, 5 ) }, // mature stem over farmland
//        { SWATCH_INDEX( 4, 8 ), BLOCK_AIR }, // leaves over air (black) - doesn't really matter, not used
//        { SWATCH_INDEX( 10, 8 ), BLOCK_AIR }, // cauldron over air (black)
//        { SWATCH_INDEX( 12, 8 ), BLOCK_AIR }, // cake over air (black) - what's this for, anyway?
//        { SWATCH_INDEX( 15, 8 ), SWATCH_INDEX( 0, 0 ) }, // vines over grass
//        { SWATCH_INDEX( 4, 9 ), BLOCK_GLASS }, // glass pane over glass color (more interesting than stone, and lets you choose)
//        { SWATCH_INDEX( 13, 9 ), SWATCH_INDEX( 1, 0 ) }, // brewing stand over stone
//        { SWATCH_INDEX( 15, 10 ), SWATCH_INDEX( 1, 0 ) }, // end portal thingy (unused) over stone
//        { SWATCH_INDEX( 4, 12 ), BLOCK_AIR }, // jungle leaves over air (black) - doesn't really matter, not used
//        { SWATCH_INDEX( 2, 14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
//        { SWATCH_INDEX( 3, 14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
//        { SWATCH_INDEX( 4, 14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
//    };

static int createBaseMaterialTexture()
{
    int row,col,srow,scol;
    int keepGoing;
    int i,adj,idx;
    //int faTableCount;
    unsigned char r,g,b,a;
    unsigned int color;
    int useTextureImage;
    int addNoise = 0;

    progimage_info *mainprog;

    mainprog = new progimage_info();

    mainprog->width = gModel.textureResolution;
    mainprog->height = gModel.textureResolution;

    // resize and clear
    mainprog->image_data.resize(gModel.textureResolution*gModel.textureResolution*4*sizeof(unsigned char),0x0);
    // TODO: any way to check if we're out of memory?

    gModel.pPNGtexture = mainprog;

    useTextureImage = (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES);

    // we fill the first NUM_BLOCKS with solid colors
    keepGoing = 1;
    gModel.swatchCount = 0;

    // crazy code: add noise when using swatches, but for VRML we don't add noise if we're
    // actually using material-only or no material modes (VRML always exports textures)
    if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES )
    {
        addNoise = 1;
        // check if VRML, and in anything but rich texture mode
        if ( (gOptions->pEFD->fileType == FILE_TYPE_VRML2) && (!gOptions->pEFD->radioExportSolidTexture[gOptions->pEFD->fileType] ) )
        {
            addNoise = 0;
        }
    }
    for ( row = 0; row < gModel.swatchesPerRow && keepGoing; row++ )
    {
        for ( col = 0; col < gModel.swatchesPerRow && keepGoing; col++ )
        {
            // VRML only uses textures for output: set solid color to white if 
            // no color should be output
            if ( gOptions->pEFD->radioExportNoMaterials[gOptions->pEFD->fileType] )
            {
                r = g = b = a = 255;
                assert(gOptions->pEFD->fileType == FILE_TYPE_VRML2);
            }
            else
            {
                // fill with a solid color
                r=(unsigned char)(gBlockDefinitions[gModel.swatchCount].color>>16);
                g=(unsigned char)((gBlockDefinitions[gModel.swatchCount].color>>8) & 0xff);
                b=(unsigned char)(gBlockDefinitions[gModel.swatchCount].color & 0xff);
                a = (unsigned char)(gBlockDefinitions[gModel.swatchCount].alpha*255.0f);

                if (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS)
                {
                    // make sure group is opaque: it can happen
                    // that the debug group got turned off in a color scheme.
                    a = 255;
                }
                // if the basic solid swatches are to be printed, or to be used as baselines
                // for compositing with texture images, they must be opaque
                else if ( gOptions->exportFlags & (EXPT_3DPRINT|EXPT_OUTPUT_TEXTURE_IMAGES) )
                {
                    a = 255;
                }
            }

            // Order in PNG file is ABGR
            color = (a<<24)|(b<<16)|(g<<8)|r;

            setColorPNGTile( mainprog, col, row, gModel.swatchSize, color );

            if ( addNoise )
            {
                // if we're using a textured swatch, multiply the boring solid colors with some noise
                // to make textures richer
                addNoisePNGTile( mainprog, col, row, gModel.swatchSize, r, g, b, a, 0.1f );
            }

            gModel.swatchCount++;
            keepGoing = (gModel.swatchCount < NUM_BLOCKS_MAP);
        }
    }
    assert( keepGoing == 0 );

    if ( useTextureImage )
    {
        int dstCol,dstRow,j;
        int glassPaneTopsCol[17] = {4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15};
        int glassPaneTopsRow[17] = {9,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21};

        // we then convert *all* 256+ tiles in terrainExt.png to 18x18 or whatever tiles, adding a 1 pixel border (SWATCH_BORDER)
        // around each (since with tile mosaics, we can't clamp to border, nor can we know that the renderer
        // will clamp and get blocky pixels)

        // this count gets used again when walking through to tile
        int currentSwatchCount = gModel.swatchCount;
        for ( row = 0; row < gModel.verticalTiles; row++ )
        {
            for ( col = 0; col < 16; col++ )
            {
                SWATCH_TO_COL_ROW( gModel.swatchCount, dstCol, dstRow );
                // main copy
                copyPNGArea( mainprog, 
                    gModel.swatchSize*dstCol+SWATCH_BORDER, // upper left corner destination
                    gModel.swatchSize*dstRow+SWATCH_BORDER,
                    gModel.tileSize, gModel.tileSize, // width, height to copy
                    gModel.pInputTerrainImage,
                    gModel.tileSize*col, // from
                    gModel.tileSize*row
                    );
                gModel.swatchCount++;
            }
        }

        // Copy the top of each glass pane tile so that if these tiles are fattened the tops look OK
        for ( i = 0; i < 17; i++ )
        {
            SWATCH_TO_COL_ROW( NUM_BLOCKS + glassPaneTopsCol[i] + glassPaneTopsRow[i]*16, dstCol, dstRow );
            for ( j = 1; j < 15; j += 2 )
            {
                if ( j != 7 )
                {
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol+SWATCH_BORDER+gModel.tileSize*j/16,    // copy to right
                        gModel.swatchSize*dstRow+SWATCH_BORDER,  // one down from top
                        gModel.tileSize*2/16, gModel.tileSize,  // 2 tile-texels wide
                        mainprog,
                        gModel.swatchSize*dstCol+SWATCH_BORDER+gModel.tileSize*7/16,
                        gModel.swatchSize*dstRow+SWATCH_BORDER
                        );
                }
            }
        }

        // Now that copy has happened, bleed outwards. This is done when we're using exact geometry for objects,
        // and so only part of the tile is actually used. Its fringe wants to then be the same color, so that interpolation
        // between texels looks better. The problem is that most renderers do not perform interpolation well; they
        // interpolate unassociated PNG samples, when they should interpolate premultiplied colors. I can't fix that from
        // my side of things, but by bleeding the edges of cutouts outwards I can lessen the problem.
        // Bleed these tiles here.
        if ( gOptions->pEFD->chkExportAll )
        {
            // bed top edge
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 5, 9 ), 0, 16, 6, 7, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 6, 9 ), 0, 16, 6, 7, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 7, 9 ), 0, 16, 6, 7, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 8, 9 ), 0, 16, 6, 7, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // cactus top and bottom
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 5, 4 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 7, 4 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // cake
            bleedPNGSwatch( mainprog, SWATCH_INDEX(  9, 7 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 10, 7 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 11, 7 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 12, 7 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // cauldron top
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 10, 8 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // end portal frame
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 15, 9 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // flower pot
            bleedPNGSwatch( mainprog, SWATCH_INDEX( 10, 11 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

            // for printing only:
            if ( gPrint3D )
            {
                // cactus side
                bleedPNGSwatch( mainprog, SWATCH_INDEX( 6, 4 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );

                // cocoa pods
                bleedPNGSwatch( mainprog, SWATCH_INDEX(  8, 10 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
                bleedPNGSwatch( mainprog, SWATCH_INDEX(  9, 10 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
                bleedPNGSwatch( mainprog, SWATCH_INDEX( 10, 10 ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 255 );
            }
        }

        // For rendering (not printing), bleed the transparent *colors* only outwards, leaving the alpha untouched. This should give
        // better fringes when bilinear interpolation is done (it's a flaw of the PNG format, that it uses unassociated alphas).
        // This interpolation should be off normally, but things like previewers such as G3D, and Blender, have problems in this area.
        // Done only for those things rendered with decals. A few extra are done; a waste, but not a big time sink normally.
        // Note, we used to bleed only for gOptions->pEFD->chkG3DMaterial, but this option is off by default and bleeding always looks
        // better (IMO), so always put bleeding on. We don't bleed for printing because decal cutout objects are not created with
        // cutouts, and some decals are used as-is, e.g. wheat, to print on sides of blocks. In other words, if we could see the black
        // fringe for the decal when 3D printing, then bleeding should not be done.
        if ( !gPrint3D )
        {
            for ( i = 0; i < TOTAL_TILES; i++ )
            {
                // If leaves are to be made solid and so should have alphas all equal to 1.0.
                if ( gOptions->pEFD->chkLeavesSolid && ( gTiles[i].flags & SBIT_LEAVES ) )
                {
                    // set all alphas in tile to 1.0.
                    setAlphaPNGSwatch( mainprog, SWATCH_INDEX( gTiles[i].txrX, gTiles[i].txrY ), gModel.swatchSize, gModel.swatchesPerRow, 255 );
                }
                else if ( gTiles[i].flags & SBIT_DECAL )
                {
                    bleedPNGSwatch( mainprog, SWATCH_INDEX( gTiles[i].txrX, gTiles[i].txrY ), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 0 );
                }
            }
        }

        // now do clamp and tile of edges of swatches
        for ( row = 0; row < gModel.verticalTiles; row++ )
        {
            for ( col = 0; col < 16; col++ )
            {
                SWATCH_TO_COL_ROW( currentSwatchCount, dstCol, dstRow );
                // copy left and right edges only if block is solid - billboards don't tile
                if ( gTiles[row*16+col].flags & SBIT_REPEAT_SIDES )
                {
                    // copy right edge from left side of tile
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*(dstCol+1)-SWATCH_BORDER,    // copy to rightmost column
                        gModel.swatchSize*dstRow+SWATCH_BORDER,  // one down from top
                        SWATCH_BORDER, gModel.tileSize,  // 1 wide
                        mainprog,
                        gModel.swatchSize*dstCol+SWATCH_BORDER,
                        gModel.swatchSize*dstRow+SWATCH_BORDER
                        );
                    // copy left edge from right side of tile
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol,    // copy to leftmost column
                        gModel.swatchSize*dstRow+SWATCH_BORDER,
                        SWATCH_BORDER, gModel.tileSize,  // 1 wide
                        mainprog,
                        gModel.swatchSize*(dstCol+1)-SWATCH_BORDER-1,
                        gModel.swatchSize*dstRow+SWATCH_BORDER
                        );
                }
                else 
                {
                    if ( gTiles[row*16+col].flags & SBIT_CLAMP_LEFT )
                    {
                        // copy left edge from left side of tile
                        copyPNGArea( mainprog, 
                            gModel.swatchSize*dstCol,    // copy to leftmost column
                            gModel.swatchSize*dstRow+SWATCH_BORDER,  // one down from top
                            SWATCH_BORDER, gModel.tileSize,  // 1 wide
                            mainprog,
                            gModel.swatchSize*dstCol+SWATCH_BORDER,
                            gModel.swatchSize*dstRow+SWATCH_BORDER
                            );
                    }
                    if ( gTiles[row*16+col].flags & SBIT_CLAMP_RIGHT )
                    {
                        // copy right edge from right side of tile
                        copyPNGArea( mainprog, 
                            gModel.swatchSize*(dstCol+1)-SWATCH_BORDER,    // copy to rightmost column
                            gModel.swatchSize*dstRow+SWATCH_BORDER,  // one down from top
                            SWATCH_BORDER, gModel.tileSize,  // 1 wide
                            mainprog,
                            gModel.swatchSize*(dstCol+1)-SWATCH_BORDER-1,
                            gModel.swatchSize*dstRow+SWATCH_BORDER
                            );
                    }
                }

                // Now do top and bottom. Note we copy the swatchSize here, not tileSize
                // top edge
                if ( gTiles[row*16+col].flags & SBIT_CLAMP_BOTTOM )
                {
                    // hold and repeat bottom of billboard, and of any "side" blocks where top and bottom don't tile
                    // NOTE: this really won't work for SWATCH_BORDER > 1, you really need to loop through each
                    // row one by one and copy it.
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol, 
                        gModel.swatchSize*(dstRow+1)-SWATCH_BORDER,    // copy to bottom of dstRow
                        gModel.swatchSize, SWATCH_BORDER,     // 1 high
                        mainprog,
                        gModel.swatchSize*dstCol,
                        gModel.swatchSize*(dstRow+1)-SWATCH_BORDER-1  // copy bottom dstRow that exists
                        );
                }
                if ( gTiles[row*16+col].flags & SBIT_CLAMP_TOP )
                {
                    // hold and repeat top (otherwise, top remains all zeroes, which is good for billboards)
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol, 
                        gModel.swatchSize*dstRow,    // copy to top of dstRow
                        gModel.swatchSize, SWATCH_BORDER,     // 1 high
                        mainprog,
                        gModel.swatchSize*dstCol,
                        gModel.swatchSize*dstRow+SWATCH_BORDER  // copy top dstRow that exists
                        );
                }
                else if ( gTiles[row*16+col].flags & SBIT_REPEAT_TOP_BOTTOM )
                {
                    // repeat tile
                    // copy upper fringe from self!
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol, 
                        gModel.swatchSize*dstRow,    // copy to top of dstRow
                        gModel.swatchSize, SWATCH_BORDER,     // 1 high
                        mainprog,
                        gModel.swatchSize*dstCol,
                        gModel.swatchSize*(dstRow+1)-SWATCH_BORDER-1   // copy bottom dstRow that exists
                        );
                    // copy lower fringe
                    copyPNGArea( mainprog, 
                        gModel.swatchSize*dstCol, 
                        gModel.swatchSize*(dstRow+1)-SWATCH_BORDER,    // copy to bottom of dstRow
                        gModel.swatchSize, SWATCH_BORDER,     // 1 high
                        mainprog,
                        gModel.swatchSize*dstCol,
                        gModel.swatchSize*dstRow+SWATCH_BORDER  // copy top dstRow that exists
                        );
                }
                currentSwatchCount++;
            }
        }

        // OLD: test if water tile is semitransparent throughout - if not, then we don't want to use water, lava, and fire tiles.
        if ( tileIsSemitransparent( gModel.pInputTerrainImage, gBlockDefinitions[BLOCK_WATER].txrX, gBlockDefinitions[BLOCK_WATER].txrY ) &&
            tileIsOpaque( gModel.pInputTerrainImage, gBlockDefinitions[BLOCK_LAVA].txrX, gBlockDefinitions[BLOCK_LAVA].txrY ) )
        {
            // Water is special, and we want to provide more user control for it, to be able to give a deep
            // blue, etc. We therefore blend between the water texture and the water swatch based on alpha:
            // the higher the alpha (more opaque) the water color is set, the more it contributes to the
            // water texture.
            blendTwoSwatches( mainprog, 
                SWATCH_INDEX( gBlockDefinitions[BLOCK_WATER].txrX, gBlockDefinitions[BLOCK_WATER].txrY ),	// texture to blend
                BLOCK_WATER,	// solid to blend
                gBlockDefinitions[BLOCK_WATER].alpha,	// how to blend the two (lower alpha will use the solid color less)
                gPrint3D ? 255 : (unsigned char)(gBlockDefinitions[BLOCK_WATER].alpha*255) );	// alpha to always assign and use
        }
        else
        {
            // This should never happen with a properly formed terrainExt.png, but just in case, leave the test in.
            int srcCol,srcRow;
            assert(0);

            // can't use water (15,13), lava (15,15), and fire (15,1), so just copy solid color over
            for ( i = 0; i < solidCount; i++ )
            {
                int id = solidTable[i];

                SWATCH_TO_COL_ROW( SWATCH_INDEX(gBlockDefinitions[id].txrX,gBlockDefinitions[id].txrY), dstCol, dstRow );
                SWATCH_TO_COL_ROW( id, srcCol, srcRow );
                copyPNGArea( mainprog, 
                    gModel.swatchSize*dstCol, // upper left corner destination
                    gModel.swatchSize*dstRow,
                    gModel.swatchSize, gModel.swatchSize, // width, height to copy
                    mainprog,
                    gModel.swatchSize*srcCol, // from
                    gModel.swatchSize*srcRow
                    );
            }
        }

        // TODO! Switch to reading actual chest tiles
        // insane corrective: truly tile between chest shared edges, grabbing samples from *adjacent* swatch
        for ( i = 0; i < 2; i++ )
        {
            SWATCH_TO_COL_ROW( SWATCH_INDEX(10,2+i), col, row );
            // copy left edge from left side of tile
            copyPNGArea( mainprog, 
                gModel.swatchSize*col,    // copy to rightmost column
                gModel.swatchSize*row,  // full column
                SWATCH_BORDER, gModel.swatchSize,  // 1 wide
                mainprog,
                gModel.swatchSize*col-2*SWATCH_BORDER,
                gModel.swatchSize*row
                );
            copyPNGArea( mainprog, 
                gModel.swatchSize*col-SWATCH_BORDER,    // copy to rightmost column
                gModel.swatchSize*row,  // full column
                SWATCH_BORDER, gModel.swatchSize,  // 1 wide
                mainprog,
                gModel.swatchSize*col+SWATCH_BORDER,
                gModel.swatchSize*row
                );
        }

        // These tiles come in grayscale and must be multiplied by some color.
        // They're grass, redstone wire, leaves, stems, etc.
        // Multiply these blocks by the solid color, e.g. grass, other white tiles

        int grassColor = 0xffffff;
        int leafColor = 0xffffff;
        int waterColor = 0xffffff;

        // Is biome in use? If so, write out biome textures, now that we actually have the biome data.
        // TODO Right now we just use the biome at the center of the export area.
        int useBiome = gOptions->exportFlags & EXPT_BIOME;
        if ( useBiome )
        {
            // get foliage and grass color from center biome: half X and half Z
            idx = (int)(gBoxSize[X]/2)*gBoxSize[Z] + gBoxSize[Z]/2;
            int biome = gBiome[idx];

            // note we don't use location height at this point to adjust temperature
            grassColor = ComputeBiomeColor( biome, 0, 1 );
            leafColor = ComputeBiomeColor( biome, 0, 0 );
            waterColor = (biome == SWAMPLAND_BIOME) ? BiomeSwampRiverColor( 0xffffff ) : 0xffffff;
        }

        for ( i = 0; i < MULT_TABLE_SIZE; i++ )
        {
            adj = multTable[i].type;

            if ( useBiome )
            {
                if ( i < MULT_TABLE_NUM_GRASS )
                {
                    // use middle biome color
                    color = grassColor;
                }
                else if ( i < MULT_TABLE_NUM_FOLIAGE )
                {
                    // use middle biome color
                    color = leafColor;
                }
                else
                {
                    color = gBlockDefinitions[adj].color;
                }
            }
            else
            {
                color = gBlockDefinitions[adj].color;
            }

            // water is special: we set the color to 0xffffff to do nothing, but for biome and swampland, the color is multiplied
            if ( i+1 == MULT_TABLE_NUM_WATER )
            {
                color = waterColor;
            }

            // check if there's no override color; if not, we can indeed use the color retrieved.
            if ( (multTable[i].colorReplace[0] == 0) && (multTable[i].colorReplace[1] == 0) && (multTable[i].colorReplace[2] == 0) )
            {
                r=(unsigned char)(color>>16);
                g=(unsigned char)((color>>8)&0xff);
                b=(unsigned char)(color&0xff);
            }
            else
            {
                // overridden color - spruce and birch leaves, currently
                r = multTable[i].colorReplace[0];
                g = multTable[i].colorReplace[1];
                b = multTable[i].colorReplace[2];
            }
            a = (unsigned char)(gBlockDefinitions[adj].alpha * 255);

            idx = SWATCH_INDEX( multTable[i].col, multTable[i].row );
            SWATCH_TO_COL_ROW( idx, dstCol, dstRow );

            // save a little work: if color is 0xffffff, no multiplication needed
            if ( r != 255 || g != 255 || b != 255 )
            {
                multiplyPNGTile(mainprog, dstCol,dstRow, gModel.swatchSize, r, g, b, a );
            }
        }

        // We need to form a special grass block side, namely
        // put into WORKSPACE (8, 2) tile 3, 0 grass and overlay tile 6, 2
        // which is now colored with the biome's color for grass.			
        SWATCH_TO_COL_ROW( SWATCH_WORKSPACE, col, row );
        SWATCH_TO_COL_ROW( SWATCH_INDEX(3,0), scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // composite biome's grass over normally colored grass
        compositePNGSwatches(mainprog,SWATCH_INDEX(6, 2),SWATCH_INDEX(6, 2),SWATCH_WORKSPACE,gModel.swatchSize,gModel.swatchesPerRow,0);

        // Make a "top of torch" template for the torch, and redstone torch on and off
        // Torch tops goes in 0,15 / 1,15 redstone on / 2,15 redstone off
        SWATCH_TO_COL_ROW( TORCH_TOP, col, row );
        SWATCH_TO_COL_ROW( SWATCH_INDEX(0,5), scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear bottom half of torch
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize*10/16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize*6/16 + SWATCH_BORDER, 0x0 );

        SWATCH_TO_COL_ROW( RS_TORCH_TOP_ON, col, row );
        SWATCH_TO_COL_ROW( SWATCH_INDEX(3,6), scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear bottom half of torch
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize*10/16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize*6/16 + SWATCH_BORDER, 0x0 );

        SWATCH_TO_COL_ROW( RS_TORCH_TOP_OFF, col, row );
        SWATCH_TO_COL_ROW( SWATCH_INDEX(3,7), scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear bottom half of torch
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize*10/16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize*6/16 + SWATCH_BORDER, 0x0 );


        // add compositing dot to 4-way wire template
        // Note that currently we have only "wire on" for all wires, no "wire off". We'd need new templates for all wires off, and we'd need an extra bit
        // to note that the wire was powered (right now we're cheating and using the data field, normally wire power, to instead hold which directions wires
        // are in).
        compositePNGSwatches(mainprog,SWATCH_INDEX(4,10),REDSTONE_WIRE_DOT,SWATCH_INDEX(4,10),gModel.swatchSize,gModel.swatchesPerRow,0);

        // make 3 way wire, then make 2 way angled wire from it
        SWATCH_TO_COL_ROW( REDSTONE_WIRE_3, col, row );
        SWATCH_TO_COL_ROW( SWATCH_INDEX(4,10), scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear left half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize, gModel.tileSize*5/16 + SWATCH_BORDER, gModel.swatchSize, 0x0 );

        SWATCH_TO_COL_ROW( REDSTONE_WIRE_ANGLED_2, col, row );
        SWATCH_TO_COL_ROW( REDSTONE_WIRE_3, scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear bottom half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize*11/16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize*5/16 + SWATCH_BORDER, 0x0 );

        // Make sure single dot of wire has something in it:
        // copy 2-way to temp area, chop limbs, composite dot and copy back to 4,11 REDSTONE_WIRE_DOT
        SWATCH_TO_COL_ROW( SWATCH_WORKSPACE, col, row );
        SWATCH_TO_COL_ROW( REDSTONE_WIRE_ANGLED_2, scol, srow );
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow );
        // clear right of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize*11/16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize*5/16 + SWATCH_BORDER, gModel.swatchSize, 0x0 );
        // clear top of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize, gModel.swatchSize, gModel.tileSize*5/16 + SWATCH_BORDER, 0x0 );
        // composite "lit bit" over wire, if any, and put into lit bit's place
        compositePNGSwatches(mainprog,REDSTONE_WIRE_DOT,REDSTONE_WIRE_DOT,SWATCH_WORKSPACE,gModel.swatchSize,gModel.swatchesPerRow,0);

        // stretch tiles to fill the area
        // plus one for the border

        // enchantment table
        stretchSwatchToTop(mainprog, SWATCH_INDEX(6,11), (float)(gModel.swatchSize*(4.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );


        // stretch only if we're not exporting it as a billboard:
        if ( !gExportBillboards )
        {
            // bed
            stretchSwatchToTop(mainprog, SWATCH_INDEX(5,9), (float)(gModel.swatchSize*(7.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );
            stretchSwatchToTop(mainprog, SWATCH_INDEX(6,9), (float)(gModel.swatchSize*(7.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );
            stretchSwatchToTop(mainprog, SWATCH_INDEX(7,9), (float)(gModel.swatchSize*(7.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );
            stretchSwatchToTop(mainprog, SWATCH_INDEX(8,9), (float)(gModel.swatchSize*(7.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );

            // cake
            stretchSwatchToTop(mainprog, SWATCH_INDEX(10,7), (float)(gModel.swatchSize*(8.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );
            stretchSwatchToTop(mainprog, SWATCH_INDEX(11,7), (float)(gModel.swatchSize*(8.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );

            // ender portal
            stretchSwatchToTop(mainprog, SWATCH_INDEX(15,9), (float)(gModel.swatchSize*(3.0/16.0)+(float)SWATCH_BORDER)/(float)gModel.swatchSize );
        }

        // make the baseline composites - really, we should add these in-place, replacing the original objects, since the originals are never used as-is. TODO
        for ( i = 0; i < COMPOSITE_TABLE_SIZE; i++ )
        {
            // very first composite, a redstone wire straight across, is rotated on the first one, (i==0) test:
            //NOTE: in 1.8 this rotation can be removed, their redstone_dust_line.png tile is rotated.
            createCompositeSwatch( compositeTable[i].cutoutSwatch, compositeTable[i].backgroundSwatch, (i==0)?90:0 );
        }

        //      // fill in all alphas that 3D export wants filled; always fill in cactus, cake, and bed fringes, for example;
        //// For printing we also then composite over other backgrounds as the defaults.
        //      faTableCount = gPrint3D ? FA_TABLE_SIZE : FA_TABLE__VIEW_SIZE;
        //      for ( i = 0; i < faTableCount; i++ )
        //      {
        //          compositePNGSwatches( mainprog,
        //              faTable[i].cutout, faTable[i].cutout, faTable[i].underlay,
        //              gModel.swatchSize, gModel.swatchesPerRow, 0 );
        //      }

        // for print, all tiles must not have any alphas;
        // we used to strip these away, but now we simply don't output the alpha channel and just export RGB,
        // so no longer need to do this process.
        //if ( gPrint3D )
        //{
        //    // finally, go through all alphas: if alpha is < 1, composite the texture onto its corresponding type (solid) block
        //    for ( i = 0; i < NUM_BLOCKS; i++ )
        //    {
        //        if ( gBlockDefinitions[i].alpha < 1.0f )
        //        {
        //            int swatchLoc = SWATCH_INDEX( gBlockDefinitions[i].txrX, gBlockDefinitions[i].txrY );
        //            compositePNGSwatches( mainprog,
        //                swatchLoc, swatchLoc, i,
        //                gModel.swatchSize, gModel.swatchesPerRow, 1 );
        //        }
        //    }
        //}
    }

    return MW_NO_ERROR;
}

static int writeBinarySTLBox( const wchar_t *world, IBox *worldBox )
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t stlFileNameWithSuffix[MAX_PATH];
    const char *justWorldFileName;
    char worldNameUnderlined[MAX_PATH];

    char outputString[256];

    int faceNo,i;

    int retCode = MW_NO_ERROR;

    FaceRecord *pFace;
    Point *vertex[4];
    // Normally each face has two triangles; triangle faces have only one, so subtract the "extra faces"
    // due to multiplying by two.
    unsigned int numTri = gModel.faceCount*2 - gModel.triangleCount;

    int colorBytes;
    unsigned short outColor = 0;
    unsigned char r,g,b;
    // export color if file format mode set that way
    int writeColor = (gOptions->exportFlags & (EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE));

    wchar_t statsFileName[MAX_PATH];

    HANDLE statsFile;

    char worldChar[MAX_PATH];

    // if no color output, don't use isMagics
    int isMagics = writeColor && (gOptions->pEFD->fileType == FILE_TYPE_BINARY_MAGICS_STL);

    concatFileName3(stlFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".stl");

    // create the STL file
    gModelFile = PortaCreate(stlFileNameWithSuffix);
    addOutputFilenameToList(stlFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

    // find last \ in world string
    wcharToChar(world,worldChar);
    justWorldFileName = removePathChar(worldChar);

    // replace spaces with underscores for world name output
    strcpy_s(worldNameUnderlined,256,justWorldFileName);
    spacesToUnderlinesChar(worldNameUnderlined);

    if ( isMagics )
    {
        unsigned int allFF = 0xffffffff;
        // Start with a default color of white - we'll always replace it
        // make it all 0x20 as we will output exactly 80 characters
        sprintf_s(outputString,256,"COLOR=");
        // start to write file
        WERROR(PortaWrite(gModelFile, outputString, 6 ));
        WERROR(PortaWrite(gModelFile, &allFF, 4 ));
        // in the example file, all the rest was 0x20's (space)
        memset(outputString,0x20,256);
        WERROR(PortaWrite(gModelFile, outputString, 70 ));
    }
    else
    {
        // Object name - give them a hint where it's from: world name and coordinates
        // make it all NULL as we will output exactly 80 characters
        memset(outputString,0,256);
        sprintf_s(outputString,256,"Mineways.com: world %s %d %d %d to %d %d %d\n", worldNameUnderlined,
            worldBox->min[X], worldBox->min[Y], worldBox->min[Z],
            worldBox->max[X], worldBox->max[Y], worldBox->max[Z] );
        // start to write file
        WERROR(PortaWrite(gModelFile, outputString, 80 ));
    }

    // number of triangles in model, unsigned int
    WERROR(PortaWrite(gModelFile, &numTri, 4 ));

    // write out the faces, it's just that simple
    for ( faceNo = 0; faceNo < gModel.faceCount; faceNo++ )
    {
        int faceTriCount;

        if ( faceNo % 1000 == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + (PG_TEXTURE-PG_OUTPUT)*((float)faceNo/(float)gModel.faceCount));

        pFace = gModel.faceList[faceNo];
        // get four face indices for the four corners
        for ( i = 0; i < 4; i++ )
        {
            vertex[i] = &gModel.vertices[pFace->vertexIndex[i]];
        }

        faceTriCount = (vertex[2] == vertex[3]) ? 1:2;

        // output a triangle or a quad, i.e. 1 or 2 faces
        for ( i = 0; i < faceTriCount; i++ )
        {
            // 3 float normals
            WERROR(PortaWrite(gModelFile, &gModel.normals[pFace->normalIndex], 12 ));

            WERROR(PortaWrite(gModelFile, vertex[0], 12 ));
            WERROR(PortaWrite(gModelFile, vertex[i+1], 12 ));
            WERROR(PortaWrite(gModelFile, vertex[i+2], 12 ));

            if ( writeColor )
            {
                // http://en.wikipedia.org/wiki/Stl_file_format#Colour_in_binary_STL
                if ( isMagics )
                {
                    // Materialise Magics 
                    colorBytes = gBlockDefinitions[pFace->type].color;
                    r=(unsigned char)(colorBytes>>16);
                    g=(unsigned char)(colorBytes>>8);
                    b=(unsigned char)(colorBytes);
                    r=r*31/255;
                    g=g*31/255;
                    b=b*31/255;
                    // topmost bit says the global color is used, so we turn it off so the per-face color here is used.
                    outColor = (b<<10) | (g<<5) | r;
                }
                else
                {
                    // VisCAM/SolidView 
                    colorBytes = gBlockDefinitions[pFace->type].color;
                    r=(unsigned char)(colorBytes>>16);
                    g=(unsigned char)(colorBytes>>8);
                    b=(unsigned char)(colorBytes);
                    r=r*31/255;
                    g=g*31/255;
                    b=b*31/255;
                    // topmost bit says this color is valid. Note order is reverse of Magics' order, above.
                    outColor = (1<<15) | (r<<10) | (g<<5) | b;
                }
            }
            WERROR(PortaWrite(gModelFile, &outColor, 2 ));
        }
    }

    // if not ok, then we will have closed the file earlier
    PortaClose(gModelFile);

    concatFileName3(statsFileName, gOutputFilePath, gOutputFileRoot, L".txt");

    // write the stats to a separate file
    statsFile = PortaCreate(statsFileName);
    addOutputFilenameToList(statsFileName);
    if (statsFile == INVALID_HANDLE_VALUE)
        return retCode|MW_CANNOT_CREATE_FILE;

    //
    retCode |= writeStatistics( statsFile, justWorldFileName, worldBox );
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    PortaClose(statsFile);

    return retCode;
}

static int writeAsciiSTLBox( const wchar_t *world, IBox *worldBox )
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t stlFileNameWithSuffix[MAX_PATH];
    const char *justWorldFileName;
    char worldNameUnderlined[MAX_PATH];
    wchar_t statsFileName[MAX_PATH];

    HANDLE statsFile;

    char outputString[256];

    int faceNo,i;

    int retCode = MW_NO_ERROR;

    int normalIndex;
    FaceRecord *pFace;
    Point *vertex[4],*pt;

    char worldChar[MAX_PATH];

    concatFileName3(stlFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".stl");

    // create the STL file
    gModelFile = PortaCreate(stlFileNameWithSuffix);
    addOutputFilenameToList(stlFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

    // find last \ in world string
    wcharToChar(world,worldChar);
    justWorldFileName = removePathChar(worldChar);

    // replace spaces with underscores for world name output
    strcpy_s(worldNameUnderlined,256,justWorldFileName);
    spacesToUnderlinesChar(worldNameUnderlined);

    // Object name - give them a hint where it's from: world name and coordinates
    sprintf_s(outputString,256,"solid %s__%d_%d_%d_to_%d_%d_%d\n", worldNameUnderlined,
        worldBox->min[X], worldBox->min[Y], worldBox->min[Z],
        worldBox->max[X], worldBox->max[Y], worldBox->max[Z] );
    // start to write file
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    // ready the normals for direct output, since we reuse them a zillion times
    resolveFaceNormals();

    char facetNormalString[NORMAL_LIST_SIZE][256];
    for ( i = 0; i < gModel.normalListCount; i++ )
    {
        sprintf_s(facetNormalString[i],256,"facet normal %e %e %e\n",gModel.normals[i][X],gModel.normals[i][Y],gModel.normals[i][Z]);
    }

    // write out the faces, it's just that simple
    for ( faceNo = 0; faceNo < gModel.faceCount; faceNo++ )
    {
        int faceTriCount;

        if ( faceNo % 1000 == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + (PG_TEXTURE-PG_OUTPUT)*((float)faceNo/(float)gModel.faceCount));

        pFace = gModel.faceList[faceNo];
        // get four face indices for the four corners
        for ( i = 0; i < 4; i++ )
        {
            vertex[i] = &gModel.vertices[pFace->vertexIndex[i]];
        }

        normalIndex = pFace->normalIndex;

        // typical output:
        //facet normal 0.000000e+000 -1.000000e+000 0.000000e+000
        //  outer loop
        //    vertex  1.000000e-002 3.000000e-002 -2.000000e-003
        //    vertex  1.200000e-002 3.000000e-002 -2.000000e-003
        //    vertex  1.200000e-002 3.000000e-002 -0.000000e+000
        //  endloop
        //endfacet

        faceTriCount = (vertex[2] == vertex[3]) ? 1:2;

        for ( i = 0; i < faceTriCount; i++ )
        {
            WERROR(PortaWrite(gModelFile, facetNormalString[normalIndex], strlen(facetNormalString[normalIndex]) ));
            WERROR(PortaWrite(gModelFile, "outer loop\n", strlen("outer loop\n") ));

            pt = vertex[0];
            sprintf_s(outputString,256,"vertex  %e %e %e\n",(double)((*pt)[X]),(double)((*pt)[Y]),(double)((*pt)[Z]));
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            pt = vertex[i+1];
            sprintf_s(outputString,256,"vertex  %e %e %e\n",(double)((*pt)[X]),(double)((*pt)[Y]),(double)((*pt)[Z]));
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            pt = vertex[i+2];
            sprintf_s(outputString,256,"vertex  %e %e %e\n",(double)((*pt)[X]),(double)((*pt)[Y]),(double)((*pt)[Z]));
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

            WERROR(PortaWrite(gModelFile, "endloop\nendfacet\n", strlen("endloop\nendfacet\n") ));
        }
    }

    sprintf_s(outputString,256,"endsolid %s\n",worldNameUnderlined);
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    // if not ok, then we will have closed the file earlier
    PortaClose(gModelFile);

    concatFileName3(statsFileName, gOutputFilePath, gOutputFileRoot, L".txt");

    // write the stats to a separate file
    statsFile = PortaCreate(statsFileName);
    addOutputFilenameToList(statsFileName);
    if (statsFile == INVALID_HANDLE_VALUE)
        return retCode|MW_CANNOT_CREATE_FILE;

    //
    retCode |= writeStatistics( statsFile, justWorldFileName, worldBox );
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    PortaClose(statsFile);

    return retCode;
}


static int writeVRML2Box( const wchar_t *world, IBox *worldBox )
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t wrlFileNameWithSuffix[MAX_PATH];
    const char *justWorldFileName;
    char justTextureFileName[MAX_PATH];	// without path

    char outputString[256];
    char textureDefOutputString[256];
    //char textureUseOutputString[256];

    int currentFace, j, firstShape, exportSingleMaterial, exportSolidColors;

    int retCode = MW_NO_ERROR;

    //char worldNameUnderlined[256];

    FaceRecord *pFace;

    char worldChar[MAX_PATH];

#define HEADER_COUNT 8
    char *header[HEADER_COUNT] = {
        "\nNavigationInfo {\n",
        "  type [ \"EXAMINE\", \"ANY\" ]\n",
        "}\n",
        "Transform {\n",
        "  scale 1 1 1\n",
        "  translation 0 0 0\n",
        "  children\n",
        "  [\n"
    };


    concatFileName3(wrlFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".wrl");

    // create the VRML wrl file
    gModelFile=PortaCreate(wrlFileNameWithSuffix);
    addOutputFilenameToList(wrlFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return retCode|MW_CANNOT_CREATE_FILE;

    exportSolidColors = (gOptions->exportFlags & EXPT_OUTPUT_MATERIALS) && !gExportTexture;

    // if you want each separate textured object to be its own shape, do this line instead:
    exportSingleMaterial = !(gOptions->exportFlags & EXPT_GROUP_BY_MATERIAL);

    wcharToChar(world,worldChar);
    justWorldFileName = removePathChar(worldChar);

    sprintf_s(outputString,256,"#VRML V2.0 utf8\n\n# VRML 97 (VRML2) file made by Mineways version %d.%d, http://mineways.com\n", gMajorVersion, gMinorVersion );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    retCode |= writeStatistics( gModelFile, justWorldFileName, worldBox );
    if ( retCode >= MW_BEGIN_ERRORS )
        goto Exit;

    retCode |= writeLines( gModelFile, header, HEADER_COUNT );
    if ( retCode >= MW_BEGIN_ERRORS )
        goto Exit;

    // note we don't need to output the normals, since the crease angle will compute them properly! Could output them for speed.
    //// write out normals, texture coordinates, vertices, and then faces grouped by material
    //for ( i = 0; i < 6; i++ )
    //{
    //    sprintf_s(outputString,256,"vn %g %g %g\n", gModel.normals[i][0], gModel.normals[i][1], gModel.normals[i][2]);
    //    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
    //}

    // output vertex coordinate loops

    // prepare generic material for texture (texture gets multiplied by the color, so we have to use just the generic)
    if ( gExportTexture )
    {
        // prepare output texture file name string
        // get texture name to export, if needed
        sprintf_s(justTextureFileName,MAX_PATH,"%s.png",gOutputFileRootCleanChar);
        // DEF/USE should be legal, http://castle-engine.sourceforge.net/vrml_engine_doc/output/xsl/html/section.def_use.html, but Shapeways doesn't like it for some reason.
        //sprintf_s(textureUseOutputString,256,"        texture USE image_Craft\n", justTextureFileName );
        sprintf_s(textureDefOutputString,256,"        texture ImageTexture { url \"%s\" }\n", justTextureFileName );
    }

    firstShape = 1;
    currentFace = 0;
    while ( currentFace < gModel.faceCount )
    {
        char mtlName[256];
        char shapeString[] = "    Shape\n    {\n      geometry DEF %s_Obj IndexedFaceSet\n      {\n        creaseAngle .5\n        solid %s\n        coord %s coord_Craft%s\n";

        int beginIndex, endIndex, currentType;

        if ( currentFace % 1000 == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + 0.3f*(PG_TEXTURE-PG_OUTPUT)*((float)currentFace/(float)gModel.faceCount));

        // start new shape
        if ( exportSingleMaterial )
        {
            strcpy_s(mtlName,256,"Neutral_White");
        }
        else
        {
            strcpy_s(mtlName,256,gBlockDefinitions[gModel.faceList[currentFace]->type].name);
            spacesToUnderlinesChar(mtlName);
        }
        sprintf_s( outputString, 256, shapeString, 
            mtlName,
            gPrint3D ? "TRUE" : "FALSE",
            firstShape ? "DEF" : "USE",
            firstShape ? " Coordinate" : "" );
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

        // if first shape, output coords and texture coords
        if ( firstShape )
        {
            // Note that we just dump everything to a single indexed face set coordinate list, which then gets reused
            strcpy_s( outputString, 256, "        {\n          point\n          [\n" );
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

            for ( j = 0; j < gModel.vertexCount; j++ )
            {
                if ( j % 1000 == 0 )
                    UPDATE_PROGRESS( PG_OUTPUT + 0.3f*(PG_TEXTURE-PG_OUTPUT) + 0.7f*(PG_TEXTURE-PG_OUTPUT)*((float)j/(float)gModel.vertexCount));

                if ( j == gModel.vertexCount-1 )
                {
                    // no comma at end
                    sprintf_s(outputString,256,"            %g %g %g\n", gModel.vertices[j][X], gModel.vertices[j][Y], gModel.vertices[j][Z] );
                }
                else
                {
                    sprintf_s(outputString,256,"            %g %g %g,\n", gModel.vertices[j][X], gModel.vertices[j][Y], gModel.vertices[j][Z] );
                }
                WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            }

            // textures need texture coordinates output, only needed when texturing
            if ( gExportTexture )
            {
                int prevSwatch = -1;
                int k;
                strcpy_s(outputString,256,"          ]\n        }\n        texCoord DEF texCoord_Craft TextureCoordinate\n        {\n          point\n          [\n");
                WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

                for ( k = 0; k < gModel.uvIndexCount; k++ )
                {
                    retCode |= writeVRMLTextureUV(gModel.uvIndexList[k].uc, gModel.uvIndexList[k].vc, prevSwatch!=gModel.uvIndexList[k].swatchLoc, gModel.uvIndexList[k].swatchLoc);
                    prevSwatch = gModel.uvIndexList[k].swatchLoc;
                    if (retCode >= MW_BEGIN_ERRORS)
                        goto Exit;
                }
            }
            // close up coordinates themselves
            strcpy_s(outputString,256,"          ]\n        }\n");
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
        }
        else
        {
            if ( gExportTexture )
            {
                strcpy_s(outputString,256,"        texCoord USE texCoord_Craft\n");
                WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            }
        }

        beginIndex = currentFace;
        currentType = exportSingleMaterial ? BLOCK_STONE : gModel.faceList[currentFace]->type;

        strcpy_s(outputString,256,"        coordIndex\n        [\n");
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

        // output face loops until next material is found, or all, if exporting no material
        while ( (currentFace < gModel.faceCount) &&
            ( (currentType == gModel.faceList[currentFace]->type) || exportSingleMaterial ) )
        {
            char commaString[256];
            strcpy_s(commaString,256,( currentFace == gModel.faceCount-1 || (currentType != gModel.faceList[currentFace+1]->type) ) ? "" : "," );

            pFace = gModel.faceList[currentFace];

            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                sprintf_s(outputString,256,"          %d,%d,%d,-1%s\n",
                    pFace->vertexIndex[0],
                    pFace->vertexIndex[1],
                    pFace->vertexIndex[2],
                    commaString);
            }
            else
            {
                sprintf_s(outputString,256,"          %d,%d,%d,%d,-1%s\n",
                    pFace->vertexIndex[0],
                    pFace->vertexIndex[1],
                    pFace->vertexIndex[2],
                    pFace->vertexIndex[3],
                    commaString);
            }
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

            currentFace++;
        }

        // output texture coordinates for face loops
        if ( gExportTexture )
        {
            strcpy_s(outputString,256,"        ]\n        texCoordIndex\n        [\n");
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

            endIndex = currentFace;

            // output texture coordinate loops
            for ( currentFace = beginIndex; currentFace < endIndex; currentFace++ )
            {
                // output the face loop
                pFace = gModel.faceList[currentFace];

                if ( pFace->uvIndex[2] == pFace->uvIndex[3] )
                {
                    sprintf_s(outputString,256,"          %d %d %d -1\n",
                        pFace->uvIndex[0],
                        pFace->uvIndex[1],
                        pFace->uvIndex[2]);
                }
                else
                {
                    sprintf_s(outputString,256,"          %d %d %d %d -1\n",
                        pFace->uvIndex[0],
                        pFace->uvIndex[1],
                        pFace->uvIndex[2],
                        pFace->uvIndex[3]);
                }
                WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            }
        }

        // close up the geometry
        strcpy_s(outputString,256,"        ]\n      }\n");
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

        // now output material
        // - if a single material or if textures are output, we use the GENERIC_MATERIAL for the type to output
        // (textured material gets multiplied by this white material)
        // - if it's a single material, then the output name is "generic"
        retCode |= writeVRMLAttributeShapeSplit( exportSolidColors ? currentType : GENERIC_MATERIAL, 
            mtlName,
            // DEF/USE - Shapeways does not like: gExportTexture ? (firstShape ? textureDefOutputString : textureUseOutputString) : NULL );
            gExportTexture ? textureDefOutputString : NULL );

        if ( retCode >= MW_BEGIN_ERRORS )
            goto Exit;

        // close up shape
        strcpy_s(outputString,256,"    }\n");
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

        firstShape = 0;
    }

    // close up Transform children
    strcpy_s(outputString,256,"  ]\n}\n");
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

Exit:
    PortaClose(gModelFile);

    return retCode;
}

// if type is GENERIC_MATERIAL, set the generic. If textureOutputString is set, output texture.
static int writeVRMLAttributeShapeSplit( int type, char *mtlName, char *textureOutputString )
{
#ifdef WIN32
    DWORD br;
#endif

    char outputString[1024];
    char tfString[256];
    char keString[256];

    char attributeString[] = "      appearance Appearance\n      {\n";

    float fRed,fGreen,fBlue;
    float ka, kd, ks, ke;
    float alpha;

    WERROR(PortaWrite(gModelFile, attributeString, strlen(attributeString) ));

    if ( type == GENERIC_MATERIAL )
    {
        fRed = fGreen = fBlue = 1.0f;
        // note that this "generic" type will never match the debug type
        type = BLOCK_STONE;
    }
    else
    {
        fRed = (gBlockDefinitions[type].color >> 16)/255.0f;
        fGreen = ((gBlockDefinitions[type].color >> 8) & 0xff)/255.0f;
        fBlue = (gBlockDefinitions[type].color & 0xff)/255.0f;
    }

    // good for blender:
    ka = 0.2f;
    kd = 0.9f;
    ks = 0.1f;
    ke = 0.0f;
    // 3d printers cannot print semitransparent surfaces, so set alpha to 1.0 so what you preview
    // is what you get. TODO Should we turn off alpha for textures, as the textures themselves will have alpha in them - this is
    // in case the model viewer tries to multiply alpha by texture; also, VRML just has one material for textures, generic.
    // Really, we could have no colors at all when textures are output, but the colors are useful for previewers that
    // do not support textures (e.g. Blender).
    //alpha = ( gPrint3D || (gExportTexture)) ? 1.0f : gBlockDefinitions[type].alpha;
    // Well, hmmm, alpha is useful in previewing (no textures displayed), at least for OBJ files
    // alpha = gPrint3D ? 1.0f : gBlockDefinitions[type].alpha;
    alpha = gBlockDefinitions[type].alpha;
    if (gOptions->exportFlags & EXPT_DEBUG_SHOW_GROUPS)
    {
        // if showing groups, make the alpha of the largest group transparent
        if ( gDebugTransparentType == type )
        {
            alpha = DEBUG_DISPLAY_ALPHA;
        }
        else
        {
            alpha = 1.0f;
        }
    }
    else if ( gPrint3D )
    {
        // for 3d printing, alpha is always 1.0
        alpha = 1.0f;
    }
    // if semitransparent, and a truly transparent thing, then alpha is used; otherwise it's probably a cutout and the overall alpha should be 1.0f
    if ( alpha < 1.0f && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES) && !(gBlockDefinitions[type].flags & BLF_TRANSPARENT) )
    {
        alpha = 1.0f;
    }

    if ( alpha < 1.0f )
    {
        // semitransparent block, such as water
        sprintf_s(tfString,256,"           transparency %g\n", alpha );
    }
    else
    {
        tfString[0] = '\0';
    }

    if (!gPrint3D && (gBlockDefinitions[type].flags & BLF_EMITTER) )
    {
        // emitter
        sprintf_s(keString,256,"          emissiveColor %g %g %g\n", fRed*ke, fGreen*ke, fBlue*ke );
    }
    else
    {
        keString[0] = '\0';
    }

    sprintf_s(outputString,1024,
        "        material DEF %s Material\n"
        "        {\n"
        "          ambientIntensity %g\n"
        "          diffuseColor %g %g %g\n"
        "          specularColor %g %g %g\n"
        "          shininess .5\n"
        "%s"
        "%s"
        "        }\n",
        mtlName,
        ka,
        kd*fRed, kd*fGreen, kd*fBlue,
        ks*fRed, ks*fGreen, ks*fBlue,
        keString,
        tfString
        );

    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    if ( textureOutputString != NULL )
    {
        WERROR(PortaWrite(gModelFile, textureOutputString, strlen(textureOutputString) ));
    }

    // close up appearance
    strcpy_s(outputString,256,"      }\n");
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    return MW_NO_ERROR;
}

static int writeVRMLTextureUV( float u, float v, int addComment, int swatchLoc )
{
#ifdef WIN32
    DWORD br;
#endif
    char outputString[1024];

    if ( addComment )
    {
        sprintf_s(outputString,1024,"# %s\n            %g %g\n",
            gBlockDefinitions[gModel.uvSwatchToType[swatchLoc]].name,
            u, v );
    }
    else
    {
        sprintf_s(outputString,1024,"            %g %g\n",
            u, v );
    }
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    return MW_NO_ERROR;
}


static int writeSchematicBox()
{
#ifdef WIN32
    //DWORD br;
#endif
    FILE *fptr;
    int err;
    gzFile gz;

    wchar_t schematicFileNameWithSuffix[MAX_PATH];

    int retCode = MW_NO_ERROR;

    int width, height, length, totalSize, maxShortSize;
    unsigned char *blocks, *block_ptr;
    unsigned char *blockData, *blockData_ptr;
    IPoint loc;
    float progressStart, progressOffset;

    int xStart, xEnd, xIncr;
    int zStart, zEnd, zIncr;

    int rotateQuarter = 0;

    width = gSolidBox.max[X] - gSolidBox.min[X] + 1;
    height = gSolidBox.max[Y] - gSolidBox.min[Y] + 1;
    length = gSolidBox.max[Z] - gSolidBox.min[Z] + 1;

    // maximum short size
    maxShortSize = (1<<16) - 1;

    if (width > maxShortSize) {
        // Width of region too large for a .schematic");
        return retCode|MW_DIMENSION_TOO_LARGE;
    }
    if (height > maxShortSize) {
        // Height of region too large for a .schematic");
        return retCode|MW_DIMENSION_TOO_LARGE;
    }
    if (length > maxShortSize) {
        // Length of region too large for a .schematic");
        return retCode|MW_DIMENSION_TOO_LARGE;
    }

    concatFileName3(schematicFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".schematic");

    // create the schematic file
    err = _wfopen_s(&fptr, schematicFileNameWithSuffix, L"wb");
    if (fptr == NULL || err != 0)
    {
        return retCode|MW_CANNOT_CREATE_FILE;
    }
    // now make it a gzip file
    gz = gzdopen(_fileno(fptr),"wb");
    if (gz == NULL)
    {
        return retCode|MW_CANNOT_CREATE_FILE;
    }

    addOutputFilenameToList(schematicFileNameWithSuffix);

    blocks = blockData = NULL;

    if ( gOptions->pEFD->radioRotate0 )
    {
        //angle = 0;
        xStart = gSolidBox.min[X];
        xEnd = gSolidBox.max[X];
        xIncr = 1;
        zStart = gSolidBox.min[Z];
        zEnd = gSolidBox.max[Z];
        zIncr = 1;
    }
    else if ( gOptions->pEFD->radioRotate90 )
    {
        //angle = 90;
        xStart = gSolidBox.max[Z];
        xEnd = gSolidBox.min[Z];
        xIncr = -1;
        zStart = gSolidBox.min[X];
        zEnd = gSolidBox.max[X];
        zIncr = 1;

        rotateQuarter = 1;
    }
    else if ( gOptions->pEFD->radioRotate180 )
    {
        //angle = 180;
        xStart = gSolidBox.max[X];
        xEnd = gSolidBox.min[X];
        xIncr = -1;
        zStart = gSolidBox.max[Z];
        zEnd = gSolidBox.min[Z];
        zIncr = -1;
    }
    else
    {
        //angle = 270;
        assert(gOptions->pEFD->radioRotate270);

        xStart = gSolidBox.min[Z];
        xEnd = gSolidBox.max[Z];
        xIncr = 1;
        zStart = gSolidBox.max[X];
        zEnd = gSolidBox.min[X];
        zIncr = -1;

        rotateQuarter = 1;
    }

    if ( rotateQuarter )
    {
        // swap X and Z widths for output
        int swapper = width;
        width = length;
        length = swapper;
    }

#define CHECK_SCHEMATIC_QUIT( b )			\
    if ( (b) == 0 ) {						\
    if ( blocks ) free( blocks );		\
    if ( blockData ) free( blockData );	\
    gzclose(gz);						\
    return retCode|MW_CANNOT_WRITE_TO_FILE;		\
    }

    // check if return codes are 0, if so failed and we should abort
    CHECK_SCHEMATIC_QUIT( schematicWriteCompoundTag(gz, "Schematic") );
    // follow a typical file structure from schematics site, giving an order
    CHECK_SCHEMATIC_QUIT( schematicWriteShortTag(gz, "Height", (short)height ) );
    CHECK_SCHEMATIC_QUIT( schematicWriteShortTag(gz, "Length", (short)length ) );
    CHECK_SCHEMATIC_QUIT( schematicWriteShortTag(gz, "Width", (short)width ) );

    //// WorldEdit likes to add these, not sure what they are. TODO someday, figure these out.
    ////schematicWriteInt("WEOriginX", new IntTag("WEOriginX", clipboard.getOrigin().getBlockX()));
    ////schematicWriteInt("WEOriginY", new IntTag("WEOriginY", clipboard.getOrigin().getBlockY()));
    ////schematicWriteInt("WEOriginZ", new IntTag("WEOriginZ", clipboard.getOrigin().getBlockZ()));
    ////schematicWriteInt("WEOffsetX", new IntTag("WEOffsetX", clipboard.getOffset().getBlockX()));
    ////schematicWriteInt("WEOffsetY", new IntTag("WEOffsetY", clipboard.getOffset().getBlockY()));
    ////schematicWriteInt("WEOffsetZ", new IntTag("WEOffsetZ", clipboard.getOffset().getBlockZ()));

    CHECK_SCHEMATIC_QUIT( schematicWriteEmptyListTag(gz, "Entities" ) );
    CHECK_SCHEMATIC_QUIT( schematicWriteEmptyListTag(gz, "TileEntities" ) );

    CHECK_SCHEMATIC_QUIT( schematicWriteString(gz, "Materials", "Alpha" ) );

    // Copy
    totalSize = width * height * length;
    blocks = block_ptr = (unsigned char *)malloc(totalSize);
    blockData = blockData_ptr = (unsigned char *)malloc(totalSize);

    progressStart = 0.80f*PG_MAKE_FACES;
    progressOffset = PG_END - progressStart;

    // go through blocks and see which is solid; use solid blocks to generate faces
    // Order is YZX according to http://www.minecraftwiki.net/wiki/Schematic_file_format
    for ( loc[Y] = gSolidBox.min[Y]; loc[Y] <= gSolidBox.max[Y]; loc[Y]++ )
    {
        float localT = ((float)(loc[Y]-gSolidBox.min[Y]+1)/(float)(gSolidBox.max[Y]-gSolidBox.min[Y]+1));
        float globalT = progressStart + progressOffset*localT;
        UPDATE_PROGRESS( globalT );

        for ( loc[Z] = zStart; loc[Z]*zIncr <= zEnd*zIncr; loc[Z]+=zIncr )
        {
            for ( loc[X] = xStart; loc[X]*xIncr <= xEnd*xIncr; loc[X]+=xIncr )
            {
                unsigned char type, data;
                int boxIndex;
                // if we rotate 90 or 270, X and Z are swapped for rotation. This allows us to
                // have just one set of loops, above.
                if ( rotateQuarter )
                {
                    boxIndex = BOX_INDEX(loc[Z],loc[Y],loc[X]);
                }
                else
                {
                    boxIndex = BOX_INDEX(loc[X],loc[Y],loc[Z]);
                }

                type = gBoxData[boxIndex].type;
                data = gBoxData[boxIndex].data;
                if ( gBoxData[boxIndex].type >= BLOCK_UNKNOWN )
                {
                    // unknown block?
                    if ( gBoxData[boxIndex].type == BLOCK_UNKNOWN )
                    {
                        // convert to bedrock, I guess...
                        data = 0x0;
                        type = BLOCK_BEDROCK;
                        retCode |= MW_UNKNOWN_BLOCK_TYPE_ENCOUNTERED;
                    }
                }
                *block_ptr++ = type;
                *blockData_ptr++ = data;
            }
        }
    }

    CHECK_SCHEMATIC_QUIT( schematicWriteByteArrayTag(gz, "Blocks", blocks, totalSize ) );
    CHECK_SCHEMATIC_QUIT( schematicWriteByteArrayTag(gz, "Data", blockData, totalSize ) );

    // TAG_End
    CHECK_SCHEMATIC_QUIT( schematicWriteUnsignedCharValue( gz, 0x0 ) );

    gzclose(gz);

    free( blocks );
    free( blockData );

    return retCode;
}

static int schematicWriteCompoundTag( gzFile gz, char *tag )
{
    int totWrite = schematicWriteTagValue( gz, 0x0A, tag );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteShortTag( gzFile gz, char *tag, short value )
{
    int totWrite = schematicWriteTagValue( gz, 0x02, tag );
    totWrite += schematicWriteShortValue( gz, value );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteEmptyListTag( gzFile gz, char *tag )
{
    int totWrite = schematicWriteTagValue( gz, 0x09, tag );
    // cheat: just force in empty data. Not sure what I'd normally see here...
    totWrite += schematicWriteUnsignedCharValue( gz, 0x0A );
    totWrite += schematicWriteIntValue( gz, 0 );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteString( gzFile gz, char *tag, char *field )
{
    int totWrite = schematicWriteTagValue( gz, 0x08, tag );
    totWrite += schematicWriteUnsignedShortValue( gz, (unsigned short)strlen(field) );
    totWrite += schematicWriteStringValue( gz, field );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteByteArrayTag( gzFile gz, char *tag, unsigned char *byteData, int totalSize )
{
    int totWrite = schematicWriteTagValue( gz, 0x07, tag );
    totWrite += schematicWriteIntValue( gz, totalSize );
    totWrite += gzwrite( gz, byteData, totalSize );
    assert(totWrite);
    return totWrite;
}


// writes tag value, length of string, and string
static int schematicWriteTagValue( gzFile gz, unsigned char tagValue, char *tag )
{
    int totWrite = schematicWriteUnsignedCharValue( gz, tagValue );
    totWrite += schematicWriteUnsignedShortValue( gz, (unsigned short)strlen(tag) );
    totWrite += schematicWriteStringValue( gz, tag );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteUnsignedCharValue( gzFile gz, unsigned char charValue )
{
    int totWrite = gzwrite( gz, &charValue, 1 );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteUnsignedShortValue( gzFile gz, unsigned short shortValue )
{
    unsigned char writeByte = (shortValue>>8)&0xff;
    int totWrite = gzwrite( gz, &writeByte, 1 );
    writeByte = shortValue&0xff;
    totWrite += gzwrite( gz, &writeByte, 1 );
    assert(totWrite);
    return totWrite;
}

// really, identical to the one above, just a different signature
static int schematicWriteShortValue( gzFile gz, short shortValue )
{
    unsigned char writeByte = (shortValue>>8)&0xff;
    int totWrite = gzwrite( gz, &writeByte, 1 );
    writeByte = shortValue&0xff;
    totWrite += gzwrite( gz, &writeByte, 1 );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteIntValue( gzFile gz, int intValue )
{
    int totWrite = schematicWriteUnsignedShortValue( gz, (unsigned short)((intValue>>16) & 0xffff) );
    totWrite += schematicWriteUnsignedShortValue( gz, (unsigned short)(intValue & 0xffff) );
    assert(totWrite);
    return totWrite;
}

static int schematicWriteStringValue( gzFile gz, char *stringValue )
{
    int totWrite = gzwrite( gz, stringValue, (unsigned int)strlen(stringValue) );
    assert(totWrite);
    return totWrite;
}


static int writeLines( HANDLE file, char **textLines, int lines )
{
#ifdef WIN32
    DWORD br;
#endif

    int i;
    for ( i = 0; i < lines; i++ )
    {
        WERROR(PortaWrite(file, textLines[i], strlen(textLines[i]) ));
    }

    return MW_NO_ERROR;
}

static float max3( Point pt )
{
    float retVal = max( pt[0], pt[1] );
    return max( retVal, pt[2] );
}
static float med3( Point pt )
{
    float retVal = max( pt[0], pt[1] );
    if ( retVal > pt[2] )
    {
        // old retVal is maximum, so compare other two
        retVal = min( pt[0], pt[1] );
        retVal = max( retVal, pt[2] );
    }
    return retVal;
}
static float min3( Point pt )
{
    float retVal = min( pt[0], pt[1] );
    return min( retVal, pt[2] );
}

static int writeStatistics( HANDLE fh, const char *justWorldFileName, IBox *worldBox )
{
#ifdef WIN32
    DWORD br;
#endif

    char outputString[256];
    char timeString[256];
    char formatString[256];
    errno_t errNum;
    struct tm newtime;
    __time32_t aclock;

    int radio;
    float angle;

    char *outputTypeString[] = {
        "Export no materials",
        "Export solid material colors only (no textures)",
        "Export richer color textures",
        "Export full color texture patterns"
    };

    float inCM = gModel.scale * METERS_TO_CM;
    float inCM3 = inCM * inCM * inCM;

    sprintf_s(outputString,256,"# Extracted from Minecraft world %s\n", justWorldFileName );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));


    _time32( &aclock );   // Get time in seconds.
    _localtime32_s( &newtime, &aclock );   // Convert time to struct tm form.

    // Print local time as a string.
    errNum = asctime_s(timeString, 32, &newtime);
    if (!errNum)
    {
        sprintf_s(outputString,256,"# %s", timeString );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    // put the selection box near the top, since I find I use these values most of all
    sprintf_s(outputString,256,"\n# Selection location min to max: %d, %d, %d to %d, %d, %d\n\n",
        worldBox->min[X], worldBox->min[Y], worldBox->min[Z],
        worldBox->max[X], worldBox->max[Y], worldBox->max[Z] );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    // If STL, say which type of STL, etc.
    switch ( gOptions->pEFD->fileType )
    {
    case FILE_TYPE_WAVEFRONT_ABS_OBJ:
        strcpy_s( formatString, 256, "Wavefront OBJ absolute indices");
        break;
    case FILE_TYPE_WAVEFRONT_REL_OBJ:
        strcpy_s( formatString, 256, "Wavefront OBJ relative indices");
        break;
    case FILE_TYPE_BINARY_MAGICS_STL:
        strcpy_s( formatString, 256, "Binary STL iMaterialise");
        break;
    case FILE_TYPE_BINARY_VISCAM_STL:
        strcpy_s( formatString, 256, "Binary STL VisCAM");
        break;
    case FILE_TYPE_ASCII_STL:
        strcpy_s( formatString, 256, "ASCII STL");
        break;
    case FILE_TYPE_VRML2:
        strcpy_s( formatString, 256, "VRML 2.0");
        break;
    default:
        strcpy_s( formatString, 256, "Unknown file type");
        assert(0);
        break;
    }
    sprintf_s(outputString,256,"# Created for %s - %s\n", gPrint3D ? "3D printing" : "Viewing", formatString );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( gPrint3D )
    {
        char warningString[256];
        int isSculpteo = ( gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ ) || ( gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ );

        if ( !isSculpteo )
        {
            // If we add materials, put the material chosen here.
            sprintf_s(outputString,256,"\n# Cost estimate for this model:\n");
            WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

            sprintf_s(warningString,256,"%s", (gModel.scale < gMtlCostTable[PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE].minWall) ? " *** WARNING, thin wall ***" : "" );
            sprintf_s(outputString,256,"#   if made using the white, strong & flexible material: $ %0.2f%s\n",
                computeMaterialCost( PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, gModel.scale, gBlockCount, gMinorBlockCount ),
                warningString);
            WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
        }

        sprintf_s(warningString,256,"%s", (gModel.scale < gMtlCostTable[isSculpteo ? PRINT_MATERIAL_FCS_SCULPTEO : PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall) ? " *** WARNING, thin wall ***" : "" );
        sprintf_s(outputString,256,"#   if made using the full color sandstone material:     $ %0.2f%s\n",
            computeMaterialCost( isSculpteo ? PRINT_MATERIAL_FCS_SCULPTEO : PRINT_MATERIAL_FULL_COLOR_SANDSTONE, gModel.scale, gBlockCount, gMinorBlockCount ),
            warningString);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        // if material is not one of these, print its cost
        if ( gPhysMtl > PRINT_MATERIAL_FULL_COLOR_SANDSTONE && gPhysMtl != PRINT_MATERIAL_FCS_SCULPTEO )
        {
            sprintf_s(warningString,256,"%s", (gModel.scale < gMtlCostTable[gPhysMtl].minWall) ? " *** WARNING, thin wall ***" : "" );
            sprintf_s(outputString,256,
                "#   if made using the %s material:     $ %0.2f%s\n",
                gMtlCostTable[gPhysMtl].name,
                computeMaterialCost( gPhysMtl, gModel.scale, gBlockCount, gMinorBlockCount ),
                warningString);
            WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
        }
        gOptions->cost = computeMaterialCost( gPhysMtl, gModel.scale, gBlockCount, gMinorBlockCount );

        sprintf_s(outputString,256, "# For %s printer, minimum wall is %g mm, maximum size is %g x %g x %g cm\n", gMtlCostTable[gPhysMtl].name, gMtlCostTable[gPhysMtl].minWall*METERS_TO_MM,
            gMtlCostTable[gPhysMtl].maxSize[0], gMtlCostTable[gPhysMtl].maxSize[1], gMtlCostTable[gPhysMtl].maxSize[2] );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    sprintf_s(outputString,256,"# Units for the model vertex data itself: %s\n", gUnitTypeTable[gOptions->pEFD->comboModelUnits[gOptions->pEFD->fileType]].name );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( gPrint3D )
    {
        float area, volume, sumOfDimensions;
        char errorString[256];

        if ( inCM * max3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[0] ||
            inCM * med3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[1] ||
            inCM * min3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[2] )
        {
            sprintf_s(errorString,256," *** WARNING, too large for %s printer", gMtlCostTable[gPhysMtl].name);
        }
        else
        {
            errorString[0] = '\0';
        }

        gOptions->dim_cm[X] = inCM * gFilledBoxSize[X];
        gOptions->dim_cm[Y] = inCM * gFilledBoxSize[Y];
        gOptions->dim_cm[Z] = inCM * gFilledBoxSize[Z];
        sprintf_s(outputString,256,"\n# world dimensions: %0.2f x %0.2f x %0.2f cm%s\n",
            gOptions->dim_cm[X], gOptions->dim_cm[Y], gOptions->dim_cm[Z], errorString);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        gOptions->dim_inches[X] = inCM * gFilledBoxSize[X]/2.54f;
        gOptions->dim_inches[Y] = inCM * gFilledBoxSize[Y]/2.54f;
        gOptions->dim_inches[Z] = inCM * gFilledBoxSize[Z]/2.54f;
        sprintf_s(outputString,256,"#   in inches: %0.2f x %0.2f x %0.2f inches%s\n",
            gOptions->dim_inches[X], gOptions->dim_inches[Y], gOptions->dim_inches[Z], errorString );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        gOptions->block_mm = gModel.scale*METERS_TO_MM;
        gOptions->block_inch = gOptions->block_mm / 25.4f;
        sprintf_s(outputString,256,"# each block is %0.2f mm on a side, and has a volume of %g mm^3\n", gOptions->block_mm, inCM3*1000 );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sumOfDimensions = 10*inCM *(gFilledBoxSize[X]+gFilledBoxSize[Y]+gFilledBoxSize[Z]);
        sprintf_s(outputString,256,"# sum of dimensions: %g mm\n", sumOfDimensions );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        volume = inCM3 * gBlockCount;
        sprintf_s(outputString,256,"# volume is %g cm^3\n", volume );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        area = AREA_IN_CM2 ;
        sprintf_s(outputString,256,"# surface area is %g cm^2\n", area );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"# block density: %d%% of volume\n",
            (int)(gStats.density*100.0f+0.5f));
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    // write out a summary, useful for various reasons
    if ( gExportBillboards )
    {
        sprintf_s(outputString,256,"\n# %d vertices, %d faces (%d triangles), %d blocks, %d billboards/bits\n", gModel.vertexCount, gModel.faceCount, 2*gModel.faceCount, gBlockCount, gModel.billboardCount);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }
    else
    {
        sprintf_s(outputString,256,"\n# %d vertices, %d faces (%d triangles), %d blocks\n", gModel.vertexCount, gModel.faceCount, 2*gModel.faceCount, gBlockCount);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }
    gOptions->totalBlocks = gBlockCount;

    sprintf_s(outputString,256,"# block dimensions: X=%g by Y=%g (height) by Z=%g blocks\n", gFilledBoxSize[X], gFilledBoxSize[Y], gFilledBoxSize[Z] );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    Vec2Op(gOptions->dimensions, =, (int)gFilledBoxSize);

    // Summarize all the options used for output
    if ( gOptions->exportFlags & EXPT_OUTPUT_MATERIALS )
    {
        if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES )
            radio = 2;
        else if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES )
            radio = 3;
        else
            radio = 1;
    }
    else
        radio = 0;

    sprintf_s(outputString,256,"# File type: %s\n", outputTypeString[radio] );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( ( gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ ) || ( gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ ) )
    {
        if ( gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
        {
            strcpy_s(outputString,256,"# OBJ relative coordinates" );
            WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
        }

        sprintf_s(outputString,256,"# Export separate objects: %s\n", gOptions->pEFD->chkMultipleObjects ? "YES" : "no" );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
        if ( gOptions->pEFD->chkMultipleObjects )
        {
            sprintf_s(outputString,256,"#  Material per object: %s\n", gOptions->pEFD->chkMaterialPerType ? "YES" : "no" );
            WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
            if ( gOptions->pEFD->chkMaterialPerType )
            {
                sprintf_s(outputString,256,"#   G3D full material: %s\n", gOptions->pEFD->chkG3DMaterial ? "YES" : "no" );
                WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
            }
        }
    }

    sprintf_s(outputString,256,"# Make Z the up direction instead of Y: %s\n", gOptions->pEFD->chkMakeZUp[gOptions->pEFD->fileType] ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"# Center model: %s\n", gOptions->pEFD->chkCenterModel ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"# Export lesser blocks: %s\n", gOptions->pEFD->chkExportAll ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( gOptions->pEFD->chkExportAll )
    {
        sprintf_s(outputString,256,"# Fatten lesser blocks: %s\n", gOptions->pEFD->chkFatten ? "YES" : "no" );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    // options only available when rendering.
    if ( !gPrint3D )
    {
        sprintf_s(outputString,256,"# Make tree leaves solid: %s\n", gOptions->pEFD->chkLeavesSolid ? "YES" : "no" );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"# Create block faces at the borders: %s\n", gOptions->pEFD->chkBlockFacesAtBorders ? "YES" : "no" );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    sprintf_s(outputString,256,"# Individual blocks: %s\n", gOptions->pEFD->chkIndividualBlocks ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"# Use biomes: %s\n", gOptions->pEFD->chkBiome ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    // now always on by default
    //sprintf_s(outputString,256,"# Merge flat blocks with neighbors: %s\n", gOptions->pEFD->chkMergeFlattop ? "YES" : "no" );
    //WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( gOptions->pEFD->radioRotate0 )
        angle = 0;
    else if ( gOptions->pEFD->radioRotate90 )
        angle = 90;
    else if ( gOptions->pEFD->radioRotate180 )
        angle = 180;
    else
    {
        angle = 270;
        assert(gOptions->pEFD->radioRotate270);
    }

    sprintf_s(outputString,256,"# Rotate model %f degrees\n", angle );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    if ( gOptions->pEFD->radioScaleByBlock )
    {
        sprintf_s(outputString,256,"# Scale model by making each block %g mm high\n", gOptions->pEFD->blockSizeVal[gOptions->pEFD->fileType] );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }
    else if ( gOptions->pEFD->radioScaleByCost )
    {
        sprintf_s(outputString,256,"# Scale model by aiming for a cost of %0.2f for the %s material\n", gOptions->pEFD->costVal, gMtlCostTable[gPhysMtl].name );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }
    else if ( gOptions->pEFD->radioScaleToHeight )
    {
        sprintf_s(outputString,256,"# Scale model by fitting to a height of %g cm\n", gOptions->pEFD->modelHeightVal );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }
    else if ( gOptions->pEFD->radioScaleToMaterial )
    {
        sprintf_s(outputString,256,"# Scale model by using the minimum wall thickness for the %s material\n", gMtlCostTable[gPhysMtl].name );
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    sprintf_s(outputString,256,"# Data operation options:\n#   Fill air bubbles: %s; Seal off entrances: %s; Fill in isolated tunnels in base of model: %s\n",
        (gOptions->pEFD->chkFillBubbles ? "YES" : "no"),
        (gOptions->pEFD->chkSealEntrances ? "YES" : "no"),
        (gOptions->pEFD->chkSealSideTunnels ? "YES" : "no"));
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"#   Connect parts sharing an edge: %s; Connect corner tips: %s; Weld all shared edges: %s\n",
        (gOptions->pEFD->chkConnectParts ? "YES" : "no"),
        (gOptions->pEFD->chkConnectCornerTips ? "YES" : "no"),
        (gOptions->pEFD->chkConnectAllEdges ? "YES" : "no"));
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"#   Delete floating objects: trees and parts smaller than %d blocks: %s\n",
        gOptions->pEFD->floaterCountVal,
        (gOptions->pEFD->chkDeleteFloaters ? "YES" : "no"));
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"#   Hollow out bottom of model, making the walls %g mm thick: %s; Superhollow: %s\n",
        gOptions->pEFD->hollowThicknessVal[gOptions->pEFD->fileType],
        (gOptions->pEFD->chkHollow[gOptions->pEFD->fileType] ? "YES" : "no"),
        (gOptions->pEFD->chkSuperHollow[gOptions->pEFD->fileType] ? "YES" : "no"));
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"# Melt snow blocks: %s\n", gOptions->pEFD->chkMeltSnow ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"#   Debug: show separate parts as colors: %s\n", gOptions->pEFD->chkShowParts ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    sprintf_s(outputString,256,"#   Debug: show weld blocks in bright colors: %s\n", gOptions->pEFD->chkShowWelds ? "YES" : "no" );
    WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

    // write out processing stats for 3D printing
    if ( gOptions->exportFlags & (EXPT_FILL_BUBBLES|EXPT_CONNECT_PARTS|EXPT_DELETE_FLOATING_OBJECTS) )
    {
        sprintf_s(outputString,256,"\n# Cleanup processing summary:\n#   Solid parts: %d\n",
            gStats.numSolidGroups);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & EXPT_FILL_BUBBLES )
    {
        sprintf_s(outputString,256,"#   Air bubbles found and filled (with glass): %d\n",
            gStats.bubblesFound);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & (EXPT_FILL_BUBBLES|EXPT_CONNECT_PARTS) )
    {
        sprintf_s(outputString,256,"#   Total solid parts merged: %d\n",
            gStats.solidGroupsMerged);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & EXPT_CONNECT_PARTS )
    {
        sprintf_s(outputString,256,"#   Number of edge passes made: %d\n",
            gStats.numberManifoldPasses);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"#     Edges found to fix: %d\n",
            gStats.nonManifoldEdgesFound);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"#     Weld blocks added: %d\n",
            gStats.blocksManifoldWelded);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & EXPT_CONNECT_CORNER_TIPS )
    {
        sprintf_s(outputString,256,"#     Tip blocks added: %d\n",
            gStats.blocksCornertipWelded);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & EXPT_DELETE_FLOATING_OBJECTS )
    {
        sprintf_s(outputString,256,"#   Floating parts removed: %d\n",
            gStats.floaterGroupsDeleted);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"#     In these floaters, total blocks removed: %d\n",
            gStats.blocksFloaterDeleted);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    if ( gOptions->exportFlags & EXPT_HOLLOW_BOTTOM )
    {
        sprintf_s(outputString,256,"#   Blocks removed by hollowing: %d\n",
            gStats.blocksHollowed);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

        sprintf_s(outputString,256,"#   Blocks removed by further super-hollowing (i.e. not just vertical hollowing): %d\n",
            gStats.blocksSuperHollowed);
        WERROR(PortaWrite(fh, outputString, strlen(outputString) ));
    }

    return MW_NO_ERROR;
}

// final checks:
// if color, is sum of dimensions too small?
// else, is wall thickness dangerous?
static int finalModelChecks()
{
    int retCode = MW_NO_ERROR;

    // go from most to least serious
    // were there multiple groups? are we 3D printing? and are we not using the "lesser" option?
    if ( gSolidGroups > 1 && gPrint3D && !gOptions->pEFD->chkExportAll )
    {
        // we care only if exporting to 3D print, without lesser.
        retCode |= MW_MULTIPLE_GROUPS_FOUND;
    }
    if ( gPrint3D )
    {
        float inCM = gModel.scale * METERS_TO_CM;
        // check that dimensions are not too large
        if ( inCM * max3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[0] ||
            inCM * med3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[1] ||
            inCM * min3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[2] )
        {
            retCode |= MW_AT_LEAST_ONE_DIMENSION_TOO_HIGH;
        }
        // check dimension sum for material output
        // Really needed only for colored sandstone: http://www.shapeways.com/design-rules/full_color_sandstone
        if ( (gFilledBoxSize[X]+gFilledBoxSize[Y]+gFilledBoxSize[Z]) < gMtlCostTable[gPhysMtl].minDimensionSum*METERS_TO_MM) {
            // give this error only if exporting to a Shapeways type of file
            if ( gOptions->pEFD->fileType == FILE_TYPE_VRML2 )
                retCode |= MW_SUM_OF_DIMENSIONS_IS_LOW;
        }
        if ( gModel.scale < gMtlCostTable[gPhysMtl].minWall )
        {
            retCode |= MW_WALLS_MIGHT_BE_THIN;
        }

        // it's not clear from http://www.shapeways.com/tutorials/how_to_use_meshlab_and_netfabb whether
        // it's one million polygons (we use quads) or one million triangles. We opt for triangles here:
        if ( gModel.faceCount*2 > 1000000 )
        {
            retCode |= MW_TOO_MANY_POLYGONS;
        }
    }

    return retCode;
}

static float computeMaterialCost( int printMaterialType, float blockEdgeSize, int numBlocks, int numMinorBlocks )
{
    // as a guess, take the minor blocks number, i.e. those printed as partial blocks such as steps, and divide by two.
    float ccmMaterial = (float)pow((double)(blockEdgeSize*METERS_TO_CM),3.0)*((float)numBlocks + (float)numMinorBlocks/2.0f);

    // compute volume for materials that have a machine bed volume cost
    float ccmMachine = 0.0f;
    if ( gMtlCostTable[gPhysMtl].costPerMachineCC )
    {
        float volumeBlockCount = computeMachineVolume();
        assert( volumeBlockCount <= gFilledBoxSize[X] * gFilledBoxSize[Y] * gFilledBoxSize[Z] );
        ccmMachine = (float)pow((double)(blockEdgeSize*METERS_TO_CM),3.0)*volumeBlockCount;
    }

    return ( gMtlCostTable[printMaterialType].costHandling + 
        gMtlCostTable[printMaterialType].costPerMachineCC * ccmMachine +
        gMtlCostTable[printMaterialType].costPerCubicCentimeter * ccmMaterial );
}

static void addOutputFilenameToList(wchar_t *filename)
{
    assert ( gOutputFileList->count < MAX_OUTPUT_FILES );

    wcsncpy_s(gOutputFileList->name[gOutputFileList->count],MAX_PATH,filename,MAX_PATH);
    gOutputFileList->count++;
}

// substitute ' ' to '_'
static void spacesToUnderlines( wchar_t *targetString )
{
    wchar_t *strPtr;

    strPtr = wcschr(targetString,(int)' ');
    while ( strPtr )
    {
        // changes targetString itself
        *strPtr = '_';
        strPtr = wcschr(targetString,(int)' ');
    }
}

// substitute ' ' to '_'
static void spacesToUnderlinesChar( char *targetString )
{
    char *strPtr;

    strPtr = strchr(targetString,(int)' ');
    while ( strPtr )
    {
        // changes targetString itself
        *strPtr = '_';
        strPtr = strchr(targetString,(int)' ');
    }
}

#define GET_PNG_TEXEL( r,g,b,a, ip ) \
    (r) = (unsigned char)((ip) & 0xff); \
    (g) = (unsigned char)(((ip)>>8) & 0xff); \
    (b) = (unsigned char)(((ip)>>16) & 0xff); \
    (a) = (unsigned char)(((ip)>>24) & 0xff);

#define SET_PNG_TEXEL( ip, r,g,b,a ) \
    (ip) = ((a)<<24)|((b)<<16)|((g)<<8)|(r);

static void copyPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, progimage_info *src, int src_x_min, int src_y_min)
{
    int row;
    int dst_offset,src_offset;

    for ( row = 0; row < size_y; row++ )
    {
        dst_offset = ((dst_y_min+row)*dst->width + dst_x_min) * 4;
        src_offset = ((src_y_min+row)*src->width + src_x_min) * 4;
        memcpy(&dst->image_data[dst_offset], &src->image_data[src_offset], size_x*4);
    }
}

static int tileIsSemitransparent(progimage_info *src, int col, int row)
{
    int r,c;

    for ( r = 0; r < gModel.tileSize; r++ )
    {
        unsigned char *src_offset = &src->image_data[((row*gModel.tileSize + r)*src->width + col*gModel.tileSize) * 4 + 3];
        for ( c = 0; c < gModel.tileSize; c++ )
        {
            if ( *src_offset <= 0 || *src_offset >= 255 )
            {
                return 0;
            }
            src_offset += 4;
        }
    }
    return 1;
}

//static int tileIsCutout(progimage_info *src, int col, int row)
//{
//    int r,c;
//    int clearFound=0;
//    int solidFound=0;
//
//    for ( r = 0; r < gModel.tileSize; r++ )
//    {
//        unsigned char *src_offset = src->image_data + ((row*gModel.tileSize + r)*src->width + col*gModel.tileSize) * 4 + 3;
//        for ( c = 0; c < gModel.tileSize; c++ )
//        {
//            // we could also test if the tile is semitransparent and reject if it is;
//            // this seems extreme, I can imagine alternate texture packs going semitransparent
//            //if ( *src_offset > 0 || *src_offset < 255 )
//            //{
//            //    return 0;
//            //}
//            //else
//            // look for if pixels found that are fully transparent or fully solid
//            if ( *src_offset == 0 )
//            {
//                clearFound=1;
//            }
//            else if ( *src_offset == 255 )
//            {
//                solidFound=1;
//            }
//            src_offset += 4;
//        }
//    }
//    return (clearFound && solidFound);
//}

static int tileIsOpaque(progimage_info *src, int col, int row)
{
    int r,c;

    for ( r = 0; r < gModel.tileSize; r++ )
    {
        unsigned char *src_offset = &src->image_data[((row*gModel.tileSize + r)*src->width + col*gModel.tileSize) * 4 + 3];
        for ( c = 0; c < gModel.tileSize; c++ )
        {
            if ( *src_offset < 255 )
            {
                return 0;
            }
            src_offset += 4;
        }
    }
    return 1;
}

static void setColorPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, unsigned int value)
{
    int row,col;
    int dst_offset;
    unsigned int *di = ((unsigned int *)(&dst->image_data[0])) + (dst_y_min * dst->width + dst_x_min);

    for ( row = 0; row < size_y; row++ )
    {
        dst_offset = row * dst->width;
        for ( col = 0; col < size_x; col++ )
        {
            *(di+dst_offset+col) = value;
        }
    }
}

// startStretch is how far down the stretch should start from, e.g. 0.25 means start 1/4th the way down and stretch 3/4ths of the content up
static void stretchSwatchToTop(progimage_info *dst, int swatchIndex, float startStretch)
{
    int row,drow,dcol,dst_offset,src_offset;
    unsigned int *di,*si;
    SWATCH_TO_COL_ROW( swatchIndex, dcol, drow );
    di = ((unsigned int *)(&dst->image_data[0])) + (drow * gModel.swatchSize * dst->width + dcol * gModel.swatchSize);
    si = di + (int)(startStretch * gModel.swatchSize) * dst->width;

    for ( row = 0; row < gModel.swatchSize; row++ )
    {
        dst_offset = row * dst->width;
        src_offset = (int)(row * (1.0f-startStretch)) * dst->width ;

        memcpy(di+dst_offset, si+src_offset, gModel.swatchSize*4);
    }
}

static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, int tileSize, progimage_info *src, int src_x, int src_y)
{
    copyPNGArea(dst, dst_x*tileSize, dst_y*tileSize, tileSize, tileSize, src, src_x*tileSize, src_y*tileSize );
}

#ifdef _DEBUG
static void drawPNGTileLetterR( progimage_info *dst, int x, int y, int tileSize )
{
    int row[12] = {1,1,1,2,2,3,3,3,4,4,5,5};
    int col[12] = {1,2,3,1,4,1,2,3,1,4,1,4};

    int i;
    for ( i = 0; i < 12; i++ )
    {
        unsigned int *di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + 8 + row[i]) * dst->width + x*tileSize + col[i]);
        *di = 0xffff00ff;
    }
}
#endif

static void setColorPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned int value)
{
    int row, col;

    assert( x*tileSize+tileSize-1 < (int)dst->width );

    for ( row = 0; row < tileSize; row++ )
    {
        unsigned int *di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x*tileSize);
        for ( col = 0; col < tileSize; col++ )
        {
            *di++ = value;
        }
    }
}

// value is the amount to subtract things from white: 0 is no noise, 1.0 is total noise
static void addNoisePNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a, float noise )
{
    int row, col;
    unsigned int *di;

    assert( x*tileSize+tileSize-1 < (int)dst->width );

    for ( row = 0; row < tileSize; row++ )
    {
        di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x*tileSize);
        for ( col = 0; col < tileSize; col++ )
        {
            double grayscale = 1.0 - noise * myrand();
            unsigned char newr = (unsigned char)((double)r * grayscale);
            unsigned char newg = (unsigned char)((double)g * grayscale);
            unsigned char newb = (unsigned char)((double)b * grayscale);
            unsigned int value = (a<<24)|(newb<<16)|(newg<<8)|newr;
            *di++ = value;
        }
    }
}

static void multiplyPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
    int row, col;
    unsigned int *di;

    assert( x*tileSize+tileSize-1 < (int)dst->width );

    for ( row = 0; row < tileSize; row++ )
    {
        di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x*tileSize);
        for ( col = 0; col < tileSize; col++ )
        {
            unsigned int value = *di;
            unsigned char dr,dg,db,da;
            GET_PNG_TEXEL(dr,dg,db,da, value);
            SET_PNG_TEXEL(*di, (unsigned char)(dr * r / 255), (unsigned char)(dg * g / 255), (unsigned char)(db * b / 255), (unsigned char)(da * a / 255));
            di++;
        }
    }
}

// in-place form of compositePNGSwatches, with no blending: fill in destination with source wherever destination is 0 alpha
//static int fillZeroAlphasPNGSwatch(progimage_info *dst, int destSwatch, int sourceSwatch, int swatchSize, int swatchesPerRow)
//{
//    int scol = sourceSwatch % swatchesPerRow;
//    int srow = sourceSwatch / swatchesPerRow;
//    int dcol = destSwatch % swatchesPerRow;
//    int drow = destSwatch / swatchesPerRow;
//    int row, col;
//    unsigned int *si = (unsigned int *)(dst->image_data) + srow*swatchSize*dst->width + scol*swatchSize;
//    unsigned int *di = (unsigned int *)(dst->image_data) + drow*swatchSize*dst->width + dcol*swatchSize;
//    unsigned int *csi, *cdi;
//
//    for ( row = 0; row < swatchSize; row++ )
//    {
//        int offset = row*dst->width;
//        csi = si + offset;
//        cdi = di + offset;
//        for ( col = 0; col < swatchSize; col++ )
//        {
//            unsigned char da = (unsigned char)(((*cdi)>>24) & 0xff);
//            // if alpha is zero, set it to swatch color
//            if ( da == 0 )
//            {
//                *cdi++ = *csi++;
//            }
//            else
//            {
//                cdi++;
//                csi++;
//            }
//        }
//    }
//    return 0;
//
//}
//

static void rotatePNGTile(progimage_info *dst, int dcol, int drow, int scol, int srow, int angle, int swatchSize )
{
    int row, col;
    int m00,m01,m10,m11,offset0,offset1;
    unsigned int *dul = ((unsigned int*)&dst->image_data[0]) + drow*swatchSize*dst->width + dcol*swatchSize;
    unsigned int *sul = ((unsigned int*)&dst->image_data[0]) + srow*swatchSize*dst->width + scol*swatchSize;

    // cannot rotate in place: need to copy to somewhere in a separate call, then rotate back to final destination
    assert( ( dcol != scol ) || ( drow != srow ) );


    switch ( angle )
    {
    default:
    case 0:
        m00 = m11 = 1;
        m01 = m10 = 0;
        offset0 = offset1 = 0;
        break;
    case 90:
        m01 = 1;
        m10 = -1;
        m00 = m11 = 0;
        offset0 = swatchSize-1;
        offset1 = 0;
        break;
    case 180:
        m00 = m11 = -1;
        m01 = m10 = 0;
        offset0 = offset1 = swatchSize-1;
        break;
    case 270:
        m01 = -1;
        m10 = 1;
        m00 = m11 = 0;
        offset0 = 0;
        offset1 = swatchSize-1;
        break;
    }
    {
        for ( row = 0; row < swatchSize; row++ )
        {
            for ( col = 0; col < swatchSize; col++ )
            {
                unsigned int *di;
                unsigned int *si = sul + col + row*dst->width;
                int docol = col*m00 + row*m10 + offset0;
                int dorow = col*m01 + row*m11 + offset1;
                di = dul + docol + dorow*dst->width;
                *di = *si;
            }
        }
    }
}

static void blendTwoSwatches( progimage_info *dst, int txrSwatch, int solidSwatch, float blend, unsigned char alpha )
{
    // these are swatch locations in index form (not yet multiplied by swatch size itself)
    int tcol = txrSwatch % gModel.swatchesPerRow;
    int trow = txrSwatch / gModel.swatchesPerRow;
    int scol = solidSwatch % gModel.swatchesPerRow;
    int srow = solidSwatch / gModel.swatchesPerRow;
    // upper left corner, starting location, of each: over, under, destination
    unsigned int *ti = (unsigned int *)(&dst->image_data[0]) + trow*gModel.swatchSize*dst->width + tcol*gModel.swatchSize;
    unsigned int *si = (unsigned int *)(&dst->image_data[0]) + srow*gModel.swatchSize*dst->width + scol*gModel.swatchSize;

    int row,col;
    unsigned int *cti,*csi;

    for ( row = 0; row < gModel.swatchSize; row++ )
    {
        int offset = row*dst->width;
        cti = ti + offset;
        csi = si + offset;

        for ( col = 0; col < gModel.swatchSize; col++ )
        {
            unsigned char tr,tg,tb,ta;
            unsigned char sr,sg,sb,sa;

            float oneMinusBlend = 1.0f - blend;

            GET_PNG_TEXEL( tr,tg,tb,ta, *cti ); // ta is unused
            GET_PNG_TEXEL( sr,sg,sb,sa, *csi );	// sa is unused

            tr = (unsigned char)(tr*oneMinusBlend + sr*blend);
            tg = (unsigned char)(tg*oneMinusBlend + sg*blend);
            tb = (unsigned char)(tb*oneMinusBlend + sb*blend);
            ta = alpha;
            SET_PNG_TEXEL( *cti, tr,tg,tb,ta );
            cti++;
            csi++;
        }
    }
}

// for transparent pixels, read from four neighbors and take average of all that exist. Second pass then marks these transparent pixels as opaque
static void bleedPNGSwatch(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha )
{
    int tileSize16 = (swatchSize-2)/16;

    // these are swatch locations in index form (not yet multiplied by swatch size itself)
    int dcol = dstSwatch % swatchesPerRow;
    int drow = dstSwatch / swatchesPerRow;
    // upper left corner, starting location, but then pulled in by one, and the x offset is built in (y offset inside tile is done by loop)
    unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize+1)*dst->width + dcol*swatchSize + 1 + xmin*tileSize16;

    int row,col;
    unsigned char dr,dg,db,da;
    unsigned int *cdsti;

    for ( row = ymin*tileSize16; row < ymax*tileSize16; row++ )
    {
        int offset = row*dst->width;
        cdsti = dsti + offset;
        for ( col = xmin*tileSize16; col < xmax*tileSize16; col++ )
        {
            GET_PNG_TEXEL( dr,dg,db,da, *cdsti );

            if ( da == 0 )
            {
                // texel is transparent, so check its neighbors
                int i;
                int neighborCount = 0;
                unsigned char nr,ng,nb,na;
                unsigned int *cneighi;
                int sumr = 0;
                int sumg = 0;
                int sumb = 0;

                // first try four neighbors by edge
                for ( i = 0; i < 4; i++ )
                {
                    switch ( i )
                    {
                    default:	// to make compiler happy
                    case 0:
                        cneighi = cdsti - dst->width;
                        break;
                    case 1:
                        cneighi = cdsti + dst->width;
                        break;
                    case 2:
                        cneighi = cdsti - 1;
                        break;
                    case 3:
                        cneighi = cdsti + 1;
                        break;
                    }
                    GET_PNG_TEXEL( nr,ng,nb,na, *cneighi );
                    // exact test, so we ignore texels we're changing in place
                    if ( na == 255 )
                    {
                        sumr += nr;
                        sumg += ng;
                        sumb += nb;
                        neighborCount++;
                    }
                }

                // anything happen?
                if ( neighborCount > 0 )
                {
                    // store texel in place, with a tag alpha of 123
                    dr = (unsigned char)(sumr / neighborCount);
                    dg = (unsigned char)(sumg / neighborCount);
                    db = (unsigned char)(sumb / neighborCount);
                    da = 123;
                    SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
                }
                else
                {
                    // try four diagonal neighbors
                    for ( i = 0; i < 4; i++ )
                    {
                        switch ( i )
                        {
                        default:	// to make compiler happy
                        case 0:
                            cneighi = cdsti - dst->width - 1;
                            break;
                        case 1:
                            cneighi = cdsti - dst->width + 1;
                            break;
                        case 2:
                            cneighi = cdsti + dst->width - 1;
                            break;
                        case 3:
                            cneighi = cdsti + dst->width + 1;
                            break;
                        }
                        GET_PNG_TEXEL( nr,ng,nb,na, *cneighi );
                        // exact test, so we ignore texels we're changing in place
                        if ( na == 255 )
                        {
                            sumr += nr;
                            sumg += ng;
                            sumb += nb;
                            neighborCount++;
                        }
                    }

                    // anything happen?
                    if ( neighborCount > 0 )
                    {
                        // store texel in place, with a tag alpha of 123
                        dr = (unsigned char)(sumr / neighborCount);
                        dg = (unsigned char)(sumg / neighborCount);
                        db = (unsigned char)(sumb / neighborCount);
                        da = 123;
                        SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
                    }
                }
            }
            cdsti++;
        }
    }

    // now go back and make all 123 alpha texels truly opaque
    // upper left corner, starting location, but then pulled in by one
    dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize+1)*dst->width + dcol*swatchSize + 1 + xmin*tileSize16;

    for ( row = ymin*tileSize16; row < ymax*tileSize16; row++ )
    {
        int offset = row*dst->width;
        cdsti = dsti + offset;
        for ( col = xmin*tileSize16; col < xmax*tileSize16; col++ )
        {
            GET_PNG_TEXEL( dr,dg,db,da, *cdsti );

            if ( da == 123 )
            {
                da = (unsigned char)alpha;
                SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
            }
            cdsti++;
        }
    }
}

static void setAlphaPNGSwatch(progimage_info *dst, int dstSwatch, int swatchSize, int swatchesPerRow, unsigned char alpha )
{
    // these are swatch locations in index form (not yet multiplied by swatch size itself)
    int dcol = dstSwatch % swatchesPerRow;
    int drow = dstSwatch / swatchesPerRow;
    // upper left corner, starting location
    unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + drow*swatchSize*dst->width + dcol*swatchSize;

    int row,col;
    unsigned char dr,dg,db,da;
    unsigned int *cdsti;

    for ( row = 0; row < swatchSize; row++ )
    {
        int offset = row*dst->width;
        cdsti = dsti + offset;
        for ( col = 0; col < swatchSize; col++ )
        {
            GET_PNG_TEXEL( dr,dg,db,da, *cdsti );
            da = alpha;
            SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
            cdsti++;
        }
    }
}


static void compositePNGSwatches(progimage_info *dst, int dstSwatch, int overSwatch, int underSwatch, int swatchSize, int swatchesPerRow, int forceSolid )
{
    // these are swatch locations in index form (not yet multiplied by swatch size itself)
    int ocol = overSwatch % swatchesPerRow;
    int orow = overSwatch / swatchesPerRow;
    int ucol = underSwatch % swatchesPerRow;
    int urow = underSwatch / swatchesPerRow;
    int dcol = dstSwatch % swatchesPerRow;
    int drow = dstSwatch / swatchesPerRow;
    // upper left corner, starting location, of each: over, under, destination
    unsigned int *overi = (unsigned int *)(&dst->image_data[0]) + orow*swatchSize*dst->width + ocol*swatchSize;
    unsigned int *underi = (unsigned int *)(&dst->image_data[0]) + urow*swatchSize*dst->width + ucol*swatchSize;
    unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + drow*swatchSize*dst->width + dcol*swatchSize;

    int row,col;
    unsigned char or,og,ob,oa;
    unsigned char ur,ug,ub,ua;
    unsigned char dr,dg,db,da;
    unsigned int *coveri,*cunderi,*cdsti;

    for ( row = 0; row < swatchSize; row++ )
    {
        int offset = row*dst->width;
        coveri = overi + offset;
        cunderi = underi + offset;
        cdsti = dsti + offset;
        for ( col = 0; col < swatchSize; col++ )
        {
            unsigned char oma;

            GET_PNG_TEXEL( or,og,ob,oa, *coveri );
            GET_PNG_TEXEL( ur,ug,ub,ua, *cunderi );

            oma = 255 - oa;

            if ( forceSolid )
            {
                ua = 255;
            }

            // finally have all data: composite o over u
            if ( oa == 0 )
            {
                // copy the under pixel
                SET_PNG_TEXEL( *cdsti, ur,ug,ub,ua );
                // essentially *cdsti = *cunderi;, but with ua possibly changed
            }
            else if ( oma == 0 )
            {
                // copy the over pixel
                *cdsti = *coveri;
            }
            else
            {
                // full blend must be done: use over, http://en.wikipedia.org/wiki/Alpha_compositing
                dr = (unsigned char)((or*oa*255 + ur*ua*oma)/(255*255));
                dg = (unsigned char)((og*oa*255 + ug*ua*oma)/(255*255));
                db = (unsigned char)((ob*oa*255 + ub*ua*oma)/(255*255));
                da = (unsigned char)((oa*255 + ua*oma)/255);
                SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
            }
            coveri++;
            cunderi++;
            cdsti++;
        }
    }
}

static int convertRGBAtoRGBandWrite(progimage_info *src, wchar_t *filename)
{
    int retCode = MW_NO_ERROR;
    int row, col;
    unsigned char *imageDst, *imageSrc;
    progimage_info dst;
    dst.height = src->height;
    dst.width = src->width;
    dst.image_data.resize(src->height*src->width * 3);

    imageSrc = &src->image_data[0];
    imageDst = &dst.image_data[0];

    for (row = 0; row < dst.height; row++)
    {
        for (col = 0; col < dst.width; col++)
        {
            // copy RGB only
            *imageDst++ = *imageSrc++;
            *imageDst++ = *imageSrc++;
            *imageDst++ = *imageSrc++;
            imageSrc++;
        }
    }

    retCode |= writepng(&dst, 3, filename);
    addOutputFilenameToList(filename);

    writepng_cleanup(&dst);

    return retCode;
}

// for debugging
static void convertAlphaToGrayscale( progimage_info *dst )
{
    int row, col;
    unsigned int *di = ((unsigned int *)(&dst->image_data[0]));

    for ( row = 0; row < dst->height; row++ )
    {
        for ( col = 0; col < dst->width; col++ )
        {
            // get alpha of pixel, use as grayscale
            unsigned int value = *di;
            unsigned char dr,dg,db,da;
            GET_PNG_TEXEL(dr,dg,db,da, value);	// dr, dg, db unused
            // we set ALL the values to the luminance. Sketchfab, by default, uses the alpha
            // channel from the alpha map, not luminance. So, set them all. I'm not sure if
            // this will affect Maya, MAX, Blender, or Cinema4D - need to test.
            SET_PNG_TEXEL(*di, da, da, da, da);
            di++;
        }
    }
}

///////////////////////////////////////////////////////////
//
// Utility functions, headers not included above
// suffix is of form ".stl" - includes dot
static void ensureSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix )
{
    int hasSuffix=0;

    // Prep file name: see if it has suffix already
    if ( wcsnlen(src,MAX_PATH) > wcsnlen(suffix,MAX_PATH) )
    {
        // look for suffix
        wchar_t foundSuffix[MAX_PATH];
        wcsncpy_s(foundSuffix,MAX_PATH,src + wcsnlen(src,MAX_PATH)-wcsnlen(suffix,MAX_PATH),20);
        _wcslwr_s(foundSuffix,MAX_PATH);
        if (wcscmp(foundSuffix,suffix) == 0)
        {
            hasSuffix = 1;
        }
    }

    wcscpy_s(dst,MAX_PATH,src);
    // was the suffix found?
    if ( !hasSuffix )
    {
        //// see if it has *any* suffix
        //const wchar_t *noPath = removePath( dst );
        //wchar_t *dstDot = wcsrchr(noPath,(wchar_t )'.');
        //if ( dstDot )
        //{
        //    // found a dot in the filename itself - get rid of this suffix
        //    *dstDot = (wchar_t)0;
        //}
        // and add the suffix
        wcscat_s(dst,MAX_PATH,suffix);
    }
}

static void removeSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix )
{
    // glue the suffix on if it's missing
    ensureSuffix( dst, src, suffix );
    // then remove it. Yeah, pretty silly, but minimal code
    if ( wcslen(dst) > wcslen(suffix) )
        dst[wcslen(dst)-wcslen(suffix)] = (wchar_t)0;
}

//static const wchar_t *removePath( const wchar_t *src )
//{
//    // find last \ in string
//    const wchar_t *strPtr = wcsrchr(src,(int)'\\');
//    if ( strPtr )
//        // found a \, so move up past it
//        strPtr++;
//    else
//    {
//        // look for /
//        strPtr = wcsrchr(src,(int)'/');
//        if ( strPtr )
//            // found a /, so move up past it
//            strPtr++;
//        else
//            // no \ or / found, just return string itself
//            return src;
//    }
//
//    return strPtr;
//}

static const char *removePathChar( const char *src )
{
    // find last \ in string
    const char *strPtr = strrchr(src,(int)'\\');
    if ( strPtr )
        // found a \, so move up past it
        strPtr++;
    else
    {
        // look for /
        const char *strPtr = strrchr(src,(int)'/');
        if ( strPtr )
            // found a /, so move up past it
            strPtr++;
        else
            // no \ or / found, just return string itself
            return src;
    }

    return strPtr;
}

static void getPathAndRoot( const wchar_t *src, int fileType, wchar_t *path, wchar_t *root )
{
    wchar_t *rootPtr;
    wchar_t tfilename[MAX_PATH];

    wcscpy_s(path,MAX_PATH,src);
    // find last \ in string
    rootPtr = wcsrchr(path,(wchar_t)'\\');
    if ( rootPtr )
        // found a \, so move up past it
        rootPtr++;
    else
    {
        // look for /
        rootPtr = wcsrchr(path,(wchar_t)'/');
        if ( rootPtr )
            // found a /, so move up past it
            rootPtr++;
        else
            // no \ or / found, just return string itself
            rootPtr = path;
    }
    // split at rootPtr
    wcscpy_s(tfilename,MAX_PATH,rootPtr);
    // this sets last character of path to null (end it)
    *rootPtr = (wchar_t)0;

    // The root doesn't really have the root at this point.
    // RemoveSuffix, depending on the file type
    switch ( fileType )
    {
    case FILE_TYPE_WAVEFRONT_REL_OBJ:
    case FILE_TYPE_WAVEFRONT_ABS_OBJ:
        removeSuffix(root,tfilename,L".obj");
        break;
    case FILE_TYPE_BINARY_MAGICS_STL:
    case FILE_TYPE_BINARY_VISCAM_STL:
    case FILE_TYPE_ASCII_STL:
        removeSuffix(root,tfilename,L".stl");
        break;
    case FILE_TYPE_VRML2:
        removeSuffix(root,tfilename,L".wrl");
        break;
    case FILE_TYPE_SCHEMATIC:
        removeSuffix(root,tfilename,L".schematic");
    }
}

static void concatFileName2(wchar_t *dst, const wchar_t *src1, const wchar_t *src2)
{
    wcscpy_s(dst,MAX_PATH,src1);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src2);
}

static void concatFileName3(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3)
{
    wcscpy_s(dst,MAX_PATH,src1);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src2);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src3);
}

static void concatFileName4(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3, const wchar_t *src4)
{
    wcscpy_s(dst,MAX_PATH,src1);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src2);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src3);
    wcscat_s(dst,MAX_PATH-wcslen(dst),src4);
}

// assumes MAX_PATH length for both strings
static void wcharToChar( const wchar_t *inWString, char *outString )
{
    //WideCharToMultiByte(CP_UTF8,0,inWString,-1,outString,MAX_PATH,NULL,NULL);
    int i;
    int oct=0;

    for ( i = 0; i < MAX_PATH; i++ )
    {
        int val = inWString[i];
        if (val >= 0 && val < 128)
        {
            outString[oct++] = (char)val;
        }
        if (val == 0)
        {
            // done: did anything get copied? (other than 0 at end)
            if ( oct <= 1 )
            {
                // put some sort of string in for the converted string, so that at least
                // the user will have something to work from.
                strcpy_s(outString,MAX_PATH,"[Block Test World]");
            }
            return;
        }
    }
    // it is unlikely that the loop above didn't terminate and return
    assert(0);
    return;
}

static void charToWchar( char *inString, wchar_t *outWString )
{
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,inString,-1,outWString,MAX_PATH);
    //MultiByteToWideChar(CP_UTF8,0,inString,-1,outWString,MAX_PATH);
}

static void wcharCleanse( wchar_t *wstring )
{
    char tempString[MAX_PATH];
    wcharToChar( wstring, tempString );
    charToWchar( tempString, wstring );
}

#define M1  134456
#define IA1   8121
#define IC1  28411
#define RM1 1.0/M1

static void myseedrand( long seed )
{
    gMySeed = seed;
}

static double myrand()
{
    gMySeed = (IC1+gMySeed*IA1) % M1;
    return gMySeed * RM1;
}