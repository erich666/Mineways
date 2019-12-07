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

#define FORCE_SOLID		1
#define FORCE_CUTOUT	2

// 1/16 - one pixel in a 16x16 tile
#define ONE_PIXEL	0.0625f

// if we see this value, it's probably bad
#define UNINITIALIZED_INT	-98789

typedef struct BoxCell {
    int group;	// for 3D printing, what connected group a block is part of
    unsigned short type;
    unsigned short origType;
    unsigned char flatFlags;	// pointer to which origType to use for face output, for "merged" snow, redstone, etc. in cell above
    unsigned char data;     // extra data for block (wool color, etc.); note that four top bits are not used
} BoxCell;

typedef struct BoxGroup 
{
    int groupID;	// which group number am I? Always matches index of gGroupInfo array - TODO: maybe could be made an unsigned short...
    int population;	// how many in the group?
    int solid;		// solid or air group?
    IBox bounds;	// the box that this group occupies. Not valid if population is 0 (merged)
} BoxGroup;

static BoxCell *gBoxData = NULL;
static unsigned char *gBiome = NULL;
static IPoint gBoxSize;
static int gBoxSizeYZ = UNINITIALIZED_INT;
static int gBoxSizeXYZ = UNINITIALIZED_INT;
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
    int swatchLoc;	// where this record is stored, for purposes of outputting comments and sorting
} UVOutput;

// We store a set of normals that get reused: 
// 26 predefined, plus 30 known to be needed for 196 blocks, plus another 400 for water and lava and just in case.
// Extra normals are from torches, levers, brewing stands, and sunflowers
#define NORMAL_LIST_SIZE (26+30+400)

// the UVs appears in the range [0-16],[0-16] within the 16x16 tiles.
#define NUM_UV_GRID_RESOLUTION	16
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
	// a 17 x 17 grid of all the possible UV locations in a tile
	int uvGridListCount;	// number of locations used in uvGridList
	int uvGridList[(NUM_UV_GRID_RESOLUTION + 1)*(NUM_UV_GRID_RESOLUTION + 1)];
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

    // The absolute maximum number of materials possible. It's actually much smaller than this, as data values
    // do not usually generate sub-materials, but we're playing it safe.
#define NUM_SUBMATERIALS	2000
    unsigned int mtlList[NUM_SUBMATERIALS];
	bool tileList[TOTAL_TILES];
	int tileListCount; // number of tiles actually used in tileList
    int mtlCount;

    progimage_info *pInputTerrainImage;
	bool terrainImageNotFound;

    int textureResolution;  // size of output texture
    float invTextureResolution; // inverse, commonly used
    int terrainWidth;   // width of input image (needed for noisy textures, when an image is not actually read in)
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
	int mcVersion;	// 12 for 1.12.2 and earlier, 13 for 1.13 and later
} Model;

static Model gModel;

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

static int gSolidGroups = UNINITIALIZED_INT;
static int gAirGroups = UNINITIALIZED_INT;

static int gGroupListSize = 0;
static BoxGroup *gGroupList = NULL;
static int gGroupCount = UNINITIALIZED_INT;

// offsets in box coordinates to the neighboring faces
static int gFaceOffset[6];
// flat flag for a neighbor that points to the original block
static int gFlagPointsTo[6];

static ProgressCallback *gpCallback = NULL;

static Options *gOptions = NULL;

static FileList *gOutputFileList = NULL;

static int gExportTexture = 0;
static int gExportTiles = 0;
static int g3d = 0;
// whether we're outputting a print or render
static int gPrint3D=0;

static int gPhysMtl;

static float gUnitsScale=1.0f;

static int gExportBillboards=0;

static int gMajorVersion=0;
static int gMinorVersion=0;
// used to be used for flowerpots, before we read in Block Entities. Now not really needed, but left in for the future.
// 0 means version 1.8 and earlier
static int gMinecraftWorldVersion = 0;
// translate the number above to a version number, e.g. 12, 13, 14 for 1.12, 1.13, 1.14
static int gMcVersion = 0;

static int gBadBlocksInModel=0;

// If set, the current faces being output will (probably) be transformed later.
// This is important to know for merging faces: if faces are to later be rotated, etc.,
// then their geometric coordinates cannot be used for seeing if the face should be removed
// because it is neighboring something that covers it.
// This is a global hack, but I didn't want to add this variable *everywhere* when it's usually 0.
// Basically, if you are going to transform geometry being created to a new spot, set this to true
// before creating the geometry, then false after.
static int gUsingTransform=0;

#define NUM_NORMALS_STORED 42

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

// skip the 8 directions for banners
// for coral top of block fans
#define DIRECTION_UP_LO_X 26
#define DIRECTION_DN_HI_X 27
#define DIRECTION_UP_HI_X 28
#define DIRECTION_DN_LO_X 29
#define DIRECTION_UP_LO_Z 30
#define DIRECTION_DN_HI_Z 31
#define DIRECTION_UP_HI_Z 32
#define DIRECTION_DN_LO_Z 33

// for coral top of block fans
#define DIRECTION_UP_TOP_WEST	34
#define DIRECTION_DN_TOP_WEST	35
#define DIRECTION_UP_TOP_EAST	36
#define DIRECTION_DN_TOP_EAST	37
#define DIRECTION_UP_TOP_NORTH	38
#define DIRECTION_DN_TOP_NORTH	39
#define DIRECTION_UP_TOP_SOUTH	40
#define DIRECTION_DN_TOP_SOUTH	41

// for rotUV, flips LO face
#define FLIP_X_FACE_VERTICALLY	0x01
#define FLIP_Z_FACE_VERTICALLY	0x02
#define ROTATE_TOP_AND_BOTTOM	0x04
#define REVOLVE_INDICES			0x08
#define ROTATE_X_FACE_90		0x10

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
#define BB_BOTTOM		7
#define BB_TOP			8
#define BB_FAN			9
#define BB_WALL_FAN	   10

// We used to offset past NUM_BLOCKS for textures, as there was a corresponding solid block color for
// each block ID, just in case. Now we don't add NUM_BLOCKS, as all blocks have textures
#define SWATCH_INDEX( col, row ) ((col) + (row)*16)

// row & column to swatch location; removed NUM_BLOCKS + offset, as no longer needed.
#define SWATCH_XY_TO_INDEX(x,y) ((y)*16 + (x))

// these are swatches that we will use for other things;
// The swatches reused are the "breaking block" animations, which we'll never need
#define TORCH_TOP               SWATCH_INDEX( 0,15 )
#define RS_TORCH_TOP_ON         SWATCH_INDEX( 1,15 )
#define RS_TORCH_TOP_OFF        SWATCH_INDEX( 2,15 )
#define REDSTONE_WIRE_VERT		SWATCH_INDEX( 4,10 )
#define REDSTONE_WIRE_HORIZ		SWATCH_INDEX( 5,10 )
#define REDSTONE_WIRE_DOT		SWATCH_INDEX( 4,11 )
#define REDSTONE_WIRE_ANGLED_2  SWATCH_INDEX( 3,15 )
#define REDSTONE_WIRE_3         SWATCH_INDEX( 4,15 )
#define REDSTONE_WIRE_4         SWATCH_INDEX( 6,26 )
#define REDSTONE_WIRE_OVERLAY   SWATCH_INDEX( 5,26 )

#define REDSTONE_WIRE_VERT_OFF		SWATCH_INDEX( 10,26 )
#define REDSTONE_WIRE_HORIZ_OFF		SWATCH_INDEX( 11,26 )
#define REDSTONE_WIRE_DOT_OFF		SWATCH_INDEX( 12,26 )
#define REDSTONE_WIRE_ANGLED_2_OFF  SWATCH_INDEX( 13,26 )
#define REDSTONE_WIRE_3_OFF			SWATCH_INDEX( 14,26 )
#define REDSTONE_WIRE_4_OFF         SWATCH_INDEX( 15,26 )

// these spots are used for compositing, as temporary places to put swatches to edit
// TODO - make separate hunks of memory that don't get output.
#define SWATCH_WORKSPACE        SWATCH_INDEX( 8, 2 )


wchar_t gOutputFilePath[MAX_PATH_AND_FILE];
wchar_t gOutputFileRoot[MAX_PATH_AND_FILE];
wchar_t gOutputFileRootClean[MAX_PATH_AND_FILE]; // used by all files that are referenced inside text files
char gOutputFileRootCleanChar[MAX_PATH_AND_FILE];

// how many blocks are needed to make a thick enough wall
static int gWallBlockThickness = UNINITIALIZED_INT;
// how many is the user specifying for hollow walls
static int gHollowBlockThickness = UNINITIALIZED_INT;

static int gBlockCount = UNINITIALIZED_INT;

static int gMinorBlockCount = UNINITIALIZED_INT;

static int gDebugTransparentType = UNINITIALIZED_INT;

static long gMySeed = 12345;

// number in lode_png when file not found
#define PNG_FILE_DOES_NOT_EXIST		78

// Ignore gOptions->pEFD->chkCompositeOverlay when exporting tiles
#define CHECK_COMPOSITE_OVERLAY	(gOptions->pEFD->chkCompositeOverlay && !gExportTiles)


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

#define BOX_INDEX_TO_WORLD_XYZ(index,x,y,z) \
	x = (index)/gBoxSizeYZ - gWorld2BoxOffset[X]; \
	z = ((index) % gBoxSizeYZ) / gBoxSize[Y] - gWorld2BoxOffset[Z]; \
	y = ((index) % gBoxSize[Y]) - gWorld2BoxOffset[Y];

#define BOX_INDEX_TO_WORLD_XZ(index,x,z) \
	x = (index)/gBoxSizeYZ - gWorld2BoxOffset[X]; \
	z = ((index) % gBoxSizeYZ) / gBoxSize[Y] - gWorld2BoxOffset[Z];

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
#define PG_CLEANUP 0.91f
// leave some time for zipping files
#define PG_END 0.95f

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


// objects that are waterlogged should be considered fully in water, as if the block was in water
#define IS_FLUID(tval)	((tval) >= BLOCK_WATER && (tval) <= BLOCK_STATIONARY_LAVA)
#define IS_NOT_FLUID(tval)	(((tval) < BLOCK_WATER) || ((tval) > BLOCK_STATIONARY_LAVA))
#define IS_WATER(tval)	(((tval) == BLOCK_WATER) || ((tval) == BLOCK_STATIONARY_WATER))

#define IS_WATERLOGGED(tval,bi) ((gBlockDefinitions[tval].flags & BLF_WATERLOG) || \
								((gBlockDefinitions[tval].flags & BLF_MAYWATERLOG) && (gBoxData[bi].data & WATERLOGGED_BIT)))



typedef struct TouchCell {
    unsigned short connections;	// bit field showing connections to edges
    unsigned char count;		// number of connections (up to 12)
    unsigned char obscurity;	// how many directions have something blocking it from visibility (up to 6). More hidden air cells get filled first
} TouchCell;

TouchCell *gTouchGrid = NULL;

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


#ifdef _DEBUG
// if we use the Reused command, make sure we're not outputting a face twice - that's an error.
static int gAssertFacesNotReusedMask = 0x0;
#endif



static void initializeWorldData( IBox *worldBox, int xmin, int ymin, int zmin, int xmax, int ymax, int zmax );
static int initializeModelData();

static int readTerrainPNG( const wchar_t *curDir, progimage_info *pII, wchar_t *terrainFileName );

static int populateBox(WorldGuide *pWorldGuide, ChangeBlockCommand *pCBC, IBox *box);
static void findChunkBounds(WorldGuide *pWorldGuide, int bx, int bz, IBox *worldBox, int mcVersion);
static void extractChunk(WorldGuide *pWorldGuide, int bx, int bz, IBox *box, int mcVersion);
static bool willChangeBlockCommandModifyAir(ChangeBlockCommand *pCBC);
static void modifySides( int editMode );
static void modifySlab(int by, int editMode);
static void editBlock( int x, int y, int z, int editMode );

static int filterBox(ChangeBlockCommand *pCBC);
static bool applyChangeBlockCommand(ChangeBlockCommand *pCBC);
static bool isWorldVolumeEmpty();
static void computeRedstoneConnectivity(int boxIndex);
static int computeFlatFlags(int boxIndex);
static int firstFaceModifier( int isFirst, int faceIndex );
static void wobbleObjectLocation(int boxIndex, float &shiftX, float &shiftZ);
static bool fenceNeighbor(int type, int boxIndex, int blockSide);
static int saveBillboardOrGeometry( int boxIndex, int type );
static int saveTriangleGeometry( int type, int dataVal, int boxIndex, int typeBelow, int dataValBelow, int boxIndexBelow, int choppedSide );
static unsigned int getStairMask(int boxIndex, int dataVal);
static void setDefaultUVs( Point2 uvs[3], int skip );
static FaceRecord * allocFaceRecordFromPool();
static unsigned short getSignificantMaterial(int type, int dataVal);
static int saveTriangleFace( int boxIndex, int swatchLoc, int type, int dataVal, int faceDirection, int startVertexIndex, int vindex[3], Point2 uvs[3] );
static void saveBlockGeometry( int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ );
static void saveBoxGeometry(int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ);
static void saveBoxTileGeometry(int boxIndex, int type, int dataVal, int swatchLoc, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ);
static void saveBoxMultitileGeometry(int boxIndex, int type, int dataVal, int topSwatchLoc, int sideSwatchLoc, int bottomSwatchLoc, int markFirstFace, int faceMask,
    int rotUVs, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ );
static void saveBoxReuseGeometryXFaces(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, float umin, float umax, float vmin, float vmax);
static void saveBoxReuseGeometryYFaces(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, float umin, float umax, float vmin, float vmax);
static void saveBoxReuseGeometry(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, int rotUVs, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ);
static int saveBoxAlltileGeometry(int boxIndex, int type, int dataVal, int swatchLocSet[6], int markFirstFace, int faceMask, int rotUVs, int reuseVerts,
    float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ );
static int findFaceDimensions(float rect[4], int faceDirection, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ);
static int lesserNeighborCoversRectangle( int faceDirection, int boxIndex, float rect[4] );
static int getFaceRect( int faceDirection, int boxIndex, int view3D, float faceRect[4] );
static int saveBoxFace( int swatchLoc, int type, int dataVal, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int reverseLoop,
    int rotUVs, float minu, float maxu, float minv, float maxv );
static int saveBoxFaceUVs( int type, int dataVal, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int uvIndices[4] );
static int saveBillboardFaces( int boxIndex, int type, int billboardType );
static int saveBillboardFacesExtraData( int boxIndex, int type, int billboardType, int dataVal, int firstFace, bool dontWobbleOverride = false);
static int checkGroupListSize();
static int checkVertexListSize();
static int checkFaceListSize();

static int findGroups();
static void addVolumeToGroup( int groupID, int minx, int miny, int minz, int maxx, int maxy, int maxz );
static void propagateSeed(IPoint point, BoxGroup *groupInfo, IPoint **seedStack, int *seedSize, int *seedCount);
static int getNeighbor( int faceDirection, IPoint newPoint );
static void getNeighborUnsafe( int faceDirection, IPoint newPoint );

static void coatSurfaces();
static void removeCoatingAndGroups();
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

static int generateBlockDataAndStatistics(IBox *tightWorldBox, IBox *worldBox);
static int tileIdCompare(void *context, const void *str1, const void *str2);
static int faceIdCompare(void *context, const void *str1, const void *str2);

static int getDimensionsAndCount( Point dimensions );
static void rotateLocation( Point pt );
static int checkAndCreateFaces( int boxIndex, IPoint loc );
static int checkMakeFace( int type, int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex, int fluidFullBlock );
static int neighborMayCoverFace( int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex );
static int lesserBlockCoversWholeFace( int faceDirection, int neighborBoxIndex, int view3D );
static int isFluidBlockFull( int type, int boxIndex );
static int cornerHeights( int type, int boxIndex, float heights[4] );
static float computeUpperCornerHeight( int type, int boxIndex, int x, int z );
static float getFluidHeightPercent( int dataVal );
static int sameFluid( int fluidType, int type );
static int saveSpecialVertices( int boxIndex, int faceDirection, IPoint loc, float heights[4], int heightIndices[4] );
static int saveVertices( int boxIndex, int faceDirection, IPoint loc );
static int saveFaceLoop( int boxIndex, int faceDirection, float heights[4], int heightIndex[4], int firstFace );
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

static int writeAsciiSTLBox(WorldGuide *pWorldGuide, IBox *box, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC);
static int writeBinarySTLBox(WorldGuide *pWorldGuide, IBox *box, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC);
static int writeOBJBox(WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC);
static int writeOBJTextureUV( float u, float v, int addComment, int swatchLoc );
static int writeOBJMtlFile();
static int writeOBJFullMtlDescription(char *mtlName, int type, char *textureRGB, char *textureRGBA, char *textureAlpha);

static int writeVRML2Box(WorldGuide *pWorldGuide, IBox *box, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC);
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

static int writeStatistics(HANDLE fh, WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBoxconst, const wchar_t *curDir, const wchar_t *terrainFileName, const wchar_t *schemeSelected, ChangeBlockCommand *pCBC);

static float computeMaterialCost( int printMaterialType, float blockEdgeSize, int numBlocks, int numMinorBlocks );
static int finalModelChecks();

static void addOutputFilenameToList(wchar_t *filename);

static void spacesToUnderlines( wchar_t *targetString );
static void spacesToUnderlinesChar( char *targetString );

static int createBaseMaterialTexture();

static void copyPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, progimage_info *src, int src_x_min, int src_y_min);
static void setColorPNGArea(progimage_info *dst, int dst_x_min, int dst_y_min, int size_x, int size_y, unsigned int value);
static void stretchSwatchToTop(progimage_info *dst, int swatchIndex, float startStretch);
static void stretchSwatchToFill(progimage_info *dst, int swatchIndex, int xlo, int ylo, int xhi, int yhi);
static void copyPNGTile(progimage_info *dst, int dst_x, int dst_y, int tileSize, progimage_info *src, int src_x, int src_y);
#ifdef _DEBUG
static void drawPNGTileLetterR( progimage_info *dst, int x, int y, int tileSize );
#endif
static void setColorPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned int value);
static void addNoisePNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a, float noise );
static void multiplyPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a);
static void multiplyClampPNGTile(progimage_info *dst, int x, int y, int tileSize, int r, int g, int b, unsigned char a);
static void bluePNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b);
static void rotatePNGTile(progimage_info *dst, int dcol, int drow, int scol, int srow, int angle, int swatchSize );
static int bleedPNGSwatch(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha);
static int bleedPNGSwatchRecursive(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha);
static void makeRemainingTileAverage(progimage_info *dst, int chosenTile, int swatchSize, int swatchesPerRow);
static void setAlphaPNGSwatch(progimage_info *dst, int dstSwatch, int swatchSize, int swatchesPerRow, unsigned char alpha );
static void compositePNGSwatches(progimage_info *dst, int dstSwatch, int overSwatch, int underSwatch, int swatchSize, int swatchesPerRow, int forceSolid );
static void compositePNGSwatchOverColor(progimage_info *dst, int dstSwatch, int overSwatch, int underColor, int swatchSize, int swatchesPerRow );
static int convertRGBAtoRGBandWrite(progimage_info *src, wchar_t *filename);
static void convertAlphaToGrayscale( progimage_info *dst );
static bool writeTileFromMasterOutput(wchar_t *filename, progimage_info *src, int swatchLoc, int swatchSize, int swatchesPerRow);
static bool doesTileHaveAlpha(progimage_info *src, int swatchLoc, int swatchSize, int swatchesPerRow);

static void ensureSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix );
static void removeSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix );
//static const wchar_t *removePath( const wchar_t *src );
static const char *removePathChar( const char *src );

static void charToWchar( char *inString, wchar_t *outWString );
static void getPathAndRoot( const wchar_t *src, int fileType, wchar_t *path, wchar_t *root );
static void concatFileName2(wchar_t *dst, const wchar_t *src1, const wchar_t *src2);
static void concatFileName3(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3);
static void concatFileName4(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3, const wchar_t *src4);
static void wcharCleanse( wchar_t *wstring );

static void myseedrand( long seed );
static double myrand();

static int analyzeChunk(WorldGuide *pWorldGuide, Options *pOptions, int bx, int bz, int minx, int miny, int minz, int maxx, int maxy, int maxz, bool ignoreTransparent, int mcVersion);

static void seedWithXYZ(int boxIndex);
static void seedWithXZ(int boxIndex);

static wchar_t gSeparator[3];

void SetSeparatorObj(const wchar_t *separator)
{
    wcscpy_s(gSeparator, 3, separator);
}

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
int SaveVolume(wchar_t *saveFileName, int fileType, Options *options, WorldGuide *pWorldGuide, const wchar_t *curDir, int xmin, int ymin, int zmin, int xmax, int ymax, int zmax,
    ProgressCallback callback, wchar_t *terrainFileName, wchar_t *schemeSelected, FileList *outputFileList, int majorVersion, int minorVersion, int worldVersion, ChangeBlockCommand *pCBC)
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
    IBox tightenedWorldBox;
    int retCode = MW_NO_ERROR;
    int needDifferentTextures = 0;

    // set up a bunch of globals
    gpCallback = &callback;
    // initial "quick" progress just so progress bar moves a bit.
    UPDATE_PROGRESS(0.20f*PG_MAKE_FACES);

    gMajorVersion = majorVersion;
    gMinorVersion = minorVersion;
    gMinecraftWorldVersion = worldVersion;
	gMcVersion = DATA_VERSION_TO_RELEASE_NUMBER(worldVersion);

    memset(&gStats,0,sizeof(ExportStatistics));
    // clear all of gModel to zeroes
    memset(&gModel,0,sizeof(Model));
    gOptions = options;
    gOptions->totalBlocks = 0;
    gOptions->cost = 0.0f;

    gExportTexture = (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE) ? 1 : 0;
	gExportTiles = (gOptions->exportFlags & EXPT_OUTPUT_SEPARATE_TEXTURE_TILES) ? 1 : 0;
	g3d = (gOptions->exportFlags & EXPT_OUTPUT_OBJ_FULL_MATERIAL) ? 1 : 0;

    gPrint3D = (gOptions->exportFlags & EXPT_3DPRINT) ? 1 : 0;
    gModel.pInputTerrainImage = NULL;


    // Billboards and true geometry to be output?
    // True only if we're exporting all geometry.
    // Must be set now, as this influences whether we stretch textures.
    gExportBillboards =
        //(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) &&
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
    wcscpy_s(gOutputFileRootClean,MAX_PATH_AND_FILE,gOutputFileRoot);
    wcharCleanse(gOutputFileRootClean);
    spacesToUnderlines(gOutputFileRootClean);
    WcharToChar(gOutputFileRootClean, gOutputFileRootCleanChar, MAX_PATH_AND_FILE);

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

    // first things very first: if full texturing is wanted, check if the TerrainExt.png input texture is readable
    if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES )
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
    tightenedWorldBox = worldBox;

    // Note that tightenedWorldBox will come back with the "solid" bounds, of where data was actually found.
    // Mostly "of interest", not particularly useful - we used to output it, but that's a bit confusing when importing.
    retCode |= populateBox(pWorldGuide, pCBC, &tightenedWorldBox);
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // nothing in box, so end.
        goto Exit;
    }

    // prepare to write texture, if needed
    if (gExportTexture)
    {
        // Make it twice as large if we're outputting image textures, too- we need the space.
		// We're just setting up here, giving something to write UVs against; even per-tile texture
		// output uses this. We export the texture at the end.
        if (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES)
        {
            // use true textures
            gModel.textureResolution = 2*gModel.pInputTerrainImage->width;
            gModel.terrainWidth = gModel.pInputTerrainImage->width;
		}
        else
        {
            // Use "noisy" colors, fixed 512 x 512 - we could actually make this texture quite small
            // Note this used to be 256 x 256, but that's only 14*14 = 196 materials, and we're now
            // at 198 or so...
            gModel.textureResolution = 512;
            // This number determines number of swatches per row. Make it 256, even though there's
            // no incoming image. This then ensures there's room for enough solid color images.
            gModel.terrainWidth = 256;    // really, no image, but act like there is
        }
        // there are always 16 tiles wide in terrainExt.png, so we divide by this.
        gModel.tileSize = gModel.terrainWidth/16;
        gModel.swatchSize = 2 + gModel.tileSize;
        gModel.invTextureResolution = 1.0f / (float)gModel.textureResolution;
        gModel.swatchesPerRow = (int)(gModel.textureResolution / gModel.swatchSize);
        gModel.textureUVPerSwatch = (float)gModel.swatchSize / (float)gModel.textureResolution; // e.g. 18 / 256
        gModel.textureUVPerTile = (float)gModel.tileSize / (float)gModel.textureResolution; // e.g. 16 / 256
        gModel.swatchListSize = gModel.swatchesPerRow*gModel.swatchesPerRow;

        retCode |= createBaseMaterialTexture();

		if (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES)
		{
			// all done with base input texture, free up its memory.
			readpng_cleanup(1, gModel.pInputTerrainImage);
			delete gModel.pInputTerrainImage;
			gModel.pInputTerrainImage = NULL;
		}
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
    retCode |= filterBox(pCBC);
    // always return the worst error
    if ( retCode >= MW_BEGIN_ERRORS )
    {
        // problem found
        goto Exit;
    }
    UPDATE_PROGRESS(0.90f*PG_MAKE_FACES);


    // At this point all data is read in and filtered. Check if we're outputting a
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

    // create database and compute statistics for output
    retCode |= generateBlockDataAndStatistics(&tightenedWorldBox, &worldBox);
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    UPDATE_PROGRESS(PG_OUTPUT);

    switch ( fileType )
    {
    case FILE_TYPE_WAVEFRONT_REL_OBJ:
    case FILE_TYPE_WAVEFRONT_ABS_OBJ:
        // for OBJ, we may use more than one texture
        needDifferentTextures = 1;
        retCode |= writeOBJBox(pWorldGuide, &worldBox, &tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
        break;
    case FILE_TYPE_BINARY_MAGICS_STL:
    case FILE_TYPE_BINARY_VISCAM_STL:
        retCode |= writeBinarySTLBox(pWorldGuide, &worldBox, &tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
        break;
    case FILE_TYPE_ASCII_STL:
        retCode |= writeAsciiSTLBox(pWorldGuide, &worldBox, &tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
        break;
    case FILE_TYPE_VRML2:
        retCode |= writeVRML2Box(pWorldGuide, &worldBox, &tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
        break;
    //case FILE_TYPE_SETTINGS:
        //retCode |= writeSettings( pWorldGuide, &worldBox, &tightenedWorldBox );
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
            int col, row;
            // if we're rendering all blocks, don't fill in cauldrons, beds, etc. as we want these cutouts for rendering; else use offset:
#define FA_TABLE__RENDER_BLOCK_START 7
#define FA_TABLE__VIEW_SIZE (1+FA_TABLE__RENDER_BLOCK_START)
#define FA_TABLE_SIZE 59
            static FillAlpha faTable[FA_TABLE_SIZE] =
            {
                // Stuff filled only if lesser (i.e. all blocks) is off for rendering, so that the cauldron is rendered as a solid block.
                // Negative values for the underlay means "use this block's solid color", whatever it is.
                // Incredible laziness: we use TRIPWIRE to mean black, since that's its "color". We can't use AIR because AIR is the same
                // value as SWATCH_INDEX(0,0), which is grass.
                { SWATCH_INDEX( 10, 9 ), -BLOCK_TRIPWIRE }, // cauldron side
                { SWATCH_INDEX( 11, 9 ), -BLOCK_TRIPWIRE }, // cauldron bottom

                { SWATCH_INDEX( 5, 9 ), -BLOCK_TRIPWIRE }, // bed
                { SWATCH_INDEX( 6, 9 ), -BLOCK_TRIPWIRE }, // bed
                { SWATCH_INDEX( 7, 9 ), -BLOCK_TRIPWIRE }, // bed
                { SWATCH_INDEX( 8, 9 ), -BLOCK_TRIPWIRE }, // bed

                { SWATCH_INDEX( 6, 4 ), -BLOCK_CACTUS }, // cactus

                // count is FA_TABLE__RENDER_BLOCK_START up to this point

                /////////////////////////////////// always do:
                // stuff that is put in always, fringes that need to be filled
                { SWATCH_INDEX( 10, 11 ), -BLOCK_FLOWER_POT }, // flower pot

                // count is FA_TABLE__VIEW_SIZE up to this point

                // stuff filled in for 3D printing only
                { SWATCH_INDEX( 11, 0 ), SWATCH_INDEX( 6, 3 ) }, // spiderweb over stone block
                { SWATCH_INDEX( 12, 0 ), SWATCH_INDEX( 0, 0 ) }, // red flower over grass
                { SWATCH_INDEX( 13, 0 ), SWATCH_INDEX( 0, 0 ) }, // yellow flower over grass
                { SWATCH_INDEX( 15, 0 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX( 12, 1 ), SWATCH_INDEX( 0, 0 ) }, // red mushroom over grass
                { SWATCH_INDEX( 13, 1 ), SWATCH_INDEX( 0, 0 ) }, // brown mushroom over grass
                { SWATCH_INDEX(  1, 3 ), -BLOCK_GLASS }, // glass over glass color
                { SWATCH_INDEX(  4, 3 ), -BLOCK_TRIPWIRE }, // transparent leaves over air (black) - doesn't really matter, not used when printing anyway
                { SWATCH_INDEX(  7, 3 ), SWATCH_INDEX( 2, 1 ) }, // dead bush over grass
                { SWATCH_INDEX(  8, 3 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX(  1, 4 ), SWATCH_INDEX( 1, 0 ) }, // spawner over stone
                { SWATCH_INDEX(  9, 4 ), SWATCH_INDEX( 0, 0 ) }, // reeds over grass
                { SWATCH_INDEX( 15, 4 ), SWATCH_INDEX( 0, 0 ) }, // sapling over grass
                { SWATCH_INDEX( 14, 1 ), SWATCH_INDEX( 0, 0 ) }, // jungle sapling over grass
                { SWATCH_INDEX(  1, 5 ), SWATCH_INDEX(10, 23 ) }, // wooden door top over slab top - was over 6, 0
                { SWATCH_INDEX(  2, 5 ), SWATCH_INDEX(10, 23 ) }, // door top over slab top - was over 6, 0
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
                { SWATCH_INDEX(  4, 8 ), -BLOCK_TRIPWIRE }, // leaves over air (black) - doesn't really matter, not used
                { SWATCH_INDEX( 10, 8 ), -BLOCK_TRIPWIRE }, // cauldron over air (black)
                { SWATCH_INDEX(  4, 9 ), -BLOCK_GLASS }, // glass pane over glass color (more interesting than stone, and lets you choose)
                { SWATCH_INDEX( 13, 9 ), SWATCH_INDEX( 1, 0 ) }, // brewing stand over stone
                { SWATCH_INDEX( 14,11 ), SWATCH_INDEX( 6, 5 ) }, // pumpkin stem over farmland
                { SWATCH_INDEX( 15,11 ), SWATCH_INDEX( 6, 5 ) }, // mature stem over farmland
                { SWATCH_INDEX(  4,12 ), -BLOCK_TRIPWIRE }, // jungle leaves over air (black) - doesn't really matter, not used
                { SWATCH_INDEX(  2,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX(  3,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX(  4,14 ), SWATCH_INDEX( 8, 6 ) }, // nether wart over soul sand
                { SWATCH_INDEX( 11,14 ), -BLOCK_WOOL }, // beacon over white color - TODO!!! change to 9,2 once we're using terrain properly
                { SWATCH_INDEX(  8,10 ), -BLOCK_COCOA_PLANT }, // cocoa, so preview looks semi-OK
                { SWATCH_INDEX(  9,10 ), -BLOCK_COCOA_PLANT }, // cocoa, so preview looks OK
                { SWATCH_INDEX( 10,10 ), -BLOCK_COCOA_PLANT }, // cocoa, so preview looks OK
                { SWATCH_INDEX(  8,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX(  9,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 10,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 11,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 12,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 13,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 14,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 15,12 ), SWATCH_INDEX( 6, 5 ) }, // potato/carrot crops over farmland
                { SWATCH_INDEX( 15, 1 ), -BLOCK_TRIPWIRE }, // fire over air (black)
            };

            int rc;

            // do only if true textures used
            if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES )
            {
                int i;
                int faTableCount;

                // fill in all alphas that 3D export wants filled
                // For printing we also then composite over other backgrounds as the defaults.
                faTableCount = gPrint3D ? FA_TABLE_SIZE : FA_TABLE__VIEW_SIZE;
                // start at solid rendering vs. leave it transparent for true cutaway rendering;
                // that is, go from 0 if printing or if we're rendering & not exporting true geometry (lesser)
                for ( i = ( gPrint3D || !gOptions->pEFD->chkExportAll ) ? 0 : FA_TABLE__RENDER_BLOCK_START; i < faTableCount; i++ )
                {
                    // passing in a negative swatch location means "use the absolute value as the index into the color of the block".
                    // See faTable explanation above.
                     if ( faTable[i].underlay >= 0 )
                    {
                        compositePNGSwatches( gModel.pPNGtexture,
                            faTable[i].cutout, faTable[i].cutout, faTable[i].underlay,
                            gModel.swatchSize, gModel.swatchesPerRow, 0 );
                    } else {
                        compositePNGSwatchOverColor( gModel.pPNGtexture,
                            faTable[i].cutout, faTable[i].cutout, gBlockDefinitions[-faTable[i].underlay].color,
                            gModel.swatchSize, gModel.swatchesPerRow );
                    }
                }

                // final swatch cleanup if textures are used and we're doing 3D printing
                if ( gPrint3D )
                {


#define FA_FINAL_TABLE_SIZE 19
                    static FillAlpha faFinalTable[] =
                    {
                        { SWATCH_INDEX( 0, 8 ), SWATCH_INDEX( 1, 0 ) }, // rail over stone
                        { SWATCH_INDEX( 0, 7 ), SWATCH_INDEX( 1, 0 ) }, // curved rail over stone
                        { SWATCH_INDEX( 0, 5 ), SWATCH_INDEX( 1, 0 ) }, // torch over stone
                        { REDSTONE_WIRE_VERT,   SWATCH_INDEX( 1, 0 ) }, // wire over stone
                        { SWATCH_INDEX( 3, 5 ), SWATCH_INDEX( 1, 0 ) }, // ladder over stone
                        { SWATCH_INDEX( 3, 11 ), SWATCH_INDEX( 1, 0 ) }, // powered rail over stone
                        { SWATCH_INDEX( 3, 10 ), SWATCH_INDEX( 1, 0 ) }, // unpowered rail over stone
                        { SWATCH_INDEX( 3, 12 ), SWATCH_INDEX( 1, 0 ) }, // detector rail over stone
                        { SWATCH_INDEX( 3, 6 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch on over stone
                        { SWATCH_INDEX( 3, 7 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch off over stone
                        { SWATCH_INDEX( 12, 4 ), SWATCH_INDEX( 15, 13 ) }, // lily pad over stationary water
                        { SWATCH_INDEX( 4, 5 ), SWATCH_INDEX( 1, 0 ) }, // trapdoor over stone
                        { SWATCH_INDEX( 15, 8 ), SWATCH_INDEX( 0, 0 ) }, // vines over grass

                        // stuff we don't use, so don't need defaults for
                        { REDSTONE_WIRE_HORIZ, SWATCH_INDEX( 1, 0 ) }, // wire over stone
                        { REDSTONE_WIRE_DOT, SWATCH_INDEX( 1, 0 ) }, // wire over stone

                        { SWATCH_INDEX( 0, 15 ), SWATCH_INDEX( 1, 0 ) }, // torch top
                        { SWATCH_INDEX( 1, 15 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch on top
                        { SWATCH_INDEX( 2, 15 ), SWATCH_INDEX( 1, 0 ) }, // redstone torch off top
                        { SWATCH_INDEX( 2, 22 ), SWATCH_INDEX( 1, 0 ) }, // iron trapdoor over stone
                    };

                    // For 3D printing, now that we're totally done, fill in the main pieces which were left empty as templates;
                    // these won't actually get used, as far as I recall. I clean them up purely for tidiness, and just in case.
                    // We could register these as previously-existing swatches, knowing that here is where we'll actually fill them in.
                    // To do so, use createCompositeSwatch - call with specific swatch location, with negative meaning "add to end".
                    // But, not to bother, for now, until the day we're really desperate for the room. TODO.
                    for (i = 0; i < FA_FINAL_TABLE_SIZE; i++)
                    {
                        compositePNGSwatches(gModel.pPNGtexture,
                            faFinalTable[i].cutout, faFinalTable[i].cutout, faFinalTable[i].underlay,
                            gModel.swatchSize, gModel.swatchesPerRow, 0);
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

                SWATCH_TO_COL_ROW( gDebugTransparentType, col, row );
                setColorPNGTile( gModel.pPNGtexture, col, row, gModel.swatchSize, color );
            }

            // just to avoid confusion as to what gets used for what, clear out the workspace texture location with white
            SWATCH_TO_COL_ROW(SWATCH_WORKSPACE, col, row);
            setColorPNGTile(gModel.pPNGtexture, col, row, gModel.swatchSize, 0xffffffff);

            UPDATE_PROGRESS(PG_TEXTURE+0.04f);

			// Exporting individual tiles?
			if (gExportTiles)
			{
				// if any checkbox for texture output is on, then all textures are output - let's not get too clever here.
				if (gOptions->pEFD->chkTextureRGBA || gOptions->pEFD->chkTextureRGB || gOptions->pEFD->chkTextureA) {
					wchar_t subpath[MAX_PATH_AND_FILE] = L"";
					if (strlen(gOptions->pEFD->tileDirString) > 0) {
						wchar_t directoryPath[MAX_PATH_AND_FILE];
						charToWchar(gOptions->pEFD->tileDirString, subpath);
						wcscat_s(subpath, MAX_PATH_AND_FILE, L"\\");
						// create subdirectory if it doesn't exist
						if (wcschr(subpath, (wchar_t)':') ) {
							// looks like an absolute path is being specified
							wcscpy_s(directoryPath, MAX_PATH_AND_FILE, subpath);
						}
						else {
							// relative path
							concatFileName4(directoryPath, gOutputFilePath, subpath, L"", L"");
						}
						if (!(CreateDirectoryW(directoryPath, NULL) ||
							ERROR_ALREADY_EXISTS == GetLastError()))
						{
							// Failed to create directory.
							retCode |= MW_CANNOT_CREATE_DIRECTORY;
							return retCode;
						}
					}
					for (int i = 0; i < TOTAL_TILES; i++) {
						// tile name is material name, period
						if (gModel.tileList[i]) {
							// tile found that should be output
							wchar_t materialTile[MAX_PATH_AND_FILE];
							concatFileName4(materialTile, gOutputFilePath, subpath, gTilesTable[i].filename, L".png");
							rc = writeTileFromMasterOutput(materialTile, gModel.pPNGtexture, i, gModel.swatchSize, gModel.swatchesPerRow);
							assert(rc == 0);
							retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc << MW_NUM_CODES)) : MW_NO_ERROR;
							// if we can't write one file, we can't write any, so break out
							if (rc)
								break;
							// maybe too frequent TODO
							UPDATE_PROGRESS(PG_TEXTURE + 0.16f*(float)i / (float)gModel.tileListCount);
						}
					}
				}
			}

			// do we need three textures, or just the one RGBA texture?
			else if (needDifferentTextures)
            {
                // need all three
                wchar_t textureRGB[MAX_PATH_AND_FILE];
                wchar_t textureRGBA[MAX_PATH_AND_FILE];
                wchar_t textureAlpha[MAX_PATH_AND_FILE];

                // Write them out! We need three texture file names: -RGB, -RGBA, -Alpha.
                // The RGB/RGBA split is needed for fast previewers like G3D to gain additional speed
                // The all-alpha image is needed for various renderers to properly read cutouts
                concatFileName4(textureRGB, gOutputFilePath, gOutputFileRootClean, PNG_RGB_SUFFIX, L".png");
                concatFileName4(textureRGBA, gOutputFilePath, gOutputFileRootClean, PNG_RGBA_SUFFIX, L".png");
                concatFileName4(textureAlpha, gOutputFilePath, gOutputFileRootClean, PNG_ALPHA_SUFFIX, L".png");

                if ( gModel.usesRGBA && gOptions->pEFD->chkTextureRGBA )
                {
                    // output RGBA version
                    rc = writepng(gModel.pPNGtexture,4,textureRGBA);
					assert(rc == 0);
					addOutputFilenameToList(textureRGBA);
					UPDATE_PROGRESS(PG_TEXTURE + 0.08f);
                    retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }

                if ( gModel.usesRGB && gOptions->pEFD->chkTextureRGB )
                {
                    // output RGB version
                    rc = convertRGBAtoRGBandWrite(gModel.pPNGtexture,textureRGB);
                    assert(rc == 0);
					// not needed, as convertRGBAtoRGBandWrite does this: addOutputFilenameToList(textureRGB);
					UPDATE_PROGRESS(PG_TEXTURE + 0.12f);
					retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }

                if ( gModel.usesAlpha && gOptions->pEFD->chkTextureA )
                {
                    // output Alpha version, which is actually RGBA, to make 3DS MAX happy
                    convertAlphaToGrayscale( gModel.pPNGtexture );
                    rc = writepng(gModel.pPNGtexture,4,textureAlpha);
					assert(rc == 0);
					addOutputFilenameToList(textureAlpha);
					UPDATE_PROGRESS(PG_TEXTURE + 0.12f);
					retCode |= rc ? (MW_CANNOT_CREATE_PNG_FILE | (rc<<MW_NUM_CODES)) : MW_NO_ERROR;
                }
            }
            else
            {
                // just the one (for VRML). If we're printing, and not debugging (debugging needs transparency), we can convert this one down to RGB
                wchar_t textureFileName[MAX_PATH_AND_FILE];
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

	// 91%
    UPDATE_PROGRESS(PG_CLEANUP);

    freeModel( &gModel );

    if ( gBoxData )
        free(gBoxData);
    gBoxData = NULL;

    if ( gBiome )
        free(gBiome);
    gBiome = NULL;


    // 95%
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

// assumes same maximum length (or longer) for both strings
void WcharToChar(const wchar_t *inWString, char *outString, int maxlength)
{
    //WideCharToMultiByte(CP_UTF8,0,inWString,-1,outString,MAX_PATH_AND_FILE,NULL,NULL);
    int i;
    int oct = 0;

    for (i = 0; i < maxlength; i++)
    {
        int val = inWString[i];
        if (val >= 0 && val < 128)
        {
            outString[oct++] = (char)val;
        }
        if (val == 0)
        {
            // done: did anything get copied? (other than 0 at end)
            //if ( oct <= 1 )
            return;
        }
    }
    // it is unlikely that the loop above didn't terminate and return
    assert(0);
    return;
}

// Strip off last thing in string, e.g. file name, directory name.
// You can have path == src, just pass it in twice
void StripLastString(const wchar_t *src, wchar_t *path, wchar_t *piece)
{
	wchar_t *piecePtr;
	wchar_t *endPathPtr;

	if (src != path) {
		wcscpy_s(path, MAX_PATH_AND_FILE, src);
	}
	// find last \ in string
	endPathPtr = piecePtr = wcsrchr(path, (wchar_t)'\\');
	if (piecePtr) {
		// found a \, so move up past it
		piecePtr++;
	}
	else {
		// look for /
		endPathPtr = piecePtr = wcsrchr(path, (wchar_t)'/');
		if (piecePtr) {
			// found a /, so move up past it 
			piecePtr++;
		}
		else {
			// no \ or / found, just return string itself, and there is no path left
			piecePtr = path;
			endPathPtr = path;
		}
	}
	// split at piecePtr - copy last part to "piece"
	wcscpy_s(piece, MAX_PATH_AND_FILE, piecePtr);
	// this sets last character of remaining path to null (end it)
	*endPathPtr = (wchar_t)0;
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

    gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]	= -gBoxSizeYZ;	// -X
    gFaceOffset[DIRECTION_BLOCK_BOTTOM]		= -1;			// -Y
    gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]	= -gBoxSize[Y];	// -Z
    gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]	= gBoxSizeYZ;	// +X
    gFaceOffset[DIRECTION_BLOCK_TOP]		= 1;			// +Y
    gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]	= gBoxSize[Y];	// +Z

    // given a face direction, note the flat flag that corresponds to that neighbor.
    gFlagPointsTo[DIRECTION_BLOCK_SIDE_LO_X] = FLAT_FACE_HI_X;	// -X
    gFlagPointsTo[DIRECTION_BLOCK_BOTTOM]	 = FLAT_FACE_ABOVE;	// -Y
    gFlagPointsTo[DIRECTION_BLOCK_SIDE_LO_Z] = FLAT_FACE_HI_Z;	// -Z
    gFlagPointsTo[DIRECTION_BLOCK_SIDE_HI_X] = FLAT_FACE_LO_X;	// +X
    gFlagPointsTo[DIRECTION_BLOCK_TOP]		 = FLAT_FACE_BELOW;	// +Y
    gFlagPointsTo[DIRECTION_BLOCK_SIDE_HI_Z] = FLAT_FACE_LO_Z;	// +Z


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

    VecScalar( gModel.billboardBounds.min, =,  INT_MAX);
    VecScalar( gModel.billboardBounds.max, =, INT_MIN);

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
                        if ( gBoxData[boxIndex + gFaceOffset[faceDirection]].type == BLOCK_AIR )
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
    int rc=0;

    if ( wcslen(selectedTerrainFileName) > 0 )
    {
        rc = readpng(pITI,selectedTerrainFileName);
    }
    else
    {
        // Really, we shouldn't ever hit this branch, as the terrain file name is now always set
        // at the start. Left just in case...
		wchar_t defaultTerrainFileName[MAX_PATH_AND_FILE];
		concatFileName2(defaultTerrainFileName,curDir,L"\\terrainExt.png");
        rc = readpng(pITI,defaultTerrainFileName);
    }

    // test if terrainExt.png file does not exist
    if ( rc == PNG_FILE_DOES_NOT_EXIST)
    {
        //FILE DOESN'T EXIST - read memory file, setting all fields
        pITI->width = gTerrainExtWidth;
        pITI->height = gTerrainExtHeight;
        pITI->image_data.insert(pITI->image_data.end(), &gTerrainExt[0], &gTerrainExt[gTerrainExtWidth*gTerrainExtHeight*4]);
		gModel.terrainImageNotFound = true;
    }
	else {
		gModel.terrainImageNotFound = false;	// i.e., we found the terrain image this time around
		if (rc)
		{
			// Some other error, so we need to quit. We *could* read the internal memory terrainExt.png instead,
			// but that would hide any problems encountered with the terrainExt.png that does exist but is invalid. 
			return (MW_CANNOT_READ_SELECTED_TERRAIN_FILE | (rc << MW_NUM_CODES));
		}
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
    // When you do this, save to file "terrainExtData" and the .cpp and .h file will appear in the export directory.
    static bool outputPNG = false;
    if (outputPNG)
    {
#ifdef WIN32
        DWORD br;
#endif
        char outputString[MAX_PATH_AND_FILE];
        int size = 4 * pITI->width * pITI->height;

        wchar_t codeFileNameWithSuffix[MAX_PATH_AND_FILE];
        concatFileName3(codeFileNameWithSuffix,gOutputFilePath,gOutputFileRoot,L".h");

        // create the Wavefront OBJ file
        //DeleteFile(codeFileNameWithSuffix);
        static PORTAFILE outFile=PortaCreate(codeFileNameWithSuffix);
        if (outFile != INVALID_HANDLE_VALUE)
        {
			// write .h file
            sprintf_s(outputString,256,"#ifndef __TERRAINEXTDATA_H__\n#define __TERRAINEXTDATA_H__\n\n" );
            WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
            sprintf_s(outputString,256,"extern int gTerrainExtWidth;\nextern int gTerrainExtHeight;\nextern unsigned char gTerrainExt[%d];\n\n#endif\n", size );
            WERROR(PortaWrite(outFile, outputString, strlen(outputString) ));
            PortaClose(outFile);

			// now write .cpp file
            concatFileName3(codeFileNameWithSuffix,gOutputFilePath,gOutputFileRoot,L".cpp");
            outFile=PortaCreate(codeFileNameWithSuffix);
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

	// this helps to fill in typeForMtl in tiles.h
	static bool crossCorrelate = false;
	if (crossCorrelate) {
		int index = 0;
		for (int row = 0; row < VERTICAL_TILES; row++) {
			for (int col = 0; col < 16; col++) {
				// find row and col in blockInfo, if possible
				bool foundIt = false;
				int i;
				for (i = 0; i < NUM_BLOCKS_DEFINED; i++) {
					if ((gBlockDefinitions[i].txrX == col) && (gBlockDefinitions[i].txrY == row)) {
						foundIt = true;
						break;
					}
				}
				TCHAR wcString[1024];
				if (foundIt) {
					wsprintf(wcString, L"Tile %2d, %2d,  %3d\n", col, row, i);
				}
				else {
					wsprintf(wcString, L"Tile %2d, %2d,  %3d\n", col, row, 6);	// put a sapling - should at least have cutout property
				}
				OutputDebugString(wcString);
				index++;
			}
		}
	}
#endif

    return MW_NO_ERROR;
}

static int populateBox(WorldGuide *pWorldGuide, ChangeBlockCommand *pCBC, IBox *worldBox)
{
    int startxblock, startzblock;
    int endxblock, endzblock;
    int blockX, blockZ;
    IBox originalWorldBox = *worldBox;

    // grab the data block needed, with a border of "air", 0, around the set
    startxblock = (int)floor((float)worldBox->min[X] / 16.0f);
    startzblock = (int)floor((float)worldBox->min[Z] / 16.0f);
    endxblock = (int)floor((float)worldBox->max[X] / 16.0f);
    endzblock = (int)floor((float)worldBox->max[Z] / 16.0f);

    // get bounds on Y coordinates, since top part of box is usually air
    VecScalar(gSolidWorldBox.min, = , INT_MAX);
    VecScalar(gSolidWorldBox.max, = , INT_MIN);

    // We now extract twice: first time is just to get bounds of solid stuff we'll actually output.
    // Results of this first pass are put in gSolidWorldBox.
    for (blockX = startxblock; blockX <= endxblock; blockX++)
    {
        //UPDATE_PROGRESS( 0.1f*(blockX-startxblock+1)/(endxblock-startxblock+1) );
        // z increases west, decreases east
        for (blockZ = startzblock; blockZ <= endzblock; blockZ++)
        {
            // this method sets gSolidWorldBox
            findChunkBounds(pWorldGuide, blockX, blockZ, worldBox, gMcVersion);
        }
    }

    // done with reading chunk for export, so free memory
    if (gOptions->moreExportMemory)
    {
        ClearCache();
    }

    if (willChangeBlockCommandModifyAir(pCBC)) {
        // have a command list - have to reset solid world bounds if we find the person is actually
        // building in the air space, turning air into something solid.
        // TODO: someday we might want to make the solid world bound limited.
        gSolidWorldBox = *worldBox;
    } else if (gSolidWorldBox.min[Y] > gSolidWorldBox.max[Y]) {
        // quick out test, nothing to do: there is nothing in the box
        return MW_NO_BLOCKS_FOUND;
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

    int edgestartxblock = (int)floor((float)edgeWorldBox.min[X] / 16.0f);
    int edgestartzblock = (int)floor((float)edgeWorldBox.min[Z] / 16.0f);
    int edgeendxblock = (int)floor((float)edgeWorldBox.max[X] / 16.0f);
    int edgeendzblock = (int)floor((float)edgeWorldBox.max[Z] / 16.0f);

    for (blockX = edgestartxblock; blockX <= edgeendxblock; blockX++)
    {
        //UPDATE_PROGRESS( 0.1f*(blockX-edgestartxblock+1)/(edgeendxblock-edgestartxblock+1) );
        // z increases south, decreases north
        for ( blockZ=edgestartzblock; blockZ<=edgeendzblock; blockZ++ )
        {
            extractChunk(pWorldGuide, blockX, blockZ, &edgeWorldBox, gMcVersion);

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

    if (gPrint3D)
    {
        // Clear slabs' type data, so that snow covering will not export if there is air below it.
        // Original types are left set so that minor objects can export properly.
        modifySlab(gAirBox.min[Y],EDIT_MODE_CLEAR_TYPE);
        modifySlab(gAirBox.max[Y],EDIT_MODE_CLEAR_TYPE);
    }

    return MW_NO_ERROR;
}

// test relevant part of a given chunk to find its size
static void findChunkBounds(WorldGuide *pWorldGuide, int bx, int bz, IBox *worldBox, int mcVersion)
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
        wcsncpy_s(pWorldGuide->directory, MAX_PATH_AND_FILE, pWorldGuide->world, MAX_PATH_AND_FILE-1);
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, gSeparator);
        if (gOptions->worldType&HELL)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM-1");
        }
        if (gOptions->worldType&ENDER)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM1");
        }
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, gSeparator);

        block = LoadBlock(pWorldGuide, bx, bz, mcVersion);
		if ((block == NULL) || (block->blockType == 2)) //blank tile, nothing to do
            return;

        Cache_Add(bx,bz,block);
    }

	// set version for later use by textures, etc.
	// yes, this gets set multiple times - so be it.
	gModel.mcVersion = block->mcVersion;

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
				// fold in the high bit to get the type
				bool isVersion13orNewer = (gModel.mcVersion >= 13);

				// 1.13 fun: if the highest bit of the data value is 1, this is a 1.13+ block of some sort,
				// so "move" that bit from data to the type. Ignore head data, which comes in with the high bit set.
				if (isVersion13orNewer && (block->data[chunkIndex] & 0x80) && (block->grid[chunkIndex] != BLOCK_HEAD) && (block->grid[chunkIndex] != BLOCK_FLOWER_POT)) {
					// high bit set, so blockID >= 256
					blockID = block->grid[chunkIndex] | 0x100;
				}
				else {
					// normal case - just transfer the data
					blockID = block->grid[chunkIndex];
				}

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
static void extractChunk(WorldGuide *pWorldGuide, int bx, int bz, IBox *edgeWorldBox, int mcVersion)
{
    int chunkX, chunkZ;

    int loopXmin, loopZmin;
    int loopXmax, loopZmax;
    int x,y,z,i;

    int chunkIndex, boxIndex;
    int blockID;

    //IPoint loc;
    //unsigned char dataVal;

    WorldBlock *block;
    block=(WorldBlock *)Cache_Find(bx,bz);

    if (block==NULL)
    {
        wcsncpy_s(pWorldGuide->directory, MAX_PATH_AND_FILE, pWorldGuide->world, MAX_PATH_AND_FILE - 1);
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"/");
        if (gOptions->worldType&HELL)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM-1");
        }
        if (gOptions->worldType&ENDER)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM1");
        }
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, gSeparator);

        block = LoadBlock(pWorldGuide, bx, bz, mcVersion);
		if ((block == NULL) || (block->blockType == 2)) //blank tile, nothing to do
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

	bool isVersion13orNewer = (gModel.mcVersion >= 13);

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
                unsigned char dataVal = block->data[chunkIndex];
				// 1.13 fun: if the highest bit of the data value is 1, this is a 1.13+ block of some sort,
				// so "move" that bit from data to the type. Ignore head data, which comes in with the high bit set.
				if (isVersion13orNewer && (dataVal & HIGH_BIT) && (block->grid[chunkIndex] != BLOCK_HEAD) && (block->grid[chunkIndex] != BLOCK_FLOWER_POT)) {
					// if you hit this, something has gone odd with the dataVal, which shouldn't happen. See nbt.cpp where it says "make sure upper bits are not set - they should not be!"
					assert(block->grid[chunkIndex] < NUM_BLOCKS_DEFINED - 256);
					gBoxData[boxIndex].data = dataVal & 0x7F;
					// high bit turns into +256
					blockID = gBoxData[boxIndex].origType =
						gBoxData[boxIndex].type = block->grid[chunkIndex] | 0x100;
				}
				else {
					// normal case - just transfer the data
					gBoxData[boxIndex].data = dataVal;
					blockID = gBoxData[boxIndex].origType =
						gBoxData[boxIndex].type = block->grid[chunkIndex];
				}

				// tile entities needed if using old data format
				if (!isVersion13orNewer) {
					// if the block is a flower pot or head, we need to extract the extra data from the block
					if ((blockID == BLOCK_FLOWER_POT) || (blockID == BLOCK_HEAD))
					{
						// don't do extraction if 1.7 or earlier data for the flower pot, i.e. the data shows there's something in the pot right now
						if (!((blockID == BLOCK_FLOWER_POT) && (dataVal > 0)))
						{
							// find the entity in the list, as possible
							BlockEntity *pBE = block->entities;
							for (i = 0; i < block->numEntities; i++, pBE++) {
								int listChunkIndex = pBE->y << 8 | pBE->zx;
								if (chunkIndex == listChunkIndex) {
									if (pBE->type == blockID) {
										// found it, data gets stored differently for heads and flowers
										if (blockID == BLOCK_FLOWER_POT) {
											gBoxData[boxIndex].data = pBE->data;
										}
										else {
											// BLOCK_HEAD

											// most arcane storage ever, as I need to fit everything in 8 bits in my extended data value field.
											// If dataVal is 2-5, rotation is not used (head is on a wall) and high bit of head type is off, else it is put on.
											// 7 | 654 | 3210
											// bit 7 - is bottom four bits 3210 the rotation on floor? If off, put on wall.
											// bits 654 - the head. Hopefully Minecraft won't add more than 8 heads...
											// bits 3210 - depends on bit 7; rotation if on floor, or on which wall (2-5)
											if (gBoxData[boxIndex].data > 1) {
												// head is on the wall, so rotation is ignored; just store the head in the high 4 bits
												assert((pBE->data & 0x80) == 0x0);	// topmost bit better not be used...
												// use wall rotation value 2-5 in lower 4 bits, put head type in next top 3 bits.
												gBoxData[boxIndex].data |= pBE->data & 0x70;
											}
											else {
												// head is on the floor, use the rotation angle too.
												assert(gBoxData[boxIndex].data == 1);
												// flag very highest bit: this means the lower data field is the rotation angle, 0-16, like sign posts.
												// use head data and rotation data, and flag topmost bit to note it's this way
												gBoxData[boxIndex].data = pBE->data | 0x80;
											}
										}
									}
									break;
								}
							}
							// we should always find a match, if things are working properly
							// (well, the test block world does not currently generate block entities, TODO, so this is not quite true)
							//assert(i < block->numEntities);
						}
					}
					else if ((blockID == BLOCK_DOUBLE_FLOWER) && (gBoxData[boxIndex].data & 0x8)) {
						// The top part of the flower doesn't always have the lower bits that identifies it. Copy these over from the block below, if available.
						// This could screw up schematic export. TODO
						if (y > edgeWorldBox->min[Y]) {
							gBoxData[boxIndex].data = 0x8 | gBoxData[boxIndex - 1].data;
						}
					}
				}

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

                if ( blockID == BLOCK_UNKNOWN )
                {
                    gBadBlocksInModel++;
                }
            }
        }
    }
}

static bool willChangeBlockCommandModifyAir(ChangeBlockCommand *pCBC)
{
    while (pCBC != NULL) {
        if (pCBC->useFromArray) {
            // check air block
            if (pCBC->fromDataBitsArray[BLOCK_AIR] & 0x1) {
                return true;
            }
        }
        else {
            // check if air is in range
            if (pCBC->simpleFromTypeBegin == BLOCK_AIR) {
                return true;
            }
        }
        pCBC = pCBC->next;
    }
    return false;
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

// This is used to clear out the bordering upper and lower slabs surrounding the model of data
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

// Clear a block of its data. Usually used for preprocessing the borders of the box of data.
static void editBlock( int x, int y, int z, int editMode )
{
    int boxIndex = BOX_INDEX(x,y,z);

    switch ( editMode )
    {
    case EDIT_MODE_CLEAR_TYPE:
		gBoxData[boxIndex].type = BLOCK_AIR;
		// don't clear data field, since origType is still intact
		break;
    case EDIT_MODE_CLEAR_ALL:
		gBoxData[boxIndex].type = gBoxData[boxIndex].origType = BLOCK_AIR;
		gBoxData[boxIndex].data = 0x0f;
		break;
    case EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES:
        // if type is an entrance, clear it fully: done so seed propagation along borders happens properly
        if ( gBlockDefinitions[gBoxData[boxIndex].origType].flags & BLF_ENTRANCE )
        {
            gBoxData[boxIndex].origType = BLOCK_AIR;
        }
        gBoxData[boxIndex].type = BLOCK_AIR;
		// don't clear data field, since origType may still be intact
		break;
    default:
        assert(0);
        break;
    }
}

// remove snow blocks and anything else not desired
static int filterBox(ChangeBlockCommand *pCBC)
{
    int boxIndex;
    int x, y, z;
    // Push flattop onto block below
    int flatten = gOptions->pEFD->chkMergeFlattop && CHECK_COMPOSITE_OVERLAY;

    int retCode = MW_NO_ERROR;
    int foundBlock = 0;

    int outputFlags, retVal;

    // Filter out all stuff that is not to be rendered. Done before anything, as these blocks simply
    // should not exist for all operations beyond.
    for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
    {
        for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
            for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
            {
                // sorry, air is never allowed to turn solid
                if (gBoxData[boxIndex].type != BLOCK_AIR)
                {
                    int flags = gBlockDefinitions[gBoxData[boxIndex].type].flags;

                    // check if it's something to be filtered out: not in the output list or alpha is 0
                    if (!(flags & gOptions->saveFilterFlags) ||
							gBlockDefinitions[gBoxData[boxIndex].type].alpha <= 0.0) {
						// things that should not be saved should be gone, gone, gone, no trace left
						gBoxData[boxIndex].type = gBoxData[boxIndex].origType = BLOCK_AIR;
						gBoxData[boxIndex].data = 0x0;
					}
				}
			}
		}
	}

	// world's loaded and normalized, now apply any change block commands
	if (pCBC != NULL) {
		ChangeBlockCommand *pCurCBC = pCBC;
		bool passed = true;
		while (pCurCBC != NULL) {
			// apply the command
			passed = applyChangeBlockCommand(pCurCBC);
			// Note the solid world bounds are useful for knowing where non-air stuff is, for all commands where "from" is not air

			// next command
			pCurCBC = pCurCBC->next;
		}
		if (!passed) {
			retCode |= MW_CHANGE_BLOCK_COMMAND_OUT_OF_BOUNDS;
		}
	}

	// recheck if modified world is now empty. Could be emptied by color scheme or commands
	if (isWorldVolumeEmpty())
	{
		retCode |= MW_NO_BLOCKS_FOUND;
	}

	// what should we output? Only 3D bits (no billboards) if printing or if textures are off
	if (gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES))
	{
		outputFlags = BLF_3D_BIT;
	}
	else
	{
		outputFlags = (BLF_BILLBOARD | BLF_SMALL_BILLBOARD | BLF_TRUE_GEOMETRY);
		if (!CHECK_COMPOSITE_OVERLAY)
		{
			outputFlags |= BLF_OFFSET;
		}
	}

	// if we are not outputting schematics, we need more passes:
	// 1) determine redstone connectivity
	// 2) output billboards and small stuff, or flatten stuff onto other blocks
	// 3) output billboard again, for offset object output, which is not done in first pass.
	if (gOptions->pEFD->fileType != FILE_TYPE_SCHEMATIC) {
		if (gExportBillboards && !CHECK_COMPOSITE_OVERLAY)
		{
			// Special pass: if we are outputting billboards and we're not doing composites,
			// we have to do a pre-pass through all redstone to get its connectivity all set up
			// before outputting to billboards. If composites are on, this is done in computeFlatFlags
			// and won't get used until the full blocks are output.
			for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
			{
				for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
				{
					boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
					for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
					{
						if (gBoxData[boxIndex].type == BLOCK_REDSTONE_WIRE)
							computeRedstoneConnectivity(boxIndex);
					}
				}
			}
		}
		int type;
		// check for billboards and lesser geometry that can flatten - immediately output. Flatten that which should be flattened. 
		for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
		{
			for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
			{
				boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
				for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
				{
					// sorry, air is never allowed to turn solid
					type = gBoxData[boxIndex].type;
					if (type != BLOCK_AIR)
					{
						if (type >= NUM_BLOCKS_DEFINED) {
							// convert to bedrock, I guess...
							// This check is more a symptom than a cure;
							// there seems to be some error where data gets read and is too large, for some reason.
							// But it's flakey - seems more like uninitialized or corrupted memory. Ugh.
							type = BLOCK_BEDROCK;
							gBoxData[boxIndex].type = BLOCK_BEDROCK;
							gBoxData[boxIndex].data = 0x0;
							gBadBlocksInModel = true;
						}
						int flags = gBlockDefinitions[type].flags;
						// check: is it a billboard we can export? Clear it out if so.
						int blockProcessed = 0;
						if (gExportBillboards)	// same as gOptions->pEFD->chkExportAll
						{
							// Export billboards or flattenable geometry, only. So test by export flags,
							// and also test if it's a billboard or flattenable thing.
							// TODO: Should any blocks that are bits get used to note connected objects,
							// so that floaters are not deleted? Probably... but we don't try to test.
							if ((flags & outputFlags) && (flags & (BLF_BILLBOARD | BLF_SMALL_BILLBOARD | BLF_FLATTEN | BLF_FLATTEN_SMALL)))
							{
								// tricksy code: if the return value > 1, then it's an error
								// and should be treated as such.
								retVal = saveBillboardOrGeometry(boxIndex, type);
								if (retVal == 1)
								{
									// this block is then cleared out, since it's been processed.
									if (IS_WATERLOGGED(type, boxIndex)) {
										// clears to water if waterlogged, e.g., seagrass
										gBoxData[boxIndex].type = BLOCK_STATIONARY_WATER;
										gBoxData[boxIndex].data = 8;
									}
									else {
										gBoxData[boxIndex].type = BLOCK_AIR;
									}
                                    // do NOT do this, as we use the data later to check if geometry
                                    // covers a voxel face, etc., e.g. stairs in particular:
                                    // NO NO NO gBoxData[boxIndex].data = 0x0;
                                    blockProcessed = 1;
                                }
                                else if (retVal >= MW_BEGIN_ERRORS)
                                {
                                    return retVal;
                                }
                            }
                        }

                        // not filtered out by the basics or billboard, so try to flatten
                        if (!blockProcessed && flatten && (flags & (BLF_FLATTEN | BLF_FLATTEN_SMALL)))
                        {
                            // this block is redstone, a rail, a ladder, etc. - shove its face to the top of the next cell down,
                            // or to its neighbor, or both (depends on dataval),
                            // instead of rendering a block for it.

                            // was: gBoxData[boxIndex-1].flatFlags = type;
                            // if object was indeed flattened, set it to air
                            if (computeFlatFlags(boxIndex))
                            {
								if (IS_WATERLOGGED(type, boxIndex)) {
									// clears to water if waterlogged, e.g., seagrass
									gBoxData[boxIndex].type = BLOCK_STATIONARY_WATER;
									gBoxData[boxIndex].data = 8; // level of water set to full
								}
								else {
									gBoxData[boxIndex].type = BLOCK_AIR;
								}
								// don't do this: we may use origType and data at some point:
                                // NO NO NO: gBoxData[boxIndex].data = 0x0;
                            }
                            else
                            {
                                // not flattened, so it is real content that's output
                                blockProcessed = 1;
                            }
                        }
                        else
                        {
                            // non-flattened, non-air block found
                            blockProcessed = 1;
                        }
                        // note that we found any sort of block that was valid (flats don't count, whatever
                        // they're pushed against needs to exist, too)
                        foundBlock |= blockProcessed;
                    }
                }
            }
        }

        // check for other lesser geometry, which could have faces that have something flattened onto them 
        if (gExportBillboards)	// same as gOptions->pEFD->chkExportAll
        {
            for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
            {
                for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
                {
                    boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
                    for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
                    {
                        // sorry, air is never allowed to turn solid
						type = gBoxData[boxIndex].type;
                        if (type != BLOCK_AIR)
                        {
                            int flags = gBlockDefinitions[type].flags;
                            // check: is it geometry we can export? Clear it out if so.

                            // If we're 3d printing, or rendering without textures, then export 3D printable bits,
                            // on the assumption that the software can merge the data properly with the solid model.
                            // TODO: Should any blocks that are bits get used to note connected objects,
                            // so that floaters are not deleted? Probably... but we don't try to test.
                            if (flags & outputFlags)
                            {
                                retVal = saveBillboardOrGeometry(boxIndex, type);
                                if (retVal == 1)
                                {
                                    // this block is then cleared out, since it's been processed.
									if (IS_WATERLOGGED(type, boxIndex)) {
										// clears to water if waterlogged, e.g., seagrass
										gBoxData[boxIndex].type = BLOCK_STATIONARY_WATER;
										gBoxData[boxIndex].data = 8;
									}
									else {
										gBoxData[boxIndex].type = BLOCK_AIR;
									}
									// do NOT do this, as we use the data later to check if geometry
                                    // covers a voxel face, etc., e.g. stairs in particular:
                                    // NO NO NO gBoxData[boxIndex].data = 0x0;
                                    foundBlock = 1;
                                }
                                else if (retVal >= MW_BEGIN_ERRORS)
                                {
                                    return retVal;
                                }
                            }
                        }
                    }
                }
            }
        }

        // 1%
        UPDATE_PROGRESS(0.70f*PG_MAKE_FACES);
        // were any useful (non-flat) blocks found? Don't do this test if we're not flattening nor exporting billboards.
        if ( foundBlock == 0 && ( flatten || gExportBillboards) )
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
        if (gOptions->exportFlags & (EXPT_FILL_BUBBLES | EXPT_CONNECT_PARTS | EXPT_DELETE_FLOATING_OBJECTS | EXPT_DEBUG_SHOW_GROUPS))
        {
            // If we're modifying blocks, we need to stash the border blocks away and clear them.
            if (!gOptions->pEFD->chkBlockFacesAtBorders)
            {
                // not putting borders in final output, so need to do two things:
                // The types in the borders must all be cleared. The original type should be left intact for output, *except*:
                // any entrance blocks need to be fully cleared so that they don't mess with the seed propagation process.
                // This is the only use of "originalType" during this block editing process below.
                modifySides(EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
                modifySlab(gAirBox.min[Y], EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
                modifySlab(gAirBox.max[Y], EDIT_MODE_CLEAR_TYPE_AND_ENTRANCES);
            }

            int foundTouching = 0;
            gGroupListSize = 200;
            gGroupList = (BoxGroup *)malloc(gGroupListSize*sizeof(BoxGroup));
            if (gGroupList == NULL)
            {
                return retCode | MW_WORLD_EXPORT_TOO_LARGE;
            }

            memset(gGroupList, 0, gGroupListSize*sizeof(BoxGroup));
            gGroupCount = 0;

            static bool showCoat = false;

            if (showCoat) {
                coatSurfaces();
            }
            else {

                // Another use of seal off entrances: look for doors and windows that can be temporarily sealed off,
                // check and remove bubbles, then we'll do this again (further below) to check if the sealed off doors
                // and windows themselves should be permanently filled in.
                if ((gOptions->exportFlags & EXPT_SEAL_ENTRANCES) && (gOptions->exportFlags & EXPT_FILL_BUBBLES)) {
                    // coat everything with fake coating of blocks
                    coatSurfaces();
                    // if we do expansion testing
                    retCode |= findGroups();
                    if (retCode >= MW_BEGIN_ERRORS) return retCode;

                    if (gAirGroups > 1)
                    {
                        checkAndRemoveBubbles();
                    }

                    // now remove the coating blocks
                    removeCoatingAndGroups();

                    // and do it again
                    gGroupCount = 0;
                    gSolidGroups = gAirGroups = 0;
                }
            }

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
    }
    return retCode;
}

// returns false if the location box doesn't overlap the selected volume
static bool applyChangeBlockCommand(ChangeBlockCommand *pCBC)
{
    int boxIndex;
    int x, y, z;
    unsigned char toType = pCBC->intoType;
    unsigned char toData = pCBC->intoData;

    IBox boxBounds = gSolidBox;
    if (pCBC->hasLocation)
    {
        // tighten box as possible
        IBox testBox;
        testBox.min[X] = pCBC->minxVal + gWorld2BoxOffset[X];
        testBox.min[Y] = pCBC->minyVal + gWorld2BoxOffset[Y];
        testBox.min[Z] = pCBC->minzVal + gWorld2BoxOffset[Z];
        testBox.max[X] = pCBC->maxxVal + gWorld2BoxOffset[X];
        testBox.max[Y] = pCBC->maxyVal + gWorld2BoxOffset[Y];
        testBox.max[Z] = pCBC->maxzVal + gWorld2BoxOffset[Z];
        if (boxBounds.min[X] < testBox.min[X])
            boxBounds.min[X] = testBox.min[X];
        if (boxBounds.min[Y] < testBox.min[Y])
            boxBounds.min[Y] = testBox.min[Y];
        if (boxBounds.min[Z] < testBox.min[Z])
            boxBounds.min[Z] = testBox.min[Z];
        if (boxBounds.max[X] > testBox.max[X])
            boxBounds.max[X] = testBox.max[X];
        if (boxBounds.max[Y] > testBox.max[Y])
            boxBounds.max[Y] = testBox.max[Y];
        if (boxBounds.max[Z] > testBox.max[Z])
            boxBounds.max[Z] = testBox.max[Z];
        // empty area?
        if ((boxBounds.min[X] > boxBounds.max[X]) ||
            (boxBounds.min[Y] > boxBounds.max[Y]) ||
            (boxBounds.min[Z] > boxBounds.max[Z])) {
            return false;
        }
    }

    for (x = boxBounds.min[X]; x <= boxBounds.max[X]; x++)
    {
        for (z = boxBounds.min[Z]; z <= boxBounds.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, boxBounds.min[Y], z);
            for (y = boxBounds.min[Y]; y <= boxBounds.max[Y]; y++, boxIndex++)
            {
                // two types of changes: array of bits, or a simple range
                bool modify = false;
                if (pCBC->useFromArray) {
                    // apply array: see if bit for type is flagged
                    if (pCBC->fromDataBitsArray[gBoxData[boxIndex].type] & (1 << gBoxData[boxIndex].data))
                        modify = true;
                }
                else {
                    // apply range and bits
                    int fromType = gBoxData[boxIndex].type;
                    if ((pCBC->simpleFromDataBits & (1 << gBoxData[boxIndex].data)) &&
                        (pCBC->simpleFromTypeBegin <= fromType) &&
                        (pCBC->simpleFromTypeEnd >= fromType)) {
                        modify = true;
                    }
                    if (modify) {
                        gBoxData[boxIndex].type = gBoxData[boxIndex].origType = toType;
                        gBoxData[boxIndex].data = toData;
                    }
                }
            }
        }
    }
    return true;
}

// both check if the world is empty, and lower the gSolidBox.max[Y] as much as possible
static bool isWorldVolumeEmpty()
{
    int boxIndex;
    int x, y, z;
    // go from high to low
    for (y = gSolidBox.max[Y]; y >= gSolidBox.min[Y]; y++)
    {
        for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++) {
            boxIndex = BOX_INDEX(x, y, gSolidBox.min[Z]);
            for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++, boxIndex += gBoxSize[Y])
            {
                if (gBoxData[boxIndex].type != BLOCK_AIR) {
                    gSolidBox.max[Y] = y;
                    return false;
                }
            }
        }
    }
    //for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
    //{
    //	for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
    //	{
    //		boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
    //		for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
    //		{
    //			if (gBoxData[boxIndex].type != BLOCK_AIR)
    //				return false;
    //		}
    //	}
    //}
    return true;
}

// connectivity, with the 4 high bits of .data storing it, leaving the power level of 0-15 intact.
static void computeRedstoneConnectivity(int boxIndex)
{
    //BLOCK_REDSTONE_WIRE:
    gBoxData[boxIndex - 1].flatFlags |= FLAT_FACE_ABOVE;
    // look to see whether there is wire neighboring and above: if so, run this wire
    // up the sides of the blocks

    // first, is the block above the redstone wire not a whole block, or is a whole block and is glass on the outside or a piston?
    // If so, then wires can run up the sides; whole blocks that are not glass cut redstone wires.
    if (!(gBlockDefinitions[gBoxData[boxIndex + 1].origType].flags & BLF_WHOLE) ||
        (gBoxData[boxIndex + 1].origType == BLOCK_PISTON) ||
        (gBoxData[boxIndex + 1].origType == BLOCK_GLASS) ||
        (gBoxData[boxIndex + 1].origType == BLOCK_GLOWSTONE) ||
        (gBoxData[boxIndex + 1].origType == BLOCK_STAINED_GLASS))
    {
        // first hurdle passed - now check each in turn: is block above wire. If so,
        // then these will connect. Note we must check again origType, as wires get culled out
        // as we go through the blocks.
        if (gBoxData[boxIndex + 1 + gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE)
        {
			// (upside down) stairs do not have redstone put on their sides.
			if (!(gBlockDefinitions[gBoxData[boxIndex + gBoxSizeYZ].type].flags & (BLF_STAIRS| BLF_HALF))) {
				gBoxData[boxIndex + gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
			}
            gBoxData[boxIndex + 1 + gBoxSizeYZ].data |= (FLAT_FACE_LO_X << 4);
            gBoxData[boxIndex].data |= (FLAT_FACE_HI_X << 4);
        }
        if (gBoxData[boxIndex + 1 - gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE)
        {
			// (upside down) stairs do not have redstone put on their sides.
			if (!(gBlockDefinitions[gBoxData[boxIndex - gBoxSizeYZ].type].flags & (BLF_STAIRS| BLF_HALF))) {
				gBoxData[boxIndex - gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
			}
            gBoxData[boxIndex + 1 - gBoxSizeYZ].data |= (FLAT_FACE_HI_X << 4);
            gBoxData[boxIndex].data |= (FLAT_FACE_LO_X << 4);
        }
        if (gBoxData[boxIndex + 1 + gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE)
        {
			// (upside down) stairs do not have redstone put on their sides.
			if (!(gBlockDefinitions[gBoxData[boxIndex + gBoxSize[Y]].type].flags & (BLF_STAIRS| BLF_HALF))) {
				gBoxData[boxIndex + gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
			}
            gBoxData[boxIndex + 1 + gBoxSize[Y]].data |= (FLAT_FACE_LO_Z << 4);
            gBoxData[boxIndex].data |= (FLAT_FACE_HI_Z << 4);
        }
        if (gBoxData[boxIndex + 1 - gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE)
        {
			// (upside down) stairs do not have redstone put on their sides.
			if (!(gBlockDefinitions[gBoxData[boxIndex - gBoxSize[Y]].type].flags & (BLF_STAIRS| BLF_HALF))) {
				gBoxData[boxIndex - gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
			}
            gBoxData[boxIndex + 1 - gBoxSize[Y]].data |= (FLAT_FACE_HI_Z << 4);
            gBoxData[boxIndex].data |= (FLAT_FACE_LO_Z << 4);
        }
    }
    // finally, check the +X and +Z neighbors on this level: if wire, connect them.
    // Note that the other wires in the world will pick up the other 6 possibilities:
    // -X and -Z on this level (by these same tests below) and the 4 "wires down a level"
    // possibilities (by these same tests above).
    // Test *all* things that redstone connects to. This could become a table, for speed.
    if ((gBlockDefinitions[gBoxData[boxIndex + gBoxSizeYZ].origType].flags & BLF_CONNECTS_REDSTONE) ||
        // repeaters attach only at their ends, so test the direction they're at
        (gBoxData[boxIndex + gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_OFF && (gBoxData[boxIndex + gBoxSizeYZ].data & 0x1)) ||
        (gBoxData[boxIndex + gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_ON && (gBoxData[boxIndex + gBoxSizeYZ].data & 0x1))
        )
    {
        if (gBoxData[boxIndex + gBoxSizeYZ].origType == BLOCK_REDSTONE_WIRE)
            gBoxData[boxIndex + gBoxSizeYZ].data |= (FLAT_FACE_LO_X << 4);
        gBoxData[boxIndex].data |= (FLAT_FACE_HI_X << 4);
    }
    if ((gBlockDefinitions[gBoxData[boxIndex + gBoxSize[Y]].origType].flags & BLF_CONNECTS_REDSTONE) ||
        // repeaters attach only at their ends, so test the direction they're at
        (gBoxData[boxIndex + gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_OFF && !(gBoxData[boxIndex + gBoxSize[Y]].data & 0x1)) ||
        (gBoxData[boxIndex + gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_ON && !(gBoxData[boxIndex + gBoxSize[Y]].data & 0x1))
        )
    {
        if (gBoxData[boxIndex + gBoxSize[Y]].origType == BLOCK_REDSTONE_WIRE)
            gBoxData[boxIndex + gBoxSize[Y]].data |= (FLAT_FACE_LO_Z << 4);
        gBoxData[boxIndex].data |= (FLAT_FACE_HI_Z << 4);
    }
    // catch redstone torches at the -X and -Z faces
    if ((gBlockDefinitions[gBoxData[boxIndex - gBoxSizeYZ].origType].flags & BLF_CONNECTS_REDSTONE) ||
        // repeaters attach only at their ends, so test the direction they're at
        (gBoxData[boxIndex - gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_OFF && (gBoxData[boxIndex - gBoxSizeYZ].data & 0x1)) ||
        (gBoxData[boxIndex - gBoxSizeYZ].origType == BLOCK_REDSTONE_REPEATER_ON && (gBoxData[boxIndex - gBoxSizeYZ].data & 0x1))
        )
    {
        gBoxData[boxIndex].data |= (FLAT_FACE_LO_X << 4);
    }
    if ((gBlockDefinitions[gBoxData[boxIndex - gBoxSize[Y]].origType].flags & BLF_CONNECTS_REDSTONE) ||
        // repeaters attach only at their ends, so test the direction they're at
        (gBoxData[boxIndex - gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_OFF && !(gBoxData[boxIndex - gBoxSize[Y]].data & 0x1)) ||
        (gBoxData[boxIndex - gBoxSize[Y]].origType == BLOCK_REDSTONE_REPEATER_ON && !(gBoxData[boxIndex - gBoxSize[Y]].data & 0x1))
        )
    {
        gBoxData[boxIndex].data |= (FLAT_FACE_LO_Z << 4);
    }
}

// return 1 if object is flattened and block should now be considered air
static int computeFlatFlags( int boxIndex )
{
    // for this box's contents, mark the neighbor(s) that should receive
    // its flatness
    IPoint loc;

    switch ( gBoxData[boxIndex].type )
    {
        // the block below this one, if solid, gets marked
    case BLOCK_STONE_PRESSURE_PLATE:						// computeFlatFlags
    case BLOCK_WOODEN_PRESSURE_PLATE:
	case BLOCK_SPRUCE_PRESSURE_PLATE:
	case BLOCK_BIRCH_PRESSURE_PLATE:
	case BLOCK_JUNGLE_PRESSURE_PLATE:
	case BLOCK_ACACIA_PRESSURE_PLATE:
	case BLOCK_DARK_OAK_PRESSURE_PLATE:
	case BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT:
    case BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY:
    case BLOCK_SNOW:
    case BLOCK_CARPET:
    case BLOCK_REDSTONE_REPEATER_OFF:
    case BLOCK_REDSTONE_REPEATER_ON:
    case BLOCK_REDSTONE_COMPARATOR:
    case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
    case BLOCK_LILY_PAD:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_BROWN_MUSHROOM:
    case BLOCK_RED_MUSHROOM:
    case BLOCK_SAPLING:
    case BLOCK_GRASS:
    case BLOCK_DEAD_BUSH:
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM:
    case BLOCK_DAYLIGHT_SENSOR:
    case BLOCK_INVERTED_DAYLIGHT_SENSOR:
	case BLOCK_DOUBLE_FLOWER:
	case BLOCK_TALL_SEAGRASS:
	case BLOCK_SEAGRASS:
	case BLOCK_KELP:
	case BLOCK_CORAL:
	case BLOCK_CORAL_FAN:
	case BLOCK_DEAD_CORAL_FAN:
	case BLOCK_CORAL_WALL_FAN:
	case BLOCK_DEAD_CORAL_WALL_FAN:
	case BLOCK_DEAD_CORAL: // TODO probably never: for 3D printing, could turn this X decal into a dead coral block
	case BLOCK_SWEET_BERRY_BUSH:
	case BLOCK_CAMPFIRE:
		gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
        break;

		// easy ones: flattops
	case BLOCK_RAIL:						// computeFlatFlags
		if (gBoxData[boxIndex].data >= 6)
		{
			// curved rail bit, it's always just flat
			gBoxData[boxIndex - 1].flatFlags |= FLAT_FACE_ABOVE;
			break;
		}
		// NOTE: if curve test failed, needed only for basic rails, continue on through tilted track tests
		// SO - don't dare put a break here, we need to flow through, to avoid repeating all this code
	case BLOCK_POWERED_RAIL:						// computeFlatFlags
	case BLOCK_DETECTOR_RAIL:
	case BLOCK_ACTIVATOR_RAIL:
		// only pay attention to sloped rails, as these mark sides;
		// remove top bit, as that's whether it's powered
		switch (gBoxData[boxIndex].data & 0x7)
		{
		case 2: // east, +X
			gBoxData[boxIndex + gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
			break;
		case 3:
			gBoxData[boxIndex - gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
			break;
		case 4:
			gBoxData[boxIndex - gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
			break;
		case 5:
			gBoxData[boxIndex + gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
			break;
		default:
			// don't do anything, this rail is not sloped; continue on down to mark top face
			break;
		}
		gBoxData[boxIndex - 1].flatFlags |= FLAT_FACE_ABOVE;
		break;

	case BLOCK_TORCH:						// computeFlatFlags
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        switch ( gBoxData[boxIndex].data )
        {
        case 1: // east, +X
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

    case BLOCK_LADDER:						// computeFlatFlags
    case BLOCK_WALL_SIGN:
    case BLOCK_WALL_BANNER:
	case BLOCK_ORANGE_WALL_BANNER:
	case BLOCK_MAGENTA_WALL_BANNER:
	case BLOCK_LIGHT_BLUE_WALL_BANNER:
	case BLOCK_YELLOW_WALL_BANNER:
	case BLOCK_LIME_WALL_BANNER:
	case BLOCK_PINK_WALL_BANNER:
	case BLOCK_GRAY_WALL_BANNER:
	case BLOCK_LIGHT_GRAY_WALL_BANNER:
	case BLOCK_CYAN_WALL_BANNER:
	case BLOCK_PURPLE_WALL_BANNER:
	case BLOCK_BLUE_WALL_BANNER:
	case BLOCK_BROWN_WALL_BANNER:
	case BLOCK_GREEN_WALL_BANNER:
	case BLOCK_RED_WALL_BANNER:
	case BLOCK_BLACK_WALL_BANNER:
		switch ( gBoxData[boxIndex].data & 0x7)
        {
        case 2: // north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 4: // west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 5: // east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_LEVER:						// computeFlatFlags
        switch ( gBoxData[boxIndex].data & 0x7 )
        {
        case 1: // east, +X
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
    case BLOCK_STONE_BUTTON:						// computeFlatFlags
    case BLOCK_WOODEN_BUTTON:
	case BLOCK_SPRUCE_BUTTON:
	case BLOCK_BIRCH_BUTTON:
	case BLOCK_JUNGLE_BUTTON:
	case BLOCK_ACACIA_BUTTON:
	case BLOCK_DARK_OAK_BUTTON:
		switch ( gBoxData[boxIndex].data & 0x7 )
        {
        case 0: // at top of block, +Y
            gBoxData[boxIndex+1].flatFlags |= FLAT_FACE_BELOW;
            break;
        case 5: // at bottom of block, -Y
            gBoxData[boxIndex-1].flatFlags |= FLAT_FACE_ABOVE;
            break;
        case 4: // north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 2: // west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 1: // east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_TRIPWIRE_HOOK:						// computeFlatFlags
        // 0x4 means "tripwire connected"
        // 0x8 means "tripwire tripped"
        switch ( gBoxData[boxIndex].data & 0x3 )
        {
        case 0: // south, +Z
            gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
            break;
        case 1: // west, -X
            gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
            break;
        case 2: // north, -Z
            gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
            break;
        case 3: // east, +X
            gBoxData[boxIndex-gBoxSizeYZ].flatFlags |= FLAT_FACE_HI_X;
            break;
        default:
            assert(0);
            return 0;
        }
        break;

    case BLOCK_TRAPDOOR:						// computeFlatFlags
    case BLOCK_IRON_TRAPDOOR:
	case BLOCK_SPRUCE_TRAPDOOR:
	case BLOCK_BIRCH_TRAPDOOR:
	case BLOCK_JUNGLE_TRAPDOOR:
	case BLOCK_ACACIA_TRAPDOOR:
	case BLOCK_DARK_OAK_TRAPDOOR:
		if ( gBoxData[boxIndex].data & 0x4 )
        {
            // trapdoor is open, so is against a wall
            switch ( gBoxData[boxIndex].data & 0x3 )
            {
            case 0: // north, -Z
                gBoxData[boxIndex+gBoxSize[Y]].flatFlags |= FLAT_FACE_LO_Z;
                break;
            case 1: // south, +Z
                gBoxData[boxIndex-gBoxSize[Y]].flatFlags |= FLAT_FACE_HI_Z;
                break;
            case 2: // west, -X
                gBoxData[boxIndex+gBoxSizeYZ].flatFlags |= FLAT_FACE_LO_X;
                break;
            case 3: // east, +X
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

    case BLOCK_VINES:						// computeFlatFlags
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

    case BLOCK_REDSTONE_WIRE:						// computeFlatFlags
        computeRedstoneConnectivity(boxIndex);
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
	// If this number is large, it's likely that we sent the wrong value in
	assert(count <= 400);
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

// output 8 vertices of pickle top
static void outputPickleTop(int boxIndex, int swatchLoc, float shift)
{
	if (!gPrint3D && IS_WATERLOGGED(BLOCK_SEA_PICKLE, boxIndex)) {
		float mtx[4][4];
		int totalVertexCount = gModel.vertexCount;
		saveBoxMultitileGeometry(boxIndex, BLOCK_SEA_PICKLE, 0x0, swatchLoc, swatchLoc, swatchLoc, 0,
			DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT,
			FLIP_Z_FACE_VERTICALLY, 0, 4, 11, 16, 8, 8);
		totalVertexCount = gModel.vertexCount - totalVertexCount;

		identityMtx(mtx);
		translateToOriginMtx(mtx, boxIndex);
		translateMtx(mtx, 6.0f / 16.0f, 0.0f, 0.0f);
		rotateMtx(mtx, 0.0f, 45.0f, 0.0f);
		translateMtx(mtx, -6.0f / 16.0f, (shift-1.0f) / 16.0f, -2.0f / 16.0f);
		translateFromOriginMtx(mtx, boxIndex);
		transformVertices(totalVertexCount, mtx);

		totalVertexCount = gModel.vertexCount;
		saveBoxMultitileGeometry(boxIndex, BLOCK_SEA_PICKLE, 0x0, swatchLoc, swatchLoc, swatchLoc, 0,
			DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT,
			FLIP_Z_FACE_VERTICALLY, 0, 4, 11, 16, 8, 8);
		totalVertexCount = gModel.vertexCount - totalVertexCount;

		identityMtx(mtx);
		translateToOriginMtx(mtx, boxIndex);
		translateMtx(mtx, 6.0f / 16.0f, 0.0f, 0.0f);
		rotateMtx(mtx, 0.0f, 135.0f, 0.0f);
		translateMtx(mtx, -6.0f / 16.0f, (shift-1.0f)  / 16.0f, -2.0f / 16.0f);
		translateFromOriginMtx(mtx, boxIndex);
		transformVertices(totalVertexCount, mtx);

		// I would have thought the upper right part of the tendrils for the sea pickle would be used. That's this:
		//saveBoxMultitileGeometry(boxIndex, BLOCK_SEA_PICKLE, 0x0, swatchLoc, swatchLoc, swatchLoc, 0,
		//	DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT,
		//	FLIP_Z_FACE_VERTICALLY, 12, 16, 11, 16, 8, 8);
		//totalVertexCount = gModel.vertexCount - totalVertexCount;

		//identityMtx(mtx);
		//static float x = -6.0f;
		//static float z = -2.0f;
		//translateToOriginMtx(mtx, boxIndex);
		//translateMtx(mtx, -6.0f / 16.0f, 0.0f, 0.0f);
		//rotateMtx(mtx, 0.0f, 135.0f, 0.0f);
		//translateMtx(mtx, x / 16.0f, -1.0f / 16.0f, z / 16.0f);
		//translateFromOriginMtx(mtx, boxIndex);
		//transformVertices(totalVertexCount, mtx);
	}
}

// Some growing objects get "wobbled" by +/- 3/16:
// Bamboo sapling - yes, other saplings - no
// Grass, fern, flowers - yes, dead bush - no
// Tall grass and other tall flowers - yes
// Seagrass - yes
// everything else (mushrooms, crops, stems, carrots, potatoes, beetroot, nether wart, kelp) do not
static void wobbleObjectLocation(int boxIndex, float &shiftX, float &shiftZ)
{
	seedWithXZ(boxIndex);
	float val = (float)rand() / (RAND_MAX + 1);
	shiftX = (float)(6 * val - 3);
	val = (float)rand() / (RAND_MAX + 1);
	shiftZ = (float)(6 * val - 3);
}

static bool fenceNeighbor(int type, int boxIndex, int blockSide)
{
	int neighborIndex = boxIndex + gFaceOffset[blockSide];
	int neighborType = gBoxData[neighborIndex].origType;
	// is neighbor of same type? Easy out
	if (type == neighborType)
		return true;

	// is neighbor one that affects fences?
	if (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR)
		return true;

	// is neighbor a fence gate?
	if (gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) {
		// fence gate only connects if it is oriented properly
		int bitSet = ((blockSide == DIRECTION_BLOCK_SIDE_LO_Z) || (blockSide == DIRECTION_BLOCK_SIDE_HI_Z)) ? 0x1 : 0x0;
		if ((gBoxData[neighborIndex].data & 0x1) == bitSet)
			return true;
	}

	// is neighbor another fence that can adjoin? Wooden fences can join each other, nether brick fences stand alone
	if ((type != BLOCK_NETHER_BRICK_FENCE) && (neighborType != BLOCK_NETHER_BRICK_FENCE) && (gBlockDefinitions[neighborType].flags & BLF_FENCE))
		return true;

	// no fence connection found
	return false;
}

// return 1 if block processed as a billboard or true geometry
static int saveBillboardOrGeometry( int boxIndex, int type )
{
    int dataVal, faceMask, tbFaceMask, dir;
    float minx, maxx, miny, maxy, minz, maxz, bitAdd;
    int swatchLoc, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc;
    int topDataVal, bottomDataVal, shiftVal, neighborType;
    int i, firstFace, totalVertexCount, littleTotalVertexCount, uberTotalVertexCount, typeBelow, dataValBelow, useInsidesAndBottom, filled;
    float xrot, yrot, zrot;
    float hasPost, newHeight;
    float mtx[4][4], angle, hingeAngle, signMult;
    int swatchLocSet[6];
    // how much to add to dimension when fattening
    float fatten = (gOptions->pEFD->chkFatten) ? 2.0f : 0.0f;
    int retCode = MW_NO_ERROR;
    int transNeighbor,boxIndexBelow;
    int groupByBlock;
    int chestType, matchType;
    float waterHeight;
	int itemCount;
	int age;
	int leafSize;
	int facing;
	float shiftX, shiftZ;
	float x, z;

    dataVal = gBoxData[boxIndex].data;

    // Add to minor count if this object has some heft. This is approximate, but better than nothing.
	// This count can be slightly off if we return 0, meaning the block wasn't a billboard or geometry after all
    if ( gBlockDefinitions[type].flags & (BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF|BLF_MIDDLER|BLF_PANE))
    {
        gMinorBlockCount++;
    }

    switch ( type )
    {
    case BLOCK_SAPLING:						// saveBillboardOrGeometry
    case BLOCK_COBWEB:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_RED_MUSHROOM:
    case BLOCK_BROWN_MUSHROOM:
    case BLOCK_GRASS:
    case BLOCK_DEAD_BUSH:
    case BLOCK_SUGAR_CANE:
    case BLOCK_DOUBLE_FLOWER:
	case BLOCK_KELP:
	case BLOCK_CORAL:
	case BLOCK_DEAD_CORAL:
	case BLOCK_SWEET_BERRY_BUSH:
		return saveBillboardFaces( boxIndex, type, BB_FULL_CROSS );
        break;	// saveBillboardOrGeometry

	case BLOCK_SEAGRASS:
	case BLOCK_TALL_SEAGRASS:
		return saveBillboardFaces(boxIndex, type, BB_GRID);
		break;	// saveBillboardOrGeometry

	case BLOCK_CORAL_FAN:
	case BLOCK_DEAD_CORAL_FAN:
		return saveBillboardFaces(boxIndex, type, BB_FAN);
		break;	// saveBillboardOrGeometry

	case BLOCK_CORAL_WALL_FAN:
	case BLOCK_DEAD_CORAL_WALL_FAN:
		return saveBillboardFaces(boxIndex, type, BB_WALL_FAN);
		break;	// saveBillboardOrGeometry

	// special: billboard and possible an extra stem to the pumpkin or melon
    case BLOCK_PUMPKIN_STEM:						// saveBillboardOrGeometry
    case BLOCK_MELON_STEM:
        saveBillboardFaces(boxIndex, type, BB_FULL_CROSS);
        // if stem is full maturity and is next to a pumpkin/melon, add extra stem
        if ((dataVal&0x7) == 7)
        {
            // fully mature, change height to 10 if the proper fruit is next door
            matchType = (type == BLOCK_PUMPKIN_STEM) ? BLOCK_PUMPKIN : BLOCK_MELON;
            if (gBoxData[boxIndex - gBoxSizeYZ].type == matchType) {
                // to west
                angle = 0;
            }
            else if (gBoxData[boxIndex + gBoxSizeYZ].type == matchType) {
                angle = 180;
            }
            else if (gBoxData[boxIndex - gBoxSize[Y]].type == matchType) {
                angle = 90;
            }
            else if (gBoxData[boxIndex + gBoxSize[Y]].type == matchType) {
                angle = 270;
            }
            else
                // all done, nothing next to it
                return 1;

            // connected melon or pumpkin stem
            swatchLoc = (type == BLOCK_PUMPKIN_STEM) ? SWATCH_INDEX(15, 11) : SWATCH_INDEX(15, 7);
            totalVertexCount = gModel.vertexCount;

            // so sleazy: make this a top geometry so that top and bottom will match. Rotate to position, twice
            gUsingTransform = 1;
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0, 16, 8, 8, 0, 16);
            gUsingTransform = 0;
            totalVertexCount = gModel.vertexCount - totalVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, -90.0f, 0.0f, 0.0f);
            rotateMtx(mtx, 0.0f, (float)angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount, mtx);
        }
        return 1;
        break; // saveBillboardOrGeometry

    case BLOCK_CROPS:						// saveBillboardOrGeometry
    case BLOCK_NETHER_WART:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES:
    case BLOCK_BEETROOT_SEEDS:
        return saveBillboardFaces( boxIndex, type, BB_GRID );
        break; // saveBillboardOrGeometry
    case BLOCK_TORCH:						// saveBillboardOrGeometry
    case BLOCK_REDSTONE_TORCH_OFF:
    case BLOCK_REDSTONE_TORCH_ON:
        return saveBillboardFaces( boxIndex, type, BB_TORCH );
        break; // saveBillboardOrGeometry
    case BLOCK_RAIL:						// saveBillboardOrGeometry
    case BLOCK_POWERED_RAIL:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
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
            switch ( modDataVal & 0x7 )
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
                switch ( modDataVal & 0x7)
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
            switch ( modDataVal & 0x7)
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
        break; // saveBillboardOrGeometry


    case BLOCK_FIRE:						// saveBillboardOrGeometry
        return saveBillboardFaces( boxIndex, type, BB_FIRE );

    case BLOCK_VINES:						// saveBillboardOrGeometry
        return saveBillboardFaces(boxIndex, type, BB_SIDE);

    case BLOCK_LADDER:
        return saveBillboardFaces(boxIndex, type, BB_SIDE);

    case BLOCK_LILY_PAD:					// saveBillboardOrGeometry
        return saveBillboardFaces(boxIndex, type, BB_BOTTOM);


    case BLOCK_REDSTONE_WIRE:				// saveBillboardOrGeometry
        return saveBillboardFaces(boxIndex, type, BB_SIDE);


    /////////////////////////////////////////////////////////////////////////////////////////
    // real-live solid output, baby
    case BLOCK_FENCE:						// saveBillboardOrGeometry
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
        //groupByBlock = (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK);
        // if fence is to be fattened, instead make it like a brick wall - stronger
        if ( fatten )
        {
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

            // always put the post
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, 1, 0x0, 4, 12, 0, 16, 4, 12);
            // left in these variables, just in case we ever want to use them.
            hasPost = 1;
            firstFace = 0;

			if ( fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_LO_X) )
            {
                // this fence connects to the neighboring block, so output the fence pieces
                // - if we're doing 3D printing, neighbor type must exactly match for the face to be removed
				// removed, because gates can shift downwards, which can expose the ends
                //transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				//saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_HI_X_BIT) | (transNeighbor ? 0x0 : DIR_LO_X_BIT), 0, 8 - hasPost * 4, 0, 13, 5, 11);
				saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, 0x0, 0, 8 - hasPost * 4, 0, 13, 5, 11);
				firstFace = 0;
            }
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_HI_X))
			{
                // this fence connects to the neighboring block, so output the fence pieces
                //transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				//saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_LO_X_BIT) | (transNeighbor ? 0x0 : DIR_HI_X_BIT), 8 + hasPost * 4, 16, 0, 13, 5, 11);
				saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, 0x0, 8 + hasPost * 4, 16, 0, 13, 5, 11);
				firstFace = 0;
            }
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_LO_Z))
			{
                // this fence connects to the neighboring block, so output the fence pieces
                //transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				//saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_HI_Z_BIT) | (transNeighbor ? 0x0 : DIR_LO_Z_BIT), 5, 11, 0, 13, 0, 8 - hasPost * 4);
				saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, 0x0, 5, 11, 0, 13, 0, 8 - hasPost * 4);
				firstFace = 0;
            }
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_HI_Z))
			{
                // this fence connects to the neighboring block, so output the fence pieces
				//transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				//saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_HI_Z_BIT) | (transNeighbor ? 0x0 : DIR_HI_Z_BIT), 5, 11, 0, 13, 8 + hasPost * 4, 16);
				saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, 0x0, 5, 11, 0, 13, 8 + hasPost * 4, 16);
				firstFace = 0;	// not necessary, but for consistency in case code is added below
            }
        }
        else
        {
            // don't really need to "fatten", as it's now done above, but keeping fatten around is good to know.

            // post, always output
            saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 6 - fatten, 10 + fatten, 0, 16, 6 - fatten, 10 + fatten);
            // which posts are needed: NSEW. Brute-force it.

            // since we erase "billboard" objects as we go, we need to test against origType.
            // Note that if a render export chops through a fence, the fence will not join. TODO - this would be good to fix, as it means tiling output doesn't work in this case.
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_LO_X))
			{
                // this fence connects to the neighboring block, so output the fence pieces
				//transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				//saveBoxGeometry(boxIndex, type, dataVal, 0, (gPrint3D ? 0x0 : DIR_HI_X_BIT) | (transNeighbor ? 0x0 : DIR_LO_X_BIT), 0, 6 - fatten, 6, 9, 7 - fatten, 9 + fatten);
				//saveBoxGeometry(boxIndex, type, dataVal, 0, (gPrint3D ? 0x0 : DIR_HI_X_BIT) | (transNeighbor ? 0x0 : DIR_LO_X_BIT), 0, 6 - fatten, 12, 15, 7 - fatten, 9 + fatten);
				saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 0, 6 - fatten, 6, 9, 7 - fatten, 9 + fatten);
				saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 0, 6 - fatten, 12, 15, 7 - fatten, 9 + fatten);
			}
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_HI_X))
			{
                // this fence connects to the neighboring block, so output the fence pieces
                //transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 10 + fatten, 16, 6, 9, 7 - fatten, 9 + fatten);
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 10 + fatten, 16, 12, 15, 7 - fatten, 9 + fatten);
            }
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_LO_Z))
			{
                // this fence connects to the neighboring block, so output the fence pieces
                //transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 7 - fatten, 9 + fatten, 6, 9, 0, 6 - fatten);
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 7 - fatten, 9 + fatten, 12, 15, 0, 6 - fatten);
            }
			if (fenceNeighbor(type, boxIndex, DIRECTION_BLOCK_SIDE_HI_Z))
			{
                // this fence connects to the neighboring block, so output the fence pieces
				//transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (gPrint3D && (type != neighborType));
				saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 7 - fatten, 9 + fatten, 6, 9, 10 + fatten, 16);
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 7 - fatten, 9 + fatten, 12, 15, 10 + fatten, 16);
            }
        }
        break; // saveBillboardOrGeometry

    case BLOCK_COBBLESTONE_WALL:						// saveBillboardOrGeometry
        groupByBlock = (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK);
        // which posts are needed: NSEW. Brute-force it.

        // TODO: get more subtle, like glass panes, and generate only the faces needed. Right now there's overlap at corners, for example.
		switch (dataVal & 0xf)
		{
		default:
			assert(0);
		case 0:
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_COBBLESTONE].txrY);
			break;
		case 1: // mossy cobblestone
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrY);
			break;
		case 2: // brick wall
			swatchLoc = SWATCH_INDEX(7, 0);
			break;
		case 3: // granite wall
			swatchLoc = SWATCH_INDEX(8, 22);
			break;
		case 4: // diorite wall
			swatchLoc = SWATCH_INDEX(6, 22);
			break;
		case 5: // andesite wall
			swatchLoc = SWATCH_INDEX(4, 22);
			break;
		case 6: // prismarine wall
			swatchLoc = SWATCH_INDEX(12, 22);
			break;
		case 7: // stone brick wall
			swatchLoc = SWATCH_INDEX(6, 3);
			break;
		case 8: // mossy stone brick wall
			swatchLoc = SWATCH_INDEX(4, 6);
			break;
		case 9: // end stone brick wall
			swatchLoc = SWATCH_INDEX(3, 24);
			break;
		case 10: // nether brick wall
			swatchLoc = SWATCH_INDEX(0, 14);
			break;
		case 11: // red nether brick wall
			swatchLoc = SWATCH_INDEX(2, 26);
			break;
		case 12: // sandstone wall
			swatchLoc = SWATCH_INDEX(0, 12);
			break;
		case 13: // red sandstone wall
			swatchLoc = SWATCH_INDEX(14, 13);
			break;
		}

        // since we erase "billboard" objects as we go, we need to test against origType.
        // Note that if a render export chops through a fence, the fence will not join.

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
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
				((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].data & 0x1)) == 0))
            {
                xCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
				((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].data & 0x1)) == 0))
            {
                xCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
				((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].data & 0x1)) == 1))
            {
                zCount++;
            }
            neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
            if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
				((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].data & 0x1)) == 1))
            {
                zCount++;
            }

            // for cobblestone walls, if the count is anything but 2 and both along an axis, put the post
            if ( !(((xCount == 2) && (zCount == 0)) || ((xCount == 0) && (zCount == 2))) )
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
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, 1, 0x0, 4, 12, 0, 16, 4, 12);
            firstFace = 0;
        }
        else
        {
            firstFace = 1;
        }

        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
			((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].data & 0x1)) == 0))
        {
            // this fence connects to the neighboring block, so output the fence pieces
			// if the neighbor is transparent, or a different type, or individual blocks are made, we'll output the face facing the neighbor (important if we connect to a fence, for example)
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (type != neighborType);
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_HI_X_BIT) | (transNeighbor ? 0x0 : DIR_LO_X_BIT), 0, 8 - hasPost * 4, 0, 13, 5, 11);
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
			((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].data & 0x1)) == 0))
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (type != neighborType);
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_LO_X_BIT) | (transNeighbor ? 0x0 : DIR_HI_X_BIT), 8 + hasPost * 4, 16, 0, 13, 5, 11);
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
			((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].data & 0x1)) == 1))
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (type != neighborType);
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_HI_Z_BIT) | (transNeighbor ? 0x0 : DIR_LO_Z_BIT), 5, 11, 0, 13, 0, 8 - hasPost * 4);
            firstFace = 0;
        }
        neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
        if ( (type == neighborType) || (gBlockDefinitions[neighborType].flags & BLF_FENCE_NEIGHBOR) ||
			((gBlockDefinitions[neighborType].flags & BLF_FENCE_GATE) && ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].data & 0x1)) == 1))
        {
            // this fence connects to the neighboring block, so output the fence pieces
            transNeighbor = (gBlockDefinitions[neighborType].flags & BLF_TRANSPARENT) || groupByBlock || (type != neighborType);
            saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, firstFace, (gPrint3D ? 0x0 : DIR_LO_Z_BIT) | (transNeighbor ? 0x0 : DIR_HI_Z_BIT), 5, 11, 0, 13, 8 + hasPost * 4, 16);
            firstFace = 0;	// not necessary, but for consistency in case code is added below
        }
        break; // saveBillboardOrGeometry

    case BLOCK_CHORUS_PLANT:						// saveBillboardOrGeometry
		{
			// 6 sides, no interior
			// for each neighbor, decide whether to extend the side (and so not put the face).

			// top
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_TOP]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType)) ? 16.0f : 13.0f;
			//tricky: when extended, cap it only if 3D printing and the neighbor is NOT the same type; i.e. we want caps when it's a flower or end stone.
			//in other words, we remove face if not 3D printing and newHeight is 16, OR if neighbor == type.
			//Really, I'm doing too much with one line, but so be it - write-only code it is!
			saveBoxGeometry(boxIndex, type, dataVal, 1, (((!gPrint3D && (newHeight == 16)) || (type == neighborType)) ? DIR_TOP_BIT : 0x0) | DIR_BOTTOM_BIT, 4, 12, 12, newHeight, 4, 12);

			// bottom
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_BOTTOM]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType) || (BLOCK_END_STONE == neighborType)) ? 0.0f : 3.0f;
			saveBoxGeometry(boxIndex, type, dataVal, 0, (((!gPrint3D && (newHeight == 0)) || (type == neighborType)) ? DIR_BOTTOM_BIT : 0x0) | DIR_TOP_BIT, 4, 12, newHeight, 4, 4, 12);

			// side panels:
			// if adjacent to chorus plant or flower, certainly extend.
			// else extend randomly, based on random value: 50/50 there's a growth or not, and 50/50 the growth is a 6x6/8x8
			seedWithXYZ(boxIndex);
			int odds = rand()%256;
			// if all bump out bits are on (no bumps) or all are off (four bumps), get another value
			while (((odds & 0x55) == 0x55) || ((odds & 0x55) == 0x0) ) {
				odds = rand() % 256;
			}

			// Lo X
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType)) ? 0.0f : 3.0f;
			// four cases:
			// connected to neighbor, always the same
			// not connected, no bump out
			// not connected, 6x6 bump out
			// not connected, 8x8 bump out
			// not connected to neighbor? 0.0 new height means connected to neighbor
			if (newHeight == 3.0f) {
				newHeight = (odds & 0x1) ? 4.0f : ((odds & 0x2) ? 3.0f : 2.0f);
			}
			float newWidth = (newHeight == 2.0f) ? 1.0f : 0.0f;
			if (newWidth != 0.0f) {
				saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_HI_X_BIT, newHeight, 4, 4 + newWidth, 12 - newWidth, 4 + newWidth, 12 - newWidth);
				// set so that 8x8 is put beneath 6x6 - so confusing, but less code...
				newHeight = 4.0f;
			}
			// add 8x8 - if height is 4.0, then don't add side faces
			saveBoxGeometry(boxIndex, type, dataVal, 0,
				(((!gPrint3D && (newHeight == 0)) || (type == neighborType)) ? DIR_LO_X_BIT : 0x0) | DIR_HI_X_BIT |
				((newHeight == 4) ? (DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT) : 0x0),	// don't create sides if a 6x6 is generated
				newHeight, 4, 4, 12, 4, 12);

			// Hi X
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType)) ? 16.0f : 13.0f;
			if (newHeight == 13.0f) {
				newHeight = ((odds >> 2) & 0x1) ? 12.0f : (((odds >> 2) & 0x2) ? 13.0f : 14.0f);
			}
			newWidth = (newHeight == 14.0f) ? 1.0f : 0.0f;
			if (newWidth != 0.0f) {
				saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_LO_X_BIT, 12, newHeight, 4 + newWidth, 12 - newWidth, 4 + newWidth, 12 - newWidth);
				// set so that 8x8 is put beneath 6x6 - so confusing, but less code...
				newHeight = 12.0f;
			}
			// add 8x8 - if height is 4.0, then don't add side faces
			saveBoxGeometry(boxIndex, type, dataVal, 0,
				(((!gPrint3D && (newHeight == 16)) || (type == neighborType)) ? DIR_HI_X_BIT : 0x0) | DIR_LO_X_BIT |
				((newHeight == 12) ? (DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT) : 0x0),	// don't create sides if a 6x6 is generated
				12, newHeight, 4, 12, 4, 12);


			// Lo Z
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType)) ? 0.0f : 3.0f;
			// four cases:
			// connected to neighbor, always the same
			// not connected, no bump out
			// not connected, 6x6 bump out
			// not connected, 8x8 bump out
			// not connected to neighbor? 0.0 new height means connected to neighbor
			if (newHeight == 3.0f) {
				newHeight = ((odds >> 4) & 0x1) ? 4.0f : (((odds >> 4) & 0x2) ? 3.0f : 2.0f);
			}
			newWidth = (newHeight == 2.0f) ? 1.0f : 0.0f;
			if (newWidth != 0.0f) {
				saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_HI_Z_BIT, 4 + newWidth, 12 - newWidth, 4 + newWidth, 12 - newWidth, newHeight, 4);
				// set so that 8x8 is put beneath 6x6 - so confusing, but less code...
				newHeight = 4.0f;
			}
			// add 8x8 - if height is 4.0, then don't add side faces
			saveBoxGeometry(boxIndex, type, dataVal, 0,
				(((!gPrint3D && (newHeight == 0)) || (type == neighborType)) ? DIR_LO_Z_BIT : 0x0) | DIR_HI_Z_BIT |
				((newHeight == 4) ? (DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT) : 0x0),	// don't create sides if a 6x6 is generated
				4, 12, 4, 12, newHeight, 4);

			// Hi Z
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
			newHeight = ((type == neighborType) || (BLOCK_CHORUS_FLOWER == neighborType)) ? 16.0f : 13.0f;
			if (newHeight == 13.0f) {
				newHeight = ((odds >> 6) & 0x1) ? 12.0f : (((odds >> 6) & 0x2) ? 13.0f : 14.0f);
			}
			newWidth = (newHeight == 14.0f) ? 1.0f : 0.0f;
			if (newWidth != 0.0f) {
				saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_LO_Z_BIT, 4 + newWidth, 12 - newWidth, 4 + newWidth, 12 - newWidth, 12, newHeight);
				// set so that 8x8 is put beneath 6x6 - so confusing, but less code...
				newHeight = 12.0f;
			}
			// add 8x8 - if height is 4.0, then don't add side faces
			saveBoxGeometry(boxIndex, type, dataVal, 0,
				(((!gPrint3D && (newHeight == 16)) || (type == neighborType)) ? DIR_HI_Z_BIT : 0x0) | DIR_LO_Z_BIT |
				((newHeight == 12) ? (DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT) : 0x0),	// don't create sides if a 6x6 is generated
				4, 12, 4, 12, 12, newHeight);

		}
        break; // saveBillboardOrGeometry

    case BLOCK_STONE_PRESSURE_PLATE:						// saveBillboardOrGeometry
    case BLOCK_WOODEN_PRESSURE_PLATE:
	case BLOCK_SPRUCE_PRESSURE_PLATE:
	case BLOCK_BIRCH_PRESSURE_PLATE:
	case BLOCK_JUNGLE_PRESSURE_PLATE:
	case BLOCK_ACACIA_PRESSURE_PLATE:
	case BLOCK_DARK_OAK_PRESSURE_PLATE:
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
        saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 1, 15, 0, 1 + fatten, 1, 15);
        if ( dataVal & 0x1 )
        {
            // pressed, kick it down half a pixel
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, -0.5f/16.0f, 0.5/16.0f);
            transformVertices(8,mtx);
        }
        break; // saveBillboardOrGeometry

    case BLOCK_CARPET:						// saveBillboardOrGeometry
        // if printing and the location below the carpet is empty, then don't make carpet (it'll be too thin)
        if ( gPrint3D &&
            ( gBoxData[boxIndex-1].origType == BLOCK_AIR ) )
        {
            gMinorBlockCount--;
            return 0;
        }
        // yes, we fall through to wool here
    case BLOCK_WOOL:						// saveBillboardOrGeometry
        // use dataVal to retrieve location. These are scattered all over.
        swatchLoc = retrieveWoolSwatch(dataVal);
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0, 16, 0, 1, 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_OAK_WOOD_STAIRS:						// saveBillboardOrGeometry
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
	case BLOCK_PURPUR_STAIRS:
	case BLOCK_PRISMARINE_STAIRS:
	case BLOCK_PRISMARINE_BRICK_STAIRS:
	case BLOCK_DARK_PRISMARINE_STAIRS:

        // set up textures
        switch ( type )
        {
        default:
            assert(0);
        case BLOCK_OAK_WOOD_STAIRS:
        case BLOCK_SPRUCE_WOOD_STAIRS:
        case BLOCK_BIRCH_WOOD_STAIRS:
        case BLOCK_JUNGLE_WOOD_STAIRS:
        case BLOCK_ACACIA_WOOD_STAIRS:
        case BLOCK_DARK_OAK_WOOD_STAIRS:
        case BLOCK_QUARTZ_STAIRS:
        case BLOCK_PURPUR_STAIRS:
		case BLOCK_PRISMARINE_STAIRS:
		case BLOCK_PRISMARINE_BRICK_STAIRS:
		case BLOCK_DARK_PRISMARINE_STAIRS:
			topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            break;
        case BLOCK_RED_SANDSTONE_STAIRS:
            // for these stairs, top, sides, and bottom differ
            topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_RED_SANDSTONE_STAIRS].txrX, gBlockDefinitions[BLOCK_RED_SANDSTONE_STAIRS].txrY );
            sideSwatchLoc = SWATCH_INDEX( 14,13 );
            bottomSwatchLoc = SWATCH_INDEX( 5,8 );
            break;
		case BLOCK_COBBLESTONE_STAIRS:
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0x0:
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case BIT_16:	// stone stairs
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(1, 0);
				break;
			case BIT_32:	// granite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(8, 22);
				break;
			case BIT_32 | BIT_16:	// polished granite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(9, 22);
				break;
			}
			break;
		case BLOCK_BRICK_STAIRS:
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0x0:
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case BIT_16:	// smooth quartz stairs
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(7, 17);
				break;
			case BIT_32:	// diorite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(6, 22);
				break;
			case BIT_32 | BIT_16:	// polished diorite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(7, 22);
				break;
			}
			break;
		case BLOCK_STONE_BRICK_STAIRS:
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0x0:
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case BIT_16:	// end stone stairs
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(3, 24);
				break;
			case BIT_32:	// andesite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(4, 22);
				break;
			case BIT_32 | BIT_16:	// polished andesite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(5, 22);
				break;
			}
			break;
		case BLOCK_NETHER_BRICK_STAIRS:
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0x0:
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case BIT_16:	// red nether brick stairs
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(2, 26);
				break;
			case BIT_32:	// mossy stone
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(4, 6);
				break;
			case BIT_32 | BIT_16:	// mossy cobblestone
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(4, 2);
				break;
			}
			break;
		case BLOCK_SANDSTONE_STAIRS:
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0x0:
				// for these stairs, top, sides, and bottom differ
				topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY);
				sideSwatchLoc = SWATCH_INDEX(0, 12);
				bottomSwatchLoc = SWATCH_INDEX(0, 13);
				break;
			case BIT_16:	// smooth sandstone stairs
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(0, 11);
				break;
			case BIT_32:	// smooth red sandstone
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(12, 19);
				break;
			}
			break;
		}

        {
            unsigned int stepMask, origStepMask;
            stepMask = origStepMask = getStairMask(boxIndex, dataVal);
            int stepLevel = (dataVal & 0x4);

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

            // Other blocks will clear faces as needed, by the save system itself (i.e. if a small face is next to a full block,
            // it'll be removed if and only if we're rendering).
            // Save the 2x2 block, bottom or top.
            saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, miny, maxy, 0, 16);

            // Now create the 2x1 box, if found
            minx = minz = 0;
            maxx = maxz = 16;
            assert( (stepMask != 0) && (stepMask != 0xff) );
            bool outputStep = false;
            bool northSouth = false;
            if ( (stepMask & 0x3) == 0x3 )
            {
                // north step covered, goes east-west
                outputStep = true;
                maxz = 8;
                stepMask &= ~0x3;
            }
            else if ( (stepMask & 0xC) == 0xC )
            {
                // south step covered, goes east-west
                outputStep = true;
                minz = 8;
                stepMask &= ~0xC;
            }
            else if ( (stepMask & 0x5) == 0x5 )
            {
                // west step covered, goes north-south
                outputStep = true;
                maxx = 8;
                stepMask &= ~0x5;
                northSouth = true;
            }
            else if ( (stepMask & 0xA) == 0xA )
            {
                // east step covered, goes north-south
                outputStep = true;
                minx = 8;
                stepMask &= ~0xA;
                northSouth = true;
            }

            if ( outputStep )
            {
                if ( stepLevel )
                {
                    // lower 2x1 step (stairs is upside down)
                    miny = 0;
                    maxy = 8;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_TOP_BIT;
                }
                else
                {
                    // upper 2x1 step
                    miny = 8;
                    maxy = 16;
                    // if 3D printing, we output all faces of the small step, as this step needs to be watertight
                    faceMask = gPrint3D ? 0x0 : DIR_BOTTOM_BIT;
                }
                // Now, try the common case of two identical steps next to each other: if this really is a 2x1 block and nothing
                // else, no 1x1 is left, then check if the two adjoining steps touching the 1 sides (not 2) are *IDENTICAL* in
                // their step masks and levels. The short version: are their data values exactly the same as this block's?
                // If so, then mask out that 1x1 side face, but only if "export separate blocks" is off. This then covers the
				// easy and common case where two identical steps are next to each other and continuing.
                if ((stepMask == 0x0) && !(gOptions->pEFD->chkIndividualBlocks))
                {
					int neighborData;
					// OK, this is a 2x1 block. Does it go north-south or east-west?
                    if (northSouth) {
                        // Goes north-south.
                        // Check north neighbor.
                        neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
                        if (gBlockDefinitions[neighborType].flags & BLF_STAIRS) {
                            // northern neighbor is stairs
                            neighborData = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].data;
                            if (neighborData == dataVal) {
                                // the data values match - but, do the stair masks match?
                                if (getStairMask(boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z], neighborData) == origStepMask) {
                                    // all matches, so mask off the northern face
                                    faceMask |= DIR_LO_Z_BIT;
                                }
                            }
                        }
                        // Check south neighbor.
                        neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
                        if (gBlockDefinitions[neighborType].flags & BLF_STAIRS) {
                            // southern neighbor is stairs
                            neighborData = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].data;
                            if (neighborData == dataVal) {
                                // the data values match - but, do the stair masks match?
                                if (getStairMask(boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z], neighborData) == origStepMask) {
                                    // all matches, so mask off the southern face
                                    faceMask |= DIR_HI_Z_BIT;
                                }
                            }
                        }
                    }
                    else {
                        // Goes east-west
                        // Check west neighbor.
                        neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
                        if (gBlockDefinitions[neighborType].flags & BLF_STAIRS) {
                            // western neighbor is stairs
                            neighborData = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].data;
                            if (neighborData == dataVal) {
                                // the data values match - but, do the stair masks match?
                                if (getStairMask(boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X], neighborData) == origStepMask) {
                                    // all matches, so mask off the southern face
                                    faceMask |= DIR_LO_X_BIT;
                                }
                            }
                        }
                        // Check east neighbor.
                        neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
                        if (gBlockDefinitions[neighborType].flags & BLF_STAIRS) {
                            // eastern neighbor is stairs
                            neighborData = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].data;
                            if (neighborData == dataVal) {
                                // the data values match - but, do the stair masks match?
                                if (getStairMask(boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X], neighborData) == origStepMask) {
                                    // all matches, so mask off the southern face
                                    faceMask |= DIR_HI_X_BIT;
                                }
                            }
                        }
                    }
                }
                saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, faceMask, 0, minx, maxx, miny, maxy, minz, maxz);
            }

            // anything left? output that little 1x1 box
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
                saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, faceMask, 0, minx, maxx, miny, maxy, minz, maxz);
            }
        }

        break; // saveBillboardOrGeometry

    case BLOCK_STONE_SLAB:						// saveBillboardOrGeometry
    case BLOCK_WOODEN_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
	case BLOCK_PURPUR_SLAB:
	case BLOCK_ANDESITE_SLAB:
		switch (type)
		{
		default:
			assert(0);
		case BLOCK_STONE_SLAB:
			switch (dataVal & 0x7)
			{
			default:
				assert(0);
			case 0:
				// 
				topSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				sideSwatchLoc = SWATCH_INDEX(11, 23); // was (5, 0);
				break;
			case 1:
				// sandstone
				topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY);
				sideSwatchLoc = SWATCH_INDEX(0, 12);
				bottomSwatchLoc = SWATCH_INDEX(0, 13);
				break;
			case 2:
				// wooden
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY);
				break;
			case 3:
				// cobblestone
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_COBBLESTONE].txrY);
				break;
			case 4:
				// brick
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_BRICK].txrX, gBlockDefinitions[BLOCK_BRICK].txrY);
				break;
			case 5:
				// stone brick
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_STONE_BRICKS].txrX, gBlockDefinitions[BLOCK_STONE_BRICKS].txrY);
				break;
			case 6:
				// nether brick
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_NETHER_BRICK].txrX, gBlockDefinitions[BLOCK_NETHER_BRICK].txrY);
				break;
			case 7:
				// quartz with distinctive sides and bottom
				topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrX, gBlockDefinitions[BLOCK_QUARTZ_BLOCK].txrY);
				sideSwatchLoc = SWATCH_INDEX(6, 17);
				bottomSwatchLoc = SWATCH_INDEX(1, 17);
				break;
			}
			break;

		case BLOCK_WOODEN_SLAB:
			switch (dataVal & 0x7)
			{
			default: // normal log
				assert(0);
			case 0:
				// no change, default plank is fine
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case 1: // spruce (dark)
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(6, 12);
				break;
			case 2: // birch
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(6, 13);
				break;
			case 3: // jungle
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(7, 12);
				break;
			case 4: // acacia
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(0, 22);
				break;
			case 5: // dark oak
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(1, 22);
				break;
			}
			break;

		case BLOCK_RED_SANDSTONE_SLAB:
			// normal, for both dataVal == 0 and == 8 
			switch (dataVal & 0x7)
			{
			default: // red sandstone
				assert(0);	// falls through
			case 0: // red sandstone
				topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				sideSwatchLoc = SWATCH_INDEX(14, 13);
				bottomSwatchLoc = SWATCH_INDEX(5, 8);
				break;
			case 1: // cut_red_sandstone_slab
				topSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(12, 19);
				sideSwatchLoc = SWATCH_INDEX(10, 19);
				break;
			case 2: // smooth_red_sandstone_slab
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(12, 19);
				break;
			case 3: // cut_sandstone_slab
				topSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(0, 11);
				sideSwatchLoc = SWATCH_INDEX(6, 14);
				break;
			case 4: // smooth_sandstone_slab
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(0, 11);
				break;
			case 5: // granite_slab
				topSwatchLoc = sideSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(8, 22);
				break;
			case 6: // polished_granite_slab
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(9, 22);
				break;
			case 7: // smooth_quartz_slab
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(1, 17);
				break;
			}
			break;

		case BLOCK_PURPUR_SLAB:
			// some confusion in the docs here: https://minecraft.gamepedia.com/Java_Edition_data_values#Stone_Slabs
			switch (dataVal & 0x7)
			{
			default: // normal log
				assert(0);
			case 0: // purpur slab
			case 1: // purpur slab, just in case...
				topSwatchLoc = sideSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
				break;
			case 2: // prismarine 1.13 - stuffed in here
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(12, 22);
				break;
			case 3: // prismarine block 1.13
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(10, 22);
				break;
			case 4: // dark prismarine 1.13
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(11, 22);
				break;
			case 5:	// red nether brick
				topSwatchLoc = sideSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_RED_NETHER_BRICK].txrX, gBlockDefinitions[BLOCK_RED_NETHER_BRICK].txrY);
				break;
			case 6:	// mossy stone brick
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(4, 6);
				break;
			case 7:	// mossy cobblestone
				topSwatchLoc = sideSwatchLoc = bottomSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrY);
				break;
			}
			break;

		case BLOCK_ANDESITE_SLAB:
			// normal, for both dataVal == 0 and == 8 
			switch (dataVal & 0x7)
			{
			default:
				assert(0);	// falls through
			case 0: // andesite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(4, 22);
				break;
			case 1: // polished andesite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(5, 22);
				break;
			case 2: // diorite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(6, 22);
				break;
			case 3: // polished diorite
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(7, 22);
				break;
			case 4: // end stone brick
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_END_BRICKS].txrX, gBlockDefinitions[BLOCK_END_BRICKS].txrY);
				break;
			case 5: // (the new 1.14) stone slab - purely flat stone
				topSwatchLoc = bottomSwatchLoc = sideSwatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_STONE].txrX, gBlockDefinitions[BLOCK_STONE].txrY);
				break;
			}
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
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, miny, maxy, 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_STONE_BUTTON:						// saveBillboardOrGeometry
    case BLOCK_WOODEN_BUTTON:
	case BLOCK_SPRUCE_BUTTON:
	case BLOCK_BIRCH_BUTTON:
	case BLOCK_JUNGLE_BUTTON:
	case BLOCK_ACACIA_BUTTON:
	case BLOCK_DARK_OAK_BUTTON:
		// The bottom 3 bits is direction of button. Top bit is whether it's pressed.
        bitAdd = (dataVal & 0x8) ? 1.0f : 0.0f;
        miny = 6;
        maxy = 10;
        switch (dataVal & 0x7)
        {
        default:    // make compiler happy
            assert(0);  // yes, fall-through
        case 0: // above
            minx = 5;
            maxx = 12;
            miny = 14 + bitAdd;
            maxy = 16;
            minz = 6;
            maxz = 10;
            break;
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
        case 5: // below
            minx = 5;
            maxx = 12;
            miny = 0;
            maxy = 2 - bitAdd;
            minz = 6;
            maxz = 10;
            break;
        }
        // we *could* save one face of the stone button, the one facing the object, but don't:
        // the thing holding the stone button could be missing, due to export limits.
        saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, minx, maxx, miny, maxy, minz, maxz);
        break; // saveBillboardOrGeometry

    case BLOCK_WALL_SIGN:						// saveBillboardOrGeometry
		switch (dataVal & (BIT_32 | BIT_16 | BIT_8)) {
		default:
		case 0:
			// oak
			swatchLoc = SWATCH_INDEX(4, 0);
			break;
		case BIT_8:
			// spruce
			swatchLoc = SWATCH_INDEX(6, 12);
			break;
		case BIT_16:
			// birch
			swatchLoc = SWATCH_INDEX(6, 13);
			break;
		case (BIT_16|BIT_8):
			// jungle
			swatchLoc = SWATCH_INDEX(7, 12);
			break;
		case BIT_32:
			// acacia
			swatchLoc = SWATCH_INDEX(0, 22);
			break;
		case (BIT_32 | BIT_8):
			// dark oak
			swatchLoc = SWATCH_INDEX(1, 22);
			break;
		}
        switch (dataVal & 0x7)
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
        gUsingTransform = 1;
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0, 16, 0, 12, 7, 9);
        gUsingTransform = 0;
        // Scale sign down, move slightly away from wall
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        // this moves block up so that bottom of sign is at Y=0
        translateMtx(mtx, 0.0f, 0.5f, 0.0f);
        scaleMtx(mtx, 1.0f, 8.0f / 12.0f, gPrint3D ? 1.0f : 2.0f / 3.0f);
        // move a bit away from wall if we're not doing 3d printing
        translateMtx(mtx, 0.0f, -0.5f, 7.0f / 16.0f );
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        // undo translation
        translateFromOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 4.5f/16.0f, 0.0f);
        transformVertices(8,mtx);
        break; // saveBillboardOrGeometry

    case BLOCK_WALL_BANNER:						// saveBillboardOrGeometry
	case BLOCK_ORANGE_WALL_BANNER:
	case BLOCK_MAGENTA_WALL_BANNER:
	case BLOCK_LIGHT_BLUE_WALL_BANNER:
	case BLOCK_YELLOW_WALL_BANNER:
	case BLOCK_LIME_WALL_BANNER:
	case BLOCK_PINK_WALL_BANNER:
	case BLOCK_GRAY_WALL_BANNER:
	case BLOCK_LIGHT_GRAY_WALL_BANNER:
	case BLOCK_CYAN_WALL_BANNER:
	case BLOCK_PURPLE_WALL_BANNER:
	case BLOCK_BLUE_WALL_BANNER:
	case BLOCK_BROWN_WALL_BANNER:
	case BLOCK_GREEN_WALL_BANNER:
	case BLOCK_RED_WALL_BANNER:
	case BLOCK_BLACK_WALL_BANNER:
		switch (dataVal & 0x7)
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
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1, 15, 0, 14, 2, 5);
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, 0.0f, -4.0f/16.0f);
            transformVertices(8,mtx);

            // lower banner
			// was using MW_BANNER, but no longer needed.
			//if (type == BLOCK_WALL_BANNER) { // TODO: really, should make banners all the same...
			//	swatchLoc++;
			//}
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1, 15, 3, 16, 10, 13);
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
            saveBoxGeometry(boxIndex, BLOCK_PISTON, 0, 1, 0x0, 1, 15, 14, 16, 0, 2);
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, -2.0f/16.0f, 7.0f/16.0f);
            transformVertices(8,mtx);

            // upper banner
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1, 15, 0, 14, 2, 3);
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, 0.0f, -4.0f/16.0f);
            transformVertices(8,mtx);

            // lower banner
			// was using MW_BANNER, but no longer needed.
			//if (type == BLOCK_WALL_BANNER) { // TODO: really, should make banners all the same...
			//	swatchLoc++;
			//}
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1, 15, 3, 16, 12, 13);
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

        break; // saveBillboardOrGeometry

    case BLOCK_TRAPDOOR:						// saveBillboardOrGeometry
    case BLOCK_IRON_TRAPDOOR:
	case BLOCK_SPRUCE_TRAPDOOR:
	case BLOCK_BIRCH_TRAPDOOR:
	case BLOCK_JUNGLE_TRAPDOOR:
	case BLOCK_ACACIA_TRAPDOOR:
	case BLOCK_DARK_OAK_TRAPDOOR:
		// On second thought, in testing it worked fine.
        //if ( gPrint3D && !(dataVal & 0x4) )
        //{
        //	// if printing, and door is down, check if there's air below.
        //	// if so, don't print it! Too thin.
        //	if ( gBoxData[boxIndex-1].type == BLOCK_AIR)
        //		return 0;
        //}
        gUsingTransform = 1;
        saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 0, 16, 0, 3, 0, 16);
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
        break; // saveBillboardOrGeometry

	case BLOCK_SIGN_POST:						// saveBillboardOrGeometry
	case BLOCK_ACACIA_SIGN_POST:						// saveBillboardOrGeometry
		// set top to plank, bottom to log end
		if (type == BLOCK_SIGN_POST) {
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0:
				// oak
				topSwatchLoc = SWATCH_INDEX(4,0);
				bottomSwatchLoc = SWATCH_INDEX(5, 1);
				sideSwatchLoc = SWATCH_INDEX(4, 1);    // log
				break;

			case BIT_16:
				// spruce
				topSwatchLoc = SWATCH_INDEX(6, 12);
				bottomSwatchLoc = SWATCH_INDEX(11, 11);
				sideSwatchLoc = SWATCH_INDEX(4, 7);    // log
				break;

			case BIT_32:
				// birch
				topSwatchLoc = SWATCH_INDEX(6, 13);
				bottomSwatchLoc = SWATCH_INDEX(12, 11);
				sideSwatchLoc = SWATCH_INDEX(5,7);    // log
				break;

			case (BIT_32 | BIT_16):
				// jungle
				topSwatchLoc = SWATCH_INDEX(7, 12);
				bottomSwatchLoc = SWATCH_INDEX(13, 11);
				sideSwatchLoc = SWATCH_INDEX(9, 9);    // log
				break;
			}
		}
		else {
			// acacia, dark oak
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0:
				// acacia
				topSwatchLoc = SWATCH_INDEX(0, 22);
				bottomSwatchLoc = SWATCH_INDEX(13, 19);
				sideSwatchLoc = SWATCH_INDEX(5, 11);    // log
				break;

			case BIT_16:
				// dark oak
				topSwatchLoc = SWATCH_INDEX(1, 22);
				bottomSwatchLoc = SWATCH_INDEX(15, 19);
				sideSwatchLoc = SWATCH_INDEX(14, 19);    // log
				break;
			}
		}
        // sign is two parts:
        // bottom post is output first, which saves one translation
        gUsingTransform = 1;
        // if printing, seal the top of the post
        saveBoxMultitileGeometry(boxIndex, type, dataVal, bottomSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, gPrint3D ? 0x0 : DIR_TOP_BIT, 0, 7 - fatten, 9 + fatten, 0, 14, 7 - fatten, 9 + fatten);
        gUsingTransform = 0;
        // scale sign down, move slightly away from wall
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        // move bottom of sign to origin, for scaling
        translateMtx(mtx, 0.0f, 0.5f, 0.0f);
        // scale down in Y, and make post thinner if rendering
        scaleMtx(mtx, gPrint3D ? 1.0f : 2.0f / 3.0f, 16.0f / 24.0f, gPrint3D ? 1.0f : 2.0f / 3.0f);
        rotateMtx(mtx, 0.0f, (dataVal&0xf)*22.5f, 0.0f);
        // undo translation
        translateMtx(mtx, 0.0f, -0.5f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        // top is 12 high, extends 2 blocks above. Scale down by 16/24 and move up by 14/24
        gUsingTransform = 1;
		saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, topSwatchLoc, topSwatchLoc, 0, 0x0, 0, 0, 16, 0, 12, 7 - fatten, 9 + fatten);
        gUsingTransform = 0;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 0.5f, 0.0f);
        // scale down in Y, and make sign thinner only in Z direction
        scaleMtx(mtx, 1.0f, 16.0f / 24.0f, gPrint3D ? 1.0f : 2.0f / 3.0f);
        rotateMtx(mtx, 0.0f, (dataVal&0xf)*22.5f, 0.0f);
        // undo translation
        translateMtx(mtx, 0.0f, 14.0f / 24.0f - 0.5f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8, mtx);

        break; // saveBillboardOrGeometry

	case BLOCK_STANDING_BANNER:						// saveBillboardOrGeometry
	case BLOCK_ORANGE_BANNER:
	case BLOCK_MAGENTA_BANNER:
	case BLOCK_LIGHT_BLUE_BANNER:
	case BLOCK_YELLOW_BANNER:
	case BLOCK_LIME_BANNER:
	case BLOCK_PINK_BANNER:
	case BLOCK_GRAY_BANNER:
	case BLOCK_LIGHT_GRAY_BANNER:
	case BLOCK_CYAN_BANNER:
	case BLOCK_PURPLE_BANNER:
	case BLOCK_BLUE_BANNER:
	case BLOCK_BROWN_BANNER:
	case BLOCK_GREEN_BANNER:
	case BLOCK_RED_BANNER:
	case BLOCK_BLACK_BANNER:
		// Banner: five pieces: vertical sticks, horizontal brace, top half, bottom half
        gUsingTransform = 1;
        totalVertexCount = gModel.vertexCount;

        // first the pole, two parts, rotate to position, made from piston head texture
        saveBoxGeometry(boxIndex, BLOCK_PISTON_HEAD, 0, 1, DIR_HI_X_BIT, 0, 12, 11, 13, 3, 5);
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -4.0f/16.0f, 0.0f);
        rotateMtx(mtx, 0.0f, 0.0f, 90.0f);
        translateMtx(mtx, 0.0f, 12.0f/16.0f, 4.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        saveBoxGeometry(boxIndex, BLOCK_PISTON_HEAD, 0, 0, DIR_LO_X_BIT, 0, 16, 11, 13, 3, 5);
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -4.0f/16.0f, 0.0f);
        rotateMtx(mtx, 0.0f, 0.0f, 90.0f);
        translateMtx(mtx, 0.0f, 0.0f/16.0f, 4.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);

        // crossbar
        saveBoxGeometry(boxIndex, BLOCK_PISTON, 0, 0, 0x0, 1, 15, 14, 16, 0, 2);
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 14.0f/16.0f, 7.0f/16.0f);
        transformVertices(8,mtx);

        // upper banner
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, 0, 1, 15, 0, 14, 2, 3);
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 1.0f, -4.0f/16.0f);
        transformVertices(8,mtx);

        // lower banner
		// white banners have the "fancy" texture, the rest do not; but, let's get rid of MW_BANNER top and bottom
		//if (type == BLOCK_STANDING_BANNER) {
		//	swatchLoc++;
		//}
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_TOP_BIT, 0, 1, 15, 3, 16, 12, 13);
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

        break; // saveBillboardOrGeometry

    case BLOCK_WOODEN_DOOR:						// saveBillboardOrGeometry
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
        saveBoxMultitileGeometry(boxIndex, type, dataVal, bottomSwatchLoc, swatchLoc, bottomSwatchLoc, 1, 0x0, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 13 - fatten, 16);
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
        break; // saveBillboardOrGeometry

    case BLOCK_SNOW:						// saveBillboardOrGeometry
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
        saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 0, 16, 0, 2 * (1 + (float)(dataVal & 0x7)), 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_END_PORTAL_FRAME:						// saveBillboardOrGeometry
        topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        sideSwatchLoc = SWATCH_INDEX( 15,9 );
        bottomSwatchLoc = SWATCH_INDEX( 15,10 );
        // TODO: actually, we want to rotate 90, 180, 270 depending on 0x3 bits 0-3 of the portal. We cheat here and rotate by 90 or not.
        // This mostly matters for the eye of ender box on top, which is not symmetric
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, (dataVal & 0x1) ? REVOLVE_INDICES : 0, 0, 16, 0, 13, 0, 16);
        if (dataVal & 0x4) {
            // eye of ender
            topSwatchLoc = SWATCH_INDEX(14, 10);
            saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, topSwatchLoc, topSwatchLoc, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, (dataVal&0x1)?REVOLVE_INDICES:0, 4, 12, 13, 16, 4, 12);
        }
        break; // saveBillboardOrGeometry

    case BLOCK_ENCHANTING_TABLE:						// saveBillboardOrGeometry
        topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
        sideSwatchLoc = SWATCH_INDEX(6,11);
        bottomSwatchLoc = SWATCH_INDEX(7,11);
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, 0, 12, 0, 16);
        break; // saveBillboardOrGeometry

        // top, sides, and bottom, and don't stretch the sides if output here
    case BLOCK_CAKE:						// saveBillboardOrGeometry
        swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = SWATCH_INDEX( 12,7 );
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = dataVal ? SWATCH_INDEX( 11,7 ) : SWATCH_INDEX( 10,7 );
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = 
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX( 10,7 );
        saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 1, 0x0, 0, 0, 1 + (float)(dataVal & 0x7) * 2, 15, 0, 8, 1, 15);
        break; // saveBillboardOrGeometry

    case BLOCK_FARMLAND:						// saveBillboardOrGeometry
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
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, 0, 15, 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_GRASS_PATH:						// saveBillboardOrGeometry
        topSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        sideSwatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX+1, gBlockDefinitions[type].txrY );
        bottomSwatchLoc = SWATCH_INDEX( 2,0 );  // dirt
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, 0, 15, 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_FENCE_GATE:						// saveBillboardOrGeometry
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
		gUsingTransform = 1;
		totalVertexCount = gModel.vertexCount;
		// Check if open
        if ( dataVal & 0x4 )
        {
            // open
            if ( dataVal & 0x1 )
            {
                // open west/east
                saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 7, 9, 5, 16, 0, 2 + fatten);
                saveBoxGeometry(boxIndex, type, dataVal, 0, 0x0, 7, 9, 5, 16, 14 - fatten, 16);
                if ( dataVal & 0x2 )
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16,  6, 9,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16, 12,15,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16,  6, 9, 14-fatten,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT), 9,16, 12,15, 14-fatten,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14,16, 9,12,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14,16, 9,12, 14-fatten,16 );
                }
                else
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7,  6, 9,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7, 12,15,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7,  6, 9, 14-fatten,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_X_BIT), 0,7, 12,15, 14-fatten,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 0,2, 9,12,  0, 2+fatten );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 0,2, 9,12, 14-fatten,16 );
                }
            }
            else
            {
                // open north/south - hinge posts:
                saveBoxGeometry( boxIndex, type, dataVal, 1, 0x0,  0, 2+fatten, 5,16, 7,9);
                saveBoxGeometry( boxIndex, type, dataVal, 0, 0x0, 14-fatten,16, 5,16, 7,9);
                if ( dataVal & 0x2 )	// north
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 0, 2+fatten,  6, 9,  0,7 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 0, 2+fatten, 12,15,  0,7 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 14-fatten,16,  6, 9, 0,7 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_HI_Z_BIT), 14-fatten,16, 12,15, 0,7 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT),  0, 2+fatten, 9,12, 0,2 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14-fatten,16, 9,12, 0,2 );
                }
                else
                {
                    // side pieces
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT),  0, 2+fatten,  6, 9, 9,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT),  0, 2+fatten, 12,15, 9,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT), 14-fatten,16,  6, 9, 9,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT), 14-fatten,16, 12,15, 9,16 );
                    // gate center
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT),  0, 2+fatten, 9,12, 14,16 );
                    saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 14-fatten,16, 9,12, 14,16 );
                }
            }
        }
        else
        {
            // closed
            if ( dataVal & 0x1 )
            {
                // open west/east
                saveBoxGeometry( boxIndex, type, dataVal, 1, 0x0, 7-fatten,9+fatten, 5,16,  0, 2);
                saveBoxGeometry( boxIndex, type, dataVal, 0, 0x0, 7-fatten,9+fatten, 5,16, 14,16);
                // side pieces
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT|DIR_HI_Z_BIT), 7-fatten,9+fatten,  6, 9, 2,14 );
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_Z_BIT|DIR_HI_Z_BIT), 7-fatten,9+fatten, 12,15, 2,14 );
                // gate center
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 7-fatten,9+fatten, 9,12, 6,10 );
            }
            else
            {
                // open north/south
                saveBoxGeometry( boxIndex, type, dataVal, 1, 0x0,  0, 2, 5,16, 7-fatten,9+fatten);
                saveBoxGeometry( boxIndex, type, dataVal, 0, 0x0, 14,16, 5,16, 7-fatten,9+fatten);
                // side pieces
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT|DIR_HI_X_BIT), 2,14,  6, 9,  7-fatten,9+fatten );
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_LO_X_BIT|DIR_HI_X_BIT), 2,14, 12,15,  7-fatten,9+fatten );
                // gate center
                saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 6,10, 9,12,  7-fatten,9+fatten);
            }
        }

		totalVertexCount = gModel.vertexCount - totalVertexCount;
		{
			// "in_wall" property on? (1.13)
			bool shiftGate = (dataVal & 0x20) ? true : false;
			if (gMcVersion <= 12) {
				// have to test if there are walls around to lower the gate - there are only two types, cobble and mossy cobble, in 1.12
				if (dataVal & 0x1)
				{
					// open west/east
					shiftGate = (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType == BLOCK_COBBLESTONE_WALL) || (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType == BLOCK_COBBLESTONE_WALL);
				}
				else {
					// open north/south
					shiftGate = (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType == BLOCK_COBBLESTONE_WALL) || (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType == BLOCK_COBBLESTONE_WALL);
				}
			}
			if (shiftGate)
			{
				// shift gate down
				identityMtx(mtx);
				translateMtx(mtx, 0.0f, -3.0f / 16.0f, 0.0f);
				transformVertices(totalVertexCount, mtx);
			}
		}
		gUsingTransform = 0;

        break; // saveBillboardOrGeometry

    case BLOCK_COCOA_PLANT:						// saveBillboardOrGeometry
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        shiftVal = 0;
        gUsingTransform = 1;
        switch ( (dataVal >> 2) & 0x3 )
        {
        case 0:
            // small
            swatchLoc += 2;
            // note all six sides are used, but with different texture coordinates
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0, 11, 15, 7, 12, 11, 15);
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 1, 5, 7, 12, 1, 5);
            // top and bottom
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 4, 0, 4, 0, 4);
            shiftVal = 5;
            break;
        case 1:
            // medium
            swatchLoc++;
            // note all six sides are used, but with different texture coordinates
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0, 9, 15, 5, 12, 9, 15);
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 1, 7, 5, 12, 1, 7);
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 6, 0, 6, 0, 6);
            shiftVal = 4;
            break;
        case 2:
        default:
            // large
            // already right swatch
            // note all six sides are used, but with different texture coordinates
            // sides:
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0, 7, 15, 3, 12, 7, 15);
            // it should really be 3,12, but Minecraft has a bug where their sides are wrong and are 3,10
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 1, 9, 3, 10, 1, 9);
            // top and bottom:
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 7, 0, 7, 0, 7);
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
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
        {
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 12, 16, 12, 16, 8, 8);
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
            transformVertices((int)bitAdd,mtx);
        }
        gUsingTransform = 0;
        break; // saveBillboardOrGeometry

    case BLOCK_CAULDRON:						// saveBillboardOrGeometry
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // if printing, we seal the cauldron against the water height (possibly empty), else for rendering we make the walls go to the bottom
        waterHeight = gPrint3D ? (6+(float)dataVal*3) : 6;
        // outsides - if printing, just go down to bottom, no real feet, else kick it up
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 1, DIR_TOP_BIT | DIR_BOTTOM_BIT, 0, 0, 16, gPrint3D ? 0.0f : 3.0f, 16, 0, 16);
		// bottom
		saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 1, swatchLoc + 16, swatchLoc + 1, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_TOP_BIT, 0,
			0, 16, gPrint3D ? 0.0f : 3.0f, 16, 0, 16);
		// top as 4 small faces, and corresponding faces inside
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            14, 16, waterHeight, 16, 2, 14 );	// top and lo_x
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            0, 2, waterHeight, 16, 2, 14 );	// top and hi_x
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, 0,
            2, 14, waterHeight, 16, 14, 16 );	// top and lo_z
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0,
            2, 14, waterHeight, 16, 0, 2 );	// top and hi_z
        // four tiny corners at top, just tops
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            0, 2, 16, 16, 0, 2 );	// top
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            0, 2, 16, 16, 14, 16 );	// top
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            14, 16, 16, 16, 0, 2 );	// top
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
            14, 16, 16, 16, 14, 16 );	// top

        // inside bottom
        // outside bottom
        if ( !gPrint3D || (dataVal == 0x0) )
        {
            // show smaller inside bottom if cauldron is empty
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 1, swatchLoc + 16, swatchLoc + 1, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT, 0,
                2, 14, 3, 6, 2, 14 );

            if ( !gPrint3D ) {
                // Good times: make four feet that are 4x4 wide with a notch cut out, so really 2x2 each, with proper sides and bottoms
				for (x = 0; x < 2; x++) {
					for (z = 0; z < 2; z++) {
						saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 1, DIR_TOP_BIT | (x ? DIR_LO_X_BIT : DIR_HI_X_BIT) | (z ? DIR_LO_Z_BIT : DIR_HI_Z_BIT), 0, 0+x*14, 2+x*14, 0, 3, 0+z*14, 2 + z * 14);
						saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 1, DIR_TOP_BIT | (x ? DIR_HI_X_BIT : DIR_LO_X_BIT), 0, 2 + x * 10, 4 + x * 10, 0, 3, 0 + z * 14, 2 + z * 14);
						saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 16, swatchLoc + 17, 1, DIR_TOP_BIT | (z ? DIR_HI_Z_BIT : DIR_LO_Z_BIT), 0, 0 + x * 14, 2 + x * 14, 0, 3, 2 + z * 10, 4 + z * 10);
					}
				}
			}
        }

        if ( dataVal > 0 && dataVal < 4 )
        {
            // water level
            saveBoxGeometry( boxIndex, BLOCK_STATIONARY_WATER, 0 /* water data value is always 0 */, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 
                2, 14, 6 + (float)dataVal * 3, 6 + (float)dataVal * 3, 2, 14);
        }

        break; // saveBillboardOrGeometry

    case BLOCK_DRAGON_EGG:						// saveBillboardOrGeometry
        // top to bottom
        saveBoxGeometry( boxIndex, type, dataVal, 1, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 6,10, 15,16, 6,10);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 5,11, 14,15, 5,11);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 4,12, 13,14, 4,12);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 3,13, 11,13, 3,13);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 2,14,  8,11, 2,14);
        saveBoxGeometry( boxIndex, type, dataVal, 0, 0x0, 1,15, 3,8, 1,15);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 2,14, 1,3, 2,14);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 5,11, 0,1, 5,11);
        break; // saveBillboardOrGeometry

    case BLOCK_ANVIL:						// saveBillboardOrGeometry
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
        saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0,
            3,13, 10,16, 0,16 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : (DIR_BOTTOM_BIT|DIR_TOP_BIT), 6,10, 5,10, 4,12);
        saveBoxGeometry( boxIndex, type, dataVal, 0, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 4,12, 4,5, 3,13);
        saveBoxGeometry( boxIndex, type, dataVal, 0, 0x0, 2,14, 0,4, 2,14);
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
        break; // saveBillboardOrGeometry

    case BLOCK_FLOWER_POT:						// saveBillboardOrGeometry
        // if we output a cactus, we don't need to output the faces inside the flowerpot
        useInsidesAndBottom = 1;
        firstFace = 1;

        // old data values mixed with new. This works because 0 means empty under both systems, and the high bits (0xff00) are set for all new-style flowers,
        // so the old data values 1-13 don't overlap the new ones, which are 16 and higher.
        // See nbt for the loop minecraft:red_flower etc. that sets these values.
        /*
        Flower Pot Contents
        Contents		Item			Data
        empty			air				0
        poppy			red_flower		0
        blue orchid		red_flower		1
        allium			red_flower		2
        houstonia		red_flower		3
        red tulip		red_flower		4
        orange tulip	red_flower		5
        white tulip		red_flower		6
        pink tulip		red_flower		7
        oxeye daisy		red_flower		8
        dandelion		yellow_flower	0
        red mushroom	red_mushroom	0
        brown mushroom	brown_mushroom	0
        oak sapling		sapling			0
        spruce sapling	sapling			1
        birch sapling	sapling			2
        jungle sapling	sapling			3
        acacia sapling	sapling			4
        dark oak sapling	sapling		5
        dead bush		deadbush		0
        fern			tallgrass		2
        cactus			cactus			0
		bamboo - new only
        */

        if (gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES))
        {
            // printing or not using images: only geometry we can add is a cactus (bamboo is too thin to print)
            if ((dataVal == 9) || (dataVal == CACTUS_FIELD))
            {
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_CACTUS].txrX, gBlockDefinitions[BLOCK_CACTUS].txrY );
                saveBoxMultitileGeometry(boxIndex, BLOCK_CACTUS, dataVal, swatchLoc + 1, swatchLoc + 1, swatchLoc + 1, firstFace, gPrint3D ? 0x0 : DIR_BOTTOM_BIT, 0, 6, 10, 6, 16, 6, 10);
                firstFace = 0;
                useInsidesAndBottom = 0;
            }
        }
        else
        {
            int addBillboard = 1;
            int typeB = 0;
            int dataValB = 0;
            int billboardType = BB_FULL_CROSS;
            float scale = 0.75f;	// hmmm, I used to think all plants were not scaled down - they are.
            // rendering
            // old data values mixed with new. This works because 0 means empty under both systems, and the high bits (0xff00) are set for all new-style flowers
            switch (dataVal & 0xff)
            {
            case 0:
                // nothing!
                addBillboard = 0;
                break;
            case 1:
            case RED_FLOWER_FIELD:
                // poppy / was: rose
                typeB = BLOCK_POPPY;
                break;
            case 2:
            case YELLOW_FLOWER_FIELD:
                // dandelion
                typeB = BLOCK_DANDELION;
                break;
            case 3:
            case SAPLING_FIELD:
                // sapling (oak)
                typeB = BLOCK_SAPLING;
                //scale = 0.75f;
                break;
            case 4:
            case SAPLING_FIELD|0x1:
                // spruce sapling flower - todo ACACIA uses this, maybe uses tile entity?
                typeB = BLOCK_SAPLING;
                dataValB = 1;
                //scale = 0.75f;
                break;
            case 5:
            case SAPLING_FIELD | 0x2:
                // birch sapling - todo DARK OAK uses this, maybe uses tile entity?
                typeB = BLOCK_SAPLING;
                dataValB = 2;
                //scale = 0.75f;
                break;
            case 6:
            case SAPLING_FIELD | 0x3:
                // jungle sapling
                typeB = BLOCK_SAPLING;
                dataValB = 3;
                //scale = 0.75f;
                break;
            case 7:
            case RED_MUSHROOM_FIELD:
                // red mushroom
                typeB = BLOCK_RED_MUSHROOM;
                break;
            case 8:
            case BROWN_MUSHROOM_FIELD:
                // brown mushroom
                typeB = BLOCK_BROWN_MUSHROOM;
                break;
            case 9:
            case CACTUS_FIELD:
                // cactus (note we're definitely not 3D printing, so no face test)
				addBillboard = 0;
				swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_CACTUS].txrX, gBlockDefinitions[BLOCK_CACTUS].txrY );
                // interestingly enough, the tiny cactus is actually all made out of the side tiles, not top and bottom
                saveBoxMultitileGeometry(boxIndex, BLOCK_CACTUS, dataVal, swatchLoc + 1, swatchLoc + 1, swatchLoc + 1, firstFace, DIR_BOTTOM_BIT, 0, 6, 10, 6, 16, 6, 10);
                firstFace = 0;
 				// cactus fills the pot, so these extra polygons are not needed
                useInsidesAndBottom = 0;
                break;
            case 10:
            case DEADBUSH_FIELD:
                // dead bush
                typeB = BLOCK_GRASS;
                //scale = 0.75f;
                break;
            case 11:
            case TALLGRASS_FIELD | 0x2:
                // fern
                typeB = BLOCK_GRASS;
                dataValB = 2;
                //scale = 0.75f;
                break;

			case 12:
			case SAPLING_FIELD | 0x4:
                // acacia sapling
                typeB = BLOCK_SAPLING;
                dataValB = 4;
                //scale = 0.75f;
                break;
			case 13:
			case SAPLING_FIELD | 0x5:
                // dark oak sapling
                typeB = BLOCK_SAPLING;
                dataValB = 5;
                //scale = 0.75f;
                break;
            case RED_FLOWER_FIELD | 0x1:
            case RED_FLOWER_FIELD | 0x2:
            case RED_FLOWER_FIELD | 0x3:
            case RED_FLOWER_FIELD | 0x4:
            case RED_FLOWER_FIELD | 0x5:
            case RED_FLOWER_FIELD | 0x6:
            case RED_FLOWER_FIELD | 0x7:
			case RED_FLOWER_FIELD | 0x8:
			case RED_FLOWER_FIELD | 0x9:
			case RED_FLOWER_FIELD | 0xA:
			case RED_FLOWER_FIELD | 0xB:
				// blue orchid through wither rose
                typeB = BLOCK_POPPY;
                dataValB = dataVal & 0xf;
                break;
			case BAMBOO_FIELD:
				// bamboo
				addBillboard = 0;
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_BAMBOO].txrX, gBlockDefinitions[BLOCK_BAMBOO].txrY);
				totalVertexCount = gModel.vertexCount;
				gUsingTransform = 1;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 0, 2, 0, 16, 0, 2);
				// other sides - UV has to be low
				saveBoxReuseGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 14, 16, 0, 16, 14, 16);
				// top, not bottom (which appears to never be visible):
				saveBoxReuseGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 14, 16, 14, 16, 14, 16);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				firstFace = 0;
				identityMtx(mtx);
				//translateToOriginMtx(mtx, boxIndex);
				translateMtx(mtx, 7.0f/16.0f, 4.0f / 16.0f, 7.0f / 16.0f);
				//translateFromOriginMtx(mtx, boxIndex);
				transformVertices(totalVertexCount, mtx);
				// leaf
				swatchLoc = SWATCH_INDEX(7, 37);
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 1, 7, 11, 15, 8, 8);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				rotateMtx(mtx, 0.0f, 180.0f, 0.0f);
				translateMtx(mtx, 0.0f, 2.0f / 16.0f, 0.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(totalVertexCount, mtx);
				gUsingTransform = 0;
				break;
			default:
                // Debug world gives: assert(0); - a value here is "fine", and should be ignored
                addBillboard = 0;
                break;
            }

            if ( addBillboard )
            {
                totalVertexCount = gModel.vertexCount;
                gUsingTransform = 1;
				// "true" at the end means "don't wobble these billboards" as they're inside flowerpots
                saveBillboardFacesExtraData( boxIndex, typeB, billboardType, dataValB, firstFace, true );
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

        // the four outside walls of the flower pot
        saveBoxGeometry( boxIndex, type, dataVal, firstFace, DIR_TOP_BIT|DIR_BOTTOM_BIT, 5,11, 0,6, 5,11 );

        // 4 top edge faces and insides, if visible
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_LO_X_BIT), 
            10,11,  4,6,   6,10 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_HI_X_BIT),
            5,6,    4,6,   6,10 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|(useInsidesAndBottom?0x0:DIR_LO_Z_BIT),
            6,10,   4,6,  10,11 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|(useInsidesAndBottom?0x0:DIR_HI_Z_BIT),
            6,10,   4,6,   5,6 );
        // top corners
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            5,6,  4,6,  5,6 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            5,6,  4,6,  10,11 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            10,11,  4,6,  5,6 );
        saveBoxGeometry( boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 
            10,11,  4,6, 10,11 );

        // inside bottom
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_DIRT].txrX, gBlockDefinitions[BLOCK_DIRT].txrY );
        if ( useInsidesAndBottom )
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 6,10,  0,4,  6,10);
        // outside bottom - in theory never seen, so we make it dirt, since the flowerpot texture itself has a hole in it at these coordinates
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0,  5,11,  0,4,  5,11);

        break; // saveBillboardOrGeometry

    case BLOCK_HEAD:	// definitely wrong for heads, TODO - have tile entity, but now need all the dratted head textures... Pumpkin half-sized for now.
        // most arcane storage ever, as I need to fit everything in 8 bits in my extended data value field.
        // If dataVal is 2-5, rotation is not used (head is on a wall) and high bit of head type is off, else it is put on.
        // 7 | 654 | 3210
        // bit 7 - is bottom four bits 3210 the rotation on floor? If off, put on wall.
        // bits 654 - the head. Hopefully Minecraft won't add more than 8 heads...
        // bits 3210 - depends on bit 7; rotation if on floor, or on which wall (2-5)

        // make pumpkin, scale it down, and prepare to translate and rotate it, etc.
        gUsingTransform = 1;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX(7, 7);	// front face, as a start
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX(6, 7);
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = SWATCH_INDEX(6, 6);
        swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX(6, 6);
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX(6, 7);
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX(6, 7);
        saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 1, 0x0, 0x0, 0,
            0, 16, 0, 16, 0, 16);

        gUsingTransform = 0;
        // scale it down, move to wall, then
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -0.5f, 0.0f);
        scaleMtx(mtx, 0.5f, 0.5f, 0.5f);

        // is head on floor or wall?
        if (dataVal & 0x80) {
            // on floor
            // TODO head type
            rotateMtx(mtx, 0.0f, 22.5f*(dataVal & 0xf), 0.0f);
        }
        else {
            // on wall
            // TODO head type

            translateMtx(mtx, 0.0f, 0.0f, 0.25f);
            yrot = 0.0f;
            switch (dataVal & 0xf) {
            default:
                assert(0);
            case 1:
                // unused, but it's in some worlds, so this case ignores it instead of asserting.
                break;
            case 2: // north
                // already set above: yrot = 0.0f;
                break;
            case 3: // south
                yrot = 180.0f;
                break;
            case 4: // east
                yrot = 270.0f;
                break;
			case 0:	// also seen as west
			case 5: // west
                yrot = 90.0f;
                break;
            }
            // rotate the head by 90's
            rotateMtx(mtx, 0.0f, yrot, 0.0f);
        }
        // undo global translation
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8, mtx);

        break; // saveBillboardOrGeometry

    case BLOCK_BED:						// saveBillboardOrGeometry
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
            saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_BOTTOM_BIT|(gPrint3D?0x0:DIR_LO_X_BIT), FLIP_Z_FACE_VERTICALLY, 0,
                0,16, 0,9, 0,16 );
        }
        else
        {
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX( 6,9 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX( 6,9 );
            swatchLocSet[DIRECTION_BLOCK_TOP] = SWATCH_INDEX( 6,8 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX( 5,9 );
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX( 8,9 );  // should normally get removed by neighbor tester code
            saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_BOTTOM_BIT|(gPrint3D?0x0:DIR_HI_X_BIT), FLIP_Z_FACE_VERTICALLY, 0,
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
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY );
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT, 0, 0,16,
            (gPrint3D ? 0.0f : 3.0f), (gPrint3D ? 0.0f : 3.0f), 0, 16);
        break; // saveBillboardOrGeometry

    case BLOCK_CACTUS:						// saveBillboardOrGeometry
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
        if ( gPrint3D || !(gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
        {
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc+1, swatchLoc+2, 1, faceMask, 0, 1,15, 0,16, 1,15 );
        }
        else
        {
            // could use billboard, but this gives two-sided faces, and Minecraft actually uses one-sided faces for thorns
            //saveBillboardFaces( boxIndex, type, dataVal, BB_GRID );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc+1, swatchLoc+2, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_TOP_BIT|DIR_BOTTOM_BIT, 0, 0,16, 0,16, 1,15 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc+1, swatchLoc+2, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|DIR_BOTTOM_BIT, 0, 1,15, 0,16, 0,16 );
            if ( faceMask != (DIR_TOP_BIT|DIR_BOTTOM_BIT))
            {
                // top and bottom
                saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc+1, swatchLoc+2, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|faceMask, 0, 1,15, 0,16, 1,15 );
            }
        }
        break; // saveBillboardOrGeometry

    case BLOCK_CHEST:						// saveBillboardOrGeometry
    case BLOCK_TRAPPED_CHEST:
        // Decide if chest is a left side, right side, or single. Get angle for rotation.
        // If it's a right-side chest or a single, make the latch (left-side chests don't get the latch, so that the right side makes just one latch).
        chestType = 0;	// left is 1, right is 2
        angle = 0;
        switch (dataVal & 0x7)
        {
        case 0:
            // old bad data, so this code matches it.
            // In reality, in 1.8 such chests just disappear! The data's still
            // in the world, but you can't see or interact with the chests.
        case 2: // facing north
            // is neighbor to west also a chest?
            if (gBoxData[boxIndex - gBoxSizeYZ].origType == type)
            {
                chestType = 1;
            }
            else if (gBoxData[boxIndex + gBoxSizeYZ].origType == type)
            {
                chestType = 2;
            }
            angle = 180;
            break;
        case 3: // facing south
            // is neighbor to east also a chest?
            if (gBoxData[boxIndex + gBoxSizeYZ].origType == type)
            {
                chestType = 1;
            }
            // else, is neighbor to west also a chest?
            else if (gBoxData[boxIndex - gBoxSizeYZ].origType == type)
            {
                chestType = 2;
            }
            angle = 0;
            break;
        case 4: // facing west
            if (gBoxData[boxIndex - gBoxSize[Y]].origType == type)
            {
                chestType = 2;
            }
            else if (gBoxData[boxIndex + gBoxSize[Y]].origType == type)
            {
                chestType = 1;
            }
            angle = 90;
            break;
        case 5: // facing east
            if (gBoxData[boxIndex + gBoxSize[Y]].origType == type)
            {
                chestType = 2;
            }
            else if (gBoxData[boxIndex - gBoxSize[Y]].origType == type)
            {
                chestType = 1;
            }
            angle = 270;
            break;
        default:
            assert(0);
            break;
        }

		// that detective work's nice for older chests, but 1.13 on have a nice "single" property, which we use here to override things.
		if ((dataVal >> 3) > 0x0) {
			// has "single" property
			chestType = (dataVal >> 3) - 1;
		}

        gUsingTransform = 1;

        // create latch, move to place, then create chest, rotate all to place
        totalVertexCount = gModel.vertexCount;
        // create latch only if a right or a single
        if (chestType != 1) {
            swatchLoc = SWATCH_INDEX(9, 13);
            // note all six sides are used, but with different texture coordinates
            // front is LO_X - 0,1 is thickness, 11,15 is height, 1,3 is width
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0, 0, 1, 11, 15, 1, 3);
            // back - have to go high with coordinates
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 1, 11, 15, 10, 12);
            // left side - have to go high with coordinates
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, 0x0, 15, 16, 11, 15, 15, 16);
            // right side
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 3, 4, 11, 15, 3, 4);
            // top
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 1, 3, 1, 3, 0, 1);
            // bottom - probably needs to be mirrored?
            saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 3, 5, 3, 5, 0, 1);
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, -4.0f / 16.0f, (chestType == 0) ? 6.0f / 16.0f : -2.0f / 16.0f);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, -90.0f, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(8, mtx);
        }

        // chest itself - facing south
        // get tiles based on left/right/single
        faceMask = 0x0;
        switch (chestType & 0x3) {
        case 0:
            swatchLoc = SWATCH_INDEX(9, 1);
            swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLoc;
            swatchLocSet[DIRECTION_BLOCK_BOTTOM] = swatchLoc;
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = swatchLoc + 1;
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = swatchLoc + 1;
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = swatchLoc + 1;
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc + 2;	// front
            faceMask = 0x0;
            break;
        case 1:	// left
            swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLocSet[DIRECTION_BLOCK_BOTTOM] = SWATCH_INDEX(9, 14);
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX(10, 1);
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX(10, 1);	// not actually used
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX(10, 3);
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX(9, 2);	// front
            faceMask = DIR_HI_X_BIT;
            break;
        case 2:	// right
            swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLocSet[DIRECTION_BLOCK_BOTTOM] = SWATCH_INDEX(10, 14);
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = SWATCH_INDEX(10, 1);	// not actually used
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = SWATCH_INDEX(10, 1);
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = SWATCH_INDEX(9, 3);
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = SWATCH_INDEX(10, 2);	// front
            faceMask = DIR_LO_X_BIT;
            break;
        }
        // latch only if single or right
        saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, (chestType == 1), faceMask, 0, 0, (chestType != 2) ? 1.0f : 0.0f, (chestType != 1) ? 15.0f : 16.0f, 0, 14, 1, 15);
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        gUsingTransform = 0;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount, mtx);
        break; // saveBillboardOrGeometry

    case BLOCK_ENDER_CHEST:						// saveBillboardOrGeometry
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] = swatchLoc;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = swatchLoc+2;	// front
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = swatchLoc+1;
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = swatchLoc+1;
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc+1;
        gUsingTransform = 1;

        // create latch, move to place, then create chest, rotate all to place
        totalVertexCount = gModel.vertexCount;
        swatchLoc = SWATCH_INDEX(9,13);
        // note all six sides are used, but with different texture coordinates
        // front is LO_X - 0,1 is thickness, 11,15 is height, 1,3 is width
        saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0, 0,1, 11,15, 1,3);
        // back - have to go high with coordinates
        saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0,1, 11,15, 10,12);
        // left side - have to go high with coordinates
        saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, 0x0, 15,16, 11,15, 15,16);
        // right side
        saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 3,4, 11,15, 3,4);
        // top
        saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 1,3, 1,3, 0,1);
        // bottom - probably needs to be mirrored?
        saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 3,5, 3,5, 0,1);
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, -4.0f / 16.0f, 6.0f / 16.0f);
        transformVertices(8, mtx);

        // chest itself - easy!
        saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0, 0x0, 0, 0, 1,15, 0,14, 1,15);
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        gUsingTransform = 0;
        switch ( dataVal & 0x7)
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
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount, mtx);
        break; // saveBillboardOrGeometry

    case BLOCK_REDSTONE_REPEATER_OFF:						// saveBillboardOrGeometry
    case BLOCK_REDSTONE_REPEATER_ON:
        swatchLoc = SWATCH_INDEX( 3, 8 + (type == BLOCK_REDSTONE_REPEATER_ON) );
        angle = 90.0f*(float)(dataVal&0x3);
        gUsingTransform = 1;
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0,16, 14,16, 0,16 );
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, -14.0f/16.0f, 0.0f );
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(8,mtx);
        if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
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
        break; // saveBillboardOrGeometry

    case BLOCK_REDSTONE_COMPARATOR:						// saveBillboardOrGeometry
    case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
        // in 1.5, comparator active is used for top bit
        // in 1.6, comparator active is not used, it depends on dataVal
        {
            int in_powered = ((type == BLOCK_REDSTONE_COMPARATOR_DEPRECATED) || (dataVal >= 8));
            int out_powered = (dataVal & 0x4) == 0x4;
            swatchLoc = SWATCH_INDEX( 14 + in_powered,14 );
            angle = 90.0f*(float)(dataVal&0x3);
            gUsingTransform = 1;
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0, 0,16, 14,16, 0,16 );
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            translateMtx(mtx, 0.0f, -14.0f/16.0f, 0.0f );
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(8,mtx);
            if ( !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
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

                // the inner one moved 1 more down if not powered
                totalVertexCount = gModel.vertexCount;
                saveBillboardFacesExtraData( boxIndex, out_powered?BLOCK_REDSTONE_TORCH_ON:BLOCK_REDSTONE_TORCH_OFF, BB_TORCH, 0x5, 0 );
                totalVertexCount = gModel.vertexCount - totalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                translateMtx(mtx, 0.0f, out_powered ? -5.0f/16.0f : -6.0f/16.0f, -5.0f/16.0f );
                rotateMtx(mtx, 0.0f, angle, 0.0f);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(totalVertexCount,mtx);
            }
            gUsingTransform = 0;
        }
        break; // saveBillboardOrGeometry

    case BLOCK_BEACON:						// saveBillboardOrGeometry
        saveBoxGeometry( boxIndex, BLOCK_GLASS, 0, 1, 0x0, 0,16, 0,16, 0,16);
        if (!gPrint3D)
        {
            // chewy interior
            saveBoxGeometry( boxIndex, BLOCK_BEACON, dataVal, 0, DIR_BOTTOM_BIT, 3,13, 3,13, 3,13);
            saveBoxGeometry( boxIndex, BLOCK_OBSIDIAN, 0, 0, 0x0, 2,14, 0,3, 2,14);
        }
        break; // saveBillboardOrGeometry

    case BLOCK_SLIME:						// saveBillboardOrGeometry
        saveBoxGeometry(boxIndex, BLOCK_SLIME, dataVal, 1, 0x0, 0, 16, 0, 16, 0, 16);
        if (!gPrint3D)
        {
            // tasty slime center
            saveBoxGeometry(boxIndex, BLOCK_SLIME, dataVal, 0, 0x0, 3, 13, 3, 13, 3, 13);
        }
        break; // saveBillboardOrGeometry

    case BLOCK_BREWING_STAND:						// saveBillboardOrGeometry
        // brewing stand exports as an ugly block for 3D printing - too delicate to print. Check that we're not printing
        assert( !gPrint3D );
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // post
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, 0x0, 0,  7,  9,  0, 14,  7,  9 );
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
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 9 - (float)filled * 9, 16 - (float)filled * 9, 0, 16, 8, 8);
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
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  2,  8,   0,  2,   1,  7 );
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  2,  8,   0,  2,   9, 15 );
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, 0x0, 0,  9, 15,   0,  2,   5, 11 );
        break; // saveBillboardOrGeometry

    case BLOCK_LEVER:						// saveBillboardOrGeometry
        // make the lever on the ground, facing east, then move it into position
        uberTotalVertexCount = gModel.vertexCount;
        totalVertexCount = gModel.vertexCount;
        littleTotalVertexCount = gModel.vertexCount;
        // tip - move over by 1
        gUsingTransform = 1;
        saveBoxGeometry( boxIndex, BLOCK_LEVER, dataVal, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT,  7,9,  10,10,  6,8);
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 0.0f, 1.0f/16.0f );
        transformVertices(littleTotalVertexCount,mtx);

        // add lever - always mask top face, which is output above. Use bottom face if 3D printing, to make object watertight.
        // That said, levers are not currently exported when 3D printing, but just in case we ever do...
        saveBoxGeometry(boxIndex, BLOCK_LEVER, dataVal, 0, (gPrint3D ? 0x0 : DIR_BOTTOM_BIT) | DIR_TOP_BIT, 7, 9, 0, 10, 7, 9);
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        translateMtx(mtx, 0.0f, 9.5f/16.0f, 0.0f );
        // tips of levers almost touch
        rotateMtx(mtx, 0.0f, 0.0f, 38.8f);
        translateMtx(mtx, 0.0f, -8.0f/16.0f, 0.0f );
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);

        saveBoxGeometry( boxIndex, BLOCK_COBBLESTONE, 0, 0, 0x0,  4,12,  0,3,  5,11);

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
        break; // saveBillboardOrGeometry

    case BLOCK_DAYLIGHT_SENSOR:						// saveBillboardOrGeometry
    case BLOCK_INVERTED_DAYLIGHT_SENSOR:
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        swatchLocSet[DIRECTION_BLOCK_TOP] = swatchLoc;	// 6,15 or 13,22
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] =
            swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] =  SWATCH_INDEX( 5,15 );
        // TODO! For tile itself, Y data was centered instead of put at bottom, so note we have to shift it down
        saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, 0x0, 0, 0, 0,16, 0,6, 0,16 );
        break; // saveBillboardOrGeometry

    case BLOCK_STICKY_PISTON:						// saveBillboardOrGeometry
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
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 1, ((neighborType == BLOCK_PISTON_HEAD)?DIR_HI_X_BIT:0x0)|(gPrint3D ? 0x0 : DIR_LO_X_BIT),  0,4,   12,16,  0,4 );
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -90.0f);
        translateMtx(mtx, 6.0f/16.0f, 12.0f/16.0f, 6.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(littleTotalVertexCount,mtx);

        // piston body
        gUsingTransform = (bottomDataVal != 1);
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc+2, swatchLoc, swatchLoc+1, 0, 0x0,         0, 0,16,   0,12,  0,16 );
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
        break; // saveBillboardOrGeometry

    case BLOCK_PISTON_HEAD:						// saveBillboardOrGeometry
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
			yrot = 270.0f;
			break;
        case 1: // pointing up
            dir = DIRECTION_BLOCK_BOTTOM;
			yrot = 270.0f;
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
        // form the piston shaft sideways, just the small bit, then we rotate upwards
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 1, (((neighborType == BLOCK_PISTON) || (neighborType == BLOCK_STICKY_PISTON))?DIR_LO_X_BIT:0x0)||(gPrint3D ? 0x0 : DIR_HI_X_BIT),  4,16,   12,16,  0,4 );
        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;

        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -90.0f);
        translateMtx(mtx, 6.0f/16.0f, -4.0f/16.0f, 6.0f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(littleTotalVertexCount,mtx);

        // piston head, formed pointing up
        saveBoxMultitileGeometry( boxIndex, type, dataVal, (dataVal & 0x8)?(swatchLoc-2):(swatchLoc-1), swatchLoc, swatchLoc-1, 0, 0x0,         0, 0,16,   12,16,  0,16 );
        totalVertexCount = gModel.vertexCount - totalVertexCount;

		// now rotate the whole thing into place:
		// this is a little confused... I think it's Y (always 90), Z, Y rotation
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0, 270.0f, zrot);
        rotateMtx(mtx, 0.0f, yrot, 0.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);
        gUsingTransform = 0;
        break; // saveBillboardOrGeometry

    case BLOCK_HOPPER:						// saveBillboardOrGeometry
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        // outsides and bottom
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc-1, swatchLoc-1, swatchLoc-1, 1, DIR_TOP_BIT, 0, 0,16,  10,16,  0,16 );
        // next level down outsides and bottom
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc-1, swatchLoc-1, swatchLoc-1, 0, gPrint3D ? 0x0 : DIR_TOP_BIT, 0, 4,12,   4,10,  4,12 );
        // bottom level cube - move to position based on dataVal
        totalVertexCount = gModel.vertexCount;
        gUsingTransform = (dataVal > 1);
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc-1, swatchLoc-1, swatchLoc-1, 0, 0x0,         0, 6,10,   0, 4,  6,10 );
        gUsingTransform = 0;
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        if ( (dataVal & 0x7) > 1 )
        {
            float xShift = 0.0f;
            float zShift = 0.0f;
            identityMtx(mtx);
            // move 4x4x4 box up and over
            switch ( dataVal & 0x7 )
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
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 
                0, 16,   16, 16,   0, 16 );
        }
        else
        {
            // top as 4 small faces, and corresponding inside faces
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16, 10+(float)gPrint3D*2, 16,   2, 14 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2,  10+(float)gPrint3D*2, 16,   2, 14 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT, 0, 
                2, 14,  10+(float)gPrint3D*2, 16,  14, 16 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0, 
                2, 14,  10+(float)gPrint3D*2, 16,   0,  2 );

            // top corners
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2,  16, 16,  0,  2 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                0,  2, 16, 16,  14, 16 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16, 16, 16,  0,  2 );
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc-1, swatchLoc-1, 0, DIR_BOTTOM_BIT|DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0, 
                14, 16, 16, 16, 14, 16 );

            // inside bottom
            saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc-2, swatchLoc-2, swatchLoc-2, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 
                2, 14, 10 + (float)gPrint3D * 2, 10 + (float)gPrint3D * 2, 2, 14);
        }
        break; // saveBillboardOrGeometry

    case BLOCK_END_ROD:						// saveBillboardOrGeometry
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        yrot = zrot = 0.0f;
        //dir = DIRECTION_BLOCK_TOP;
        switch ( dataVal & 0x7 )
        {
        case 0: // pointing down
            //dir = DIRECTION_BLOCK_BOTTOM;
            zrot = 180.0f;
            break;
        case 1: // pointing up
            //dir = DIRECTION_BLOCK_TOP;
            break;
        case 2: // pointing north
            //dir = DIRECTION_BLOCK_SIDE_LO_Z;
            zrot = 90.0f;
            yrot = 270.0f;
            break;
        case 3: // pointing south
            //dir = DIRECTION_BLOCK_SIDE_HI_Z;
            zrot = 90.0f;
            yrot = 90.0f;
            break;
        case 4: // pointing west
            //dir = DIRECTION_BLOCK_SIDE_LO_X;
            zrot = 90.0f;
            yrot = 180.0f;
            break;
        case 5: // pointing east
            //dir = DIRECTION_BLOCK_SIDE_HI_X;
            zrot = 90.0f;
            break;
        default:
            assert(0);
        }

        totalVertexCount = gModel.vertexCount;
        littleTotalVertexCount = gModel.vertexCount;

        // we definitely do move the shaft into place, always
        gUsingTransform = 1;

        // form the rod
        // the sides
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0,  0,2,  1,16,  0,2);
        // for the high X and Z, we need to use (1-u) for x and z
        saveBoxReuseGeometry( boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_Z_BIT, 0x0, 14,16,  1,16, 14,16);
        // the ends; we need to use (1-v) for z here
        saveBoxReuseGeometry( boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0x0, 2,4,  1,16,  0,2);

        littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;

        identityMtx(mtx);
        //translateToOriginMtx(mtx, boxIndex);
        //rotateMtx(mtx, 0.0f, 0.0f, -90.0f);
        translateMtx(mtx, 7.0f/16.0f, 0.0f, 7.0f/16.0f);
        //translateFromOriginMtx(mtx, boxIndex);
        transformVertices(littleTotalVertexCount,mtx);

        // form the base of the rod, put it at the bottom
        //gUsingTransform = (bottomDataVal != 1);
        saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT, 0,  2,6,  0,1,  2,6);
        saveBoxReuseGeometry( boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT, 0x0, 2,6,  9,10,  2,6);
        // for the high X and Z, we need to use (1-u) for x and z
        saveBoxReuseGeometry( boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT|DIR_TOP_BIT|DIR_LO_X_BIT|DIR_HI_Z_BIT, 0x0, 10,14,  9,10,  10,14);
        
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateMtx(mtx, 4.0f/16.0f, 0.0f, 4.0f/16.0f);
        // note we transform just the last object, the base
        transformVertices(totalVertexCount-littleTotalVertexCount,mtx);

        // if any rotation occurs, here we go
        if ( (zrot != 0.0) || (yrot != 0.0) )
        {
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0, 0.0f, zrot);
            rotateMtx(mtx, 0.0f, yrot, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount,mtx);
        }
        gUsingTransform = 0;
        break; // saveBillboardOrGeometry

    case BLOCK_CHORUS_FLOWER:						// saveBillboardOrGeometry
        // 6 sides, no interior
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
        if ( dataVal == 5 ) {
            // fully mature
            swatchLoc++;
        }
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 1, DIR_BOTTOM_BIT, 2,14, 14,16, 2,14);
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 0, DIR_TOP_BIT, 2,14, 0,2, 2,14);
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 0, DIR_HI_X_BIT, 0,2, 2,14, 2,14);
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 0, DIR_LO_X_BIT, 14,16, 2,14, 2,14);
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 0, DIR_HI_Z_BIT, 2,14, 2,14, 0,2);
        saveBoxTileGeometry( boxIndex, type, dataVal, swatchLoc, 0, DIR_LO_Z_BIT, 2,14, 2,14, 14,16);
        break; // saveBillboardOrGeometry

    case BLOCK_IRON_BARS:						// saveBillboardOrGeometry
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

        // in 1.9 addition of posts made glass and bars merge differently
        //neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_BOTTOM]].origType;
        //if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
        //    (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        //{
        //    // neighbor above, turn off edge
        //    faceMask |= DIR_BOTTOM_BIT;
        //    tbFaceMask |= DIR_BOTTOM_BIT;
        //}

        //neighborType = gBoxData[boxIndex+gFaceOffset[DIRECTION_BLOCK_TOP]].origType;
        //if ( (neighborType == BLOCK_IRON_BARS) || (neighborType == BLOCK_GLASS_PANE) || (neighborType == BLOCK_STAINED_GLASS_PANE) || 
        //    (gBlockDefinitions[neighborType].flags & BLF_WHOLE) )
        //{
        //    // neighbor below, turn off edge
        //    faceMask |= DIR_TOP_BIT;
        //    tbFaceMask |= DIR_TOP_BIT;
        //}

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

        if ( (type == BLOCK_IRON_BARS) && !gPrint3D && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
        {
            // for rendering iron bars, we just need one side of each wall - easier
            switch (filled)
            {
            case 0:
                // just a little box
                // bottom & top, double-sided
                saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0,
                    7, 9, 0, 0, 7, 9);
                saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0,
                    7, 9, 16, 16, 7, 9);

                // north-south
                saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7, 9, 0, 16, 8, 8);

                // east-west
                saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8, 8, 0, 16, 7, 9);
                break;
            case 15:
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 9,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 9,16 );

                // bottom & top of east-west wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // north and south ends
                //saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                //	7, 9, 0,16, 0,16 );

                // east and west ends
                //saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                //	7, 9, 0,0, 0,16 );

                // north-south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );
                // east-west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 1:
                // north wall only, just south edge as border
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,9 );

                // south end
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7, 9, 0,16, 9,9 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,9 );
                break;
            case 2:
                // east wall only
                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    7,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    7,16, 16,16, 7, 9 );

                // west end
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7,7, 0,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7,16, 0,16, 8,8 );
                break;
            case 3:
                // north and east: build west face of north wall, plus top and bottom
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,8 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,8 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 4:
                // south wall only
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 7,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 7,16 );

                // south end
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    7, 9, 0,16, 7,7 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 7,16 );
                break;
            case 5:
                // north and south - easy!
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // north-south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );
                break;
            case 6:
                // east and south
                // south and east: build west face of north wall, plus top and bottom
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 8,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 8,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    8,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 7:
                // north, east, and south - 5 faces horizontally
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );

                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    9,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    9,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    8,16, 0,16, 8,8 );
                break;
            case 8:
                // west wall only
                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,9, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,9, 16,16, 7, 9 );

                // west end
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    9,9, 0,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,9, 0,16, 8,8 );
                break;
            case 9:
                // north and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,8 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,8 );

                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // bottom & top of east wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 16,16, 7, 9 );

                // west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 10:
                // east and west - have to mess with top and bottom being rotated
                // bottom & top of wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 11:
                // north, east, and west
                // north top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,7 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,7 );
                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,8 );

                // east-west bottom & top
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,16, 0,16, 8,8 );
                break;
            case 12:
                // south and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 8,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 8,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // bottom & top of west wall - tiny bit of overlap at corner
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,8, 16,16, 7, 9 );

                // west wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 13:
                // north, south, and west
                // bottom & top of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 0,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 0,16 );

                // south wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 0,16 );

                // bottom & top of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,7, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,7, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
                    0,8, 0,16, 8,8 );
                break;
            case 14:
                // east, south, and west
                // south top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 0,0, 9,16 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, 0x0, 0,
                    7, 9, 16,16, 9,16 );
                // north wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    8,8, 0,16, 8,16 );

                // east-west bottom & top
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_TOP_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,0, 7, 9 );
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|tbFaceMask, ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 16,16, 7, 9 );

                // east wall (easy!)
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY, 0,
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
                // post
                saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 1, 0x0, FLIP_Z_FACE_VERTICALLY | ROTATE_TOP_AND_BOTTOM, 0,
                    7 - fatten, 9 + fatten, 0, 16, 7 - fatten, 9 + fatten);
                break;
            case 15:
                // all four; 15 has no outside edges.
                // top & bottom of north-south wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );

                // east and west faces of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,7-fatten );
                // east and west faces of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north and south face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // north and south face of east wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 1:
                // north wall only, just south edge as border
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                break;
            case 2:
                // east wall only
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 3:
                // north and east: build west face of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                // east face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 4:
                // south wall only
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 7-fatten,16 );
                break;
            case 5:
                // north and south - easy!
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                break;
            case 6:
                // east and south
                // build west face of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 7-fatten,16 );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north face of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    7-fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 7:
                // north, east, and south - 5 faces horizontally
                // west face of north-south wall, and top & bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                // east face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north and south face of east wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                break;
            case 8:
                // west wall only
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 9+fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 9:
                // north and west
                // build east face of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 9+fatten );
                // west face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // south face of west wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 9+fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 10:
                // east and west - have to mess with top and bottom being rotated
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, faceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 11:
                // north, east, and west
                // east and west faces of north wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north face of west wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // north face of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // south face of west-east wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            case 12:
                // south and west
                // build east face of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 7-fatten,16 );
                // west face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // north face of west wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,9+fatten, 0,16, 7-fatten, 9+fatten );
                // south face of west wall, shorter at west, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,7-fatten, 0,16, 7-fatten, 9+fatten );
                break;
            case 13:
                // north, south, and west
                // east face of north-south wall, and top & bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0,16 );
                // west face of north wall, shorter at south
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 0, 7-fatten );
                // north and south face of west wall, shorter at east, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,7-fatten, 0,16, 7-fatten, 9+fatten );
                // east face of south wall, shorter at north
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                break;
            case 14:
                // east, south, and west
                // east and west faces of south wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 1, DIR_LO_Z_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_X_FACE_VERTICALLY, 0,
                    7-fatten, 9+fatten, 0,16, 9+fatten,16 );
                // south face of west wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0, 7-fatten, 0,16, 7-fatten, 9+fatten );
                // south face of east wall
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    9+fatten,16, 0,16, 7-fatten, 9+fatten );
                // north face of west-east wall, plus top and bottom
                saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_HI_Z_BIT|tbFaceMask, FLIP_Z_FACE_VERTICALLY|ROTATE_TOP_AND_BOTTOM, 0,
                    0,16, 0,16, 7-fatten, 9+fatten );
                break;
            }
        }
        break; // saveBillboardOrGeometry

    case BLOCK_TRIPWIRE_HOOK:						// saveBillboardOrGeometry
        {
            // 0x4 means "tripwire connected"
            // 0x8 means "tripwire tripped"
            bool tripwireConnected = (gBoxData[boxIndex].data & 0x4) ? true : false;
            bool tripwireTripped = (gBoxData[boxIndex].data & 0x8) ? true : false;

            // make the tripwire hook in position, facing east, then rotate it as needed
            totalVertexCount = gModel.vertexCount;

            // There are three elements to the tripwire hook (well, four with the wire):
            // 1) the box that attaches to the wall. That's wood planks. 2 deep by 4 wide by 8 high, 1 pixel off ground. Hole starts about 1.5 pixels down and goes to 4.
            // 2) the stick made of wood sticking out. That's 5 long by 2x2, top is inside the hole, outside by half a pixel.
            // Something strange happens if you substitute in a replacement trip_wire_source.png in Minecraft. It resizes!
            // 2a) the stick made of wood sticking out. That's 7 long by 2x2, top is inside the hole, outside by half a pixel. This piece is 6/7ths scale.
            // 3) the stone ring. 6x6x1 with a 2x2 hole. It's actually 4x4 pixels wide, so scaled by 2/3.
            // finally:
            // 4) the tripwire, if any. TODO - this one's a pain, since when triggered a piece is flat on the ground, not the whole thing, making for a tough composite.

            gUsingTransform = 1;
            // box on wall
            littleTotalVertexCount = gModel.vertexCount;
            // left half
            saveBoxGeometry(boxIndex, BLOCK_OAK_PLANKS, 0, 1, 0x0, 6, 10, 1, 9, 3, 5);
            // right half
            //saveBoxGeometry(boxIndex, BLOCK_OAK_PLANKS, 0, 0, 0x0, 9, 10, 1, 9, 3, 5);
            // top bit in middle
            //saveBoxGeometry(boxIndex, BLOCK_OAK_PLANKS, 0, 0, (gPrint3D ? 0x0 : DIR_LO_X_BIT | DIR_HI_X_BIT), 7, 9, 7, 9, 3, 5);
            // bottom bit in middle
            //saveBoxGeometry(boxIndex, BLOCK_OAK_PLANKS, 0, 0, (gPrint3D ? 0x0 : DIR_LO_X_BIT | DIR_HI_X_BIT), 7, 9, 1, 5, 3, 5);
            littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
            identityMtx(mtx);
            translateMtx(mtx, 0.0f, 0.0f, -3 * ONE_PIXEL);
            transformVertices(littleTotalVertexCount, mtx);

            // add wood lever - would definitely break off in 3D print (which we currently don't allow), so do only if not printing
            if (!gPrint3D) {
                swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);

                littleTotalVertexCount = gModel.vertexCount;
                saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT, 7, 9, 0, 5, 7, 9);
                saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 7, 9, 0, 5, 14, 16);
                littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                rotateMtx(mtx, 90.0f, 0.0f, 0.0f);
                scaleMtx(mtx, 4.0f / 5.0f, 4.0f / 5.0f, 4.0f / 5.0f);
                // note: tripwire connected but not tripped is the "neutral" pose; rotations and so on must be applied if tripped or if not connected
                if (!tripwireConnected || tripwireTripped) {
                    rotateMtx(mtx, tripwireTripped ? -22.5f : 45.0f, 0.0f, 0.0f);
                    translateMtx(mtx, 0.0f, tripwireTripped ? 0.96f * ONE_PIXEL : -1.8f * ONE_PIXEL, tripwireTripped ? 0.2f * ONE_PIXEL : 0.7f * ONE_PIXEL);
                }
                translateMtx(mtx, 0.0f, -2 * ONE_PIXEL, -8.4f * ONE_PIXEL);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(littleTotalVertexCount, mtx);

                littleTotalVertexCount = gModel.vertexCount;
                // left half
                saveBoxGeometry(boxIndex, type, dataVal, 1, DIR_LO_X_BIT | DIR_HI_X_BIT, 5, 7, 7, 8, 3, 9);
                saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 5, 11, 7, 8, 5, 11);
                // right half
                saveBoxGeometry(boxIndex, type, dataVal, 1, DIR_LO_X_BIT | DIR_HI_X_BIT, 9, 11, 7, 8, 3, 9);
                saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 5, 11, 7, 8, 5, 11);
                // top bit in middle
                saveBoxGeometry(boxIndex, type, dataVal, 1, DIR_LO_X_BIT | DIR_HI_X_BIT, 7, 9, 7, 8, 7, 9);
                // bottom bit in middle
                saveBoxGeometry(boxIndex, type, dataVal, 1, DIR_LO_X_BIT | DIR_HI_X_BIT, 7, 9, 7, 8, 3, 5);

                littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
                identityMtx(mtx);
                translateToOriginMtx(mtx, boxIndex);
                //rotateMtx(mtx, 90.0f, 0.0f, 0.0f);
                scaleMtx(mtx, 2.0f / 3.0f, 4.0f / 5.0f, 2.0f / 3.0f);
                // note: unlike above, tripwire connected and tripped is the "neutral" pose; rotations and so on must be applied if not tripped or if not connected
                if (!tripwireTripped) {
                    rotateMtx(mtx, tripwireConnected ? -22.5f : -45.0f, 0.0f, 0.0f);
                    translateMtx(mtx, 0.0f, tripwireConnected ? 0.95f * ONE_PIXEL : 2.45f * ONE_PIXEL, tripwireConnected ? 0.35f * ONE_PIXEL : -1.30f * ONE_PIXEL);
                }
                translateMtx(mtx, 0.0f, -3.5f * ONE_PIXEL, 1.05f * ONE_PIXEL);
                translateFromOriginMtx(mtx, boxIndex);
                transformVertices(littleTotalVertexCount, mtx);
            }

            totalVertexCount = gModel.vertexCount - totalVertexCount;

            // rotate whole into position
            switch (dataVal & 0x3)
            {
            default:
            case 0:
                angle = 0;
                break;
            case 1:
                angle = 90;
                break;
            case 2:
                angle = 180;
                break;
            case 3:
                angle = 270;
                break;
            }
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            rotateMtx(mtx, 0.0f, angle, 0.0f);
            translateFromOriginMtx(mtx, boxIndex);
            transformVertices(totalVertexCount, mtx);
            gUsingTransform = 0;
        }
        break; // saveBillboardOrGeometry

    case BLOCK_STRUCTURE_VOID:						// saveBillboardOrGeometry
													// tiny little red wool block
		swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		saveBoxTileGeometry(boxIndex, type, dataVal, swatchLoc, 1, 0x0, 7, 9, 7, 9, 7, 9);
		break; // saveBillboardOrGeometry

	case BLOCK_CONDUIT:						// saveBillboardOrGeometry
		swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		gUsingTransform = 1;
		// note all six sides are used, but with different texture coordinates
		// we do not correctly set these sides, but do a "reasonable fascimile", grabbing four random sides. TODO - someday get it exactly right...
		// modify z
		saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, 0x0, 2, 8, 2, 8, 2, 8);
		// modify x
		saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 8, 14, 8, 14, 8, 14);
		// set top and bottom:
		saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 2, 8, 2, 8, 8, 14);

		identityMtx(mtx);
		translateMtx(mtx, 3.0f / 16.0f, 3.0f / 16.0f, 3.0f / 16.0f);
		transformVertices(8, mtx);

		gUsingTransform = 0;
		break; // saveBillboardOrGeometry

	case BLOCK_SEA_PICKLE:						// saveBillboardOrGeometry
		swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		itemCount = (dataVal&0x3) + 1;
		{

			gUsingTransform = 1;
			// sea pickle sizes, geometry and texture match
			// 1,2,3,4
			// 4x6, 4x4, 4x6, 4x7
			// TODO: the pickle textures are more or less right, but not really

			totalVertexCount = gModel.vertexCount;

			// make a pickle, 6 high
			// modify z
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 0, 4, 5, 11, 4, 8);
			// set top
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 4, 8, 4, 8, 1, 5);
			// set bottom:
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 8, 12, 4, 8, 1, 5);
			// output pickle top billboards
			outputPickleTop(boxIndex, swatchLoc, 0.0f);

			identityMtx(mtx);
			float xShift = 2.0f;
			float zShift = 6.0f;
			switch (itemCount) {
			case 1: xShift = 6.0f; zShift = 2.0f; break;
			case 2: xShift = 9.0f; zShift = 5.0f; break;
			case 3: xShift = 9.0f; zShift = 2.0f; break;
			case 4: xShift = 2.0f; zShift = 6.0f; break;
			}
			translateMtx(mtx, xShift / 16.0f, -5.0f / 16.0f, zShift / 16.0f);

			totalVertexCount = gModel.vertexCount - totalVertexCount;
			transformVertices(totalVertexCount, mtx);

			if (itemCount > 1) {
				// pickle 2 - 4 high
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 0, 4, 6, 10, 4, 8);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 4, 8, 4, 8, 1, 5);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 8, 12, 4, 8, 1, 5);
				outputPickleTop(boxIndex, swatchLoc, -1.0f);

				identityMtx(mtx);
				switch (itemCount) {
				case 2: xShift = 4.0f; zShift = 0.0f; break;
				case 3: xShift = 2.0f; zShift = 6.0f; break;
				case 4: xShift = 10.0f; zShift = -1.0f; break;
				}
				translateMtx(mtx, xShift / 16.0f, -6.0f / 16.0f, zShift / 16.0f);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				transformVertices(totalVertexCount, mtx);
			}
			if (itemCount > 2) {
				// pickle 3 - 6 high
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 0, 4, 5, 11, 4, 8);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 4, 8, 4, 8, 1, 5);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 8, 12, 4, 8, 1, 5);
				outputPickleTop(boxIndex, swatchLoc, 0.0f);

				identityMtx(mtx);
				switch (itemCount) {
				case 3: xShift = 4.0f; zShift = 0.0f; break;
				case 4: xShift = 2.0f; zShift = -1.0f; break;
				}
				translateMtx(mtx, xShift / 16.0f, -5.0f / 16.0f, zShift / 16.0f);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				transformVertices(totalVertexCount, mtx);
			}
			if (itemCount > 3) {
				// pickle 4 - 7 high
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, 0x0, 0, 4, 4, 11, 4, 8);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 4, 8, 4, 8, 1, 5);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 8, 12, 4, 8, 1, 5);
				outputPickleTop(boxIndex, swatchLoc, 0.0f);

				identityMtx(mtx);
				translateMtx(mtx, 8.0f / 16.0f, -4.0f / 16.0f, 6.0f / 16.0f);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				transformVertices(totalVertexCount, mtx);
			}

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_TURTLE_EGG:						// saveBillboardOrGeometry
		// hatching causes swatchLoc to increment
		swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		swatchLoc += ((dataVal >> 2) & 0x3);
		itemCount = (dataVal & 0x3) + 1;
		{
			gUsingTransform = 1;
			// egg sizes, 1,2,3,4
			// geometry: 5x7, 4x5, 3x4, 4x4 (was 3x3 in 1.13)
			// texture: 4x7, 4x5, 3x4, 4x4
			// TODO: the egg textures are more or less right, but not really

			// make an egg
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 4, 4, 11, 0, 4);
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 1, 5, 4, 11, 1, 5);
			// for the high X and Z, we need to use (1-u) for x and z
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 11, 15, 4, 11, 11, 15);

			identityMtx(mtx);
			float xShift = 6.0f;
			float zShift = 3.0f;
			switch (itemCount) {
			case 1: xShift = 8.0f; zShift = 5.0f; break;
			case 2: xShift = 4.0f; zShift = 7.0f; break;
			case 3: xShift = 7.0f; zShift = 8.0f; break;
			case 4: xShift = 5.0f; zShift = 4.0f; break;
			}
			translateMtx(mtx, xShift / 16.0f, -4.0f / 16.0f, zShift / 16.0f);
			transformVertices(8, mtx);

			if (itemCount > 1) {
				// egg 2 - 5 high
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 6, 10, 5, 10, 7, 11);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 1, 5, 5, 10, 1, 5);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 11, 15, 5, 10, 11, 15);
				identityMtx(mtx);
				switch (itemCount) {
				case 2: xShift = 1.0f; zShift = 4.0f; break;
				case 3: xShift = 5.0f; zShift = -2.0f; break;
				case 4: xShift = -5.0f; zShift = 0.0f; break;
				}
				translateMtx(mtx, xShift / 16.0f, -5.0f / 16.0f, zShift / 16.0f);
				transformVertices(8, mtx);
			}
			if (itemCount > 2) {
				// egg 3 - 4 high by 3
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 3, 5, 9, 0, 3);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 1, 4, 5, 9, 1, 4);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 11, 14, 5, 9, 11, 14);
				identityMtx(mtx);
				switch (itemCount) {
				case 3: xShift = 2.0f; zShift = 6.0f; break;
				case 4: xShift = 11.0f; zShift = 7.0f; break;
				}
				translateMtx(mtx, xShift / 16.0f, -5.0f / 16.0f, zShift / 16.0f);
				transformVertices(8, mtx);
			}
			if (itemCount > 3) {
				// egg 4 - 4 high by 4
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 4, 1, 5, 0, 4);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 0, 4, 1, 5, 0, 4);
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 11, 15, 1, 5, 11, 15);
				identityMtx(mtx);
				translateMtx(mtx, 6.0f / 16.0f, -1.0f / 16.0f, 9.0f / 16.0f);
				transformVertices(8, mtx);
			}

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_BAMBOO:						// saveBillboardOrGeometry
		{
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			age = (dataVal & 0x1);
			leafSize = (dataVal & 0x6) >> 1;

			gUsingTransform = 1;
			totalVertexCount = gModel.vertexCount;

			// +/-3 - TODO: how does Minecraft do this?
			wobbleObjectLocation(boxIndex, shiftX, shiftZ);

			seedWithXYZ(boxIndex);
		
			// shift which bamboo gets used: 0-4, 4-8, 8-12
			float txrShift = (float)(((int)((rand()/(RAND_MAX+1)) * 4))*3);
			if (age == 0) {
				// note all six sides are used, but with different texture coordinates
				// sides:
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 0 + txrShift, 2 + txrShift, 0, 16, 0 + txrShift, 2 + txrShift);
				// other sides - UV has to be low
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 14 - txrShift, 16 - txrShift, 0, 16, 14 - txrShift, 16 - txrShift);
				// top, not bottom (which appears to never be visible):
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 14, 16, 14, 16, 14, 16);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, (shiftX - txrShift + 7.0f) / 16.0f, 0.0f / 16.0f, (shiftZ - txrShift + 7.0f) / 16.0f);
			}
			else {
				// note all six sides are used, but with different texture coordinates
				// sides:
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 0 + txrShift, 3 + txrShift, 0, 16, 0 + txrShift, 3 + txrShift);
				// other sides - UV has to be low
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_Z_BIT, 0x0, 13 - txrShift, 16 - txrShift, 0, 16, 13 - txrShift, 16 - txrShift);
				// top, not bottom (which appears to never be visible):
				saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 13, 16, 13, 16, 13, 16);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, (shiftX - txrShift + 6.5f) / 16.0f, 0.0f / 16.0f, (shiftZ - txrShift + 7.0f) / 16.0f);
			}
			transformVertices(totalVertexCount, mtx);

			// leaf
			if (leafSize == 2) {
				swatchLoc = SWATCH_INDEX(6, 37);
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 8, 8);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				rotateMtx(mtx, 0.0f, 90.0f, 0.0f);
				translateMtx(mtx, shiftX / 16.0f, 0.0f, shiftZ / 16.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(totalVertexCount, mtx);

				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 8, 8);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, shiftX / 16.0f, 0.0f, shiftZ / 16.0f);
				transformVertices(totalVertexCount, mtx);
			}
			else if (leafSize == 1) {
				swatchLoc = SWATCH_INDEX(8, 37);
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 8, 8);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				rotateMtx(mtx, 0.0f, 90.0f, 0.0f);
				translateMtx(mtx, shiftX / 16.0f, 0.0f, shiftZ / 16.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(totalVertexCount, mtx);

				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 8, 8);
				totalVertexCount = gModel.vertexCount - totalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, shiftX / 16.0f, 0.0f, shiftZ / 16.0f);
				transformVertices(totalVertexCount, mtx);
			}

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_COMPOSTER:						// saveBillboardOrGeometry
		{
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			// we seal the composter against the compost height (possibly empty)
			int heightVal = (dataVal & 0xf);
			bool fullBin = (heightVal == 8);
			if (fullBin) {
				heightVal = 7;
			}
			// when filled, height 1 -> 3, height 2 -> 5,..., height 7->15
			float compostHeight = 1 + (float)heightVal * 2;
			if (heightVal == 0) {
				// but when empty, height 0 -> 2
				compostHeight = 2.0f;
			}
			// outsides
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 1, DIR_TOP_BIT, 0, 0, 16, 0, 16, 0, 16);
			// top as 4 small faces, and corresponding inside faces
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				14, 16, compostHeight, 16, 2, 14);	// top and lo_x
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				0, 2, compostHeight, 16, 2, 14);	// top and hi_x
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, 0,
				2, 14, compostHeight, 16, 14, 16);	// top and lo_z
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0,
				2, 14, compostHeight, 16, 0, 2);	// top and hi_z
			// four tiny corners, just tops
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				0, 2, 16, 16, 0, 2);	// top
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				0, 2, 16, 16, 14, 16);	// top
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				14, 16, 16, 16, 0, 2);	// top
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc + 1, swatchLoc + 2, 0, DIR_BOTTOM_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0,
				14, 16, 16, 16, 14, 16);	// top

			// bottom
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 2, swatchLoc + 2, swatchLoc + 2, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_TOP_BIT, 0,
				0, 16, 3, 6, 0, 16);

			// inside bottom
			if (heightVal == 0) {
				// show clean inside bottom if cauldron is empty
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 2, swatchLoc + 2, swatchLoc + 2, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT, 0,
					2, 14, 3, 6, 2, 14);
			} else if (fullBin) {
				// show bone meal compost
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 4, swatchLoc + 4, swatchLoc + 4, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT, 0,
					2, 14, compostHeight, compostHeight, 2, 14);
			} else {
				// show compost
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc + 3, swatchLoc + 3, swatchLoc + 3, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT, 0,
					2, 14, compostHeight, compostHeight, 2, 14);
			}
		}
		break; // saveBillboardOrGeometry

	case BLOCK_STONECUTTER:						// saveBillboardOrGeometry
		topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		sideSwatchLoc = topSwatchLoc + 1;
		bottomSwatchLoc = topSwatchLoc + 2;
		saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, 0x0, 0, 0, 16, 0, 9, 0, 16);

		if (!gPrint3D) {
			// saw blade
			gUsingTransform = 1;
			swatchLoc = SWATCH_INDEX(7, 41);
			totalVertexCount = gModel.vertexCount;
			saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 8, 8, 8);
			totalVertexCount = gModel.vertexCount - totalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			// rotate into place - the only thing that is affected by direction
			rotateMtx(mtx, 0.0f, 90.0f*(dataVal&0x3), 0.0f);
			translateMtx(mtx, 0.0f, 8.0f / 16.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(totalVertexCount, mtx);
			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_GRINDSTONE: // saveBillboardOrGeometry
		{
			int face = (dataVal & 0xc) >> 2;	// floor/wall/ceiling
			facing = dataVal & 0x3;

			gUsingTransform = 1;
			totalVertexCount = littleTotalVertexCount = gModel.vertexCount;

			// note all six sides are used, but with different texture coordinates
			// side:
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			// rules: the 0,12, 4,16, 4,12 defines the size of the object in X, Y, and Z. These vertices can be reused and assigned new UVs. So this object is 12x12x8.
			// We first select the Z face, so X 0-12 and Y 4-16 selects from the texture tile and applies that face. FLIP_Z_FACE_VERTICALLY then mirrors the face to the DIRECTION_BLOCK_SIDE_LO_Z side.
			//saveBoxMultitileGeometry(... DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, xmin, xmax, ymin, ymax, zmin, zmax);
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 12, 4, 16, 4, 12);
			// other sides; use the side tile of the grindstone:
			swatchLoc += 2;
			// So, X will use U,V = 16-zmax,ymin to 16-zmin,ymax for the mapping to the X faces. (The x values are ignored, they're only used for geometry) In other words, treat Y as X in the texture itself.
			// This is not the slightest bit entirely confusing. Basically, the y axis gets used for both x and z faces.
			//saveBoxReuseGeometry(... DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, FLIP_X_FACE_VERTICALLY, 0, 0, vmin, vmax, 16 - umax, 16 - umin);
			//saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, FLIP_X_FACE_VERTICALLY, 0, 0, 4, 16, 8, 16);
			saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 0, 8, 4, 16);
			// top and bottom: these use xmin,16-zmax and xmax,16-zmin normally. y's are ignored
			//saveBoxReuseGeometry(... DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, umin, umax, 0, 0, 16 - vmax, 16 - vmin);
			//saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 0, 8, 0, 0, 0, 12);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 0, 8, 4, 16);
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateMtx(mtx, 2.0f / 16.0f, 0.0f, 0.0f);
			transformVertices(littleTotalVertexCount, mtx);

			// now add the two supports
			for (i = 0; i < 2; i++) {
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY) + 1;
				littleTotalVertexCount = gModel.vertexCount;
				// make the 6x6x2 wood axle
				// rules: the 0,12, 4,16, 4,12 defines the size of the object in X, Y, and Z. These vertices can be reused and assigned new UVs. So this object is 6x6x2.
				// We first select the Z face, so X 0-12 and Y 4-16 selects from the texture tile and applies that face. FLIP_Z_FACE_VERTICALLY then mirrors the face to the DIRECTION_BLOCK_SIDE_LO_Z side.
				//saveBoxMultitileGeometry(... DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, xmin, xmax, ymin, ymax, zmin, zmax);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | (gPrint3D ? 0x0 : ((i == 0) ? DIR_HI_Z_BIT : DIR_LO_Z_BIT)), FLIP_Z_FACE_VERTICALLY, 0, 6, 10, 16, 2, 4);
				// So, X will use U,V = 16-zmax,ymin to 16-zmin,ymax for the mapping to the X faces. (The x values are ignored, they're only used for geometry) In other words, treat Y as X in the texture itself.
				// This is not the slightest bit entirely confusing. Basically, the y axis gets used for both x and z faces.
				//saveBoxReuseGeometry(... DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, FLIP_X_FACE_VERTICALLY, 0, 0, vmin, vmax, 16 - umax, 16 - umin);
				//saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, FLIP_X_FACE_VERTICALLY, 0, 0, 10, 16, 8, 10);
				saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 6, 8, 10, 16);
				// top and bottom: these use xmin,16-zmax and xmax,16-zmin normally. y's are ignored
				//saveBoxReuseGeometry(... DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, umin, umax, 0, 0, 16 - vmax, 16 - vmin);
				//saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, REVOLVE_INDICES, 8, 10, 0, 0, 0, 6);
				saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 8, 10, 10, 16);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, 5.0f / 16.0f, -3.0f / 16.0f, (float)i*10.0f / 16.0f);
				transformVertices(littleTotalVertexCount, mtx);

				swatchLoc = SWATCH_INDEX(14, 19);
				littleTotalVertexCount = gModel.vertexCount;
				// make the 2x7x4 support
				// rules: the 0,12, 4,16, 4,12 defines the size of the object in X, Y, and Z. These vertices can be reused and assigned new UVs. So this object is 2x7x4.
				// We first select the Z face, so X 0-12 and Y 4-16 selects from the texture tile and applies that face. FLIP_Z_FACE_VERTICALLY then mirrors the face to the DIRECTION_BLOCK_SIDE_LO_Z side.
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, (gPrint3D ? 0x0 : DIR_TOP_BIT), FLIP_Z_FACE_VERTICALLY| FLIP_X_FACE_VERTICALLY | REVOLVE_INDICES, 6, 10, 0, 7, 2, 4);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateMtx(mtx, 0.0f, 0.0f, (float)i*10.0f / 16.0f);
				transformVertices(littleTotalVertexCount, mtx);
			}

			totalVertexCount = gModel.vertexCount - totalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			rotateMtx(mtx, 0.0f, 0.0f, (float)face*90.0f);
			rotateMtx(mtx, 0.0f, (float)facing*90.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(totalVertexCount, mtx);

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_LECTERN: // saveBillboardOrGeometry
		{
			facing = dataVal & 0x3;

			gUsingTransform = 1;
			totalVertexCount = littleTotalVertexCount = gModel.vertexCount;

			// base
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY) + 2;
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 14, 16, 0, 16);
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 6, 8, 0, 0);
			saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, DIR_HI_X_BIT, 0, 16, 0, 2);
			saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT, 0, 16, 6, 8);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT, 0, 16, 0, 16);
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT, 0, 16, 0, 16);
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateMtx(mtx, 0.0f, -14.0f / 16.0f, 0.0f);
			transformVertices(littleTotalVertexCount, mtx);

			// column - set front - annoyingly, the side part wanted is actually rotated 90 degrees
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY) + 3;
			littleTotalVertexCount = gModel.vertexCount;
			// establish geometry, but don't output anything, ugh
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 0, 8, 3, 15, 4, 12);
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, FLIP_Z_FACE_VERTICALLY, 8, 16, 0, 12, 0, 0);
			swatchLoc -= 2;	// set side
			//saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 8, 16, 0, 12);
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_HI_X_BIT, FLIP_X_FACE_VERTICALLY | ROTATE_X_FACE_90, 0, 0, 0, 8, 0, 12);
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_LO_X_BIT, ROTATE_X_FACE_90, 0, 0, 0, 8, 1, 13);
			if (gPrint3D) {
				// just to make the column watertight - the texture doesn't really matter
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY);
				saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 0, 8, 4, 12);
			}
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateMtx(mtx, 4.0f / 16.0f, -1.0f / 16.0f, 0.0f);
			// should really do it so that the front is in the right place, but forget it...
			translateToOriginMtx(mtx, boxIndex);
			rotateMtx(mtx, 0.0f, 90.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(littleTotalVertexCount, mtx);
		
			// lectern top
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY)+1;
			littleTotalVertexCount = gModel.vertexCount;
			// start with the lower side texture only
			saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY | REVOLVE_INDICES, 3, 16, 12, 16, 0, 16);
			// Z sides and X hit bit edge
			saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT, FLIP_Z_FACE_VERTICALLY, 3, 16, 8, 12, 0, 16);
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			// top
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT, 0, 16, 1, 14);
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY);
			// bottom
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_TOP_BIT, 0, 16, 1, 14);
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			rotateMtx(mtx, 0.0f, 0.0f, -22.5f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(littleTotalVertexCount, mtx);

			totalVertexCount = gModel.vertexCount - totalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			rotateMtx(mtx, 0.0f, 180.0f+(float)facing*90.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(totalVertexCount, mtx);

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_BELL: // saveBillboardOrGeometry
		{
			topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			sideSwatchLoc = topSwatchLoc + 1;
			bottomSwatchLoc = topSwatchLoc + 2;
			int attachment = (dataVal & 0xc) >> 2;
			facing = dataVal & 0x3;

			gUsingTransform = 1;
			totalVertexCount = littleTotalVertexCount = gModel.vertexCount;

			// bottom of bell
			saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 0, 8, 7, 9, 8, 16);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, topSwatchLoc, DIR_BOTTOM_BIT, 0, 8, 8, 16);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, bottomSwatchLoc, DIR_TOP_BIT, 0, 8, 8, 16);
			saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 1, 7, 9, 16, 9, 15);
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, topSwatchLoc, DIR_BOTTOM_BIT, 0, 8, 8, 16);
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateMtx(mtx, 4.0f / 16.0f, -3.0f / 16.0f, -4.0f / 16.0f);
			transformVertices(littleTotalVertexCount, mtx);

			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_DARK_OAK_WOOD_STAIRS].txrX, gBlockDefinitions[BLOCK_DARK_OAK_WOOD_STAIRS].txrY);
			switch (attachment) {
			default:
				assert(0);
			case 0: // floor
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 2, 14, 13, 15 + (fatten/2.0f), 7 - (fatten/2.0f), 9 + (fatten/2.0f));
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_STONE].txrX, gBlockDefinitions[BLOCK_STONE].txrY);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 0, 2, 0, 16, 6, 10);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 14, 16, 0, 16, 6, 10);
				break;
			case 1: // ceiling
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 7, 9, 13, 16, 7 - (fatten/2.0f), 9 + (fatten/2.0f));
				break;
			case 2: // single wall
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 3, 16, 13, 15 + (fatten/2.0f), 7 - (fatten/2.0f), 9 + (fatten/2.0f));
				break;
			case 3:	// double wall
				saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 0, 16, 13, 15 + (fatten/2.0f), 7 - (fatten/2.0f), 9 + (fatten/2.0f));
				break;
			}

			totalVertexCount = gModel.vertexCount - totalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			rotateMtx(mtx, 0.0f, (float)facing*90.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(totalVertexCount, mtx);

			gUsingTransform = 0;
		}
		break; // saveBillboardOrGeometry

	case BLOCK_LANTERN: // saveBillboardOrGeometry
	{
		swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		int hanging = (dataVal & 0x1);

		gUsingTransform = 1;

		// bottom of lantern
		littleTotalVertexCount = gModel.vertexCount;
		saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 0, 6, 7, 14, 10, 16);
		saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 0, 6, 1, 7);
		littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
		identityMtx(mtx);
		// put it on ground or hanging, 1 pixel higher
		translateMtx(mtx, 5.0f / 16.0f, ((float)hanging - 7.0f) / 16.0f, -5.0f / 16.0f);
		transformVertices(littleTotalVertexCount, mtx);

		// top of lantern
		littleTotalVertexCount = gModel.vertexCount;
		saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT, FLIP_Z_FACE_VERTICALLY | FLIP_X_FACE_VERTICALLY, 1, 5, 14, 16, 11, 15);
		saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT, 1, 5, 2, 6);
		littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
		identityMtx(mtx);
		// put it on ground or hanging, 1 pixel higher
		translateMtx(mtx, 5.0f / 16.0f, ((float)hanging - 7.0f) / 16.0f, -5.0f / 16.0f);
		transformVertices(littleTotalVertexCount, mtx);

		// chain & connector
		if (!gPrint3D) {
			// connector at top
			for (i = 0; i < 2 - hanging; i++) {
				littleTotalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 11, 14, 4, 6, 8, 8);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				translateMtx(mtx, -4.5f / 16.0f, ((float)hanging + 5.0f) / 16.0f, 0.0f);
				rotateMtx(mtx, 0.0f, (float)i*90.0f + 45.0f, 0.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(littleTotalVertexCount, mtx);
			}
			// chain, if any
			if (hanging) {
				// link
				littleTotalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 11, 14, 11, 15, 8, 8);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				translateMtx(mtx, -4.5f / 16.0f, 0.0f / 16.0f, 0.0f);
				rotateMtx(mtx, 0.0f, 135.0f, 0.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(littleTotalVertexCount, mtx);

				// top link
				littleTotalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, swatchLoc, swatchLoc, swatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 11, 14, 8, 10, 8, 8);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				translateMtx(mtx, -4.5f / 16.0f, 6.0f / 16.0f, 0.0f);
				rotateMtx(mtx, 0.0f, 45.0f, 0.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(littleTotalVertexCount, mtx);
			}
		}

		gUsingTransform = 0;
	}
	break; // saveBillboardOrGeometry

	case BLOCK_CAMPFIRE: // saveBillboardOrGeometry
	{
		facing = dataVal & 0x3;
		int lit = ((dataVal & 0x4) >> 2);

		int fireSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		int unburntSwatchLoc = fireSwatchLoc + 1;
		// unburnt or burnt look
		swatchLoc = unburntSwatchLoc + lit;

		gUsingTransform = 1;
		totalVertexCount = gModel.vertexCount;

		// bed of coals
		// 6x1x16 - and, amazingly, don't need a transform
		saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 1, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 5, 11, 0, 1, 0, 16);
		saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, unburntSwatchLoc, DIR_TOP_BIT, 0, 16, 2, 8);
		saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT, 0, 16, 2, 8);

		// logs - bottom two are unburnt
		for (i = 0; i < 2; i++) {
			littleTotalVertexCount = gModel.vertexCount;
			//saveBoxMultitileGeometry(boxIndex, type, dataVal, unburntSwatchLoc, unburntSwatchLoc, unburntSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 12, 16, 1 + (float)i * 10, 5 + (float)i * 10);
			saveBoxMultitileGeometry(boxIndex, type, dataVal, unburntSwatchLoc, unburntSwatchLoc, unburntSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 4, 8, 12, 0, 16);
			//saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, unburntSwatchLoc, 0x0, 0, 4, 8, 12);	// log caps
			saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, unburntSwatchLoc, 0x0, 0, 16, 12, 16);	// log sides
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, unburntSwatchLoc, 0x0, 0, 16, 12, 16);	// log top & bottom
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateMtx(mtx, ((float)i*10.0f + 1.0f) / 16.0f, -8.0f / 16.0f, 0.0f);
			transformVertices(littleTotalVertexCount, mtx);
		}

		// logs - top two are maybe burnt on sides
		for (i = 0; i < 2; i++) {
			littleTotalVertexCount = gModel.vertexCount;
			// even though the tile shows a burnt endcap, it's not used.
			saveBoxMultitileGeometry(boxIndex, type, dataVal, unburntSwatchLoc, unburntSwatchLoc, unburntSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 4, 8, 12, 0, 16);
			saveBoxReuseGeometryXFaces(boxIndex, type, dataVal, swatchLoc, 0x0, 0, 16, 12, 16);	// log sides
			saveBoxReuseGeometryYFaces(boxIndex, type, dataVal, unburntSwatchLoc, 0x0, 0, 16, 12, 16);	// log top & bottom
			littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
			identityMtx(mtx);
			translateToOriginMtx(mtx, boxIndex);
			translateMtx(mtx, ((float)i*10.0f + 1.0f) / 16.0f, -5.0f / 16.0f, 0.0f);
			rotateMtx(mtx, 0.0f, 90.0f, 0.0f);
			translateFromOriginMtx(mtx, boxIndex);
			transformVertices(littleTotalVertexCount, mtx);
		}

		if (!gPrint3D && lit) {
			// fire
			for (i = 0; i < 2; i++) {
				littleTotalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, BLOCK_BAMBOO, dataVal, fireSwatchLoc, fireSwatchLoc, fireSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT, FLIP_Z_FACE_VERTICALLY, 0, 16, 0, 16, 8, 8);
				littleTotalVertexCount = gModel.vertexCount - littleTotalVertexCount;
				identityMtx(mtx);
				translateToOriginMtx(mtx, boxIndex);
				// interestingly, flame leaps 1 pixel above the block's bounds
				translateMtx(mtx, 0.0f, 1.0f / 16.0f, 0.0f);
				rotateMtx(mtx, 0.0f, (float)i*90.0f + 45.0f, 0.0f);
				translateFromOriginMtx(mtx, boxIndex);
				transformVertices(littleTotalVertexCount, mtx);
			}
		}

		totalVertexCount = gModel.vertexCount - totalVertexCount;
		identityMtx(mtx);
		translateToOriginMtx(mtx, boxIndex);
		rotateMtx(mtx, 0.0f, (float)facing*90.0f + 90.0f, 0.0f);
		translateFromOriginMtx(mtx, boxIndex);
		transformVertices(totalVertexCount, mtx);

		gUsingTransform = 0;
	}
	break; // saveBillboardOrGeometry

	case BLOCK_SCAFFOLDING:						// saveBillboardOrGeometry
		{
			int bottom = (dataVal & 0x1);
			topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
			sideSwatchLoc = topSwatchLoc + 1;
			bottomSwatchLoc = topSwatchLoc + 2;

			for (i = 0; i <= bottom; i++) {
				gUsingTransform = i;
				totalVertexCount = gModel.vertexCount;
				saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, i-1, 0x0, 0x0, 0, 16, 14, 16, 0, 16);

				// now make four inner polygons to seal off the top - good times!
				saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 16, 14, 16, 14, 16);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0x0, 0, 16, 14, 16, 0, 2);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_HI_Z_BIT, 0x0, 14, 16, 14, 16, 0, 16);
				saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT, 0x0, 2, 16, 14, 16, 0, 16);

				if (i) {
					totalVertexCount = gModel.vertexCount - totalVertexCount;
					identityMtx(mtx);
					translateMtx(mtx, 0.0f, -14.0f / 16.0f, 0.0f);
					transformVertices(totalVertexCount, mtx);
				}
			}
			gUsingTransform = 0;

			// and the posts
			for (x = 0; x < 2; x++) {
				for (z = 0; z < 2; z++) {
					// make bottom 2x2 if there is no bottom
					saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, (bottom ? DIR_BOTTOM_BIT : 0x0) | DIR_TOP_BIT, 0x0, 0 + x * 14, 2 + x * 14, (float)bottom * 2, 14, 0 + z * 14, 2 + z * 14);
				}
			}
		}
		break; // saveBillboardOrGeometry

	case BLOCK_HONEY:
		topSwatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
		sideSwatchLoc = topSwatchLoc - 1;
		bottomSwatchLoc = topSwatchLoc - 2;

		saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, 0x0, 0x0, 0, 16, 0, 16, 0, 16);
		if (!gPrint3D)
		{
			// tasty honey inside
			saveBoxMultitileGeometry(boxIndex, type, dataVal, topSwatchLoc, sideSwatchLoc, bottomSwatchLoc, 0, 0x0, 0x0, 1, 15, 1, 15, 1, 15);
		}
		break; // saveBillboardOrGeometry

	case BLOCK_NETHER_PORTAL:
		if (gPrint3D) {
			// maybe too thin (4 blocks) to print safely
			if (dataVal == 1) {
				// east-west
				saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 0, 16, 0, 16, 6 - fatten, 10 + fatten);
			}
			else {
				// north-south
				saveBoxGeometry(boxIndex, type, dataVal, 1, 0x0, 6 - fatten, 10 + fatten, 0, 16, 0, 16);
			}
		}
		// 4 is east-west, 8 is north-south
		if (dataVal == 0)
		{
			// pre 1.13, so figure out axis if we can
			// infer direction from surrounding neighbors
			// north/south?
			int ns = 0;
			int ew = 0;
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].origType;
			if ((neighborType == BLOCK_NETHER_PORTAL) || (neighborType == BLOCK_OBSIDIAN)) {
				ew += (neighborType == BLOCK_NETHER_PORTAL) ? 10 : 1;
			}
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].origType;
			if ((neighborType == BLOCK_NETHER_PORTAL) || (neighborType == BLOCK_OBSIDIAN)) {
				ew += (neighborType == BLOCK_NETHER_PORTAL) ? 10 : 1;
			}
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].origType;
			if ((neighborType == BLOCK_NETHER_PORTAL) || (neighborType == BLOCK_OBSIDIAN)) {
				ns += (neighborType == BLOCK_NETHER_PORTAL) ? 10 : 1;
			}
			neighborType = gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].origType;
			if ((neighborType == BLOCK_NETHER_PORTAL) || (neighborType == BLOCK_OBSIDIAN)) {
				ns += (neighborType == BLOCK_NETHER_PORTAL) ? 10 : 1;
			}
			if (ns || ew) {
				// billboard output, so decide which wins, as best we can
				dataVal = (ns > ew) ? 2 : 1;
			}
			else {
				// can't really figure this portal out, it has *no* sensible neighbors.
				return 0;
			}
		}
		facing = (dataVal == 1) ? (DIR_LO_Z_BIT | DIR_HI_Z_BIT) : (DIR_LO_X_BIT | DIR_HI_X_BIT);

		// does the portal need sides? (happens if portal is chopped off by export box)
		if (gOptions->pEFD->chkBlockFacesAtBorders) {
			int wx, wy, wz;
			BOX_INDEX_TO_WORLD_XYZ(boxIndex, wx, wy, wz);
			if (dataVal == 1)
			{
				// east-west, so check east and west neighbors
				if (wx <= gSolidWorldBox.min[X]) {
					facing |= DIR_LO_X_BIT;
				}
				if (wx >= gSolidWorldBox.max[X]) {
					facing |= DIR_HI_X_BIT;
				}
			}
			else {
				// north-south
				if (wz <= gSolidWorldBox.min[Z]) {
					facing |= DIR_LO_Z_BIT;
				}
				if (wz >= gSolidWorldBox.max[Z]) {
					facing |= DIR_HI_Z_BIT;
				}
			}
			// does the portal need a top or bottom cap? Happens only if at limits
			if (wy >= gSolidWorldBox.max[Y]) {
				facing |= DIR_TOP_BIT;
			}
			if (wy <= gSolidWorldBox.min[Y]) {
				facing |= DIR_BOTTOM_BIT;
			}
		}
		// we now get the faces we *don't* want to output
		facing = ~facing & DIR_ALL_BITS;
		if (dataVal == 1) {
			// east-west
			saveBoxGeometry(boxIndex, type, dataVal, 1, facing, 0, 16, 0, 16, 6, 10);
		}
		else {
			// north-south
			saveBoxGeometry(boxIndex, type, dataVal, 1, facing, 6, 10, 0, 16, 0, 16);
		}

		break; // saveBillboardOrGeometry

	default:
        // something tagged as billboard or geometry, but no case here!
        assert(0);
        gMinorBlockCount--;
        return 0;
    }

    // this should always be set back to off after this method.
    assert(gUsingTransform == 0);

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
        retCode |= saveBoxFaceUVs( typeBelow, dataValBelow, DIRECTION_LO_X_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices );
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
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_Z, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x1;		    // xmin, ymin, zmax
        vindex[1] = 0x1|0x4;		// xmax, ymin, zmax
        vindex[2] = 0x1|0x4|0x2;	// xmax, ymax, zmax
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_HI_X_BIT:
        retCode |= saveBoxFaceUVs(typeBelow, dataValBelow, DIRECTION_HI_X_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices);
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
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_Z, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x1;		    // xmin, ymin, zmax
        vindex[1] = 0x1|0x4;		// xmax, ymin, zmax
        vindex[2] = 0x1|0x2;	    // xmin, ymax, zmax
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, boxIndexBelow, NULL );
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_Z, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_LO_Z_BIT:	// north side, going from north low to south high (i.e. sloping up to south)
        retCode |= saveBoxFaceUVs(typeBelow, dataValBelow, DIRECTION_LO_Z_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices);
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
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_X, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x4|0x1;		// zmax, ymin, xmax
        vindex[1] = 0x4;		    // zmin, ymin, xmax
        vindex[2] = 0x4|0x1|0x2;	// zmax, ymax, xmax
        setDefaultUVs( uvs, 2 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    case DIR_HI_Z_BIT:
        retCode |= saveBoxFaceUVs(typeBelow, dataValBelow, DIRECTION_HI_Z_HI_Y, 1, startVertexIndex, vindex, uvSlopeIndices);
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
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_LO_X, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        vindex[0] = 0x4|0x1;		// zmax, ymin, xmax
        vindex[1] = 0x4;		    // zmin, ymin, xmax
        vindex[2] = 0x4|0x2;	    // zmin, ymax, xmax
        setDefaultUVs( uvs, 3 );
        swatchLoc = getSwatch( typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, boxIndexBelow, NULL );
        retCode |= saveTriangleFace(boxIndex, swatchLoc, typeBelow, dataValBelow, DIRECTION_BLOCK_SIDE_HI_X, startVertexIndex, vindex, uvs);
        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

        break;
    default:
        assert(0);
    }

    gModel.triangleCount += 2;

    return retCode;
}

static unsigned int getStairMask(int boxIndex, int dataVal)
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
    const StairsTable stairs[4] = {
        { 10, DIRECTION_BLOCK_SIDE_HI_X, { 0, 0, 8, 2 }, { 0, 0, DIRECTION_BLOCK_SIDE_LO_Z, DIRECTION_BLOCK_SIDE_HI_Z }, { 0, 0, 14, 11 } },
        { 5, DIRECTION_BLOCK_SIDE_LO_X, { 0, 0, 4, 1 }, { 0, 0, DIRECTION_BLOCK_SIDE_LO_Z, DIRECTION_BLOCK_SIDE_HI_Z }, { 0, 0, 13, 7 } },
        { 12, DIRECTION_BLOCK_SIDE_HI_Z, { 8, 4, 0, 0 }, { DIRECTION_BLOCK_SIDE_LO_X, DIRECTION_BLOCK_SIDE_HI_X, 0, 0 }, { 14, 13, 0, 0 } },
        { 3, DIRECTION_BLOCK_SIDE_LO_Z, { 2, 1, 0, 0 }, { DIRECTION_BLOCK_SIDE_LO_X, DIRECTION_BLOCK_SIDE_HI_X, 0, 0 }, { 11, 7, 0, 0 } }
    };

    // The bottom 2 bits is direction of step.
    int stepDir = (dataVal & 0x3);
    int stepLevel = (dataVal & 0x4);
    unsigned int stepMask = stairs[stepDir].mask;
    bool sideNeighbor;
    unsigned int newMask;
    int neighborDataVal;

    int neighborIndex = boxIndex + gFaceOffset[stairs[stepDir].backDir];
    int neighborType = gBoxData[neighborIndex].origType;
    // is there a stairs behind us that subtracted a block?
    bool subtractedBlock = false;
    if (gBlockDefinitions[neighborType].flags & BLF_STAIRS)
    {
        // get the data value and check it
        neighborDataVal = gBoxData[neighborIndex].data;

        // first, are slabs on same level?
        if ((neighborDataVal & 0x4) == stepLevel)
        {
            // On the same level. Is neighbor value one of the values that can affect this block?
            newMask = stairs[stepDir].behind[(neighborDataVal & 0x3)];
            if (newMask != 0x0)
            {
                // The behind value indeed affects the step. Now we need to check if the corresponding
                // neighbor to the side is a stairs, at the same level, and has the same orientation.
                // If so, we ignore subtraction, else allow it. Basically, steps next to steps keep
                // the step's upper step "in place" without subtraction.
                sideNeighbor = false;
                neighborIndex = boxIndex + gFaceOffset[stairs[stepDir].sideDir[(neighborDataVal & 0x3)]];
                assert(neighborIndex != boxIndex);
                neighborType = gBoxData[neighborIndex].origType;
                // is there a stairs to the key side of us?
                if (gBlockDefinitions[neighborType].flags & BLF_STAIRS)
                {
                    // get the data value and check it
                    neighborDataVal = gBoxData[neighborIndex].data;

                    // first, are slabs on same level?
                    if ((neighborDataVal & 0x4) == stepLevel)
                    {
                        // On the same level. Is neighbor value the same as the block's value, i.e. are the
                        // stairs facing the same direction?
                        if ((neighborDataVal & 0x3) == stepDir)
                        {
                            // so, this stairs hold the stair step in place, no subtraction.
                            sideNeighbor = true;
                        }
                    }
                }

                if (!sideNeighbor)
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
    if (!subtractedBlock)
    {
        // now check the neighbor in front, in a similar manner.
        neighborIndex = boxIndex + gFaceOffset[(stairs[stepDir].backDir + 3) % 6];
        neighborType = gBoxData[neighborIndex].origType;
        // is there a stairs in front of us?
        if (gBlockDefinitions[neighborType].flags & BLF_STAIRS)
        {
            // get the data value and check it
            neighborDataVal = gBoxData[neighborIndex].data;

            // first, are slabs on same level?
            if ((neighborDataVal & 0x4) == stepLevel)
            {
                // On the same level. Is neighbor value one of the values that can affect this block?
                newMask = stairs[stepDir].front[(neighborDataVal & 0x3)];
                if (newMask != 0x0)
                {
                    // The front value indeed affects the step. Now we need to check if the corresponding
                    // neighbor to the side is a stairs, at the same level, and has the same orientation.
                    // If so, we ignore addition, else allow it. Basically, steps next to steps keep
                    // the step's upper step "in place" without addition.
                    sideNeighbor = false;
                    neighborIndex = boxIndex + gFaceOffset[(stairs[stepDir].sideDir[(neighborDataVal & 0x3)] + 3) % 6];
                    assert(neighborIndex != boxIndex);
                    neighborType = gBoxData[neighborIndex].origType;
                    // is there a stairs to the key side of us?
                    if (gBlockDefinitions[neighborType].flags & BLF_STAIRS)
                    {
                        // get the data value and check it
                        neighborDataVal = gBoxData[neighborIndex].data;

                        // first, are slabs on same level?
                        if ((neighborDataVal & 0x4) == stepLevel)
                        {
                            // On the same level. Is neighbor value the same as the block's value, i.e. are the
                            // stairs facing the same direction?
                            if ((neighborDataVal & 0x3) == stepDir)
                            {
                                // so, this stairs hold the stair step in place, no subtraction.
                                sideNeighbor = true;
                            }
                        }
                    }

                    if (!sideNeighbor)
                    {
                        // No side neighbor holding the step in place, so set this as the new mask, and we're done;
                        // subtraction takes precedence over addition.
                        stepMask = newMask;
                    }
                }
            }
        }
    }
    return stepMask;
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
	if (gModel.faceRecordPool->count >= FACE_RECORD_POOL_SIZE)
	{
		// allocate new pool
		FaceRecordPool* pFRP = (FaceRecordPool*)malloc(sizeof(FaceRecordPool));
		if (pFRP) {
			pFRP->count = 0;
			pFRP->pPrev = gModel.faceRecordPool;
			gModel.faceRecordPool = pFRP;
		}
		else {
			// out of memory! Not sure what to do...
			assert(pFRP);
		}
    }
    return &(gModel.faceRecordPool->fr[gModel.faceRecordPool->count++]);
}

static unsigned short getSignificantMaterial(int type, int dataVal)
{
    // Return only the "material-related" bits of the data value. That is, only those bits
    // that differentiate the various sub-materials. So, dirt has all low bits returned
    // (might as well account for future data values 0x3F - bit 0x40 is waterlogged, bit 0x80 is the extended object bit), ladders have none, since the
    // bits all affect orientation and not material itself.

    // special cases
    switch (type) {
    case BLOCK_QUARTZ_BLOCK:
        // make the pillar quartzs, 2-4, all have the same value
        if (dataVal > 2)
            dataVal = 2;
        break;
    case BLOCK_HUGE_RED_MUSHROOM:
    case BLOCK_HUGE_BROWN_MUSHROOM:
        // 0 - pores are fine as is
        // 1 - cap and filling; 2-9 and 14 go to 1
        // 10 - stem; 15 goes to 10
        if (((dataVal >= 2) && (dataVal <= 9)) || (dataVal == 14))
            dataVal = 1;
        else if (dataVal == 15)
            dataVal = 10;
        break;
	// not needed - is now part of the mask
    //case BLOCK_HEAD:
    //    // bits 654 are actually the head
    //    dataVal = (dataVal >> 4) & 0x7;
    //    break;
    }
    return (unsigned short)(gBlockDefinitions[type].subtype_mask & dataVal);
}

// Output the face of triangle slope element
static int saveTriangleFace( int boxIndex, int swatchLoc, int type, int dataVal, int faceDirection, int startVertexIndex, int vindex[3], Point2 uvs[3] )
{
    FaceRecord *face;
    int retCode = MW_NO_ERROR;
    float rect[4];

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
		int uvIndices[3];
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
        face->materialType = (short)type;
        face->materialDataVal = getSignificantMaterial(type, dataVal);

        // always the same normal, which directly corresponds to the normals[] array in gModel
        face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : (short)faceDirection;

        // get three face indices for the three corners of the triangular face, and always create each
        for ( int j = 0; j < 3; j++ )
        {
            face->vertexIndex[j] = startVertexIndex + vindex[j];
            if (gExportTexture)
                face->uvIndex[j] = (short)uvIndices[j];
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
static void saveBlockGeometry( int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ )
{
    int swatchLocSet[6];
    int i;
    for ( i = 0; i < 6; i++ )
    {
        swatchLocSet[i] = getSwatch(type,dataVal,i,boxIndex,NULL);
    }
    saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, markFirstFace, faceMask, 0, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ);
}

static void saveBoxGeometry(int boxIndex, int type, int dataVal, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
{
    int swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

    saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, markFirstFace, faceMask, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ);
}

// With this one we specify the swatch location explicitly, vs. get it from the type - e.g., cobblestone vs. moss stone for cobblestone walls
static void saveBoxTileGeometry(int boxIndex, int type, int dataVal, int swatchLoc, int markFirstFace, int faceMask, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
{
    saveBoxMultitileGeometry( boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, markFirstFace, faceMask, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}


static void saveBoxMultitileGeometry(int boxIndex, int type, int dataVal, int topSwatchLoc, int sideSwatchLoc, int bottomSwatchLoc, int markFirstFace, int faceMask, int rotUVs, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
{
    int swatchLocSet[6];
    swatchLocSet[DIRECTION_BLOCK_TOP] = topSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_BOTTOM] = bottomSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] = sideSwatchLoc;
    swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = sideSwatchLoc;
    saveBoxAlltileGeometry( boxIndex, type, dataVal, swatchLocSet, markFirstFace, faceMask, rotUVs, 0, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ );
}

// Sane way to set the X pair of faces with a part of a tile. You will reuse geometry (see BLOCK_GRINDSTONE for setting the geometry). You then
// call this similarly to saveBoxReuseGeometry, but you only need to specify the U and V range on the texture tile - it then gets applied to both X faces (or one, if you use faceMask to mask one out).
static void saveBoxReuseGeometryXFaces(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, float umin, float umax, float vmin, float vmax)
{
	saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_BOTTOM_BIT | DIR_TOP_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | faceMask, FLIP_X_FACE_VERTICALLY, 0, 0, vmin, vmax, 16 - umax, 16 - umin);
}
// Sane way to set the top and bottom pair of faces with a part of a tile. You will reuse geometry (see BLOCK_GRINDSTONE for setting the geometry). You then
// call this similarly to saveBoxReuseGeometry, but you only need to specify the U and V range on the texture tile - it then gets applied to both Y faces (or one, if you use faceMask to mask one out).
static void saveBoxReuseGeometryYFaces(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, float umin, float umax, float vmin, float vmax)
{
	saveBoxReuseGeometry(boxIndex, type, dataVal, swatchLoc, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | faceMask, REVOLVE_INDICES, umin, umax, 0, 0, 16 - vmax, 16 - vmin);
}

static void saveBoxReuseGeometry(int boxIndex, int type, int dataVal, int swatchLoc, int faceMask, int rotUVs, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
{
    int swatchLocSet[6];
#ifdef _DEBUG
	if (gAssertFacesNotReusedMask & (~faceMask & 0x3f)) {
		// some face was already output and is being output again!
		assert(0);
	}
	// note which faces are being output
	gAssertFacesNotReusedMask |= (~faceMask & 0x3f);
#endif
    swatchLocSet[DIRECTION_BLOCK_TOP] =
        swatchLocSet[DIRECTION_BLOCK_BOTTOM] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_X] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_X] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_LO_Z] =
        swatchLocSet[DIRECTION_BLOCK_SIDE_HI_Z] = swatchLoc;
    saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0, faceMask, rotUVs, 1, minPixX, maxPixX, minPixY, maxPixY, minPixZ, maxPixZ);
}

// rotUVs == FLIP_X_FACE_VERTICALLY means vertically flip X face
// rotUVs == FLIP_Z_FACE_VERTICALLY means vertically flip Z face
// rotUVs == ROTATE_TOP_AND_BOTTOM means rotate top and bottom tile 90 degrees; for glass panes.
static int saveBoxAlltileGeometry(int boxIndex, int type, int dataVal, int swatchLocSet[6], int markFirstFace, int faceMask, int rotUVs, int reuseVerts, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
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

#ifdef _DEBUG
	// note which faces are being output
	gAssertFacesNotReusedMask = (~faceMask & 0x3f);
#endif

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
        fminx = minPixX / 16.0f;
        fmaxx = maxPixX / 16.0f;
        fminy = minPixY / 16.0f;
        fmaxy = maxPixY / 16.0f;
        fminz = minPixZ / 16.0f;
        fmaxz = maxPixZ / 16.0f;
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
            float rect[4];
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
                    vindex[0] = 0x2|0x1;	// zmax, ymax
                    vindex[1] = 0x2;		// zmin, ymax
                    vindex[2] = 0;			// zmin, ymin
                    vindex[3] = 0x1;		// zmax, ymin
					useRotUVs = (rotUVs&ROTATE_X_FACE_90) ? 3 : 0;
					// rotate 90 or 180 - used for glass
                    // when rotUVs are used currently, also reverse the loop
                    if (rotUVs&FLIP_X_FACE_VERTICALLY)
                    {
                        // to mirror the face, use the same coordinates as the HI_X face
                        minu = (float)(16.0f - maxPixZ) / 16.0f;
                        maxu = (float)(16.0f - minPixZ) / 16.0f;
                        minv = (float)minPixY / 16.0f;
                        maxv = (float)maxPixY / 16.0f;
                        useRotUVs += 2;
                        reverseLoop = 1;
                    }
                    else
                    {
                        minu = (float)minPixZ / 16.0f;
                        maxu = (float)maxPixZ / 16.0f;
                        minv = (float)minPixY / 16.0f;
                        maxv = (float)maxPixY / 16.0f;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X:	// CCW
                    vindex[0] = 0x4|0x2;		// zmax, ymax
					vindex[1] = 0x4 | 0x2 | 0x1;// zmin, ymax
					vindex[2] = 0x4 | 0x1;		// zmin, ymin
					vindex[3] = 0x4;			// zmax, ymin
					useRotUVs = (rotUVs&ROTATE_X_FACE_90) ? 1 : 0;
					// normal case
					// On the hi X face, the Z direction is negative, so we negate
					minu = (float)(16.0f - maxPixZ) / 16.0f;
					maxu = (float)(16.0f - minPixZ) / 16.0f;
					minv = (float)minPixY / 16.0f;
					maxv = (float)maxPixY / 16.0f;
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
                        useRotUVs += 2;
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
                    // the REVOLVE_INDICES wants rotUVs to happen, but not the coordinate swap - used for chest latches
                    useRotUVs = (rotUVs&(ROTATE_TOP_AND_BOTTOM | REVOLVE_INDICES)) ? 1 : 0;
                    if (rotUVs&ROTATE_TOP_AND_BOTTOM)
                    {
                        // rotate 90 as far as bounds go
                        // used for glass pane and nothing else.
                        // NOTE: may need some "(16.0f-" adjustment
                        // similar to below if the object rendered
                        // is NOT symmetric around the center 8,8
                        minu = (float)minPixZ/16.0f;
                        maxu = (float)maxPixZ/16.0f;
						minv = (float)minPixX / 16.0f;
						maxv = (float)maxPixX / 16.0f;
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
                    useRotUVs = (rotUVs&(ROTATE_TOP_AND_BOTTOM|REVOLVE_INDICES)) ? 1 : 0;
                    if (rotUVs&ROTATE_TOP_AND_BOTTOM)
                    {
                        // rotate 90 as far as bounds go
                        // used for glass pane and nothing else.
                        // NOTE: may need some "(16.0f-" adjustment
                        // similar to below if the object rendered
                        // is NOT symmetric around the center 8,8
                        minu = (float)minPixZ/16.0f;
                        maxu = (float)maxPixZ/16.0f;
						minv = (float)minPixX / 16.0f;
						maxv = (float)maxPixX / 16.0f;
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

                // check flattening flags
                int special = 0;
                if ( gBoxData[boxIndex].flatFlags )
                {
                    switch ( faceDirection )
                    {
                    case DIRECTION_BLOCK_TOP:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_ABOVE )
                        {
                            special = 1;
                        }
                        break;
                    case DIRECTION_BLOCK_BOTTOM:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_BELOW )
                        {
                            special = 1;
                        }
                        break;
                    case DIRECTION_BLOCK_SIDE_LO_X:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_X )
                        {
                            special = 1;
                        }
                        break;
                    case DIRECTION_BLOCK_SIDE_HI_X:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_X )
                        {
                            special = 1;
                        }
                        break;
                    case DIRECTION_BLOCK_SIDE_LO_Z:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_Z )
                        {
                            special = 1;
                        }
                        break;
                    case DIRECTION_BLOCK_SIDE_HI_Z:
                        if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_Z )
                        {
                            special = 1;
                        }
                        break;
                    default:
                        // only direction left is down, and nothing gets merged with those faces
                        break;
                    }
                }
                if ( special )
                {
                    // Minor geometric object with face that has a flat on it.
                    if ( (minu == 0.0f)&&(minv == 0.0f)&&(maxu == 1.0f)&&(maxv == 1.0f) )
                    {
                        // a bit wasteful: vertices are set separately here in the grid
                        retCode |= saveVertices( boxIndex, faceDirection, anchor );
                        if ( retCode >= MW_BEGIN_ERRORS ) return retCode;
                        saveFaceLoop( boxIndex, faceDirection, NULL, NULL, markFirstFace );

                        // face output, so don't need to mark first face
                        markFirstFace = 0;
                    }
                    else
                    {
                        // For some reason, face is not a full-sized face, yet it has a flat getting
                        // applied to it. This can happen in rare circumstances, such as a torch on top of a fence post.
                        // TODO. Ignore the flat and export just the face normally without any composite.
                        // To fix we'd have to figure out a new composite, special UVs, etc. - sounds like work.
                        //assert(0);
                        goto Normal;
                    }
                }
                else
                {
                    Normal:
                    // mark the first face if calling routine wants it, and this is the first face of six
                    retCode |= saveBoxFace( swatchLoc, type, dataVal, faceDirection, markFirstFace, startVertexIndex, vindex, reverseLoop, useRotUVs, minu, maxu, minv, maxv );
                    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                    // face output, so don't need to mark first face
                    markFirstFace = 0;
                }
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
static int findFaceDimensions(float rect[4], int faceDirection, float minPixX, float maxPixX, float minPixY, float maxPixY, float minPixZ, float maxPixZ)
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
static int lesserNeighborCoversRectangle( int faceDirection, int boxIndex, float rect[4] )
{
    int type, neighborType, neighborBoxIndex;
    float neighborRect[4];

    if ( gOptions->exportFlags & EXPT_GROUP_BY_BLOCK )
    {
        // mode where every block is output regardless of neighbors, so return false
        return 0;
    }

    // If we are maintaining borders, we need to ignore neighbors above and below that are in air space.
    neighborBoxIndex = boxIndex + gFaceOffset[faceDirection];
    neighborType = gBoxData[neighborBoxIndex].type;
    // super-quick out: is neighbor air? Common enough case, let's test for it.
    if (neighborType == BLOCK_AIR)
        return 0;

    // If we are sealing borders, we haven't yet clears "air" blocks above and below, as these are needed sometimes for
    // two-block-high plants, doors, etc. But, we should ignore them for coverage tess.
    if (gOptions->pEFD->chkBlockFacesAtBorders) {
        if ((faceDirection == DIRECTION_BLOCK_TOP) || (faceDirection == DIRECTION_BLOCK_BOTTOM)) {
            IPoint anchor;
            boxIndexToLoc(anchor, neighborBoxIndex);
            if (faceDirection == DIRECTION_BLOCK_TOP) {
                // limit above
                if (anchor[Y] == gAirBox.max[Y])
                    return 0;
            }
            else {
                // below
                if (anchor[Y] == gAirBox.min[Y])
                    return 0;
            }
        }
    }

    // check for easy case: if neighbor is a full block, neighbor covers all, so return 1
    // (or, for printing, return 1 if the block being covered *exactly* matches)
    type = gBoxData[boxIndex].type;
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
            if ((gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) &&
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
static int getFaceRect( int faceDirection, int boxIndex, int view3D, float faceRect[4] )
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
            float setBottom = 0;
            // The idea here is that setTop is set by the various minor blocks below
            float setTop = 0;
            // a minor block exists, so check its coverage given the face direction
            switch ( origType )
            {
            case BLOCK_STONE_SLAB:
            case BLOCK_WOODEN_SLAB:
            case BLOCK_RED_SANDSTONE_SLAB:
            case BLOCK_PURPUR_SLAB:
			case BLOCK_ANDESITE_SLAB:
				// The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
                // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
                if (dataVal & 0x8)
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
            case BLOCK_PURPUR_STAIRS:
			case BLOCK_PRISMARINE_STAIRS:
			case BLOCK_PRISMARINE_BRICK_STAIRS:
			case BLOCK_DARK_PRISMARINE_STAIRS:
				// TODO: Right now stairs are dumb: only the large rectangle of the base is returned.
                // Returning the little block, which can further be trimmed to a cube, is a PAIN.
                // This does mean the little stair block sides won't be deleted. Ah well.
                // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
                // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
                
                if ( dataVal & 0x4 )
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
                setTop = 2 * (1 + (float)(dataVal&0x7));
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
            case BLOCK_REDSTONE_COMPARATOR:
            case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
                // annoyingly, repeaters undergo transforms, so repeaters next to each other won't clear faces on the sides for each other...
                setTop = 2;
                break;

            case BLOCK_DAYLIGHT_SENSOR:
            case BLOCK_INVERTED_DAYLIGHT_SENSOR:
                setTop = 6;
                break;

			case BLOCK_STONECUTTER:
				setTop = 9;
				break;

			case BLOCK_ENCHANTING_TABLE:
				setTop = 12;
				break;

			case BLOCK_HOPPER:
				// blocks bottom
				setTop = 16;
				setBottom = 10;
				break;

			case BLOCK_TRAPDOOR:
            case BLOCK_IRON_TRAPDOOR:
			case BLOCK_SPRUCE_TRAPDOOR:
			case BLOCK_BIRCH_TRAPDOOR:
			case BLOCK_JUNGLE_TRAPDOOR:
			case BLOCK_ACACIA_TRAPDOOR:
			case BLOCK_DARK_OAK_TRAPDOOR:
				if ( !(dataVal & 0x4) )
                {
                    // trapdoor is flat on ground
                    setTop = 3;
                }
                break;

			case BLOCK_COMPOSTER:
				// fills the block, and if something is above it, it will cover that, too.
				setTop = 16;
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
static int saveBoxFace( int swatchLoc, int type, int dataVal, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int reverseLoop, int rotUVs, float minu, float maxu, float minv, float maxv )
{
	int retCode = MW_NO_ERROR;
	int uvIndices[4];
	int orderedUvIndices[4];

    if ( gExportTexture )
    {
        // output each face
		// get the four UV texture vertices, stored by swatch type
        saveRectangleTextureUVs( swatchLoc, type, minu, maxu, minv, maxv, uvIndices );

        // get four face indices for the four corners of the face, and always create each
        for ( int j = 0; j < 4; j++ )
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

    retCode |= saveBoxFaceUVs( type, dataVal, faceDirection, markFirstFace, startVertexIndex, vindex, orderedUvIndices );
    return retCode;
}

static int saveBoxFaceUVs( int type, int dataVal, int faceDirection, int markFirstFace, int startVertexIndex, int vindex[4], int uvIndices[4] )
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
    face->materialType = (short)type;
    face->materialDataVal = getSignificantMaterial(type, dataVal);

    // always the same normal, which directly corresponds to the normals[] array in gModel
    face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : (short)faceDirection;

    // get four face indices for the four corners of the face, and always create each
    for ( j = 0; j < 4; j++ )
    {
        face->vertexIndex[j] = startVertexIndex + vindex[j];
        if ( gExportTexture )
            face->uvIndex[j] = (short)uvIndices[j];
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

static int saveBillboardFacesExtraData(int boxIndex, int type, int billboardType, int dataVal, int firstFace, bool dontWobbleOverride /*= false*/ )
{
	int i, j, fc, swatchLoc;
	FaceRecord *face;
	int faceDir[2 * 5];			// vines need 5 total
	Point vertexOffsets[5][4];	// vines need 5 total, if one is under the block
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
	int matchType;
	bool vineUnderBlock = false;
	bool redstoneWireOnBottom = false;
	int faceDirection;
	float distanceOffset = ONE_PIXEL;
	float mtx[4][4];
	int redstoneDirFlags = 0x0;
	bool redstoneOn = true;
	int redstoneOrigDataVal = 0;
	float angle = 0.0f;
	int metaVertexCount = 0;
	float shiftX = 0.0f;
	float shiftZ = 0.0f;
	bool wobbleIt = false;
	int origDataVal = dataVal;

	assert(!gPrint3D);

	swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);

	// some types use data values for which billboard to use
	switch (type)
	{
	case BLOCK_SAPLING:				// saveBillboardFacesExtraData
		// The 0x8 bit functions as the counter. The counter is cleared when a sapling is dropped as an item.
		switch (dataVal & 0x7)
		{
		default:
			assert(0);
		case 0: // OAK sapling
			// set OK already
			break;
		case 1:
			// spruce sapling
			swatchLoc = SWATCH_INDEX(15, 3);
			break;
		case 2:
			// birch sapling
			swatchLoc = SWATCH_INDEX(15, 4);
			break;
		case 3:
			// jungle sapling
			swatchLoc = SWATCH_INDEX(14, 1);
			break;
		case 4:
			// acacia sapling
			swatchLoc = SWATCH_INDEX(14, 18);
			break;
		case 5:
			// dark oak sapling
			swatchLoc = SWATCH_INDEX(15, 18);
			break;
		case 6:
			// bamboo
			swatchLoc = SWATCH_INDEX(9, 37);
			wobbleIt = true;
			break;
		}
		break;
	case BLOCK_GRASS:				// saveBillboardFacesExtraData
		switch (dataVal & 0x3)
		{
		case 0:
			// dead bush appearance
			swatchLoc = SWATCH_INDEX(7, 3);
			// surprisingly, wobbleIt is false!
			break;
		case 1:
		default:
			// set OK already
			wobbleIt = true;
			break;
		case 2:
			// fern
			swatchLoc = SWATCH_INDEX(8, 3);
			wobbleIt = true;
			break;
		}
		break;
	case BLOCK_TORCH:				// saveBillboardFacesExtraData
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
	case BLOCK_CROPS:				// saveBillboardFacesExtraData
		// adjust for growth
		// undocumented: village-grown wheat appears to have
		// the 0x8 bit set, which doesn't seem to matter. Mask it out.
		swatchLoc += ((dataVal & 0x7) - 7);
		break;
	case BLOCK_CARROTS:				// saveBillboardFacesExtraData
	case BLOCK_POTATOES:
		switch (dataVal & 0x7)
		{
		case 0:
		case 1:
			swatchLoc -= 3;
			break;
		case 2:
		case 3:
			swatchLoc -= 2;
			break;
		case 4:
		case 5:
		case 6:
			swatchLoc--;
			break;
		case 7:
		default:
			break;
		}
		break;
	case BLOCK_BEETROOT_SEEDS:				// saveBillboardFacesExtraData
		// mask out high bits just in case
		swatchLoc += (dataVal & 0x3) - 3;
		break;
	case BLOCK_NETHER_WART:				// saveBillboardFacesExtraData
		if (dataVal == 0)
		{
			swatchLoc -= 2;
		}
		else if (dataVal <= 2)
		{
			swatchLoc--;
		}
		break;
	case BLOCK_PUMPKIN_STEM:				// saveBillboardFacesExtraData
	case BLOCK_MELON_STEM:
		// offset about height of stem - 1 extra down for farmland shift
		height = ((float)dataVal*2.0f - 15.0f) / 16.0f;
		// the tricky bit is rotating the stem to a reasonable pumpkin,
		// which we do as a separate piece
		if (dataVal == 7)
		{
			// fully mature, change height to 10 if the proper fruit is next door
			matchType = (type == BLOCK_PUMPKIN_STEM) ? BLOCK_PUMPKIN : BLOCK_MELON;
			if ((gBoxData[boxIndex - gBoxSizeYZ].type == matchType) ||
				(gBoxData[boxIndex + gBoxSizeYZ].type == matchType) ||
				(gBoxData[boxIndex - gBoxSize[Y]].type == matchType) ||
				(gBoxData[boxIndex + gBoxSize[Y]].type == matchType)) {
				height = -9.0 / 16.0f;
			}
		}
		break;

	case BLOCK_POWERED_RAIL:				// saveBillboardFacesExtraData
	case BLOCK_DETECTOR_RAIL:
	case BLOCK_ACTIVATOR_RAIL:
		switch (type)
		{
		case BLOCK_POWERED_RAIL:
			if (!(dataVal & 0x8))
			{
				// unpowered rail
				swatchLoc = SWATCH_INDEX(3, 10);
			}
			break;
		case BLOCK_DETECTOR_RAIL:
			// by default, the detector rail is in its undetected state
			if (dataVal & 0x8)
			{
				// rail detector activated (same tile in basic game)
				swatchLoc = SWATCH_INDEX(11, 17);
			}
			break;
		case BLOCK_ACTIVATOR_RAIL:
			// by default, unactivated
			if (dataVal & 0x8)
			{
				// activated rail
				swatchLoc = SWATCH_INDEX(9, 17);
			}
			break;
		}
		// if not a normal rail, there are no curve bits, so mask off upper bit, which is
		// whether the rail is powered or not.
		dataVal &= 0x7;
		// fall through:
	case BLOCK_RAIL:				// saveBillboardFacesExtraData
		if (CHECK_COMPOSITE_OVERLAY) {
			switch (dataVal & 0x7)
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
		}
		else {
			// if a curved rail, change swatch
			if (type == BLOCK_RAIL) {
				switch (dataVal & 0xf)
				{
				case 6:
				case 7:
				case 8:
				case 9:
					// curved
					swatchLoc = SWATCH_INDEX(0, 7);
					break;
				default:
					break;
				}
			}
		}
		break;
	case BLOCK_VINES:				// saveBillboardFacesExtraData
		if (CHECK_COMPOSITE_OVERLAY) {
			if (dataVal == 0) {
				// compositing, and the vine's only sitting underneath a block, so flattened
				return(0);
			}
		}
		else {
			// full check: is dataVal == 0 (means only a vine above) or is there a solid block above?
			if ((dataVal == 0) || (gBlockDefinitions[gBoxData[boxIndex + 1].type].flags & BLF_WHOLE)) {
				vineUnderBlock = true;
			}
		}
		break;
	case BLOCK_LADDER:				// saveBillboardFacesExtraData
		assert(!CHECK_COMPOSITE_OVERLAY);
		// we convert dataVal for ladder into the bit-wise conversion for vines
		switch (dataVal & 0x7)
		{
		case 2: // north, -Z
			dataVal = 0x1;
			break;
		case 3: // south, +Z
			dataVal = 0x4;
			break;
		case 4: // west, -X
			dataVal = 0x8;
			break;
		case 5: // east, +X
			dataVal = 0x2;
			break;
		default:
			assert(0);
			return 0;
		}
		break;
	case BLOCK_CACTUS:				// saveBillboardFacesExtraData
		// side faces are one higher
		swatchLoc++;
		break;
	case BLOCK_DANDELION:
		wobbleIt = true;
		break;
	case BLOCK_POPPY:				// saveBillboardFacesExtraData
		wobbleIt = true;
		if (dataVal > 0)
		{
			if (dataVal < 9) {
				// row 20 has these flowers; else poppy (12,0) is used
				swatchLoc = SWATCH_INDEX(dataVal - 1, 19);
			}
			else {
				// cornflower, lily of the valley, wither rose
				swatchLoc = SWATCH_INDEX(dataVal - 6, 37);
			}
		}
		break;
	case BLOCK_DOUBLE_FLOWER:				// saveBillboardFacesExtraData
		wobbleIt = true;
		if (dataVal >= 8)
		{
			// top half of plant, so need data value of block below
			// to know which sort of plant
			// (could be zero if block is missing, in which case it'll be a sunflower, which is fine)
			// row 19 (#18) has these
			// old code, before we shoved the dataVal from the bottom half (if available) into the top half, in extractChunk.
			// But, should work the same, so don't mess with it.
			swatchLoc = SWATCH_INDEX(gBoxData[boxIndex - 1].data * 2 + 3, 18);
			// for material differentiation set the dataVal to the bottom half
			origDataVal = gBoxData[boxIndex - 1].data;
			if (gBoxData[boxIndex - 1].data == 0)
			{
				foundSunflowerTop = 1;
			}
		}
		else
		{
			// bottom half of plant, from row 19
			swatchLoc = SWATCH_INDEX(dataVal * 2 + 2, 18);
		}
		break;
	case BLOCK_TALL_SEAGRASS:				// saveBillboardFacesExtraData
		wobbleIt = true;
		if (dataVal >= 8)
		{
			// top half of plant
			swatchLoc = SWATCH_INDEX(15, 33);
			// for material differentiation set the dataVal to the bottom half
			origDataVal = gBoxData[boxIndex - 1].data;
		}
		//else
		//{
		//	// bottom half of plant, which is given
		//}
		break;
	case BLOCK_KELP:				// saveBillboardFacesExtraData
		if (dataVal > 0)
		{
			// subtract 1 if needed to get to "kelp", the top of the plant
			swatchLoc = SWATCH_INDEX(11 - dataVal, 33);
		}
		break;
	case BLOCK_CORAL:				// saveBillboardFacesExtraData
		if (dataVal > 0)
		{
			// add to get to tile
			swatchLoc = SWATCH_INDEX(11 + dataVal, 35);
		}
		break;
	case BLOCK_CORAL_FAN:				// saveBillboardFacesExtraData
	case BLOCK_CORAL_WALL_FAN:				// saveBillboardFacesExtraData
		if (dataVal > 0)
		{
			// add to get to tile
			swatchLoc = SWATCH_INDEX((dataVal & 0x7), 36);
		}
		break;
	case BLOCK_DEAD_CORAL_FAN:				// saveBillboardFacesExtraData
	case BLOCK_DEAD_CORAL_WALL_FAN:				// saveBillboardFacesExtraData
		if (dataVal > 0)
		{
			// add to get to tile
			swatchLoc = SWATCH_INDEX(5 + (dataVal & 0x7), 36);
		}
		break;
	case BLOCK_DEAD_CORAL:				// saveBillboardFacesExtraData
		if (dataVal > 0)
		{
			// add to get to tile
			swatchLoc = SWATCH_INDEX(14 + (dataVal & 0x7), 36);
		}
		break;
	case BLOCK_REDSTONE_WIRE:				// saveBillboardFacesExtraData
		// Let the games begin. Wherever redstone has been found, it has set the
		// flat flags of its neighbors. Check each neighbor for the corresponding flat flag in the proper direction.
		// If set, then output redstone. Also, remove the flat flag, so it's not used later.

		// dataVal is used for the sides and which should be output. We derive it here.
		// The data value holds the direction flags in 0xf0, and the original data (power) value in 0x0f bits
		redstoneDirFlags = dataVal >> 4;
		redstoneOrigDataVal = dataVal & 0xf;
		redstoneOn = (dataVal & 0xf) ? true : false;
		// we reuse dataVal here for edges.
		dataVal = 0x0;
		distanceOffset = ONE_PIXEL * 0.25;

		// go through the six directions to the neighbors. For example, if direction is down, check if
		// the neighbor below has its "above" flat flag set.
		for (faceDirection = 0; faceDirection < 6; faceDirection++)
		{
			// get neighbor's flatFlags in that direction.
			int neighborBoxIndex = boxIndex + gFaceOffset[faceDirection];
			// is the corresponding flag set to point at the redstone?
			if (gBoxData[neighborBoxIndex].flatFlags & gFlagPointsTo[faceDirection]) {
				// yes it is - clear flat flag, and output redstone
				gBoxData[neighborBoxIndex].flatFlags &= ~gFlagPointsTo[faceDirection];

				// direction of neighbor
				switch (faceDirection)
				{
				case DIRECTION_BLOCK_TOP:
					// should never hit here: redstone doesn't go on the bottom faces of solid blocks
					assert(0);
					break;
				case DIRECTION_BLOCK_BOTTOM:
					redstoneWireOnBottom = 1;
					break;
					// all that follow are a wire up the side
				case DIRECTION_BLOCK_SIDE_LO_X:
					dataVal |= 0x2;
					break;
				case DIRECTION_BLOCK_SIDE_HI_X:
					dataVal |= 0x8;
					break;
				case DIRECTION_BLOCK_SIDE_LO_Z:
					dataVal |= 0x4;
					break;
				case DIRECTION_BLOCK_SIDE_HI_Z:
					dataVal |= 0x1;
					break;
				default:
					// only direction left is down, and nothing gets merged with those faces
					break;
				}
			}
		}
		// make sure something is getting output - it should
		assert(dataVal > 0 || redstoneWireOnBottom);
		if (dataVal > 0) {
			swatchLoc = redstoneOn ? REDSTONE_WIRE_VERT : REDSTONE_WIRE_VERT_OFF;
		}
		break;

	case BLOCK_SWEET_BERRY_BUSH:				// saveBillboardFacesExtraData
		// adjust for growth
		swatchLoc += (dataVal & 0x3);
		break;

	default:
		// perfectly fine to hit here, the billboard is generic
		break;
	}

	// may need to wobble it
	// If dontWobbleOverride is true, then we force wobbleIt to false;
	// this is for flower pots
	if (dontWobbleOverride) {
		wobbleIt = false;
	}
	if ( wobbleIt) {
		gUsingTransform = 1;
		metaVertexCount = gModel.vertexCount;

		wobbleObjectLocation(boxIndex, shiftX, shiftZ);
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

            Vec3Scalar( vertexOffsets[0][0], =, texelLow,   height, texelLow );
            Vec3Scalar( vertexOffsets[0][1], =, texelHigh,  height, texelHigh );
            Vec3Scalar( vertexOffsets[0][2], =, texelHigh,1+height, texelHigh );
            Vec3Scalar( vertexOffsets[0][3], =, texelLow, 1+height, texelLow );

            Vec3Scalar( vertexOffsets[1][0], = , texelLow,   height, texelHigh);
            Vec3Scalar( vertexOffsets[1][1], = , texelHigh,  height, texelLow);
            Vec3Scalar( vertexOffsets[1][2], = , texelHigh,1+height, texelLow );
            Vec3Scalar( vertexOffsets[1][3], = , texelLow, 1+height, texelHigh);
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
        // We keep this a "billboard" so that it's easy to add to comparators, repeaters, etc.
        // Nowadays we output actual geometry, with bleeding.
        break;
    case BB_RAILS:
        {
            float texelUp;

            // approximate width of billboard across block, eyeballing it.
            // add height offset if we're not compositing, so that tracks join properly
            texelUp = CHECK_COMPOSITE_OVERLAY ? 0.0f : ONE_PIXEL;
            faceCount = 2;

            // note that by the time we get here the upper bit has been masked off (on/off) for powered/detector rails
            switch ( dataVal & 0xf )
            {
            case 2: // ascend east +x
                // two paired billboards
                faceDir[0] = DIRECTION_LO_X_HI_Y;
                faceDir[1] = DIRECTION_HI_X_LO_Y;

                Vec3Scalar(vertexOffsets[0][0], = , 1.0f, 1.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][1], = , 1.0f, 1.0f + texelUp, 0.0f);
                Vec3Scalar(vertexOffsets[0][2], = , 0.0f, 0.0f + texelUp, 0.0f);
                Vec3Scalar(vertexOffsets[0][3], = , 0.0f, 0.0f + texelUp, 1.0f);
                break;
            case 3: // ascend west -x
                faceDir[0] = DIRECTION_HI_X_HI_Y;
                faceDir[1] = DIRECTION_LO_X_LO_Y;

                Vec3Scalar(vertexOffsets[0][0], = , 0.0f, 1.0f + texelUp, 0.0f);
                Vec3Scalar(vertexOffsets[0][1], = , 0.0f, 1.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][2], = , 1.0f, 0.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][3], = , 1.0f, 0.0f + texelUp, 0.0f);
                break;
            case 4: // ascend north -z
                faceDir[0] = DIRECTION_HI_Z_HI_Y;
                faceDir[1] = DIRECTION_LO_Z_LO_Y;

                Vec3Scalar(vertexOffsets[0][0], = , 0.0f, 0.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][1], = , 1.0f, 0.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][2], = , 1.0f, 1.0f + texelUp, 0.0f);
                Vec3Scalar(vertexOffsets[0][3], = , 0.0f, 1.0f + texelUp, 0.0f);
                break;
            case 5: // ascend south +z
                faceDir[0] = DIRECTION_LO_Z_HI_Y;
                faceDir[1] = DIRECTION_HI_Z_LO_Y;

                Vec3Scalar(vertexOffsets[0][0], = , 0.0f, 1.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][1], = , 1.0f, 1.0f + texelUp, 1.0f);
                Vec3Scalar(vertexOffsets[0][2], = , 1.0f, 0.0f + texelUp, 0.0f);
                Vec3Scalar(vertexOffsets[0][3], = , 0.0f, 0.0f + texelUp, 0.0f);
                break;
            default:
                // it's a flat
                if (!CHECK_COMPOSITE_OVERLAY) {
                    faceDir[0] = DIRECTION_BLOCK_TOP;
                    faceDir[1] = DIRECTION_BLOCK_BOTTOM;
                    switch (dataVal & 0xf)
                    {
                    default:
                        assert(0);
                    case 6:
                        // Curved rail connecting to the south and east.
                    case 0:
                        // north-south straight
                        Vec3Scalar(vertexOffsets[0][0], = , 0.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][1], = , 1.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][2], = , 1.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][3], = , 0.0f, texelUp, 0.0f);
                        break;

                    case 7:
                        // Curved rail connecting to the south and west.
                    case 1:
                        // east-west straight
                        Vec3Scalar(vertexOffsets[0][0], = , 0.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][1], = , 0.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][2], = , 1.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][3], = , 1.0f, texelUp, 0.0f);
                        break;

                    case 8:
                        // Curved rail connecting to the north and west.
                        Vec3Scalar(vertexOffsets[0][0], = , 1.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][1], = , 0.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][2], = , 0.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][3], = , 1.0f, texelUp, 1.0f);
                        break;

                    case 9:
                        // Curved rail connecting to the north and east.
                        Vec3Scalar(vertexOffsets[0][0], = , 1.0f, texelUp, 1.0f);
                        Vec3Scalar(vertexOffsets[0][1], = , 1.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][2], = , 0.0f, texelUp, 0.0f);
                        Vec3Scalar(vertexOffsets[0][3], = , 0.0f, texelUp, 1.0f);
                        break;
                    }
                } else
                    return(0);
            }
        }
        break;

    case BB_SIDE:
        {
            // vines and ladders

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
                    // side has a vine or ladder or redstone vertical, so put out billboard
                    switch ( sideCount )
                    {
                    case 0:
                        // south face +Z
                        inZdirection = 1;
                        inPositive = 1;
                        texelDist = 1.0f - distanceOffset;
                        break;
                    case 1:
                        // west face -X
                        inZdirection = 0;
                        inPositive = 0;
                        texelDist = distanceOffset;
                        break;
                    case 2:
                        // north face -Z
                        inZdirection = 1;
                        inPositive = 0;
                        texelDist = distanceOffset;
                        break;
                    case 3:
                        // east face +X
                        inZdirection = 0;
                        inPositive = 1;
                        texelDist = 1.0f - distanceOffset;
                        break;
                    default:
                        assert(0);
                    }
                    if ( inPositive )
                    {
                        faceDir[faceCount++] = inZdirection ? DIRECTION_BLOCK_SIDE_LO_Z : DIRECTION_BLOCK_SIDE_LO_X;
                        faceDir[faceCount++] = inZdirection ? DIRECTION_BLOCK_SIDE_HI_Z : DIRECTION_BLOCK_SIDE_HI_X;
                    }
                    else
                    {
                        faceDir[faceCount++] = inZdirection ? DIRECTION_BLOCK_SIDE_HI_Z : DIRECTION_BLOCK_SIDE_HI_X;
                        faceDir[faceCount++] = inZdirection ? DIRECTION_BLOCK_SIDE_LO_Z : DIRECTION_BLOCK_SIDE_LO_X;
                    }
                    assert(faceCount <= 10);

                    switch ( sideCount )
                    {
                    case 0:
                        // south face +Z
                        Vec3Scalar( vertexOffsets[billCount][0], =, 1.0f,0.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][1], =, 0.0f,0.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][2], =, 0.0f,1.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][3], =, 1.0f,1.0f,texelDist );
                        break;
                    case 1:
                        // west face -X
                        Vec3Scalar( vertexOffsets[billCount][0], =, texelDist,0.0f,1.0f );
                        Vec3Scalar( vertexOffsets[billCount][1], =, texelDist,0.0f,0.0f );
                        Vec3Scalar( vertexOffsets[billCount][2], =, texelDist,1.0f,0.0f );
                        Vec3Scalar( vertexOffsets[billCount][3], =, texelDist,1.0f,1.0f );
                        break;
                    case 2:
                        // north face -Z
                        Vec3Scalar( vertexOffsets[billCount][0], =, 0.0f,0.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][1], =, 1.0f,0.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][2], =, 1.0f,1.0f,texelDist );
                        Vec3Scalar( vertexOffsets[billCount][3], =, 0.0f,1.0f,texelDist );
                        break;
                    case 3:
                        // east face +X
                        Vec3Scalar( vertexOffsets[billCount][0], =, texelDist,0.0f,0.0f );
                        Vec3Scalar( vertexOffsets[billCount][1], =, texelDist,0.0f,1.0f );
                        Vec3Scalar( vertexOffsets[billCount][2], =, texelDist,1.0f,1.0f );
                        Vec3Scalar( vertexOffsets[billCount][3], =, texelDist,1.0f,0.0f );
                        break;
                    default:
                        assert(0);
                    }
                    billCount++;
                    assert(billCount <= 5);
                }

                sideCount++;
                dataVal = dataVal>>1;
            }
            if (vineUnderBlock) {
                // vine under block
                // two paired billboards
                faceDir[faceCount++] = DIRECTION_BLOCK_TOP;
                faceDir[faceCount++] = DIRECTION_BLOCK_BOTTOM;
                assert(faceCount <= 10);

                Vec3Scalar(vertexOffsets[billCount][0], = , 1.0f, 1.0f - ONE_PIXEL, 1.0f);
                Vec3Scalar(vertexOffsets[billCount][1], = , 1.0f, 1.0f - ONE_PIXEL, 0.0f);
                Vec3Scalar(vertexOffsets[billCount][2], = , 0.0f, 1.0f - ONE_PIXEL, 0.0f);
                Vec3Scalar(vertexOffsets[billCount][3], = , 0.0f, 1.0f - ONE_PIXEL, 1.0f);
                billCount++;
                assert(billCount <= 5);
                break;
            }
        }
        break;
    case BB_BOTTOM:
        // lily pad
        faceCount = 2;

        // two paired billboards
        faceDir[0] = DIRECTION_BLOCK_TOP;
        faceDir[1] = DIRECTION_BLOCK_BOTTOM;

		// distanceOffset is used for silly case https://www.reddit.com/r/Minecraft/comments/c9r6qd/a_new_building_trick_that_might_come_in_handy
		// in which the lily pad is actually on top of a slab
        Vec3Scalar(vertexOffsets[0][0], = , 1.0f, distanceOffset, 1.0f);
        Vec3Scalar(vertexOffsets[0][1], = , 1.0f, distanceOffset, 0.0f);
        Vec3Scalar(vertexOffsets[0][2], = , 0.0f, distanceOffset, 0.0f);
        Vec3Scalar(vertexOffsets[0][3], = , 0.0f, distanceOffset, 1.0f);
        break;

	// corals
	case BB_FAN:
		{
			float texelCenter, texelExtent, texelHigh;
			texelCenter = 0.5f;
			texelExtent = 0.924f;
			texelHigh = 0.4f;
			faceCount = 8;  // really, 4
							// two paired billboards
			faceDir[0] = DIRECTION_UP_LO_X;	// TODO!!!
			faceDir[1] = DIRECTION_DN_HI_X;
			faceDir[2] = DIRECTION_UP_HI_X;
			faceDir[3] = DIRECTION_DN_LO_X;
			faceDir[4] = DIRECTION_UP_LO_Z;
			faceDir[5] = DIRECTION_DN_HI_Z;
			faceDir[6] = DIRECTION_UP_HI_Z;
			faceDir[7] = DIRECTION_DN_LO_Z;

			Vec3Scalar(vertexOffsets[0][0], =, texelCenter, 0, 0);
			Vec3Scalar(vertexOffsets[0][1], =, texelCenter, 0, 1);
			Vec3Scalar(vertexOffsets[0][2], =, texelCenter + texelExtent, texelHigh, 1);
			Vec3Scalar(vertexOffsets[0][3], =, texelCenter + texelExtent, texelHigh, 0);

			Vec3Scalar(vertexOffsets[1][0], =, texelCenter, 0, 1);
			Vec3Scalar(vertexOffsets[1][1], =, texelCenter, 0, 0);
			Vec3Scalar(vertexOffsets[1][2], =, texelCenter - texelExtent, texelHigh, 0);
			Vec3Scalar(vertexOffsets[1][3], =, texelCenter - texelExtent, texelHigh, 1);

			Vec3Scalar(vertexOffsets[2][0], =, 1, 0, texelCenter);
			Vec3Scalar(vertexOffsets[2][1], =, 0, 0, texelCenter);
			Vec3Scalar(vertexOffsets[2][2], =, 0, texelHigh, texelCenter + texelExtent);
			Vec3Scalar(vertexOffsets[2][3], =, 1, texelHigh, texelCenter + texelExtent);

			Vec3Scalar(vertexOffsets[3][0], =, 0, 0, texelCenter);
			Vec3Scalar(vertexOffsets[3][1], =, 1, 0, texelCenter);
			Vec3Scalar(vertexOffsets[3][2], =, 1, texelHigh, texelCenter - texelExtent);
			Vec3Scalar(vertexOffsets[3][3], =, 0, texelHigh, texelCenter - texelExtent);
		}
		break;
	case BB_WALL_FAN:
		{
			float texelExtent, texelLow, texelHigh;
			texelExtent = 0.916f;
			texelLow = 0.45f;
			texelHigh = 0.90f;
			faceCount = 4;  // really, 2
							// two paired billboards

			// time for some brute force; let's not get too clever
			switch ((dataVal >> 4) & 0x3) {	// set in nbt.cpp with FAN_PROP
			case 0:
				// attached to wall north of block, facing south
				faceDir[0] = DIRECTION_UP_TOP_NORTH;
				faceDir[1] = DIRECTION_DN_TOP_NORTH;
				faceDir[2] = DIRECTION_UP_TOP_SOUTH;
				faceDir[3] = DIRECTION_DN_TOP_SOUTH;

				Vec3Scalar(vertexOffsets[0][0], =, 1, texelLow, 0);
				Vec3Scalar(vertexOffsets[0][1], =, 0, texelLow, 0);
				Vec3Scalar(vertexOffsets[0][2], =, 0, texelHigh, texelExtent);
				Vec3Scalar(vertexOffsets[0][3], =, 1, texelHigh, texelExtent);

				Vec3Scalar(vertexOffsets[1][0], =, 1, 1 - texelLow, 0);
				Vec3Scalar(vertexOffsets[1][1], =, 0, 1 - texelLow, 0);
				Vec3Scalar(vertexOffsets[1][2], =, 0, 1 - texelHigh, texelExtent);
				Vec3Scalar(vertexOffsets[1][3], =, 1, 1 - texelHigh, texelExtent);
				break;
			case 1:
				// attached to east, facing west
				faceDir[0] = DIRECTION_UP_TOP_EAST;
				faceDir[1] = DIRECTION_DN_TOP_EAST;
				faceDir[2] = DIRECTION_UP_TOP_WEST;
				faceDir[3] = DIRECTION_DN_TOP_WEST;

				Vec3Scalar(vertexOffsets[0][0], =, 1, texelLow, 1);
				Vec3Scalar(vertexOffsets[0][1], =, 1, texelLow, 0);
				Vec3Scalar(vertexOffsets[0][2], =, 1 - texelExtent, texelHigh, 0);
				Vec3Scalar(vertexOffsets[0][3], =, 1 - texelExtent, texelHigh, 1);

				Vec3Scalar(vertexOffsets[1][0], =, 1, 1 - texelLow, 1);
				Vec3Scalar(vertexOffsets[1][1], =, 1, 1 - texelLow, 0);
				Vec3Scalar(vertexOffsets[1][2], =, 1 - texelExtent, 1 - texelHigh, 0);
				Vec3Scalar(vertexOffsets[1][3], =, 1 - texelExtent, 1 - texelHigh, 1);
				break;
			case 2:
				// attached to wall south of block, facing north
				faceDir[0] = DIRECTION_UP_TOP_SOUTH;
				faceDir[1] = DIRECTION_DN_TOP_SOUTH;
				faceDir[2] = DIRECTION_UP_TOP_NORTH;
				faceDir[3] = DIRECTION_DN_TOP_NORTH;

				Vec3Scalar(vertexOffsets[0][0], =, 0, texelLow, 1);
				Vec3Scalar(vertexOffsets[0][1], =, 1, texelLow, 1);
				Vec3Scalar(vertexOffsets[0][2], =, 1, texelHigh, 1 - texelExtent);
				Vec3Scalar(vertexOffsets[0][3], =, 0, texelHigh, 1 - texelExtent);

				Vec3Scalar(vertexOffsets[1][0], =, 0, 1 - texelLow, 1);
				Vec3Scalar(vertexOffsets[1][1], =, 1, 1 - texelLow, 1);
				Vec3Scalar(vertexOffsets[1][2], =, 1, 1 - texelHigh, 1 - texelExtent);
				Vec3Scalar(vertexOffsets[1][3], =, 0, 1 - texelHigh, 1 - texelExtent);
				break;
			case 3:
				// attached to west, facing east
				faceDir[0] = DIRECTION_UP_TOP_WEST;
				faceDir[1] = DIRECTION_DN_TOP_WEST;
				faceDir[2] = DIRECTION_UP_TOP_EAST;
				faceDir[3] = DIRECTION_DN_TOP_EAST;

				Vec3Scalar(vertexOffsets[0][0], =, 0, texelLow, 0);
				Vec3Scalar(vertexOffsets[0][1], =, 0, texelLow, 1);
				Vec3Scalar(vertexOffsets[0][2], =, texelExtent, texelHigh, 1);
				Vec3Scalar(vertexOffsets[0][3], =, texelExtent, texelHigh, 0);

				Vec3Scalar(vertexOffsets[1][0], =, 0, 1 - texelLow, 0);
				Vec3Scalar(vertexOffsets[1][1], =, 0, 1 - texelLow, 1);
				Vec3Scalar(vertexOffsets[1][2], =, texelExtent, 1 - texelHigh, 1);
				Vec3Scalar(vertexOffsets[1][3], =, texelExtent, 1 - texelHigh, 0);
				break;
			}
		}
		break;
	default:
        assert(0);
        return 0;
    }

    totalVertexCount = gModel.vertexCount;

	// box value - compute now. Used for billboard output and adding to bounds (e.g., the torch doesn't actually output anything here,
	// but is still considered a billboard - I forget why...).
	boxIndexToLoc(anchor, boxIndex);

	// now output, if anything found (redstone wire may not actually have any sides)
    if (faceCount>0) {
        // get the four UV texture vertices, based on type of block
        saveTextureCorners(swatchLoc, type, uvIndices);

        bool normalUnknown = ((billboardType == BB_TORCH) || (billboardType == BB_FIRE) || foundSunflowerTop);

        for (i = 0; i < faceCount; i++)
        {
            // torches are 4 sides facing out: don't output 8 sides
            if (doubleSided || (i % 2 == 0))
            {
                face = allocFaceRecordFromPool();
                if (face == NULL)
                {
                    return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                }

                // if  we sort, we want to keep faces in the order generated, which is
                // generally cache-coherent (and also just easier to view in the file)
                face->faceIndex = firstFaceModifier((i == 0) && firstFace, gModel.faceCount);
                face->materialType = (short)type;
                face->materialDataVal = getSignificantMaterial(type, origDataVal);

                // if no transform happens, then use faceDir index for normal index
                face->normalIndex = normalUnknown ? COMPUTE_NORMAL : (short)faceDir[i];

                // two faces have the same vertices and uv's, just the normal is reversed.
                fc = i / 2;

                // first or second half of billboard (we assume polygons are one-sided)
                if (i % 2 == 0)
                {
                    // first half of billboard
                    startVertexCount = gModel.vertexCount;

                    // get four face indices for the four corners of the billboard, and always create each
                    for (j = 0; j < 4; j++)
                    {
                        // get vertex location and store location
                        float *pt;
                        retCode |= checkVertexListSize();
                        if (retCode >= MW_BEGIN_ERRORS) return retCode;

                        pt = (float *)gModel.vertices[gModel.vertexCount];

                        pt[X] = (float)(anchor[X] + vertexOffsets[fc][j][X]);
                        pt[Y] = (float)(anchor[Y] + vertexOffsets[fc][j][Y]);
                        pt[Z] = (float)(anchor[Z] + vertexOffsets[fc][j][Z]);

                        face->vertexIndex[j] = startVertexCount + j;
                        if (gExportTexture)
                            face->uvIndex[j] = (short)uvIndices[j];

                        gModel.vertexCount++;
                        assert(gModel.vertexCount <= gModel.vertexListSize);
                    }
                }
                else
                {
                    // second half of billboard
                    // vertices already created by code above, for previous billboard
                    // reverse both loops

                    // use startVertexCount for the locations of the four vertices;
                    // these are always the same
                    for (j = 0; j < 4; j++)
                    {
                        face->vertexIndex[3 - j] = startVertexCount + j;
                        if (gExportTexture)
                            face->uvIndex[3 - j] = (short)uvIndices[j];
                    }
                }

                // all set, so save it away
                retCode |= checkFaceListSize();
                if (retCode >= MW_BEGIN_ERRORS) return retCode;

                gModel.faceList[gModel.faceCount++] = face;
            }
        }
    }

    // post-processing
    if ( billboardType == BB_TORCH )
    {
        gUsingTransform = 1;
        // 11 high, 2x2 but extending out by 1 on each side
        saveBoxGeometry(boxIndex, type, dataVal, 1, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, 6,10, 0,11, 7, 9);
        saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT, 7, 9, 0, 11, 6, 10);

        // add a tip to the torch, shift it one texel in X
        //saveBoxMultitileGeometry( boxIndex, type, swatchLoc, swatchLoc, swatchLoc, 0, DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT, 0, 7,9, 10,10, 6,8);
        int torchVertexCount = gModel.vertexCount;
        saveBoxGeometry(boxIndex, type, dataVal, 0, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT, 7, 9, 10, 10, 6, 8);
        gUsingTransform = 0;

        torchVertexCount = gModel.vertexCount - torchVertexCount;
        identityMtx(mtx);
        translateMtx(mtx, 0.0f, 0.0f, 1.0f/16.0f);
        transformVertices(torchVertexCount,mtx);

        if ( dataVal != 5 )
        {
            //static float shr = -0.8f/2.0f;
            static float trans = 8.0f/16.0f;
            float yAngle;
            switch (dataVal & 0x7)
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
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            // this moves block up so that bottom of torch is at Y=0
            // also move to wall
            translateMtx(mtx, 0.0f, 0.5f, 0.0f );
            rotateMtx(mtx, 20.0f, 0.0f, 0.0f);
            // in 1.7 and earlier torches are sheared:
            // shearMtx(mtx, 0.0f, shr);
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
		int faceidx;
        for (faceidx = 0; faceidx < 4; faceidx++ )
        {
            // add sheared "cross flames" inside, 8 in all
            int fireVertexCount = gModel.vertexCount;
            gUsingTransform = 1;
            saveBoxMultitileGeometry(boxIndex, type, dataVal, swatchLoc, swatchLoc, swatchLoc, 0,
                DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT,
                FLIP_Z_FACE_VERTICALLY, 0,16, 0,16, 16,16);
            gUsingTransform = 0;
            fireVertexCount = gModel.vertexCount - fireVertexCount;
            identityMtx(mtx);
            translateToOriginMtx(mtx, boxIndex);
            shearMtx(mtx, 0.0f, 3.2f/8.0f );
            translateMtx(mtx, 0.0f, 0.0f, -8.0f/16.0f);
            switch (faceidx)
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
        // note that if culling is off, the front face is likely to hide the back.
        retCode |= saveBoxAlltileGeometry(boxIndex, type, dataVal, swatchLocSet, 0,
            DIR_LO_Z_BIT | DIR_HI_Z_BIT | DIR_BOTTOM_BIT | DIR_TOP_BIT,
            FLIP_X_FACE_VERTICALLY, 0,  8,8, 0,16, 0,16);
        if ( retCode > MW_BEGIN_ERRORS ) return retCode;

        gUsingTransform = 0;
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, 0.0f, -20.0f);
        translateMtx(mtx, 1.8f/16.0f, 0.0f, 0.2f/16.0f);
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount,mtx);
    }
    else if (redstoneWireOnBottom) {
        switch (redstoneDirFlags)
        {
        case 0x0:
            // no connections, just a dot
            swatchLoc = redstoneOn ? REDSTONE_WIRE_DOT : REDSTONE_WIRE_DOT_OFF;
            break;

        case FLAT_FACE_LO_X:
        case FLAT_FACE_HI_X:
            // one node, but it's a two-way in Minecraft: no single branch
        case FLAT_FACE_LO_X | FLAT_FACE_HI_X:
            angle = 270;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_HORIZ : REDSTONE_WIRE_HORIZ_OFF;
            break;
        case FLAT_FACE_LO_Z:
        case FLAT_FACE_HI_Z:
        case FLAT_FACE_LO_Z | FLAT_FACE_HI_Z:
            swatchLoc = redstoneOn ? REDSTONE_WIRE_VERT : REDSTONE_WIRE_VERT_OFF;
            break;

            // angled 2 wire:
        case FLAT_FACE_LO_X | FLAT_FACE_LO_Z:
            angle = 270;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
            break;
        case FLAT_FACE_LO_Z | FLAT_FACE_HI_X:
            angle = 0;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
            break;
        case FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
            angle = 90;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
            break;
        case FLAT_FACE_HI_Z | FLAT_FACE_LO_X:
            angle = 180;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
            break;

            // 3 wire
        case FLAT_FACE_LO_X | FLAT_FACE_LO_Z | FLAT_FACE_HI_X:
            angle = 270;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
            break;
        case FLAT_FACE_LO_Z | FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
            angle = 0;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
            break;
        case FLAT_FACE_HI_X | FLAT_FACE_HI_Z | FLAT_FACE_LO_X:
            angle = 90;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
            break;
        case FLAT_FACE_HI_Z | FLAT_FACE_LO_X | FLAT_FACE_LO_Z:
            angle = 180;
            swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
            break;

        default:
            assert(0);
        case FLAT_FACE_LO_X | FLAT_FACE_LO_Z | FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
            swatchLoc = redstoneOn ? REDSTONE_WIRE_4 : REDSTONE_WIRE_4_OFF;
            break;
        }
        // note that if culling is off, the front face is likely to hide the back.
        totalVertexCount = gModel.vertexCount;
        gUsingTransform = 1;
        // if dataVal is 0, no sides were output and this is actually the first face
        saveBoxTileGeometry(boxIndex, type, redstoneOrigDataVal, swatchLoc, (dataVal > 0) ? 0 : 1, DIR_LO_X_BIT | DIR_HI_X_BIT | DIR_LO_Z_BIT | DIR_HI_Z_BIT, 0, 16, 0, 0, 0, 16);

        gUsingTransform = 0;
        totalVertexCount = gModel.vertexCount - totalVertexCount;
        identityMtx(mtx);
        translateToOriginMtx(mtx, boxIndex);
        rotateMtx(mtx, 0.0f, angle, 0.0f);
        translateMtx(mtx, 0.0f, distanceOffset, 0.0f);
        // undo translation
        translateFromOriginMtx(mtx, boxIndex);
        transformVertices(totalVertexCount, mtx);
    }
	if (wobbleIt) {
		gUsingTransform = 0;
		metaVertexCount = gModel.vertexCount - metaVertexCount;
		identityMtx(mtx);
		translateMtx(mtx, shiftX / 16.0f, 0.0f / 16.0f, shiftZ / 16.0f);
		transformVertices(metaVertexCount, mtx);
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
    if (CHECK_COMPOSITE_OVERLAY && (billboardType == BB_SIDE) && (gBlockDefinitions[gBoxData[boxIndex + 1].type].flags & BLF_WHOLE))
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
	// TODO: for rendering, we could test all edge neighbors, too, in order to not delete "blocks as roofs" or similar structures 
    for ( faceDirection = 0; faceDirection < 6; faceDirection++ )
    {
        // check that new location is valid for testing
        Vec2Op( newPt, =, point );
        if ( getNeighbor( faceDirection, newPt ) )
        {
            newBoxIndex = BOX_INDEXV(newPt);
            // is neighbor not in a group, and the same sort of thing as our seed (solid or not)?
            if ( (gBoxData[newBoxIndex].group == NO_GROUP_SET) &&
				// for 3D printing, only check fully-filled cells; for rendering, check if anything is in cell, so that minor stuff will "connect" areas and so is less likely to erase anything
                (((gPrint3D?gBoxData[newBoxIndex].type:gBoxData[newBoxIndex].origType) > BLOCK_AIR) == pGroup->solid) ) {

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

static void coatSurfaces()
{
    int boxIndex;
    int x, y, z;

    for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
    {
        for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
            for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
            {
                // air?
                if (gBoxData[boxIndex].type == BLOCK_AIR)
                {
                    // Are the two neighbors on each side solid? Could be a door or window.
                    static int rule = 2;
                    switch (rule) {
                    default:
                    case 1:
                        if (((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].type > BLOCK_AIR)) ||
                            ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].type > BLOCK_AIR)))
                            gBoxData[boxIndex].type = BLOCK_FAKE;
                        break;

                        // We could also test if the two in the other direction *are* air. This will not plug holes in the roof, though, so let's not.
                    case 2:
                        if (((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].type > BLOCK_AIR) &&
                            (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].type == BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].type == BLOCK_AIR)) ||
                            ((gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_X]].type == BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_X]].type == BLOCK_AIR) &&
                            (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_LO_Z]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[DIRECTION_BLOCK_SIDE_HI_Z]].type > BLOCK_AIR)))
                            gBoxData[boxIndex].type = BLOCK_FAKE;
                        break;

                        // Are the two neighbors on each side solid, or solid on both ends with one air block in between? Could be a door or window.
                        // This works for two-wide doors and windows. I've left it off as generally unnecessary, but someday could be an option.
                    case 3:
                    {
                        bool madeFake = false;
                        for (int xlo = 1; xlo < 3 && !madeFake; xlo++) {
                            if (x - xlo >= gSolidBox.min[X] - 1) {
                                for (int xhi = 1; xhi < 3 && !madeFake; xhi++) {
                                    if (x + xhi <= gSolidBox.max[X] + 1 && xlo + xhi < 4) {
                                        if ((gBoxData[boxIndex + gFaceOffset[xlo*DIRECTION_BLOCK_SIDE_LO_X]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[xhi*DIRECTION_BLOCK_SIDE_HI_X]].type > BLOCK_AIR)) {
                                            gBoxData[boxIndex].type = BLOCK_FAKE;
                                            madeFake = true;
                                        }
                                    }
                                }
                            }
                        }
                        if (!madeFake) {
                            for (int zlo = 1; zlo < 3 && !madeFake; zlo++) {
                                if (z - zlo >= gSolidBox.min[Z] - 1) {
                                    for (int zhi = 1; zhi < 3 && !madeFake; zhi++) {
                                        if (z + zhi <= gSolidBox.max[Z] + 1 && zlo + zhi < 4) {
                                            if ((gBoxData[boxIndex + gFaceOffset[zlo*DIRECTION_BLOCK_SIDE_LO_Z]].type > BLOCK_AIR) && (gBoxData[boxIndex + gFaceOffset[zhi*DIRECTION_BLOCK_SIDE_HI_Z]].type > BLOCK_AIR)) {
                                                gBoxData[boxIndex].type = BLOCK_FAKE;
                                                madeFake = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                    }
                }
            }
        }
    }
}

static void removeCoatingAndGroups()
{
    int boxIndex;
    int x, y, z;

    // we go larger so that the group ID is reset for all
    for (x = gAirBox.min[X]; x <= gAirBox.max[X]; x++)
    {
        for (z = gAirBox.min[Z]; z <= gAirBox.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, gAirBox.min[Y], z);
            for (y = gAirBox.min[Y]; y <= gAirBox.max[Y]; y++, boxIndex++)
            {
                gBoxData[boxIndex].group = 0;
                // turn fake blocks back to air
                if (gBoxData[boxIndex].type == BLOCK_FAKE)
                {
                    gBoxData[boxIndex].type = BLOCK_AIR;
                }
            }
        }
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
                VecScalar( bounds.min, =,  INT_MAX);
                VecScalar( bounds.max, =, INT_MIN);
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
//		VecScalar( gGroupList[i].bounds.min, =, INT_MAX);
//		VecScalar( gGroupList[i].bounds.max, =, INT_MIN);
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
                        // no real reason to clear data field, and we may
                        // in fact want to check it later with origType:
                        // NO NO NO: gBoxData[boxIndex].data = 0x0;
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
    int x, y, z;
    int touchCount = 0;
    int i;
    Point avgLoc, floc;
    int solidBlocks = 0;
    TouchRecord *touchList = NULL;
    //int maxVal;

    // big allocation, not much to be done about it.
    gTouchGrid = (TouchCell*)malloc(gBoxSizeXYZ*sizeof(TouchCell));
    memset(gTouchGrid, 0, gBoxSizeXYZ*sizeof(TouchCell));

    gTouchSize = 0;

    VecScalar(avgLoc, = , 0.0f);

    // Find solid cells to test for edges to connect
    for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
    {
        for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
            for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
            {
                // check if it's solid - if so, add to average center computations,
                // and then see if there are any edges that touch
                if (gBoxData[boxIndex].type > BLOCK_AIR)
                {
                    // TODO: this just averages all solid blocks purely by position.
                    // Some other weighting from center of air space might be better?
                    Vec3Scalar(avgLoc, += , x, y, z);
                    solidBlocks++;

                    // Test the following edges:
                    // +X face and the four edges there
                    // +Z+Y and +Z-Y
                    // This order guarantees us that we'll always count forward into grid,
                    // which I'm not sure is important (I once thought it was), i.e. that
                    // we will never examine cells for solidity that have already been touched.

                    // quick out: if +X cell is air, +X face edges are processed, else all can be ignored
                    if (gBoxData[boxIndex + gBoxSizeYZ].type == BLOCK_AIR)
                    {
                        checkForTouchingEdge(boxIndex, 1, -1, 0);
                        checkForTouchingEdge(boxIndex, 1, 0, -1);
                        checkForTouchingEdge(boxIndex, 1, 1, 0);
                        checkForTouchingEdge(boxIndex, 1, 0, 1);
                    }
                    // quick out, if +Z cell is air, the two +Z face edges are processed, else all can be ignored
                    if (gBoxData[boxIndex + gBoxSize[Y]].type == BLOCK_AIR)
                    {
                        checkForTouchingEdge(boxIndex, 0, -1, 1);
                        checkForTouchingEdge(boxIndex, 0, 1, 1);
                    }
                }
            }
        }
    }
    assert(solidBlocks);

    // were no touching edges found? Then return!
    if (gTouchSize == 0) {
        goto FreeMemory;
    }

    // get average location
    VecScalar(avgLoc, /= , solidBlocks);

    // now find largest of three, X,Y,Z, and subtract it from Y center. We want to bias towards putting
    // blocks *under* other blocks, since we tend to look down on terrain.
    //maxVal = ( gSolidBox.max[X]-gSolidBox.min[X] > gSolidBox.max[Z]-gSolidBox.min[Z] ) ? gSolidBox.max[X]-gSolidBox.min[X]+1 : gSolidBox.max[Z]-gSolidBox.min[Z]+1;
    //maxVal = ( maxVal > gSolidBox.max[Y]-gSolidBox.min[Y] ) ? maxVal : gSolidBox.max[Y]-gSolidBox.min[Y]+1;
    //avgLoc[Y] -= maxVal;

    // allocate space for touched air cells
    touchList = (TouchRecord *)malloc(gTouchSize*sizeof(TouchRecord));

    // what is the distance from corner to corner of the solid box? We choose this distance because
    // avgLoc could be skewed, way over to some other location, due to an imbalance in where stuff is
    // located. We want to ensure that the Y location always dominates over the XZ distance value, so
    // that lower blocks are favored over upper ones. At least, that's today's theory of what's good to add
    // in for a block that connects two edges.
    float norm = (float)sqrt((float)gBoxSize[X] * (float)gBoxSize[X] + (float)gBoxSize[Z] * (float)gBoxSize[Z]);

    // go through the grid, collecting up the locations needing processing
    for (x = gSolidBox.min[X]; x <= gSolidBox.max[X]; x++)
    {
        for (z = gSolidBox.min[Z]; z <= gSolidBox.max[Z]; z++)
        {
            boxIndex = BOX_INDEX(x, gSolidBox.min[Y], z);
            for (y = gSolidBox.min[Y]; y <= gSolidBox.max[Y]; y++, boxIndex++)
            {
                // find potential fill locations and put them in a list
                if (gTouchGrid[boxIndex].count > 0)
                {
                    // here's a possible manifold-fill location, an air block that if
                    // we fill it in we will get rid of a manifold edge.
                    // So add a record.
                    touchList[touchCount].obscurity = gTouchGrid[boxIndex].obscurity;
                    touchList[touchCount].count = gTouchGrid[boxIndex].count;
                    touchList[touchCount].boxIndex = boxIndex;
                    assert(gBoxData[boxIndex].type == BLOCK_AIR);

                    Vec3Scalar(floc, = (float), x, y, z);
                    touchList[touchCount].distance = computeHidingDistance(floc, avgLoc, norm);
                    touchCount++;
                }
            }
        }
    }

    assert(touchCount == gTouchSize);

    // we have the list of touched air cells that could be filled - sort it!
    qsort_s(touchList, touchCount, sizeof(TouchRecord), touchRecordCompare, NULL);

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

    for (i = 0; i < touchCount; i++)
    {
        // note that the ONLY thing we should be accessing from the touchList is the boxIndex.
        // We know the order is sorted now, so the other two bits of data (*especially* the count)
        // are useless. We're going to decrement the count on the gTouchGrid and its neighbors,
        // which is why this count is the one to access.
        boxIndex = touchList[i].boxIndex;

        // Any edges left to fix in the touch grid cell on the sorted list? Previous operations
        // might have decremented its count to 0.

        if (gTouchGrid[boxIndex].count > 0)
        {
            int boxMtlIndex = UNINITIALIZED_INT;
            int foundBlock = 0;
            int faceGroupIDCount = 0;
            int faceGroupID[6];
            int masterGroupID;
            int maxPop;
            IPoint loc;

            // decrement this cell's neighbors' counts and pointers to it, each by one
            decrementNeighbors(boxIndex);

            // choose material for fill: +Y neighbor, on down to -Y - use an array to test
            // also get groups of solid blocks found

            // find 6 neighboring solids, if they exist (at least two will)
            for (i = 0; i < 6; i++)
            {
                int index = boxIndex + gFaceOffset[i];
                foundBlock = (gBoxData[index].type > BLOCK_AIR);
                if (foundBlock)
                {
                    int j;
                    int foundGroup = 0;
                    int groupID = gBoxData[index].group;
                    if (boxMtlIndex < 0)
                        // store away the index of the first material found
                        boxMtlIndex = index;

                    for (j = 0; j < faceGroupIDCount && !foundGroup; j++)
                    {
                        if (faceGroupID[j] == groupID)
                        {
                            foundGroup = 1;
                        }
                    }
                    if (!foundGroup)
                    {
                        // add a new group to list
                        faceGroupID[faceGroupIDCount] = groupID;
                        faceGroupIDCount++;
                    }
                }
            }
            assert(faceGroupIDCount > 0);

            // Now time for fun with the group list:
            // Which group is the largest? That one will be used to merge the rest. The new block also
            // gets this group ID, adds to pop.
            masterGroupID = faceGroupID[0];
            maxPop = gGroupList[faceGroupID[0]].population;
            for (i = 1; i < faceGroupIDCount; i++)
            {
                if (gGroupList[faceGroupID[i]].population > maxPop)
                {
                    assert(gGroupList[faceGroupID[i]].solid);
                    masterGroupID = faceGroupID[i];
                    maxPop = gGroupList[masterGroupID].population;
                }
            }

            // tada! The actual work: the air block is now filled
            // if weld debugging is going on, we should make these some special color - what?
            assert(gBoxData[boxIndex].type == BLOCK_AIR);
            if (gOptions->exportFlags & EXPT_DEBUG_SHOW_WELDS)
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
            boxIndexToLoc(loc, boxIndex);
            addBounds(loc, &gGroupList[masterGroupID].bounds);
            assert(gGroupList[masterGroupID].solid);

            // If multiple groups found, then we must merge all the groups on neighborList.
            if (faceGroupIDCount > 1)
            {
                IBox bounds;
                int *neighborGroups = (int *)malloc((gGroupCount + 1)*sizeof(int));
                memset(neighborGroups, 0, (gGroupCount + 1)*sizeof(int));

                gStats.solidGroupsMerged += (faceGroupIDCount - 1);
                gSolidGroups -= (faceGroupIDCount - 1);
                assert(gSolidGroups >= 1);

                // Get the union of the bounds of all groups found to merge.
                VecScalar(bounds.min, = , INT_MAX);
                VecScalar(bounds.max, = , INT_MIN);
                // mark all the neighbor groups that are to merge
                for (i = 0; i < faceGroupIDCount; i++)
                {
                    if (faceGroupID[i] != masterGroupID)
                    {
                        neighborGroups[faceGroupID[i]] = 1;
                        addBoundsToBounds(gGroupList[faceGroupID[i]].bounds, &bounds);
                    }
                }

                assert(bounds.max[Y] >= bounds.min[Y]);

                // Go through this sub-box and change all groups tagged to the new fill group.
                // We can fill air with whatever we want, since it won't be visible.
                // We don't actually want to fill with bedrock, but put that in for debugging -
                // really, groups just get merged. Also, note that the method below adds to
                // the master group's bounds.
                fillGroups(&bounds, masterGroupID, 1, BLOCK_LAVA, neighborGroups);

                free(neighborGroups);
            }

            // Note that we do not check if this air block getting filled gives a new
            // manifold edge somewhere or not, as some later block filling in might
            // make it go away, or change things some other way. TODO We could actually
            // check here, but it's not a huge deal to check everything again, I hope...
        }
    }

FreeMemory:
    if (touchList) {
        free(touchList);
        touchList = NULL;
    }
    if (gTouchGrid) {
        free(gTouchGrid);
        gTouchGrid = NULL;
    }
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
            int n1index=UNINITIALIZED_INT;
            int n2index=UNINITIALIZED_INT;
            int n1neighbor=UNINITIALIZED_INT;
            int n2neighbor=UNINITIALIZED_INT;
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
            int incr=UNINITIALIZED_INT;
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
    return ( (float)(loc1[Y] + sqrt( vec[X] + vec[Z] ) / norm) );
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
    int survivorGroup = UNINITIALIZED_INT;
    int maxPop = UNINITIALIZED_INT;
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
            // set uninitialized Y values, where the valid range is 0-255
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

static int generateBlockDataAndStatistics(IBox *tightWorldBox, IBox *worldBox)
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

	// top of block coral fans
	{ -0.397273093f,  0.917700410f, 0.0f },  // DIRECTION_UP_LO_X
	{  0.397273093f, -0.917700410f, 0.0f },  // DIRECTION_DN_HI_X
	{  0.397273093f,  0.917700410f, 0.0f },  // DIRECTION_UP_HI_X
	{ -0.397273093f, -0.917700410f, 0.0f },  // DIRECTION_DN_LO_X

	{ 0.0f,  0.917700410f, -0.397273093f },  // DIRECTION_UP_LO_Z
	{ 0.0f, -0.917700410f,  0.397273093f },  // DIRECTION_DN_HI_Z
	{ 0.0f,  0.917700410f,  0.397273093f },  // DIRECTION_UP_HI_Z
	{ 0.0f, -0.917700410f, -0.397273093f },  // DIRECTION_DN_LO_Z

	// side of block coral fans
	{ -0.440931618f,  0.897540629f, 0.0f },  // DIRECTION_UP_TOP_WEST
	{  0.440931618f, -0.897540629f, 0.0f },  // DIRECTION_DN_TOP_WEST
	{  0.440931618f,  0.897540629f, 0.0f },  // DIRECTION_UP_TOP_EAST
	{ -0.440931618f, -0.897540629f, 0.0f },  // DIRECTION_DN_TOP_EAST

	{ 0.0f,  0.897540629f, -0.440931618f },  // DIRECTION_UP_TOP_NORTH
	{ 0.0f, -0.897540629f,  0.440931618f },  // DIRECTION_DN_TOP_NORTH
	{ 0.0f,  0.897540629f,  0.440931618f },  // DIRECTION_UP_TOP_SOUTH
	{ 0.0f, -0.897540629f, -0.440931618f },  // DIRECTION_DN_TOP_SOUTH

	};

    // Add the following to this, everyone adds it
    if ( gOptions->pEFD->chkCenterModel )
    {
        // Compute center for output.
        // We use the difference of the worldBox coordinates here (used to be the average of gSolidBox) because we want to allow
        // people to continue to use centering if they export the same model multiple times.
        // They might do this, for example, to have separate models for certain elements, using
        // color schemes to filter out various blocks with each pass.

        // Here we are figuring out how much was "shaved off" the original bounds in order to get the gSolidBox. Basically, the
        // tight world box min is >= the world box min, so we either subtract nothing or subtract the air cushion from the solid box.
        IPoint minOffset;
        for (i = 0; i < 3; i++) {
            minOffset[i] = gSolidBox.min[i] + worldBox->min[i] - tightWorldBox->min[i];
        }

        // The bottom of the solid box's original bounds is set to be the "0" level when outputting a level.
        gModel.center[Y] = (float)minOffset[Y];

        // We now take the lower bounds offset for the amount to subtract from the X and Z location. We also subtract
        // half the length of the whole box from the output location (rounded to a whole number) so that the model is
        // centered in X and Z on the origin.
        // Note that we don't perfectly center all the time but instead round, which makes the blocks
        // align with whole numbers. If you output in meters, you get no fractions, so get
        // a smaller and slightly more accurate OBJ file.
        gModel.center[X] = (float)(minOffset[X] + floor((float)(worldBox->max[X] - worldBox->min[X] + 1) / 2.0f));
        gModel.center[Z] = (float)(minOffset[Z] + floor((float)(worldBox->max[Z] - worldBox->min[Z] + 1) / 2.0f));
    }
    else
    {
        // keep world coordinates (may get rescaled, of course)
        Vec2Op(gModel.center, =, (float)gWorld2BoxOffset);
    }

    // get the normals into their proper orientations.
    // We need only 6 when only blocks are exported.
    gModel.normalListCount = gExportBillboards ? NUM_NORMALS_STORED : 6;
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

	// If we are exporting per tile, group by tile type - required, as we need to output a separate material per tile ID
	if (gOptions->exportFlags & EXPT_OUTPUT_SEPARATE_TEXTURE_TILES) {
		qsort_s(gModel.faceList, gModel.faceCount, sizeof(FaceRecord*), tileIdCompare, NULL);
	}
	// If we are grouping by material (e.g., STL does not need this), then we need to sort by material
    else if ((gOptions->exportFlags & EXPT_OUTPUT_OBJ_MTL_PER_TYPE) && !(gOptions->exportFlags & EXPT_OUTPUT_EACH_BLOCK_A_GROUP))
    {
        qsort_s(gModel.faceList,gModel.faceCount,sizeof(FaceRecord*),faceIdCompare,NULL);
    }

    return retCode;
}

#define UV_TO_SWATCHLOC(pUV) 
// compare swatch locations and sort by these.
static int tileIdCompare(void* context, const void *str1, const void *str2)
{
	FaceRecord *f1;
	FaceRecord *f2;
	f1 = *(FaceRecord**)str1;
	f2 = *(FaceRecord**)str2;
	context;    // make a useless reference to the unused variable, to avoid C4100 warning
	if (f1->materialType == f2->materialType)
	{
		// tie break of the data value
		// TODO: do this test only if we really want sub-materials
		if (f1->materialDataVal == f2->materialDataVal) {
			// compare swatchLocs
			int swatchLoc1 = gModel.uvIndexList[f1->uvIndex[0]].swatchLoc;
			int swatchLoc2 = gModel.uvIndexList[f2->uvIndex[0]].swatchLoc;
			if (swatchLoc1 == swatchLoc2) {
				// Not necessary, but...
				// Tie break is face loop starting vertex, so that data is
				// output with some coherence. May help mesh caching and memory access.
				// Also, the data just looks more tidy in the file.
				return ((f1->faceIndex < f2->faceIndex) ? -1 : ((f1->faceIndex == f2->faceIndex)) ? 0 : 1);
			}
			else {
				return ((swatchLoc1 < swatchLoc2) ? -1 : 1);
			}
		}
		else return ((f1->materialDataVal < f2->materialDataVal) ? -1 : 1);
	}
	else return ((f1->materialType < f2->materialType) ? -1 : 1);
}

static int faceIdCompare( void* context, const void *str1, const void *str2)
{
    FaceRecord *f1;
    FaceRecord *f2;
    f1 = *(FaceRecord**)str1;
    f2 = *(FaceRecord**)str2;
    context;    // make a useless reference to the unused variable, to avoid C4100 warning
    if ( f1->materialType == f2->materialType )
    {
        // tie break of the data value
        // TODO: do this test only if we really want sub-materials
        if (f1->materialDataVal == f2->materialDataVal) {
            // Not necessary, but...
            // Tie break is face loop starting vertex, so that data is
            // output with some coherence. May help mesh caching and memory access.
            // Also, the data just looks more tidy in the file.
            return ((f1->faceIndex < f2->faceIndex) ? -1 : ((f1->faceIndex == f2->faceIndex)) ? 0 : 1);
        }
        else return ((f1->materialDataVal < f2->materialDataVal) ? -1 : 1);
    }
    else return ( (f1->materialType < f2->materialType) ? -1 : 1 );
}


// return 0 if nothing solid in box
// Note that the dimensions are returned in floats, for later use for statistics
static int getDimensionsAndCount( Point dimensions )
{
    IPoint loc;
    int boxIndex;
    IBox bounds;
    int count = 0;
    VecScalar( bounds.min, =, INT_MAX);
    VecScalar( bounds.max, =, INT_MIN);

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

    if (gExportBillboards)	// same as gOptions->pEFD->chkExportAll
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
        if ( checkMakeFace( type, neighborType, !gPrint3D, testPartial, faceDirection, neighborBoxIndex, false ) )
        {
            // Air (or water, or portal) found next to solid block: time to write it out.
            // First write out any vertices that are needed (this may do nothing, if they're
            // already written out by previous faces doing output of the same vertices).

            // check if we're rendering (always), or 3D printing & lesser, and exporting a fluid block.
            if ( ( !gPrint3D || testPartial ) && 
                IS_FLUID(type) &&
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
                    // full block, so save vertices as-is;
                    // If we're doing a 3D print export, we still check if the neighbor fully covers this full face.
                    if ( gPrint3D && testPartial )
                    {
                        if ( !checkMakeFace( type, neighborType, !gPrint3D, testPartial, faceDirection, neighborBoxIndex, true ) )
                            // face is covered and we're 3D printing
                            continue;
                    }
                    // rendering, or 3D printing and face is not covered
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
                    saveFaceLoop( boxIndex, faceDirection, heights, heightIndices, (faceDirection == 0) );
                    gUsingTransform = 0;
                }
            }
            else
            {
SaveFullBlock:
                // normal face save: not fluid, etc.
                retCode |= saveVertices( boxIndex, faceDirection, loc );
                if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

                saveFaceLoop( boxIndex, faceDirection, NULL, NULL, (faceDirection == 0) );
            }
        }
    }
    return retCode;
}

// Called for lava and water faces 
// Assumes the following: billboards and lesser stuff has been output and their blocks made into air -
//   this then means that if any neighbor is found, it must be a full block and so will cover the face.
// Check if we should make a face: return 1 if face should be output
// type is type of face of currect voxel,
// neighborType is neighboring type,
// faceDirection is which way things connect
// boxIndex and neighborBoxIndex is real locations, in case more info is needed
// Return TRUE if we are to make the face, FALSE if not
static int checkMakeFace( int type, int neighborType, int view3D, int testPartial, int faceDirection, int neighborBoxIndex, int fluidFullBlock )
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
            if ( gBlockDefinitions[neighborType].alpha < 1.0f )
            {
                // and this object is a different
                // type - including ice on water, glass next to water, etc. - then output face
                if ( neighborType != type )
                {
                    // special check: if water next to stationary water, or (rare, user-defined semitransparent) lava next to stationary lava, that doesn't
                    // make a face
                    if ( IS_NOT_FLUID(type) || !sameFluid(type, neighborType) )
                    {
                        // semitransparent neighbor of a different type reveals face.
                        return 1;
                    }
                }
            }
            // look for blocks with cutouts next to them - only for rendering
            if ((gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) &&
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
        if ( IS_FLUID(type) )
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
                    // We are 3D printing, so if this fluid block is *known* to be full, then don't output (return 0), as the face is covered.
                    // This occurs when the fluid block is tagged as full (all faces are full) or the bottom face is tested (it's always full).
                    return !(fluidFullBlock || (faceDirection == DIRECTION_BLOCK_BOTTOM));
                }
            }
        }
        // Faces that are left to test at this point: full block faces, and partial faces for rendered fluids.
        // These are assumed to be fully hidden by the neighbor *unless* the neighbor is lava or water, in which case
        // additional testing is needed.
        if (IS_FLUID(neighborType) )
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
        case BLOCK_OAK_WOOD_STAIRS:						// lesserBlockCoversWholeFace
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
        case BLOCK_PURPUR_STAIRS:
		case BLOCK_PRISMARINE_STAIRS:
		case BLOCK_PRISMARINE_BRICK_STAIRS:
		case BLOCK_DARK_PRISMARINE_STAIRS:
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

        case BLOCK_STONE_SLAB:						// lesserBlockCoversWholeFace
        case BLOCK_WOODEN_SLAB:
        case BLOCK_RED_SANDSTONE_SLAB:
        case BLOCK_PURPUR_SLAB:
		case BLOCK_ANDESITE_SLAB:
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

        case BLOCK_BED:						// lesserBlockCoversWholeFace
            // top
            if ( !view3D )
            {
                // only the print version actually covers the top of the neighboring block
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }
            break;

        case BLOCK_CARPET:						// lesserBlockCoversWholeFace
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_END_PORTAL_FRAME:						// lesserBlockCoversWholeFace
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_FARMLAND:						// lesserBlockCoversWholeFace
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_TRAPDOOR:						// lesserBlockCoversWholeFace
        case BLOCK_IRON_TRAPDOOR:
		case BLOCK_SPRUCE_TRAPDOOR:
		case BLOCK_BIRCH_TRAPDOOR:
		case BLOCK_JUNGLE_TRAPDOOR:
		case BLOCK_ACACIA_TRAPDOOR:
		case BLOCK_DARK_OAK_TRAPDOOR:
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

        case BLOCK_SNOW:						// lesserBlockCoversWholeFace
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_CAULDRON:						// lesserBlockCoversWholeFace
            if ( !view3D )
            {
                return (faceDirection == DIRECTION_BLOCK_TOP);
            }
            break;

        case BLOCK_REDSTONE_REPEATER_OFF:						// lesserBlockCoversWholeFace
        case BLOCK_REDSTONE_REPEATER_ON:
        case BLOCK_REDSTONE_COMPARATOR:
        case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
        case BLOCK_DAYLIGHT_SENSOR:
        case BLOCK_INVERTED_DAYLIGHT_SENSOR:
		case BLOCK_ENCHANTING_TABLE:
		case BLOCK_STONECUTTER:
			// blocks top of block below
            return (faceDirection == DIRECTION_BLOCK_TOP);

        case BLOCK_HOPPER:						// lesserBlockCoversWholeFace
            // blocks bottom of cube above
            return (faceDirection == DIRECTION_BLOCK_BOTTOM);

        case BLOCK_RAIL:						// lesserBlockCoversWholeFace
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
                switch ( modDataVal & 0xf )
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
                switch ( modDataVal & 0xf)
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

        case BLOCK_WATER:						// lesserBlockCoversWholeFace
        case BLOCK_STATIONARY_WATER:
        case BLOCK_LAVA:
        case BLOCK_STATIONARY_LAVA:
            // if these are above, the bottom is always a full face
            if (faceDirection == DIRECTION_BLOCK_TOP)
                return 1;
            break;

		case BLOCK_COMPOSTER:						// lesserBlockCoversWholeFace
			// really, covers whole block on all sides, in a sense
			return 1;
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
		// hmmmm, not sure what this was for, but dataHeight is no longer accessed...
        //int dataHeight = gBoxData[boxIndex].data;
        //if ( dataHeight >= 8 )
        //{
        //    dataHeight = 0;
        //}
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
        int newx = clamp( loc[X]+offx, gAirBox.min[X], gAirBox.max[X] );
        int offz = z-1 + (i%2);
        int newz = clamp( loc[Z]+offz, gAirBox.min[Z], gAirBox.max[Z] );
        neighbor[i] = BOX_INDEX(newx, loc[Y], newz);
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
    if ( IS_WATER(fluidType) ) {
        return IS_WATER(type);
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

static int saveFaceLoop( int boxIndex, int faceDirection, float heights[4], int heightIndices[4], int firstFace )
{
    int i;
    FaceRecord *face;
    int dataVal = 0;
    unsigned short originalType = gBoxData[boxIndex].type;
    int computedSpecialUVs = 0;
    int specialUVindices[4];
    int regularUVindices[4];
    int retCode = MW_NO_ERROR;

    face = allocFaceRecordFromPool();

    // if we sort, we want to keep faces in the order generated, which is
    // generally cache-coherent (and also just easier to view in the file)
    face->faceIndex = firstFaceModifier( firstFace, gModel.faceCount );

    // always the same normal, which directly corresponds to the normals[6] array in gModel
    face->normalIndex = gUsingTransform ? COMPUTE_NORMAL : (short)faceDirection;

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
                        if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES )
                        // we used to check if the block had no textures, but now all blocks have textures, or are invisible
                        //    !( gBlockDefinitions[type].flags & BLF_IMAGE_TEXTURE) )
                        {
                            // use a solid color
                            swatchLoc = type;
                        }
                        else
                        {
                            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );
                            // special: if type is lava, use flowing lava; if water, use flowing water or overlay water
                            if ( IS_WATER(type) ) {
                                if ((faceDirection != DIRECTION_BLOCK_BOTTOM) && (faceDirection != DIRECTION_BLOCK_TOP))
                                {
                                    int neighborType = gBoxData[boxIndex + gFaceOffset[faceDirection]].origType;
                                    swatchLoc = ((neighborType == BLOCK_GLASS) || (neighborType == BLOCK_STAINED_GLASS)) ? SWATCH_INDEX(15, 25) : SWATCH_INDEX(8, 26);
                                }
                            }
                            else if ((type == BLOCK_LAVA) || (type == BLOCK_STATIONARY_LAVA)) {
                                swatchLoc = SWATCH_INDEX(9, 26);
                            }
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
            face->materialType = (short)getMaterialUsingGroup(gBoxData[boxIndex].group);
            face->materialDataVal = 0;
        }
        else
        {
            // if we're doing FLATTOP compression, the topId and dataVal for the block will
            // have been set to what is above the block before now (in the filter code).
            // If the value is not 0 (air), use that material instead
            int special = 0;
            if ( gBoxData[boxIndex].flatFlags )
            {
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_TOP:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_ABOVE )
                    {
                        face->materialType = gBoxData[boxIndex+1].origType;
                        dataVal = gBoxData[boxIndex+1].data;    // this should still be intact, even if neighbor block is cleared to air
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_BOTTOM:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_BELOW )
                    {
                        face->materialType = gBoxData[boxIndex-1].origType;
                        dataVal = gBoxData[boxIndex-1].data;    // this should still be intact, even if neighbor block is cleared to air
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_X )
                    {
                        face->materialType = gBoxData[boxIndex-gBoxSizeYZ].origType;
                        dataVal = gBoxData[boxIndex-gBoxSizeYZ].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_X )
                    {
                        face->materialType = gBoxData[boxIndex+gBoxSizeYZ].origType;
                        dataVal = gBoxData[boxIndex+gBoxSizeYZ].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_LO_Z )
                    {
                        face->materialType = gBoxData[boxIndex-gBoxSize[Y]].origType;
                        dataVal = gBoxData[boxIndex-gBoxSize[Y]].data;
                        special = 1;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    if ( gBoxData[boxIndex].flatFlags & FLAT_FACE_HI_Z )
                    {
                        face->materialType = gBoxData[boxIndex+gBoxSize[Y]].origType;
                        dataVal = gBoxData[boxIndex+gBoxSize[Y]].data;
                        special = 1;
                    }
                    break;
                default:
                    // only direction left is down, and nothing gets merged with those faces
                    break;
                }
            }
            // did flattening happen?
            if ( !special )
            {
                // no flattening, normal storage.
                face->materialType = originalType;
                dataVal = gBoxData[boxIndex].data;
                face->materialDataVal = getSignificantMaterial(face->materialType, dataVal);
            }
            else
            {
                // A flattening has happened.
                // Test just in case something's wedged
                if ( face->materialType == BLOCK_AIR )
                {
                    assert(0);
                    face->materialType = originalType;
                    dataVal = gBoxData[boxIndex].data;
                    face->materialDataVal = getSignificantMaterial(face->materialType, dataVal);
                    return retCode | MW_INTERNAL_ERROR;
                }
                else {
                    // normal case, flattening found and so save material data value
                    face->materialDataVal = getSignificantMaterial(face->materialType, dataVal);
                }
            }
        }

        assert(face->materialType);
    }
    // else no material, so type is not needed

    if ( gExportTexture )
    {
        // I guess we really don't need the swatch location returned; its
        // main effect is to set the proper indices in the texture map itself
        // and note that the swatch is being used
        (int)getSwatch(face->materialType, dataVal, faceDirection, boxIndex, computedSpecialUVs ? NULL : regularUVindices);

        if (computedSpecialUVs)
            for ( i = 0; i < 4; i++ )
                face->uvIndex[i] = (short)specialUVindices[i];
        else
            for (i = 0; i < 4; i++)
                face->uvIndex[i] = (short)regularUVindices[i];


        // if we're exporting the full texture, then a composite was used:
        // we might then actually want to use the original type
        //if ( Options.exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES )
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

// this gives us different materials for debug output, a set of various essentially random materials
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
    switch (dataVal & 0xf)
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
    {if ( (faceDirection) == DIRECTION_BLOCK_BOTTOM )          \
    swatchLoc = SWATCH_XY_TO_INDEX( (bx), (by) );         \
else if ( (faceDirection) != DIRECTION_BLOCK_TOP )        \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );}

#define SWATCH_SWITCH_SIDE( faceDirection, sx,sy )      \
    {if ( ((faceDirection) != DIRECTION_BLOCK_BOTTOM) && ((faceDirection) != DIRECTION_BLOCK_TOP))      \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );}

#define SWATCH_SWITCH_SIDE_VERTICAL( faceDirection, sx,sy, vx,vy ) \
    {if ( (faceDirection) == DIRECTION_BLOCK_TOP || (faceDirection) == DIRECTION_BLOCK_BOTTOM )          \
    swatchLoc = SWATCH_XY_TO_INDEX( (vx), (vy) );             \
else													  \
    swatchLoc = SWATCH_XY_TO_INDEX( (sx), (sy) );}

// Get the face of the full block in a given direction and rotate and flip it as needed.
// note that, for flattops and sides, the dataVal passed in is indeed the data value of the neighboring flattop being merged
static int getSwatch( int type, int dataVal, int faceDirection, int backgroundIndex, int uvIndices[4] )
{
    int swatchLoc;
    int angle = 0;
    int localIndices[4] = { 0, 1, 2, 3 };

    // outputting swatches
    if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES )
    {
        // use a solid color - TODO we could add carpet and stairs here, among others. Real low priority...
        // note that this is not fleshed out (e.g. I don't know if snow works) as full textures, which is the popular mode.
        swatchLoc = type;
        switch ( type )
        {
        case BLOCK_STONE_DOUBLE_SLAB:						// getSwatch
        case BLOCK_STONE_SLAB:
            switch ( dataVal & 0xf )
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
                swatchLoc = BLOCK_OAK_PLANKS;
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
                swatchLoc = BLOCK_NETHER_BRICK;
                break;
            case 7:	// quartz
            case 15:	// quartz
                // quartz (normally same quartz on all faces? See http://minecraft.gamepedia.com/Data_values)
                swatchLoc = BLOCK_QUARTZ_BLOCK;
                break;
            case 10:
                // quartz (normally same quartz on all faces? See http://minecraft.gamepedia.com/Data_values)
                swatchLoc = (type == BLOCK_STONE_DOUBLE_SLAB) ? BLOCK_QUARTZ_BLOCK : BLOCK_OAK_PLANKS;
                break;
            }
            break;
        }
		// should add case BLOCK_PURPUR_DOUBLE_SLAB:						// getSwatch
	}
    else
    {
        // Outputting textured face

        int head, bottom, inside, outside, newFaceDirection, flip, neighborType;
        int xoff, xstart, dir, dirBit, frontLoc, trimVal, xloc, yloc;
        // north is 0, east is 1, south is 2, west is 3
        int faceRot[6] = { 0, 0, 1, 2, 0, 3 };

        // use the textures:
        // use the txrX and txrY to find which to go to.
        swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY );

        // now do anything special needed for the particular type, data, and face direction
        switch ( type )
        {
        case BLOCK_GRASS_BLOCK:						// getSwatch
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
        case BLOCK_DIRT:						// getSwatch
            switch ( dataVal & 0x3 )
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
				// same as grass block: switch to snow covered look
				if (faceDirection != DIRECTION_BLOCK_TOP && faceDirection != DIRECTION_BLOCK_BOTTOM)
				{
					// check if block above is snow; if so, use snow side tile; note we
					// check against the original type, since the snow block is likely to be flattened
					if (gBoxData[backgroundIndex + 1].origType == BLOCK_SNOW)
					{
						swatchLoc = SWATCH_INDEX(4, 4);
					}
				}
				break;
            }
            break;
        case BLOCK_STONE_DOUBLE_SLAB:						// getSwatch
        case BLOCK_STONE_SLAB:
            // The topmost bit is about whether the half-slab is in the top half or bottom half (used to always be bottom half).
            // Since we're exporting full blocks, we don't care, and so mask off this 0x8 bit.
            // See http://www.minecraftwiki.net/wiki/Block_ids#Slabs_and_Double_Slabs
            // high order bit for double slabs means something else
            trimVal = ( type == BLOCK_STONE_SLAB ) ? (dataVal & 0x7) : dataVal;
            switch ( trimVal & 0xf )
            {
            default:
                // Debug world gives: assert(0);
            case 0:
                // smooth stone, two slabs
                // use stone side pattern
                SWATCH_SWITCH_SIDE( faceDirection, 11, 23 ); // was 5, 0 );
                break;
            case 1:
                // sandstone
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_SANDSTONE].txrX, gBlockDefinitions[BLOCK_SANDSTONE].txrY );
                SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12,  0,13 );
                break;
            case 2:
                // wooden
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_OAK_PLANKS].txrX, gBlockDefinitions[BLOCK_OAK_PLANKS].txrY );
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
                swatchLoc = SWATCH_INDEX( gBlockDefinitions[BLOCK_NETHER_BRICK].txrX, gBlockDefinitions[BLOCK_NETHER_BRICK].txrY );
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
        case BLOCK_RED_SANDSTONE_DOUBLE_SLAB:						// getSwatch
		case BLOCK_RED_SANDSTONE_SLAB:						// getSwatch
			if ( (type == BLOCK_RED_SANDSTONE_DOUBLE_SLAB) && (dataVal == 0xf) ) {
				// don't do anything to double slab with 15 set - means smooth all over, so do nothing - just use top everywhere
			}
			else {
				switch (dataVal & 0x7)
				{
				default:
					assert(0);
				case 0:
					// normal block
					SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 14, 13, 5, 8);
					break;
				case 1: // cut_red_sandstone_slab
					swatchLoc = SWATCH_INDEX(12, 19);
					SWATCH_SWITCH_SIDE(faceDirection, 10, 19);
					break;
				case 2: // smooth_red_sandstone_slab
					swatchLoc = SWATCH_INDEX(12, 19);
					break;
				case 3: // cut_sandstone_slab
					swatchLoc = SWATCH_INDEX(0, 11);
					SWATCH_SWITCH_SIDE(faceDirection, 6, 14);
					break;
				case 4: // smooth_sandstone_slab
					swatchLoc = SWATCH_INDEX(0, 11);
					break;
				case 5: // granite_slab
					swatchLoc = SWATCH_INDEX(8, 22);
					break;
				case 6: // polished_granite_slab
					swatchLoc = SWATCH_INDEX(9, 22);
					break;
				case 7: // smooth_quartz_slab
					swatchLoc = SWATCH_INDEX(1, 17);
					break;
				}
			}
            break;
        case BLOCK_SANDSTONE_STAIRS:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12,  0,13 );
            break;
        case BLOCK_RED_SANDSTONE_STAIRS:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 14,13,  5,8 );
            break;
        case BLOCK_ENDER_CHEST:						// getSwatch
            frontLoc = 1;	// is it a front or a side? (not top or bottom)
            angle = 0;
            switch ( faceDirection )
            {
            default:
            case DIRECTION_BLOCK_SIDE_LO_X:
                //angle = 0;
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
                switch ( dataVal & 0x7 )
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
                    // front
                    swatchLoc = SWATCH_INDEX( 12,13 );
                }
                else
                {
                    // side
                    swatchLoc = SWATCH_INDEX( 11,13 );
                }
            }
            break;
		case BLOCK_PURPUR_DOUBLE_SLAB:		// getSwatch
		case BLOCK_PURPUR_SLAB:		// getSwatch
			// some confusion in the docs here: https://minecraft.gamepedia.com/Java_Edition_data_values#Stone_Slabs
			switch (dataVal & 0x7)
			{
			default: // normal log
				assert(0);
			case 0: // purpur slab
			case 1: // purpur slab, just in case...
				break;
			case 2: // prismarine 1.13 - stuffed in here
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_PRISMARINE_STAIRS].txrX, gBlockDefinitions[BLOCK_PRISMARINE_STAIRS].txrY);
				break;
			case 3: // prismarine block 1.13
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_PRISMARINE_BRICK_STAIRS].txrX, gBlockDefinitions[BLOCK_PRISMARINE_BRICK_STAIRS].txrY);
				break;
			case 4: // dark prismarine 1.13
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_DARK_PRISMARINE_STAIRS].txrX, gBlockDefinitions[BLOCK_DARK_PRISMARINE_STAIRS].txrY);
				break;
			case 5:	// red nether slab
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_RED_NETHER_BRICK].txrX, gBlockDefinitions[BLOCK_RED_NETHER_BRICK].txrY);
				break;
			case 6:	// mossy stone slab
				swatchLoc = SWATCH_INDEX(4, 6);
				break;
			case 7:	// mossy cobblestone
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrY);
				break;
			}
			break;
        case BLOCK_LOG:						// getSwatch
        case BLOCK_AD_LOG:
		case BLOCK_STRIPPED_OAK:
		case BLOCK_STRIPPED_ACACIA:
		case BLOCK_STRIPPED_OAK_WOOD:
		case BLOCK_STRIPPED_ACACIA_WOOD:
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
			switch( type ) {
			case BLOCK_LOG:
				if (dataVal & BIT_16) {
					// it's wood, not a log - always switch
					switch (dataVal & 0x3)
					{
					default: // normal wood
					case 0:
						swatchLoc = SWATCH_XY_TO_INDEX(4, 1);
						break;
					case 1: // spruce (dark)
						swatchLoc = SWATCH_XY_TO_INDEX(4, 7);
						break;
					case 2: // birch
						swatchLoc = SWATCH_XY_TO_INDEX(5, 7);
						break;
					case 3: // jungle
						swatchLoc = SWATCH_XY_TO_INDEX(9, 9);
						break;
					}
				}
				else {
					// log - set everything to side unless it's a top or bottom
					switch (dataVal & 0x3)
					{
					default: // normal log
					case 0:
						SWATCH_SWITCH_SIDE(newFaceDirection, 4, 1);
						break;
					case 1: // spruce (dark)
						SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 4, 7, 12, 11);
						break;
					case 2: // birch
						SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 5, 7, 11, 11);
						break;
					case 3: // jungle
						SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 9, 9, 13, 11);
						break;
					}
				}
				break;
			case BLOCK_AD_LOG:
				if (dataVal & BIT_16) {
					// it's wood, not a log - always switch
					switch (dataVal & 0x3)
					{
					default: // normal wood
					case 0: // acacia
						swatchLoc = SWATCH_XY_TO_INDEX(5, 11);
						break;
					case 1: // dark oak
						swatchLoc = SWATCH_XY_TO_INDEX(14, 19);
						break;
					}
				} else {
					// log - set everything to side unless it's a top or bottom
					switch (dataVal & 0x3)
					{
					default: // normal log
					case 0: // acacia
						SWATCH_SWITCH_SIDE(newFaceDirection, 5, 11);
						break;
					case 1: // dark oak
						SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 14, 19, 15, 19);
						break;
					}
				}
				break;
			case BLOCK_STRIPPED_OAK:
				switch (dataVal & 0x3)
				{
				default: // normal log
				case 0:
					SWATCH_SWITCH_SIDE(newFaceDirection, 0, 34);
					break;
				case 1: // spruce (dark)
					SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 1, 34, 7, 34);
					break;
				case 2: // birch
					SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 2, 34, 8, 34);
					break;
				case 3: // jungle
					SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 3, 34, 9, 34);
					break;
				}
				break;
			case BLOCK_STRIPPED_ACACIA:
				switch (dataVal & 0x3)
				{
				default: // normal log
				case 0: // acacia
					SWATCH_SWITCH_SIDE(newFaceDirection, 4, 34);
					break;
				case 1: // dark oak
					SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection, 5, 34, 11, 34);
					break;
				}
				break;
			case BLOCK_STRIPPED_OAK_WOOD:
				switch (dataVal & 0x3)
				{
				default: // normal log
				case 0:
					break;
				case 1: // spruce (dark)
					swatchLoc = SWATCH_INDEX(1, 34);
					break;
				case 2: // birch
					swatchLoc = SWATCH_INDEX(2, 34);
					break;
				case 3: // jungle
					swatchLoc = SWATCH_INDEX(3, 34);
					break;
				}
				break;
			case BLOCK_STRIPPED_ACACIA_WOOD:
				switch (dataVal & 0x3)
				{
				default: // normal log
				case 0: // acacia
					break;
				case 1: // dark oak
					swatchLoc = SWATCH_INDEX(5, 34);
					break;
				}
				break;
			}
            if ( angle != 0 && uvIndices )
                rotateIndices( localIndices, angle );
            if ( flip && uvIndices )
                flipIndicesLeftRight( localIndices );
            break;
        case BLOCK_OAK_PLANKS:						// getSwatch
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
        case BLOCK_STONE:						// getSwatch
            switch ( dataVal & 0x7 )
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
        case BLOCK_LEAVES:						// getSwatch
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
        case BLOCK_AD_LEAVES:						// getSwatch
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
        case BLOCK_SAND:						// getSwatch
            switch ( dataVal & 0x1 )
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
        case BLOCK_DISPENSER:						// getSwatch
        case BLOCK_DROPPER:
        case BLOCK_FURNACE:
        case BLOCK_BURNING_FURNACE:
			// establish top/side/bottom
			switch (dataVal & (BIT_32 | BIT_16)) {
			default:
			case 0:	// furnace, dropper, dispenser - all the same
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 13, 2, 14, 3);
				break;
			case BIT_16:	// loom
				swatchLoc = SWATCH_INDEX(4, 40);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 5, 40, 6, 40);
				break;
			case BIT_32:	// smoker
				swatchLoc = SWATCH_INDEX(11, 40);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 12, 40, 13, 40);
				break;
			case BIT_32 | BIT_16:	// blast furnace
				swatchLoc = SWATCH_INDEX(7, 38);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 8, 38, 7, 38);
				break;
			}
			// if a side, we may need to change a face to be a front or front_on
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
 					switch (dataVal & (BIT_32 | BIT_16)) {
					default:
					case 0:	// furnace
						frontLoc = SWATCH_INDEX(12, 2);
						break;
					case BIT_16:	// loom
						frontLoc = SWATCH_INDEX(7, 40);
						break;
					case BIT_32:	// smoker
						frontLoc = SWATCH_INDEX(14, 40);
						break;
					case BIT_32 | BIT_16:	// blast furnace
						frontLoc = SWATCH_INDEX(9, 38);
						break;
					}
					break;
                case BLOCK_BURNING_FURNACE:
                default:
					switch (dataVal & (BIT_32 | BIT_16)) {
					default:
					case 0:	// furnace
						frontLoc = SWATCH_INDEX(13, 3);
						break;
					case BIT_16:	// loom
						frontLoc = SWATCH_INDEX(7, 40);
						break;
					case BIT_32:	// smoker
						frontLoc = SWATCH_INDEX(15, 40);
						break;
					case BIT_32 | BIT_16:	// blast furnace
						frontLoc = SWATCH_INDEX(10, 38);
						break;
					}
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
                // top or bottom face.
				switch (dataVal & 0x7)
				{
				case 0:	// dispenser/dropper facing down, can't be anything else
					if (faceDirection == DIRECTION_BLOCK_BOTTOM)
					{
						swatchLoc = (type == BLOCK_DISPENSER) ? SWATCH_INDEX(15, 2) : SWATCH_INDEX(8, 15);
					}
					else
					{
						swatchLoc = SWATCH_INDEX(14, 3);
					}
					break;
				case 1: // dispenser/dropper facing up, can't be anything else
					if (faceDirection == DIRECTION_BLOCK_TOP)
					{
						swatchLoc = (type == BLOCK_DISPENSER) ? SWATCH_INDEX(15, 2) : SWATCH_INDEX(8, 15);
					}
					else
					{
						swatchLoc = SWATCH_INDEX(14, 3);
					}
					break;
				case 2: // North -Z
					// no rotation needed
					break;
				case 3: // South +Z
					if (uvIndices) {
						rotateIndices(localIndices, 180);
					}
					break;
				case 4: // West
					if (uvIndices) {
						rotateIndices(localIndices, 270);
					}
					break;
				case 5: // East
				default:
					if (uvIndices) {
						rotateIndices(localIndices, 90);
					}
					break;
				}
            }
            break;
        case BLOCK_OBSERVER:
            // I suspect the top bit is whether the observer is firing, but don't know. 
            swatchLoc = (dataVal & 0x8) ? SWATCH_INDEX(1, 33) : SWATCH_INDEX(0, 33);
            switch (dataVal & 0x7)
            {
            case 0:	// back side facing down
                switch (faceDirection) {
                case DIRECTION_BLOCK_BOTTOM: swatchLoc = SWATCH_INDEX(2, 33); break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X: swatchLoc = SWATCH_INDEX(3, 33);
                    if (uvIndices) { 
                        rotateIndices(localIndices, 90);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY); break;
                default: break;
                }
                break;
            case 1: // facing up
                switch (faceDirection) {
                case DIRECTION_BLOCK_TOP: swatchLoc = SWATCH_INDEX(2, 33);
                    if (uvIndices) {
                        rotateIndices(localIndices, 180);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X: swatchLoc = SWATCH_INDEX(3, 33);
                    if (uvIndices) {
                        rotateIndices(localIndices, 90);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY); 
                    if (uvIndices) {
                        rotateIndices(localIndices, 180);
                    }
                    break;
                default: break;
                }
                break;
            case 2: // North -Z
                switch (faceDirection) {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
                    if (uvIndices) {
						flipIndicesLeftRight(localIndices);
                        rotateIndices(localIndices, 180);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X: swatchLoc = SWATCH_INDEX(3, 33); break;
                case DIRECTION_BLOCK_SIDE_LO_Z: swatchLoc = SWATCH_INDEX(2, 33); break;
                default: break;
                }
                break;
            case 3: // South +Z
                switch (faceDirection) {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
					if (uvIndices) {
						flipIndicesLeftRight(localIndices);
					}
					break;
				case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X: swatchLoc = SWATCH_INDEX(3, 33); break;
                case DIRECTION_BLOCK_SIDE_HI_Z: swatchLoc = SWATCH_INDEX(2, 33); break;
                default: break;
                }
                break;
            case 4: // West
                switch (faceDirection) {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
                    if (uvIndices) {
						flipIndicesLeftRight(localIndices);
						rotateIndices(localIndices, 90);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X: swatchLoc = SWATCH_INDEX(2, 33); break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z: swatchLoc = SWATCH_INDEX(3, 33); break;
                default: break;
                }
                break;
            case 5: // East
            default:
                switch (faceDirection) {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP: swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
                    if (uvIndices) {
						flipIndicesLeftRight(localIndices);
						rotateIndices(localIndices, 270);
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X: swatchLoc = SWATCH_INDEX(2, 33); break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z: swatchLoc = SWATCH_INDEX(3, 33); break;
                default: break;
                }
                break;
            }
            break;
        case BLOCK_SHULKER_CHEST:
        case BLOCK_SHULKER_CHEST + 1:
        case BLOCK_SHULKER_CHEST + 2:
        case BLOCK_SHULKER_CHEST + 3:
        case BLOCK_SHULKER_CHEST + 4:
        case BLOCK_SHULKER_CHEST + 5:
        case BLOCK_SHULKER_CHEST + 6:
        case BLOCK_SHULKER_CHEST + 7:
        case BLOCK_SHULKER_CHEST + 8:
        case BLOCK_SHULKER_CHEST + 9:
        case BLOCK_SHULKER_CHEST + 10:
        case BLOCK_SHULKER_CHEST + 11:
        case BLOCK_SHULKER_CHEST + 12:
        case BLOCK_SHULKER_CHEST + 13:
        case BLOCK_SHULKER_CHEST + 14:
        case BLOCK_SHULKER_CHEST + 15:
            if (faceDirection == DIRECTION_BLOCK_BOTTOM) {
                swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY + 5);
            }
            else if (faceDirection != DIRECTION_BLOCK_TOP) {
                swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY + 4);
            }
            break;
        case BLOCK_GLAZED_TERRACOTTA:
        case BLOCK_GLAZED_TERRACOTTA + 1:
        case BLOCK_GLAZED_TERRACOTTA + 2:
        case BLOCK_GLAZED_TERRACOTTA + 3:
        case BLOCK_GLAZED_TERRACOTTA + 4:
        case BLOCK_GLAZED_TERRACOTTA + 5:
        case BLOCK_GLAZED_TERRACOTTA + 6:
        case BLOCK_GLAZED_TERRACOTTA + 7:
        case BLOCK_GLAZED_TERRACOTTA + 8:
        case BLOCK_GLAZED_TERRACOTTA + 9:
        case BLOCK_GLAZED_TERRACOTTA + 10:
        case BLOCK_GLAZED_TERRACOTTA + 11:
        case BLOCK_GLAZED_TERRACOTTA + 12:
        case BLOCK_GLAZED_TERRACOTTA + 13:
        case BLOCK_GLAZED_TERRACOTTA + 14:
        case BLOCK_GLAZED_TERRACOTTA + 15:
            if (uvIndices) {
                switch (faceDirection)
                {
                case DIRECTION_BLOCK_BOTTOM:
                    switch (dataVal & 0x3) {
                    case 0: rotateIndices(localIndices, 180); break;
                    case 1: rotateIndices(localIndices, 270); break;
                    case 2: break;
                    case 3: rotateIndices(localIndices, 90); break;
                    }
                    break;
                case DIRECTION_BLOCK_TOP: // done
                    switch (dataVal & 0x3) {
                    case 0: break;
                    case 1: rotateIndices(localIndices, 90); break;
                    case 2: rotateIndices(localIndices, 180); break;
                    case 3: rotateIndices(localIndices, 270); break;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                    switch (dataVal & 0x3) {
                    case 0: rotateIndices(localIndices, 90); break;
                    case 1: break;
                    case 2: rotateIndices(localIndices, 270); break;
                    case 3: rotateIndices(localIndices, 180); break;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    switch (dataVal & 0x3) {
                    case 0: rotateIndices(localIndices, 270); break;
                    case 1: rotateIndices(localIndices, 180); break;
                    case 2: rotateIndices(localIndices, 90); break;
                    case 3: break;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X: // done
                    switch (dataVal & 0x3) {
                    case 0: break;
                    case 1: rotateIndices(localIndices, 270); break;
                    case 2: rotateIndices(localIndices, 180); break;
                    case 3: rotateIndices(localIndices, 90); break;
                    }
                    break;
                case DIRECTION_BLOCK_SIDE_HI_X: // done
                    switch (dataVal & 0x3) {
                    case 0: rotateIndices(localIndices, 180); break;
                    case 1: rotateIndices(localIndices, 90); break;
                    case 2: break;
                    case 3: rotateIndices(localIndices, 270); break;
                    }
                    break;
                }
                break;
            }
            break;
        case BLOCK_CONCRETE:
        case BLOCK_CONCRETE_POWDER:
            swatchLoc = SWATCH_INDEX(gBlockDefinitions[type].txrX + dataVal, gBlockDefinitions[type].txrY);
            break;
        case BLOCK_POWERED_RAIL:						// getSwatch
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
        case BLOCK_RAIL:						// getSwatch
            // get data of track itself
            switch ( dataVal & 0xf )
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
        case BLOCK_SANDSTONE:						// getSwatch
            // top is always sandy, just leave it be
            if ( faceDirection != DIRECTION_BLOCK_TOP )
            {
                // something must be done
                // use data to figure out which type of sandstone
                switch ( dataVal & 0x3 )
                {
                default:
                    assert(0);
                case 0:
                    // normal sandstone, bottom does get changed
                    SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 0,12, 0,13 );
                    break;
                case 1: // chiseled - bottom unchanged
                    SWATCH_SWITCH_SIDE( faceDirection, 5,14 );
                    break;
                case 2: // smooth - bottom unchanged
                    SWATCH_SWITCH_SIDE( faceDirection, 6,14 );
                    break;
                }
            }
            break;
        case BLOCK_RED_SANDSTONE:						// getSwatch
            // top is always sandy, just leave it be
            if ( faceDirection != DIRECTION_BLOCK_TOP )
            {
                // something must be done
                // use data to figure out which type of sandstone
                switch ( dataVal & 0x3 )
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
        case BLOCK_NOTEBLOCK:						// getSwatch
            SWATCH_SWITCH_SIDE( faceDirection, 12, 8 );	// was 10, 4 jukebox side, now is separate at 
            break;
        case BLOCK_BED:						// getSwatch
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
        case BLOCK_STICKY_PISTON:						// getSwatch
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
					angle = 180*dir;
                }
                else if ( faceDirection == DIRECTION_BLOCK_TOP )
                {
                    head = dir;
                    bottom = 1 - dir;
					angle = 180*dir;
                }
                // else it's a side, and since sides are aligned, rotate all 180 or none
                else
                {
                    angle=180*(1-dir);
                }
                break;
            case 2: // pointing north
            case 3: // pointing south
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
                else if ( ( faceDirection == DIRECTION_BLOCK_BOTTOM ) ||
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
            case 4: // pointing west
            case 5: // pointing east
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
			}
			if (uvIndices && (angle != 0))
				rotateIndices(localIndices, angle);
			break;
        case BLOCK_PISTON_HEAD:						// getSwatch
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
					angle = 180 * dir;
				}
                else if ( faceDirection == DIRECTION_BLOCK_TOP )
                {
                    head = dir;
                    bottom = 1 - dir;
					angle = 180 * dir;
				}
                // else it's a side, and since sides are aligned, rotate all 180 or none
                else
                {
                    angle=180*(1-dir);
                }
                break;
            case 2: // pointing north
            case 3: // pointing south
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
                else if ( ( faceDirection == DIRECTION_BLOCK_BOTTOM ) ||
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
            case 4: // pointing west
            case 5: // pointing east
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
            }
			if (uvIndices && (angle != 0))
				rotateIndices(localIndices, angle);
			break;
        case BLOCK_TNT:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 8, 0,  10, 0 );
            break;
        case BLOCK_BOOKSHELF:						// getSwatch
            SWATCH_SWITCH_SIDE( faceDirection, 3, 2 );
            break;
        case BLOCK_WOODEN_DOOR:						// getSwatch
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

        case BLOCK_TORCH:						// getSwatch
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
        case BLOCK_LEVER:						// getSwatch
            // TODO in a perfect world, we'd move the lever up a bit when compositing, so its bottom was more centered.
            angle = (dataVal & 0x8 ) ? 180 : 0;
            if ( ((dataVal & 0x7) == 5) || ((dataVal & 0x7) == 7) )
                angle += 180;
            else if ( ((dataVal & 0x7) == 6) || ((dataVal & 0x7) == 0) )
                angle += 90;
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, angle );
            break;
        case BLOCK_TRIPWIRE_HOOK:				// getSwatch
            // currently we don't adjust the tripwire hook
            angle = 0;
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, angle );
            break;
        case BLOCK_CHEST:						// getSwatch
        case BLOCK_TRAPPED_CHEST:
            // for these full blocks, note we have stretched the input data to fit, and put a latch on
            // set side of chest as default
            SWATCH_SWITCH_SIDE( faceDirection, 10, 1 );
            angle = 0;
            switch ( dataVal & 0x7 )
            {
            case 2: // facing north
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
                else if (faceDirection == DIRECTION_BLOCK_TOP || faceDirection == DIRECTION_BLOCK_BOTTOM) // top or bottom
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if (gBoxData[backgroundIndex - gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 14);
                    }
                    else if (gBoxData[backgroundIndex + gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 14);
                    }
                    angle = 180;
                }
                break;
            case 3: // facing south
                if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z) // south
                {
                    // front of chest, on possibly long face
                    swatchLoc = SWATCH_INDEX(11, 1);	// front
                    // is neighbor to east also a chest?
                    if (gBoxData[backgroundIndex + gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 2);
                    }
                    // else, is neighbor to west also a chest?
                    else if (gBoxData[backgroundIndex - gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 2);
                    }
                }
                else if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) // north
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if (gBoxData[backgroundIndex + gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 3);
                    }
                    else if (gBoxData[backgroundIndex - gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 3);
                    }
                }
                else if (faceDirection == DIRECTION_BLOCK_TOP || faceDirection == DIRECTION_BLOCK_BOTTOM) // top or bottom
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if (gBoxData[backgroundIndex + gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 14);
                    }
                    else if (gBoxData[backgroundIndex - gBoxSizeYZ].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 14);
                    }
                }
                break;
            case 4: // facing west
                if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X) // west
                {
                    swatchLoc = SWATCH_INDEX(11, 1);
                    if (gBoxData[backgroundIndex - gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 2);
                    }
                    else if (gBoxData[backgroundIndex + gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 2);
                    }
                }
                else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X) // east
                {
                    // back of chest, on possibly long face
                    // is neighbor to north a chest, too?
                    if (gBoxData[backgroundIndex - gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 3);
                    }
                    else if (gBoxData[backgroundIndex + gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 3);
                    }
                }
                else if (faceDirection == DIRECTION_BLOCK_TOP || faceDirection == DIRECTION_BLOCK_BOTTOM) // top or bottom
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if (gBoxData[backgroundIndex - gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 14);
                    }
                    else if (gBoxData[backgroundIndex + gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 14);
                    }
                    angle = 270;
                }
                break;
            case 5: // facing east
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
                else if (faceDirection == DIRECTION_BLOCK_TOP || faceDirection == DIRECTION_BLOCK_BOTTOM) // top or bottom
                {
                    // back of chest, on possibly long face - keep it a "side" unless changed by neighbor
                    if (gBoxData[backgroundIndex + gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(9, 14);
                    }
                    else if (gBoxData[backgroundIndex - gBoxSize[Y]].type == type)
                    {
                        swatchLoc = SWATCH_INDEX(10, 14);
                    }
                    angle = 90;
                }
                break;
            case 0:
                // old bad data, so this code matches it.
                // In reality, in 1.8 such chests just disappear! The data's still
                // in the world, but you can't see or interact with the chests.
                // We leave this code here for older worlds encountered.
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
            if (angle != 0 && uvIndices)
                rotateIndices(localIndices, angle);
            //if (flip && uvIndices)
            //	flipIndicesLeftRight(localIndices);
            break;
        case BLOCK_CRAFTING_TABLE:						// getSwatch
			switch (dataVal & 0xf) {
			default:
				assert(0);
			case 0:
				// crafting
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 11, 3, 4, 0);
				if ((faceDirection == DIRECTION_BLOCK_SIDE_LO_X) || (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z))
				{
					SWATCH_SWITCH_SIDE(faceDirection, 12, 3);
				}
				break;
			case 1:
				// cartography
				swatchLoc = SWATCH_INDEX(3, 39);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 6, 39, 1, 22);	// dark oak on bottom
				if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X)
				{
					SWATCH_SWITCH_SIDE(faceDirection, 5, 39);
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z) {
					SWATCH_SWITCH_SIDE(faceDirection, 4, 39);
				}
				break;
			case 2:
				// fletching
				swatchLoc = SWATCH_INDEX(7, 39);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 8, 39, 6, 13);	// birch on bottom
				if ((faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) || (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z))
				{
					SWATCH_SWITCH_SIDE(faceDirection, 9, 39);
				}
				break;
			case 3:
				// crafting
				swatchLoc = SWATCH_INDEX(0, 41);
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 1, 41, 2, 41);
				if ((faceDirection == DIRECTION_BLOCK_SIDE_LO_Z) || (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z))
				{
					SWATCH_SWITCH_SIDE(faceDirection, 3, 41);
				}
				break;
			}
            break;
        case BLOCK_CACTUS:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6, 4,  7, 4 );
            break;
        case BLOCK_PUMPKIN:						// getSwatch
        case BLOCK_JACK_O_LANTERN:
        case BLOCK_HEAD:	// definitely wrong for heads, TODO - have tile entity, but now need all the dratted head textures...
            SWATCH_SWITCH_SIDE( faceDirection, 6, 7 );
            xoff = ( type == BLOCK_PUMPKIN ) ? 7 : 8;
            // if it's a head, we round the rotation found into a dataVal
            if (type == BLOCK_HEAD) {
                // TODO head type
                // is head on floor or wall?
                float yrot = 0.0f;
                if (dataVal & 0x80) {
                    // on floor
                    yrot = 22.5f*(dataVal & 0xf);
                }
                else {
                    // on wall
                    switch (dataVal & 0xf) {
                    default:
                        assert(0);
                    case 2: // north
                        yrot = 0.0f;
                        break;
                    case 3: // south
                        yrot = 180.0f;
                        break;
                    case 4: // east
                        yrot = 270.0f;
                        break;
					case 0:
                    case 5: // west
                        yrot = 90.0f;
                        break;
                    }
                }
                if (yrot >= 45.0f && yrot < 135.0f)
                    dataVal = 3;
                else if (yrot >= 135.0f && yrot < 225.0f)
                    dataVal = 0;
                else if (yrot >= 225.0f && yrot < 315.0f)
                    dataVal = 1;
                else
                    dataVal = 2;
            }
            if ((faceDirection != DIRECTION_BLOCK_TOP) && (faceDirection != DIRECTION_BLOCK_BOTTOM))
            {
                switch ( dataVal & 0x7 )
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
                    if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
                    {
                        swatchLoc = SWATCH_INDEX(xoff, 7);
                    }
                    break;
                case 4: // no face at all! Uncarved
                    // http://minecraft.gamepedia.com/Pumpkin#Block_data
                    break;
                }
            }
            else
            {
                if ( uvIndices )
                {
                    int iangle = (2+(dataVal&03))*90;
                    if ( faceDirection == DIRECTION_BLOCK_BOTTOM )
                        iangle += 270;
                    rotateIndices( localIndices, iangle%360);
                }
            }
            break;
        case BLOCK_JUKEBOX:						// getSwatch
            SWATCH_SWITCH_SIDE( faceDirection, 10, 4 );
            break;
        case BLOCK_CAKE:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 10, 7,  12, 7 );
            break;
        case BLOCK_FARMLAND:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 2, 0,  2, 0 );
            break;
        case BLOCK_REDSTONE_REPEATER_OFF:						// getSwatch
        case BLOCK_REDSTONE_REPEATER_ON:
            swatchLoc = SWATCH_INDEX( 3, 8 + (type == BLOCK_REDSTONE_REPEATER_ON) );
            if ( uvIndices )
                rotateIndices( localIndices, 90*(dataVal&0x3));
            break;
        case BLOCK_REDSTONE_COMPARATOR:						// getSwatch
        case BLOCK_REDSTONE_COMPARATOR_DEPRECATED:
            // in 1.5, comparator active is used for top bit
            // in 1.6, comparator active is not used, it depends on dataVal
            {
                int in_powered = ((type == BLOCK_REDSTONE_COMPARATOR_DEPRECATED) || (dataVal >= 8));
                swatchLoc = SWATCH_INDEX( 14 + in_powered,14 );
            }
            if ( uvIndices )
                rotateIndices( localIndices, 90*(dataVal&0x3));
            break;
        case BLOCK_REDSTONE_WIRE:						// getSwatch
            {
                angle = 0;
                bool redstoneOn = (dataVal & 0xf) ? true : false;
                if (faceDirection == DIRECTION_BLOCK_TOP)
                {
                    // flat wire - note that the two high bits have to do with direction, not waterlogged, etc.
                    switch (dataVal >> 4)
                    {
                    case 0x0:
                        // no connections, just a dot
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_DOT : REDSTONE_WIRE_DOT_OFF;
                        break;

                    case FLAT_FACE_LO_X:
                    case FLAT_FACE_HI_X:
                        // one node, but it's a two-way in Minecraft: no single branch
                    case FLAT_FACE_LO_X | FLAT_FACE_HI_X:
                        angle = 270;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_HORIZ : REDSTONE_WIRE_HORIZ_OFF;
                        break;
                    case FLAT_FACE_LO_Z:
                    case FLAT_FACE_HI_Z:
                    case FLAT_FACE_LO_Z | FLAT_FACE_HI_Z:
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_VERT : REDSTONE_WIRE_VERT_OFF;
                        break;

                        // angled 2 wire:
                    case FLAT_FACE_LO_X | FLAT_FACE_LO_Z:
                        angle = 270;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
                        break;
                    case FLAT_FACE_LO_Z | FLAT_FACE_HI_X:
                        angle = 0;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
                        break;
                    case FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
                        angle = 90;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
                        break;
                    case FLAT_FACE_HI_Z | FLAT_FACE_LO_X:
                        angle = 180;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_ANGLED_2 : REDSTONE_WIRE_ANGLED_2_OFF;
                        break;

                        // 3 wire
                    case FLAT_FACE_LO_X | FLAT_FACE_LO_Z | FLAT_FACE_HI_X:
                        angle = 270;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
                        break;
                    case FLAT_FACE_LO_Z | FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
                        angle = 0;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
                        break;
                    case FLAT_FACE_HI_X | FLAT_FACE_HI_Z | FLAT_FACE_LO_X:
                        angle = 90;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
                        break;
                    case FLAT_FACE_HI_Z | FLAT_FACE_LO_X | FLAT_FACE_LO_Z:
                        angle = 180;
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_3 : REDSTONE_WIRE_3_OFF;
                        break;

                    default:
                        assert(0);
                    case FLAT_FACE_LO_X | FLAT_FACE_LO_Z | FLAT_FACE_HI_X | FLAT_FACE_HI_Z:
                        swatchLoc = redstoneOn ? REDSTONE_WIRE_4 : REDSTONE_WIRE_4_OFF;
                        break;
                    }
                }
                else
                {
                    // vertical wire
                    if (redstoneOn) {
                        SWATCH_SWITCH_SIDE(faceDirection, 4, 10);	// REDSTONE_WIRE_VERT
                    } else {
                        SWATCH_SWITCH_SIDE(faceDirection, 10, 26);	// REDSTONE_WIRE_VERT_OFF
                    }
                    angle = 0;
                }
                swatchLoc = getCompositeSwatch(swatchLoc, backgroundIndex, faceDirection, angle);
            }
            break;
        case BLOCK_STONE_BRICKS:						// getSwatch
            switch ( dataVal & 0x3 )
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
                // chiseled circle - added in 1.2.4
                swatchLoc = SWATCH_INDEX( 5,13 );
                break;
            }
            break;
        case BLOCK_FROSTED_ICE:						// getSwatch
            switch ( dataVal & 0x3 )
            {
            default:
                assert(0);
            case 0:
                // no change
                break;
            case 1:
                swatchLoc++;
                break;
            case 2:
                swatchLoc += 2;
                break;
            case 3:
                swatchLoc += 3;
                break;
            }
            break;
        case BLOCK_TRAPDOOR:						// getSwatch
        case BLOCK_IRON_TRAPDOOR:
		case BLOCK_SPRUCE_TRAPDOOR:
		case BLOCK_BIRCH_TRAPDOOR:
		case BLOCK_JUNGLE_TRAPDOOR:
		case BLOCK_ACACIA_TRAPDOOR:
		case BLOCK_DARK_OAK_TRAPDOOR:
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
		case BLOCK_SEAGRASS:
		case BLOCK_CAMPFIRE:
			swatchLoc = getCompositeSwatch(swatchLoc, backgroundIndex, faceDirection, (type == BLOCK_LILY_PAD) ? 270 : 0);
            break;
        case BLOCK_POPPY:						// getSwatch
            if ( (dataVal & 0xf) > 0 )
            {
				if (dataVal < 9) {
					// row 20 has these flowers; else poppy (12,0) is used
					swatchLoc = SWATCH_INDEX(dataVal - 1, 19);
				}
				else {
					// cornflower, lily of the valley, wither rose
					swatchLoc = SWATCH_INDEX(dataVal - 6, 37);
				}
            }
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_DOUBLE_FLOWER:						// getSwatch
            if ((dataVal & 0xf) < 8 )
            {
                // bottom half of plant
                if ((dataVal & 0xf) == 0 )
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
		case BLOCK_TALL_SEAGRASS:				// saveBillboardFacesExtraData
			// bottom half of plant
			swatchLoc = SWATCH_INDEX(14, 33);
			swatchLoc = getCompositeSwatch(swatchLoc, backgroundIndex, faceDirection, 0);
			break;
		case BLOCK_SAPLING:						// getSwatch
            switch (dataVal & 0x7)
            {
            default:
                assert(0);
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
            case 3:
                // jungle sapling
                swatchLoc = SWATCH_INDEX(14,1);
                break;
            case 4:
                // acacia sapling
                swatchLoc = SWATCH_INDEX(14, 18);
                break;
			case 5:
				// dark oak sapling
				swatchLoc = SWATCH_INDEX(15, 18);
				break;
			case 6:
				// bamboo sapling
				swatchLoc = SWATCH_INDEX(9, 37);
				break;
			}
            swatchLoc = getCompositeSwatch( swatchLoc, backgroundIndex, faceDirection, 0 );
            break;
        case BLOCK_GRASS:						// getSwatch
            switch ( dataVal & 0x3 )
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
        case BLOCK_VINES:						// getSwatch
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
        case BLOCK_INFESTED_STONE:						// getSwatch
            switch ( dataVal & 0x7 )
            {
            case 0:
            default:
                // default
                break;
            case 1: // cobblestone
                swatchLoc = SWATCH_INDEX(0, 1);
                break;
            case 2: // stone brick
                swatchLoc = SWATCH_INDEX(6, 3);
                break;
            case 3: // mossy stone brick
                swatchLoc = SWATCH_INDEX(4, 6);
                break;
            case 4: // Cracked Stone Brick
                swatchLoc = SWATCH_INDEX(5, 6);
                break;
            case 5: // Chiseled Stone Brick
                swatchLoc = SWATCH_INDEX(5, 13);
                break;
            }
            break;
        case BLOCK_HUGE_BROWN_MUSHROOM:						// getSwatch
        case BLOCK_HUGE_RED_MUSHROOM:
            inside = SWATCH_INDEX( 14, 8 );	// pores
            outside = SWATCH_INDEX( (type == BLOCK_HUGE_BROWN_MUSHROOM) ? 14 : 13, 7 );
            swatchLoc = inside;
            switch ( dataVal & 0xf )
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
				SWATCH_SWITCH_SIDE(faceDirection, 13, 8);	// stem texture
				break;
			case 14: //14	 Cap texture on all six sides
				swatchLoc = outside;
				break;
			case 15: //15	 Stem texture on all six sides
				swatchLoc = SWATCH_INDEX(13, 8);
				break;
			}
            break;
        case BLOCK_MELON:						// getSwatch
            SWATCH_SWITCH_SIDE( faceDirection, 8, 8 );
            break;
        case BLOCK_MYCELIUM:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 13, 4,  2, 0 );
            break;
        case BLOCK_ENCHANTING_TABLE:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 6,11,  7,11 );
            break;
        case BLOCK_BREWING_STAND:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 13, 9,  12, 9 );
            break;
        case BLOCK_CAULDRON:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 10, 9,  11, 9 );
            break;
        case BLOCK_END_PORTAL_FRAME:						// getSwatch
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 15, 9,  15,10 );
            // TODO: if eye of ender is in place, 0x4 bit, then we should use some
            // new (doesn't exist right now) composited tile of 14, 9 and 14,10
            // Rotate top and bottom
            switch (faceDirection)
            {
            case DIRECTION_BLOCK_BOTTOM:
            case DIRECTION_BLOCK_TOP:
                if (uvIndices) {
                    rotateIndices(localIndices, (dataVal&0x3)*90);
                }
                break;
            default:
                break;
            }

            break;
        case BLOCK_COBBLESTONE_WALL:						// getSwatch
			// TODO - this could be a subroutine, as it's exactly the same as for the other use of BLOCK_COBBLESTONE_WALL here.
			switch (dataVal & 0xf)
			{
			default:
				assert(0);
			case 0:
				// no change, default cobblestone is fine
				break;
			case 1: // mossy cobblestone
				swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrX, gBlockDefinitions[BLOCK_MOSSY_COBBLESTONE].txrY);
				break;
			case 2: // brick wall
				swatchLoc = SWATCH_INDEX(7, 0);
				break;
			case 3: // granite wall
				swatchLoc = SWATCH_INDEX(8, 22);
				break;
			case 4: // diorite wall
				swatchLoc = SWATCH_INDEX(6, 22);
				break;
			case 5: // andesite wall
				swatchLoc = SWATCH_INDEX(4, 22);
				break;
			case 6: // prismarine wall
				swatchLoc = SWATCH_INDEX(12, 22);
				break;
			case 7: // stone brick wall
				swatchLoc = SWATCH_INDEX(6, 3);
				break;
			case 8: // mossy stone brick wall
				swatchLoc = SWATCH_INDEX(4, 6);
				break;
			case 9: // end stone brick wall
				swatchLoc = SWATCH_INDEX(3, 24);
				break;
			case 10: // nether brick wall
				swatchLoc = SWATCH_INDEX(0, 14);
				break;
			case 11: // red nether brick wall
				swatchLoc = SWATCH_INDEX(2, 26);
				break;
			case 12: // sandstone wall
				swatchLoc = SWATCH_INDEX(0, 12);
				break;
			case 13: // red sandstone wall
				swatchLoc = SWATCH_INDEX(5, 22);
				break;
			}
			break;
        case BLOCK_CARPET:						// getSwatch
        case BLOCK_WOOL:
            swatchLoc = retrieveWoolSwatch( dataVal );
            break;
        case BLOCK_STAINED_GLASS:						// getSwatch
        case BLOCK_STAINED_GLASS_PANE:
            // add data value to retrieve proper texture
            swatchLoc = SWATCH_INDEX( gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY ) + dataVal;
            break;
        case BLOCK_COLORED_TERRACOTTA:						// getSwatch
            swatchLoc += dataVal;
            break;
        case BLOCK_HOPPER:						// getSwatch
            // full block version - inside gets used for bottom; a little goofy, but smoother, I think.
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 12,15,  11,15 );
            break;
        case BLOCK_QUARTZ_BLOCK:						// getSwatch
            // use data to figure out which type of quartz and orientation
            switch ( dataVal & 0x7 )
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
            case 3: // pillar quartz block (east-west)
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
            case 4: // pillar quartz block (north-south)
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
        case BLOCK_PRISMARINE:						// getSwatch
            switch ( dataVal&0x7 )
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
		// TODO could give these separate names and colors in the MinewaysMap code
		case BLOCK_SPONGE:						// getSwatch
            switch ( dataVal & 0x1 )
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
        case BLOCK_CHORUS_FLOWER:						// getSwatch
            if ( dataVal == 5 ) {
                // fully mature
                swatchLoc++;
            }
            break;

        case BLOCK_HAY:						// getSwatch
        case BLOCK_PURPUR_PILLAR:
            switch ( type )
            {
            default: // make compiler happy
                assert(0);  // yes, fall-through
            case BLOCK_HAY:
                xloc = 10;
                yloc = 15;
                break;
            case BLOCK_PURPUR_PILLAR:
                xloc = 2;
                yloc = 24;
                break;
            }
            // use data to figure out direction of pillar
            switch ( dataVal & 0xf )
            {
            case 0: // pillar vertical
                SWATCH_SWITCH_SIDE_VERTICAL( faceDirection, xloc-1,yloc, xloc,yloc );
                break;
            case 4: // pillar east-west
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP:
                    swatchLoc = SWATCH_INDEX( xloc-1,yloc );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    swatchLoc = SWATCH_INDEX( xloc-1,yloc );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X:
                    swatchLoc = SWATCH_INDEX( xloc,yloc );
                    break;
                }
                break;
            case 8: // pillar north-south
                switch ( faceDirection )
                {
                case DIRECTION_BLOCK_BOTTOM:
                case DIRECTION_BLOCK_TOP:
                    swatchLoc = SWATCH_INDEX( xloc-1,yloc );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_X:
                case DIRECTION_BLOCK_SIDE_HI_X:
                    swatchLoc = SWATCH_INDEX( xloc-1,yloc );
                    if ( uvIndices )
                        rotateIndices( localIndices, 90 );
                    break;
                case DIRECTION_BLOCK_SIDE_LO_Z:
                case DIRECTION_BLOCK_SIDE_HI_Z:
                    swatchLoc = SWATCH_INDEX( xloc,yloc );
                    break;
                }
                break;
            default:
                assert(0);
            }
            break;

        case BLOCK_GRASS_PATH:						// getSwatch
            // normal block
            SWATCH_SWITCH_SIDE_BOTTOM( faceDirection, 9,24, 2,0 );
            break;

        case BLOCK_COMMAND_BLOCK:						// getSwatch
        case BLOCK_REPEATING_COMMAND_BLOCK:
        case BLOCK_CHAIN_COMMAND_BLOCK:
		case BLOCK_JIGSAW:
            // swatch loc right now is set to the top; bottom is +1, side is +2, conditional is +3
            // Use dataVal*6:
            // Order is: bottom, top, north, south, west, east
            {
                int swatchFaceType[6*6] = { 
                    2,0,2,2,1,2,    // bottom
                    2,1,2,2,0,2,    // top
                    2,2,0,2,2,1,    // north
                    2,2,1,2,2,0,    // south
                    0,2,2,1,2,2,    // west
                    1,2,2,0,2,2     // east
                };
                int swatchAngle[6*6] = { 
                    180,0,180,180,0,180,    // bottom
                    0,0,0,0,0,0,    // top
                    270,0,0,90,0,0,    // north
                    90,180,0,270,180,0,    // south
                    0,90,90,0,270,270,    // west
                    0,270,270,0,90,90     // east
                };

                assert((dataVal&0x7)<6);
                int swatchOffset = swatchFaceType[(dataVal&0x7)*6+faceDirection];
                // need to rotate?
                if ( swatchOffset == 2 )
                {
                    // it's a side
                    if (dataVal & 0x8) {
                        // conditional face, go to one past the side face
                        swatchOffset++;
                    }

                    if ( uvIndices )
                    {
                        int iangle = swatchAngle[(dataVal&0x7)*6+faceDirection];
                        if ( iangle != 0 )
                        {
                            rotateIndices( localIndices, iangle );
                        }
                    }
                }

                // adjust swatch location
                swatchLoc += swatchOffset;
            
                // very very sleazy: set faceDirection "off" so that 
                // "faceDirection == DIRECTION_BLOCK_BOTTOM" at the end of this long method does not affect it.
                faceDirection = -1;
            }
            break;

        case BLOCK_BONE_BLOCK:						// getSwatch
            // bit tricksy: rotate by rotating face direction itself
            newFaceDirection = faceDirection;
            angle = 0;
            flip = 0;
            switch (dataVal & 0xC)
            {
            default:
            case 0x0:
                // as above: newFaceDirection = faceDirection;
                break;
            case 0x4:
                switch (faceDirection)
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
                switch (faceDirection)
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
			SWATCH_SWITCH_SIDE_VERTICAL(newFaceDirection,
                gBlockDefinitions[BLOCK_BONE_BLOCK].txrX-1, gBlockDefinitions[BLOCK_BONE_BLOCK].txrY,	// sides
                gBlockDefinitions[BLOCK_BONE_BLOCK].txrX, gBlockDefinitions[BLOCK_BONE_BLOCK].txrY);	// top
			// probably not quite right, but better than not doing this; at least all the sides, regardless of direction,
			// follow the axis, though may be flipped vertically or horizontally (for the NS and EW axis blocks)
			if (angle != 0 && uvIndices)
				rotateIndices(localIndices, angle);
			if (flip && uvIndices)
				flipIndicesLeftRight(localIndices);
			break;
        
        case BLOCK_STRUCTURE_BLOCK:						// getSwatch
            // use data to figure out which type of structure block
			switch (dataVal & 0x7) {
			case 1:
			default:
				// data - 1
				swatchLoc++;
				break;
			case 2:
				// save - 3
				swatchLoc += 3;
				break;
			case 3:
				// load - 2
				swatchLoc += 2;
				break;
			case 4:
				// corner - 0
				// do nothing
				break;
			}
            break;

        case BLOCK_STATIONARY_WATER:						// getSwatch
        case BLOCK_WATER:
            // sides are flowing, top and bottom is stationary so doesn't change (someday we might figure out the flowing algorithm for non-level top water - TODO)
            // if sides are next to a glass block, use overlay water instead.
            if ((faceDirection != DIRECTION_BLOCK_BOTTOM) && (faceDirection != DIRECTION_BLOCK_TOP))
            {
                neighborType = gBoxData[backgroundIndex + gFaceOffset[faceDirection]].origType;
                swatchLoc = ((neighborType == BLOCK_GLASS) || (neighborType == BLOCK_STAINED_GLASS)) ? SWATCH_INDEX(15, 25) : SWATCH_INDEX(8, 26);
            }
            break;

        case BLOCK_STATIONARY_LAVA:						// getSwatch
        case BLOCK_LAVA:
            // sides are flowing, top and bottom is stationary so doesn't change (someday we might figure out the flowing algorithm for non-level top lava - TODO)
            switch (faceDirection)
            {
            default:
            case DIRECTION_BLOCK_SIDE_LO_X:
            case DIRECTION_BLOCK_SIDE_HI_X:
            case DIRECTION_BLOCK_SIDE_LO_Z:
            case DIRECTION_BLOCK_SIDE_HI_Z:
                swatchLoc = SWATCH_INDEX(9, 26);
                break;
            case DIRECTION_BLOCK_BOTTOM:
            case DIRECTION_BLOCK_TOP:
                // no change, use default
                break;
            }
            break;

		case BLOCK_SMOOTH_STONE:
			switch (dataVal & 0x3)
			{
			default:
				assert(0);
			case 0:
				break;
			case 1: // sandstone
				swatchLoc = SWATCH_INDEX(0, 11);
				break;
			case 2: // red sandstone
				swatchLoc = SWATCH_INDEX(12, 19);
				break;
			case 3: // quartz
				swatchLoc = SWATCH_INDEX(7, 17);
				break;
			}
			break;

		case BLOCK_CORAL_BLOCK:					// getSwatch
		case BLOCK_DEAD_CORAL_BLOCK:
			swatchLoc += (dataVal & 0x7);
			break;

		case BLOCK_CORAL:						// getSwatch
		case BLOCK_CORAL_FAN:
		case BLOCK_DEAD_CORAL_FAN:
		case BLOCK_CORAL_WALL_FAN:
		case BLOCK_DEAD_CORAL_WALL_FAN:
		case BLOCK_DEAD_CORAL:
			swatchLoc += (dataVal&0x7);
			swatchLoc = getCompositeSwatch(swatchLoc, backgroundIndex, faceDirection, 0);
			break;

		case BLOCK_DRIED_KELP:						// getSwatch
			SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 8, 33, 9, 33);
			// flip so wrapping twine lines up.
			if (uvIndices && ((faceDirection == DIRECTION_BLOCK_SIDE_HI_X) || (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z))) {
				flipIndicesLeftRight(localIndices);
			}
			break;

		case BLOCK_SWEET_BERRY_BUSH:				// getSwatch
			swatchLoc += (dataVal & 0x3);
			swatchLoc = getCompositeSwatch(swatchLoc, backgroundIndex, faceDirection, 0);
			break;

		case BLOCK_ANDESITE_DOUBLE_SLAB:				// getSwatch
		case BLOCK_ANDESITE_SLAB:						// getSwatch
			switch (dataVal & 0x7)
			{
			default:
				assert(0);	// fall through
			case 0:
				// fine as is
				break;
			case 1: // polished andesite
				swatchLoc = SWATCH_INDEX(5,22);
				break;
			case 2: // diorite
				swatchLoc = SWATCH_INDEX(6,22);
				break;
			case 3: // polished diorite
				swatchLoc = SWATCH_INDEX(7,22);
				break;
			case 4: // end stone brick
				swatchLoc = SWATCH_INDEX(3,24);
				break;
			case 5: // (the new 1.14) stone slab - not chiseled, more just like normal stone on all sides
				swatchLoc = SWATCH_INDEX(1, 0);
				break;
			}
			break;

		case BLOCK_COMPOSTER:						// getSwatch
			SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 12, 38, 13, 38);
			break;

        case BLOCK_BARREL:
			// copied from piston code - whew, quite the piece of work - but with further rotations for barrel top
			// 0,38 head, 1,38 side, 2,38 bottom, 3,38 open top
			head = bottom = 0;
			dir = dataVal & 7;
			angle = 0;
			switch (dir)
			{
			case 0: // pointing down
			case 1: // pointing up
				if (faceDirection == DIRECTION_BLOCK_BOTTOM)
				{
					head = 1 - dir;
					bottom = dir;
				}
				else if (faceDirection == DIRECTION_BLOCK_TOP)
				{
					head = dir;
					bottom = 1 - dir;
				}
				// else it's a side, and since sides are aligned, rotate all 180 or none
				else
				{
					angle = 180 * (1 - dir);
				}
				break;
			case 2: // pointing north
			case 3: // pointing south
				dirBit = dir - 2;
				if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z)
				{
					head = 1 - dirBit;
					bottom = dirBit;
					angle = 180;
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z)
				{
					head = dirBit;
					bottom = 1 - dirBit;
					angle = 180;
				}
				// else it's a side
				else if ((faceDirection == DIRECTION_BLOCK_BOTTOM) ||
					(faceDirection == DIRECTION_BLOCK_TOP))
				{
					angle = dirBit * 180;
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
				{
					angle = 90 + dirBit * 180;
				}
				else
				{
					angle = 270 + dirBit * 180;
				}
				break;
			case 4: // pointing west
			case 5: // pointing east
				dirBit = dir - 4;
				if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X)
				{
					head = 1 - dirBit;
					bottom = dirBit;
					angle = 180;
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
				{
					head = dirBit;
					bottom = 1 - dirBit;
					angle = 180;
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z)
				{
					angle = 270 + dirBit * 180;
				}
				else if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z)
				{
					angle = 90 + dirBit * 180;
				}
				else
				{
					angle = 270 + dirBit * 180;
				}
				break;
			}
			// ok, now we know head vs. bottom vs. side & angle
			if (head)
			{
				if (dataVal & 8)
				{
					// open
					swatchLoc = SWATCH_INDEX(3, 38);
				}
				else
				{
					// top, closed
					swatchLoc = SWATCH_INDEX(0, 38);
				}
			}
			else if (bottom)
			{
				// easy!
				swatchLoc = SWATCH_INDEX(2, 38);
			}
			else
			{
				// side
				swatchLoc = SWATCH_INDEX(1, 38);
			}
			if (uvIndices && angle != 0)
				rotateIndices(localIndices, angle);
			break;
		case BLOCK_GRINDSTONE:
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_SMOOTH_STONE].txrX, gBlockDefinitions[BLOCK_SMOOTH_STONE].txrY);
			break;
		case BLOCK_STONECUTTER:						// getSwatch
			SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 5, 41, 6, 41);
			break;
		case BLOCK_BELL:
			// use gold - why not?
			swatchLoc = SWATCH_INDEX(gBlockDefinitions[BLOCK_OF_GOLD].txrX, gBlockDefinitions[BLOCK_OF_GOLD].txrY);
			break;
		case BLOCK_SCAFFOLDING:
			SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 9, 40, 106, 40);
			break;
		case BLOCK_BEE_NEST:
			// establish top/side/bottom
			switch (dataVal & BIT_32) {
			default:
			case 0:			// bee nest
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 11, 41, 8, 41);
				break;
			case BIT_32:	// beehive
				swatchLoc = SWATCH_INDEX(13, 41);	// top
				SWATCH_SWITCH_SIDE_BOTTOM(faceDirection, 0, 42, 13, 41);
				break;
			}
			// if a side, we may need to change a face to be a front or front_on
			if ((faceDirection != DIRECTION_BLOCK_TOP) && (faceDirection != DIRECTION_BLOCK_BOTTOM))
			{
				bool filled_with_honey = ((dataVal & 0x1c) >> 2) >= 5;
				switch (dataVal & BIT_32)
				{
				default:
				case 0:			// bee_nest
					frontLoc = SWATCH_INDEX(filled_with_honey ? 10 : 9, 41);
					break;
				case BIT_32:	// beehive
					frontLoc = SWATCH_INDEX(filled_with_honey ? 15 : 14, 41);
					break;
				}
				// facing ESWN
				switch (dataVal & 0x3)
				{
				case 3: // North
					if (faceDirection == DIRECTION_BLOCK_SIDE_LO_Z)
						swatchLoc = frontLoc;
					break;
				case 1: // South
					if (faceDirection == DIRECTION_BLOCK_SIDE_HI_Z)
						swatchLoc = frontLoc;
					break;
				case 2: // West
					if (faceDirection == DIRECTION_BLOCK_SIDE_LO_X)
						swatchLoc = frontLoc;
					break;
				case 0: // East
				default:
					if (faceDirection == DIRECTION_BLOCK_SIDE_HI_X)
						swatchLoc = frontLoc;
					break;
				}
			}
			else
			{
				// top or bottom face - rotation? Not really sure if top of bee_nests rotate, but do it anyway
				switch (dataVal & 0x3)
				{
				case 3: // North -Z
					// no rotation needed
					break;
				case 1: // South +Z
					if (uvIndices) {
						rotateIndices(localIndices, 180);
					}
					break;
				case 2: // West
					if (uvIndices) {
						rotateIndices(localIndices, 270);
					}
					break;
				case 0: // East
				default:
					if (uvIndices) {
						rotateIndices(localIndices, 90);
					}
					break;
				}
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
            // earlier - easier than testing for that special case here)
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
    assert(CHECK_COMPOSITE_OVERLAY);
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
	assert(!gExportTiles);

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
	int tmp;
	tmp = localIndices[0];
	switch ( (angle+360)%360 )
    {
    case 0: // do nothing
        break;
    case 90: // rotate 90 clockwise - you rotate the UV's counterclockwise
        localIndices[0] = localIndices[1];
        localIndices[1] = localIndices[2];
        localIndices[2] = localIndices[3];
		localIndices[3] = tmp;
        break;
    case 180:
        localIndices[0] = localIndices[2];
		localIndices[2] = tmp;
		tmp = localIndices[1];
		localIndices[1] = localIndices[3];
        localIndices[3] = tmp;
        break;
    case 270:
		localIndices[0] = localIndices[3];
		localIndices[3] = localIndices[2];
		localIndices[2] = localIndices[1];
		localIndices[1] = tmp;
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

	assert(u >= 0.0f && u <= 1.0f);
	assert(v >= 0.0f && v <= 1.0f);
	gModel.uvIndexList[gModel.uvIndexCount].uc = (float)col * gModel.textureUVPerSwatch + u * gModel.textureUVPerTile + gModel.invTextureResolution;
    gModel.uvIndexList[gModel.uvIndexCount].vc = 1.0f - ((float)row * gModel.textureUVPerSwatch + (1.0f-v) * gModel.textureUVPerTile + gModel.invTextureResolution);
    gModel.uvIndexList[gModel.uvIndexCount].swatchLoc = swatchLoc;
    gModel.uvIndexCount++;

    // also save what type is associated with this swatchLoc, to allow output of name in comments.
    // Multiple types can be associated with the same swatchLoc, we just save the last one (often the most
    // visible one) here. Could get fancier and also pass in and save dataVal and use RetrieveBlockSubname on output. 
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

    SwatchComposite *pSwatch = gModel.swatchCompositeList;
    while (pSwatch)
    {
        SwatchComposite *pSwatchNext = pSwatch->next;
        free(pSwatch);

        pSwatch = pSwatchNext;
    }
    gModel.swatchCompositeList = gModel.swatchCompositeListEnd = NULL;
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
    vecdot = (float)(1.0/sqrt(vecdot));
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
            pFace->normalIndex = (short)findMatchingNormal( pFace, normal, gModel.normals, gModel.normalListCount );

            // Check if a good match was not found; need to save a new normal if not
            if ( pFace->normalIndex == COMPUTE_NORMAL )
            {
                pFace->normalIndex = (short)addNormalToList( normal, gModel.normals, &gModel.normalListCount, NORMAL_LIST_SIZE );
            }
            assert( pFace->normalIndex != COMPUTE_NORMAL );
        }
// to test if the normal direction saved by the code is in agreement with the normal direction computed.
// We could simply take out the normalIndex == COMPUTE_NORMAL test at the start and have every normal get computed and
// checked, but that would slow things down a bit (probably not all that much...). Anyway, all blocks check out.
#ifdef _DEBUG
        else
        {
            Vector tnormal;
            int index = findMatchingNormal( pFace, tnormal, gModel.normals, gModel.normalListCount );
            if (pFace->normalIndex != index) {
                // put a break on this line to see any problem
                assert(0); index;
            }
        }
#endif
    }
}

bool isASubblock(int type, int dataVal)
{
	switch (type) {
	case BLOCK_GRASS:
		// special case: Grass, the default popular name for that block type, actually has a data value of 1; the dead shrub is 0
		if ((dataVal & 0x3) == 1)
			return false;
		break;
	case BLOCK_PURPUR_SLAB:
		// special case: Purpur slab, 0 and 1 are the same thing, from what I can tell
		if (dataVal == 0 || dataVal == 1)
			return false;
		break;
	case BLOCK_PUMPKIN:
		// only if dataVal == 4 is it not carved
		if (dataVal & 0x4)
			return false;
		break;

	// All of these have colors or types or other characteristics, for all versions, beyond the generic name such as "Wool"
	case BLOCK_WOOL:
	case BLOCK_STAINED_GLASS:
	case BLOCK_STAINED_GLASS_PANE:
	case BLOCK_CARPET:
	case BLOCK_CONCRETE:
	case BLOCK_CONCRETE_POWDER:
		// Wool wants to be White Wool when it's a subblock, so the default block name is not OK
	case BLOCK_COLORED_TERRACOTTA:
		// special case: the block is "colored terracotta" but subblocks have color, so always use subblock color if requested
	case BLOCK_HEAD:
		// which mob head, if specified?
	case BLOCK_SIGN_POST:
	case BLOCK_ACACIA_SIGN_POST:
	case BLOCK_WALL_SIGN:
		// each have names
	case BLOCK_CORAL_BLOCK:
	case BLOCK_CORAL:
	case BLOCK_CORAL_FAN:
	case BLOCK_CORAL_WALL_FAN:
	case BLOCK_DEAD_CORAL_BLOCK:
	case BLOCK_DEAD_CORAL:
	case BLOCK_DEAD_CORAL_FAN:
	case BLOCK_DEAD_CORAL_WALL_FAN:
		// corals are named when subblocks
	case BLOCK_REDSTONE_WIRE:
		// so that the power is always shown
	case BLOCK_PUMPKIN_STEM:
	case BLOCK_MELON_STEM:
		// so that the age is always shown
		return true;

	default:
		if (dataVal == 0)
			return false;
		break;
	}
	return true;
}

// return 0 if no write
static int writeOBJBox(WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC)
{
    // set to 1 if you want absolute (positive) indices used in the faces
    int absoluteIndices = (gOptions->exportFlags & EXPT_OUTPUT_OBJ_REL_COORDINATES) ? 0 : 1;

#ifdef WIN32
    DWORD br;
#endif
    wchar_t objFileNameWithSuffix[MAX_PATH_AND_FILE];

    char outputString[MAX_PATH_AND_FILE];
    char mtlName[MAX_PATH_AND_FILE];

    int i, j, groupCount, index, x, y;

    unsigned char outputMaterial[NUM_BLOCKS];

    int exportMaterials;

    int retCode = MW_NO_ERROR;

    char worldNameUnderlined[256];
    int prevType;

    FaceRecord *pFace;

    char worldChar[MAX_PATH_AND_FILE];

	float resScale;

	int vt[4];

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

    sprintf_s(outputString,256,"# Wavefront OBJ file made by Mineways version %d.%02d, http://mineways.com\n", gMajorVersion, gMinorVersion );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    const char *justWorldFileName;
    WcharToChar(pWorldGuide->world, worldChar, MAX_PATH_AND_FILE);
    justWorldFileName = removePathChar(worldChar);

    retCode |= writeStatistics(gModelFile, pWorldGuide, worldBox, tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
    if ( retCode >= MW_BEGIN_ERRORS )
        goto Exit;

    // If we use materials, say where the file is
    if ( exportMaterials )
    {
        char justMtlFileName[MAX_PATH_AND_FILE];
        sprintf_s(justMtlFileName, MAX_PATH_AND_FILE, "%s.mtl", gOutputFileRootCleanChar);

		sprintf_s(outputString, 256, "\nmtllib %s\n", justMtlFileName);
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
		if (gExportTiles ) {
			// in this case we need to now merge all the UV tiles into a single list, as there are a lot of duplicate values repeated again and again for separate swatches
			// We also need to make a "translation layer" from the old ID to the new ID that will get used when writing the faces themselves.
			// We use a 17x17 array of all the possible UV coords, then just whip through list and mark the ones used, then use this to output the valid ones.
			// This list then gets filled with the new index to use, 0 if not used, i.e., if count is >=1, then give it the next ID (start at 1, since that's what we'll actually output).
			// Then as we go through the face list we translate again, find the loc, and in the array we find the new UV index to us
			resScale = 16.0f / gModel.tileSize;
			for (i = 0; i < gModel.uvIndexCount; i++)
			{
				// Unscramble the eggs. Given a UV, multiply by the resolution of the map. This give 0-512 or whatever, really 1-511.
				// Modulo the swatchSize. This gives 0-18 inclusive (or whatever the swatch size is), really 1-17.
				// Subtract 1 to give 0-16 or whatever. Divide by tileSize and multiply by 16 to get the 0-16 index location
				// the minus one is to get rid of the guard band and keep the numbers inside the 0-17 range
				index = (int)((((int)(gModel.uvIndexList[i].uc * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale) +
					(NUM_UV_GRID_RESOLUTION + 1) * (int)(16-((((int)((1.0f-gModel.uvIndexList[i].vc) * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale));
				assert((int)((((int)(gModel.uvIndexList[i].uc * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale) >= 0);	// TODOTODOTODO test
				assert((int)((((int)(gModel.uvIndexList[i].uc * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale) <= 16);
				assert((int)(16 - ((((int)((1.0f - gModel.uvIndexList[i].vc) * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale)) >= 0);
				assert((int)(16 - ((((int)((1.0f - gModel.uvIndexList[i].vc) * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale)) <= 16);
				gModel.uvGridList[index]++;
			}
			// Now go through grid and output the vt values that are used.
			index = 0;
			for (y = 0; y < NUM_UV_GRID_RESOLUTION+1; y++) {
				for (x = 0; x < NUM_UV_GRID_RESOLUTION+1; x++) {
					if (gModel.uvGridList[index] > 0) {
						// with this location noted as used, now set it to the UV index the face lists will use, starting with 1.
						gModel.uvGridList[index] = ++gModel.uvGridListCount;
						retCode |= writeOBJTextureUV((float)x / (float)NUM_UV_GRID_RESOLUTION, (float)y / (float)NUM_UV_GRID_RESOLUTION, false, 0);
						if (retCode >= MW_BEGIN_ERRORS)
							goto Exit;
					}
					index++;
				}
			}
		}
		else {
			// just output as-is
			int prevSwatch = -1;
			for (i = 0; i < gModel.uvIndexCount; i++)
			{
				retCode |= writeOBJTextureUV(gModel.uvIndexList[i].uc, gModel.uvIndexList[i].vc, prevSwatch != gModel.uvIndexList[i].swatchLoc, gModel.uvIndexList[i].swatchLoc);
				prevSwatch = gModel.uvIndexList[i].swatchLoc;
				if (retCode >= MW_BEGIN_ERRORS)
					goto Exit;
			}
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
    int prevDataVal = -1;
	int prevSwatchLoc = -1;
    groupCount = 0;
    // outputMaterial notes when a material is used for the first time;
    // should only be needed for when objects are not sorted by material (grouped by block).
    memset(outputMaterial,0,sizeof(outputMaterial));

    // test for a single material output. If so, do it now and reset materials in general
    if ( exportMaterials )
    {
        // should there be just one single material in this OBJ file?
        if ( !(gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK) )
        {
            sprintf_s(outputString,256,"\nusemtl %s\n", MINECRAFT_SINGLE_MATERIAL);
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
        }
    }

    bool subtypeMaterial = ((gOptions->exportFlags & EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE) != 0x0);

	// how often to update progress? Add some minimum check of 1000 faces, plus true # of faces per 1%.
	int progCheck = (int)((float)gModel.faceCount / (100.0f * 0.5f*(PG_TEXTURE - PG_OUTPUT))) + 1000;

    for ( i = 0; i < gModel.faceCount; i++ )
    {
		// every 1% or so check on progress
        if ( i % progCheck == 0 )
            UPDATE_PROGRESS( PG_OUTPUT + 0.5f*(PG_TEXTURE-PG_OUTPUT) + 0.5f*(PG_TEXTURE-PG_OUTPUT)*((float)i/(float)gModel.faceCount));

        if ( exportMaterials )
        {
            // should there be more than one material or group output in this OBJ file?
            if ( gOptions->exportFlags & (EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK|EXPT_OUTPUT_OBJ_SEPARATE_TYPES) )
            {
				bool newGroupPossible = (prevType != gModel.faceList[i]->materialType) ||
					(subtypeMaterial && (prevDataVal != gModel.faceList[i]->materialDataVal));
                // did we reach a new material?
                if ( newGroupPossible ||
					(gExportTiles && (prevSwatchLoc != gModel.uvIndexList[gModel.faceList[i]->uvIndex[0]].swatchLoc)))
                {
                    // New material definitely found, so make a new one to be output.
                    prevType = gModel.faceList[i]->materialType;
                    prevDataVal = gModel.faceList[i]->materialDataVal;
					prevSwatchLoc = gModel.uvIndexList[gModel.faceList[i]->uvIndex[0]].swatchLoc;
                    // New ID encountered, so output it: material name, and group.
                    // Group isn't really required, but can be useful.
                    // Output group only if we're not already using it for individual blocks.
                    strcpy_s(mtlName,256,gBlockDefinitions[prevType].name);

                    if (subtypeMaterial && isASubblock(prevType, prevDataVal) ) {
                        // use subtype name or add a dataval suffix.
                        // If possible, turn these data values into the actual sub-material type names.
						const char *subName = RetrieveBlockSubname(prevType, prevDataVal);
                        char tempString[MAX_PATH_AND_FILE];
						if (strcmp(subName, mtlName) == 0) {
							// No unique subname found for this data value, so use the data value.
							// Shouldn't ever hit here, actually; all things should be named by now.
							sprintf_s(tempString, 256, "%s__%d", mtlName, prevDataVal);
							strcpy_s(mtlName, 256, tempString);
						}
						else {
							// Name does not match, so use it
							// was: sprintf_s(tempString, 256, "%s__%s", mtlName, subName);
							strcpy_s(mtlName, 256, subName);
						}
                    }

                    // substitute ' ' to '_'
                    spacesToUnderlinesChar( mtlName );
                    // usemtl materialName
                    if (gOptions->exportFlags & EXPT_GROUP_BY_BLOCK)
                    {
                        // output every block individually
                        sprintf_s(outputString, 256, "\nusemtl %s\n", mtlName);
                        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
                        if (!(gOptions->exportFlags & EXPT_OUTPUT_EACH_BLOCK_A_GROUP))
                        {
                            // new group for objects of same type (which are sorted)
                            sprintf_s(outputString, 256, "g %s\n", mtlName);
                            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
                        }
                        if (subtypeMaterial) {
                            // We can't use outputMaterial, a simple array of types. We need to
                            // instead check the whole previous list and see if the material's
                            // already on it. Check from last to first for speed, I hope.
							// NODO: slightly better (though slower) would be to add by name, vs. typeData;
							// there are materials with different typeData's but that actually have the same name,
							// such as Purpur Block, but these show up only in the test world, so don't bother.
                            int curCount = gModel.mtlCount-1;
                            unsigned int typeData = prevType << 8 | prevDataVal;
                            while (curCount >= 0) {
                                if (gModel.mtlList[curCount--] == typeData) {
                                    // found it; exit loop
                                    curCount = -999;
                                }
                            }
                            // did we find it?
                            if (curCount != -999) {
                                // did not find type/data combinationTOD - add it to list
                                gModel.mtlList[gModel.mtlCount++] = typeData;
                                assert(gModel.mtlCount < NUM_SUBMATERIALS);
                            }
                        }
                        else {
                            // note which material is to be output, if not output already
                            if (outputMaterial[prevType] == 0)
                            {
                                gModel.mtlList[gModel.mtlCount++] = prevType << 8 | prevDataVal;
                                assert(gModel.mtlCount < NUM_SUBMATERIALS);
                                outputMaterial[prevType] = 1;
                            }
                        }
                    }
                    else
                    {
                        // don't output by individual block: output by group, and/or by material, or by tile, or none at all (one material for scene)
                        strcpy_s(outputString, 256, "\n");
                        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));


						// don't output group if just the tile ID differed and the block type didn't
                        if ((gOptions->exportFlags & EXPT_OUTPUT_OBJ_SEPARATE_TYPES) && newGroupPossible)
                        {
                            // new group for objects of same type (which are sorted)
                            sprintf_s(outputString, 256, "g %s\n", mtlName);
                            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
                        }
						if (gExportTiles) {
							// new material per tile ID
							// swatch locations exactly correspond with tiles.h names
							assert(prevSwatchLoc < TOTAL_TILES);
							WcharToChar(gTilesTable[prevSwatchLoc].filename, mtlName, MAX_PATH_AND_FILE);
							sprintf_s(outputString, 256, "usemtl %s\n", mtlName);
							WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
							// note in an array that this separate tile should be output as a material
							gModel.tileList[prevSwatchLoc] = true;
							assert(gModel.mtlCount < NUM_SUBMATERIALS);
						}
                        else if (gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK)
                        {
                            // new material per object
                            sprintf_s(outputString, 256, "usemtl %s\n", mtlName);
                            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
                            gModel.mtlList[gModel.mtlCount++] = prevType <<8 | prevDataVal;
                            assert(gModel.mtlCount < NUM_SUBMATERIALS);
                        }
                        // else don't output material, there's only one for the whole scene
                    }
                }
            }
        }

        // output the actual face
        pFace = gModel.faceList[i];

        // if we're outputting each individual block in a group, set a unique group name here.
        if ((gOptions->exportFlags & EXPT_OUTPUT_EACH_BLOCK_A_GROUP) && pFace->faceIndex <= 0)
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
			// if output per tile, then we use the UV values to find the index to the new index in the grid
			if (gExportTiles) {
				resScale = 16.0f / gModel.tileSize;
				for (j = 0; j < 4; j++) {
					index = (int)((((int)(gModel.uvIndexList[pFace->uvIndex[j]].uc * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale) +
						(NUM_UV_GRID_RESOLUTION + 1) * (int)(16 - ((((int)((1.0f - gModel.uvIndexList[pFace->uvIndex[j]].vc) * (float)gModel.textureResolution) % gModel.swatchSize) - 1.0f)*resScale));
					vt[j] = gModel.uvGridList[index];
				}
			}
			else {
				// else just get the UVs
				for (j = 0; j < 4; j++) {
					vt[j] = pFace->uvIndex[j] + 1;
				}
			}
			
			// with normals - not really needed by most renderers, but good to include;
            // GLC, for example, does smoothing if normals are not present.
            // Check if last two vertices match - if so, output a triangle instead 
            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // triangle
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]+1, vt[0], outputFaceDirection,
                        pFace->vertexIndex[1]+1, vt[1], outputFaceDirection,
                        pFace->vertexIndex[2]+1, vt[2], outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, vt[0]-gModel.uvIndexCount-1, outputFaceDirection,
                        pFace->vertexIndex[1]-gModel.vertexCount, vt[1]-gModel.uvIndexCount-1, outputFaceDirection,
                        pFace->vertexIndex[2]-gModel.vertexCount, vt[2]-gModel.uvIndexCount-1, outputFaceDirection
                        );
                }
            }
            else
            {
                // Output a quad
                // if normal sums negative, rotate order by one so that dumb tessellators
                // match up the faces better, which should make matching face removal work better. I hope.
                int offset = 0;
                int idx = pFace->normalIndex;
                if (gModel.normals[idx][X] + gModel.normals[idx][Y] + gModel.normals[idx][Z] < 0.0f)
                    offset = 1;

                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[offset] + 1, vt[offset], outputFaceDirection,
                        pFace->vertexIndex[offset + 1] + 1, vt[offset + 1], outputFaceDirection,
                        pFace->vertexIndex[offset + 2] + 1, vt[offset + 2], outputFaceDirection,
                        pFace->vertexIndex[(offset + 3) % 4] + 1, vt[(offset + 3) % 4], outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d\n",
                        pFace->vertexIndex[offset] - gModel.vertexCount, vt[offset] - gModel.uvIndexCount - 1, outputFaceDirection,
                        pFace->vertexIndex[offset + 1] - gModel.vertexCount, vt[offset + 1] - gModel.uvIndexCount - 1, outputFaceDirection,
                        pFace->vertexIndex[offset + 2]-gModel.vertexCount, vt[offset + 2]-gModel.uvIndexCount - 1, outputFaceDirection,
                        pFace->vertexIndex[(offset + 3) % 4]-gModel.vertexCount, vt[(offset + 3) % 4]-gModel.uvIndexCount - 1, outputFaceDirection
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
                        pFace->vertexIndex[0]+1, vt[0],
                        pFace->vertexIndex[1]+1, vt[1],
                        pFace->vertexIndex[2]+1, vt[2]
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, vt[0]-gModel.uvIndexCount - 1,
                        pFace->vertexIndex[1]-gModel.vertexCount, vt[1]-gModel.uvIndexCount - 1,
                        pFace->vertexIndex[2]-gModel.vertexCount, vt[2]-gModel.uvIndexCount - 1
                        );
                }
            }
            else
            {
                if ( absoluteIndices )
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]+1, vt[0],
                        pFace->vertexIndex[1]+1, vt[1],
                        pFace->vertexIndex[2]+1, vt[2],
                        pFace->vertexIndex[3]+1, vt[3]
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d/%d %d/%d %d/%d %d/%d\n",
                        pFace->vertexIndex[0]-gModel.vertexCount, vt[0]-gModel.uvIndexCount - 1,
                        pFace->vertexIndex[1]-gModel.vertexCount, vt[1]-gModel.uvIndexCount - 1,
                        pFace->vertexIndex[2]-gModel.vertexCount, vt[2]-gModel.uvIndexCount - 1,
                        pFace->vertexIndex[3]-gModel.vertexCount, vt[3]-gModel.uvIndexCount - 1
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
                // Output a quad
                // if normal sums negative, rotate order by one so that dumb tessellators
                // match up the faces better, which should make matching face removal work better. I hope.
                int offset = 0;
                int idx = pFace->normalIndex;
                if (gModel.normals[idx][X] + gModel.normals[idx][Y] + gModel.normals[idx][Z] < 0.0f)
                    offset = 1;
                if (absoluteIndices)
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[offset] + 1, outputFaceDirection,
                        pFace->vertexIndex[offset + 1] + 1, outputFaceDirection,
                        pFace->vertexIndex[offset + 2] + 1, outputFaceDirection,
                        pFace->vertexIndex[(offset + 3) % 4] + 1, outputFaceDirection
                        );
                }
                else
                {
                    sprintf_s(outputString,256,"f %d//%d %d//%d %d//%d %d//%d\n",
                        pFace->vertexIndex[offset]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[offset + 1] - gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[offset + 2]-gModel.vertexCount, outputFaceDirection,
                        pFace->vertexIndex[(offset + 3) % 4]-gModel.vertexCount, outputFaceDirection
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
		if (swatchLoc < TOTAL_TILES) {
			char outName[MAX_PATH_AND_FILE];
			WcharToChar(gTilesTable[swatchLoc].filename, outName, MAX_PATH_AND_FILE);
			assert(strlen(outName) > 0);
			sprintf_s(outputString, 1024, "# %s\nvt %g %g\n",
				outName,
				u, v);
		}
		else {
			// old "by block" code, not really useful - better to use tiles.h name
			sprintf_s(outputString, 1024, "# %s type\nvt %g %g\n",
				gBlockDefinitions[gModel.uvSwatchToType[swatchLoc]].name,
				u, v);
		}
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
	
	wchar_t mtlFileName[MAX_PATH_AND_FILE];
    char outputString[2048];

    char textureRGB[MAX_PATH_AND_FILE];
    char textureRGBA[MAX_PATH_AND_FILE];
    char textureAlpha[MAX_PATH_AND_FILE];

	int i, num;
	int retCode;
	char mtlName[MAX_PATH_AND_FILE];

    concatFileName3(mtlFileName, gOutputFilePath, gOutputFileRootClean, L".mtl");

    gMtlFile=PortaCreate(mtlFileName);
    addOutputFilenameToList(mtlFileName);
    if (gMtlFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

	if (gExportTiles) {
		for (i = 0; i < TOTAL_TILES; i++) {
			// tile name is material name, period
			if (gModel.tileList[i]) {
				gModel.tileListCount++;
			}
		}
		num = gModel.tileListCount;
	}
	else {
		num = (gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK) ? gModel.mtlCount : 1;
	}
	sprintf_s(outputString, 2048, "Wavefront OBJ material file\n# Contains %d materials\n", num);
	WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString) ));

    if (gExportTexture && !gExportTiles)
    {
        // Write them out! We need three texture file names: -RGB, -RGBA, -Alpha.
        // The RGB/RGBA split is needed for fast previewers like G3D to gain additional speed
        // The all-alpha image is needed for various renderers to properly read cutouts, since map_d is poorly defined
        sprintf_s(textureRGB,MAX_PATH_AND_FILE,"%s%s.png",gOutputFileRootCleanChar,PNG_RGB_SUFFIXCHAR);
        sprintf_s(textureRGBA,MAX_PATH_AND_FILE,"%s%s.png",gOutputFileRootCleanChar,PNG_RGBA_SUFFIXCHAR);
        sprintf_s(textureAlpha,MAX_PATH_AND_FILE,"%s%s.png",gOutputFileRootCleanChar,PNG_ALPHA_SUFFIXCHAR);
    }

	// for 3D prints, only usesRGB is true, if texture is output
	// for Sketchfab export, only RGBA is needed
	// for other rendering, either only RGB is needed (no alphas found), or RGBA/RGB/A are all output
    if ( !(gOptions->exportFlags & EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK) )
    {
        // output a single material - should not really affect per tile output, but just in case
        if ( gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES )
        {
            if ( gPrint3D )
            {
                gModel.usesRGB = 1;
                gModel.usesAlpha = 0;
            }
            else
            {
                // cutouts or alpha
                // we could be more clever here and go through all the materials to see which are needed,
                // but we'd basically have to copy the code further below for who has what alpha, etc.
                gModel.usesRGBA = 1;
                gModel.usesAlpha = 1;
            }

            sprintf_s(outputString, 2048,
                "\nnewmtl %s\n"
                "Kd 1 1 1\n"
                "Ks 0 0 0\n"
                "# for G3D, to make textures look blocky:\n"
                "interpolateMode NEAREST_MAGNIFICATION_TRILINEAR_MIPMAP_MINIFICATION\n"
                "map_Kd %s\n"
                ,
                MINECRAFT_SINGLE_MATERIAL,
                gModel.usesRGB ? textureRGB : textureRGBA);
            WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString)));

            if (gModel.usesAlpha) {
                sprintf_s(outputString, 2048,
                    "%smap_d %s\n", (gExportTiles || g3d) ? "#":"", textureAlpha);
                WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString)));
            }
        }
        else
        {
            sprintf_s(outputString,2048,
                "\nnewmtl %s\n"
                "Kd 1 1 1\n"
                "Ks 0 0 0\n"
                ,
                MINECRAFT_SINGLE_MATERIAL );
            WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString)));
        }
    }
    else
    {
        // output materials
		if (gExportTiles) {
			// Go through all tiles and see which are used
			for (i = 0; i < TOTAL_TILES; i++) {
				// tile name is material name, period
				if (gModel.tileList[i]) {
					// tile found that should be output
					WcharToChar(gTilesTable[i].filename, mtlName, MAX_PATH_AND_FILE);
					// output material
					sprintf_s(textureRGBA, MAX_PATH_AND_FILE, "%s.png", mtlName);
					if (strlen(gOptions->pEFD->tileDirString) > 0) {
						char fullPathName[MAX_PATH_AND_FILE];
						sprintf_s(fullPathName, MAX_PATH_AND_FILE, "%s/%s", gOptions->pEFD->tileDirString, textureRGBA);
						strcpy_s(textureRGBA, fullPathName);
					}
					retCode = writeOBJFullMtlDescription(mtlName, gTilesTable[i].typeForMtl, textureRGBA, textureRGBA, textureRGBA);
					if (retCode != MW_NO_ERROR)
						return retCode;
				}
			}
		}
		else {
			for (i = 0; i < gModel.mtlCount; i++)
			{
				int type;
				int dataVal = 0;

				bool subtypeMaterial = ((gOptions->exportFlags & EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE) != 0x0);

				type = gModel.mtlList[i] >> 8;
				if (subtypeMaterial)
					dataVal = gModel.mtlList[i] & 0xff;

				// print header: material name
				strcpy_s(mtlName, 256, gBlockDefinitions[type].name);

				// add a dataval suffix:
				// If possible, turn these data values into the actual sub-material type names.
				if (subtypeMaterial && isASubblock(type, dataVal)) {
					// use subtype name or add a dataval suffix.
					// If possible, turn these data values into the actual sub-material type names.
					const char *subName = RetrieveBlockSubname(type, dataVal);
					char tempString[MAX_PATH_AND_FILE];
					if (strcmp(subName, mtlName) == 0) {
						// No unique subname found for this data value, so use the data value.
						// Shouldn't ever hit here, actually; all things should be named by now.
						sprintf_s(tempString, 256, "%s__%d", mtlName, dataVal);
						strcpy_s(mtlName, 256, tempString);
					}
					else {
						// Name does not match, so use it
						// was: sprintf_s(tempString, 256, "%s__%s", mtlName, subName);
						strcpy_s(mtlName, 256, subName);
					}
				}
				spacesToUnderlinesChar(mtlName);

				retCode = writeOBJFullMtlDescription(mtlName, type, textureRGB, textureRGBA, textureAlpha);
				if (retCode != MW_NO_ERROR)
					return retCode;
			}
		}
    }

    PortaClose(gMtlFile);

    return MW_NO_ERROR;
}

static int writeOBJFullMtlDescription(char *mtlName, int type, char *textureRGB, char *textureRGBA, char *textureAlpha)
{
#ifdef WIN32
	DWORD br;
#endif

	char outputString[2048];
	char tfString[256];
	char mapdString[256];
	char mapKeString[256];
	char keString[256];
	char *typeTextureFileName;
	char fullMtl[256];
	double alpha;
	double fRed, fGreen, fBlue;
	double ka, kd, ks;

	if (gOptions->exportFlags & EXPT_OUTPUT_OBJ_FULL_MATERIAL)
	{
		// Use full material description, and include illumination model.
		// Works by uncommenting lines where "fullMtl" is used in the output.
		// Really currently tailored for G3D, and things like Tr are commented out always.
		strcpy_s(fullMtl, 256, "");
	}
	else
	{
		// don't use full material, comment it out, just output the basics
		strcpy_s(fullMtl, 256, "# ");
	}

	// if we want a neutral material, set to white
	// was: if (gOptions->exportFlags & EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL)
	// In fact, texture should be multiplied by color, according to the spec: http://paulbourke.net/dataformats/mtl/
	// So use a white color if we're outputting a texture, since it could get multiplied. This
	// will make Blender previewing look all-white, but people should turn on texturing anyway.
	if (gExportTexture)
	{
		fRed = fGreen = fBlue = 1.0f;
	}
	else
	{
		// use color in file, which is nice for previewing. Blender and 3DS MAX like this, for example. G3D does not.
		fRed = (gBlockDefinitions[type].color >> 16) / 255.0f;
		fGreen = ((gBlockDefinitions[type].color >> 8) & 0xff) / 255.0f;
		fBlue = (gBlockDefinitions[type].color & 0xff) / 255.0f;
	}

	// good for blender:
	ka = 0.2;
	kd = 1.0;
	ks = 0.0;
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
		if (gDebugTransparentType == type)
		{
			alpha = DEBUG_DISPLAY_ALPHA;
		}
		else
		{
			alpha = 1.0f;
		}
	}
	else if (gPrint3D)
	{
		// for 3d printing, alpha is always 1.0
		alpha = 1.0f;
	}
	// if semitransparent, and a truly transparent thing, then alpha is used; otherwise it's probably a cutout and the overall alpha should be 1.0f for export
	// (this is true for glass panes, but stained glass pains are semitransparent)
	if (alpha < 1.0f && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) && !(gBlockDefinitions[type].flags & BLF_TRANSPARENT))
	{
		alpha = 1.0f;
	}

	if (alpha < 1.0f)
	{
		// semitransparent block, such as water
		gModel.usesRGBA = 1;
		gModel.usesAlpha = 1;
		sprintf_s(tfString, 256, "%sTf %g %g %g\n", fullMtl, 1.0f - (float)(fRed*alpha), 1.0f - (float)(fGreen*alpha), 1.0f - (float)(fBlue*alpha));
	}
	else
	{
		tfString[0] = '\0';
	}

	// export map_d only if CUTOUTS.
	if (!gPrint3D &&
        (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) && 
		(alpha < 1.0 || (gBlockDefinitions[type].flags & BLF_CUTOUTS)) &&
		!(gOptions->pEFD->chkLeavesSolid && (gBlockDefinitions[type].flags & BLF_LEAF_PART)))
	{
		// cutouts or alpha
#ifdef SKETCHFAB
		if ((gOptions->exportFlags & EXPT_SKFB))
		{
			// for Sketchfab, don't need alpha map
			gModel.usesRGBA = 1;
			gModel.usesAlpha = 0;
			typeTextureFileName = textureRGBA;
			sprintf_s(mapdString, 256, "map_d %s\n", typeTextureFileName);
		}
		else
#endif
		{
			// otherwise, always export both, if not exporting tiles, since we don't know what the modeler likes
			// When exporting tiles, assume map_d is not needed, as map_Kd will have alphas if needed - spec is unclear here
			gModel.usesRGBA = 1;
			gModel.usesAlpha = 1;
			typeTextureFileName = textureRGBA;
			sprintf_s(mapdString, 256, "%smap_d %s\n", (gExportTiles || g3d) ? "#" : "", textureAlpha);
		}
	}
	else
	{
		if (gExportTexture)
		{
#ifdef SKETCHFAB
			if (gOptions->exportFlags & EXPT_SKFB)
			{
				gModel.usesRGBA = 1;
				typeTextureFileName = textureRGBA;
			}
			else
#endif
			{
				gModel.usesRGB = 1;
				typeTextureFileName = textureRGB;
			}
		}
		else
		{
			typeTextureFileName = '\0';
		}
		mapdString[0] = '\0';
	}
	// TODO it would be nice if, when various light blocks (such as campfire) are not actually on, they could be set to not be an emitter
	if (!gPrint3D && (gBlockDefinitions[type].flags & BLF_EMITTER))
	{
		sprintf_s(keString, 256, "Ke 1 1 1\n");
		if (gExportTexture)
		{
			sprintf_s(mapKeString, 256, "map_Ke %s\n", typeTextureFileName);
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

	// Any last-minute adjustments due to material?
	// I like to give the water a slight reflectivity, it's justifiable. Same with glass.
	// Simplify this to all transparent surfaces, which are likely to be shiny, except for
	// BLOCK_FROSTED_ICE (hey, it's frosted).
	if ((gBlockDefinitions[type].read_alpha < 1.0f) && (type != BLOCK_FROSTED_ICE)) {
		// just a little - too much in G3D reflects the sun too much
		ks = 0.03;
	}

	if (gExportTexture)
	{
		sprintf_s(outputString, 2048,
			"\nnewmtl %s\n"
			"%sNs 0\n"	// specular highlight power
			"%sKa %g %g %g\n"
			"Kd %g %g %g\n"
			"Ks %g %g %g\n"
			"%s" // emissive
			"%smap_Ka %s\n"
			"# for G3D, to make textures look blocky:\n"
			"interpolateMode NEAREST_MAGNIFICATION_TRILINEAR_MIPMAP_MINIFICATION\n"
			"map_Kd %s\n"
			"%s" // map_d, if there's a cutout
			"%s"	// map_Ke
			// "Ni 1.0\n" - Blender likes to output this - no idea what it is
			"%sillum %d\n"
			"# d %g\n"	// some use Tr here - Blender likes "d"
			"# Tr %g\n"	// we put both, in hopes of helping both types of importer; comment out one, as 3DS MAX doesn't like it; Tr 1 means fully visible in some systems, fully transparent in others, including G3D, so left out
			"%s"	//Tf, if needed
			,
			// colors are premultiplied by alpha, Wavefront OBJ doesn't want that
			mtlName,
			fullMtl,
			fullMtl, (float)(fRed*ka), (float)(fGreen*ka), (float)(fBlue*ka),
			(float)(fRed*kd), (float)(fGreen*kd), (float)(fBlue*kd),
			(float)(fRed*ks), (float)(fGreen*ks), (float)(fBlue*ks),
			keString,
			fullMtl, typeTextureFileName,
			typeTextureFileName,
			mapdString,
			mapKeString,
			fullMtl, (alpha < 1.0f ? 4 : 2), // ray trace if transparent overall, e.g. water
			(float)(alpha),
			(float)(1.0f-alpha),
			tfString);
	}
	else
	{
		sprintf_s(outputString, 2048,
			"\nnewmtl %s\n"
			"%sNs 0\n"	// specular highlight power
			"%sKa %g %g %g\n"
			"Kd %g %g %g\n"
			"Ks %g %g %g\n"
			"%s%s" // emissive
			// "Ni 1.0\n" - Blender likes to output this - no idea what it is
			"%sillum %d\n"
			"d %g\n"	// some use Tr here - Blender likes "d"
			"Tr %g\n"	// we put both, in hopes of helping both types of importer
			"%s%s\n"	//Tf, if needed
			,
			// colors are premultiplied by alpha, Wavefront OBJ doesn't want that
			mtlName,
			fullMtl,
			fullMtl, (float)(fRed*ka), (float)(fGreen*ka), (float)(fBlue*ka),
			(float)(fRed*kd), (float)(fGreen*kd), (float)(fBlue*kd),
			(float)(fRed*ks), (float)(fGreen*ks), (float)(fBlue*ks),
			fullMtl, keString,
			fullMtl, (alpha < 1.0f ? 4 : 2), // ray trace if transparent overall, e.g. water
			(float)(alpha),
			(float)(1.0f-alpha),
			fullMtl, tfString);
	}
	WERROR(PortaWrite(gMtlFile, outputString, strlen(outputString)));

	return MW_NO_ERROR;
}


// all the blocks that need premultiplication by a color.
// See http://www.minecraftwiki.net/wiki/File:TerrainGuide.png
#define MULT_TABLE_SIZE 26
#define MULT_TABLE_NUM_GRASS	8
#define MULT_TABLE_NUM_FOLIAGE	(MULT_TABLE_NUM_GRASS+4)
#define MULT_TABLE_NUM_WATER	(MULT_TABLE_NUM_FOLIAGE+3)
static TypeTile multTable[MULT_TABLE_SIZE] = {
    { BLOCK_GRASS_BLOCK /* grass */, 0,0, {0,0,0} },
    { BLOCK_GRASS_BLOCK /* side grass overlay */, 6, 2, {0,0,0} },
    //{ BLOCK_GRASS_BLOCK /* unused grass, now a workspace */, 8, 2, {0,0,0} },
    { BLOCK_GRASS /* tall grass */, 7, 2, {0,0,0} },
    { BLOCK_GRASS /* fern */, 8, 3, {0,0,0} },
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
    { BLOCK_WATER /* water */, 15, 13, { 0, 0, 0 } },
    { BLOCK_WATER /* water */, 15, 25, { 0, 0, 0 } },
    { BLOCK_WATER /* water */, 8, 26, { 0, 0, 0 } },

    /////////////////////////////
    // not affected by biomes

    // These two have fixed colors, unchangeable in Minecraft (and there are no controls in Mineways, because of this)
    { BLOCK_LEAVES /* spruce leaves fancy */, 4, 8, {61,98,61} },	// 0x3D623D
    { BLOCK_LEAVES /* birch leaves, fancy */, 13,13, {107,141,70} },	// 0x6B8D46

    { BLOCK_LILY_PAD /* lily pad */, 12, 4, {0,0,0} },
    { BLOCK_MELON_STEM /* melon stem */, 15,6, { 0, 0, 0 } },
    { BLOCK_MELON_STEM /* melon stem, matured */, 15,7, {0,0,0} }, /* TODO: probably want a different color, a yellow? */
    { BLOCK_PUMPKIN_STEM /* pumpkin stem */, 14,11, { 0, 0, 0 } },
    { BLOCK_PUMPKIN_STEM /* pumpkin stem, matured */, 15,11, { 0, 0, 0 } }, /* TODO: probably want a different color, a yellow? */
    { BLOCK_VINES /* vines */, 15, 8, {0,0,0} },
    { BLOCK_REDSTONE_WIRE /* REDSTONE_WIRE_VERT */, 4,10, {0,0,0} },
    { BLOCK_REDSTONE_WIRE /* REDSTONE_WIRE_HORIZ */, 5,10, {0,0,0} },
    { BLOCK_REDSTONE_WIRE /* REDSTONE_WIRE_DOT */, 4, 11, { 0, 0, 0 } },
};

// the blocks that should be solid if valid water tile is not found
int solidCount = 5;
static int solidTable[] = { BLOCK_WATER, BLOCK_STATIONARY_WATER, BLOCK_LAVA, BLOCK_STATIONARY_LAVA, BLOCK_FIRE };

static int createBaseMaterialTexture()
{
    int row,col,srow,scol;
    int keepGoing;
    int i,adj,idx;
    //int faTableCount;
	int ir, ig, ib;
	unsigned char r, g, b, a;
    unsigned int color;
    int useTextureImage;
    int addNoise = 0;
	int index;

    progimage_info *mainprog;

    mainprog = new progimage_info();

    mainprog->width = gModel.textureResolution;
    mainprog->height = gModel.textureResolution;

    // resize and clear
    mainprog->image_data.resize(gModel.textureResolution*gModel.textureResolution*4*sizeof(unsigned char),0x0);
    // TODO: any way to check if we're out of memory?

    gModel.pPNGtexture = mainprog;

    useTextureImage = (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES);

    // we fill the first NUM_BLOCKS with solid colors, if not using true textures
    gModel.swatchCount = 0;

    if ( !useTextureImage )
    {
        keepGoing = 1;

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
                    else if ( gOptions->exportFlags & (EXPT_3DPRINT|EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) )
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
    }

    if (useTextureImage)
    {
        int dstCol, dstRow, srcCol, srcRow, j;
        int glassPaneTopsCol[17] = { 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        int glassPaneTopsRow[17] = { 9, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21 };

        // we then convert *all* 256+ tiles in terrainExt.png to 18x18 or whatever tiles, adding a 1 pixel border (SWATCH_BORDER)
        // around each (since with tile mosaics, we can't clamp to border, nor can we know that the renderer
        // will clamp and get blocky pixels)

        // this count gets used again when walking through to tile
        int currentSwatchCount = gModel.swatchCount;
        for (row = 0; row < gModel.verticalTiles; row++)
        {
            for (col = 0; col < 16; col++)
            {
                SWATCH_TO_COL_ROW(gModel.swatchCount, dstCol, dstRow);
                // main copy
                copyPNGArea(mainprog,
                    gModel.swatchSize*dstCol + SWATCH_BORDER, // upper left corner destination
                    gModel.swatchSize*dstRow + SWATCH_BORDER,
                    gModel.tileSize, gModel.tileSize, // width, height to copy
                    gModel.pInputTerrainImage,
                    gModel.tileSize*col, // from
                    gModel.tileSize*row
                    );
                gModel.swatchCount++;
            }
        }

        // Copy the top of each glass pane tile so that if these tiles are fattened the tops look OK
        for (i = 0; i < 17; i++)
        {
            SWATCH_TO_COL_ROW(glassPaneTopsCol[i] + glassPaneTopsRow[i] * 16, dstCol, dstRow);
            for (j = 1; j < 15; j += 2)
            {
                if (j != 7)
                {
                    copyPNGArea(mainprog,
                        gModel.swatchSize*dstCol + SWATCH_BORDER + gModel.tileSize*j / 16,    // copy to right
                        gModel.swatchSize*dstRow + SWATCH_BORDER,  // one down from top
                        gModel.tileSize * 2 / 16, gModel.tileSize,  // 2 tile-texels wide
                        mainprog,
                        gModel.swatchSize*dstCol + SWATCH_BORDER + gModel.tileSize * 7 / 16,
                        gModel.swatchSize*dstRow + SWATCH_BORDER
                        );
                }
            }
        }

        // fix grass path block
        if (gPrint3D && !gExportBillboards)
        {
            // exporting whole block - stretch to top
            stretchSwatchToTop(mainprog, SWATCH_INDEX(gBlockDefinitions[BLOCK_GRASS_PATH].txrX + 1, gBlockDefinitions[BLOCK_GRASS_PATH].txrY),
                (float)(gModel.swatchSize*(1.0 / 16.0) + (float)SWATCH_BORDER) / (float)gModel.swatchSize);
        }
        else
        {
            // copy the next-to-top row to top, to avoid black bleed.
            SWATCH_TO_COL_ROW(SWATCH_INDEX(gBlockDefinitions[BLOCK_GRASS_PATH].txrX + 1, gBlockDefinitions[BLOCK_GRASS_PATH].txrY), dstCol, dstRow);
            copyPNGArea(mainprog,
                gModel.swatchSize*dstCol + SWATCH_BORDER,    // copy to top
                gModel.swatchSize*dstRow + SWATCH_BORDER,
                gModel.tileSize, gModel.tileSize * 1 / 16,  // 1 tile-texels high
                mainprog,
                gModel.swatchSize*dstCol + SWATCH_BORDER,
                gModel.swatchSize*dstRow + SWATCH_BORDER + gModel.tileSize * 1 / 16 // copy from one row down
                );
        }

		// Copy middle of top of sea pickle to fill in hole in top of sea pickle
		SWATCH_TO_COL_ROW(SWATCH_INDEX(gBlockDefinitions[BLOCK_SEA_PICKLE].txrX, gBlockDefinitions[BLOCK_SEA_PICKLE].txrY), dstCol, dstRow);
		copyPNGArea(mainprog,
			gModel.swatchSize*dstCol + gModel.tileSize * 5 / 16 + SWATCH_BORDER,
			gModel.swatchSize*dstRow + gModel.tileSize * 2 / 16 + SWATCH_BORDER,
			gModel.tileSize * 2 / 16, gModel.tileSize * 2 / 16,		// 2x2 area
			mainprog,
			gModel.swatchSize*dstCol + gModel.tileSize * 10 / 16 + SWATCH_BORDER,
			gModel.swatchSize*dstRow + gModel.tileSize * 3 / 16 + SWATCH_BORDER
			);


        // now do clamp and tile of edges of swatches
        for ( row = 0; row < gModel.verticalTiles; row++ )
        {
            for ( col = 0; col < 16; col++ )
            {
                SWATCH_TO_COL_ROW( currentSwatchCount, dstCol, dstRow );
                // copy left and right edges only if block is solid - billboards don't tile
                if ( gTilesTable[row*16+col].flags & SBIT_REPEAT_SIDES )
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
                    if ( gTilesTable[row*16+col].flags & SBIT_CLAMP_LEFT )
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
                    if ( gTilesTable[row*16+col].flags & SBIT_CLAMP_RIGHT )
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
                if ( gTilesTable[row*16+col].flags & SBIT_CLAMP_BOTTOM )
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
                if ( gTilesTable[row*16+col].flags & SBIT_CLAMP_TOP )
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
                else if ( gTilesTable[row*16+col].flags & SBIT_REPEAT_TOP_BOTTOM )
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

        // involved corrective: truly tile between double-chest front, back, top shared edges, grabbing samples from *adjacent* swatch
        for ( i = 0; i < 3; i++ )
        {
            if (i < 2) {
				// i=0 is front at 14,1 in output, i=1 is back at 2,2 in output
                SWATCH_TO_COL_ROW(SWATCH_INDEX(10, 2 + i), col, row);
            }
            else {
                // MW_DCHEST_TOP_RIGHT - to 10,8 (column 10, row 8, starting at 0,0) in output
                SWATCH_TO_COL_ROW(SWATCH_INDEX(10, 14), col, row);
            }
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

		// The tile for water in 1.13 is gray now, so for all worlds we need to make it blue
		idx = SWATCH_INDEX(15, 13);
		SWATCH_TO_COL_ROW(idx, dstCol, dstRow);
		bluePNGTile(mainprog, dstCol, dstRow, gModel.swatchSize, 55, 90, 245); // water_still
		idx = SWATCH_INDEX(15, 25);
		SWATCH_TO_COL_ROW(idx, dstCol, dstRow);
		bluePNGTile(mainprog, dstCol, dstRow, gModel.swatchSize, 55, 90, 245); // water_still
		idx = SWATCH_INDEX(8, 26);
		SWATCH_TO_COL_ROW(idx, dstCol, dstRow);
		bluePNGTile(mainprog, dstCol, dstRow, gModel.swatchSize, 55, 90, 245); // water_still
		//{ BLOCK_WATER /* water */, 15, 13, { 0, 0, 0 } },
		//{ BLOCK_WATER /* water */, 15, 25,{ 0, 0, 0 } },
		//{ BLOCK_WATER /* water */, 8, 26,{ 0, 0, 0 } },
		
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
            if (multTable[i].type == BLOCK_WATER)
            {
                color = waterColor;
			}

            // check if there's no override color; if not, we can indeed use the color retrieved.
            if ( (multTable[i].colorReplace[0] == 0) && (multTable[i].colorReplace[1] == 0) && (multTable[i].colorReplace[2] == 0) )
            {
                ir=(color>>16);
                ig=((color>>8)&0xff);
                ib=(color&0xff);
            }
            else
            {
                // overridden color - spruce and birch leaves, currently
                ir = multTable[i].colorReplace[0];
                ig = multTable[i].colorReplace[1];
                ib = multTable[i].colorReplace[2];
            }
            a = (unsigned char)(gBlockDefinitions[adj].alpha * 255);

			if (multTable[i].type == BLOCK_GRASS_BLOCK || multTable[i].type == BLOCK_GRASS || 
				multTable[i].type == BLOCK_DOUBLE_FLOWER || multTable[i].type == BLOCK_LILY_PAD ||
				multTable[i].type == BLOCK_LEAVES || multTable[i].type == BLOCK_AD_LEAVES)
			{
				// funny stuff: (A) Minecraft makes sides of blocks dimmer than tops, even under full light,
				// (B) the color set for Grass in the color scheme is used to multiply the grayscale texture,
				// (C) this color wants to be fairly "average" for display, but wants to be bright for multiplying
				// this textures. So we boost it here.
				ir = (int)(ir * 1.34);
				ig = (int)(ig * 1.34);
				ib = (int)(ib * 1.34);
			}
			
			idx = SWATCH_INDEX( multTable[i].col, multTable[i].row );
            SWATCH_TO_COL_ROW( idx, dstCol, dstRow );

            // save a little work: if color is 0xffffff, no multiplication needed
            if ( ir != 255 || ig != 255 || ib != 255 )
            {
				if (ir > 255 || ig > 255 || ib > 255) {
					multiplyClampPNGTile(mainprog, dstCol, dstRow, gModel.swatchSize, ir, ig, ib, a);
				}
				else {
					multiplyPNGTile(mainprog, dstCol, dstRow, gModel.swatchSize, (unsigned char)ir, (unsigned char)ig, (unsigned char)ib, a);
				}
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

        // For torches, some like Sphax include extra data for their more elaborate geometry in the outer edges of the tiles.
        // Clear these areas, we can't use them currently, and they might mess up bleeding. TODO - someday, geometry from JSON descriptions or whatever.
        SWATCH_TO_COL_ROW(SWATCH_INDEX(0, 5), col, row);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 0 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 12 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);
        SWATCH_TO_COL_ROW(SWATCH_INDEX(3, 6), col, row);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 0 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 12 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);
        SWATCH_TO_COL_ROW(SWATCH_INDEX(3, 7), col, row);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 0 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);
        setColorPNGArea(mainprog, col*gModel.swatchSize + gModel.tileSize * 12 / 16 + SWATCH_BORDER, row*gModel.swatchSize, gModel.tileSize * 4 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);

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

        /////////////////////////////////////////////////////
        // Add compositing dot to 4-way wire template.
        // Note that currently we have only "wire on" for all wires, no "wire off". We'd need new templates for all wires off, and we'd need an extra bit
        // to note that the wire was powered (right now we're cheating and using the data field, normally wire power, to instead hold which directions wires
        // are in).

        // We first apply the red color for the wire to all colors (multiply). This is done above by multTable to our dot, vertical, and horizontal wires.
        // We then apply the overlay atop the tiles, using the rules: no blending, just replace if alpha >= 128, alpha otherwise ignored.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_VERT, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_VERT, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_HORIZ, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_HORIZ, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_DOT, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_DOT, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // We have our three building blocks.
        // Make the horizontal line for the 4-way.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_4, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_HORIZ, scol, srow);
        rotatePNGTile(gModel.pPNGtexture, col, row, scol, srow, 270, gModel.swatchSize);

        // Make the vertical top half of the two angle.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_ANGLED_2, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_VERT, scol, srow);
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        // clear bottom half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize * 8 / 16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize * 8 / 16 + SWATCH_BORDER, 0x0);

        // Make the horizontal right half of the three angle, in a separate spot.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_3, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_4, scol, srow);
        // first copy the horizontal line from the 4 way.
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        // clear left half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize, gModel.tileSize * 8 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);

        // Now composite this horizontal right half in wire-3 on to the vertical top half of wire-2: 2 angle is now done, other than the dot.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_ANGLED_2, REDSTONE_WIRE_3, REDSTONE_WIRE_ANGLED_2, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Now composite the full vertical line on to the 3-angle.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_3, REDSTONE_WIRE_VERT, REDSTONE_WIRE_3, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Now form the four wire: add vertical wire.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_4, REDSTONE_WIRE_VERT, REDSTONE_WIRE_4, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Finally, add dots to 2,3,4 wires.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_ANGLED_2, REDSTONE_WIRE_DOT, REDSTONE_WIRE_ANGLED_2, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_3, REDSTONE_WIRE_DOT, REDSTONE_WIRE_3, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_4, REDSTONE_WIRE_DOT, REDSTONE_WIRE_4, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        //////////////////////////////////////////////////
        // Do the whole thing again, but with a dimmer red
        // We first apply the red color for the wire to all colors (multiply).
        color = gBlockDefinitions[BLOCK_REDSTONE_WIRE].color;
        float dim = 0.3f;
        r = (unsigned char)((color >> 16)*dim);
        g = (unsigned char)(((color >> 8) & 0xff)*dim);
        b = (unsigned char)((color & 0xff)*dim);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_VERT_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_VERT, scol, srow);
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        multiplyPNGTile(mainprog, col, row, gModel.swatchSize, r, g, b, a);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_HORIZ_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_HORIZ, scol, srow);
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        multiplyPNGTile(mainprog, col, row, gModel.swatchSize, r, g, b, a);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_DOT_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_DOT, scol, srow);
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        multiplyPNGTile(mainprog, col, row, gModel.swatchSize, r, g, b, a);

        // We then apply the overlay atop the tiles, using the rules: no blending, just replace if alpha >= 128, alpha otherwise ignored.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_VERT_OFF, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_VERT_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_HORIZ_OFF, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_HORIZ_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_DOT_OFF, REDSTONE_WIRE_OVERLAY, REDSTONE_WIRE_DOT_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // We have our three building blocks.
        // Make the horizontal line for the 4-way.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_4_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_HORIZ_OFF, scol, srow);
        rotatePNGTile(gModel.pPNGtexture, col, row, scol, srow, 270, gModel.swatchSize);

        // Make the vertical top half of the two angle.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_ANGLED_2_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_VERT_OFF, scol, srow);
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        // clear bottom half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize + gModel.tileSize * 8 / 16 + SWATCH_BORDER, gModel.swatchSize, gModel.tileSize * 8 / 16 + SWATCH_BORDER, 0x0);

        // Make the horizontal right half of the three angle, in a separate spot.
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_3_OFF, col, row);
        SWATCH_TO_COL_ROW(REDSTONE_WIRE_4_OFF, scol, srow);
        // first copy the horizontal line from the 4 way.
        copyPNGTile(mainprog, col, row, gModel.swatchSize, mainprog, scol, srow);
        // clear left half of wire
        setColorPNGArea(mainprog, col*gModel.swatchSize, row*gModel.swatchSize, gModel.tileSize * 8 / 16 + SWATCH_BORDER, gModel.swatchSize, 0x0);

        // Now composite this horizontal right half in wire-3 on to the vertical top half of wire-2: 2 angle is now done, other than the dot.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_ANGLED_2_OFF, REDSTONE_WIRE_3_OFF, REDSTONE_WIRE_ANGLED_2_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Now composite the full vertical line on to the 3-angle.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_3_OFF, REDSTONE_WIRE_VERT_OFF, REDSTONE_WIRE_3_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Now form the four wire: add vertical wire.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_4_OFF, REDSTONE_WIRE_VERT_OFF, REDSTONE_WIRE_4_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);

        // Finally, add dots to 2,3,4 wires.
        compositePNGSwatches(mainprog, REDSTONE_WIRE_ANGLED_2_OFF, REDSTONE_WIRE_DOT_OFF, REDSTONE_WIRE_ANGLED_2_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_3_OFF, REDSTONE_WIRE_DOT_OFF, REDSTONE_WIRE_3_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);
        compositePNGSwatches(mainprog, REDSTONE_WIRE_4_OFF, REDSTONE_WIRE_DOT_OFF, REDSTONE_WIRE_4_OFF, gModel.swatchSize, gModel.swatchesPerRow, FORCE_CUTOUT);


        // Stretch tiles to fill the area
        // stretch only if we're exporting full blocks
        if (!gOptions->pEFD->chkExportAll)
        {
            // bed
            stretchSwatchToFill(mainprog, SWATCH_INDEX(5, 9), 0, 7, 15, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(6, 9), 0, 7, 15, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(7, 9), 0, 7, 15, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(8, 9), 0, 7, 15, 15);

            // cake
            stretchSwatchToFill(mainprog, SWATCH_INDEX(9, 7), 1, 1, 14, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 7), 1, 8, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(11, 7), 1, 8, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(12, 7), 1, 1, 14, 14);

            // add latches to single and double chests, before interpolation, so they're exact
            int pixelsPerTexel = gModel.tileSize / 16;
            // single front face of chest
            SWATCH_TO_COL_ROW(SWATCH_INDEX(11, 1), dstCol, dstRow);
            SWATCH_TO_COL_ROW(SWATCH_INDEX(7, 26), srcCol, srcRow);
            copyPNGArea(mainprog,
                gModel.swatchSize*dstCol + 1 + 7 * pixelsPerTexel, // upper left corner destination
                gModel.swatchSize*dstRow + 1 + 5 * pixelsPerTexel,
                2 * pixelsPerTexel, 4 * pixelsPerTexel, // width, height to copy
                mainprog,
                gModel.swatchSize*srcCol + 1 + 1 * pixelsPerTexel, // from
                gModel.swatchSize*srcRow + 1 + 1 * pixelsPerTexel
                );
            // left double chest
            SWATCH_TO_COL_ROW(SWATCH_INDEX(9, 2), dstCol, dstRow);
            copyPNGArea(mainprog,
                gModel.swatchSize*dstCol + 1 + 15 * pixelsPerTexel, // upper left corner destination
                gModel.swatchSize*dstRow + 1 + 5 * pixelsPerTexel,
                1 * pixelsPerTexel, 4 * pixelsPerTexel, // width, height to copy
                mainprog,
                gModel.swatchSize*srcCol + 1 + 1 * pixelsPerTexel, // from
                gModel.swatchSize*srcRow + 1 + 1 * pixelsPerTexel
                );
            // right double chest
            SWATCH_TO_COL_ROW(SWATCH_INDEX(10, 2), dstCol, dstRow);
            copyPNGArea(mainprog,
                gModel.swatchSize*dstCol + 1 + 0 * pixelsPerTexel, // upper left corner destination
                gModel.swatchSize*dstRow + 1 + 5 * pixelsPerTexel,
                1 * pixelsPerTexel, 4 * pixelsPerTexel, // width, height to copy
                mainprog,
                gModel.swatchSize*srcCol + 1 + 1 * pixelsPerTexel, // from
                gModel.swatchSize*srcRow + 1 + 1 * pixelsPerTexel
                );
            // ender chest
            SWATCH_TO_COL_ROW(SWATCH_INDEX(12, 13), dstCol, dstRow);
            SWATCH_TO_COL_ROW(SWATCH_INDEX(9, 13), srcCol, srcRow);
            copyPNGArea(mainprog,
                gModel.swatchSize*dstCol + 1 + 7 * pixelsPerTexel, // upper left corner destination
                gModel.swatchSize*dstRow + 1 + 5 * pixelsPerTexel,
                2 * pixelsPerTexel, 4 * pixelsPerTexel, // width, height to copy
                mainprog,
                gModel.swatchSize*srcCol + 1 + 1 * pixelsPerTexel, // from
                gModel.swatchSize*srcRow + 1 + 1 * pixelsPerTexel
                );

            // chest
            stretchSwatchToFill(mainprog, SWATCH_INDEX(9, 1), 1, 1, 14, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 1), 1, 2, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(11, 1), 1, 2, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(9, 2), 1, 2, 15, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 2), 0, 2, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(9, 3), 1, 2, 15, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 3), 0, 2, 14, 15);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(9, 14), 1, 1, 15, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 14), 0, 1, 14, 14);

            // cactus top and bottom
            stretchSwatchToFill(mainprog, SWATCH_INDEX(5, 4), 1, 1, 14, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(7, 4), 1, 1, 14, 14);

            // ender portal
            stretchSwatchToFill(mainprog, SWATCH_INDEX(15, 9), 0, 3, 15, 15);

            // ender chest
            stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 13), 1, 1, 14, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(11, 13), 1, 1, 14, 14);
            stretchSwatchToFill(mainprog, SWATCH_INDEX(12, 13), 1, 1, 14, 14);

            // banner top and bottom - no longer done
            //stretchSwatchToFill(mainprog, SWATCH_INDEX(10, 23), 1, 2, 14, 15);
            //stretchSwatchToFill(mainprog, SWATCH_INDEX(11, 23), 1, 0, 14, 12);

			// enchanting table
			stretchSwatchToFill(mainprog, SWATCH_INDEX(6, 11), 0, 3, 15, 15);

			// stonecutter
			stretchSwatchToFill(mainprog, SWATCH_INDEX(5, 41), 0, 7, 15, 15);
		}
    }

	// Finally, for rendering (not printing), bleed the transparent *colors* only outwards, leaving the alpha untouched. This should give
	// better fringes when bilinear interpolation is done (it's a flaw of the PNG format, that it uses unassociated alphas).
	// This interpolation should be off normally, but things like previewers such as G3D, and Blender, have problems in this area.
	// Done only for those things rendered with decals. A few extra are done; a waste, but not a big time sink normally.
	// Note, we used to bleed only for gOptions->pEFD->chkG3DMaterial, but this option is off by default and bleeding always looks
	// better (IMO), so always put bleeding on. We don't bleed for printing because decal cutout objects are not created with
	// cutouts, and some decals are used as-is, e.g. wheat, to print on sides of blocks. In other words, if we could see the black
	// fringe for the decal when 3D printing, then bleeding should not be done.
	// See http://www.realtimerendering.com/blog/png-srgb-cutoutdecal-aa-problematic/

	// For 3D printing we need to do this to only the true geometry that has a "cutout", so that the fringes are OK
	if (gOptions->pEFD->chkExportAll)
	{
		int flagTest = gPrint3D ? SBIT_CUTOUT_GEOMETRY : (SBIT_DECAL | SBIT_CUTOUT_GEOMETRY);
		for (i = 0; i < TOTAL_TILES; i++)
		{
			// If leaves are to be made solid and so should have alphas all equal to 1.0.
			if (gOptions->pEFD->chkLeavesSolid && (gTilesTable[i].flags & SBIT_LEAVES))
			{
				// set all alphas in tile to 1.0.
				setAlphaPNGSwatch(mainprog, SWATCH_INDEX(gTilesTable[i].txrX, gTilesTable[i].txrY), gModel.swatchSize, gModel.swatchesPerRow, 255);
			}
			else if (gTilesTable[i].flags & flagTest)
			{
				// could bleed to very edges, which is better, but slower.
				static int bleedToEdge = 1;
				switch (bleedToEdge) {
				default:
				case 0:
					bleedPNGSwatch(mainprog, SWATCH_INDEX(gTilesTable[i].txrX, gTilesTable[i].txrY), 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 0);
					break;
				case 1:
					// another option is to carefully bleed once, but then set all other black alphas left to be the average color
					index = SWATCH_INDEX(gTilesTable[i].txrX, gTilesTable[i].txrY);
					bleedPNGSwatch(mainprog, index, 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 0);
					makeRemainingTileAverage(mainprog, index, gModel.swatchSize, gModel.swatchesPerRow);
					break;
				case 2:
					// bleed multiple times, until the returned value (currently ignored) was zero, i.e., bleed to the edges, so that mipmapping works well.
					// NOTE: this can work, mostly, but only if the "seed" of existing pixels is 3x3 in size to begin with. If smaller, no spread will happen.
					// If no spread, then the neighbors to spread value should be set to 2, then to 1 - this code is not there.
					int modCount = 0;
					index = SWATCH_INDEX(gTilesTable[i].txrX, gTilesTable[i].txrY);
					do {
						modCount = bleedPNGSwatchRecursive(mainprog, index, 0, 16, 0, 16, gModel.swatchSize, gModel.swatchesPerRow, 0);
					} while (modCount > 0);
					break;
				}
			}
		}
	}

	return MW_NO_ERROR;
}

static int writeBinarySTLBox(WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC)
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t stlFileNameWithSuffix[MAX_PATH_AND_FILE];
    const char *justWorldFileName;
    char worldNameUnderlined[MAX_PATH_AND_FILE];

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

    wchar_t statsFileName[MAX_PATH_AND_FILE];

    HANDLE statsFile;

    char worldChar[MAX_PATH_AND_FILE];

    // if no color output, don't use isMagics
    int isMagics = writeColor && (gOptions->pEFD->fileType == FILE_TYPE_BINARY_MAGICS_STL);

    concatFileName3(stlFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".stl");

    // create the STL file
    gModelFile = PortaCreate(stlFileNameWithSuffix);
    addOutputFilenameToList(stlFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

    // find last \ in world string
    WcharToChar(pWorldGuide->world, worldChar, MAX_PATH_AND_FILE);
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
        // For export quad
        // if normal sums negative, rotate order by one so that we
        // match up the faces better, which should make matching face removal work better. I hope.
        int offset = 0;
        i = pFace->normalIndex;
        if ((faceTriCount > 1) && (gModel.normals[i][X] + gModel.normals[i][Y] + gModel.normals[i][Z] < 0.0f))
            offset = 1;

        // output a triangle or a quad, i.e. 1 or 2 faces
        for ( i = 0; i < faceTriCount; i++ )
        {
            // 3 float normals
            WERROR(PortaWrite(gModelFile, &gModel.normals[pFace->normalIndex], 12 ));

            // two triangles: 0 1 2 and 0 2 3 (or 1 2 3 and 1 3 0)
            WERROR(PortaWrite(gModelFile, vertex[offset], 12));
            WERROR(PortaWrite(gModelFile, vertex[offset+i+1], 12 ));
            WERROR(PortaWrite(gModelFile, vertex[(offset+i+2)%4], 12 ));

            if ( writeColor )
            {
                // http://en.wikipedia.org/wiki/Stl_file_format#Colour_in_binary_STL
                if ( isMagics )
                {
                    // Materialise Magics 
                    colorBytes = gBlockDefinitions[pFace->materialType].color;
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
                    colorBytes = gBlockDefinitions[pFace->materialType].color;
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
    retCode |= writeStatistics(statsFile, pWorldGuide, worldBox, tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    PortaClose(statsFile);

    return retCode;
}

static int writeAsciiSTLBox(WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC)
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t stlFileNameWithSuffix[MAX_PATH_AND_FILE];
    const char *justWorldFileName;
    char worldNameUnderlined[MAX_PATH_AND_FILE];
    wchar_t statsFileName[MAX_PATH_AND_FILE];

    HANDLE statsFile;

    char outputString[256];

    int faceNo,i;

    int retCode = MW_NO_ERROR;

    int normalIndex;
    FaceRecord *pFace;
    Point *vertex[4],*pt;

    char worldChar[MAX_PATH_AND_FILE];

    concatFileName3(stlFileNameWithSuffix, gOutputFilePath, gOutputFileRoot, L".stl");

    // create the STL file
    gModelFile = PortaCreate(stlFileNameWithSuffix);
    addOutputFilenameToList(stlFileNameWithSuffix);
    if (gModelFile == INVALID_HANDLE_VALUE)
        return MW_CANNOT_CREATE_FILE;

    // find last \ in world string
    WcharToChar(pWorldGuide->world, worldChar, MAX_PATH_AND_FILE);
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
        // For export quad
        // if normal sums negative, rotate order by one so that we
        // match up the faces better, which should make matching face removal work better. I hope.
        int offset = 0;
        i = pFace->normalIndex;
        if ((faceTriCount>1) && (gModel.normals[i][X] + gModel.normals[i][Y] + gModel.normals[i][Z] < 0.0f))
            offset = 1;

        for ( i = 0; i < faceTriCount; i++ )
        {
            WERROR(PortaWrite(gModelFile, facetNormalString[normalIndex], strlen(facetNormalString[normalIndex]) ));
            WERROR(PortaWrite(gModelFile, "outer loop\n", strlen("outer loop\n") ));

            // two triangles: 0 1 2 and 0 2 3 (or 1 2 3 and 1 3 0)
            pt = vertex[offset];
            sprintf_s(outputString,256,"vertex  %e %e %e\n",(double)((*pt)[X]),(double)((*pt)[Y]),(double)((*pt)[Z]));
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            pt = vertex[(offset + i + 1) % 4];	// shouldn't need % 4, but let's be safe, and makes cppcheck happy
            sprintf_s(outputString,256,"vertex  %e %e %e\n",(double)((*pt)[X]),(double)((*pt)[Y]),(double)((*pt)[Z]));
            WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));
            pt = vertex[(offset + i + 2) % 4];
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
    retCode |= writeStatistics(statsFile, pWorldGuide, worldBox, tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
    if ( retCode >= MW_BEGIN_ERRORS ) return retCode;

    PortaClose(statsFile);

    return retCode;
}


static int writeVRML2Box(WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, wchar_t *schemeSelected, ChangeBlockCommand *pCBC)
{
#ifdef WIN32
    DWORD br;
#endif

    wchar_t wrlFileNameWithSuffix[MAX_PATH_AND_FILE];
    const char *justWorldFileName;
    char justTextureFileName[MAX_PATH_AND_FILE];	// without path

    char outputString[256];
    char textureDefOutputString[256];
    //char textureUseOutputString[256];

    int currentFace, j, firstShape, exportSingleMaterial, exportSolidColors;

    int retCode = MW_NO_ERROR;

    //char worldNameUnderlined[256];

    FaceRecord *pFace;

    char worldChar[MAX_PATH_AND_FILE];

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
    exportSingleMaterial = !(gOptions->exportFlags & EXPT_OUTPUT_OBJ_MTL_PER_TYPE);

    WcharToChar(pWorldGuide->world, worldChar, MAX_PATH_AND_FILE);
    justWorldFileName = removePathChar(worldChar);

    sprintf_s(outputString,256,"#VRML V2.0 utf8\n\n# VRML 97 (VRML2) file made by Mineways version %d.%02d, http://mineways.com\n", gMajorVersion, gMinorVersion );
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

    retCode |= writeStatistics(gModelFile, pWorldGuide, worldBox, tightenedWorldBox, curDir, terrainFileName, schemeSelected, pCBC);
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
        sprintf_s(justTextureFileName,MAX_PATH_AND_FILE,"%s.png",gOutputFileRootCleanChar);
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
            strcpy_s(mtlName,256,gBlockDefinitions[gModel.faceList[currentFace]->materialType].name);
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
        currentType = exportSingleMaterial ? BLOCK_STONE : gModel.faceList[currentFace]->materialType;

        strcpy_s(outputString,256,"        coordIndex\n        [\n");
        WERROR(PortaWrite(gModelFile, outputString, strlen(outputString) ));

        // output face loops until next material is found, or all, if exporting no material
        while ( (currentFace < gModel.faceCount) &&
            ( (currentType == gModel.faceList[currentFace]->materialType) || exportSingleMaterial ) )
        {
            char commaString[256];
            strcpy_s(commaString,256,( currentFace == gModel.faceCount-1 || (currentType != gModel.faceList[currentFace+1]->materialType) ) ? "" : "," );

            pFace = gModel.faceList[currentFace];

            if ( pFace->vertexIndex[2] == pFace->vertexIndex[3] )
            {
                // export triangle
                sprintf_s(outputString,256,"          %d,%d,%d,-1%s\n",
                    pFace->vertexIndex[0],
                    pFace->vertexIndex[1],
                    pFace->vertexIndex[2],
                    commaString);
            }
            else
            {
                // Export quad
                // if normal sums negative, rotate order by one so that dumb tessellators
                // match up the faces better, which should make matching face removal work better. I hope.
                int offset = 0;
                int i = pFace->normalIndex;
                if (gModel.normals[i][X] + gModel.normals[i][Y] + gModel.normals[i][Z] < 0.0f)
                    offset = 1;
                sprintf_s(outputString,256,"          %d,%d,%d,%d,-1%s\n",
                    pFace->vertexIndex[offset],
                    pFace->vertexIndex[offset+1],
                    pFace->vertexIndex[offset+2],
                    pFace->vertexIndex[(offset+3)%4],
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
                    // Export quad
                    // if normal sums negative, rotate order by one so that dumb tessellators
                    // match up the faces better, which should make matching face removal work better. I hope.
                    int offset = 0;
                    int i = pFace->normalIndex;
                    if (gModel.normals[i][X] + gModel.normals[i][Y] + gModel.normals[i][Z] < 0.0f)
                        offset = 1;
                    sprintf_s(outputString, 256, "          %d %d %d %d -1\n",
                        pFace->uvIndex[offset],
                        pFace->uvIndex[offset+1],
                        pFace->uvIndex[offset+2],
                        pFace->uvIndex[(offset+3)%4]);
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
    if ( alpha < 1.0f && (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES) && !(gBlockDefinitions[type].flags & BLF_TRANSPARENT) )
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
		if (swatchLoc < TOTAL_TILES) {
			char outName[MAX_PATH_AND_FILE];
			WcharToChar(gTilesTable[swatchLoc].filename, outName, MAX_PATH_AND_FILE);
			assert(strlen(outName) > 0);
			sprintf_s(outputString, 1024, "# %s\n            %g %g\n",
				outName,
				u, v);
		}
		else {
			// old "by block family" code, which is less useful
			sprintf_s(outputString, 1024, "# %s\n            %g %g\n",
				gBlockDefinitions[gModel.uvSwatchToType[swatchLoc]].name,
				u, v);
		}
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

    wchar_t schematicFileNameWithSuffix[MAX_PATH_AND_FILE];

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

        // note: the *Incr values are either -1 or 1, depending on rotation
        for ( loc[Z] = zStart; loc[Z]*zIncr <= zEnd*zIncr; loc[Z]+=zIncr )
        {
            for ( loc[X] = xStart; loc[X]*xIncr <= xEnd*xIncr; loc[X]+=xIncr )
            {
				unsigned char type;
                unsigned char data;
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

				// if you're storing 1.13 types, you're out of luck - converted to grass
				if (gBoxData[boxIndex].type < 256) {
					type = (unsigned char)gBoxData[boxIndex].type;
					data = gBoxData[boxIndex].data;
				}
				else {
					type = BLOCK_GRASS_BLOCK;
					data = 0x0;
					retCode |= MW_UNKNOWN_BLOCK_TYPE_ENCOUNTERED;
				}
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


static int writeLines(HANDLE file, char **textLines, int lines)
{
#ifdef WIN32
    DWORD br;
#endif

    int i;
    for (i = 0; i < lines; i++)
    {
        WERROR(PortaWrite(file, textLines[i], strlen(textLines[i])));
    }

    return MW_NO_ERROR;
}

static float max3(Point pt)
{
    float retVal = max(pt[0], pt[1]);
    return max(retVal, pt[2]);
}
static float med3(Point pt)
{
    float retVal = max(pt[0], pt[1]);
    if (retVal > pt[2])
    {
        // old retVal is maximum, so compare other two
        retVal = min(pt[0], pt[1]);
        retVal = max(retVal, pt[2]);
    }
    return retVal;
}
static float min3(Point pt)
{
    float retVal = min(pt[0], pt[1]);
    return min(retVal, pt[2]);
}

static int writeStatistics(HANDLE fh, WorldGuide *pWorldGuide, IBox *worldBox, IBox *tightenedWorldBox, const wchar_t *curDir, const wchar_t *terrainFileName, const wchar_t *schemeSelected, ChangeBlockCommand *pCBC)
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
		"Export full color texture patterns",
		"Export tiles for textures"
	};

	float inCM = gModel.scale * METERS_TO_CM;
	float inCM3 = inCM * inCM * inCM;

	char outChar[MAX_PATH_AND_FILE];

	// Path info (originally just meant for debugging)
	char worldChar[MAX_PATH_AND_FILE];
	WcharToChar(pWorldGuide->world, worldChar, MAX_PATH_AND_FILE);	// don't touch worldChar after this, as justWorldFileName depends on it
	const char *justWorldFileName = removePathChar(worldChar);

	if (justWorldFileName == NULL || strlen(justWorldFileName) == 0)
	{
		strcpy_s(outputString, 256, "# Minecraft world: [Block Test World]\n");
	}
	else {
		sprintf_s(outputString, 256, "# Minecraft world: %s\n", justWorldFileName);
	}
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if (gModel.terrainImageNotFound) {
		strcpy_s(outChar, MAX_PATH_AND_FILE, "default");
	}
	else {
		WcharToChar(terrainFileName, outChar, MAX_PATH_AND_FILE);
	}
	// this code removes the path, but the path is now useful for scripting, so keep it in.
	//char *outPtr = strrchr(outChar, '\\');
	//if (outPtr == NULL)
	//{
	//	outPtr = outChar;
	//}
	//else
	//{
	//	// get past \\ 
	//	outPtr++;
	//}
	//sprintf_s(outputString, 256, "# Terrain file name: %s\n", outPtr);
	sprintf_s(outputString, 256, "# Terrain file name: %s\n", outChar);
	WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));

	// if we output the Nether or The End, note it here. Otherwise overworld is assumed.
	if (gOptions->worldType&(ENDER | HELL)) {
		if (gOptions->worldType&HELL) {
			sprintf_s(outputString, 256, "# View Nether\n");
		}
		else {
			assert(gOptions->worldType&ENDER);
			sprintf_s(outputString, 256, "# View The End\n");
		}
		WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
	}

	if (schemeSelected == NULL || wcslen(schemeSelected) == 0)
	{
		sprintf_s(outputString, 256, "# Color scheme: Standard\n");
		WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
	}
	else
	{
		WcharToChar(schemeSelected, outChar, MAX_PATH_AND_FILE);
		sprintf_s(outputString, 256, "# Color scheme: %s\n", outChar);
		WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));
	}


	_time32(&aclock);   // Get time in seconds.
	_localtime32_s(&newtime, &aclock);   // Convert time to struct tm form.

	// Print local time as a string.
	errNum = asctime_s(timeString, 32, &newtime);
	if (!errNum)
	{
		sprintf_s(outputString, 256, "# %s", timeString);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	// put the selection box near the top, since I find I use these values most of all
	sprintf_s(outputString, 256, "\n# Selection location min to max: %d, %d, %d to %d, %d, %d\n",
		worldBox->min[X], worldBox->min[Y], worldBox->min[Z],
		worldBox->max[X], worldBox->max[Y], worldBox->max[Z]);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Non-empty selection location min to max: %d, %d, %d to %d, %d, %d\n\n",
		tightenedWorldBox->min[X], tightenedWorldBox->min[Y], tightenedWorldBox->min[Z],
		tightenedWorldBox->max[X], tightenedWorldBox->max[Y], tightenedWorldBox->max[Z]);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	// If STL, say which type of STL, etc.
	switch (gOptions->pEFD->fileType)
	{
	case FILE_TYPE_WAVEFRONT_ABS_OBJ:
		strcpy_s(formatString, 256, "Wavefront OBJ absolute indices");
		break;
	case FILE_TYPE_WAVEFRONT_REL_OBJ:
		strcpy_s(formatString, 256, "Wavefront OBJ relative indices");
		break;
	case FILE_TYPE_BINARY_MAGICS_STL:
		strcpy_s(formatString, 256, "Binary STL iMaterialise");
		break;
	case FILE_TYPE_BINARY_VISCAM_STL:
		strcpy_s(formatString, 256, "Binary STL VisCAM");
		break;
	case FILE_TYPE_ASCII_STL:
		strcpy_s(formatString, 256, "ASCII STL");
		break;
	case FILE_TYPE_VRML2:
		strcpy_s(formatString, 256, "VRML 2.0");
		break;
	default:
		strcpy_s(formatString, 256, "Unknown file type");
		assert(0);
		break;
	}
	sprintf_s(outputString, 256, "# Set %s type: %s\n", gPrint3D ? "3D print" : "render", formatString);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if (gPrint3D)
	{
		char warningString[256];
		int isSculpteo = (gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ) || (gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ);

		if (!isSculpteo)
		{
			// If we add materials, put the material chosen here.
			sprintf_s(outputString, 256, "\n# Cost estimate for this model:\n");
			WERROR(PortaWrite(fh, outputString, strlen(outputString)));

			sprintf_s(warningString, 256, "%s", (gModel.scale < gMtlCostTable[PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE].minWall) ? " *** WARNING, thin wall ***" : "");
			sprintf_s(outputString, 256, "#   if made using the white, strong & flexible material: $ %0.2f%s\n",
				computeMaterialCost(PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, gModel.scale, gBlockCount, gMinorBlockCount),
				warningString);
			WERROR(PortaWrite(fh, outputString, strlen(outputString)));
		}

		sprintf_s(warningString, 256, "%s", (gModel.scale < gMtlCostTable[isSculpteo ? PRINT_MATERIAL_FCS_SCULPTEO : PRINT_MATERIAL_FULL_COLOR_SANDSTONE].minWall) ? " *** WARNING, thin wall ***" : "");
		sprintf_s(outputString, 256, "#   if made using the full color sandstone material:     $ %0.2f%s\n",
			computeMaterialCost(isSculpteo ? PRINT_MATERIAL_FCS_SCULPTEO : PRINT_MATERIAL_FULL_COLOR_SANDSTONE, gModel.scale, gBlockCount, gMinorBlockCount),
			warningString);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		// if material is not one of these, print its cost
		if (gPhysMtl > PRINT_MATERIAL_FULL_COLOR_SANDSTONE && gPhysMtl != PRINT_MATERIAL_FCS_SCULPTEO)
		{
			sprintf_s(warningString, 256, "%s", (gModel.scale < gMtlCostTable[gPhysMtl].minWall) ? " *** WARNING, thin wall ***" : "");
			sprintf_s(outputString, 256,
				"#   if made using the %s material:     %0.2f%s\n",	// TODO: could add custom material currency symbol
				gMtlCostTable[gPhysMtl].name,
				computeMaterialCost(gPhysMtl, gModel.scale, gBlockCount, gMinorBlockCount),
				warningString);
			WERROR(PortaWrite(fh, outputString, strlen(outputString)));
		}
		gOptions->cost = computeMaterialCost(gPhysMtl, gModel.scale, gBlockCount, gMinorBlockCount);

		sprintf_s(outputString, 256, "# For %s printer, minimum wall is %g mm, maximum size is %g x %g x %g cm\n", gMtlCostTable[gPhysMtl].name, gMtlCostTable[gPhysMtl].minWall*METERS_TO_MM,
			gMtlCostTable[gPhysMtl].maxSize[0], gMtlCostTable[gPhysMtl].maxSize[1], gMtlCostTable[gPhysMtl].maxSize[2]);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "# Units for the model vertex data itself: %s\n", gUnitTypeTable[gOptions->pEFD->comboModelUnits[gOptions->pEFD->fileType]].name);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if (gPrint3D)
	{
		float area, volume, sumOfDimensions;
		char errorString[256];

		if (inCM * max3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[0] ||
			inCM * med3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[1] ||
			inCM * min3(gFilledBoxSize) > gMtlCostTable[gPhysMtl].maxSize[2])
		{
			sprintf_s(errorString, 256, " *** WARNING, too large for %s printer", gMtlCostTable[gPhysMtl].name);
		}
		else
		{
			errorString[0] = '\0';
		}

		gOptions->dim_cm[X] = inCM * gFilledBoxSize[X];
		gOptions->dim_cm[Y] = inCM * gFilledBoxSize[Y];
		gOptions->dim_cm[Z] = inCM * gFilledBoxSize[Z];
		sprintf_s(outputString, 256, "\n# world dimensions: %0.2f x %0.2f x %0.2f cm%s\n",
			gOptions->dim_cm[X], gOptions->dim_cm[Y], gOptions->dim_cm[Z], errorString);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		gOptions->dim_inches[X] = inCM * gFilledBoxSize[X] / 2.54f;
		gOptions->dim_inches[Y] = inCM * gFilledBoxSize[Y] / 2.54f;
		gOptions->dim_inches[Z] = inCM * gFilledBoxSize[Z] / 2.54f;
		sprintf_s(outputString, 256, "#   in inches: %0.2f x %0.2f x %0.2f inches%s\n",
			gOptions->dim_inches[X], gOptions->dim_inches[Y], gOptions->dim_inches[Z], errorString);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		gOptions->block_mm = gModel.scale*METERS_TO_MM;
		gOptions->block_inch = gOptions->block_mm / 25.4f;
		sprintf_s(outputString, 256, "# each block is %0.2f mm on a side, and has a volume of %g mm^3\n", gOptions->block_mm, inCM3 * 1000);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sumOfDimensions = 10 * inCM *(gFilledBoxSize[X] + gFilledBoxSize[Y] + gFilledBoxSize[Z]);
		sprintf_s(outputString, 256, "# sum of dimensions: %g mm\n", sumOfDimensions);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		volume = inCM3 * gBlockCount;
		sprintf_s(outputString, 256, "# volume is %g cm^3\n", volume);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		area = AREA_IN_CM2;
		sprintf_s(outputString, 256, "# surface area is %g cm^2\n", area);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "# block density: %d%% of volume\n",
			(int)(gStats.density*100.0f + 0.5f));
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	// write out a summary, useful for various reasons
	if (gExportBillboards)
	{
		sprintf_s(outputString, 256, "\n# %d vertices, %d faces (%d triangles), %d blocks, %d billboards/bits\n", gModel.vertexCount, gModel.faceCount, 2 * gModel.faceCount, gBlockCount, gModel.billboardCount);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}
	else
	{
		sprintf_s(outputString, 256, "\n# %d vertices, %d faces (%d triangles), %d blocks\n", gModel.vertexCount, gModel.faceCount, 2 * gModel.faceCount, gBlockCount);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}
	gOptions->totalBlocks = gBlockCount;

	sprintf_s(outputString, 256, "# block dimensions: X=%g by Y=%g (height) by Z=%g blocks\n", gFilledBoxSize[X], gFilledBoxSize[Y], gFilledBoxSize[Z]);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	Vec2Op(gOptions->dimensions, =, (int)gFilledBoxSize);

	// Summarize all the options used for output
	if (gOptions->exportFlags & EXPT_OUTPUT_MATERIALS)
	{
		if (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_SWATCHES)
			radio = 2;
		else if (gOptions->exportFlags & EXPT_OUTPUT_SEPARATE_TEXTURE_TILES)
			radio = 4;
		else if (gOptions->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES)
			radio = 3;
		else
			radio = 1;
	}
	else
		radio = 0;

	if (radio == 4) {
		sprintf_s(outputString, 256, "# File type: %s to directory %s\n", outputTypeString[radio], gOptions->pEFD->tileDirString);
	}
	else {
		sprintf_s(outputString, 256, "# File type: %s\n", outputTypeString[radio]);
	}
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "# Texture output RGB: %s\n", gOptions->pEFD->chkTextureRGB ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "# Texture output A: %s\n", gOptions->pEFD->chkTextureA ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "# Texture output RGBA: %s\n", gOptions->pEFD->chkTextureRGBA ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if ((gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ) || (gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ))
	{
		if (gOptions->pEFD->fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
		{
			strcpy_s(outputString, 256, "# OBJ relative coordinates\n");
			WERROR(PortaWrite(fh, outputString, strlen(outputString)));
		}

		sprintf_s(outputString, 256, "# Export separate objects: %s\n", gOptions->pEFD->chkSeparateTypes ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
		sprintf_s(outputString, 256, "# Individual blocks: %s\n", gOptions->pEFD->chkIndividualBlocks ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		if (gOptions->pEFD->chkSeparateTypes || gOptions->pEFD->chkIndividualBlocks)
		{
			sprintf_s(outputString, 256, "#   Material per object: %s\n", gOptions->pEFD->chkMaterialPerBlock ? "YES" : "no");
			WERROR(PortaWrite(fh, outputString, strlen(outputString)));
		}

		// was, pre-7.0, "Split materials into subtypes"
		sprintf_s(outputString, 256, "#   Split by block type: %s\n", gOptions->pEFD->chkSplitByBlockType ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "# G3D full material: %s\n", gOptions->pEFD->chkG3DMaterial ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "# Make Z the up direction instead of Y: %s\n", gOptions->pEFD->chkMakeZUp[gOptions->pEFD->fileType] ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	// option only available when rendering - always true when 3D printing.
	if (!gPrint3D)
	{
		// note that we output gOptions->pEFD->chkCompositeOverlay even if gExportTiles is true
		sprintf_s(outputString, 256, "# Create composite overlay faces: %s\n", gOptions->pEFD->chkCompositeOverlay ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "# Center model: %s\n", gOptions->pEFD->chkCenterModel ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "# Export lesser blocks: %s\n", gOptions->pEFD->chkExportAll ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if (gOptions->pEFD->chkExportAll)
	{
		sprintf_s(outputString, 256, "# Fatten lesser blocks: %s\n", gOptions->pEFD->chkFatten ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	// options only available when rendering.
	if (!gPrint3D)
	{
		sprintf_s(outputString, 256, "# Create block faces at the borders: %s\n", gOptions->pEFD->chkBlockFacesAtBorders ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "# Make tree leaves solid: %s\n", gOptions->pEFD->chkLeavesSolid ? "YES" : "no");
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "# Use biomes: %s\n", gOptions->pEFD->chkBiome ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	// now always on by default
	//sprintf_s(outputString,256,"# Merge flat blocks with neighbors: %s\n", gOptions->pEFD->chkMergeFlattop ? "YES" : "no" );
	//WERROR(PortaWrite(fh, outputString, strlen(outputString) ));

	if (gOptions->pEFD->radioRotate0)
		angle = 0;
	else if (gOptions->pEFD->radioRotate90)
		angle = 90;
	else if (gOptions->pEFD->radioRotate180)
		angle = 180;
	else
	{
		angle = 270;
		assert(gOptions->pEFD->radioRotate270);
	}

	sprintf_s(outputString, 256, "# Rotate model %f degrees\n", angle);
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	if (gOptions->pEFD->radioScaleByBlock)
	{
		sprintf_s(outputString, 256, "# Scale model by making each block %g mm high\n", gOptions->pEFD->blockSizeVal[gOptions->pEFD->fileType]);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}
	else if (gOptions->pEFD->radioScaleByCost)
	{
		sprintf_s(outputString, 256, "# Scale model by aiming for a cost of %0.2f for the %s material\n", gOptions->pEFD->costVal, gMtlCostTable[gPhysMtl].name);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}
	else if (gOptions->pEFD->radioScaleToHeight)
	{
		sprintf_s(outputString, 256, "# Scale model by fitting to a height of %g cm\n", gOptions->pEFD->modelHeightVal);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}
	else if (gOptions->pEFD->radioScaleToMaterial)
	{
		sprintf_s(outputString, 256, "# Scale model by using the minimum wall thickness for the %s material\n", gMtlCostTable[gPhysMtl].name);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "# Data operation options:\n#   Fill air bubbles: %s; Seal off entrances: %s; Fill in isolated tunnels in base of model: %s\n",
		(gOptions->pEFD->chkFillBubbles ? "YES" : "no"),
		(gOptions->pEFD->chkSealEntrances ? "YES" : "no"),
		(gOptions->pEFD->chkSealSideTunnels ? "YES" : "no"));
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Connect parts sharing an edge: %s; Connect corner tips: %s; Weld all shared edges: %s\n",
		(gOptions->pEFD->chkConnectParts ? "YES" : "no"),
		(gOptions->pEFD->chkConnectCornerTips ? "YES" : "no"),
		(gOptions->pEFD->chkConnectAllEdges ? "YES" : "no"));
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Delete floating objects: trees and parts smaller than %d blocks: %s\n",
		gOptions->pEFD->floaterCountVal,
		(gOptions->pEFD->chkDeleteFloaters ? "YES" : "no"));
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Hollow out bottom of model, making the walls %g mm thick: %s; Superhollow: %s\n",
		gOptions->pEFD->hollowThicknessVal[gOptions->pEFD->fileType],
		(gOptions->pEFD->chkHollow[gOptions->pEFD->fileType] ? "YES" : "no"),
		(gOptions->pEFD->chkSuperHollow[gOptions->pEFD->fileType] ? "YES" : "no"));
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "# Melt snow blocks: %s\n", gOptions->pEFD->chkMeltSnow ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Debug: show separate parts as colors: %s\n", gOptions->pEFD->chkShowParts ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	sprintf_s(outputString, 256, "#   Debug: show weld blocks in bright colors: %s\n", gOptions->pEFD->chkShowWelds ? "YES" : "no");
	WERROR(PortaWrite(fh, outputString, strlen(outputString)));

	// write out processing stats for 3D printing
	if (gOptions->exportFlags & (EXPT_FILL_BUBBLES | EXPT_CONNECT_PARTS | EXPT_DELETE_FLOATING_OBJECTS))
	{
		sprintf_s(outputString, 256, "\n# Cleanup processing summary:\n#   Solid parts: %d\n",
			gStats.numSolidGroups);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & EXPT_FILL_BUBBLES)
	{
		sprintf_s(outputString, 256, "#   Air bubbles found and filled (with glass): %d\n",
			gStats.bubblesFound);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & (EXPT_FILL_BUBBLES | EXPT_CONNECT_PARTS))
	{
		sprintf_s(outputString, 256, "#   Total solid parts merged: %d\n",
			gStats.solidGroupsMerged);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & EXPT_CONNECT_PARTS)
	{
		sprintf_s(outputString, 256, "#   Number of edge passes made: %d\n",
			gStats.numberManifoldPasses);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "#     Edges found to fix: %d\n",
			gStats.nonManifoldEdgesFound);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "#     Weld blocks added: %d\n",
			gStats.blocksManifoldWelded);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & EXPT_CONNECT_CORNER_TIPS)
	{
		sprintf_s(outputString, 256, "#     Tip blocks added: %d\n",
			gStats.blocksCornertipWelded);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & EXPT_DELETE_FLOATING_OBJECTS)
	{
		sprintf_s(outputString, 256, "#   Floating parts removed: %d\n",
			gStats.floaterGroupsDeleted);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "#     In these floaters, total blocks removed: %d\n",
			gStats.blocksFloaterDeleted);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (gOptions->exportFlags & EXPT_HOLLOW_BOTTOM)
	{
		sprintf_s(outputString, 256, "#   Blocks removed by hollowing: %d\n",
			gStats.blocksHollowed);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));

		sprintf_s(outputString, 256, "#   Blocks removed by further super-hollowing (i.e. not just vertical hollowing): %d\n",
			gStats.blocksSuperHollowed);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	if (pCBC) {
		int cmdCount = 0;
		while (pCBC != NULL) {
			cmdCount++;
			pCBC = pCBC->next;
		}
		sprintf_s(outputString, 256, "# Change commands issued: %d\n", cmdCount);
		WERROR(PortaWrite(fh, outputString, strlen(outputString)));
	}

	sprintf_s(outputString, 256, "\n# Full world path: %s\n", worldChar);
	WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));

	if (gModel.terrainImageNotFound) {
		strcpy_s(outChar, MAX_PATH_AND_FILE, "default");
	}
	else {
		WcharToChar(terrainFileName, outChar, MAX_PATH_AND_FILE);
	}
    sprintf_s(outputString, 256, "# Full terrainExt.png path: %s\n", outChar);
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));

    WcharToChar(curDir, outChar, MAX_PATH_AND_FILE);
    sprintf_s(outputString, 256, "# Full current path: %s\n", outChar);
    WERROR(PortaWrite(gModelFile, outputString, strlen(outputString)));

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

    wcsncpy_s(gOutputFileList->name[gOutputFileList->count],MAX_PATH_AND_FILE,filename,MAX_PATH_AND_FILE);
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
    int row, drow, dcol, dst_offset, src_offset;
    unsigned int *di, *si;
    SWATCH_TO_COL_ROW(swatchIndex, dcol, drow);
    di = ((unsigned int *)(&dst->image_data[0])) + (drow * gModel.swatchSize * dst->width + dcol * gModel.swatchSize);
    si = di + (int)(startStretch * gModel.swatchSize) * dst->width;

    for (row = 0; row < gModel.swatchSize; row++)
    {
        dst_offset = row * dst->width;
        src_offset = (int)(row * (1.0f - startStretch)) * dst->width;

        memcpy(di + dst_offset, si + src_offset, gModel.swatchSize * 4);
    }
}

// Bilinearly interpolate from original to the full tile. That is, go through the full tile and
// figure out how much of each original smaller tile contributes to each pixel.
// values are the pixel locations within the canonical 16x16 tile, so 0,0,15,15 would be mapping the tile to itself
static void stretchSwatchToFill(progimage_info *dst, int swatchIndex, int xlo, int ylo, int xhi, int yhi)
{
    int row, col, dcol, drow, scol, srow;
    float xwidth = (float)(xhi - xlo);
    float ywidth = (float)(yhi - ylo);
    unsigned int *di, *si;

    // copy source to workspace location, as we really can't do this in-place
    SWATCH_TO_COL_ROW(SWATCH_WORKSPACE, scol, srow);
    SWATCH_TO_COL_ROW(swatchIndex, dcol, drow);	// use dcol and drow, as we'll reverse direction in a moment
    copyPNGTile(dst, scol, srow, gModel.swatchSize, dst, dcol, drow);

    // now go through all pixels in tile (not including swatch border, which should be empty at this point)
    // of original and figure out interpolated color from the workspace copy.
    // si is first "real" pixel in the (copied) swatch, which we'll index from.
    float tsm = (float)(gModel.tileSize - 1);
    si = ((unsigned int *)(&dst->image_data[0])) +
        (srow * gModel.swatchSize + ylo + 1) * dst->width + (scol * gModel.swatchSize + xlo) + 1;
    for (row = 0; row < gModel.tileSize; row++)
    {
        // start 1,1 in from corner of destination swatch
        di = ((unsigned int *)(&dst->image_data[0])) +
            (drow * gModel.swatchSize + row + 1) * dst->width + (dcol * gModel.swatchSize) + 1;

        float yf = (float)row * ywidth / tsm;
        float yfrac = (float)fmod(yf,1.0f);
        int yilo = (int)floor(yf);
        for (col = 0; col < gModel.tileSize; col++)
        {
            // Compute interpolated value.
            // First, what is this row and column location's position?
            // This position is in terms of 0.0 to 1.0.
            float xf = (float)col * xwidth / tsm;
            float xfrac = (float)fmod(xf,1.0f);
            int xilo = (int)floor(xf);

            // To compute, we need to know the influence of the four pixels touched by the current location.
            // compute weights for the four nearby pixels
            // 0 1
            // 2 3
            float weight[4];
            weight[0] = (1.0f - yfrac)*(1.0f - xfrac);
            weight[1] = (1.0f - yfrac)*xfrac;
            weight[2] = yfrac*(1.0f - xfrac);
            weight[3] = yfrac*xfrac;

            // the four locations to interpolate from, indexing into si
            int loc[4];
            loc[0] = yilo * dst->width + xilo;
            loc[1] = loc[0] + 1;
            loc[2] = loc[0] + dst->width;
            loc[3] = loc[2] + 1;

            float red, green, blue, alpha;
            red = green = blue = 0.0f;
            alpha = 255.0f;	// assumed; if not true, then we need to premultiply, etc.
            for (int ix = 0; ix < 2; ix++) {
                for (int iy = 0; iy < 2; iy++) {
                    unsigned int value = *(si + loc[ix+iy*2]);
                    unsigned char dr, dg, db, da;
                    GET_PNG_TEXEL(dr, dg, db, da, value);
                    red   += (float)dr * weight[ix + iy * 2];
                    green += (float)dg * weight[ix + iy * 2];
                    blue  += (float)db * weight[ix + iy * 2];
                }
            }
            SET_PNG_TEXEL(*di, (unsigned char)red, (unsigned char)green, (unsigned char)blue, (unsigned char)alpha);

            // next pixel in row
            di++;
        }
    }

    // now fill in the edges with adjacent colors, to round things out
    // First copy columns outwards
    copyPNGArea(dst, dcol * gModel.swatchSize,           drow * gModel.swatchSize + 1, 1, gModel.tileSize, dst,       dcol * gModel.swatchSize + 1, drow * gModel.swatchSize + 1); // 0,1 copied from 1,1
    copyPNGArea(dst, (dcol + 1) * gModel.swatchSize - 1, drow * gModel.swatchSize + 1, 1, gModel.tileSize, dst, (dcol + 1) * gModel.swatchSize - 2, drow * gModel.swatchSize + 1);
    // then copy full rows outwards
    copyPNGArea(dst, dcol * gModel.swatchSize,       drow * gModel.swatchSize,     gModel.swatchSize, 1, dst, dcol * gModel.swatchSize,       drow * gModel.swatchSize + 1);
    copyPNGArea(dst, dcol * gModel.swatchSize, (drow + 1) * gModel.swatchSize - 1, gModel.swatchSize, 1, dst, dcol * gModel.swatchSize, (drow + 1) * gModel.swatchSize - 2);
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

static void multiplyPNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	int row, col;
	unsigned int *di;

	assert(x*tileSize + tileSize - 1 < (int)dst->width);

	for (row = 0; row < tileSize; row++)
	{
		di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x * tileSize);
		for (col = 0; col < tileSize; col++)
		{
			unsigned int value = *di;
			unsigned char dr, dg, db, da;
			GET_PNG_TEXEL(dr, dg, db, da, value);
			SET_PNG_TEXEL(*di, (unsigned char)(dr * r / 255), (unsigned char)(dg * g / 255), (unsigned char)(db * b / 255), (unsigned char)(da * a / 255));
			di++;
		}
	}
}

// more a rescale than a clamp, but that's fine
static void multiplyClampPNGTile(progimage_info *dst, int x, int y, int tileSize, int r, int g, int b, unsigned char a)
{
	int row, col;
	unsigned int *di;
	int idr, idg, idb, imax;

	assert(x*tileSize + tileSize - 1 < (int)dst->width);

	for (row = 0; row < tileSize; row++)
	{
		di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x * tileSize);
		for (col = 0; col < tileSize; col++)
		{
			unsigned int value = *di;
			unsigned char dr, dg, db, da;
			GET_PNG_TEXEL(dr, dg, db, da, value);
			idr = dr * r / 255;
			idg = dg * g / 255;
			idb = db * b / 255;
			imax = max(idr, idg);
			imax = max(imax, idb);
			if (imax > 255) {
				// scale results to be in 0-255 range and maintain hue, more or less
				// recalculate from scratch, to avoid precision problems
				idr = dr * r / imax;
				idg = dg * g / imax;
				idb = db * b / imax;
			}
			SET_PNG_TEXEL(*di, (unsigned char)idr, (unsigned char)idg, (unsigned char)idb, (unsigned char)(da * a / 255));
			di++;
		}
	}
}

// very special purpose, for water: the blue channel wants to be the value set, the red and green get multiplied by the grayscale value of the tile
static void bluePNGTile(progimage_info *dst, int x, int y, int tileSize, unsigned char r, unsigned char g, unsigned char b)
{
	int row, col;
	unsigned int *di;

	assert(x*tileSize + tileSize - 1 < (int)dst->width);

	for (row = 0; row < tileSize; row++)
	{
		di = ((unsigned int *)(&dst->image_data[0])) + ((y*tileSize + row) * dst->width + x * tileSize);
		for (col = 0; col < tileSize; col++)
		{
			unsigned int value = *di;
			unsigned char dr, dg, db, da;
			GET_PNG_TEXEL(dr, dg, db, da, value);
			SET_PNG_TEXEL(*di, (unsigned char)(dr * r / 255), (unsigned char)(dg * g / 255), b, da);
			di++;
		}
	}
}

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

// for transparent pixels, read from four neighbors and take average of all that exist. Second pass then marks these transparent pixels as opaque
static int bleedPNGSwatch(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha)
{
	int tileSize16 = (swatchSize - 2) / 16;

	// these are swatch locations in index form (not yet multiplied by swatch size itself)
	int dcol = dstSwatch % swatchesPerRow;
	int drow = dstSwatch / swatchesPerRow;
	// upper left corner, starting location, but then pulled in by one, and the x offset is built in (y offset inside tile is done by loop)
	unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize + 1 + ymin)*dst->width + dcol * swatchSize + 1 + xmin * tileSize16;

	int row, col;
	unsigned char dr, dg, db, da;
	unsigned int *cdsti;
	int modCount = 0;

	for (row = ymin * tileSize16; row < ymax*tileSize16; row++)
	{
		int offset = row * dst->width;
		cdsti = dsti + offset;
		for (col = xmin * tileSize16; col < xmax*tileSize16; col++)
		{
			GET_PNG_TEXEL(dr, dg, db, da, *cdsti);

			if (da == 0)
			{
				// texel is transparent, so check its neighbors
				int i;
				int neighborCount = 0;
				unsigned char nr, ng, nb, na;
				unsigned int *cneighi;
				int sumr = 0;
				int sumg = 0;
				int sumb = 0;

				// first try four neighbors by edge
				for (i = 0; i < 4; i++)
				{
					switch (i)
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
					GET_PNG_TEXEL(nr, ng, nb, na, *cneighi);
					// exact test, so we ignore texels we're changing in place
					if (na != 123 && na != 0)
					{
						sumr += nr;
						sumg += ng;
						sumb += nb;
						neighborCount++;
					}
				}

				// anything happen?
				if (neighborCount > 0)
				{
					// store texel in place, with a tag alpha of 123
					dr = (unsigned char)(sumr / neighborCount);
					dg = (unsigned char)(sumg / neighborCount);
					db = (unsigned char)(sumb / neighborCount);
					da = 123;
					SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
					modCount++;
				}
				else
				{
					// try four diagonal neighbors
					for (i = 0; i < 4; i++)
					{
						switch (i)
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
						GET_PNG_TEXEL(nr, ng, nb, na, *cneighi);
						// exact test, so we ignore texels we're changing in place
						if (na != 123 && na != 0)
						{
							sumr += nr;
							sumg += ng;
							sumb += nb;
							neighborCount++;
						}
					}

					// anything happen?
					if (neighborCount > 0)
					{
						// store texel in place, with a tag alpha of 123
						dr = (unsigned char)(sumr / neighborCount);
						dg = (unsigned char)(sumg / neighborCount);
						db = (unsigned char)(sumb / neighborCount);
						da = 123;
						SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
						modCount++;
					}
				}
			}
			cdsti++;
		}
	}

	// did any pixels get modified?
	if (modCount > 0) {

		// now go back and make all 123 alpha texels truly opaque
		// upper left corner, starting location, but then pulled in by one
		dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize + 1)*dst->width + dcol * swatchSize + 1 + xmin * tileSize16;

		for (row = ymin * tileSize16; row < ymax*tileSize16; row++)
		{
			int offset = row * dst->width;
			cdsti = dsti + offset;
			for (col = xmin * tileSize16; col < xmax*tileSize16; col++)
			{
				GET_PNG_TEXEL(dr, dg, db, da, *cdsti);

				if (da == 123)
				{
					da = (unsigned char)alpha;
					SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
				}
				cdsti++;
			}
		}
	}
	// uncomment to find any tiles that had no alphas in them. A few do, such as doors, trapdoors, etc. that don't have transparency in the original MC tiles but could in texture packs
	//else {
	//	assert(0);
	//}

	// how many pixels were modified; we could loop and modify them all, "bleed to edge"
	return modCount;
}

// this one can be called repeatedly, to expand outwards
static int bleedPNGSwatchRecursive(progimage_info *dst, int dstSwatch, int xmin, int xmax, int ymin, int ymax, int swatchSize, int swatchesPerRow, unsigned char alpha)
{
	int tileSize16 = (swatchSize - 2) / 16;

	// these are swatch locations in index form (not yet multiplied by swatch size itself)
	int dcol = dstSwatch % swatchesPerRow;
	int drow = dstSwatch / swatchesPerRow;
	// upper left corner, starting location, but then pulled in by one, and the x offset is built in (y offset inside tile is done by loop)
	unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize + 1 + ymin)*dst->width + dcol * swatchSize + 1 + xmin * tileSize16;

	int row, col;
	unsigned char dr, dg, db, da;
	unsigned int *cdsti;
	int modCount = 0;

	// better: store a list of alpha = 0 locations, X & Y, and go through these. Shrink list as alpha pixels are processed.
	// TODO: try on a high-res tree leaf texture (one we can see through) and see how mipmapping works on it.
	for (row = ymin * tileSize16; row < ymax*tileSize16; row++)
	{
		int offset = row * dst->width;
		cdsti = dsti + offset;
		for (col = xmin * tileSize16; col < xmax*tileSize16; col++)
		{
			GET_PNG_TEXEL(dr, dg, db, da, *cdsti);

			// single pass: if (da == 0 )
			// check if pixel is a cutout and is all black, i.e., can be modified
			if (da == 0 && dr == 0 && dg == 0 && db == 0)
			{
				// texel is transparent, so check its neighbors
				int i;
				int neighborCount = 0;
				unsigned char nr, ng, nb, na;
				unsigned int *cneighi;
				int sumr = 0;
				int sumg = 0;
				int sumb = 0;

				// try eight neighbors
				for (i = 0; i < 8; i++)
				{
					switch (i)
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
					case 4:
						cneighi = cdsti - dst->width - 1;
						break;
					case 5:
						cneighi = cdsti - dst->width + 1;
						break;
					case 6:
						cneighi = cdsti + dst->width - 1;
						break;
					case 7:
						cneighi = cdsti + dst->width + 1;
						break;
					}
					GET_PNG_TEXEL(nr, ng, nb, na, *cneighi);
					// exact test, so we ignore texels we're changing in place, i.e., those with an alpha of 123
					if ((na > 0 && na != 123) || (na == 0 && (nr != 0 || ng != 0 || nb != 0)))
					{
						sumr += nr;
						sumg += ng;
						sumb += nb;
						neighborCount++;
					}
				}

				// anything happen? Expand only when three neighbors are found that touch the pixel: edge or crevice
				if (neighborCount > 2)
				{
					// store texel in place, with a tag alpha of 123
					dr = (unsigned char)(sumr / neighborCount);
					dg = (unsigned char)(sumg / neighborCount);
					db = (unsigned char)(sumb / neighborCount);
					da = 123;
					modCount++;
					if (dr == 0 && dg == 0 && db == 0) {
						db = 1;	// tag it as "done", tiny bit above black
					}
					SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
				}
				/*
				else
				{
					// try four diagonal neighbors
					for (i = 0; i < 4; i++)
					{
						switch (i)
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
						GET_PNG_TEXEL(nr, ng, nb, na, *cneighi);
						// exact test, so we ignore texels we're changing in place, i.e., those with an alpha of 123
						if ((na > 0 && na != 123) || (na == 0 && (nr != 0 || ng != 0 || nb != 0)))
						{
							sumr += nr;
							sumg += ng;
							sumb += nb;
							neighborCount++;
						}
					}

					// anything happen?
					if (neighborCount > 0)
					{
						// store texel in place, with a tag alpha of 123
						dr = (unsigned char)(sumr / neighborCount);
						dg = (unsigned char)(sumg / neighborCount);
						db = (unsigned char)(sumb / neighborCount);
						da = 123;
						modCount++;
						if (dr == 0 && dg == 0 && db == 0) {
							db = 1;	// tag it as "done"
						}
						SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
					}
					else {
						// do we get here ever?
						dr = 1;
					}
				}
				*/
			}
			cdsti++;
		}
	}

	if (modCount > 0) {

		// now go back and make all 123 alpha texels truly opaque
		// upper left corner, starting location, but then pulled in by one
		dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize + 1)*dst->width + dcol * swatchSize + 1 + xmin * tileSize16;

		for (row = ymin * tileSize16; row < ymax*tileSize16; row++)
		{
			int offset = row * dst->width;
			cdsti = dsti + offset;
			for (col = xmin * tileSize16; col < xmax*tileSize16; col++)
			{
				GET_PNG_TEXEL(dr, dg, db, da, *cdsti);

				if (da == 123)
				{
					da = alpha; // (unsigned char)alpha;
					SET_PNG_TEXEL(*cdsti, dr, dg, db, da);
				}
				cdsti++;
			}
		}
	}

	// how many pixels were modified; we could loop and modify them all, "bleed to edge"
	return modCount;
}

// get average color of tile, spread it to all alpha==0 pixels so that mipmapping looks OK
static void makeRemainingTileAverage(progimage_info *dst, int dstSwatch, int swatchSize, int swatchesPerRow)
{
	int tileSize16 = (swatchSize - 2) / 16;

	// these are swatch locations in index form (not yet multiplied by swatch size itself)
	int dcol = dstSwatch % swatchesPerRow;
	int drow = dstSwatch / swatchesPerRow;
	// upper left corner, starting location, but then pulled in by one, and the x offset is built in (y offset inside tile is done by loop)
	unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + (drow*swatchSize + 1)*dst->width + dcol * swatchSize + 1;

	int row, col;
	unsigned int *cdsti;
	unsigned char color[4];
	unsigned char dr, dg, db, da;

	double dcolor[4];
	double sum_color[3], sum;

	sum_color[0] = sum_color[1] = sum_color[2] = sum = 0;

	// better: store a list of alpha = 0 locations, X & Y, and go through these. Shrink list as alpha pixels are processed.
	// TODO: try on a high-res tree leaf texture (one we can see through) and see how mipmapping works on it.
	// TODO: really, we should also bleed output edges to be average color, too. Not how it works currently.
	for (row = 0; row < 16*tileSize16; row++)
	{
		int offset = row * dst->width;
		cdsti = dsti + offset;
		for (col = 0; col < 16*tileSize16; col++)
		{
			GET_PNG_TEXEL(dr, dg, db, da, *cdsti);
			// linearize; really we should use sRGB conversions, but this is close enough
			dcolor[0] = pow(dr / 255.0, 2.2);
			dcolor[1] = pow(dg / 255.0, 2.2);
			dcolor[2] = pow(db / 255.0, 2.2);
			dcolor[3] = da / 255.0;
			sum_color[0] += dcolor[0] * dcolor[3];
			sum_color[1] += dcolor[1] * dcolor[3];
			sum_color[2] += dcolor[2] * dcolor[3];
			sum += dcolor[3];
			cdsti++;
		}
	}
	if (sum > 0) {
		// gamma correct and then unassociate for PNG storage
		color[0] = (unsigned char)(0.5 + 255.0 * pow((sum_color[0] / sum), 1 / 2.2));
		color[1] = (unsigned char)(0.5 + 255.0 * pow((sum_color[1] / sum), 1 / 2.2));
		color[2] = (unsigned char)(0.5 + 255.0 * pow((sum_color[2] / sum), 1 / 2.2));
		//color[3] = 255;
		
		// for output, don't pull in by one, we want to cover the whole swatch with the color
		dsti = (unsigned int *)(&dst->image_data[0]) + drow*swatchSize*dst->width + dcol * swatchSize;
		for (row = 0; row < 16 * tileSize16 + 2; row++)
		{
			int offset = row * dst->width;
			cdsti = dsti + offset;
			for (col = 0; col < 16 * tileSize16 + 2; col++)
			{
				GET_PNG_TEXEL(dr, dg, db, da, *cdsti);
				if (da == 0 && dr == 0 && dg == 0 && db == 0) {
					// cutout mode: set color of fully transparent pixels to the average color
					SET_PNG_TEXEL(*cdsti, color[0], color[1], color[2], 0);
					// don't touch alpha, leave it unassociated
				}
				cdsti++;
			}
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
	//unsigned char or,og,ob,oa;
	unsigned char oa;
	unsigned char ur,ug,ub,ua;
    unsigned int *coveri,*cunderi,*cdsti;

    for ( row = 0; row < swatchSize; row++ )
    {
        int offset = row*dst->width;
        coveri = overi + offset;
        cunderi = underi + offset;
        cdsti = dsti + offset;
        for ( col = 0; col < swatchSize; col++ )
        {
            //GET_PNG_TEXEL( or,og,ob,oa, *coveri );
			// need only oa
			oa = (unsigned char)(((*coveri) >> 24) & 0xff);
            GET_PNG_TEXEL( ur,ug,ub,ua, *cunderi );

            if ( forceSolid == FORCE_SOLID )
            {
                ua = 255;
            }
            else if (forceSolid == FORCE_CUTOUT)
            {
                // for redstone wires, just make sure the alpha is 0 or 255
                ua = (ua<128) ? 0 : 255;
            }

            // finally have all data: composite o over u
            // Minecraft use oa as a cutout, so < 128 means 0
            if (oa < 128)
            {
                // copy the under pixel
                SET_PNG_TEXEL( *cdsti, ur,ug,ub,ua );
                // essentially *cdsti = *cunderi;, but with ua possibly changed
            }
            else
            {
                // copy the over pixel
                *cdsti = *coveri;
            }
            //else if (oma == 0)
            //{
            //	// copy the over pixel
            //	*cdsti = *coveri;
            //}
            //else
   // we never blend - Minecraft basically uses alpha as a cutout, 0 or 255
   //         {
   //             // full blend must be done: use over, http://en.wikipedia.org/wiki/Alpha_compositing
   //             dr = (unsigned char)((or*oa*255 + ur*ua*oma)/(255*255));
   //             dg = (unsigned char)((og*oa*255 + ug*ua*oma)/(255*255));
   //             db = (unsigned char)((ob*oa*255 + ub*ua*oma)/(255*255));
   //             da = (unsigned char)((oa*255 + ua*oma)/255);
   //             SET_PNG_TEXEL( *cdsti, dr,dg,db,da );
   //         }
            coveri++;
            cunderi++;
            cdsti++;
        }
    }
}

static void compositePNGSwatchOverColor(progimage_info *dst, int dstSwatch, int overSwatch, int underColor, int swatchSize, int swatchesPerRow )
{
    unsigned char ur,ug,ub,ua;
    // reversed, because that's how it works
    GET_PNG_TEXEL( ub,ug,ur,ua, underColor );
    ua = 255;   // always solid underlying color

    // these are swatch locations in index form (not yet multiplied by swatch size itself)
    int ocol = overSwatch % swatchesPerRow;
    int orow = overSwatch / swatchesPerRow;
    int dcol = dstSwatch % swatchesPerRow;
    int drow = dstSwatch / swatchesPerRow;
    // upper left corner, starting location, of each: over, under, destination
    unsigned int *overi = (unsigned int *)(&dst->image_data[0]) + orow*swatchSize*dst->width + ocol*swatchSize;
    unsigned int *dsti = (unsigned int *)(&dst->image_data[0]) + drow*swatchSize*dst->width + dcol*swatchSize;

    int row,col;
    unsigned char or,og,ob,oa;
    unsigned char dr,dg,db,da;
    unsigned int *coveri,*cdsti;

    for ( row = 0; row < swatchSize; row++ )
    {
        int offset = row*dst->width;
        coveri = overi + offset;
        cdsti = dsti + offset;
        for ( col = 0; col < swatchSize; col++ )
        {
            unsigned char oma;

            GET_PNG_TEXEL( or,og,ob,oa, *coveri );

            oma = 255 - oa;

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

// for output of tiles

static bool writeTileFromMasterOutput(wchar_t *filename, progimage_info *src, int swatchLoc, int swatchSize, int swatchesPerRow)
{
	int retCode = MW_NO_ERROR;
	bool usesAlpha = doesTileHaveAlpha(src, swatchLoc, swatchSize, swatchesPerRow);
	// TODOTODO OK, this is a horrible kludge. For MWO_ chest tiles, we want these to not have alphas, to avoid transparency costs
	if ((wcsstr(filename, L"MWO_") != 0) && (wcsstr(filename, L"_chest_") != 0)) {
		usesAlpha = false;
	}
	int scol = swatchLoc % swatchesPerRow;
	int srow = swatchLoc / swatchesPerRow;

	int col, row;
	int numChannels = usesAlpha ? 4 : 3;
	unsigned char *imageDst, *imageSrc;
	progimage_info dst;
	dst.height = swatchSize - 2;
	dst.width = swatchSize - 2;
	dst.image_data.resize(dst.height*dst.width * numChannels);

	imageDst = &dst.image_data[0];

	for (row = 0; row < dst.height; row++)
	{
		// upper left corner, starting location, plus 1 row and column
		imageSrc = &src->image_data[0] + ((srow * swatchSize + 1 + row)*src->width + scol * swatchSize + 1)*4;
		for (col = 0; col < dst.width; col++)
		{
			// copy RGB only
			*imageDst++ = *imageSrc++;
			*imageDst++ = *imageSrc++;
			*imageDst++ = *imageSrc++;
			if ( usesAlpha )
				*imageDst++ = *imageSrc++;
			else
				imageSrc++;
		}
	}

	retCode |= writepng(&dst, numChannels, filename);
	addOutputFilenameToList(filename);

	writepng_cleanup(&dst);

	return retCode;
}

static bool doesTileHaveAlpha(progimage_info *src, int swatchLoc, int swatchSize, int swatchesPerRow)
{
	// these are swatch locations in index form (not yet multiplied by swatch size itself)
	int scol = swatchLoc % swatchesPerRow;
	int srow = swatchLoc / swatchesPerRow;

	int col, row;
	unsigned char dr, dg, db, da;

	// tileSize is always swatchSize-2
	for (row = 0; row < swatchSize - 2; row++)
	{
		// upper left corner, starting location, plus 1 row and column
		unsigned int *srci = (unsigned int *)(&src->image_data[0]) + (srow * swatchSize + 1 + row)*src->width + scol * swatchSize + 1;
		for (col = 0; col < swatchSize - 2; col++)
		{
			GET_PNG_TEXEL(dr, dg, db, da, *srci);
			if (da != 255)
				return true;
			srci++;
		}
	}
	return false;
}



///////////////////////////////////////////////////////////
//
// Utility functions, headers not included above
// suffix is of form ".stl" - includes dot
static void ensureSuffix( wchar_t *dst, const wchar_t *src, const wchar_t *suffix )
{
    int hasSuffix=0;

    // Prep file name: see if it has suffix already
    if ( wcsnlen(src,MAX_PATH_AND_FILE) > wcsnlen(suffix,MAX_PATH_AND_FILE) )
    {
        // look for suffix
        wchar_t foundSuffix[MAX_PATH_AND_FILE];
        wcsncpy_s(foundSuffix,MAX_PATH_AND_FILE,src + wcsnlen(src,MAX_PATH_AND_FILE)-wcsnlen(suffix,MAX_PATH_AND_FILE),20);
        _wcslwr_s(foundSuffix,MAX_PATH_AND_FILE);
        if (wcscmp(foundSuffix,suffix) == 0)
        {
            hasSuffix = 1;
        }
    }

    wcscpy_s(dst,MAX_PATH_AND_FILE,src);
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
        wcscat_s(dst,MAX_PATH_AND_FILE,suffix);
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

    // also look for final /
    const char *strfPtr = strrchr(src,(int)'/');
    if ( strfPtr )
        // found a /, so move up past it
        strPtr = strfPtr+1;
    else if ( strPtr == NULL )
        // no \ or / found, just return string itself
        return src;

    return strPtr;
}

static void getPathAndRoot( const wchar_t *src, int fileType, wchar_t *path, wchar_t *root )
{
    wchar_t *rootPtr;
    wchar_t tfilename[MAX_PATH_AND_FILE];

    wcscpy_s(path,MAX_PATH_AND_FILE,src);
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
    wcscpy_s(tfilename,MAX_PATH_AND_FILE,rootPtr);
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
		break;
    }
}

static void concatFileName2(wchar_t *dst, const wchar_t *src1, const wchar_t *src2)
{
    wcscpy_s(dst,MAX_PATH_AND_FILE,src1);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src2);
}

static void concatFileName3(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3)
{
    wcscpy_s(dst,MAX_PATH_AND_FILE,src1);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src2);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src3);
}

static void concatFileName4(wchar_t *dst, const wchar_t *src1, const wchar_t *src2, const wchar_t *src3, const wchar_t *src4)
{
    wcscpy_s(dst,MAX_PATH_AND_FILE,src1);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src2);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src3);
    wcscat_s(dst,MAX_PATH_AND_FILE-wcslen(dst),src4);
}

static void charToWchar( char *inString, wchar_t *outWString )
{
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,inString,-1,outWString,MAX_PATH_AND_FILE);
    //MultiByteToWideChar(CP_UTF8,0,inString,-1,outWString,MAX_PATH_AND_FILE);
}

static void wcharCleanse( wchar_t *wstring )
{
    char tempString[MAX_PATH_AND_FILE];
    WcharToChar(wstring, tempString, MAX_PATH_AND_FILE);
    charToWchar( tempString, wstring );
}

// for noise on textures, only - not really trustworthy otherwise
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

//=============================================

// return 0 if nothing found in volume
int GetMinimumSelectionHeight(WorldGuide *pWorldGuide, Options *pOptions, int minx, int minz, int maxx, int maxz, bool expandByOne, bool ignoreTransparent, int maxy)
{
    int minHeightFound = 256;

    if (expandByOne) {
        // We expand the bounds checked by one, so that if the selection is right on a cliff face and the bottom of
        // the cliff is outside the bounds, include that outside area so that the selection is reasonable. This is
        // the default.
        minx--;
        minz--;
        maxx++;
        maxz++;
    }

    int edgestartxblock = (int)floor((float)minx / 16.0f);
    int edgestartzblock = (int)floor((float)minz / 16.0f);
    int edgeendxblock = (int)floor((float)maxx / 16.0f);
    int edgeendzblock = (int)floor((float)maxz / 16.0f);

    for (int blockX = edgestartxblock; blockX <= edgeendxblock; blockX++)
    {
        for (int blockZ = edgestartzblock; blockZ <= edgeendzblock; blockZ++)
        {
            if (pOptions == NULL) {
                pOptions = gOptions;
            }

            int heightFound = analyzeChunk(pWorldGuide, pOptions, blockX, blockZ, minx, 0, minz, maxx, maxy, maxz, ignoreTransparent, gMcVersion);
            if (heightFound < minHeightFound)
            {
                minHeightFound = heightFound;
            }
        }
    }

    return (minHeightFound == 256) ? 0 : minHeightFound;
}

// find first (optional: non-transparent) block visible from above
static int analyzeChunk(WorldGuide *pWorldGuide, Options *pOptions, int bx, int bz, int minx, int miny, int minz, int maxx, int maxy, int maxz, bool ignoreTransparent, int mcVersion)
{
    int minHeight = 256;

    int chunkX, chunkZ;

    int loopXmin, loopZmin;
    int loopXmax, loopZmax;
    int x, y, z;

    int chunkIndex;

    WorldBlock *block;
    block = (WorldBlock *)Cache_Find(bx, bz);

    if (block == NULL)
    {
        wcsncpy_s(pWorldGuide->directory, MAX_PATH_AND_FILE, pWorldGuide->world, MAX_PATH_AND_FILE - 1);
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"/");
        if (pOptions->worldType&HELL)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM-1");
        }
        if (pOptions->worldType&ENDER)
        {
            wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, L"DIM1");
        }
        wcscat_s(pWorldGuide->directory, MAX_PATH_AND_FILE, gSeparator);

        block = LoadBlock(pWorldGuide, bx, bz, mcVersion);
		if ((block == NULL) || (block->blockType == 2)) //blank tile, nothing to do
            return minHeight;

        Cache_Add(bx, bz, block);
    }

    // loop through area of box that overlaps with this chunk
    chunkX = bx * 16;
    chunkZ = bz * 16;

    loopXmin = max(minx, chunkX);
    loopZmin = max(minz, chunkZ);

    loopXmax = min(maxx, chunkX + 15);
    loopZmax = min(maxz, chunkZ + 15);

    // Have we seen an empty voxel yet? Initialize to true if the volume scanned is at the maximum height, else
    // we might be in a cave and in nether hide-obscured mode and so ignore solid voxels until we hit air.
    int seenempty = (maxy == MAP_MAX_HEIGHT) ? 1 : 0;

    // don't need to turn on showobscured if we've seen an empty location already. NOTE: this is not in the map
    // draw() method, though probably needs to be. That's touchy code, though, so I won't mess with it now.
    bool showobscured = !seenempty && (pOptions->worldType & HIDEOBSCURED);

    for (x = loopXmin; x <= loopXmax; x++) {
        for (z = loopZmin; z <= loopZmax; z++) {
            chunkIndex = CHUNK_INDEX(bx, bz, x, maxy, z);
            int adj_maxy = maxy;
            if (showobscured) {
                // ignore solid voxels, continue from the first "hollow" empty area
                for (; adj_maxy >= miny; adj_maxy--) {
                    int type = block->grid[chunkIndex];

                    // always ignore air; if we ignore transparent, then anything with alpha < 1.0 is ignored, else alpha == 0.0 only is ignored
                    if ((type == BLOCK_AIR) || (gBlockDefinitions[type].alpha == 0.0)) {
                        break;
                    }
                    chunkIndex -= 256;
                }
            }
            for (y = adj_maxy; y >= miny; y--) {
                int type = block->grid[chunkIndex];

                // always ignore air; if we ignore transparent, then anything with alpha < 1.0 is ignored, else alpha == 0.0 only is ignored
                if ((type != BLOCK_AIR) &&
                    (ignoreTransparent ? (gBlockDefinitions[type].alpha == 1.0) : (gBlockDefinitions[type].alpha > 0.0))) {

                    if (y < minHeight)
                        minHeight = y;

                    // found something in this column, go to next column
                    break;
                }

                // go to next lower layer
                chunkIndex -= 256;
            }
        }
    }
    return minHeight;
}

static void seedWithXYZ(int boxIndex)
{
	int x, y, z;
	BOX_INDEX_TO_WORLD_XYZ(boxIndex, x, y, z);
	unsigned int iseed = (((x % 199) * 215 + (z % 211)) * 223 + (y % 223)) * 197 + 2147483647;
	srand(iseed);
}

static void seedWithXZ(int boxIndex)
{
	int x, y, z;
	BOX_INDEX_TO_WORLD_XYZ(boxIndex, x, y, z);
	unsigned int iseed = (((x % 199) * 215 + (z % 211)) * 223) * 197 + 2147483647;
	srand(iseed);
}