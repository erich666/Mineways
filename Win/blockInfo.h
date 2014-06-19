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


#ifndef __BLOCKINFO_H__
#define __BLOCKINFO_H__

#include <stdlib.h>

typedef unsigned int    UINT;


#define MAP_MAX_HEIGHT 255
#define MAP_MAX_HEIGHT_STRING L"255"

// sea level (it was 63 before 1.0 release)
#define SEA_LEVEL 62
#define SEA_LEVEL_STRING L"62"

//
//// Minimum *supported* wall thickness in mm http://www.shapeways.com/tutorials/thin_walls_tutorial
//// White should really be more like 1 mm or even 2 mm, not 0.7 mm - TODO documentation
//#define MINIMUM_WHITE_WALL_THICKNESS    (0.7f*MM_TO_METERS)
//#define MINIMUM_COLOR_WALL_THICKNESS    (2.0f*MM_TO_METERS)
//
//// sum of dimensions must be 65 mm: http://www.shapeways.com/design-rules/full_color_sandstone
//#define MINIMUM_COLOR_DIMENSION_SUM    (65.0f*MM_TO_METERS)
//
//// Material costs
//#define COST_WHITE_HANDLING 1.50f
//// officially it's 1.40f, but calculations on a 10 cm cube show it's this:
//#define COST_WHITE_CCM      1.39984314f
//// TODO: find out real minimal dimension sum
//#define COST_WHITE_MINIMUM  0.05f
//// density & size discount: http://www.shapeways.com/blog/archives/490-Significant-price-reduction-on-dense-models.html
//#define COST_WHITE_DISCOUNT_DENSITY_LEVEL 0.1f
//#define COST_WHITE_DISCOUNT_CCM_LEVEL 20.0f
//#define COST_WHITE_CCM_DISCOUNT (COST_WHITE_CCM*0.50f)
//
//#define COST_COLOR_HANDLING 3.00f
//// officially 0.75f, but from computations it's a tiny bit lower
//#define COST_COLOR_CCM      0.74991f


// conversions from meters
#define METERS_TO_MM   1000.0f
#define METERS_TO_CM   100.0f
#define METERS_TO_INCHES   (METERS_TO_CM/2.54f)
#define MM_TO_METERS   (1.0f/METERS_TO_MM)
#define CM_TO_METERS   (1.0f/METERS_TO_CM)

// TODO: yes, this is dumb, we have a separate static mtlCostTable for every code file that includes this .h file.

// http://www.shapeways.com/materials/material-options
#define MTL_COST_TABLE_SIZE 11

#define PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE 0
#define PRINT_MATERIAL_FULL_COLOR_SANDSTONE 1
#define PRINT_MATERIAL_FCS_SCULPTEO (MTL_COST_TABLE_SIZE-1)

static struct {
    // lame on my part: we really shouldn't be using wide characters in the dialog TODO
    wchar_t *wname;
    char *name;
    // minimum recommended wall thickness in mm, though usually you want to go 50% or more above this
    // Minimum *supported* wall thickness in mm http://www.shapeways.com/tutorials/thin_walls_tutorial
    // White should really be more like 1 mm or even 2 mm, not 0.7 mm - TODO documentation
    float minWall;
    // width + height + depth in mm - colored sandstone has this limit: http://www.shapeways.com/design-rules/full_color_sandstone
    float minDimensionSum;
    // fixed handling cost
    float costHandling;
    // cost of material per cubic centimeter
    // officially it's 1.40f for white, but calculations on a 10 cm cube show it's 1.39984314f
    float costPerCubicCentimeter;
    // discount: http://www.shapeways.com/blog/archives/490-Significant-price-reduction-on-dense-models.html
    float costDiscountDensityLevel; // model has to be at least this dense
    float costDiscountCCMLevel; // first X cubic centimeters are at normal price; above this is half price
    // cost minimum is a guess, doesn't include handling TODO
    float costMinimum;
    float costPerSquareCentimeter;   // cost per square centimeter of surface (currently only for ceramic)
    float maxSize[3];
} mtlCostTable[MTL_COST_TABLE_SIZE]={
    // wname                 name                  minWall in meters  minSum in meters    costH  costPerCCM cDens    cDisCCM  costMinimum?  costPerCM2 maxSizeCM (descending order)
    { L"White & Flexible",   "white & flexible",   0.7f*MM_TO_METERS, 0.0f,               1.50f, 1.40f,      0.10f,  20.0f,   1.50f*0.10f,  0.0f,      65.0f,55.0f,35.0f },
    { L"Colored Sandstone",  "colored sandstone",  2.0f*MM_TO_METERS, 65.0f*MM_TO_METERS, 3.00f, 0.75f,    999.0f, 99999.0f,  3.00f*0.134f, 0.0f,      38.0f,25.0f,20.0f },
	{ L"Colored & Flexible", "colored & flexible", 0.7f*MM_TO_METERS, 0.0f,               1.75f, 1.75f,    999.0f, 99999.0f,  1.75f*0.10f,  0.0f,      31.0f,23.0f,18.0f },
    { L"Alumide/Detail",     "alumide/detail",	   1.0f*MM_TO_METERS, 0.0f,               2.50f, 2.99f,    999.0f, 99999.0f,  2.50f*0.10f,  0.0f,      25.0f,25.0f,20.0f },
	{ L"Frosted Detail",     "frosted detail",     0.5f*MM_TO_METERS, 0.0f,               5.00f, 2.39f,    999.0f, 99999.0f,  5.00f*0.10f,  0.0f,      29.8f,20.3f,18.5f },
	{ L"Elasto Plastic",     "elasto plastic",     0.8f*MM_TO_METERS, 0.0f,               1.95f, 1.75f,    999.0f, 99999.0f,  1.95f*0.10f,  0.0f,      30.0f,30.0f,25.0f },
	{ L"Brass",              "brass",              0.6f*MM_TO_METERS, 0.0f,               10.0f, 16.0f,    999.0f, 99999.0f, 10.0f *0.10f,  0.0f,      10.0f, 8.9f, 8.9f },
	{ L"Silver",             "silver",             0.6f*MM_TO_METERS, 0.0f,               30.0f, 20.0f,    999.0f, 99999.0f, 20.0f *0.10f,  0.0f,      11.0f,11.0f, 3.0f },
	{ L"Stainless Steel",    "stainless steel",    3.0f*MM_TO_METERS, 0.0f,               6.00f, 8.00f,    999.0f, 99999.0f,  8.00f*0.10f,  0.0f,     100.0f,45.0f,25.0f },
	{ L"Glazed Ceramics",    "glazed ceramics",    3.0f*MM_TO_METERS, 0.0f,               6.00f, 0.35f,    999.0f, 99999.0f,  6.00f,        0.0f,      30.0f,22.0f,17.0f },
	// old model, based on surface area: { L"Glazed Ceramics",    "glazed ceramics",    3.0f*MM_TO_METERS, 0.0f,               0.00f, 0.00f,    999.0f, 99999.0f,  1.00f,        0.18f,     30.0f,22.0f,17.0f },
	// I haven't figured out Sculpteo's cost model. Prices tend to be 50% higher, so that's what's here, but I think it also has to do with dimensions, which affects print time.
	// With full-sized rectangular blocks, the costPerCCM is more like 1.05
	{ L"Sculpteo Multicolor","Sculpteo multicolor",2.0f*MM_TO_METERS, 0.0f,               2.55f, 1.375f,   999.0f, 99999.0f,  2.55f,        0.0f,      38.0f,25.0f,20.0f },	// Sculpteo sandstone
};


#define MODELS_UNITS_TABLE_SIZE 4

#define UNITS_METER 0
#define UNITS_CENTIMETER 1
#define UNITS_MILLIMETER 2
#define UNITS_INCHES 3

