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

#include <stdlib.h>
#include <string>

typedef unsigned int    UINT;


#define INIT_MAP_MAX_HEIGHT 255
#define ABSOLUTE_MAX_MAP_HEIGHT 319
#define ABSOLUTE_MIN_MAP_HEIGHT -64

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
    const wchar_t* wname;
    const char* name;
    wchar_t* currency;
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

typedef struct UnitType {
    wchar_t* wname;
    char* name;
    float unitsPerMeter;
} UnitType;

extern UnitType gUnitTypeTable[];

// west/bottom/north/east/top/south
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
#define EXPT_DEBUG_SHOW_GROUPS			0x00000001
// DEBUG: output welds as lava - TODO
#define EXPT_DEBUG_SHOW_WELDS			0x00000002

// Output material file
#define EXPT_OUTPUT_MATERIALS		    0x00000004
// Model is meant to be watertight, e.g. for 3D printing
#define EXPT_3DPRINT				    0x00000008
// Export a noisy color texture instead of simple solid materials
#define EXPT_OUTPUT_TEXTURE_SWATCHES    0x00000010
// Export a true block texture instead of simple solid materials
#define EXPT_OUTPUT_TEXTURE_IMAGES	    0x00000020
// Export separate textures, one per tile
#define EXPT_OUTPUT_SEPARATE_TEXTURE_TILES 0x00000040
#define EXPT_OUTPUT_TEXTURE (EXPT_OUTPUT_TEXTURE_SWATCHES|EXPT_OUTPUT_TEXTURE_IMAGES|EXPT_OUTPUT_SEPARATE_TEXTURE_TILES)
#define EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES (EXPT_OUTPUT_TEXTURE_IMAGES|EXPT_OUTPUT_SEPARATE_TEXTURE_TILES)

// These flags are useful as we sometimes want to check for any of them (e.g. for whether
// to make parts).
// Fill bubbles, hollow spots inside a model not visible from the outside
// This can be useful for models for viewing if you never plan to go inside the structure
#define EXPT_FILL_BUBBLES			    0x00000080
// Join any two cubes sharing an edge and having different groups (i.e. are different parts)
#define EXPT_CONNECT_PARTS			    0x00000100
// Join any two cubes sharing an edge, period. Aggressive, do only if needed by 3D printer.
// http://www.shapeways.com/tutorials/things-to-keep-in-mind says it's needed, but uploading
// two separate cubes sharing an edge works fine with Shapeways. The newer
// http://www.shapeways.com/tutorials/fixing-non-manifold-models doesn't mention shared edges.
#define EXPT_CONNECT_ALL_EDGES          0x00000200
// Join any two cubes *in different meta-groups* that share corner tips
#define EXPT_CONNECT_CORNER_TIPS		0x00000400
// Delete objects < 16 blocks or all tree that are not at the ground level
#define EXPT_DELETE_FLOATING_OBJECTS	0x00000800
// Hollow out the bottom of the model (TODO: could add a base, with a hole in it)
#define EXPT_HOLLOW_BOTTOM				0x00001000
// Aggressively hollow out an area, carving from the bottom around tunnels.
// The danger is that holes can be created which may not get emptied.
#define EXPT_SUPER_HOLLOW_BOTTOM		0x00002000
// Allow BLF_ENTRANCE blocks to act solid when forming air groups. This allows
// insides of buildings to get sealed off and filled.
#define EXPT_SEAL_ENTRANCES				0x00004000
// Tunnels on the sides and bottom of the solid box are considered bubbles and filled with glass
// For now, make it the same thing as sealing entrances - I'm not sure it needs yet another checkbox
#define EXPT_SEAL_SIDE_TUNNELS			0x00008000

// should each block type be given a separate material?
// In OBJ we have two ways to group: there are groups, each group defining an object. We allow
// two kinds of grouping:
// * One group for each object type. Example: all acacia logs are an object type, so can be modified as a whole.
// * One group for each block, with each block fully formed with all its side. Example: each acacia block is an object.
// There are materials, and we share in different ways:
// * One material for all objects. This is a find choice for 3D printing, where it doesn't matter.
// * One material for each object type. So water might be transparent, lava not.

// This logic is confusing, so here are some examples:
// No OBJ export boxes checked (ignore the G3D box for all of these): no group output, single material.
// Check "Export separate types": all polygons are grouped by type (Grass_Block, Sand, Dirt), single material.
// Check "Export separate types" and "Separate materials/blocks" (the default): polygons are grouped by type, each type has a material.
// Check "Export individual blocks": same as above, polygon are grouped by type, each type has a material. However, geometrically, every block has all its faces output.
// Check "Export individual blocks" and "Separate materials/blocks": each Minecraft block is in its own group, each type has a material, blocks each use the relevant material.

// when exporting objects with OBJ, do we want to export groups at all? For rendering, the default is on; 3D printing, off. "Export separate types"
#define EXPT_OUTPUT_OBJ_SEPARATE_TYPES		0x00010000
// do we want to export individual blocks? That is, every individual Minecraft block is in its own group? Normally off.
#define EXPT_INDIVIDUAL_BLOCKS				0x00020000

