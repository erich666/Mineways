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
#include <string>

typedef unsigned int    UINT;


#define MAP_MAX_HEIGHT 255
#define MAP_MAX_HEIGHT_STRING L"255"

// sea level (it was 63 before 1.0 release)
#define SEA_LEVEL 62
#define SEA_LEVEL_STRING L"62"


// turn on to dump stuff to screen (for Mac debugging, mostly)
extern bool gDebug;

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

// https://www.shapeways.com/materials - I left out gold and platinum; 14 is about the maximum usable length, after that the dialog scrolls, which is confusing
#define MTL_COST_TABLE_SIZE 14

#define PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE 0
#define PRINT_MATERIAL_FULL_COLOR_SANDSTONE 1
// last one can be set to whatever you like
#define PRINT_MATERIAL_CUSTOM_MATERIAL 13
#define PRINT_MATERIAL_FCS_SCULPTEO (PRINT_MATERIAL_CUSTOM_MATERIAL-1)

typedef struct MaterialCost {
    // lame on my part: we really shouldn't be using wide characters in the dialog TODO
    wchar_t *wname;
    char *name;
	wchar_t *currency;
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
    float costDiscountDensityLevel; // model has to be at least this dense - DEPRECATED, not used
    float costDiscountCCMLevel; // first X cubic centimeters are at normal price; above this is half price - DEPRECATED, not used
    // cost minimum is a guess, doesn't include handling TODO
    float costMinimum;
    // see https://www.shapeways.com/blog/archives/18174-how-much-does-it-cost-when-you-3d-print-a-thousand-different-parts-all-at-once.html
    float costPerMachineCC;   // cost per cubic centimeter of "machine space" in the machine, for white & flexible etc.
    float maxSize[3];
} MaterialCost;

extern MaterialCost gMtlCostTable[];

#define MODELS_UNITS_TABLE_SIZE 4

#define UNITS_METER 0
#define UNITS_CENTIMETER 1
#define UNITS_MILLIMETER 2
#define UNITS_INCHES 3

int unitIndex; // initialize to UNITS_METER

typedef struct UnitType {
    wchar_t *wname;
    char *name;
    float unitsPerMeter;
} UnitType;

extern UnitType gUnitTypeTable[];


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

// For 3d Printing, make the model as small as the material chosen allows (colored sandstone currently assumed)
#define EXPT_MAKE_SMALL				    0x0100

// These flags are useful as we sometimes want to check for any of them (e.g. for whether
// to make parts).
// Fill bubbles, hollow spots inside a model not visible from the outside
// This can be useful for models for viewing if you never plan to go inside the structure
#define EXPT_FILL_BUBBLES			    0x0200
// Join any two cubes sharing an edge and having different groups (i.e. are different parts)
#define EXPT_CONNECT_PARTS			    0x0400
// Join any two cubes sharing an edge, period. Aggressive, do only if needed by 3D printer.
// http://www.shapeways.com/tutorials/things-to-keep-in-mind says it's needed, but uploading
// two separate cubes sharing an edge works fine with Shapeways. The newer
// http://www.shapeways.com/tutorials/fixing-non-manifold-models doesn't mention shared edges.
#define EXPT_CONNECT_ALL_EDGES          0x0800
// Join any two cubes *in different meta-groups* that share corner tips
#define EXPT_CONNECT_CORNER_TIPS		0x1000
// Delete objects < 16 blocks or all tree that are not at the ground level
#define EXPT_DELETE_FLOATING_OBJECTS	0x2000
// Hollow out the bottom of the model (TODO: could add a base, with a hole in it)
#define EXPT_HOLLOW_BOTTOM				0x4000
// Aggressively hollow out an area, carving from the bottom around tunnels.
// The danger is that holes can be created which may not get emptied.
#define EXPT_SUPER_HOLLOW_BOTTOM		0x8000
// Allow BLF_ENTRANCE blocks to act solid when forming air groups. This allows
// insides of buildings to get sealed off and filled.
#define EXPT_SEAL_ENTRANCES				0x10000
// Tunnels on the sides and bottom of the solid box are considered bubbles and filled with glass
// For now, make it the same thing as sealing entrances - I'm not sure it needs yet another checkbox
#define EXPT_SEAL_SIDE_TUNNELS			0x20000