int unitIndex; // initialize to UNITS_METER

static struct {
    wchar_t *wname;
    char *name;
    float unitsPerMeter;
} unitTypeTable[MODELS_UNITS_TABLE_SIZE] = {
    { L"Meters", "meters", 1.0f },
    { L"Centimeters", "centimeters", 100.0f },
    { L"Millimeters", "millimeters", 1000.0f },
    { L"Inches", "inches", 100.0f/2.54f }
};


#define DIRECTION_BLOCK_SIDE_LO_X	0	
#define DIRECTION_BLOCK_BOTTOM		1	
#define DIRECTION_BLOCK_SIDE_LO_Z	2	
#define DIRECTION_BLOCK_SIDE_HI_X	3	
#define DIRECTION_BLOCK_TOP			4
#define DIRECTION_BLOCK_SIDE_HI_Z	5

#define DIR_LO_X_BIT    (1<<DIRECTION_BLOCK_SIDE_LO_X)	
#define DIR_BOTTOM_BIT  (1<<DIRECTION_BLOCK_BOTTOM)	
#define DIR_LO_Z_BIT    (1<<DIRECTION_BLOCK_SIDE_LO_Z)	
#define DIR_HI_X_BIT    (1<<DIRECTION_BLOCK_SIDE_HI_X)	
#define DIR_TOP_BIT     (1<<DIRECTION_BLOCK_TOP)
#define DIR_HI_Z_BIT    (1<<DIRECTION_BLOCK_SIDE_HI_Z)
#define DIR_ALL_BITS	(DIR_LO_X_BIT|DIR_HI_X_BIT|DIR_LO_Z_BIT|DIR_HI_Z_BIT|DIR_BOTTOM_BIT|DIR_TOP_BIT)


// options flags for export
// - note that these are useful, in that material output is turned on when
// the checkbox is checked
// DEBUG: output groups as separate materials, working from black wool down
#define EXPT_DEBUG_SHOW_GROUPS			0x0001
// DEBUG: output welds as lava - TODO
#define EXPT_DEBUG_SHOW_WELDS			0x0002
// leave 0x4 and 0x8 for future debugging modes

// Output material file
#define EXPT_OUTPUT_MATERIALS		    0x0010
// Model is meant to be watertight, e.g. for 3D printing
#define EXPT_3DPRINT				    0x0020
// Export a noisy color texture instead of simple solid materials
#define EXPT_OUTPUT_TEXTURE_SWATCHES    0x0040
// Export a true block texture instead of simple solid materials
#define EXPT_OUTPUT_TEXTURE_IMAGES	    0x0080
#define EXPT_OUTPUT_TEXTURE (EXPT_OUTPUT_TEXTURE_SWATCHES|EXPT_OUTPUT_TEXTURE_IMAGES)

// should faces be grouped by their material type?
#define EXPT_GROUP_BY_MATERIAL          0x0100

// Make the model as small as the material chosen allows (colored sandstone currently assumed)
#define EXPT_MAKE_SMALL				    0x0200

// These flags are useful as we sometimes want to check for any of them (e.g. for whether
// to make parts).
// Fill bubbles, hollow spots inside a model not visible from the outside
// This can be useful for models for viewing if you never plan to go inside the structure
#define EXPT_FILL_BUBBLES			    0x0400
// Join any two cubes sharing an edge and having different groups (i.e. are different parts)
#define EXPT_CONNECT_PARTS			    0x0800
// Join any two cubes sharing an edge, period. Aggressive, do only if needed by 3D printer.
// http://www.shapeways.com/tutorials/things-to-keep-in-mind says it's needed, but uploading
// two separate cubes sharing an edge works fine with Shapeways. The newer
// http://www.shapeways.com/tutorials/fixing-non-manifold-models doesn't mention shared edges.
#define EXPT_CONNECT_ALL_EDGES          0x1000
// Join any two cubes *in different meta-groups* that share corner tips
#define EXPT_CONNECT_CORNER_TIPS		0x2000
// Delete objects < 16 blocks or all tree that are not at the ground level
#define EXPT_DELETE_FLOATING_OBJECTS	0x4000
// Hollow out the bottom of the model (TODO: could add a base, with a hole in it)
#define EXPT_HOLLOW_BOTTOM				0x8000
// Aggressively hollow out an area, carving from the bottom around tunnels.
// The danger is that holes can be created which may not get emptied.
#define EXPT_SUPER_HOLLOW_BOTTOM		0x10000
// Allow BLF_ENTRANCE blocks to act solid when forming air groups. This allows
// insides of buildings to get sealed off and filled.
#define EXPT_SEAL_ENTRANCES				0x20000
// Tunnels on the sides and bottom of the solid box are considered bubbles and filled with glass
// For now, make it the same thing as sealing entrances - I'm not sure it needs yet another checkbox
#define EXPT_SEAL_SIDE_TUNNELS			0x40000

// do we want to export by block? That is, every block creates a group?
#define EXPT_GROUP_BY_BLOCK				0x80000

// when exporting objects with OBJ, do we want to export groups at all (overridden by Group by Block, though this doesn't show on the interface dialog currently)
#define EXPT_OUTPUT_OBJ_GROUPS				0x100000
// when exporting objects with OBJ, do we want multiple materials?
// This is the norm, but for things like Blender and Maya, a single material can be less work
#define EXPT_OUTPUT_OBJ_MATERIAL_PER_TYPE	0x200000
// when exporting textures with OBJ, output a uniform material: Ka and Kd set to white (G3D likes this)
#define EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL	0x400000
// when exporting materials with OBJ, output the extra values (G3D likes this)
#define EXPT_OUTPUT_OBJ_FULL_MATERIAL		0x800000

// relative or absolute coordinates for OBJ
#define EXPT_OUTPUT_OBJ_REL_COORDINATES		0x1000000

#define EP_FIELD_LENGTH 20

// linked to the ofn.lpstrFilter in Mineways.cpp
// This is a variant: some viewers (e.g. Deep View) will multiply the material color by the texture;
// use this variant if you notice textures getting shaded different colors.
#define FILE_TYPE_WAVEFRONT_ABS_OBJ 0
#define FILE_TYPE_WAVEFRONT_REL_OBJ 1
#define FILE_TYPE_BINARY_MAGICS_STL 2
#define FILE_TYPE_BINARY_VISCAM_STL 3
#define FILE_TYPE_ASCII_STL 4
#define FILE_TYPE_VRML2 5
// this is an entirely separate file type, only exportable through the schematic export option
#define FILE_TYPE_SCHEMATIC 6

#define FILE_TYPE_TOTAL         7