// When exporting objects with OBJ, do we want multiple materials, i.e., one per family or type of block?
// This is the norm, but for things like Blender and Maya, a single material can be less work when importing and hand-editing.
// A single material for all objects has the EXPT_OUTPUT_OBJ_MTL_PER_TYPE off entirely.
// To have a material for each object *type*, both of the flags that follow are on. On by default.
#define EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK	0x00040000
// If true (and EXPT_OUTPUT_OBJ_SEPARATE_TYPES is on), then there's a separate material for each *type* of block.
#define EXPT_OUTPUT_OBJ_MTL_PER_TYPE		0x00080000
// If true, output each individual block as its own group
#define EXPT_OUTPUT_EACH_BLOCK_A_GROUP		0x00100000

// Special modifiers for material output for OBJ, only:
// when exporting textures with OBJ, output a uniform material: Ka and Kd set to white (G3D likes this)
// no longer used for anything #define EXPT_OUTPUT_OBJ_NEUTRAL_MATERIAL	0x200000
// when exporting materials with OBJ, output the extra values (G3D likes this)
#define EXPT_OUTPUT_CUSTOM_MATERIAL		    0x00200000

// relative or absolute coordinates for OBJ
#define EXPT_OUTPUT_OBJ_REL_COORDINATES		0x00400000

// should subtypes (types differentiated by the data value bits) be output as separate textures?
#define EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE	0x00800000

// use biomes for export
#define EXPT_BIOME							0x01000000
#ifdef SKETCHFAB
// Sketchfab export (to export only RGBA texture)
#define EXPT_SKFB							0x02000000
#endif

// when exporting materials with OBJ, each group is also made into an object
#define EXPT_OUTPUT_OBJ_MAKE_GROUPS_OBJECTS		0x04000000

// export MDL or not for USD (affects custom material display, too)
#define EXPT_EXPORT_MDL                         0x08000000

// string length for export dialog, etc.
#define EP_FIELD_LENGTH 20

// linked to the ofn.lpstrFilter in Mineways.cpp
// This is a variant: some viewers (e.g. Deep Vie# OBJ relative coordinatesw) will multiply the material color by the texture;
// use this variant if you notice textures getting shaded different colors.
#define FILE_TYPE_WAVEFRONT_ABS_OBJ 0
#define FILE_TYPE_WAVEFRONT_REL_OBJ 1
#define FILE_TYPE_USD               2
#define FILE_TYPE_BINARY_MAGICS_STL 3
#define FILE_TYPE_BINARY_VISCAM_STL 4
#define FILE_TYPE_ASCII_STL         5
#define FILE_TYPE_VRML2             6
// this is an entirely separate file type, only exportable through the schematic export option
#define FILE_TYPE_SCHEMATIC         7

#define FILE_TYPE_TOTAL         8

#ifdef SKETCHFAB
// Sketchfab API field limits
#define SKFB_NAME_LIMIT         48
#define SKFB_DESC_LIMIT         1024
#define SKFB_TAG_LIMIT          29   // actually 48 but "mineways " and "minecraft " are automatically added so subtract 19 total, giving 29
#define SKFB_TOKEN_LIMIT        32
#define SKFB_PASSWORD_LIMIT     64

typedef struct PublishSkfbData
{
    // Sketchfab
    char skfbApiToken[SKFB_TOKEN_LIMIT + 1];
    char skfbName[SKFB_NAME_LIMIT + 1];
    char skfbDescription[SKFB_DESC_LIMIT + 1];
    char skfbTags[256];
    bool skfbPrivate;
    char skfbPassword[SKFB_PASSWORD_LIMIT + 1];
    bool skfbDraft;
    std::string skfbFilePath;
} PublishSkfbData;
#endif

typedef struct ExportFileData
{
    // dialog file type last chosen in export dialog; this is used next time.
    // Note that this value is *not* valid during export itself; fileType is passed in.
    int fileType;           // 0,1 - OBJ, 2 - USD, 3,4 - Binary STL, 5 - ASCII STL, 6 - VRML2, 7 - Schematic

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
    UINT radioExportTileTextures[FILE_TYPE_TOTAL];

    char tileDirString[MAX_PATH];

    UINT chkTextureRGB;
    UINT chkTextureA;
    UINT chkTextureRGBA;

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
    UINT chkBlockFacesAtBorders;    // should block faces be generated at the borders of the export? True for rendering. Doesn't affect 3D printing.
    UINT chkDecimate;    // Decimate meshes as possible
    UINT chkBiome;
    UINT chkCompositeOverlay;	// true means we'll make a composite texture of anything such as redstone wire that overlays a tile; false means make a separate floating object above the tile.

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

    UINT chkMakeGroupsObjects;
    UINT chkSeparateTypes;	// "Export separate types"
    UINT chkIndividualBlocks[FILE_TYPE_TOTAL];
    UINT chkMaterialPerFamily;	// "Material per family" of blocks
    UINT chkSplitByBlockType;	// "Split by block type"
    UINT chkCustomMaterial[FILE_TYPE_TOTAL];
    UINT chkExportMDL;  // applies only to USD files
    UINT chkDoubledBillboards;  // "Doubled billboards" means two (single-sided) billboards are output, back to back

    char scaleLightsString[EP_FIELD_LENGTH];
    float scaleLightsVal;
    char scaleEmittersString[EP_FIELD_LENGTH];
    float scaleEmittersVal;

    UINT flags;
} ExportFileData;