// should each block type be given a separate material?
// In OBJ we have two ways to group: there are groups, each group defining an object. We allow
// two kinds of grouping:
// * One group for each object type. Example: all acacia logs are an object type, so can be modified as a whole.
// * One group for each block, with each block fully formed with all its side. Example: each acacia block is an object.
// There are materials, and we share in three ways:
// * One material for all objects. This is a find choice for 3D printing, where it doesn't matter.
// * One material for each object type. So water might be transparent, lava not.
// * One material for each object.

// when exporting objects with OBJ, do we want to export groups at all? For rendering, the default is on; 3D printing, off.
#define EXPT_OUTPUT_OBJ_GROUPS				0x40000
// do we want to export by block? That is, every individual Minecraft block is in its own group? Normally off.
#define EXPT_GROUP_BY_BLOCK					0x80000

// when exporting objects with OBJ, do we want multiple materials?
// This is the norm, but for things like Blender and Maya, a single material can be less work.
// A single material for all objects has the EXPT_OUTPUT_OBJ_MTL_PER_TYPE off entirely.
// To have a material for each object type, both of the flags that follow are on. On by default.
#define EXPT_OUTPUT_OBJ_MULTIPLE_MTLS		0x100000
// If true (and EXPT_OUTPUT_OBJ_GROUPS is on), then there's a separate material for each type of block.
#define EXPT_OUTPUT_OBJ_MTL_PER_TYPE		0x200000
// If true, output each individual block as its own group
#define EXPT_OUTPUT_EACH_BLOCK_A_GROUP		0x400000

// Special modifiers for material output for OBJ, only:
// when exporting textures with OBJ, output a uniform material: Ka and Kd set to white (G3D likes this)
#define EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL	0x800000
// when exporting materials with OBJ, output the extra values (G3D likes this)
#define EXPT_OUTPUT_OBJ_FULL_MATERIAL		0x1000000

// relative or absolute coordinates for OBJ
#define EXPT_OUTPUT_OBJ_REL_COORDINATES		0x2000000

// use biomes for export
#define EXPT_BIOME							0x4000000
// Sketchfab export (to export only RGBA texture)
#define EXPT_SKFB							0x8000000

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

typedef struct PublishSkfbData
{
    // Sketchfab
    char skfbApiToken[33];
    char skfbName[49];
    char skfbDescription[1025];
    char skfbTags[256];
    bool skfbPrivate;
    char skfbPassword[25];
    bool skfbDraft;
    std::string skfbFilePath;
} PublishSkfbData;

typedef struct ExportFileData
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
    UINT chkLeavesSolid;    // should tree leaves be output as solid or semitransparent? Normally false, i.e., semitransparent for rendering. Doesn't affect 3D printing
    UINT chkBlockFacesAtBorders;    // should block faces be generated at the borders of the export? False for rendering. Doesn't affect 3D printing.
    UINT chkBiome;
	UINT chkCompositeOverlay;

    UINT chkFillBubbles;
    UINT chkSealEntrances;
    UINT chkSealSideTunnels;
    UINT chkConnectParts;
    UINT chkConnectCornerTips;
    UINT chkConnectAllEdges;
    UINT chkDeleteFloaters;
    UINT chkHollow[FILE_TYPE_TOTAL];
    UINT chkSuperHollow[FILE_TYPE_TOTAL];
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
	UINT chkIndividualBlocks;
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
    int worldType;          // what world we're looking at: HELL, ENDER, etc., and other option toggles
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