typedef struct
{
    // dialog file type last chosen in export dialog; this is used next time.
    // Note that this value is *not* valid during export itself; fileType is passed in.
    int fileType;           // 0,1 - OBJ, 2,3 - Binary STL, 4 - ASCII STL, 5 - VRML2, 6 - Schematic

    // in reality, the character fields could be kept private, but whatever
    char minxString[EP_FIELD_LENGTH];
    char minyString[EP_FIELD_LENGTH];
    char minzString[EP_FIELD_LENGTH];
    char maxxString[EP_FIELD_LENGTH];
    char maxyString[EP_FIELD_LENGTH];
    char maxzString[EP_FIELD_LENGTH];
    int minxVal;
    int minyVal;
    int minzVal;
    int maxxVal;
    int maxyVal;
    int maxzVal;

    UINT radioExportNoMaterials[FILE_TYPE_TOTAL];
    UINT radioExportMtlColors[FILE_TYPE_TOTAL];
    UINT radioExportSolidTexture[FILE_TYPE_TOTAL];
    UINT radioExportFullTexture[FILE_TYPE_TOTAL];

	// I decided to make flattening always on; the only use I've seen to not flattening is to make the torches on the
	// Eiffel tower become yellow blocks, when not exporting textures.
    UINT chkMergeFlattop;
    UINT chkExportAll;
	UINT chkFatten;
    UINT chkMakeZUp[FILE_TYPE_TOTAL];

    UINT radioRotate0;
    UINT radioRotate90;
    UINT radioRotate180;
    UINT radioRotate270;

    UINT radioScaleToHeight;
    UINT radioScaleToMaterial;
    UINT radioScaleByBlock;
    UINT radioScaleByCost;

    char modelHeightString[EP_FIELD_LENGTH];
    char blockSizeString[EP_FIELD_LENGTH];
    char costString[EP_FIELD_LENGTH];
    float modelHeightVal;
    float blockSizeVal[FILE_TYPE_TOTAL];
    float costVal;

	UINT chkCreateZip[FILE_TYPE_TOTAL];
	UINT chkCreateModelFiles[FILE_TYPE_TOTAL];	// i.e. don't delete them at end

    UINT chkCenterModel;
	UINT chkIndividualBlocks;

    UINT chkFillBubbles;
    UINT chkSealEntrances;
    UINT chkSealSideTunnels;
    UINT chkConnectParts;
    UINT chkConnectCornerTips;
    UINT chkConnectAllEdges;
    UINT chkDeleteFloaters;
    UINT chkHollow;
    UINT chkSuperHollow;
    UINT chkMeltSnow;

    char floaterCountString[EP_FIELD_LENGTH];
    int floaterCountVal;
    char hollowThicknessString[EP_FIELD_LENGTH];    // note we do not keep multiple of the strings, they're transitory
    float hollowThicknessVal[FILE_TYPE_TOTAL];

    int comboPhysicalMaterial[FILE_TYPE_TOTAL];
    int comboModelUnits[FILE_TYPE_TOTAL];

    UINT chkShowParts;
    UINT chkShowWelds;

	UINT chkMultipleObjects;
	UINT chkMaterialPerType;
	UINT chkG3DMaterial;

	UINT flags;
} ExportFileData;

#define MAX_OUTPUT_FILES 5

typedef struct FileList {
    int count;
    wchar_t name[MAX_OUTPUT_FILES][260];  // output file list, MAX_PATH == 260
} FileList;


typedef struct Options {
    int worldType;          // what world we're looking at: HELL, ENDER, etc.
    int saveFilterFlags;	// what objects should be kept - basic difference is flatsides get shown
    int exportFlags;		// exporting options
    int moreExportMemory;             // use more memory for caching or not?
	int currentCacheSize;
    ExportFileData *pEFD;   // print or view option values, etc.
    ///// these are really statistics, but let's shove them in here - so sloppy!
    int dimensions[3];
    float dim_inches[3];
    float dim_cm[3];
    float cost;
    int totalBlocks;
	float block_mm;
	float block_inch;
} Options;


// number of official Minecraft blocks (take highest block ID and add 1)
#define NUM_BLOCKS_STANDARD 176
// number we support right now (1.6)
#define NUM_BLOCK_SUPPORTED 174
// number of blocks + 16 for the 16 colored wool, plus one for the unknown block
// NOTE! if we ever go past 256, search for 256 in code and start fixing!
#define NUM_BLOCKS (NUM_BLOCKS_STANDARD+16+1)

// number of texture swatches
#define NUM_SWATCHES (NUM_BLOCKS+256)

// absolute max the 2x2 * 16x16 space of swatches could have (without borders)
#define NUM_MAX_SWATCHES (4*16*16)

// row & column to swatch location
#define SWATCH_XY_TO_INDEX(x,y) (NUM_BLOCKS + (y)*16 + (x))




#define BLF_NONE			0x0000
// fills whole block
#define BLF_WHOLE			0x0001
// almost a whole block
#define BLF_ALMOST_WHOLE    0x0002
// stairs
#define BLF_STAIRS			0x0004
// half block
#define BLF_HALF			0x0008
// fair-sized, worth rendering, has geometry
#define BLF_MIDDLER			0x0010
// larger billboard object worth rendering
#define BLF_BILLBOARD		0x0020
// billboard flat through middle, usually transparent (portal, glass pane)
#define BLF_PANE			0x0040
// sits on top of a block below it
#define BLF_FLATTOP			0x0080
// flat on a wall: sign, ladder, etc. - normally culled out
#define BLF_FLATSIDE		0x0100
// small, not as worth rendering, has geometry - normally culled out
#define BLF_SMALL_MIDDLER	0x0200
// small thing: lever, flower - normally culled out
#define BLF_SMALL_BILLBOARD	0x0400

// has an alpha for the whole block (vs. glass, which often has a frame that's solid)
#define BLF_TRANSPARENT		0x0800
// has cutout parts to its texture, on or off (no semitransparent alpha)
#define BLF_CUTOUTS			0x1000
// trunk
#define BLF_TRUNK_PART      0x2000
// leaf
#define BLF_LEAF_PART       0x4000
// is related to trees - if something is floating and is a tree, delete it for printing
#define BLF_TREE_PART       (BLF_TRUNK_PART|BLF_LEAF_PART)
// is an entrance of some sort, for sealing off building interiors
#define BLF_ENTRANCE        0x8000
// export image texture for this object, as it makes sense - almost everything has this property (i.e. has a texture tile)
#define BLF_IMAGE_TEXTURE   0x10000
// this object emits light
#define BLF_EMITTER         0x20000
// this object attaches to fences; note that fences do not have this property themselves, so that nether & regular fence won't attach
#define BLF_FENCE_NEIGHBOR	0x40000
// this object outputs its true geometry (not just a block) for rendering
#define BLF_TRUE_GEOMETRY	0x80000
// this object outputs its special non-full-block geometry for 3D printing, if the printer can glue together the bits.
// Slightly different than TRUE_GEOMETRY in that things that are just too thin don't have this bit set.
#define BLF_3D_BIT          0x100000
// this object is a 3D bit, and this bit is set if it can actually glue horizontal neighboring blocks together
// - not really used. TODO - may want to use this to decide whether objects should be grouped together or whatever.
#define BLF_3D_BIT_GLUE     0x200000
// set if the block does not affect fluid height
#define BLF_DNE_FLUID		0x400000
// set if the block connects to redstone - do only if there's no orientation to the block, e.g. repeaters attach only on two sides, so don't have this flag
#define BLF_CONNECTS_REDSTONE		0x800000
// set if the name of the block should be ignored and whatever is underneath it should be given. Currently only water and glass are hidden.
#define BLF_HIDE_ON_MAP		0x1000000