// USDA + 5 MDL files + color/normal/MER files
#define MAX_OUTPUT_FILES (6+5*TOTAL_TILES)

typedef struct FileList {
    int count;
    wchar_t* name[MAX_OUTPUT_FILES];  // output file list
} FileList;


typedef struct Options {
    int worldType;          // what world we're looking at: HELL, ENDER, etc., and other option toggles
    int saveFilterFlags;	// what objects should be kept - basic difference is flatsides get shown
    int exportFlags;		// exporting options
    bool moreExportMemory;   // use more memory for caching or not?
    int currentCacheSize;
    ExportFileData* pEFD;   // print or view option values, etc.
    ///// these are really statistics, but let's shove them in here - so sloppy!
    int dimensions[3];
    float dim_inches[3];
    float dim_cm[3];
    float cost;
    int totalBlocks;
    float block_mm;
    float block_inch;
} Options;

// number of blocks with entries in block info table (max number at bottom + 1) - now that 255 is used, we need this
#define NUM_BLOCKS_DEFINED 453
// number of official Minecraft blocks (no longer needed - we used to have the block after everything be special)
#define NUM_BLOCKS_STANDARD NUM_BLOCKS_DEFINED
// number of blocks we want to show on the map (no longer needed - includes the unknown one)
#define NUM_BLOCKS_MAP (NUM_BLOCKS_STANDARD)
// Total number of blocks. We used to include 16 wool blocks, but have now deleted these.
// NUM_BLOCKS_MAP could probably be turned into NUM_BLOCKS at this point, but keeping both for now
// until the code settles down.
#define NUM_BLOCKS NUM_BLOCKS_MAP

// for ColorSchemes, needed to have ColorScheme code not break for now.
#define NUM_BLOCKS_CS	NUM_BLOCKS_MAP

// absolute max the 4x4 * 16x16 space that tiles could have (without borders)
// TODO: memory-wise, might be better to alloc and realloc as needed, as this is now 4096 in size
#define NUM_MAX_SWATCHES (4*4*16*16)



// fills whole block
#define BLF_WHOLE			0x0001
// almost a whole block
#define BLF_ALMOST_WHOLE    0x0002
// stairs
#define BLF_STAIRS			0x0004
// half block, i.e., a slab
#define BLF_HALF			0x0008
// fair-sized, worth rendering, has geometry
#define BLF_MIDDLER			0x0010
// larger billboard object worth putting on the map
#define BLF_BILLBOARD		0x0020
// billboard flat through middle, usually transparent (portal, glass pane)
#define BLF_PANE			0x0040
// sits on top of a block below it
#define BLF_FLATTEN			0x0080
// flat on a wall: sign, ladder, etc. - normally not shown on the map; to make something visible on map, use BLF_FLATTEN instead, which otherwise is identical
#define BLF_FLATTEN_SMALL		0x0100
// small (or placed weirdly), not worth representing in 3D print (will disappear if not flattened, etc. when exporting for a 3D print), has geometry - normally not shown on the map
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

// this object emits light - affects output material
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
// set if the block does not affect fluid height. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_DNE_FLUID		0x200000
// set if the block connects to redstone - do only if there's no orientation to the block, e.g. repeaters attach only on two sides, so don't have this flag
// What this means: given a block, it will have BLF_CONNECTS_REDSTONE for it if, when you put the block
// down and put redstone dust next to it, the dust forms a single straight line into the block, instead of a cross.
#define BLF_CONNECTS_REDSTONE		0x400000
// has no geometry, on purpose
#define BLF_NONE			0x800000
// is an offset tile, rendered separately: rails, vines, lily pad, redstone, ladder (someday, tripwire? TODO)
#define BLF_OFFSET			0x1000000
// is a billboard or similar that is always underwater, such as seagrass and kelp. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_WATERLOG		0x2000000
// is a billboard or similar that may waterlog, such as coral fans; bit 0x100 is set if waterlogged. See https://minecraft.gamepedia.com/Waterlogging
#define BLF_MAYWATERLOG		0x4000000
// this object is a gate that attachs to fences if oriented properly - like BLF_FENCE_NEIGHBOR, but needs orientation to connect
#define BLF_FENCE_GATE		0x8000000
// this object is a fence that attachs to fences if of the right type - like BLF_FENCE_NEIGHBOR, but needs for types (nether, wood) to match to connect
#define BLF_FENCE			0x10000000
// this object is a double-slab or other voxel-filling block. Officially, it can waterlog, but for Mineways this behavior is basically idiotic, AFAIK, and causes additional unneeded instance outputs
#define BLF_LAME_WATERLOG	0x20000000