// number of official Minecraft blocks (take highest valid block ID and add 1)
#define NUM_BLOCKS_STANDARD 218
// number of blocks we want to show on the map (includes the unknown one)
#define NUM_BLOCKS_MAP (NUM_BLOCKS_STANDARD+1)
// number of blocks with entries in block info table - now that 255 is used, we need this
#define NUM_BLOCKS_DEFINED 256
// Total number of blocks. We used to include 16 wool blocks, but have now deleted these.
// NUM_BLOCKS_MAP could probably be turned into NUM_BLOCKS at this point, but keeping both for now
// until the code settles down.
// NOTE! if we ever go past 256, search for 256 in code and start fixing!
#define NUM_BLOCKS NUM_BLOCKS_MAP

// number of texture swatches
#define NUM_SWATCHES (NUM_BLOCKS+256)

// absolute max the 2x2 * 16x16 space of swatches could have (without borders)
#define NUM_MAX_SWATCHES (4*16*16)



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
// actually, now everything has this property, so it's eliminated
//#define BLF_IMAGE_TEXTURE   0x10000
// this object emits light
#define BLF_EMITTER         0x10000
// this object attaches to fences; note that fences do not have this property themselves, so that nether & regular fence won't attach
#define BLF_FENCE_NEIGHBOR	0x20000
// this object outputs its true geometry (not just a block) for rendering
#define BLF_TRUE_GEOMETRY	0x40000
// this object outputs its special non-full-block geometry for 3D printing, if the printer can glue together the bits.
// Slightly different than TRUE_GEOMETRY in that things that are just too thin don't have this bit set.
#define BLF_3D_BIT          0x80000
// this object is a 3D bit, and this bit is set if it can actually glue horizontal neighboring blocks together
// - not really used. TODO - may want to use this to decide whether objects should be grouped together or whatever.
#define BLF_3D_BIT_GLUE     0x100000
// set if the block does not affect fluid height
#define BLF_DNE_FLUID		0x200000
// set if the block connects to redstone - do only if there's no orientation to the block, e.g. repeaters attach only on two sides, so don't have this flag
#define BLF_CONNECTS_REDSTONE		0x400000
// has no geometry, on purpose
#define BLF_NONE			0x800000
// is an offset tile, rendered separately: rails, vines, lily pad, redstone, ladder (someday, tripwire? TODO)
#define BLF_OFFSET			0x1000000

#define BLF_CLASS_SET   (BLF_NONE|BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF|BLF_MIDDLER|BLF_BILLBOARD|BLF_PANE|BLF_FLATTOP|BLF_FLATSIDE|BLF_SMALL_MIDDLER|BLF_SMALL_BILLBOARD)

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
typedef struct BlockDefinition {
    const char *name;
    unsigned int read_color;	// r,g,b, locked in place, never written to: used for initial setting of color
    float read_alpha;
    unsigned int color;	// r,g,b, NOT multiplied by alpha - input by the user, result of color scheme application
    unsigned int pcolor;	// r,g,b, premultiplied by alpha (basically, unmultColor * alpha) - used (only) in mapping
    float alpha;
    int txrX;   // column and row, from upper left, of 16x16 tiles in terrainExt.png, for TOP view of block
    int txrY;
    unsigned int flags;
} BlockDefinition;