// IMPORTANT: note that *every* module that includes this data structure has
// *their own copy*. So ColorSchemes has its own master copy (which it never
// changes), MinewaysMaps gets its changed by ColorSchemes, and ObjFileManip
// has its own that 
// NOTE: do not change the 256 below to numBlocks! The ColorScheme system depends on
// this fixed size.
/*
We use pre-multiplied alpha.
That means that if the color is #ffffff, and alpha is 0.5, then the color,alpha,pcolor entry should be:
0xffffff,0.5,0x7f7f7f
*/
// The color is kept separately so that we can toggle various classes of objects - billboards, etc. -
// on and off someday.
static struct {
    const char *name;
    unsigned int color;	// r,g,b, NOT multiplied by alpha - input by the user
    float alpha;
    unsigned int pcolor;	// r,g,b, premultiplied by alpha (basically, unmultColor * alpha) - used (only) in mapping
    int txrX;   // column and row, from upper left, of 16x16 tiles in terrainExt.png, for top view of block
    int txrY;
    unsigned int flags;
} gBlockDefinitions[256]={	// IMPORTANT: do not change 256 size here. Crazy as it sounds, this is important: color schemes use this hard-wired size to copy stuff over.
	// Don't trust the premultiplied colors - these really are just placeholders, it's color * alpha that sets them when the program starts up.
    // name                    color     alpha   prem-clr  txX,Y  flags
    {"Air",                    0x000000, 0.000f, 0x000000, 13,14, BLF_NONE},	//00
    {"Stone",                  0x787878, 1.000f, 0x787878,  1, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//01
    {"Grass",                  0x90BB58, 1.000f, 0x78b34d,  0, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//02 3,0 side
    {"Dirt",                   0x8c6344, 1.000f, 0x8c6344,  2, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//03
    {"Cobblestone",            0x828282, 1.000f, 0x828282,  0, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//04
    {"Wooden Plank",           0x9f8150, 1.000f, 0x9f8150,  4, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//05
    {"Sapling",                0x7b9a29, 1.000f, 0x7b9a29, 15, 0, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//06
    {"Bedrock",                0x565656, 1.000f, 0x565656,  1, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//07
    {"Water",                  0x295dfe, 0.535f, 0x163288, 15,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRANSPARENT|BLF_HIDE_ON_MAP},	//08
    {"Stationary Water",       0x295dfe, 0.535f, 0x163288, /* same as above - change? TODOTODO */ 15,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRANSPARENT|BLF_HIDE_ON_MAP},	//09
    {"Lava",                   0xf56d00, 1.000f, 0xf56d00, 15,15, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER},	//0a
    {"Stationary Lava",        0xf56d00, 1.000f, 0xf56d00, /* same as above - change? TODOTODO */ 15,15, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER},	//0b
    {"Sand",                   0xe0d8a6, 1.000f, 0xe0d8a6,  2, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//0c/12
    {"Gravel",                 0x857b7b, 1.000f, 0x857b7b,  3, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//0d
    {"Gold Ore",               0xfcee4b, 1.000f, 0xfcee4b,  0, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//0e
    {"Iron Ore",               0xbc9980, 1.000f, 0xbc9980,  1, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//0f
    {"Coal Ore",               0x343434, 1.000f, 0x343434,  2, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//10
    {"Log",                    0xb1905a, 1.000f, 0xb1905a,  5, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUNK_PART|BLF_FENCE_NEIGHBOR},	//11/17
    {"Leaves",                 0x39ab27, 1.000f, 0x39ab27,  4, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_LEAF_PART},	//12
    {"Sponge",                 0xc7c743, 1.000f, 0xc7c743,  0, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//13
    {"Glass",                  0xc0f6fe, 0.500f, 0x607b7f,  1, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_HIDE_ON_MAP},	//14 - note that BLF_TRANSPARENT is not flagged, because glass is either fully on or off, not blended
    {"Lapis Lazuli Ore",       0x143880, 1.000f, 0x143880,  0,10, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//15
    {"Lapis Lazuli Block",     0x1b4ebb, 1.000f, 0x1b4ebb,  0, 9, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//16
    {"Dispenser",              0x6f6f6f, 1.000f, 0x6f6f6f, 14, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE},	//17 14,2 front, 13,2 sides
    {"Sandstone",              0xe0d8a6, 1.000f, 0xe0d8a6,  0,11, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//18 0,12 side, 0,13 bottom
    {"Note Block",             0x342017, 1.000f, 0x342017, 10, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//19 10,4 side
    {"Bed",                    0xff3333, 1.000f, 0xff3333,  6, 8, BLF_HALF|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//1a
    {"Powered Rail",           0xAB0301, 1.000f, 0xAB0301,  3,11, BLF_FLATTOP|BLF_BILLBOARD|BLF_3D_BIT|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//1b/27
    {"Detector Rail",          0xCD5E58, 1.000f, 0xCD5E58,  3,12, BLF_FLATTOP|BLF_BILLBOARD|BLF_3D_BIT|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},	//1c
    {"Sticky Piston",          0x719e60, 1.000f, 0x719e60, 12, 6, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//1d
    {"Cobweb",                 0xeeeeee, 1.000f, 0xeeeeee, 11, 0, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS},	//1e
    {"Tall Grass",             0x68a33d, 1.000f, 0x68a33d,  7, 2, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//1f/31
    {"Dead Bush",              0x946428, 1.000f, 0x946428,  7, 3, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//20/32
    {"Piston",                 0x95774b, 1.000f, 0x95774b, 12, 6, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//21
    {"Piston Head",            0x95774b, 1.000f, 0x95774b, 11, 6, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//22/34
    {"Wool",                   0xdcdcdc, 1.000f, 0xdcdcdc,  0, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//23 - gets converted to colors at end
    {"Block Moved By Piston",  0x000000, 1.000f, 0x000000, 11, 6, BLF_NONE},	//24 (36) - really, nothing...
    {"Dandelion",              0xD3DD05, 1.000f, 0xD3DD05, 13, 0, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//25
    {"Poppy",                  0xD81F1F, 1.000f, 0xD81F1F, 12, 0, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//26
    {"Brown Mushroom",         0xc19171, 1.000f, 0xc19171, 13, 1, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//27
    {"Red Mushroom",           0xfc5c5d, 1.000f, 0xfc5c5d, 12, 1, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//28
    {"Gold Block",             0xfef74e, 1.000f, 0xfef74e,  7, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//29
    {"Iron Block",             0xeeeeee, 1.000f, 0xeeeeee,  6, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//2a
    {"Double Stone Slab",      0xa6a6a6, 1.000f, 0xa6a6a6,  6, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//2b/43 - 5,0 side
    {"Slab",                   0xa5a5a5, 1.000f, 0xa5a5a5,  6, 0, BLF_HALF|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},	//2c/44 - 5,0 side
    {"Brick",                  0xa0807b, 1.000f, 0xa0807b,  7, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//2d 
    {"TNT",                    0xdb441a, 1.000f, 0xdb441a,  9, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE},	//2e 7,0 side, 9,0 under
    {"Bookshelf",              0x795a39, 1.000f, 0x795a39,  4, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//2f 3,2
    {"Mossy Stone",            0x627162, 1.000f, 0x627162,  4, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//30
    {"Obsidian",               0x1b1729, 1.000f, 0x1b1729,  5, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//31
    {"Torch",                  0xfcfc00, 1.000f, 0xfcfc00,  0, 5, BLF_MIDDLER|BLF_FLATSIDE|BLF_SMALL_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_EMITTER|BLF_DNE_FLUID},	//32/50 - should be BLF_EMITTER, flatten torches only if sides get flattened, too
    {"Fire",                   0xfca100, 1.000f, 0xfca100, /* somewhat bogus */ 15, 1, BLF_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_EMITTER|BLF_DNE_FLUID},	//33/51 - no billboard, sadly BLF_CUTOUTS
    {"Monster Spawner",        0x254254, 1.000f, 0x254254,  1, 4, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS},	//34 - TODO: not quite whole
    {"Wooden Stairs",          0x9e804f, 1.000f, 0x9e804f,  4, 0, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},	//35
    {"Chest",                  0xa06f23, 1.000f, 0xa06f23,  9, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE},	//36 (10,1) side; (11,1) front - TODO: in release it's not whole
    {"Redstone Wire",          0xd60000, 1.000f, 0xd60000,  4, 10, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},	//37
    {"Diamond Ore",            0x5decf5, 1.000f, 0x5decf5,  2, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//38
    {"Diamond Block",          0x7fe3df, 1.000f, 0x7fe3df,  8, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//39
    {"Crafting Table",         0x825432, 1.000f, 0x825432, 11, 2, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//3a - 12,3 side; 11,3 side2
    {"Crops",                  0x766615, 1.000f, 0x766615, 15, 5, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//3b
    {"Farmland",               0x40220b, 1.000f, 0x40220b,  6, 5, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},	//3c - 7,5 dry
    {"Furnace",                0x767677, 1.000f, 0x767677, 14, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//3d 13,2 side, 12,2 front
    {"Burning Furnace",        0x777676, 1.000f, 0x777676, 14, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//3e 13,2 side, 13,3 front
    {"Sign Post",              0x9f814f, 1.000f, 0x9f814f, /* bogus */ 4, 0, BLF_SMALL_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//3f (63)
    {"Wooden Door",            0x7e5d2d, 1.000f, 0x7e5d2d,  1, 5, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT}, // 40 1,6 bottom	//40 TODO: BLF_FLATSIDE?
    {"Ladder",                 0xaa8651, 1.000f, 0xaa8651,  3, 5, BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_DNE_FLUID},	//41
    {"Rail",                   0x686868, 1.000f, 0x686868,  0, 8, BLF_FLATTOP|BLF_BILLBOARD|BLF_3D_BIT|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//42 - TODO: doesn't do angled pieces, top to bottom edge
    {"Cobblestone Stairs",     0x818181, 1.000f, 0x818181,  0, 1, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},	//43 (67)
    {"Wall Sign",              0xa68a46, 1.000f, 0xa68a46, /* bogus */ 4, 0, BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},	//44
    {"Lever",                  0x8a6a3d, 1.000f, 0x8a6a3d,  0, 6, BLF_SMALL_MIDDLER|BLF_TRUE_GEOMETRY|BLF_FLATTOP|BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},	//45
    {"Stone Plate",            0xa4a4a4, 1.000f, 0xa4a4a4,  1, 0, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CONNECTS_REDSTONE},	//46 (70)
    {"Iron Door",              0xb2b2b2, 1.000f, 0xb2b2b2,  2, 5, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//47 (71) 2,6 bottom TODO BLF_FLATSIDE?
    {"Wooden Plate",           0x9d7f4e, 1.000f, 0x9d7f4e,  4, 0, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CONNECTS_REDSTONE},	//48
    {"Redstone Ore",           0x8f0303, 1.000f, 0x8f0303,  3, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//49
    {"Redstone Ore (glowing)", 0x900303, 1.000f, 0x900303,  3, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER|BLF_FENCE_NEIGHBOR},	//4a (74)
    {"Redstone Torch (off)",   0x560000, 1.000f, 0x560000,  3, 7, BLF_MIDDLER|BLF_FLATSIDE|BLF_SMALL_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},	//4b
    {"Redstone Torch (on)",    0xfd0000, 1.000f, 0xfd0000,  3, 6, BLF_MIDDLER|BLF_FLATSIDE|BLF_SMALL_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_EMITTER|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},	//4c should be BLF_EMITTER, but it makes the whole block glow
    {"Stone Button",           0xacacac, 1.000f, 0xacacac,  1, 0, BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},	//4d
    {"Snow",                   0xf0fafa, 1.000f, 0xf0fafa,  2, 4, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},	//4e
    {"Ice",                    0x7dacfe, 0.613f, 0x4d6a9c,  3, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRANSPARENT},	//4f
    {"Snow Block",             0xf1fafa, 1.000f, 0xf1fafa,  2, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//50 4,4 side
    {"Cactus",                 0x0D6118, 1.000f, 0x0D6118,  5, 4, BLF_ALMOST_WHOLE|BLF_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	//51 6,4 side - note: the cutouts are not used when "lesser" is off for rendering, but so it goes.
    {"Clay",                   0xa2a7b4, 1.000f, 0xa2a7b4,  8, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//52
    {"Sugar Cane",             0x72944e, 1.000f, 0x72944e,  9, 4, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//53
    {"Jukebox",                0x8a5a40, 1.000f, 0x8a5a40, 11, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//54 11,3 side
    {"Fence",                  0x9f814e, 1.000f, 0x9f814e,  4, 0, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},	//55
    {"Pumpkin",                0xc07615, 1.000f, 0xc07615,  6, 6, BLF_WHOLE|BLF_IMAGE_TEXTURE},	//56 6,7 side, 7,7 face
    {"Netherrack",             0x723a38, 1.000f, 0x723a38,  7, 6, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //57
    {"Soul Sand",              0x554134, 1.000f, 0x554134,  8, 6, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //58
    {"Glowstone",              0xf9d49c, 1.000f, 0xf9d49c,  9, 6, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER},   //59
    {"Portal",                 0x472272, 0.800f, 0x381b5b, 14, 0, BLF_PANE|BLF_IMAGE_TEXTURE|BLF_TRANSPARENT|BLF_EMITTER|BLF_DNE_FLUID},   //5a/90 - 0xd67fff unpremultiplied
    {"Jack-O-Lantern",         0xe9b416, 1.000f, 0xe9b416,  6, 6, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER},   //5b 6,7 side, 8,7 lit face
    {"Cake",                   0xfffdfd, 1.000f, 0xfffdfd,  9, 7, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},   //5c 10,7 side, 11,7 inside, 12,7 under - TODO: not really whole
    {"Repeater (off)",         0x560000, 1.000f, 0x560000,  3, 8, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_FLATTOP|BLF_DNE_FLUID|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},   //5d
    {"Repeater (on)",          0xee5555, 1.000f, 0xee5555,  3, 9, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_FLATTOP|BLF_DNE_FLUID|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},   //5e
	// in 1.7 locked chest was replaced by stained glass block
    //{"Locked Chest",           0xa06f23, 1.000f, 0xa06f23,  9, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE},   //5f/95 (10,1) side; (11,1) front
	{"Stained Glass",          0xeaeaea, 0.500f, 0x607b7f,  0,20, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_HIDE_ON_MAP|BLF_TRANSPARENT},	//5f/95 - note BLF_CUTOUTS is off, since all pixels are semitransparent
    {"Trapdoor",               0x886634, 1.000f, 0x886634,  4, 5, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_FLATSIDE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT},   //60/96 - tricky case: could be a flattop, or a flatside. For now, render it
    {"Hidden Silverfish",      0x787878, 1.000f, 0x787878,  1, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE},   //61
    {"Stone Brick",            0x797979, 1.000f, 0x797979,  6, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //62
    {"Huge Brown Mushroom",    0x654b39, 1.000f, 0x654b39, 14, 7, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //63
    {"Huge Red Mushroom",      0xa91b19, 1.000f, 0xa91b19, 13, 7, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //64
    {"Iron Bars",              0xa3a4a4, 1.000f, 0xa3a4a4,  5, 5, BLF_PANE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CUTOUTS},   //65
    {"Glass Pane",             0xc0f6fe, 0.500f, 0x607b7f,  1, 3, BLF_PANE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CUTOUTS|BLF_HIDE_ON_MAP},   //66
    {"Melon",                  0xaead27, 1.000f, 0xaead27,  9, 8, BLF_WHOLE|BLF_IMAGE_TEXTURE},   //67 (8,8) side
    {"Pumpkin Stem",           0xaa9715, 1.000f, 0xaa9715, 14,11, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},   //68/104 15,11 connected TODOTODO
    {"Melon Stem",             0xa89514, 1.000f, 0xa89514, 15, 6, BLF_FLATTOP|BLF_SMALL_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},   //69/105 15,7 connected TODOTODO
    {"Vines",                  0x39ab27, 1.000f, 0x39ab27, 15, 8, BLF_BILLBOARD|BLF_FLATSIDE|BLF_PANE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_LEAF_PART|BLF_DNE_FLUID},   //6a
    {"Fence Gate",             0xa88754, 1.000f, 0xa88754, /* bogus */ 4, 0, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_FENCE_NEIGHBOR|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //6b
    {"Brick Stairs",           0xa0807b, 1.000f, 0xa0807b,  7, 0, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //6c
    {"Stone Brick Stairs",     0x797979, 1.000f, 0x797979,  6, 3, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //6d
    {"Mycelium",               0x685d69, 1.000f, 0x685d69, 14, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //6e 13,4 side, 2,0 bottom
    {"Lily Pad",               0x0c5f14, 1.000f, 0x0c5f14, 12, 4, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},   //6f
    {"Nether Brick",           0x32171c, 1.000f, 0x32171c,  0,14, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //70/112
    {"Nether Brick Fence",     0x241316, 1.000f, 0x241316,  0,14, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //71
    {"Nether Brick Stairs",    0x32171c, 1.000f, 0x32171c,  0,14, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //72
    {"Nether Wart",            0x81080a, 1.000f, 0x81080a,  4,14, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS},   //73
    {"Enchantment Table",      0xa6701a, 1.000f, 0xa6701a,  6,10, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE},   //74 6,11 side, 7,11 under - TODO: not really whole TODO: NEED BETTER COLORS HERE ON DOWN (let Sean do it?)
    {"Brewing Stand",          0x77692e, 1.000f, 0x77692e, /* bogus */ 13, 9, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_CUTOUTS},   //75 13,8 base - no BLF_IMAGE_TEXTURE
    {"Cauldron",               0x3b3b3b, 1.000f, 0x3b3b3b, 10, 8, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //76 - 10,8 top (inside's better), 10,9 side, 11,9 feet TODO: not really whole
    {"End Portal",             0x0c0b0b, 0.7f, 0x0c0b0b,   /* bogus */ 14, 0, BLF_PANE|BLF_IMAGE_TEXTURE|BLF_TRANSPARENT},   //77 - not really whole, no real texture, make it a portal
    {"End Portal Frame",       0x3e6c60, 1.000f, 0x3e6c60, 14, 9, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //78 15,9 side, 15,10 bottom
    {"End Stone",              0xdadca6, 1.000f, 0xdadca6, 15,10, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //79
    {"Dragon Egg",             0x1b1729, 1.000f, 0x1b1729,  7,10, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},    //7A - not really whole
    {"Redstone Lamp (off)",    0x9F6D4D, 1.000f, 0x9F6D4D,  3,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   //7b
    {"Redstone Lamp (on)",     0xf9d49c, 1.000f, 0xf9d49c,  4,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR|BLF_EMITTER},    //7c
    {"Wooden Double Slab",     0x9f8150, 1.000f, 0x9f8150,  4, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},    //7d
    {"Wooden Slab",            0x9f8150, 1.000f, 0x9f8150,  4, 0, BLF_HALF|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //7e
    {"Cocoa Plant",            0xBE742D, 1.000f, 0xBE742D,  8,10, BLF_SMALL_MIDDLER|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},    //7f/127
    {"Sandstone Stairs",       0xe0d8a6, 1.000f, 0xe0d8a6,  0,11, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //80/128
    {"Emerald Ore",            0x900303, 1.000f, 0x900303, 11,10, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},    //81
    {"Ender Chest",            0x293A3C, 1.000f, 0x293A3C, 12,13, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},    //82 - don't really have tiles for this one, added to terrainExt.png
    {"Tripwire Hook",          0xC79F63, 1.000f, 0xC79F63, 12,10, BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},    //83 - decal
    {"Tripwire",               0x000000, 1.000f, 0x000000, 13,10, BLF_NONE},    //84 - sorta redwire decal, but really it should be invisible, so BLF_NONE. Color 0x8F8F8F
    // alternate {"Tripwire",               0x8F8F8F, 1.000f, 0x8F8F8F, 13,10, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_CUTOUTS},    //84 - sorta redwire decal, but really it should be invisible, so BLF_NONE. Color 0x8F8F8F
    {"Block of Emerald",       0x53D778, 1.000f, 0x53D778, 12,14, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},    //85 - should be at 9,1 for 1.3.X TODO!, but that wipes out the chest. This location given works with my special terrainExt.png file
    {"Spruce Wood Stairs",     0x785836, 1.000f, 0x785836,  6,12, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //86
    {"Birch Wood Stairs",      0xD7C185, 1.000f, 0xD7C185,  6,13, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //87
    {"Jungle Wood Stairs",     0xB1805C, 1.000f, 0xB1805C,  7,12, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //88
    {"Command Block",          0xD6A17E, 1.000f, 0xD6A17E,  8,11, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	//89 - TODO: is this really a fence neighbor?
    {"Beacon Block",           0x9CF2ED, 0.800f, 0x447370, 11,14, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_EMITTER},	//8A/138 - it's a whole block sorta, it doesn't attach to fences or block wires
    {"Cobblestone Wall",       0x828282, 1.000f, 0x828282,  0, 1, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},   //8B
    {"Flower Pot",             0x7C4536, 1.000f, 0x7C4536, 10,11, BLF_SMALL_MIDDLER|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},   //8C
    {"Carrots",                0x056B05, 1.000f, 0x056B05, 11,12, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//8d/141
    {"Potatoes",               0x00C01B, 1.000f, 0x00C01B, 15,12, BLF_BILLBOARD|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	//8e/142
	{"Wooden Button",          0x9f8150, 1.000f, 0x9f8150,  4, 0, BLF_FLATSIDE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},	//8f/143
    {"Head",			       0xcacaca, 1.000f, 0xcacaca,  6, 6, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	//90/144 - TODO! |BLF_TRUE_GEOMETRY|BLF_3D_BIT
	{"Anvil",                  0x404040, 1.000f, 0x404040,  7,13, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},    // 91/145 - NOTE: the top swatch is not used, the generic side swatch is
	// 1.6
	{"Trapped Chest",          0xa06f23, 1.000f, 0xa06f23,  9, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_CONNECTS_REDSTONE},	// 92/146 TODO from chest
	{"Weighted Pressure Plate (Light)",0xEFE140, 1.000f, 0xEFE140,  7, 1, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CONNECTS_REDSTONE},	// 93/147 gold
	{"Weighted Pressure Plate (Heavy)",0xD7D7D7, 1.000f, 0xD7D7D7,  6, 1, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CONNECTS_REDSTONE},	// 94/148 iron
	{"Redstone Comparator (off)",0xC5BAAD, 1.000f, 0xC5BAAD, 14,14, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_FLATTOP|BLF_DNE_FLUID|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},   // 95/149 TODO from repeater off
	{"Redstone Comparator (on)",0xD1B5AA, 1.000f, 0xD1B5AA, 15,14, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_FLATTOP|BLF_DNE_FLUID|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID|BLF_CONNECTS_REDSTONE},   // 96/150 TODO from repeater on
	{"Daylight Sensor",        0xBBA890, 1.000f, 0xBBA890,  6,15, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_FLATSIDE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_CONNECTS_REDSTONE},   // 97/151 TODO from trapdoor
	{"Block of Redstone",      0xA81E09, 1.000f, 0xA81E09, 14,15, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_EMITTER|BLF_FENCE_NEIGHBOR|BLF_CONNECTS_REDSTONE},	// 98/152
	{"Nether Quartz Ore",      0x7A5B57, 1.000f, 0x7A5B57,  8,17, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR}, // 99/153
	{"Hopper",                 0x363636, 1.000f, 0x363636, 13,15, BLF_ALMOST_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT},    // 9A/154 - note that 3d print version is simpler, no indentation, so it's thick enough
	{"Block of Quartz",        0xE0DDD7, 1.000f, 0xE0DDD7,  7,17, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// 9B/155
	{"Quartz Stairs",          0xE1DCD1, 1.000f, 0xE1DCD1,  7,17, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    // 9C/156
	{"Activator Rail",         0x880300, 1.000f, 0x880300, 10,17, BLF_FLATTOP|BLF_BILLBOARD|BLF_3D_BIT|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_DNE_FLUID},	// 9D/157
	{"Dropper",                0x6E6E6E, 1.000f, 0x6E6E6E, 14, 3, BLF_WHOLE|BLF_IMAGE_TEXTURE},	// 9E/158
	{"Stained Clay",           0xD4B7A3, 1.000f, 0xD4B7A3,  0,16, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// 9F/159
	{"Stained Glass Pane",     0xd0f9ff, 0.500f, 0x607b7f,  0,20, BLF_PANE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_HIDE_ON_MAP|BLF_TRANSPARENT},	// A0/160 - semitransparent, not a cutout like glass panes are
	{"Acacia/Dark Oak Leaves", 0x39ab27, 1.000f, 0x39ab27, 11,19, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_CUTOUTS|BLF_LEAF_PART},	//A1/161
	{"Acacia/Dark Oak Log",    0x6C655A, 1.000f, 0x6C655A, 13,19, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUNK_PART|BLF_FENCE_NEIGHBOR},	//A2/162
	{"Acacia Wood Stairs",     0xBA683B, 1.000f, 0xBA683B,  0,22, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //A3
	{"Dark Oak Wood Stairs",   0x492F17, 1.000f, 0x492F17,  1,22, BLF_STAIRS|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_3D_BIT_GLUE},    //A4/164
	{"Slime",                  0x787878, 0.500f, 0x787878,  3,22, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_TRANSPARENT|BLF_FENCE_NEIGHBOR},	// A5/165 - 1.8
	{"Barrier",                0x000000, 1.000f, 0x000000, 13,14, BLF_NONE},	// A6/166 - 1.8
	{"Iron Trapdoor",          0xC0C0C0, 1.000f, 0xC0C0C0,  2,22, BLF_MIDDLER|BLF_IMAGE_TEXTURE|BLF_ENTRANCE|BLF_FLATSIDE|BLF_CUTOUTS|BLF_TRUE_GEOMETRY|BLF_3D_BIT},	// A7/167 - 1.8
	{"Unused 168",             0x787878, 1.000f, 0x787878,  1, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// Stone
	{"Unused 169",             0x787878, 1.000f, 0x787878,  1, 0, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// Stone
	{"Hay",                    0xB5970C, 1.000f, 0xB5970C, 10,15, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// AA/170
	{"Carpet",                 0xEBEBEB, 1.000f, 0xEBEBEB,  0, 4, BLF_FLATTOP|BLF_IMAGE_TEXTURE|BLF_TRUE_GEOMETRY|BLF_3D_BIT|BLF_DNE_FLUID},	// AB/171
	{"Hardened Clay",          0x945A41, 1.000f, 0x945A41,  0,17, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// AC/172
	{"Block of Coal",          0x191919, 1.000f, 0x191919, 13,14, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},	// AD/173
	// 1.7
	{"Packed Ice",             0x7dacfe, 1.000f, 0x4d6a9c, 12,17, BLF_WHOLE|BLF_IMAGE_TEXTURE},	// AE/174 - like ice, but not transparent
	{"Large Flower",           0x67873F, 1.000f, 0x67873F,  0,18, BLF_FLATTOP|BLF_BILLBOARD|BLF_CUTOUTS|BLF_IMAGE_TEXTURE|BLF_DNE_FLUID},	// AF/175 - note color is used to multiply grayscale textures, so don't change it
// for simplicity for mapping, wool gets converted to its colors when read in, and made separate blocks - in this way, it shows up on the map as different colors easily
	{"White Wool",             0xDDDDDD, 1.000f, 0xDDDDDD,  0, 4, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},   // admittedly a repeat
    {"Orange Wool",            0xEA8037, 1.000f, 0xEA8037,  2,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Magenta Wool",           0xBF49CA, 1.000f, 0xBF49CA,  2,12, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Light Blue Wool",        0x6689D3, 1.000f, 0x6689D3,  2,11, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Yellow Wool",            0xC1B41C, 1.000f, 0xC1B41C,  2,10, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Lime Wool",              0x3ABD2E, 1.000f, 0x3ABD2E,  2, 9, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Pink Wool",              0xD9829A, 1.000f, 0xD9829A,  2, 8, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Gray Wool",              0x434343, 1.000f, 0x434343,  2, 7, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Light Gray Wool",        0x9DA4A4, 1.000f, 0x9DA4A4,  1,14, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Cyan Wool",              0x277494, 1.000f, 0x277494,  1,13, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Purple Wool",            0x8031C6, 1.000f, 0x8031C6,  1,12, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Blue Wool",              0x263399, 1.000f, 0x263399,  1,11, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Brown Wool",             0x56331B, 1.000f, 0x56331B,  1,10, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Green Wool",             0x374D18, 1.000f, 0x374D18,  1, 9, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
    {"Red Wool",               0xA32C28, 1.000f, 0xA32C28,  1, 8, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
	{"Black Wool",             0x1B1717, 1.000f, 0x1B1717,  1, 7, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR},
	// special
	{"Unknown Block",          0x565656, 1.000f, 0x565656,  1, 1, BLF_WHOLE|BLF_IMAGE_TEXTURE|BLF_FENCE_NEIGHBOR}	// same as bedrock
// someday may want to add entries for stained clay and carpet TODO
};

//unsigned int gWoolColors[16]={
//    0xDDDDDD, //     0x0	 Regular wool (white)
//    0xEA8037, //	 0x1	 Orange
//    0xBF49CA, //	 0x2	 Magenta
//    0x6689D3, //	 0x3	 Light Blue
//    0xC1B41C, //	 0x4	 Yellow
//    0x3ABD2E, //	 0x5	 Lime
//    0xD9829A, //	 0x6	 Pink
//    0x434343, //	 0x7	 Gray
//    0x9DA4A4, //	 0x8	 Light Gray
//    0x277494, //	 0x9	 Cyan
//    0x8031C6, //	 0xA	 Purple
//    0x263399, //	 0xB	 Blue
//    0x56331B, //	 0xC	 Brown
//    0x374D18, //	 0xD	 Green
//    0xA32C28, //	 0xE	 Red
//    0x1B1717  //	 0xF	 Black
//}


enum block_types {
    BLOCK_AIR = 0x00,
    BLOCK_STONE = 0x01,
    BLOCK_GRASS = 0x02,
    BLOCK_DIRT= 0x03,
    BLOCK_COBBLESTONE = 0x04,
    BLOCK_WOODEN_PLANKS = 0x05,
    BLOCK_SAPLING= 0x06,
    BLOCK_BEDROCK = 0x07,
    BLOCK_WATER = 0x08,
    BLOCK_STATIONARY_WATER = 0x09,
    BLOCK_LAVA = 0x0a,
    BLOCK_STATIONARY_LAVA = 0x0b,
	BLOCK_SAND = 0x0c,
    BLOCK_LOG = 0x11,
    BLOCK_LEAVES = 0x12,
    BLOCK_GLASS = 0x14,
    BLOCK_DISPENSER = 0x17,
    BLOCK_SANDSTONE = 0x18,
    BLOCK_NOTEBLOCK = 0x19,
    BLOCK_BED = 0x1a,
    BLOCK_POWERED_RAIL = 0x1b,
    BLOCK_DETECTOR_RAIL = 0x1c,
    BLOCK_STICKY_PISTON = 0x1d,
    BLOCK_COBWEB = 0x1e,
    BLOCK_TALL_GRASS = 0x1f,
    BLOCK_DEAD_BUSH = 0x20,
	BLOCK_PISTON = 0x21,
	BLOCK_PISTON_HEAD = 0x22,
    BLOCK_DANDELION = 0x25,
    BLOCK_POPPY = 0x26,
    BLOCK_BROWN_MUSHROOM = 0x27,
    BLOCK_RED_MUSHROOM = 0x28,
    BLOCK_BOOKSHELF = 0x2f,
    BLOCK_WOOL = 0x23,
    BLOCK_DOUBLE_STONE_SLAB = 0x2b,
    BLOCK_STONE_SLAB = 0x2c,
    BLOCK_BRICK = 0x2d,
    BLOCK_TNT = 0x2e,
	BLOCK_MOSS_STONE = 0x30,
	BLOCK_OBSIDIAN = 0x31,
    BLOCK_TORCH = 0x32,
    BLOCK_FIRE = 0x33,
    BLOCK_MONSTER_SPAWNER = 0x34,
	BLOCK_OAK_WOOD_STAIRS = 0x35,
    BLOCK_CHEST = 0x36,
    BLOCK_REDSTONE_WIRE = 0x37,
    BLOCK_CRAFTING_TABLE = 0x3a,
    BLOCK_CROPS = 0x3b,
    BLOCK_FARMLAND = 0x3c,
    BLOCK_FURNACE = 0x3d,
    BLOCK_BURNING_FURNACE = 0x3e,
	BLOCK_SIGN_POST = 0x3f,
    BLOCK_WOODEN_DOOR = 0x40,
    BLOCK_LADDER = 0x41,
    BLOCK_RAIL = 0x42,
	BLOCK_COBBLESTONE_STAIRS = 0x43,
    BLOCK_WALL_SIGN = 0x44,
    BLOCK_LEVER = 0x45,
    BLOCK_STONE_PRESSURE_PLATE = 0x46,
    BLOCK_IRON_DOOR = 0x47,
    BLOCK_WOODEN_PRESSURE_PLATE = 0x48,
    BLOCK_REDSTONE_TORCH_OFF = 0x4b,
    BLOCK_REDSTONE_TORCH_ON = 0x4c,
    BLOCK_STONE_BUTTON = 0x4d,
    BLOCK_SNOW = 0x4e,          // this is just the snow covering the ground
    BLOCK_SNOW_BLOCK = 0x50,    // confusing, eh?
    BLOCK_CACTUS = 0x51,
    BLOCK_SUGAR_CANE = 0x53,
    BLOCK_JUKEBOX = 0x54,
	BLOCK_FENCE = 0x55,
    BLOCK_PUMPKIN = 0x56,
    BLOCK_JACK_O_LANTERN = 0x5b,
    BLOCK_CAKE = 0x5c,
    BLOCK_REDSTONE_REPEATER_OFF = 0x5d,
    BLOCK_REDSTONE_REPEATER_ON = 0x5e,
    BLOCK_STAINED_GLASS = 0x5f,	// was BLOCK_LOCKED_CHEST, which went away in 1.7
    BLOCK_TRAPDOOR = 0x60,
    BLOCK_HIDDEN_SILVERFISH = 0x61,
    BLOCK_STONE_BRICKS = 0x62,
	BLOCK_HUGE_BROWN_MUSHROOM = 0x63,
	BLOCK_HUGE_RED_MUSHROOM = 0x64,
    BLOCK_IRON_BARS = 0x65,
    BLOCK_GLASS_PANE = 0x66,
    BLOCK_MELON = 0x67,
    BLOCK_PUMPKIN_STEM = 0x68,
    BLOCK_MELON_STEM = 0x69,
    BLOCK_VINES = 0x6a,
	BLOCK_FENCE_GATE = 0x6b,
	BLOCK_BRICK_STAIRS = 0x6c,
	BLOCK_STONE_BRICK_STAIRS = 0x6d,
    BLOCK_MYCELIUM = 0x6e,
    BLOCK_LILY_PAD = 0x6f,
	BLOCK_NETHER_BRICKS = 0x70,
    BLOCK_NETHER_BRICK_FENCE = 0x71,
	BLOCK_NETHER_BRICK_STAIRS = 0x72,
    BLOCK_NETHER_WART = 0x73,
    BLOCK_ENCHANTMENT_TABLE = 0x74,
    BLOCK_BREWING_STAND = 0x75,
    BLOCK_CAULDRON = 0x76,
    BLOCK_END_PORTAL_FRAME = 0x78,
    BLOCK_DRAGON_EGG = 0x7a,
	BLOCK_WOODEN_DOUBLE_SLAB = 0x7d,
	BLOCK_WOODEN_SLAB = 0x7e,
	BLOCK_COCOA_PLANT = 0x7f,
	BLOCK_SANDSTONE_STAIRS = 0x80,
	BLOCK_ENDER_CHEST = 0x82,
	BLOCK_TRIPWIRE_HOOK = 0x83,
	BLOCK_TRIPWIRE = 0x84,
	BLOCK_SPRUCE_WOOD_STAIRS = 0x86,
	BLOCK_BIRCH_WOOD_STAIRS = 0x87,
	BLOCK_JUNGLE_WOOD_STAIRS = 0x88,
	BLOCK_BEACON = 0x8A,
	BLOCK_COBBLESTONE_WALL = 0x8B,
	BLOCK_FLOWER_POT = 0x8C,
	BLOCK_CARROTS = 0x8D,
	BLOCK_POTATOES = 0x8E,
	BLOCK_WOODEN_BUTTON = 0x8F,
	BLOCK_HEAD = 0x90,
	BLOCK_ANVIL = 0x91,
	// 1.5.2
	BLOCK_TRAPPED_CHEST = 0x92,
	// TODO
	BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT = 0x93,
	BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY = 0x94,
	BLOCK_REDSTONE_COMPARATOR_INACTIVE = 0x95,	// TODO line 1916
	BLOCK_REDSTONE_COMPARATOR_ACTIVE = 0x96,
	BLOCK_DAYLIGHT_SENSOR = 0x97,	// TODO line 1916, make like trapdoor?
	BLOCK_REDSTONE_BLOCK = 0x98,
	BLOCK_NETHER_QUARTZ_ORE = 0x99,
	BLOCK_HOPPER = 0x9A,
	BLOCK_QUARTZ_BLOCK = 0x9B,
	BLOCK_QUARTZ_STAIRS = 0x9C,
	BLOCK_ACTIVATOR_RAIL = 0x9D,
	BLOCK_DROPPER = 0x9E,
	// 1.6 & 1.7.2
	BLOCK_STAINED_CLAY = 0x9F,
	BLOCK_STAINED_GLASS_PANE = 0xA0,
	BLOCK_AD_LEAVES = 0xA1,
	BLOCK_AD_LOG = 0xA2,
	BLOCK_ACACIA_WOOD_STAIRS = 0xA3,
	BLOCK_DARK_OAK_WOOD_STAIRS = 0xA4,
	BLOCK_HAY = 0xAA,
	BLOCK_CARPET = 0xAB,
	BLOCK_HARDENED_CLAY = 0xAC,
	BLOCK_COAL_BLOCK = 0xAD,
	// 1.7.2
	BLOCK_DOUBLE_FLOWER = 0xAF,
	// 1.8
	BLOCK_SLIME = 0xA5,
	BLOCK_BARRIER = 0xA6,
	BLOCK_IRON_TRAPDOOR = 0xA7,

	// colored wool at end - special internal IDs, so that the mapping program part can show wool in different colors
    BLOCK_WHITE_WOOL = (NUM_BLOCKS-16-1),
    BLOCK_BLACK_WOOL = (NUM_BLOCKS-1-1),
	BLOCK_UNKNOWN = (NUM_BLOCKS-1)
};

#endif