#define BLF_CLASS_SET   (BLF_NONE|BLF_WHOLE|BLF_ALMOST_WHOLE|BLF_STAIRS|BLF_HALF|BLF_MIDDLER|BLF_BILLBOARD|BLF_PANE|BLF_FLATTEN|BLF_FLATTEN_SMALL|BLF_SMALL_MIDDLER|BLF_SMALL_BILLBOARD)

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
    const char* name;
    unsigned int read_color;	// r,g,b, locked in place, never written to: used for initial setting of color for map (and light source emission color)
    float read_alpha;
    unsigned int color;	// r,g,b, NOT multiplied by alpha - input by the user, result of color scheme application
    unsigned int pcolor;	// r,g,b, premultiplied by alpha (basically, unmultColor * alpha) - used (only) in mapping
    float alpha;
    int txrX;   // column and row, from upper left, of 16x16 tiles in terrainExt.png, for TOP view of block
    int txrY;
    unsigned char subtype_mask;	// bits that are used in the data value to determine whether this is a separate material
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
    BLOCK_AIR = 0,
    BLOCK_STONE = 1,
    BLOCK_GRASS_BLOCK = 2,
    BLOCK_DIRT = 3,
    BLOCK_COBBLESTONE = 4,
    BLOCK_OAK_PLANKS = 5,
    BLOCK_SAPLING = 6,
    BLOCK_BEDROCK = 7,
    BLOCK_WATER = 8,
    BLOCK_STATIONARY_WATER = 9,
    BLOCK_LAVA = 10,
    BLOCK_STATIONARY_LAVA = 11,
    BLOCK_SAND = 12,
    BLOCK_GRAVEL = 13,
    BLOCK_LOG = 17,
    BLOCK_LEAVES = 18,
    BLOCK_SPONGE = 19,
    BLOCK_GLASS = 20,
    BLOCK_DISPENSER = 23,
    BLOCK_SANDSTONE = 24,
    BLOCK_NOTEBLOCK = 25,
    BLOCK_BED = 26,
    BLOCK_POWERED_RAIL = 27,
    BLOCK_DETECTOR_RAIL = 28,
    BLOCK_STICKY_PISTON = 29,
    BLOCK_COBWEB = 30,
    BLOCK_GRASS = 31,
    BLOCK_DEAD_BUSH = 32,
    BLOCK_PISTON = 33,
    BLOCK_PISTON_HEAD = 34,
    BLOCK_WOOL = 35,
    BLOCK_DANDELION = 37,
    BLOCK_POPPY = 38,
    BLOCK_BROWN_MUSHROOM = 39,
    BLOCK_RED_MUSHROOM = 40,
    BLOCK_OF_GOLD = 41,
    BLOCK_STONE_DOUBLE_SLAB = 43,
    BLOCK_STONE_SLAB = 44,
    BLOCK_BRICK = 45,
    BLOCK_TNT = 46,
    BLOCK_BOOKSHELF = 47,
    BLOCK_MOSSY_COBBLESTONE = 48,
    BLOCK_OBSIDIAN = 49,
    BLOCK_TORCH = 50,
    BLOCK_FIRE = 51,
    BLOCK_MONSTER_SPAWNER = 52,
    BLOCK_OAK_WOOD_STAIRS = 53,
    BLOCK_CHEST = 54,
    BLOCK_REDSTONE_WIRE = 55,
    // diamond ore = 56,
    // block of diamond = 57,
    BLOCK_CRAFTING_TABLE = 58,
    BLOCK_WHEAT = 59,
    BLOCK_FARMLAND = 60,
    BLOCK_FURNACE = 61,
    BLOCK_BURNING_FURNACE = 62,
    BLOCK_SIGN_POST = 63,
    BLOCK_WOODEN_DOOR = 64,
    BLOCK_LADDER = 65,
    BLOCK_RAIL = 66,
    BLOCK_COBBLESTONE_STAIRS = 67,
    BLOCK_WALL_SIGN = 68,
    BLOCK_LEVER = 69,
    BLOCK_STONE_PRESSURE_PLATE = 70,
    BLOCK_IRON_DOOR = 71,
    BLOCK_WOODEN_PRESSURE_PLATE = 72,
    BLOCK_REDSTONE_ORE = 73,
    BLOCK_GLOWING_REDSTONE_ORE = 74,
    BLOCK_REDSTONE_TORCH_OFF = 75,
    BLOCK_REDSTONE_TORCH_ON = 76,
    BLOCK_STONE_BUTTON = 77,
    BLOCK_SNOW = 78,          // this is just the snow covering the ground
    BLOCK_ICE = 79,
    BLOCK_SNOW_BLOCK = 80,    // confusing, eh?
    BLOCK_CACTUS = 81,
    // clay = 82 - never referenced!
    BLOCK_SUGAR_CANE = 83,
    BLOCK_JUKEBOX = 84,
    BLOCK_FENCE = 85,
    BLOCK_PUMPKIN = 86,
    BLOCK_NETHERRACK = 87,
    BLOCK_SOUL_SAND = 88,
    BLOCK_GLOWSTONE = 89,
    BLOCK_NETHER_PORTAL = 90,
    BLOCK_JACK_O_LANTERN = 91,
    BLOCK_CAKE = 92,
    BLOCK_REDSTONE_REPEATER_OFF = 93,
    BLOCK_REDSTONE_REPEATER_ON = 94,
    BLOCK_STAINED_GLASS = 95,	// was BLOCK_LOCKED_CHEST, which went away in 1.7
    BLOCK_TRAPDOOR = 96,
    BLOCK_INFESTED_STONE = 97,
    BLOCK_STONE_BRICKS = 98,
    BLOCK_HUGE_BROWN_MUSHROOM = 99,
    BLOCK_HUGE_RED_MUSHROOM = 100,
    BLOCK_IRON_BARS = 101,
    BLOCK_GLASS_PANE = 102,
    BLOCK_MELON = 103,
    BLOCK_PUMPKIN_STEM = 104,
    BLOCK_MELON_STEM = 105,
    BLOCK_VINES = 106,
    BLOCK_FENCE_GATE = 107,
    BLOCK_BRICK_STAIRS = 108,
    BLOCK_STONE_BRICK_STAIRS = 109,
    BLOCK_MYCELIUM = 110,
    BLOCK_LILY_PAD = 111,
    BLOCK_NETHER_BRICKS = 112,
    BLOCK_NETHER_BRICK_FENCE = 113,
    BLOCK_NETHER_BRICK_STAIRS = 114,
    BLOCK_NETHER_WART = 115,
    BLOCK_ENCHANTING_TABLE = 116,
    BLOCK_BREWING_STAND = 117,
    BLOCK_CAULDRON = 118,
    BLOCK_END_PORTAL = 119,
    BLOCK_END_PORTAL_FRAME = 120,
    BLOCK_END_STONE = 121,
    BLOCK_DRAGON_EGG = 122,
    BLOCK_WOODEN_DOUBLE_SLAB = 125,
    BLOCK_WOODEN_SLAB = 126,
    BLOCK_COCOA_PLANT = 127,
    BLOCK_SANDSTONE_STAIRS = 128,
    BLOCK_ENDER_CHEST = 130,
    BLOCK_TRIPWIRE_HOOK = 131,
    BLOCK_TRIPWIRE = 132,
    BLOCK_SPRUCE_WOOD_STAIRS = 134,
    BLOCK_BIRCH_WOOD_STAIRS = 135,
    BLOCK_JUNGLE_WOOD_STAIRS = 136,
    BLOCK_COMMAND_BLOCK = 137,
    BLOCK_BEACON = 138,
    BLOCK_COBBLESTONE_WALL = 139,
    BLOCK_FLOWER_POT = 140,
    BLOCK_CARROTS = 141,
    BLOCK_POTATOES = 142,
    BLOCK_WOODEN_BUTTON = 143,
    BLOCK_HEAD = 144,
    BLOCK_ANVIL = 145,
    // 1.5.2
    BLOCK_TRAPPED_CHEST = 146,
    BLOCK_WEIGHTED_PRESSURE_PLATE_LIGHT = 147,
    BLOCK_WEIGHTED_PRESSURE_PLATE_HEAVY = 148,
    BLOCK_REDSTONE_COMPARATOR = 149,
    BLOCK_REDSTONE_COMPARATOR_DEPRECATED = 150,
    BLOCK_DAYLIGHT_SENSOR = 151,	// TODO line 1916, make like trapdoor?
    BLOCK_REDSTONE_BLOCK = 152,
    BLOCK_NETHER_QUARTZ_ORE = 153,
    BLOCK_HOPPER = 154,
    BLOCK_QUARTZ_BLOCK = 155,
    BLOCK_QUARTZ_STAIRS = 156,
    BLOCK_ACTIVATOR_RAIL = 157,
    BLOCK_DROPPER = 158,
    // 1.6 & 1.7.2
    BLOCK_COLORED_TERRACOTTA = 159,	// was stained clay, now called terracotta: it's colored
    BLOCK_STAINED_GLASS_PANE = 160,
    BLOCK_AD_LEAVES = 161,
    BLOCK_AD_LOG = 162,
    BLOCK_ACACIA_WOOD_STAIRS = 163,
    BLOCK_DARK_OAK_WOOD_STAIRS = 164,
    // 1.8
    BLOCK_SLIME = 165,
    BLOCK_BARRIER = 166,
    BLOCK_IRON_TRAPDOOR = 167,
    BLOCK_PRISMARINE = 168,
    BLOCK_SEA_LANTERN = 169,
    BLOCK_HAY = 170,
    BLOCK_CARPET = 171,
    BLOCK_HARDENED_CLAY = 172,	// also called terracotta, but we keep the name here
    BLOCK_COAL_BLOCK = 173,
    // 1.9
    BLOCK_PACKED_ICE = 174,
    BLOCK_DOUBLE_FLOWER = 175,  // yes, it's out of order.
    //
    BLOCK_STANDING_BANNER = 176,
    BLOCK_WALL_BANNER = 177,
    BLOCK_INVERTED_DAYLIGHT_SENSOR = 178,
    BLOCK_RED_SANDSTONE = 179,
    BLOCK_RED_SANDSTONE_STAIRS = 180,
    BLOCK_RED_SANDSTONE_DOUBLE_SLAB = 181,
    BLOCK_RED_SANDSTONE_SLAB = 182,
    BLOCK_SPRUCE_FENCE_GATE = 183,
    BLOCK_BIRCH_FENCE_GATE = 184,
    BLOCK_JUNGLE_FENCE_GATE = 185,
    BLOCK_DARK_OAK_FENCE_GATE = 186,
    BLOCK_ACACIA_FENCE_GATE = 187,
    BLOCK_SPRUCE_FENCE = 188,
    BLOCK_BIRCH_FENCE = 189,
    BLOCK_JUNGLE_FENCE = 190,
    BLOCK_DARK_OAK_FENCE = 191,
    BLOCK_ACACIA_FENCE = 192,
    BLOCK_SPRUCE_DOOR = 193,
    BLOCK_BIRCH_DOOR = 194,
    BLOCK_JUNGLE_DOOR = 195,
    BLOCK_ACACIA_DOOR = 196,
    BLOCK_DARK_OAK_DOOR = 197,
    BLOCK_END_ROD = 198,
    BLOCK_CHORUS_PLANT = 199,
    BLOCK_CHORUS_FLOWER = 200,
    BLOCK_PURPUR_BLOCK = 201,
    BLOCK_PURPUR_PILLAR = 202,
    BLOCK_PURPUR_STAIRS = 203,
    BLOCK_PURPUR_DOUBLE_SLAB = 204,
    BLOCK_PURPUR_SLAB = 205,
    BLOCK_END_BRICKS = 206,
    BLOCK_BEETROOT_SEEDS = 207,
    BLOCK_DIRT_PATH = 208,
    BLOCK_END_GATEWAY = 209,
    BLOCK_REPEATING_COMMAND_BLOCK = 210,
    BLOCK_CHAIN_COMMAND_BLOCK = 211,
    BLOCK_FROSTED_ICE = 212,
    BLOCK_MAGMA_BLOCK = 213,
    BLOCK_NETHER_WART_BLOCK = 214,
    BLOCK_RED_NETHER_BRICK = 215,
    BLOCK_BONE_BLOCK = 216,
    BLOCK_STRUCTURE_VOID = 217,
    BLOCK_OBSERVER = 218,
    BLOCK_SHULKER_CHEST = 219,

    BLOCK_GLAZED_TERRACOTTA = 235,

    BLOCK_CONCRETE = 251,
    BLOCK_CONCRETE_POWDER = 252,

    BLOCK_UNKNOWN = 253,
    BLOCK_FAKE = 254,
    BLOCK_STRUCTURE_BLOCK = 255,

    // 1.13
    BLOCK_PRISMARINE_STAIRS = 257,
    BLOCK_PRISMARINE_BRICK_STAIRS = 258,
    BLOCK_DARK_PRISMARINE_STAIRS = 259,
    BLOCK_SPRUCE_TRAPDOOR = 260,
    BLOCK_BIRCH_TRAPDOOR = 261,
    BLOCK_JUNGLE_TRAPDOOR = 262,
    BLOCK_ACACIA_TRAPDOOR = 263,
    BLOCK_DARK_OAK_TRAPDOOR = 264,
    BLOCK_SPRUCE_BUTTON = 265,
    BLOCK_BIRCH_BUTTON = 266,
    BLOCK_JUNGLE_BUTTON = 267,
    BLOCK_ACACIA_BUTTON = 268,
    BLOCK_DARK_OAK_BUTTON = 269,
    BLOCK_SPRUCE_PRESSURE_PLATE = 270,
    BLOCK_BIRCH_PRESSURE_PLATE = 271,
    BLOCK_JUNGLE_PRESSURE_PLATE = 272,
    BLOCK_ACACIA_PRESSURE_PLATE = 273,
    BLOCK_DARK_OAK_PRESSURE_PLATE = 274,
    BLOCK_STRIPPED_OAK = 275,
    BLOCK_STRIPPED_ACACIA = 276,
    BLOCK_STRIPPED_OAK_WOOD = 277,
    BLOCK_STRIPPED_ACACIA_WOOD = 278,
    BLOCK_ORANGE_BANNER = 279,
    BLOCK_MAGENTA_BANNER = 280,
    BLOCK_LIGHT_BLUE_BANNER = 281,
    BLOCK_YELLOW_BANNER = 282,
    BLOCK_LIME_BANNER = 283,
    BLOCK_PINK_BANNER = 284,
    BLOCK_GRAY_BANNER = 285,
    BLOCK_LIGHT_GRAY_BANNER = 286,
    BLOCK_CYAN_BANNER = 287,
    BLOCK_PURPLE_BANNER = 288,
    BLOCK_BLUE_BANNER = 289,
    BLOCK_BROWN_BANNER = 290,
    BLOCK_GREEN_BANNER = 291,
    BLOCK_RED_BANNER = 292,
    BLOCK_BLACK_BANNER = 293,
    BLOCK_ORANGE_WALL_BANNER = 294,
    BLOCK_MAGENTA_WALL_BANNER = 295,
    BLOCK_LIGHT_BLUE_WALL_BANNER = 296,
    BLOCK_YELLOW_WALL_BANNER = 297,
    BLOCK_LIME_WALL_BANNER = 298,
    BLOCK_PINK_WALL_BANNER = 299,
    BLOCK_GRAY_WALL_BANNER = 300,
    BLOCK_LIGHT_GRAY_WALL_BANNER = 301,
    BLOCK_CYAN_WALL_BANNER = 302,
    BLOCK_PURPLE_WALL_BANNER = 303,
    BLOCK_BLUE_WALL_BANNER = 304,
    BLOCK_BROWN_WALL_BANNER = 305,
    BLOCK_GREEN_WALL_BANNER = 306,
    BLOCK_RED_WALL_BANNER = 307,
    BLOCK_BLACK_WALL_BANNER = 308,
    BLOCK_TALL_SEAGRASS = 309,
    BLOCK_SEAGRASS = 310,
    BLOCK_SMOOTH_STONE = 311,
    BLOCK_BLUE_ICE = 312,
    BLOCK_DRIED_KELP = 313,
    BLOCK_KELP = 314,
    BLOCK_CORAL_BLOCK = 315,
    BLOCK_DEAD_CORAL_BLOCK = 316,
    BLOCK_CORAL = 317,
    BLOCK_CORAL_FAN = 318,
    BLOCK_DEAD_CORAL_FAN = 319,
    BLOCK_CORAL_WALL_FAN = 320,
    BLOCK_DEAD_CORAL_WALL_FAN = 321,
    BLOCK_CONDUIT = 322,
    BLOCK_SEA_PICKLE = 323,
    BLOCK_TURTLE_EGG = 324,

    // 1.14: we give each new block type a number, trying to make it sensible. Put in blockinfo.cpp.
    // in BlockTranslations in nbt.cpp put the name used by Minecraft to convert to a number.
    BLOCK_DEAD_CORAL = 325, // 5 different types
    BLOCK_ACACIA_SIGN_POST = 326,
    BLOCK_SWEET_BERRY_BUSH = 327,
    BLOCK_BAMBOO = 328,
    BLOCK_ANDESITE_DOUBLE_SLAB = 329,
    BLOCK_ANDESITE_SLAB = 330,
    BLOCK_JIGSAW = 331,
    BLOCK_COMPOSTER = 332,
    BLOCK_BARREL = 333,
    BLOCK_STONECUTTER = 334,
    BLOCK_GRINDSTONE = 335,
    BLOCK_LECTERN = 336,
    BLOCK_BELL = 337,
    BLOCK_LANTERN = 338,
    BLOCK_CAMPFIRE = 339,
    BLOCK_SCAFFOLDING = 340,
    BLOCK_BEE_NEST = 341,
    BLOCK_HONEY = 342,
    BLOCK_HONEYCOMB = 343,
    BLOCK_CRYING_OBSIDIAN = 344,
    BLOCK_RESPAWN_ANCHOR = 345,
    BLOCK_CRIMSON_TRAPDOOR = 346,
    BLOCK_WARPED_TRAPDOOR = 347,
    BLOCK_CRIMSON_BUTTON = 348,
    BLOCK_WARPED_BUTTON = 349,
    BLOCK_POLISHED_BLACKSTONE_BUTTON = 350,
    BLOCK_CRIMSON_FENCE = 351,
    BLOCK_WARPED_FENCE = 352,
    BLOCK_CRIMSON_FENCE_GATE = 353,
    BLOCK_WARPED_FENCE_GATE = 354,
    BLOCK_CRIMSON_DOOR = 355,
    BLOCK_WARPED_DOOR = 356,
    BLOCK_CRIMSON_PRESSURE_PLATE = 357,
    BLOCK_WARPED_PRESSURE_PLATE = 358,
    BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE = 359,
    BLOCK_CRIMSON_DOUBLE_SLAB = 360,
    BLOCK_CRIMSON_SLAB = 361,
    BLOCK_SOUL_TORCH = 362,
    BLOCK_WEEPING_VINES = 363,
    BLOCK_CHAIN = 364,

    // no more lumping the stairs together
    BLOCK_STONE_STAIRS = 365,
    BLOCK_GRANITE_STAIRS = 366,
    BLOCK_POLISHED_GRANITE_STAIRS = 367,
    BLOCK_SMOOTH_QUARTZ_STAIRS = 368,
    BLOCK_DIORITE_STAIRS = 369,
    BLOCK_POLISHED_DIORITE_STAIRS = 370,
    BLOCK_END_STONE_BRICK_STAIRS = 371,
    BLOCK_ANDESITE_STAIRS = 372,
    BLOCK_POLISHED_ANDESITE_STAIRS = 373,
    BLOCK_RED_NETHER_BRICK_STAIRS = 374,
    BLOCK_MOSSY_STONE_BRICK_STAIRS = 375,
    BLOCK_MOSSY_COBBLESTONE_STAIRS = 376,
    BLOCK_SMOOTH_SANDSTONE_STAIRS = 377,
    BLOCK_SMOOTH_RED_SANDSTONE_STAIRS = 378,
    BLOCK_CRIMSON_STAIRS = 379,
    BLOCK_WARPED_STAIRS = 380,
    BLOCK_BLACKSTONE_STAIRS = 381,
    BLOCK_POLISHED_BLACKSTONE_STAIRS = 382,
    BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS = 383,

    // 1.17
    BLOCK_CANDLE = 384,
    BLOCK_LIT_CANDLE = 385,
    BLOCK_COLORED_CANDLE = 386,
    BLOCK_LIT_COLORED_CANDLE = 387,
    BLOCK_AMETHYST = 388,
    BLOCK_AMETHYST_BUD = 389,
    BLOCK_POINTED_DRIPSTONE = 390,
    BLOCK_CUT_COPPER_STAIRS = 391,
    BLOCK_EXPOSED_CUT_COPPER_STAIRS = 392,
    BLOCK_WEATHERED_CUT_COPPER_STAIRS = 393,
    BLOCK_OXIDIZED_CUT_COPPER_STAIRS = 394,
    BLOCK_LIGHTNING_ROD = 395,
    BLOCK_RESERVED_FLOWER_POT = 396,
    BLOCK_CUT_COPPER_DOUBLE_SLAB = 397,
    BLOCK_CUT_COPPER_SLAB = 398,
    BLOCK_WAXED_CUT_COPPER_STAIRS = 399,
    BLOCK_RESERVED_MOB_HEAD = 400,
    BLOCK_WAXED_EXPOSED_CUT_COPPER_STAIRS = 401,
    BLOCK_WAXED_WEATHERED_CUT_COPPER_STAIRS = 402,
    BLOCK_WAXED_OXIDIZED_CUT_COPPER_STAIRS = 403,
    BLOCK_CAVE_VINES = 404,
    BLOCK_CAVE_VINES_LIT = 405,
    BLOCK_SPORE_BLOSSOM = 406,
    BLOCK_AZALEA = 407,
    BLOCK_BIG_DRIPLEAF = 408,
    BLOCK_SMALL_DRIPLEAF = 409,
    BLOCK_GLOW_LICHEN = 410,
    BLOCK_SCULK_SENSOR = 411,
    BLOCK_COBBLED_DEEPSLATE_STAIRS = 412,
    BLOCK_POLISHED_DEEPSLATE_STAIRS = 413,
    BLOCK_DEEPSLATE_BRICKS_STAIRS = 414,
    BLOCK_DEEPSLATE_TILES_STAIRS = 415,
    //BLOCK_LIGHT = 416,

    BLOCK_MANGROVE_LOG = 416,
    BLOCK_DECORATED_POT = 417,    // goofy numbering, I know. Was MANGROVE_PLANKS, but that's not needed.
    BLOCK_MANGROVE_DOOR = 418,
    BLOCK_MANGROVE_TRAPDOOR = 419,
    BLOCK_MANGROVE_PROPAGULE = 420,
    BLOCK_MANGROVE_ROOTS = 421,
    BLOCK_MUDDY_MANGROVE_ROOTS = 422,
    BLOCK_STRIPPED_MANGROVE = 423,
    BLOCK_STRIPPED_MANGROVE_WOOD = 424,
    BLOCK_MANGROVE_STAIRS = 425,
    BLOCK_MUD_BRICK_STAIRS = 426,
    BLOCK_MANGROVE_SIGN_POST = 427,
    BLOCK_MANGROVE_WALL_SIGN = 428,
    BLOCK_MANGROVE_PRESSURE_PLATE = 429,
    BLOCK_MANGROVE_BUTTON = 430,
    BLOCK_MANGROVE_FENCE = 431,
    BLOCK_MANGROVE_FENCE_GATE = 432,
    BLOCK_SCULK_SHRIEKER = 433,
    BLOCK_SCULK_VEIN = 434,
    BLOCK_FROGSPAWN = 435,
    BLOCK_FROGLIGHT = 436,
    BLOCK_MANGROVE_LEAVES = 437,

    BLOCK_CHERRY_BUTTON = 438,
    BLOCK_CHERRY_DOOR = 439,
    BLOCK_CHERRY_FENCE = 440,
    BLOCK_CHERRY_FENCE_GATE = 441,
    BLOCK_CHERRY_PRESSURE_PLATE = 442,
    BLOCK_CHERRY_STAIRS = 443,
    BLOCK_CHERRY_TRAPDOOR = 444,
    BLOCK_BAMBOO_BUTTON = 445,
    BLOCK_BAMBOO_DOOR = 446,
    BLOCK_BAMBOO_FENCE = 447,
    BLOCK_BAMBOO_FENCE_GATE = 448,
    BLOCK_BAMBOO_PRESSURE_PLATE = 449,
    BLOCK_BAMBOO_STAIRS = 450,
    BLOCK_BAMBOO_TRAPDOOR = 451,
    BLOCK_BAMBOO_MOSAIC_STAIRS = 452,
};