extern BlockDefinition gBlockDefinitions[];

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
    BLOCK_SPONGE = 0x13,
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
	BLOCK_GLOWSTONE = 0x59,
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
    BLOCK_END_STONE = 0x79,
    BLOCK_DRAGON_EGG = 0x7a,
    BLOCK_WOODEN_DOUBLE_SLAB = 0x7d,
    BLOCK_WOODEN_SLAB = 0x7e,
    BLOCK_COCOA_PLANT = 0x7f,
    BLOCK_SANDSTONE_STAIRS = 0x80,
    BLOCK_ENDER_CHEST = 0x82,
    BLOCK_TRIPWIRE_HOOK = 131,
    BLOCK_TRIPWIRE = 132,
    BLOCK_SPRUCE_WOOD_STAIRS = 134,
    BLOCK_BIRCH_WOOD_STAIRS = 0x87,
    BLOCK_JUNGLE_WOOD_STAIRS = 0x88,
    BLOCK_COMMAND_BLOCK = 0x89,
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
    BLOCK_REDSTONE_COMPARATOR = 0x95,
    BLOCK_REDSTONE_COMPARATOR_DEPRECATED = 0x96,
    BLOCK_DAYLIGHT_SENSOR = 0x97,	// TODO line 1916, make like trapdoor?
    BLOCK_REDSTONE_BLOCK = 0x98,
    BLOCK_NETHER_QUARTZ_ORE = 0x99,
    BLOCK_HOPPER = 0x9A,
    BLOCK_QUARTZ_BLOCK = 0x9B,
    BLOCK_QUARTZ_STAIRS = 0x9C,
    BLOCK_ACTIVATOR_RAIL = 0x9D,
    BLOCK_DROPPER = 0x9E,
    // 1.6 & 1.7.2
    BLOCK_STAINED_CLAY = 159,	// now called hardened clay
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
    BLOCK_PRISMARINE = 0xA8,
    BLOCK_SEA_LANTERN = 0xA9,
    BLOCK_STANDING_BANNER = 0xB0,
    BLOCK_WALL_BANNER = 0xB1,
    BLOCK_INVERTED_DAYLIGHT_SENSOR = 0xB2,
    BLOCK_RED_SANDSTONE = 0xB3,
    BLOCK_RED_SANDSTONE_STAIRS = 0xB4,
    BLOCK_DOUBLE_RED_SANDSTONE_SLAB = 0xB5,
    BLOCK_RED_SANDSTONE_SLAB = 0xB6,
    BLOCK_SPRUCE_FENCE_GATE = 0xB7,
    BLOCK_BIRCH_FENCE_GATE = 0xB8,
    BLOCK_JUNGLE_FENCE_GATE = 0xB9,
    BLOCK_DARK_OAK_FENCE_GATE = 0xBA,
    BLOCK_ACACIA_FENCE_GATE = 0xBB,
    BLOCK_SPRUCE_FENCE = 0xBC,
    BLOCK_BIRCH_FENCE = 0xBD,
    BLOCK_JUNGLE_FENCE = 0xBE,
    BLOCK_DARK_OAK_FENCE = 0xBF,
    BLOCK_ACACIA_FENCE = 0xC0,
    BLOCK_SPRUCE_DOOR = 0xC1,
    BLOCK_BIRCH_DOOR = 0xC2,
    BLOCK_JUNGLE_DOOR = 0xC3,
    BLOCK_ACACIA_DOOR = 0xC4,
    BLOCK_DARK_OAK_DOOR = 0xC5,
    BLOCK_END_ROD = 0xC6,
    BLOCK_CHORUS_PLANT = 0xC7,
    BLOCK_CHORUS_FLOWER = 0xC8,
    BLOCK_PURPUR_BLOCK = 0xC9,
    BLOCK_PURPUR_PILLAR = 0xCA,
    BLOCK_PURPUR_STAIRS = 0xCB,
    BLOCK_PURPUR_DOUBLE_SLAB = 0xCC,
    BLOCK_PURPUR_SLAB = 0xCD,
    BLOCK_END_BRICKS = 0xCE,
    BLOCK_BEETROOT_SEEDS = 0xCF,
    BLOCK_GRASS_PATH = 0xD0,
    BLOCK_END_GATEWAY = 0xD1,
    BLOCK_REPEATING_COMMAND_BLOCK = 0xD2,
    BLOCK_CHAIN_COMMAND_BLOCK = 0xD3,
    BLOCK_FROSTED_ICE = 0xD4,
	BLOCK_MAGMA_BLOCK = 0xD5,
	BLOCK_NETHER_WART_BLOCK = 0xD6,
	BLOCK_NETHER_BRICK = 0xD7,
	BLOCK_BONE_BLOCK = 0xD8,
	BLOCK_STRUCTURE_VOID = 0xD9,

	BLOCK_FAKE = 0xFE,	// I hope we never get this far

    BLOCK_STRUCTURE_BLOCK = 0xFF,

    BLOCK_UNKNOWN = (NUM_BLOCKS_STANDARD),
};

#endif
