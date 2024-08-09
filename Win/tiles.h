// tiles.h - defines where each Minecraft texture block is used (if used at all)

//#include "targetver.h"

#pragma once

#define SBIT_REPEAT_SIDES       0x01
#define SBIT_REPEAT_TOP_BOTTOM  0x02
#define SBIT_CLAMP_BOTTOM       0x04
#define SBIT_CLAMP_TOP          0x08
#define SBIT_CLAMP_RIGHT        0x10
#define SBIT_CLAMP_LEFT         0x20

// if tile is a cutout, note this, as it should be "bled" outwards before output when rendering
#define SBIT_DECAL				0x40
// if tile is cutout geometry, note this so it's bled out for 3D printing and rendering. Cutout geometry differs from a decal in that
// the texture defines exactly where various content is supposed to exist - alpha transparency is not actually needed for the object,
// and in fact should be discouraged, as if nearest neighbor sampling is not used, the alphas will cause gaps in the object. Examples:
// chests, cauldron top.
#define SBIT_CUTOUT_GEOMETRY	0x80
// if tile has full transparency for some other reason, e.g., it's an overlay, tag it here so that we know that's the case 
#define SBIT_ALPHA_OVERLAY		0x100

// special bit: if the tile is a leaf tile, Mineways itself can optionally make it solid
#define SBIT_LEAVES				0x200

// If this tile is not the final tile, identify it as being something that gets used to synthesize a new output tile.
// That is, the tile, when output by Mineways, will have a different name with a _y.png suffix.
#define SBIT_SYNTHESIZED         0x400

// If set, the incoming .png's black pixels should be treated as having an alpha of 0.
// Normally Minecraft textures have alpha set properly, but this is a workaround for those that don't.
// Currently not needed - they've cleaned up their act.
#define SBIT_BLACK_ALPHA		0x8000


// types of blocks: tiling, billboard, and sides (which tile only horizontally)
// Internally, an 18x18 tile is made from a 16x16, and the four border edges of this new tile are each classified as of three things:
// 1. Repeat the opposite edge's content. This is done for grass or decorative tiles, for example.
// 2. Clamp the edge, i.e., take the edge of the 16x16 and copy to the border. If interpolation occurs, this edge then properly
//    gets the color if interpolation occurs.
// 3. Do neither. If not repeated or clamped, it means the edge is made entirely transparent. This is the norm for most decals.
// Repeat all is for things like grass.
#define SWATCH_REPEAT_ALL                   (SBIT_REPEAT_SIDES|SBIT_REPEAT_TOP_BOTTOM)
// Repeat sides else clamp is for tiles like the sides of grass, where top and bottom should be clamped.
#define SWATCH_REPEAT_SIDES_ELSE_CLAMP      (SBIT_REPEAT_SIDES|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
// Repeat top and bottom is for cactus sides and rails, also some vines, kelp
#define SWATCH_TILE_BOTTOM_AND_TOP          SBIT_REPEAT_TOP_BOTTOM
// Bottom and right is for the curved rail
#define SWATCH_CLAMP_BOTTOM_AND_RIGHT       (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT)
#define SWATCH_CLAMP_BOTTOM_AND_RIGHT       (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT)
// Bottom and top clamp only (no repeat) for double-height (two block high) plants, kelp, tall sea grass
#define SWATCH_CLAMP_BOTTOM_AND_TOP         (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
// Clamp bottom and sides for bed and enchanting table and stonecutter
#define SWATCH_CLAMP_ALL_BUT_TOP            (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)
// Clamp all is normally used for "geometric" cutout tiles SBIT_CUTOUT_GEOMETRY where just a part of the tile is selected. For 3D printing
// and for interpolation, you want to have "invisible" texels off the edges to be clamp copied so that they are properly interpolated.
#define SWATCH_CLAMP_ALL                    (SBIT_CLAMP_TOP|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)


// If this number changes, also change warning #7 in gPopupInfo (see TerrainExt.png in that message) in Mineways.cpp
#define VERTICAL_TILES 67
#define TOTAL_TILES (VERTICAL_TILES*16)
static struct {
    int txrX;   // column and row, from upper left, of 16x16+ tiles in terrain.png, for top view of block
    int txrY;
    // Representative block type for the tile, usually the first block to use this tile. Needed for setting the shininess for the tile.
    // Currently the name of the type (not the tile's name) is scanned for terms such as "iron", "glass", "polished", etc.
    // Because we often lump in many different block types into one, e.g., STONE has granite and polished granite, we differentiate
    // the polished granite tile by setting the "material type" as "polished granite stairs".
    // Use crossCorrelate code in _DEBUG in Mineways to *help* build this column.
    int typeForMtl;
    // the data value associated with the tile type. Needed to give the illumination level of the tile.
    int dataValForMtl;
    const wchar_t* filename;
    const wchar_t* altFilename;   // new 1.13 name
    int flags;

    // Mineways uses a few special tiles for input, and for output. Tiles starting "MW_" are ones that are not (easily) found in Minecraft
    // in the block textures and so are ones cobbled together to provide the effect: bed parts, end portal effect, shulker box side and bottom.
    // Tiles starting "MWO_" are not required as inputs (though could be, if needed), but are output. These are chests and redstone wire,
    // at this point, which the TileMaker normally reads specially from the chests directory and writes to these locations, or Mineways
    // creates on the fly internally.
    // TODO: someday, just read from the unzipped texture pack itself, using the proper paths.
    // NOTE! If you change anything here, you'll have to change faceCanTile's swatchLoc exception list.
} gTilesTable[TOTAL_TILES] = {
    {  0,  0,   2, 0, L"grass_block_top", L"grass_top", SWATCH_REPEAT_ALL | SBIT_SYNTHESIZED },	// tinted by grass color
    {  1,  0,   1, 0, L"stone", L"", SWATCH_REPEAT_ALL },
    {  2,  0,   3, 0, L"dirt", L"", SWATCH_REPEAT_ALL },
    {  3,  0,   2, 0, L"grass_block_side", L"grass_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_SYNTHESIZED },
    {  4,  0,   5, 0, L"oak_planks", L"planks_oak", SWATCH_REPEAT_ALL },
    {  5,  0,  44, 0, L"stone_slab_side", L"", SWATCH_REPEAT_ALL }, // no longer used in 1.16; we substitute 11,23
    {  6,  0,  44, 0, L"stone_slab_top", L"", SWATCH_REPEAT_ALL }, // no longer used in 1.16; we substitute 10,23
    {  7,  0,  45, 0, L"bricks", L"brick", SWATCH_REPEAT_ALL },
    {  8,  0,  46, 0, L"tnt_side", L"", SWATCH_REPEAT_ALL },
    {  9,  0,  46, 0, L"tnt_top", L"", SWATCH_REPEAT_ALL },
    { 10,  0,  46, 0, L"tnt_bottom", L"", SWATCH_REPEAT_ALL },
    { 11,  0,  30, 0, L"cobweb", L"web", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12,  0,  38, 0, L"poppy", L"flower_rose", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13,  0,  37, 0, L"dandelion", L"flower_dandelion", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14,  0,  90, 0, L"nether_portal", L"portal", SWATCH_REPEAT_ALL },	// really, bluish originally, now it's better
    { 15,  0,   6, 0, L"oak_sapling", L"sapling_oak", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0,  1,   4, 0, L"cobblestone", L"", SWATCH_REPEAT_ALL },
    {  1,  1,   7, 0, L"bedrock", L"", SWATCH_REPEAT_ALL },
    {  2,  1,  12, 0, L"sand", L"", SWATCH_REPEAT_ALL },
    {  3,  1,  13, 0, L"gravel", L"", SWATCH_REPEAT_ALL },
    {  4,  1,  17, 0, L"oak_log", L"log_oak", SWATCH_REPEAT_ALL },
    {  5,  1,  17, 0, L"oak_log_top", L"log_oak_top", SWATCH_REPEAT_ALL },	// and every other log, we don't separate these out
    {  6,  1,  42, 0, L"iron_block", L"", SWATCH_REPEAT_ALL },
    {  7,  1,  41, 0, L"gold_block", L"", SWATCH_REPEAT_ALL },
    {  8,  1,  57, 0, L"diamond_block", L"", SWATCH_REPEAT_ALL },
    {  9,  1,  54, 0, L"MWO_chest_top", L"chest_top", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest. Find chests in \assets\minecraft\textures\entity\chest and include in blocks\chest subdirectory
    { 10,  1,  54, 0, L"MWO_chest_side", L"chest_side", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11,  1,  54, 0, L"MWO_chest_front", L"chest_front", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest - note these three are sometimes in Bedrock with the alternate name
    { 12,  1,  40, 0, L"red_mushroom", L"mushroom_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13,  1,  39, 0, L"brown_mushroom", L"mushroom_brown", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14,  1,   6, 0, L"jungle_sapling", L"sapling_jungle", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15,  1,  51, 0, L"fire_0", L"fire_layer_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// input is fire animation strip - ignoring fire_1
    {  0,  2,  14, 0, L"gold_ore", L"", SWATCH_REPEAT_ALL },
    {  1,  2,  15, 0, L"iron_ore", L"", SWATCH_REPEAT_ALL },
    {  2,  2,  16, 0, L"coal_ore", L"", SWATCH_REPEAT_ALL },
    {  3,  2,  47, 0, L"bookshelf", L"", SWATCH_REPEAT_ALL }, // side - top and bottom are oak planks
    {  4,  2,  48, 0, L"mossy_cobblestone", L"cobblestone_mossy", SWATCH_REPEAT_ALL },
    {  5,  2,  49, 0, L"obsidian", L"", SWATCH_REPEAT_ALL },
    {  6,  2,   2, 0, L"grass_block_side_overlay", L"grass_side_overlay", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_ALPHA_OVERLAY }, // was "grass_side_overlay" - we use it for temporary work - grass_side_overlay tinted by grass.png, but we don't use it.
    {  7,  2,  31, 0, L"grass", L"short_grass", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },    // 1.20.3 changed grass to short_grass for ID and texture
    {  8,  2,   1, 0, L"MWO_workspace1", L"", SWATCH_REPEAT_ALL },	// we use it for temporary work - output as white? top grayscale, but we don't use it, nor does Mojang - left as "it's stone"
    {  9,  2,  54, 0, L"MWO_double_chest_front_left", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY }, // was beacon - taken by chest
    { 10,  2,  54, 0, L"MWO_double_chest_front_right", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11,  2,  58, 0, L"crafting_table_top", L"", SWATCH_REPEAT_ALL },
    { 12,  2,  61, 0, L"furnace_front", L"furnace_front_off", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 13,  2,  61, 0, L"furnace_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 14,  2,  23, 0, L"dispenser_front", L"dispenser_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 15,  2,  23, 0, L"dispenser_front_vertical", L"", SWATCH_REPEAT_ALL }, // ADD-IN; instead, input could be second fire animation strip "fire_layer_1" - TODO use both fire tiles?
    {  0,  3,  19, 0, L"sponge", L"", SWATCH_REPEAT_ALL },
    {  1,  3,  20, 0, L"glass", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  2,  3,  56, 0, L"diamond_ore", L"", SWATCH_REPEAT_ALL },
    {  3,  3,  73, 0, L"redstone_ore", L"", SWATCH_REPEAT_ALL },
    {  4,  3,  18, 0, L"oak_leaves", L"leaves_oak", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },
    {  5,  3,   3, 0, L"coarse_dirt", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8 - replaced leaves_oak_opaque
    {  6,  3,  98, 0, L"stone_bricks", L"stonebrick", SWATCH_REPEAT_ALL },
    {  7,  3,  32, 0, L"dead_bush", L"deadbush", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8,  3,  31, 0, L"fern", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  9,  3,  54, 0, L"MWO_double_chest_back_left", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 10,  3,  54, 0, L"MWO_double_chest_back_right", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11,  3,  58, 0, L"crafting_table_side", L"", SWATCH_REPEAT_ALL },
    { 12,  3,  58, 0, L"crafting_table_front", L"", SWATCH_REPEAT_ALL },
    { 13,  3,  62, 0, L"furnace_front_on", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },   // note that we make only the front of the furnace be an emitter, by calling it 62
    { 14,  3,  23, 0, L"furnace_top", L"", SWATCH_REPEAT_ALL },	// also used for dispenser
    { 15,  3,   6, 0, L"spruce_sapling", L"sapling_spruce", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0,  4,  35, 0, L"white_wool", L"wool_colored_white", SWATCH_REPEAT_ALL },
    {  1,  4,  52, 0, L"spawner", L"mob_spawner", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  2,  4,  78, 0, L"snow", L"", SWATCH_REPEAT_ALL },
    {  3,  4,  79, 0, L"ice", L"", SWATCH_REPEAT_ALL },
    {  4,  4,   2, 0, L"grass_block_snow", L"grass_side_snowed", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  5,  4,  81, 0, L"cactus_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  6,  4,  81, 0, L"cactus_side", L"", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL | SBIT_CUTOUT_GEOMETRY },	// weird one: cutout, but also for 3D printing it's geometry
    {  7,  4,  81, 0, L"cactus_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8,  4,  82, 0, L"clay", L"", SWATCH_REPEAT_ALL },
    {  9,  4,  83, 0, L"sugar_cane", L"reeds", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 10,  4,  84, 0, L"jukebox_side", L"", SWATCH_REPEAT_ALL },	// was noteblock, which is now below
    { 11,  4,  84, 0, L"jukebox_top", L"juketop", SWATCH_REPEAT_ALL },  // alt is from LunaHD
    { 12,  4, 111, 0, L"lily_pad", L"waterlily", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    { 13,  4, 110, 0, L"mycelium_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 14,  4, 110, 0, L"mycelium_top", L"", SWATCH_REPEAT_ALL },
    { 15,  4,   6, 0, L"birch_sapling", L"sapling_birch", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0,  5,  50, 0, L"torch", L"torch_on", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1,  5,  64, 0, L"oak_door_top", L"door_wood_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  2,  5,  71, 0, L"iron_door_top", L"door_iron_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  3,  5,  65, 0, L"ladder", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  4,  5,  96, 0, L"oak_trapdoor", L"trapdoor", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  5,  5, 101, 0, L"iron_bars", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  6,  5,  60, 0, L"farmland_moist", L"farmland_wet", SWATCH_REPEAT_ALL },
    {  7,  5,  60, 0, L"farmland", L"farmland_dry", SWATCH_REPEAT_ALL },
    {  8,  5,  59, 0, L"wheat_stage0", L"wheat_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  9,  5,  59, 0, L"wheat_stage1", L"wheat_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10,  5,  59, 0, L"wheat_stage2", L"wheat_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11,  5,  59, 0, L"wheat_stage3", L"wheat_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12,  5,  59, 0, L"wheat_stage4", L"wheat_stage_4", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13,  5,  59, 0, L"wheat_stage5", L"wheat_stage_5", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14,  5,  59, 0, L"wheat_stage6", L"wheat_stage_6", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15,  5,  59, 0, L"wheat_stage7", L"wheat_stage_7", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0,  6,  69, 0, L"lever", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1,  6,  64, 0, L"oak_door_bottom", L"door_wood_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL }, // note: normally not a decal, but done for resource packs
    {  2,  6,  71, 0, L"iron_door_bottom", L"door_iron_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL }, // note: normally not a decal, but done for resource packs
    {  3,  6,  76, 0, L"redstone_torch", L"redstone_torch_on", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4,  6,  45, 0, L"mossy_stone_bricks", L"stone_bricks_mossy", SWATCH_REPEAT_ALL },
    {  5,  6,  45, 0, L"cracked_stone_bricks", L"stone_bricks_cracked", SWATCH_REPEAT_ALL },
    {  6,  6,  86, 0, L"pumpkin_top", L"", SWATCH_REPEAT_ALL },
    {  7,  6,  87, 0, L"netherrack", L"", SWATCH_REPEAT_ALL },
    {  8,  6,  88, 0, L"soul_sand", L"", SWATCH_REPEAT_ALL },
    {  9,  6,  89, 0, L"glowstone", L"", SWATCH_REPEAT_ALL },
    { 10,  6,  29, 0, L"piston_top_sticky", L"", SWATCH_REPEAT_ALL },
    { 11,  6,  34, 0, L"piston_top", L"piston_top_normal", SWATCH_REPEAT_ALL },
    { 12,  6,  29, 0, L"piston_side", L"", SWATCH_REPEAT_ALL },
    { 13,  6,  34, 0, L"piston_bottom", L"", SWATCH_REPEAT_ALL },
    { 14,  6,  34, 0, L"piston_inner", L"", SWATCH_REPEAT_ALL },
    { 15,  6, 105, 0, L"melon_stem", L"melon_stem_disconnected", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  0,  7,  66, 0, L"rail_corner", L"rail_normal_turned", SWATCH_CLAMP_BOTTOM_AND_RIGHT | SBIT_DECAL },
    {  1,  7,  35, 0, L"black_wool", L"wool_colored_black", SWATCH_REPEAT_ALL },
    {  2,  7,  35, 0, L"gray_wool", L"wool_colored_gray", SWATCH_REPEAT_ALL },
    {  3,  7,  75, 0, L"redstone_torch_off", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4,  7,  17, 0, L"spruce_log", L"log_spruce", SWATCH_REPEAT_ALL },
    {  5,  7,  17, 0, L"birch_log", L"log_birch", SWATCH_REPEAT_ALL },
    {  6,  7,  86, 0, L"pumpkin_side", L"", SWATCH_REPEAT_ALL },
    {  7,  7,  86, 0, L"carved_pumpkin", L"pumpkin_face_off", SWATCH_REPEAT_ALL },
    {  8,  7,  91, 0, L"jack_o_lantern", L"pumpkin_face_on", SWATCH_REPEAT_ALL },
    {  9,  7,  92, 0, L"cake_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10,  7,  92, 0, L"cake_side", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 11,  7,  92, 0, L"cake_inner", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 12,  7,  92, 0, L"cake_bottom", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13,  7, 100, 0, L"red_mushroom_block", L"mushroom_block_skin_red", SWATCH_REPEAT_ALL },
    { 14,  7,  99, 0, L"brown_mushroom_block", L"mushroom_block_skin_brown", SWATCH_REPEAT_ALL },
    { 15,  7, 105, 0, L"attached_melon_stem", L"melon_stem_connected", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  0,  8,  66, 0, L"rail", L"rail_normal", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    {  1,  8,  35, 0, L"red_wool", L"wool_colored_red", SWATCH_REPEAT_ALL },
    {  2,  8,  35, 0, L"pink_wool", L"wool_colored_pink", SWATCH_REPEAT_ALL },
    {  3,  8,  93, 0, L"repeater", L"repeater_off", SWATCH_REPEAT_ALL },
    {  4,  8,  18, 0, L"spruce_leaves", L"leaves_spruce", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },
    {  5,  8, 179, 0, L"red_sandstone_bottom", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    {  6,  8,  26, 0, L"MW_bed_feet_top", L"bed_feet_top", SWATCH_CLAMP_ALL },
    {  7,  8,  26, 0, L"MW_bed_head_top", L"bed_head_top", SWATCH_CLAMP_ALL },
    {  8,  8, 103, 0, L"melon_side", L"", SWATCH_REPEAT_ALL },
    {  9,  8, 103, 0, L"melon_top", L"", SWATCH_REPEAT_ALL },
    { 10,  8, 118, 0, L"cauldron_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11,  8, 118, 0, L"cauldron_inner", L"", SWATCH_REPEAT_ALL },
    { 12,  8,  25, 0, L"note_block", L"noteblock", SWATCH_REPEAT_ALL },
    { 13,  8, 100, 0, L"mushroom_stem", L"mushroom_block_skin_stem", SWATCH_REPEAT_ALL },
    { 14,  8, 100, 0, L"mushroom_block_inside", L"", SWATCH_REPEAT_ALL },
    { 15,  8, 106, 0, L"vine", L"", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL | SBIT_SYNTHESIZED },	// grayscale; just tops and bottoms match up
    {  0,  9,  22, 0, L"lapis_block", L"", SWATCH_REPEAT_ALL },
    {  1,  9,  35, 0, L"green_wool", L"wool_colored_green", SWATCH_REPEAT_ALL },
    {  2,  9,  35, 0, L"lime_wool", L"wool_colored_lime", SWATCH_REPEAT_ALL },
    {  3,  9,  94, 0, L"repeater_on", L"", SWATCH_REPEAT_ALL },
    {  4,  9, 102, 0, L"glass_pane_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  5,  9,  26, 0, L"MW_bed_feet_end", L"bed_feet_end", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  6,  9,  26, 0, L"MW_bed_feet_side", L"bed_feet_side", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  7,  9,  26, 0, L"MW_bed_head_side", L"bed_head_side", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  8,  9,  26, 0, L"MW_bed_head_end", L"bed_head_end", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  9,  9,  17, 0, L"jungle_log", L"log_jungle", SWATCH_REPEAT_ALL },
    { 10,  9, 118, 0, L"cauldron_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_CUTOUT_GEOMETRY },
    { 11,  9, 118, 0, L"cauldron_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12,  9, 117, 0, L"brewing_stand_base", L"", SWATCH_REPEAT_ALL },
    { 13,  9, 117, 0, L"brewing_stand", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14,  9, 120, 0, L"end_portal_frame_top", L"endframe_top", SWATCH_REPEAT_ALL },
    { 15,  9, 120, 0, L"end_portal_frame_side", L"endframe_side", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  0, 10,  21, 0, L"lapis_ore", L"", SWATCH_REPEAT_ALL },
    {  1, 10,  35, 0, L"brown_wool", L"wool_colored_brown", SWATCH_REPEAT_ALL },
    {  2, 10,  35, 0, L"yellow_wool", L"wool_colored_yellow", SWATCH_REPEAT_ALL },
    {  3, 10,  27, 0, L"powered_rail", L"rail_golden", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    {  4, 10,  55, 0, L"redstone_dust_line0", L"redstone_dust_line", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_SYNTHESIZED },	// vertical (runs north-south), alt is smoolistic
    {  5, 10,  55, 0, L"redstone_dust_line1", L"", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_SYNTHESIZED },	// "horizontal": appears vertically in the texture, but runs east-west
    {  6, 10, 116, 0, L"enchanting_table_top", L"", SWATCH_REPEAT_ALL },
    {  7, 10, 122, 0, L"dragon_egg", L"", SWATCH_REPEAT_ALL },
    {  8, 10, 127, 0, L"cocoa_stage2", L"cocoa_stage_2", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 10, 127, 0, L"cocoa_stage1", L"cocoa_stage_1", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 10, 127, 0, L"cocoa_stage0", L"cocoa_stage_0", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 10, 129, 0, L"emerald_ore", L"", SWATCH_REPEAT_ALL },
    { 12, 10, 131, 0, L"tripwire_hook", L"trip_wire_source", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 10, 132, 0, L"tripwire", L"trip_wire", SWATCH_CLAMP_ALL | SBIT_DECAL },
    { 14, 10, 120, 0, L"end_portal_frame_eye", L"endframe_eye", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 10, 121, 0, L"end_stone", L"", SWATCH_REPEAT_ALL },
    {  0, 11,  24, 0, L"sandstone_top", L"", SWATCH_REPEAT_ALL },
    {  1, 11,  35, 0, L"blue_wool", L"wool_colored_blue", SWATCH_REPEAT_ALL },
    {  2, 11,  35, 0, L"light_blue_wool", L"wool_colored_light_blue", SWATCH_REPEAT_ALL },
    {  3, 11,  27, 0, L"powered_rail_on", L"rail_golden_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    {  4, 11,  55, 0, L"redstone_dust_dot", L"", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  5, 11,  17, 0, L"acacia_log", L"log_acacia", 0x0 },	// ADD-IN 1.7.2
    {  6, 11, 116, 0, L"enchanting_table_side", L"", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  7, 11, 116, 0, L"enchanting_table_bottom", L"", SWATCH_REPEAT_ALL },
    {  8, 11, 119, 0, L"MW_end_portal", L"", SWATCH_REPEAT_ALL },    // custom - the 3D effect seen through the end portal - TODO: extract a small chunk from assets\minecraft\textures\entity
    {  9, 11,  50, 0, L"MWO_flattened_soul_torch_top", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// MANUFACTURED used for flattened soul torch top; not used in rendering, but 3D printing uses for composites for torches from above
    { 10, 11, 140, 0, L"flower_pot", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 11,  17, 0, L"birch_log_top", L"log_birch_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 12, 11,  17, 0, L"spruce_log_top", L"log_spruce_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 13, 11,  17, 0, L"jungle_log_top", L"log_jungle_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 14, 11, 104, 0, L"pumpkin_stem", L"pumpkin_stem_disconnected", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },	// ADD-IN
    { 15, 11, 104, 0, L"attached_pumpkin_stem", L"pumpkin_stem_connected", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },	// ADD-IN
    {  0, 12,  24, 0, L"sandstone", L"sandstone_normal", SWATCH_REPEAT_ALL },
    {  1, 12,  35, 0, L"purple_wool", L"wool_colored_purple", SWATCH_REPEAT_ALL },
    {  2, 12,  35, 0, L"magenta_wool", L"wool_colored_magenta", SWATCH_REPEAT_ALL },
    {  3, 12,  28, 0, L"detector_rail", L"rail_detector", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    {  4, 12,  18, 0, L"jungle_leaves", L"leaves_jungle", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },
    {  5, 12, 179, 0, L"chiseled_red_sandstone", L"red_sandstone_chiseled", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    {  6, 12, 134, 0, L"spruce_planks", L"planks_spruce", SWATCH_REPEAT_ALL },
    {  7, 12, 136, 0, L"jungle_planks", L"planks_jungle", SWATCH_REPEAT_ALL },
    {  8, 12, 141, 0, L"carrots_stage0", L"carrots_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_0 in basic game, but can be different in texture packs
    {  9, 12, 141, 0, L"carrots_stage1", L"carrots_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_1
    { 10, 12, 141, 0, L"carrots_stage2", L"carrots_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_2
    { 11, 12, 141, 0, L"carrots_stage3", L"carrots_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 12, 142, 0, L"potatoes_stage0", L"potatoes_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 12, 142, 0, L"potatoes_stage1", L"potatoes_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 12, 142, 0, L"potatoes_stage2", L"potatoes_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 12, 142, 0, L"potatoes_stage3", L"potatoes_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 13,  24, 0, L"sandstone_bottom", L"", SWATCH_REPEAT_ALL },
    {  1, 13,  35, 0, L"cyan_wool", L"wool_colored_cyan", SWATCH_REPEAT_ALL },
    {  2, 13,  35, 0, L"orange_wool", L"wool_colored_orange", SWATCH_REPEAT_ALL },
    {  3, 13, 123, 0, L"redstone_lamp", L"redstone_lamp_off", SWATCH_REPEAT_ALL },
    {  4, 13, 124, 0, L"redstone_lamp_on", L"", SWATCH_REPEAT_ALL },
    {  5, 13,  98, 0, L"chiseled_stone_bricks", L"stonebrick_carved", SWATCH_REPEAT_ALL },
    {  6, 13, 135, 0, L"birch_planks", L"planks_birch", SWATCH_REPEAT_ALL },
    {  7, 13, 145, 0, L"anvil", L"anvil_base", SWATCH_REPEAT_ALL },
    {  8, 13, 145, 0, L"chipped_anvil_top", L"anvil_top_damaged_1", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 13, 130, 0, L"MWO_ender_chest_latch", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 13, 130, 0, L"MWO_ender_chest_top", L"ender_chest_top", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 13, 130, 0, L"MWO_ender_chest_side", L"ender_chest_side", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 13, 130, 0, L"MWO_ender_chest_front", L"ender_chest_front", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 13,  18, 0, L"birch_leaves", L"leaves_birch", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },	// ADD-IN
    { 14, 13, 179, 0, L"red_sandstone", L"red_sandstone_normal", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 15, 13,   9, 0, L"water_still", L"water_still_grey", SWATCH_REPEAT_ALL | SBIT_SYNTHESIZED },   // we really want to use the "water_still_grey", but at least this gives a warning
    {  0, 14, 112, 0, L"nether_bricks", L"nether_brick", SWATCH_REPEAT_ALL },
    {  1, 14,  35, 0, L"light_gray_wool", L"wool_colored_silver", SWATCH_REPEAT_ALL },
    {  2, 14, 115, 0, L"nether_wart_stage0", L"nether_wart_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 14, 115, 0, L"nether_wart_stage1", L"nether_wart_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 14, 115, 0, L"nether_wart_stage2", L"nether_wart_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 14,  24, 0, L"chiseled_sandstone", L"sandstone_chiseled", SWATCH_REPEAT_ALL },
    {  6, 14,  24, 0, L"cut_sandstone", L"sandstone_smooth", SWATCH_REPEAT_ALL },
    {  7, 14, 145, 0, L"anvil_top", L"anvil_top_damaged_0", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 14, 145, 0, L"damaged_anvil_top", L"anvil_top_damaged_2", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 14,  54, 0, L"MWO_double_chest_top_left", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// was unused, ender chest moved to here
    { 10, 14,  54, 0, L"MWO_double_chest_top_right", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// was unused, ender chest moved to here
    { 11, 14, 138, 0, L"beacon", L"", SWATCH_REPEAT_ALL },	// was unused, beacon was moved to here
    { 12, 14, 133, 0, L"emerald_block", L"", SWATCH_REPEAT_ALL },	// was unused, emerald was moved to here
    { 13, 14, 173, 0, L"coal_block", L"", SWATCH_REPEAT_ALL },
    { 14, 14, 149, 0, L"comparator", L"comparator_off", SWATCH_REPEAT_ALL },
    { 15, 14, 150, 0, L"comparator_on", L"", SWATCH_REPEAT_ALL },
    {  0, 15,  50, 0, L"MWO_flattened_torch_top", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened torch top; not used in rendering, but 3D printing uses for composites for torches from above
    {  1, 15,  76, 0, L"MWO_flattened_redstone_torch_top", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened redstone torch top, on; not used in rendering, but 3D printing uses for composites for torches from above
    {  2, 15,  75, 0, L"MWO_flattened_redstone_torch_top_off", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened redstone torch top, off; not used in rendering, but 3D printing uses for composites for torches from above
    {  3, 15,  55, 0, L"MWO_redstone_dust_angled", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for angled redstone wire
    {  4, 15,  55, 0, L"MWO_redstone_dust_three_way", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for three-way redstone wire
    {  5, 15, 151, 0, L"daylight_detector_side", L"", SWATCH_REPEAT_ALL },	// Note that daylight_detector is an alt for Hardtop; it's an alt for top, next line, for Meteor. Ugh.
    {  6, 15, 151, 0, L"daylight_detector_top", L"daylight_detector", SWATCH_REPEAT_ALL },  // alt: Meteor
    {  7, 15, 158, 0, L"dropper_front", L"dropper_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  8, 15, 158, 0, L"dropper_front_vertical", L"", SWATCH_REPEAT_ALL },
    {  9, 15, 170, 0, L"hay_block_side", L"", SWATCH_REPEAT_ALL },
    { 10, 15, 170, 0, L"hay_block_top", L"", SWATCH_REPEAT_ALL },
    { 11, 15, 154, 0, L"hopper_inside", L"", SWATCH_REPEAT_ALL },
    { 12, 15, 154, 0, L"hopper_outside", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 13, 15, 154, 0, L"hopper_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 15, 152, 0, L"redstone_block", L"", SWATCH_REPEAT_ALL },
    { 15, 15,  11, 4, L"lava_still", L"", SWATCH_REPEAT_ALL },
    {  0, 16, 159, 0, L"white_terracotta", L"hardened_clay_stained_white", SWATCH_REPEAT_ALL }, //** Brave new world, off the 256x256 edge
    {  1, 16, 159, 0, L"orange_terracotta", L"hardened_clay_stained_orange", SWATCH_REPEAT_ALL },
    {  2, 16, 159, 0, L"magenta_terracotta", L"hardened_clay_stained_magenta", SWATCH_REPEAT_ALL },
    {  3, 16, 159, 0, L"light_blue_terracotta", L"hardened_clay_stained_light_blue", SWATCH_REPEAT_ALL },
    {  4, 16, 159, 0, L"yellow_terracotta", L"hardened_clay_stained_yellow", SWATCH_REPEAT_ALL },
    {  5, 16, 159, 0, L"lime_terracotta", L"hardened_clay_stained_lime", SWATCH_REPEAT_ALL },
    {  6, 16, 159, 0, L"pink_terracotta", L"hardened_clay_stained_pink", SWATCH_REPEAT_ALL },
    {  7, 16, 159, 0, L"gray_terracotta", L"hardened_clay_stained_gray", SWATCH_REPEAT_ALL },
    {  8, 16, 159, 0, L"light_gray_terracotta", L"hardened_clay_stained_silver", SWATCH_REPEAT_ALL },
    {  9, 16, 159, 0, L"cyan_terracotta", L"hardened_clay_stained_cyan", SWATCH_REPEAT_ALL },
    { 10, 16, 159, 0, L"purple_terracotta", L"hardened_clay_stained_purple", SWATCH_REPEAT_ALL },
    { 11, 16, 159, 0, L"blue_terracotta", L"hardened_clay_stained_blue", SWATCH_REPEAT_ALL },
    { 12, 16, 159, 0, L"brown_terracotta", L"hardened_clay_stained_brown", SWATCH_REPEAT_ALL },
    { 13, 16, 159, 0, L"green_terracotta", L"hardened_clay_stained_green", SWATCH_REPEAT_ALL },
    { 14, 16, 159, 0, L"red_terracotta", L"hardened_clay_stained_red", SWATCH_REPEAT_ALL },
    { 15, 16, 159, 0, L"black_terracotta", L"hardened_clay_stained_black", SWATCH_REPEAT_ALL },
    {  0, 17, 172, 0, L"terracotta", L"hardened_clay", SWATCH_REPEAT_ALL },
    {  1, 17, 155, 0, L"quartz_block_bottom", L"smooth_quartz", SWATCH_REPEAT_ALL },    // despite the name, this is used on all sides of the smooth quartz block and nothing else
    {  2, 17, 155, 0, L"chiseled_quartz_block_top", L"quartz_block_chiseled_top", SWATCH_REPEAT_ALL },
    {  3, 17, 155, 0, L"chiseled_quartz_block", L"quartz_block_chiseled", SWATCH_REPEAT_ALL },
    {  4, 17, 155, 0, L"quartz_pillar_top", L"quartz_block_lines_top", SWATCH_REPEAT_ALL },
    {  5, 17, 155, 0, L"quartz_pillar", L"quartz_block_lines", SWATCH_REPEAT_ALL },
    {  6, 17, 155, 0, L"quartz_block_side", L"", SWATCH_REPEAT_ALL }, // appears to be identical with the next tile; we'll use it as-is
    {  7, 17, 155, 0, L"quartz_block_top", L"", SWATCH_REPEAT_ALL },   // also used for bottom
    {  8, 17, 153, 0, L"nether_quartz_ore", L"quartz_ore", SWATCH_REPEAT_ALL },
    {  9, 17, 157, 0, L"activator_rail", L"rail_activator", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 10, 17, 157, 0, L"activator_rail_on", L"rail_activator_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 11, 17,  28, 0, L"detector_rail_on", L"rail_detector_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 12, 17, 174, 0, L"packed_ice", L"ice_packed", SWATCH_REPEAT_ALL },	// 1.7
    { 13, 17,  12, 0, L"red_sand", L"", SWATCH_REPEAT_ALL },
    { 14, 17,   3, 0, L"podzol_side", L"dirt_podzol_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 15, 17,   3, 0, L"podzol_top", L"dirt_podzol_top", SWATCH_REPEAT_ALL },
    {  0, 18, 175, 0, L"sunflower_back", L"double_plant_sunflower_back", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 18, 175, 0, L"sunflower_front", L"double_plant_sunflower_front", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 18, 175, 0, L"sunflower_bottom", L"double_plant_sunflower_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    {  3, 18, 175, 0, L"sunflower_top", L"double_plant_sunflower_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 18, 175, 0, L"lilac_bottom", L"double_plant_syringa_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },	// lily
    {  5, 18, 175, 0, L"lilac_top", L"double_plant_syringa_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 18, 175, 0, L"tall_grass_bottom", L"double_plant_grass_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  7, 18, 175, 0, L"tall_grass_top", L"double_plant_grass_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  8, 18, 175, 0, L"large_fern_bottom", L"double_plant_fern_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL | SBIT_SYNTHESIZED },
    {  9, 18, 175, 0, L"large_fern_top", L"double_plant_fern_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL | SBIT_SYNTHESIZED },
    { 10, 18, 175, 0, L"rose_bush_bottom", L"double_plant_rose_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 11, 18, 175, 0, L"rose_bush_top", L"double_plant_rose_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 18, 175, 0, L"peony_bottom", L"double_plant_paeonia_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },	// peony
    { 13, 18, 175, 0, L"peony_top", L"double_plant_paeonia_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 18,   6, 0, L"acacia_sapling", L"sapling_acacia", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 18,   6, 0, L"dark_oak_sapling", L"sapling_roofed_oak", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// yes, "roofed"
    {  0, 19,  38, 0, L"blue_orchid", L"flower_blue_orchid", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 19,  38, 0, L"allium", L"flower_allium", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 19,  38, 0, L"azure_bluet", L"flower_houstonia", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// azure bluet
    {  3, 19,  38, 0, L"red_tulip", L"flower_tulip_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 19,  38, 0, L"orange_tulip", L"flower_tulip_orange", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 19,  38, 0, L"white_tulip", L"flower_tulip_white", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 19,  38, 0, L"pink_tulip", L"flower_tulip_pink", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 19,  38, 0, L"oxeye_daisy", L"flower_oxeye_daisy", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 19, 310, 0, L"seagrass", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// 1.13 - was flower_paeonia - no longer used TODO
    {  9, 19, 161, 0, L"acacia_leaves", L"leaves_acacia", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },	// ADD-IN 1.7.2
    { 10, 19, 179, 0, L"cut_red_sandstone", L"red_sandstone_smooth", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 11, 19, 161, 0, L"dark_oak_leaves", L"leaves_big_oak", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },	// ADD-IN 1.7.2
    { 12, 19, 179, 0, L"red_sandstone_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 13, 19, 162, 0, L"acacia_log_top", L"log_acacia_top", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 14, 19,  17, 0, L"dark_oak_log", L"log_big_oak", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 15, 19,  17, 0, L"dark_oak_log_top", L"log_big_oak_top", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    {  0, 20,  95, 0, L"white_stained_glass", L"glass_white", SWATCH_REPEAT_ALL },
    {  1, 20,  95, 0, L"orange_stained_glass", L"glass_orange", SWATCH_REPEAT_ALL },
    {  2, 20,  95, 0, L"magenta_stained_glass", L"glass_magenta", SWATCH_REPEAT_ALL },
    {  3, 20,  95, 0, L"light_blue_stained_glass", L"glass_light_blue", SWATCH_REPEAT_ALL },
    {  4, 20,  95, 0, L"yellow_stained_glass", L"glass_yellow", SWATCH_REPEAT_ALL },
    {  5, 20,  95, 0, L"lime_stained_glass", L"glass_lime", SWATCH_REPEAT_ALL },
    {  6, 20,  95, 0, L"pink_stained_glass", L"glass_pink", SWATCH_REPEAT_ALL },
    {  7, 20,  95, 0, L"gray_stained_glass", L"glass_gray", SWATCH_REPEAT_ALL },
    {  8, 20,  95, 0, L"light_gray_stained_glass", L"glass_silver", SWATCH_REPEAT_ALL },
    {  9, 20,  95, 0, L"cyan_stained_glass", L"glass_cyan", SWATCH_REPEAT_ALL },
    { 10, 20,  95, 0, L"purple_stained_glass", L"glass_purple", SWATCH_REPEAT_ALL },
    { 11, 20,  95, 0, L"blue_stained_glass", L"glass_blue", SWATCH_REPEAT_ALL },
    { 12, 20,  95, 0, L"brown_stained_glass", L"glass_brown", SWATCH_REPEAT_ALL },
    { 13, 20,  95, 0, L"green_stained_glass", L"glass_green", SWATCH_REPEAT_ALL },
    { 14, 20,  95, 0, L"red_stained_glass", L"glass_red", SWATCH_REPEAT_ALL },
    { 15, 20,  95, 0, L"black_stained_glass", L"glass_black", SWATCH_REPEAT_ALL },
    {  0, 21,  95, 0, L"white_stained_glass_pane_top", L"glass_pane_top_white", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  1, 21,  95, 0, L"orange_stained_glass_pane_top", L"glass_pane_top_orange", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  2, 21,  95, 0, L"magenta_stained_glass_pane_top", L"glass_pane_top_magenta", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  3, 21,  95, 0, L"light_blue_stained_glass_pane_top", L"glass_pane_top_light_blue", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  4, 21,  95, 0, L"yellow_stained_glass_pane_top", L"glass_pane_top_yellow", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  5, 21,  95, 0, L"lime_stained_glass_pane_top", L"glass_pane_top_lime", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  6, 21,  95, 0, L"pink_stained_glass_pane_top", L"glass_pane_top_pink", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  7, 21,  95, 0, L"gray_stained_glass_pane_top", L"glass_pane_top_gray", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 21,  95, 0, L"light_gray_stained_glass_pane_top", L"glass_pane_top_silver", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 21,  95, 0, L"cyan_stained_glass_pane_top", L"glass_pane_top_cyan", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 21,  95, 0, L"purple_stained_glass_pane_top", L"glass_pane_top_purple", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 21,  95, 0, L"blue_stained_glass_pane_top", L"glass_pane_top_blue", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 21,  95, 0, L"brown_stained_glass_pane_top", L"glass_pane_top_brown", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 21,  95, 0, L"green_stained_glass_pane_top", L"glass_pane_top_green", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 21,  95, 0, L"red_stained_glass_pane_top", L"glass_pane_top_red", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 21,  95, 0, L"black_stained_glass_pane_top", L"glass_pane_top_black", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  0, 22, 163, 0, L"acacia_planks", L"planks_acacia", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    {  1, 22, 164, 0, L"dark_oak_planks", L"planks_big_oak", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    {  2, 22, 167, 0, L"iron_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// 1.8
    {  3, 22, 165, 0, L"slime_block", L"slime", SWATCH_REPEAT_ALL },
    {  4, 22, 329, 0, L"andesite", L"stone_andesite", SWATCH_REPEAT_ALL },
    {  5, 22, 373, 0, L"polished_andesite", L"andesite_smooth", SWATCH_REPEAT_ALL },
    {  6, 22, 369, 0, L"diorite", L"stone_diorite", SWATCH_REPEAT_ALL },
    {  7, 22, 370, 0, L"polished_diorite", L"diorite_smooth", SWATCH_REPEAT_ALL },
    {  8, 22, 366, 0, L"granite", L"stone_granite", SWATCH_REPEAT_ALL },    // I've also seen stone_granite be something different, in Vanilla-Normals-Renewed-master
    {  9, 22, 367, 0, L"polished_granite", L"stone_granite_smooth", SWATCH_REPEAT_ALL },
    { 10, 22, 258, 0, L"prismarine_bricks", L"", SWATCH_REPEAT_ALL },
    { 11, 22, 259, 0, L"dark_prismarine", L"prismarine_dark", SWATCH_REPEAT_ALL },
    { 12, 22, 168, 0, L"prismarine", L"prismarine_rough", SWATCH_REPEAT_ALL },
    { 13, 22, 178, 0, L"daylight_detector_inverted_top", L"", SWATCH_REPEAT_ALL },
    { 14, 22, 169, 0, L"sea_lantern", L"", SWATCH_REPEAT_ALL },
    { 15, 22,  19, 0, L"wet_sponge", L"sponge_wet", SWATCH_REPEAT_ALL },
    {  0, 23, 193, 0, L"spruce_door_bottom", L"door_spruce_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  1, 23, 193, 0, L"spruce_door_top", L"door_spruce_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	    // this particular one does not need SBIT_DECAL normally, but mods could use it
    {  2, 23, 194, 0, L"birch_door_bottom",  L"door_birch_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  3, 23, 194, 0, L"birch_door_top", L"door_birch_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },		// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  4, 23, 195, 0, L"jungle_door_bottom", L"door_jungle_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  5, 23, 195, 0, L"jungle_door_top", L"door_jungle_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  6, 23, 196, 0, L"acacia_door_bottom", L"door_acacia_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  7, 23, 196, 0, L"acacia_door_top", L"door_acacia_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  8, 23, 197, 0, L"dark_oak_door_bottom", L"door_dark_oak_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  9, 23, 197, 0, L"dark_oak_door_top", L"door_dark_oak_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    { 10, 23, 311, 0, L"smooth_stone", L"smooth_stone_top", SWATCH_REPEAT_ALL },	// now reused for 1.14 - was top of banner; NOTE: this looks a heckuva lot like "stone_slab_top" - which gets used? This one, so it's used for 6,0
    { 11, 23, 311, 0, L"smooth_stone_slab_side", L"", SWATCH_REPEAT_ALL },	// now reused for 1.14 - was bottom of banner; NOTE: this looks a heckuva lot like "stone_slab_side" - which gets used? This one, so it's used for 5,0
    { 12, 23, 198, 0, L"end_rod", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 23, 199, 0, L"chorus_plant", L"", SWATCH_REPEAT_ALL },
    { 14, 23, 200, 0, L"chorus_flower", L"", SWATCH_REPEAT_ALL },
    { 15, 23, 200, 0, L"chorus_flower_dead", L"", SWATCH_REPEAT_ALL },
    {  0, 24, 201, 0, L"purpur_block", L"", SWATCH_REPEAT_ALL },
    {  1, 24, 201, 0, L"purpur_pillar", L"", SWATCH_REPEAT_ALL },
    {  2, 24, 202, 0, L"purpur_pillar_top", L"", SWATCH_REPEAT_ALL },
    {  3, 24, 206, 0, L"end_stone_bricks", L"end_bricks", SWATCH_REPEAT_ALL },
    {  4, 24, 207, 0, L"beetroots_stage0", L"beetroots_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 24, 207, 0, L"beetroots_stage1", L"beetroots_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 24, 207, 0, L"beetroots_stage2", L"beetroots_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 24, 207, 0, L"beetroots_stage3", L"beetroots_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 24, 208, 0, L"dirt_path_top", L"grass_path_top", SWATCH_REPEAT_ALL }, // note that in 1.16 and earlier this was called grass_path_top. Name and texture changed for some reason. In 1.20 these are new textures, but are identical.
    {  9, 24, 208, 0, L"dirt_path_side", L"grass_path_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_ALPHA_OVERLAY }, // in 1.20, separate textures, but different! 1.20.1 uses dirt_path_side.png
    { 10, 24, 137, 0, L"command_block_front", L"", SWATCH_REPEAT_ALL },
    { 11, 24, 210, 0, L"command_block_back", L"", SWATCH_REPEAT_ALL },  // also "commandBlock", but no room...
    { 12, 24, 210, 0, L"command_block_side", L"", SWATCH_REPEAT_ALL },
    { 13, 24, 210, 0, L"command_block_conditional", L"", SWATCH_REPEAT_ALL },
    { 14, 24, 210, 0, L"repeating_command_block_front", L"", SWATCH_REPEAT_ALL },
    { 15, 24, 210, 0, L"repeating_command_block_back", L"", SWATCH_REPEAT_ALL },
    {  0, 25, 210, 0, L"repeating_command_block_side", L"", SWATCH_REPEAT_ALL },
    {  1, 25, 210, 0, L"repeating_command_block_conditional", L"", SWATCH_REPEAT_ALL },
    {  2, 25, 211, 0, L"chain_command_block_front", L"", SWATCH_REPEAT_ALL },
    {  3, 25, 211, 0, L"chain_command_block_back", L"", SWATCH_REPEAT_ALL },
    {  4, 25, 211, 0, L"chain_command_block_side", L"", SWATCH_REPEAT_ALL },
    {  5, 25, 211, 0, L"chain_command_block_conditional", L"", SWATCH_REPEAT_ALL },
    {  6, 25, 212, 0, L"frosted_ice_0", L"", SWATCH_REPEAT_ALL },
    {  7, 25, 212, 0, L"frosted_ice_1", L"", SWATCH_REPEAT_ALL },
    {  8, 25, 212, 0, L"frosted_ice_2", L"", SWATCH_REPEAT_ALL },
    {  9, 25, 212, 0, L"frosted_ice_3", L"", SWATCH_REPEAT_ALL },
    { 10, 25, 255, 0, L"structure_block_corner", L"", SWATCH_REPEAT_ALL },
    { 11, 25, 255, 0, L"structure_block_data", L"", SWATCH_REPEAT_ALL },
    { 12, 25, 255, 0, L"structure_block_load", L"", SWATCH_REPEAT_ALL },
    { 13, 25, 255, 0, L"structure_block_save", L"", SWATCH_REPEAT_ALL },
    { 14, 25, 166, 0, L"barrier", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// TODO: extract more directly from .jar, as this is currently in assets\minecraft\textures\item
    { 15, 25,   9, 0, L"water_overlay", L"", SWATCH_REPEAT_ALL | SBIT_SYNTHESIZED },    // 1.9 - water looks like this through glass.
    {  0, 26, 213, 0, L"magma", L"", SWATCH_REPEAT_ALL },
    {  1, 26, 214, 0, L"nether_wart_block", L"", SWATCH_REPEAT_ALL },
    {  2, 26, 215, 0, L"red_nether_bricks", L"red_nether_brick", SWATCH_REPEAT_ALL },
    {  3, 26, 216, 0, L"bone_block_side", L"", SWATCH_REPEAT_ALL },
    {  4, 26, 216, 0, L"bone_block_top", L"", SWATCH_REPEAT_ALL },
    {  5, 26,  55, 0, L"redstone_dust_overlay", L"", SWATCH_REPEAT_ALL | SBIT_ALPHA_OVERLAY },	// could use alternate name such as redstone_dust_cross_overlay if old texture pack, but Modern HD does weird stuff with it
    {  6, 26,  55, 0, L"MWO_redstone_dust_four_way", L"redstone_dust_cross", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED 4 way redstone wire - reserved (alt: Smoolistic)
    {  7, 26,  54, 0, L"MWO_chest_latch", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 26,   8, 0, L"water_flow", L"water_flow_grey", SWATCH_REPEAT_ALL | SBIT_SYNTHESIZED },	// special: double-wide. TODO: some packs make "water_flow" a colored version, using "water_flow_grey" for the right one
    {  9, 26,  10, 4, L"lava_flow", L"", SWATCH_REPEAT_ALL },		// special: double-wide
    { 10, 26,  55, 0, L"MWO_redstone_dust_line0_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_VERT_OFF: vertical and runs north-south
    { 11, 26,  55, 0, L"MWO_redstone_dust_line1_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_HORIZ_OFF: "horizontal": appears vertically in the texture, but runs east-west
    { 12, 26,  55, 0, L"MWO_redstone_dust_dot_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_DOT_OFF
    { 13, 26,  55, 0, L"MWO_redstone_dust_angled_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_ANGLED_2_OFF
    { 14, 26,  55, 0, L"MWO_redstone_dust_three_way_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_3_OFF
    { 15, 26,  55, 0, L"MWO_redstone_dust_four_way_off", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_4_OFF
    {  0, 27, 219, 0, L"white_shulker_box", L"shulker_top_white", SWATCH_REPEAT_ALL },
    {  1, 27, 220, 0, L"orange_shulker_box", L"shulker_top_orange", SWATCH_REPEAT_ALL },
    {  2, 27, 221, 0, L"magenta_shulker_box", L"shulker_top_magenta", SWATCH_REPEAT_ALL },
    {  3, 27, 222, 0, L"light_blue_shulker_box", L"shulker_top_light_blue", SWATCH_REPEAT_ALL },
    {  4, 27, 223, 0, L"yellow_shulker_box", L"shulker_top_yellow", SWATCH_REPEAT_ALL },
    {  5, 27, 224, 0, L"lime_shulker_box", L"shulker_top_lime", SWATCH_REPEAT_ALL },
    {  6, 27, 225, 0, L"pink_shulker_box", L"shulker_top_pink", SWATCH_REPEAT_ALL },
    {  7, 27, 226, 0, L"gray_shulker_box", L"shulker_top_gray", SWATCH_REPEAT_ALL },
    {  8, 27, 227, 0, L"light_gray_shulker_box", L"shulker_top_silver", SWATCH_REPEAT_ALL },
    {  9, 27, 228, 0, L"cyan_shulker_box", L"shulker_top_cyan", SWATCH_REPEAT_ALL },
    { 10, 27, 229, 0, L"purple_shulker_box", L"shulker_top_purple", SWATCH_REPEAT_ALL },
    { 11, 27, 230, 0, L"blue_shulker_box", L"shulker_top_blue", SWATCH_REPEAT_ALL },
    { 12, 27, 231, 0, L"brown_shulker_box", L"shulker_top_brown", SWATCH_REPEAT_ALL },
    { 13, 27, 232, 0, L"green_shulker_box", L"shulker_top_green", SWATCH_REPEAT_ALL },
    { 14, 27, 233, 0, L"red_shulker_box", L"shulker_top_red", SWATCH_REPEAT_ALL },
    { 15, 27, 234, 0, L"black_shulker_box", L"shulker_top_black", SWATCH_REPEAT_ALL },
    {  0, 28, 235, 0, L"white_glazed_terracotta", L"glazed_terracotta_white", SWATCH_REPEAT_ALL },
    {  1, 28, 236, 0, L"orange_glazed_terracotta", L"glazed_terracotta_orange", SWATCH_REPEAT_ALL },
    {  2, 28, 237, 0, L"magenta_glazed_terracotta", L"glazed_terracotta_magenta", SWATCH_REPEAT_ALL },
    {  3, 28, 238, 0, L"light_blue_glazed_terracotta", L"glazed_terracotta_light_blue", SWATCH_REPEAT_ALL },
    {  4, 28, 239, 0, L"yellow_glazed_terracotta", L"glazed_terracotta_yellow", SWATCH_REPEAT_ALL },
    {  5, 28, 240, 0, L"lime_glazed_terracotta", L"glazed_terracotta_lime", SWATCH_REPEAT_ALL },
    {  6, 28, 241, 0, L"pink_glazed_terracotta", L"glazed_terracotta_pink", SWATCH_REPEAT_ALL },
    {  7, 28, 242, 0, L"gray_glazed_terracotta", L"glazed_terracotta_gray", SWATCH_REPEAT_ALL },
    {  8, 28, 243, 0, L"light_gray_glazed_terracotta", L"glazed_terracotta_silver", SWATCH_REPEAT_ALL },
    {  9, 28, 244, 0, L"cyan_glazed_terracotta", L"glazed_terracotta_cyan", SWATCH_REPEAT_ALL },
    { 10, 28, 245, 0, L"purple_glazed_terracotta", L"glazed_terracotta_purple", SWATCH_REPEAT_ALL },
    { 11, 28, 246, 0, L"blue_glazed_terracotta", L"glazed_terracotta_blue", SWATCH_REPEAT_ALL },
    { 12, 28, 247, 0, L"brown_glazed_terracotta", L"glazed_terracotta_brown", SWATCH_REPEAT_ALL },
    { 13, 28, 248, 0, L"green_glazed_terracotta", L"glazed_terracotta_green", SWATCH_REPEAT_ALL },
    { 14, 28, 249, 0, L"red_glazed_terracotta", L"glazed_terracotta_red", SWATCH_REPEAT_ALL },
    { 15, 28, 250, 0, L"black_glazed_terracotta", L"glazed_terracotta_black", SWATCH_REPEAT_ALL },
    {  0, 29, 176, 0, L"white_concrete", L"concrete_white", SWATCH_REPEAT_ALL },
    {  1, 29, 279, 0, L"orange_concrete", L"concrete_orange", SWATCH_REPEAT_ALL },
    {  2, 29, 280, 0, L"magenta_concrete", L"concrete_magenta", SWATCH_REPEAT_ALL },
    {  3, 29, 281, 0, L"light_blue_concrete", L"concrete_light_blue", SWATCH_REPEAT_ALL },
    {  4, 29, 282, 0, L"yellow_concrete", L"concrete_yellow", SWATCH_REPEAT_ALL },
    {  5, 29, 283, 0, L"lime_concrete", L"concrete_lime", SWATCH_REPEAT_ALL },
    {  6, 29, 284, 0, L"pink_concrete", L"concrete_pink", SWATCH_REPEAT_ALL },
    {  7, 29, 285, 0, L"gray_concrete", L"concrete_gray", SWATCH_REPEAT_ALL },
    {  8, 29, 286, 0, L"light_gray_concrete", L"concrete_silver", SWATCH_REPEAT_ALL },
    {  9, 29, 287, 0, L"cyan_concrete", L"concrete_cyan", SWATCH_REPEAT_ALL },
    { 10, 29, 288, 0, L"purple_concrete", L"concrete_purple", SWATCH_REPEAT_ALL },
    { 11, 29, 289, 0, L"blue_concrete", L"concrete_blue", SWATCH_REPEAT_ALL },
    { 12, 29, 290, 0, L"brown_concrete", L"concrete_brown", SWATCH_REPEAT_ALL },
    { 13, 29, 291, 0, L"green_concrete", L"concrete_green", SWATCH_REPEAT_ALL },
    { 14, 29, 292, 0, L"red_concrete", L"concrete_red", SWATCH_REPEAT_ALL },
    { 15, 29, 293, 0, L"black_concrete", L"concrete_black", SWATCH_REPEAT_ALL },
    {  0, 30, 252, 0, L"white_concrete_powder", L"concrete_powder_white", SWATCH_REPEAT_ALL },
    {  1, 30, 252, 0, L"orange_concrete_powder", L"concrete_powder_orange", SWATCH_REPEAT_ALL },
    {  2, 30, 252, 0, L"magenta_concrete_powder", L"concrete_powder_magenta", SWATCH_REPEAT_ALL },
    {  3, 30, 252, 0, L"light_blue_concrete_powder", L"concrete_powder_light_blue", SWATCH_REPEAT_ALL },
    {  4, 30, 252, 0, L"yellow_concrete_powder", L"concrete_powder_yellow", SWATCH_REPEAT_ALL },
    {  5, 30, 252, 0, L"lime_concrete_powder", L"concrete_powder_lime", SWATCH_REPEAT_ALL },
    {  6, 30, 252, 0, L"pink_concrete_powder", L"concrete_powder_pink", SWATCH_REPEAT_ALL },
    {  7, 30, 252, 0, L"gray_concrete_powder", L"concrete_powder_gray", SWATCH_REPEAT_ALL },
    {  8, 30, 252, 0, L"light_gray_concrete_powder", L"concrete_powder_silver", SWATCH_REPEAT_ALL },
    {  9, 30, 252, 0, L"cyan_concrete_powder", L"concrete_powder_cyan", SWATCH_REPEAT_ALL },
    { 10, 30, 252, 0, L"purple_concrete_powder", L"concrete_powder_purple", SWATCH_REPEAT_ALL },
    { 11, 30, 252, 0, L"blue_concrete_powder", L"concrete_powder_blue", SWATCH_REPEAT_ALL },
    { 12, 30, 252, 0, L"brown_concrete_powder", L"concrete_powder_brown", SWATCH_REPEAT_ALL },
    { 13, 30, 252, 0, L"green_concrete_powder", L"concrete_powder_green", SWATCH_REPEAT_ALL },
    { 14, 30, 252, 0, L"red_concrete_powder", L"concrete_powder_red", SWATCH_REPEAT_ALL },
    { 15, 30, 252, 0, L"black_concrete_powder", L"concrete_powder_black", SWATCH_REPEAT_ALL },
    {  0, 31, 219, 0, L"shulker_side_white", L"", SWATCH_REPEAT_ALL },    // optional tiles - BD Craft has them, for example
    {  1, 31, 220, 0, L"shulker_side_orange", L"", SWATCH_REPEAT_ALL },
    {  2, 31, 221, 0, L"shulker_side_magenta", L"", SWATCH_REPEAT_ALL },
    {  3, 31, 222, 0, L"shulker_side_light_blue", L"", SWATCH_REPEAT_ALL },
    {  4, 31, 223, 0, L"shulker_side_yellow", L"", SWATCH_REPEAT_ALL },
    {  5, 31, 224, 0, L"shulker_side_lime", L"", SWATCH_REPEAT_ALL },
    {  6, 31, 225, 0, L"shulker_side_pink", L"", SWATCH_REPEAT_ALL },
    {  7, 31, 226, 0, L"shulker_side_gray", L"", SWATCH_REPEAT_ALL },
    {  8, 31, 227, 0, L"shulker_side_silver", L"", SWATCH_REPEAT_ALL },
    {  9, 31, 228, 0, L"shulker_side_cyan", L"", SWATCH_REPEAT_ALL },
    { 10, 31, 229, 0, L"shulker_side_purple", L"", SWATCH_REPEAT_ALL },
    { 11, 31, 230, 0, L"shulker_side_blue", L"", SWATCH_REPEAT_ALL },
    { 12, 31, 231, 0, L"shulker_side_brown", L"", SWATCH_REPEAT_ALL },
    { 13, 31, 232, 0, L"shulker_side_green", L"", SWATCH_REPEAT_ALL },
    { 14, 31, 233, 0, L"shulker_side_red", L"", SWATCH_REPEAT_ALL },
    { 15, 31, 234, 0, L"shulker_side_black", L"", SWATCH_REPEAT_ALL },
    {  0, 32, 219, 0, L"shulker_bottom_white", L"", SWATCH_REPEAT_ALL },    // optional tiles - BD Craft has them, for example
    {  1, 32, 220, 0, L"shulker_bottom_orange", L"", SWATCH_REPEAT_ALL },
    {  2, 32, 221, 0, L"shulker_bottom_magenta", L"", SWATCH_REPEAT_ALL },
    {  3, 32, 222, 0, L"shulker_bottom_light_blue", L"", SWATCH_REPEAT_ALL },
    {  4, 32, 223, 0, L"shulker_bottom_yellow", L"", SWATCH_REPEAT_ALL },
    {  5, 32, 224, 0, L"shulker_bottom_lime", L"", SWATCH_REPEAT_ALL },
    {  6, 32, 225, 0, L"shulker_bottom_pink", L"", SWATCH_REPEAT_ALL },
    {  7, 32, 226, 0, L"shulker_bottom_gray", L"", SWATCH_REPEAT_ALL },
    {  8, 32, 227, 0, L"shulker_bottom_silver", L"", SWATCH_REPEAT_ALL },
    {  9, 32, 228, 0, L"shulker_bottom_cyan", L"", SWATCH_REPEAT_ALL },
    { 10, 32, 229, 0, L"shulker_bottom_purple", L"", SWATCH_REPEAT_ALL },
    { 11, 32, 230, 0, L"shulker_bottom_blue", L"", SWATCH_REPEAT_ALL },
    { 12, 32, 231, 0, L"shulker_bottom_brown", L"", SWATCH_REPEAT_ALL },
    { 13, 32, 232, 0, L"shulker_bottom_green", L"", SWATCH_REPEAT_ALL },
    { 14, 32, 233, 0, L"shulker_bottom_red", L"", SWATCH_REPEAT_ALL },
    { 15, 32, 234, 0, L"shulker_bottom_black", L"", SWATCH_REPEAT_ALL },
    {  0, 33, 218, 0, L"observer_back", L"", SWATCH_REPEAT_ALL },
    {  1, 33, 218, 0, L"observer_back_on", L"observer_back_lit", SWATCH_REPEAT_ALL },
    {  2, 33, 218, 0, L"observer_front", L"", SWATCH_REPEAT_ALL },
    {  3, 33, 218, 0, L"observer_side", L"", SWATCH_REPEAT_ALL },
    {  4, 33, 218, 0, L"observer_top", L"", SWATCH_REPEAT_ALL },   // alternate name is Sphax BD Craft
    {  5, 33, 219, 0, L"MW_SHULKER_SIDE", L"MW_shulker_side", SWATCH_REPEAT_ALL },
    {  6, 33, 219, 0, L"MW_SHULKER_BOTTOM", L"MW_shulker_bottom", SWATCH_REPEAT_ALL },
    {  7, 33, 313, 0, L"dried_kelp_top", L"", SWATCH_REPEAT_ALL },  // 1.13 starts here
    {  8, 33, 313, 0, L"dried_kelp_side", L"dried_kelp_side_a", SWATCH_REPEAT_ALL },    // alt bedrock; there's also a side_b
    {  9, 33, 313, 0, L"dried_kelp_bottom", L"", SWATCH_REPEAT_ALL },
    { 10, 33, 314, 0, L"kelp", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11, 33, 314, 0, L"kelp_plant", L"", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 12, 33, 323, 0, L"sea_pickle", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 33, 312, 0, L"blue_ice", L"", SWATCH_REPEAT_ALL },
    { 14, 33, 309, 0, L"tall_seagrass_bottom", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL }, // seagrass_doubletall_bottom in Muddle, but in TGA
    { 15, 33, 310, 0, L"tall_seagrass_top", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },  // just clamp bottom. Not clamping the top lets it properly be transparent and taper off
    {  0, 34, 277, 0, L"stripped_oak_log", L"", SWATCH_REPEAT_ALL },
    {  1, 34, 277, 0, L"stripped_spruce_log", L"", SWATCH_REPEAT_ALL },
    {  2, 34, 277, 0, L"stripped_birch_log", L"", SWATCH_REPEAT_ALL },
    {  3, 34, 277, 0, L"stripped_jungle_log", L"", SWATCH_REPEAT_ALL },
    {  4, 34, 278, 0, L"stripped_acacia_log", L"", SWATCH_REPEAT_ALL },
    {  5, 34, 278, 0, L"stripped_dark_oak_log", L"", SWATCH_REPEAT_ALL },
    {  6, 34, 275, 0, L"stripped_oak_log_top", L"", SWATCH_REPEAT_ALL },
    {  7, 34, 275, 0, L"stripped_spruce_log_top", L"", SWATCH_REPEAT_ALL },
    {  8, 34, 275, 0, L"stripped_birch_log_top", L"", SWATCH_REPEAT_ALL },
    {  9, 34, 275, 0, L"stripped_jungle_log_top", L"", SWATCH_REPEAT_ALL },
    { 10, 34, 276, 0, L"stripped_acacia_log_top", L"", SWATCH_REPEAT_ALL },
    { 11, 34, 276, 0, L"stripped_dark_oak_log_top", L"", SWATCH_REPEAT_ALL },
    { 12, 34, 260, 0, L"spruce_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    { 13, 34, 261, 0, L"birch_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    { 14, 34, 262, 0, L"jungle_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 15, 34, 263, 0, L"acacia_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  0, 35, 264, 0, L"dark_oak_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// this particular one does not need SBIT_DECAL normally, but mods could use it
    {  1, 35, 316, 0, L"dead_tube_coral_block", L"coral_blue_dead", SWATCH_REPEAT_ALL },
    {  2, 35, 316, 0, L"dead_brain_coral_block", L"coral_pink_dead", SWATCH_REPEAT_ALL },
    {  3, 35, 316, 0, L"dead_bubble_coral_block", L"coral_purple_dead", SWATCH_REPEAT_ALL },
    {  4, 35, 316, 0, L"dead_fire_coral_block", L"coral_red_dead", SWATCH_REPEAT_ALL },
    {  5, 35, 316, 0, L"dead_horn_coral_block", L"coral_yellow_dead", SWATCH_REPEAT_ALL },
    {  6, 35, 315, 0, L"tube_coral_block", L"coral_blue", SWATCH_REPEAT_ALL },
    {  7, 35, 315, 0, L"brain_coral_block", L"coral_pink", SWATCH_REPEAT_ALL },
    {  8, 35, 315, 0, L"bubble_coral_block", L"coral_purple", SWATCH_REPEAT_ALL },
    {  9, 35, 315, 0, L"fire_coral_block", L"coral_red", SWATCH_REPEAT_ALL },
    { 10, 35, 315, 0, L"horn_coral_block", L"coral_yellow", SWATCH_REPEAT_ALL },
    { 11, 35, 317, 0, L"tube_coral", L"coral_plant_blue", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 35, 317, 0, L"brain_coral", L"coral_plant_pink", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 35, 317, 0, L"bubble_coral", L"coral_plant_purple", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 35, 317, 0, L"fire_coral", L"coral_plant_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 35, 317, 0, L"horn_coral", L"coral_plant_yellow", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 36, 318, 0, L"tube_coral_fan", L"coral_fan_blue", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 36, 318, 0, L"brain_coral_fan", L"coral_fan_pink", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 36, 318, 0, L"bubble_coral_fan", L"coral_fan_purple", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 36, 318, 0, L"fire_coral_fan", L"coral_fan_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 36, 318, 0, L"horn_coral_fan", L"coral_fan_yellow", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 36, 319, 0, L"dead_tube_coral_fan", L"coral_fan_blue_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 36, 319, 0, L"dead_brain_coral_fan", L"coral_fan_pink_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 36, 319, 0, L"dead_bubble_coral_fan", L"coral_fan_purple_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 36, 319, 0, L"dead_fire_coral_fan", L"coral_fan_red_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  9, 36, 319, 0, L"dead_horn_coral_fan", L"coral_fan_yellow_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 36, 324, 0, L"turtle_egg", L"turtle_egg_not_cracked", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 36, 324, 0, L"turtle_egg_slightly_cracked", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 36, 324, 0, L"turtle_egg_very_cracked", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 36, 322, 0, L"conduit", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 36, 325, 0, L"dead_tube_coral", L"coral_plant_blue_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 36, 325, 0, L"dead_brain_coral", L"coral_plant_pink_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 37, 325, 0, L"dead_bubble_coral", L"coral_plant_purple_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 37, 325, 0, L"dead_fire_coral", L"coral_plant_red_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 37, 325, 0, L"dead_horn_coral", L"coral_plant_yellow_dead", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 37,  38, 0, L"cornflower", L"flower_cornflower", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 37,  38, 0, L"lily_of_the_valley", L"flower_lily_of_the_valley", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 37,  38, 0, L"wither_rose", L"flower_wither_rose", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 37, 328, 0, L"bamboo_large_leaves", L"bamboo_leaf", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 37, 328, 0, L"bamboo_singleleaf", L"bamboo_single_leaf", SBIT_CLAMP_BOTTOM | SBIT_DECAL },    // alt: Hardtop
    {  8, 37, 328, 0, L"bamboo_small_leaves", L"bamboo_small_leaf", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  9, 37, 328, 0, L"bamboo_stage0", L"bamboo_sapling", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// X decal
    { 10, 37, 328, 0, L"bamboo_stalk", L"bamboo_stem", SWATCH_CLAMP_ALL },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    { 11, 37, 338, 0, L"lantern", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 37, 327, 0, L"sweet_berry_bush_stage0", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 37, 327, 0, L"sweet_berry_bush_stage1", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 37, 327, 0, L"sweet_berry_bush_stage2", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 37, 327, 0, L"sweet_berry_bush_stage3", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 38, 333, 0, L"barrel_top", L"", SWATCH_REPEAT_ALL },
    {  1, 38, 333, 0, L"barrel_side", L"", SWATCH_REPEAT_ALL },
    {  2, 38, 333, 0, L"barrel_bottom", L"", SWATCH_REPEAT_ALL },
    {  3, 38, 333, 0, L"barrel_top_open", L"", SWATCH_REPEAT_ALL },
    {  4, 38, 337, 0, L"bell_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  5, 38, 337, 0, L"bell_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  6, 38, 337, 0, L"bell_bottom", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  7, 38,  61, 0, L"blast_furnace_top", L"", SWATCH_REPEAT_ALL },
    {  8, 38,  61, 0, L"blast_furnace_side", L"", SWATCH_REPEAT_ALL },
    {  9, 38,  61, 0, L"blast_furnace_front", L"blast_furnace_front_off", SWATCH_REPEAT_ALL },
    { 10, 38,  62, 0, L"blast_furnace_front_on", L"", SWATCH_REPEAT_ALL },
    { 11, 38, 332, 0, L"composter_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 38, 332, 0, L"composter_side", L"", SWATCH_REPEAT_ALL },
    { 13, 38, 332, 0, L"composter_bottom", L"", SWATCH_REPEAT_ALL },
    { 14, 38, 332, 0, L"composter_compost", L"compost", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },  // Alternate name from Muddle
    { 15, 38, 332, 0, L"composter_ready", L"compost_ready", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },  // Alternate name from Muddle
    {  0, 39, 339, 0, L"campfire_fire", L"campfire", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 39, 339, 0, L"campfire_log", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  2, 39, 339, 4, L"campfire_log_lit", L"", SBIT_CLAMP_BOTTOM },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  3, 39,  58, 0, L"cartography_table_top", L"cartography_top", SWATCH_REPEAT_ALL },
    {  4, 39,  58, 0, L"cartography_table_side1", L"cartography_sides", SWATCH_REPEAT_ALL },
    {  5, 39,  58, 0, L"cartography_table_side2", L"cartography_sides", SWATCH_REPEAT_ALL },
    {  6, 39,  58, 0, L"cartography_table_side3", L"cartography_sides", SWATCH_REPEAT_ALL },
    {  7, 39,  58, 0, L"fletching_table_top", L"", SWATCH_REPEAT_ALL },
    {  8, 39,  58, 0, L"fletching_table_side", L"", SWATCH_REPEAT_ALL },
    {  9, 39,  58, 0, L"fletching_table_front", L"", SWATCH_REPEAT_ALL },
    { 10, 39, 335, 0, L"grindstone_side", L"grindstone", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY }, // alternate name used by KOW, I think
    { 11, 39, 335, 0, L"grindstone_pivot", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 39, 335, 0, L"grindstone_round", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 39, 331, 0, L"jigsaw_top", L"", SWATCH_REPEAT_ALL },
    { 14, 39, 331, 0, L"jigsaw_bottom", L"", SWATCH_REPEAT_ALL },	// usually we go top/side/bottom - for command-block sorts of things, we go front/back/side, which is named top/bottom/side here
    { 15, 39, 331, 0, L"jigsaw_side", L"", SWATCH_REPEAT_ALL },
    {  0, 40, 336, 0, L"lectern_top", L"", SWATCH_CLAMP_ALL },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  1, 40, 336, 0, L"lectern_sides", L"", SWATCH_CLAMP_ALL },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  2, 40, 336, 0, L"lectern_base", L"", SWATCH_CLAMP_ALL },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  3, 40, 336, 0, L"lectern_front", L"", SWATCH_CLAMP_ALL },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  4, 40,  61, 0, L"loom_top", L"", SWATCH_REPEAT_ALL },
    {  5, 40,  61, 0, L"loom_side", L"", SWATCH_REPEAT_ALL },
    {  6, 40,  61, 0, L"loom_bottom", L"", SWATCH_REPEAT_ALL },
    {  7, 40,  61, BIT_16, L"loom_front", L"", SWATCH_REPEAT_ALL },
    {  8, 40, 340, 0, L"scaffolding_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 40, 340, 0, L"scaffolding_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 40, 340, 0, L"scaffolding_bottom", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 40,  61, 0, L"smoker_top", L"", SWATCH_REPEAT_ALL },
    { 12, 40,  61, 0, L"smoker_side", L"", SWATCH_REPEAT_ALL },
    { 13, 40,  61, 0, L"smoker_bottom", L"", SWATCH_REPEAT_ALL },
    { 14, 40,  61, 0, L"smoker_front", L"smoker_front_off", SWATCH_REPEAT_ALL },
    { 15, 40,  62, 0, L"smoker_front_on", L"", SWATCH_REPEAT_ALL },
    {  0, 41,  58, 0, L"smithing_table_top", L"", SWATCH_REPEAT_ALL },
    {  1, 41,  58, 0, L"smithing_table_side", L"", SWATCH_REPEAT_ALL },
    {  2, 41,  58, 0, L"smithing_table_bottom", L"", SWATCH_REPEAT_ALL },
    {  3, 41,  58, 0, L"smithing_table_front", L"", SWATCH_REPEAT_ALL },
    {  4, 41, 334, 0, L"stonecutter_top", L"", SWATCH_REPEAT_ALL },
    {  5, 41, 334, 0, L"stonecutter_side", L"", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    {  6, 41, 334, 0, L"stonecutter_bottom", L"", SWATCH_REPEAT_ALL },
    {  7, 41, 334, 0, L"stonecutter_saw", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 41, 341, 0, L"bee_nest_bottom", L"", SWATCH_REPEAT_ALL },
    {  9, 41, 341, 0, L"bee_nest_front", L"", SWATCH_REPEAT_ALL },
    { 10, 41, 341, 0, L"bee_nest_front_honey", L"", SWATCH_REPEAT_ALL },
    { 11, 41, 341, 0, L"bee_nest_side", L"", SWATCH_REPEAT_ALL },
    { 12, 41, 341, 0, L"bee_nest_top", L"", SWATCH_REPEAT_ALL },
    { 13, 41, 341, 0, L"beehive_end", L"beehive_top", SWATCH_REPEAT_ALL },  // alternate from Homa
    { 14, 41, 341, 0, L"beehive_front", L"", SWATCH_REPEAT_ALL },
    { 15, 41, 341, 0, L"beehive_front_honey", L"", SWATCH_REPEAT_ALL },
    {  0, 42, 341, 0, L"beehive_side", L"", SWATCH_REPEAT_ALL },
    {  1, 42, 342, 0, L"honey_block_bottom", L"honey_bottom", SWATCH_REPEAT_ALL },
    {  2, 42, 342, 0, L"honey_block_side", L"honey_side", SWATCH_REPEAT_ALL },
    {  3, 42, 342, 0, L"honey_block_top", L"honey_top", SWATCH_REPEAT_ALL },
    {  4, 42, 343, 0, L"honeycomb_block", L"honeycomb", SWATCH_REPEAT_ALL },
    {  5, 42, 156, 0, L"quartz_bricks", L"quartz_brick_side", SWATCH_REPEAT_ALL },  // 1.16, jg-rtx alt.
    {  6, 42,  88, 0, L"soul_soil", L"", SWATCH_REPEAT_ALL },
    {  7, 42,   1, 0, L"basalt_top", L"", SWATCH_REPEAT_ALL },
    {  8, 42,   1, 0, L"basalt_side", L"", SWATCH_REPEAT_ALL },
    {  9, 42, 367, 0, L"polished_basalt_top", L"", SWATCH_REPEAT_ALL },
    { 10, 42, 367, 0, L"polished_basalt_side", L"", SWATCH_REPEAT_ALL },
    { 11, 42, 362, 0, L"soul_torch", L"soul_fire_torch", SBIT_CLAMP_BOTTOM | SBIT_DECAL }, // second name from an earlier beta
    { 12, 42,  51, BIT_16, L"soul_fire_0", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// input is fire animation strip - ignoring soul_fire_1
    { 13, 42, 338, 2, L"soul_lantern", L"soul_fire_lantern", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY }, // second name from an earlier beta
    { 14, 42, 339, 0xC, L"soul_campfire_fire", L"soul_campfire", SBIT_CLAMP_BOTTOM | SBIT_DECAL },    // alternate in Smoolistic
    { 15, 42, 339, 0xC, L"soul_campfire_log_lit", L"", SBIT_CLAMP_BOTTOM },	// geometry - this particular one does not need SBIT_CUTOUT_GEOMETRY as it fills the tile
    {  0, 43, 162, 0, L"crimson_stem_top", L"", SWATCH_REPEAT_ALL },	// more like a log
    {  1, 43, 162, 0, L"crimson_stem", L"crimson_stem_side", SWATCH_REPEAT_ALL },	// _side naming from Smoolistic
    {  2, 43,   3, 0, L"crimson_nylium", L"crimson_nylium_top", SWATCH_REPEAT_ALL },
    {  3, 43,   3, 0, L"crimson_nylium_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  4, 43,  40, 0, L"crimson_fungus", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 43,  31, 0, L"nether_sprouts", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 43,  31, 0, L"crimson_roots", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 43,  31, 0, L"crimson_roots_pot", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 43,   5, 0, L"crimson_planks", L"", SWATCH_REPEAT_ALL },
    {  9, 43, 346, 0, L"crimson_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 10, 43, 355, 0, L"crimson_door_bottom", L"crimson_door_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL }, // alt bedrock
    { 11, 43, 355, 0, L"crimson_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 12, 43, 276, 0, L"stripped_crimson_stem_top", L"", SWATCH_REPEAT_ALL },
    { 13, 43, 276, 0, L"stripped_crimson_stem", L"stripped_crimson_stem_side", SWATCH_REPEAT_ALL },	// more like wood
    { 14, 43, 363, 0, L"weeping_vines", L"weeping_vines_bottom", SBIT_CLAMP_TOP | SBIT_DECAL },	// bizarrely, upside down
    { 15, 43, 363, 0, L"weeping_vines_plant", L"weeping_vines_base", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    {  0, 44, 162, 0, L"warped_stem_top", L"", SWATCH_REPEAT_ALL },	// more like a log
    {  1, 44, 162, 0, L"warped_stem", L"warped_stem_side", SWATCH_REPEAT_ALL },	// more like a log				
    {  2, 44,   3, 0, L"warped_nylium", L"warped_nylium_top", SWATCH_REPEAT_ALL },  // alternate from Homa
    {  3, 44,   3, 0, L"warped_nylium_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  4, 44,  40, 0, L"warped_fungus", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 44, 214, 0, L"warped_wart_block", L"", SWATCH_REPEAT_ALL },
    {  6, 44,  31, 0, L"warped_roots", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 44,  31, 0, L"warped_roots_pot", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 44,   5, 0, L"warped_planks", L"", SWATCH_REPEAT_ALL },
    {  9, 44, 347, 0, L"warped_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 10, 44, 356, 0, L"warped_door_bottom", L"warped_door_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },   // Smoolistic _lower
    { 11, 44, 356, 0, L"warped_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 12, 44, 276, 0, L"stripped_warped_stem_top", L"", SWATCH_REPEAT_ALL },	// more like wood?
    { 13, 44, 276, 0, L"stripped_warped_stem", L"stripped_warped_stem_side", SWATCH_REPEAT_ALL },	// more like wood
    { 14, 44, 363, 0, L"twisting_vines", L"twisting_vines_bottom", SBIT_CLAMP_BOTTOM | SBIT_DECAL },  // alternate from Homa
    { 15, 44, 363, 0, L"twisting_vines_plant", L"twisting_vines_base", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },  // alternate from Homa
    {  0, 45,   1, 0, L"ancient_debris_top", L"", SWATCH_REPEAT_ALL },
    {  1, 45,   1, 0, L"ancient_debris_side", L"", SWATCH_REPEAT_ALL },
    {  2, 45, 344, 0, L"crying_obsidian", L"", SWATCH_REPEAT_ALL },
    {  3, 45, 345, 0, L"respawn_anchor_top", L"", SWATCH_REPEAT_ALL },
    {  4, 45, 345, 0, L"respawn_anchor_top_off", L"", SWATCH_REPEAT_ALL },
    {  5, 45, 345, 0, L"respawn_anchor_side0", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  6, 45, 345, 1, L"respawn_anchor_side1", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  7, 45, 345, 2, L"respawn_anchor_side2", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  8, 45, 345, 3, L"respawn_anchor_side3", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  9, 45, 345, 4, L"respawn_anchor_side4", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 10, 45, 345, 0, L"respawn_anchor_bottom", L"", SWATCH_REPEAT_ALL },
    { 11, 45,  58, 0, L"lodestone_top", L"", SWATCH_REPEAT_ALL },
    { 12, 45,  58, 0, L"lodestone_side", L"", SWATCH_REPEAT_ALL },
    { 13, 45,   1, 0, L"netherite_block", L"", SWATCH_REPEAT_ALL },
    { 14, 45,  14, 0, L"nether_gold_ore", L"", SWATCH_REPEAT_ALL },
    { 15, 45,   1, 0, L"gilded_blackstone", L"", SWATCH_REPEAT_ALL },
    {  0, 46, 381, 0, L"blackstone_top", L"", SWATCH_REPEAT_ALL },
    {  1, 46, 381, 0, L"blackstone", L"", SWATCH_REPEAT_ALL },
    {  2, 46, 382, 0, L"chiseled_polished_blackstone", L"", SWATCH_REPEAT_ALL },
    {  3, 46, 382, 0, L"cracked_polished_blackstone_bricks", L"", SWATCH_REPEAT_ALL },
    {  4, 46, 382, 0, L"polished_blackstone", L"", SWATCH_REPEAT_ALL },
    {  5, 46, 383, 0, L"polished_blackstone_bricks", L"", SWATCH_REPEAT_ALL },
    {  6, 46, 112, 0, L"chiseled_nether_bricks", L"", SWATCH_REPEAT_ALL },
    {  7, 46, 112, 0, L"cracked_nether_bricks", L"", SWATCH_REPEAT_ALL },
    {  8, 46,  89, 0, L"shroomlight", L"", SWATCH_REPEAT_ALL },
    {  9, 46, 331, 0, L"jigsaw_lock", L"", SWATCH_REPEAT_ALL },
    { 10, 46,  46, 0, L"target_top", L"", SWATCH_REPEAT_ALL },
    { 11, 46,  46, 0, L"target_side", L"", SWATCH_REPEAT_ALL },
    { 12, 46, 364, 0, L"chain", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },    // TODO: bedrock uses chain1 and chain2: these could be composited into the single chain tile
    { 13, 46, 370, 0, L"tinted_glass", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 46, 384, 0, L"candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 46, 385, BIT_16, L"candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 47, 386, 0, L"white_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 47, 386, 0, L"orange_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 47, 386, 0, L"magenta_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 47, 386, 0, L"light_blue_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 47, 386, 0, L"yellow_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 47, 386, 0, L"lime_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 47, 386, 0, L"pink_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 47, 386, 0, L"gray_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 47, 386, 0, L"light_gray_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  9, 47, 386, 0, L"cyan_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 47, 386, 0, L"purple_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11, 47, 386, 0, L"blue_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 47, 386, 0, L"brown_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 47, 386, 0, L"green_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 47, 386, 0, L"red_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 47, 386, 0, L"black_candle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 48, 387, BIT_16, L"white_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 48, 387, BIT_16, L"orange_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 48, 387, BIT_16, L"magenta_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 48, 387, BIT_16, L"light_blue_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 48, 387, BIT_16, L"yellow_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 48, 387, BIT_16, L"lime_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 48, 387, BIT_16, L"pink_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  7, 48, 387, BIT_16, L"gray_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  8, 48, 387, BIT_16, L"light_gray_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  9, 48, 387, BIT_16, L"cyan_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 48, 387, BIT_16, L"purple_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11, 48, 387, BIT_16, L"blue_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 48, 387, BIT_16, L"brown_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 48, 387, BIT_16, L"green_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 48, 387, BIT_16, L"red_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 48, 387, BIT_16, L"black_candle_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 49, 388, 0, L"amethyst_block", L"", SWATCH_REPEAT_ALL },                  // TODO figure out numbers and flags
    {  1, 49, 389, 1, L"small_amethyst_bud", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 49, 389, 2, L"medium_amethyst_bud", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 49, 389, 3, L"large_amethyst_bud", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 49, 389, 0, L"amethyst_cluster", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 49, 389, 0, L"budding_amethyst", L"", SWATCH_REPEAT_ALL },
    {  6, 49,   1, 0, L"calcite", L"", SWATCH_REPEAT_ALL },
    {  7, 49,   1, 0, L"tuff", L"", SWATCH_REPEAT_ALL },
    {  8, 49, 390, 0, L"dripstone_block", L"", SWATCH_REPEAT_ALL },
    {  9, 49, 390, 0, L"pointed_dripstone_up_tip", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 10, 49, 390, 0, L"pointed_dripstone_up_tip_merge", L"pointed_dripstone_up_merge", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL }, // Bedrock alt
    { 11, 49, 390, 0, L"pointed_dripstone_up_frustum", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 12, 49, 390, 0, L"pointed_dripstone_up_middle", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 49, 390, 0, L"pointed_dripstone_up_base", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 14, 49, 390, 0, L"pointed_dripstone_down_tip", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 15, 49, 390, 0, L"pointed_dripstone_down_tip_merge", L"pointed_dripstone_down_merge", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL }, // Bedrock alt
    {  0, 50, 390, 0, L"pointed_dripstone_down_frustum", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    {  1, 50, 390, 0, L"pointed_dripstone_down_middle", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    {  2, 50, 390, 0, L"pointed_dripstone_down_base", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    {  3, 50,  14, 0, L"copper_ore", L"", SWATCH_REPEAT_ALL },
    {  4, 50,  14, 0, L"deepslate_copper_ore", L"", SWATCH_REPEAT_ALL },
    {  5, 50,  42, 0, L"copper_block", L"", SWATCH_REPEAT_ALL },
    {  6, 50,  14, 0, L"exposed_copper", L"", SWATCH_REPEAT_ALL },
    {  7, 50,  14, 0, L"weathered_copper", L"", SWATCH_REPEAT_ALL },
    {  8, 50,  14, 0, L"oxidized_copper", L"", SWATCH_REPEAT_ALL },
    {  9, 50,  42, 0, L"cut_copper", L"", SWATCH_REPEAT_ALL },
    { 10, 50,  14, 0, L"exposed_cut_copper", L"", SWATCH_REPEAT_ALL },
    { 11, 50,  14, 0, L"weathered_cut_copper", L"", SWATCH_REPEAT_ALL },
    { 12, 50,  14, 0, L"oxidized_cut_copper", L"", SWATCH_REPEAT_ALL },
    { 13, 50, 395, 0, L"lightning_rod", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 50, 395, 0, L"lightning_rod_on", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 50, 404, 0, L"cave_vines", L"cave_vines_head", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  0, 51, 404, 0, L"cave_vines_plant", L"cave_vines_body", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  1, 51, 405, 0, L"cave_vines_lit", L"cave_vines_head_berries", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 51, 405, 0, L"cave_vines_plant_lit", L"cave_vines_body_berries", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 51, 406, 0, L"spore_blossom", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 51, 406, 0, L"spore_blossom_base", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 51, 407, 0, L"azalea_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },   // not cutout normally, but resource packs such as KOW do
    {  6, 51, 407, 0, L"flowering_azalea_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },   // not cutout normally, but resource packs such as KOW do
    {  7, 51, 407, 0, L"azalea_side", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    {  8, 51, 407, 0, L"flowering_azalea_side", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    {  9, 51, 407, 0, L"azalea_plant", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    { 10, 51, 407, 0, L"potted_azalea_bush_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 51, 407, 0, L"potted_flowering_azalea_bush_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 51, 407, 0, L"potted_azalea_bush_side", L"", SBIT_DECAL },
    { 13, 51, 407, 0, L"potted_flowering_azalea_bush_side", L"", SBIT_DECAL },
    { 14, 51, 407, 0, L"potted_azalea_bush_plant", L"", SBIT_DECAL },
    { 15, 51, 407, 0, L"potted_flowering_azalea_bush_plant", L"", SBIT_DECAL },
    {  0, 52, 161, 0, L"azalea_leaves", L"", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    {  1, 52, 161, 0, L"flowering_azalea_leaves", L"azalea_leaves_flowers", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    {  2, 52,   2, 0, L"moss_block", L"", SWATCH_REPEAT_ALL },
    {  3, 52, 408, 0, L"big_dripleaf_top", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    {  4, 52, 408, 0, L"big_dripleaf_side", L"big_dripleaf_side1", SWATCH_CLAMP_ALL | SBIT_DECAL }, // alt bedrock
    {  5, 52, 408, 0, L"big_dripleaf_tip", L"big_dripleaf_side2", SWATCH_CLAMP_ALL | SBIT_DECAL },
    {  6, 52, 408, 0, L"big_dripleaf_stem", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    {  7, 52, 409, 0, L"small_dripleaf_top", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    {  8, 52, 409, 0, L"small_dripleaf_side", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    {  9, 52, 409, 0, L"small_dripleaf_stem_top", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    { 10, 52, 409, 0, L"small_dripleaf_stem_bottom", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },
    { 11, 52,   2, 0, L"rooted_dirt", L"dirt_with_roots", SWATCH_REPEAT_ALL },
    { 12, 52, 363, 0, L"hanging_roots", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    { 13, 52,  78, 0, L"powder_snow", L"", SWATCH_REPEAT_ALL },
    { 14, 52, 410, 0, L"glow_lichen", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 15, 52, 411, 0, L"sculk_sensor_top", L"", SWATCH_REPEAT_ALL },
    {  0, 53, 411, 0, L"sculk_sensor_side", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  1, 53, 411, 0, L"sculk_sensor_bottom", L"", SWATCH_REPEAT_ALL },
    {  2, 53, 411, 0, L"sculk_sensor_tendril_active", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 53, 411, 0, L"sculk_sensor_tendril_inactive", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 53,   1, 0, L"deepslate_top", L"", SWATCH_REPEAT_ALL },   // really attached to the Amethyst block type, but type is more like Stone (like Calcite is)
    {  5, 53,   1, 0, L"deepslate", L"", SWATCH_REPEAT_ALL },
    {  6, 53,   1, 0, L"cobbled_deepslate", L"", SWATCH_REPEAT_ALL },
    {  7, 53,   1, 0, L"chiseled_deepslate", L"", SWATCH_REPEAT_ALL },
    {  8, 53,   1, 0, L"polished_deepslate", L"", SWATCH_REPEAT_ALL },
    {  9, 53,   1, 0, L"deepslate_bricks", L"", SWATCH_REPEAT_ALL },
    { 10, 53,   1, 0, L"deepslate_tiles", L"", SWATCH_REPEAT_ALL },
    { 11, 53,   1, 0, L"cracked_deepslate_bricks", L"", SWATCH_REPEAT_ALL },
    { 12, 53,   1, 0, L"cracked_deepslate_tiles", L"", SWATCH_REPEAT_ALL },
    { 13, 53,  14, 0, L"deepslate_gold_ore", L"", SWATCH_REPEAT_ALL },
    { 14, 53,  15, 0, L"deepslate_iron_ore", L"", SWATCH_REPEAT_ALL },
    { 15, 53,  16, 0, L"deepslate_coal_ore", L"", SWATCH_REPEAT_ALL },
    {  0, 54,  56, 0, L"deepslate_diamond_ore", L"", SWATCH_REPEAT_ALL },
    {  1, 54,  73, 0, L"deepslate_redstone_ore", L"", SWATCH_REPEAT_ALL },
    {  2, 54,  21, 0, L"deepslate_lapis_ore", L"", SWATCH_REPEAT_ALL },
    {  3, 54, 129, 0, L"deepslate_emerald_ore", L"", SWATCH_REPEAT_ALL },
    {  4, 54, 311, 0, L"smooth_basalt", L"", SWATCH_REPEAT_ALL },
    {  5, 54,  15, 0, L"raw_iron_block", L"", SWATCH_REPEAT_ALL },
    {  6, 54,  14, 0, L"raw_copper_block", L"", SWATCH_REPEAT_ALL },
    {  7, 54,  14, 0, L"raw_gold_block", L"", SWATCH_REPEAT_ALL },
    {  8, 54, 435, 0, L"frogspawn", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },    // start 1.19
    {  9, 54, 418, 0, L"mangrove_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 10, 54, 418, 0, L"mangrove_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 11, 54, 437, 0, L"mangrove_leaves", L"", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES | SBIT_SYNTHESIZED },
    { 12, 54, 416, 0, L"mangrove_log_top", L"", SWATCH_REPEAT_ALL },
    { 13, 54, 416, 0, L"mangrove_log", L"", SWATCH_REPEAT_ALL },
    { 14, 54, 423, 0, L"stripped_mangrove_log_top", L"", SWATCH_REPEAT_ALL },
    { 15, 54, 423, 0, L"stripped_mangrove_log", L"", SWATCH_REPEAT_ALL },
    {  0, 55,   5, 0, L"mangrove_planks", L"", SWATCH_REPEAT_ALL },
    {  1, 55, 420, 0, L"mangrove_propagule", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  2, 55, 420, 0, L"mangrove_propagule_hanging", L"", SBIT_CLAMP_TOP | SBIT_DECAL },
    {  3, 55, 421, 0, L"mangrove_roots_top", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  4, 55, 421, 0, L"mangrove_roots_side", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  5, 55, 419, 0, L"mangrove_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  6, 55,   1, 0, L"mud", L"", SWATCH_REPEAT_ALL },
    {  7, 55,  45, 0, L"mud_bricks", L"", SWATCH_REPEAT_ALL },
    {  8, 55, 422, 0, L"muddy_mangrove_roots_top", L"", SWATCH_REPEAT_ALL },
    {  9, 55, 422, 0, L"muddy_mangrove_roots_side", L"", SWATCH_REPEAT_ALL },
    { 10, 55,   1, 0, L"packed_mud", L"", SWATCH_REPEAT_ALL },
    { 11, 55, 436, 0, L"ochre_froglight_top", L"", SWATCH_REPEAT_ALL },
    { 12, 55, 436, 0, L"ochre_froglight_side", L"", SWATCH_REPEAT_ALL },
    { 13, 55, 436, 0, L"pearlescent_froglight_top", L"", SWATCH_REPEAT_ALL },
    { 14, 55, 436, 0, L"pearlescent_froglight_side", L"", SWATCH_REPEAT_ALL },
    { 15, 55, 436, 0, L"verdant_froglight_top", L"", SWATCH_REPEAT_ALL },
    {  0, 56, 436, 0, L"verdant_froglight_side", L"", SWATCH_REPEAT_ALL },
    {  1, 56, 388, 0, L"sculk", L"", SWATCH_REPEAT_ALL },
    {  2, 56,   3, 1, L"sculk_catalyst_top", L"", SWATCH_REPEAT_ALL },
    {  3, 56,   3, 1, L"sculk_catalyst_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  4, 56,   3, 1, L"sculk_catalyst_bottom", L"", SWATCH_REPEAT_ALL },
    {  5, 56,   3, 1, L"sculk_catalyst_top_bloom", L"", SWATCH_REPEAT_ALL },
    {  6, 56,   3, 1, L"sculk_catalyst_side_bloom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    {  7, 56, 433, 0, L"sculk_shrieker_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 56, 433, 0, L"sculk_shrieker_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_CUTOUT_GEOMETRY },
    {  9, 56, 433, 0, L"sculk_shrieker_bottom", L"", SWATCH_REPEAT_ALL },
    { 10, 56, 433, 0, L"sculk_shrieker_can_summon_inner_top", L"", SWATCH_REPEAT_ALL },
    { 11, 56, 433, 0, L"sculk_shrieker_inner_top", L"", SWATCH_REPEAT_ALL },
    { 12, 56, 434, 0, L"sculk_vein", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 56, 388, 0, L"reinforced_deepslate_top", L"", SWATCH_REPEAT_ALL },
    { 14, 56, 388, 0, L"reinforced_deepslate_side", L"", SWATCH_REPEAT_ALL },
    { 15, 56, 388, 0, L"reinforced_deepslate_bottom", L"", SWATCH_REPEAT_ALL },
    {  0, 57, 411, 0, L"calibrated_sculk_sensor_amethyst", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },    // start of 1.20
    {  1, 57, 411, 0, L"calibrated_sculk_sensor_input_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_CUTOUT_GEOMETRY },
    {  2, 57, 411, 0, L"calibrated_sculk_sensor_top", L"", SWATCH_REPEAT_ALL },
    {  3, 57, 439, 0, L"cherry_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  4, 57, 439, 0, L"cherry_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  5, 57, 437, 0, L"cherry_leaves", L"", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    {  6, 57, 416, 0, L"cherry_log", L"", SWATCH_REPEAT_ALL },
    {  7, 57, 416, 0, L"cherry_log_top", L"", SWATCH_REPEAT_ALL },
    {  8, 57,   5, 0, L"cherry_planks", L"", SWATCH_REPEAT_ALL },
    {  9, 57,   6, 0, L"cherry_sapling", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 57, 444, 0, L"cherry_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 11, 57, 453, 0, L"pink_petals", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 12, 57, 453, 0, L"pink_petals_stem", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },    // stem is along left edge, grayscale
    { 13, 57, 454, 0, L"pitcher_crop_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 57, 454, 0, L"pitcher_crop_bottom_stage_1", L"", SBIT_CLAMP_TOP | SBIT_CUTOUT_GEOMETRY },
    { 15, 57, 454, 0, L"pitcher_crop_bottom_stage_2", L"", SBIT_CLAMP_TOP | SBIT_CUTOUT_GEOMETRY },
    {  0, 58, 454, 0, L"pitcher_crop_bottom_stage_3", L"", SBIT_CLAMP_TOP | SBIT_CUTOUT_GEOMETRY },
    {  1, 58, 454, 0, L"pitcher_crop_bottom_stage_4", L"", SBIT_CLAMP_TOP | SBIT_CUTOUT_GEOMETRY },
    {  2, 58, 454, 0, L"pitcher_crop_side", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  3, 58, 454, 0, L"pitcher_crop_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  4, 58, 454, 0, L"pitcher_crop_top_stage_3", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 58, 454, 0, L"pitcher_crop_top_stage_4", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  6, 58, 455, 0, L"sniffer_egg_not_cracked_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  7, 58, 455, 0, L"sniffer_egg_not_cracked_east", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 58, 455, 0, L"sniffer_egg_not_cracked_north", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  9, 58, 455, 0, L"sniffer_egg_not_cracked_south", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 58, 455, 0, L"sniffer_egg_not_cracked_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 58, 455, 0, L"sniffer_egg_not_cracked_west", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 58, 455, 0, L"sniffer_egg_slightly_cracked_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 58, 455, 0, L"sniffer_egg_slightly_cracked_east", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 58, 455, 0, L"sniffer_egg_slightly_cracked_north", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 58, 455, 0, L"sniffer_egg_slightly_cracked_south", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  0, 59, 455, 0, L"sniffer_egg_slightly_cracked_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  1, 59, 455, 0, L"sniffer_egg_slightly_cracked_west", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  2, 59, 455, 0, L"sniffer_egg_very_cracked_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  3, 59, 455, 0, L"sniffer_egg_very_cracked_east", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  4, 59, 455, 0, L"sniffer_egg_very_cracked_north", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  5, 59, 455, 0, L"sniffer_egg_very_cracked_south", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  6, 59, 455, 0, L"sniffer_egg_very_cracked_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  7, 59, 455, 0, L"sniffer_egg_very_cracked_west", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    {  8, 59, 423, 0, L"stripped_cherry_log", L"", SWATCH_REPEAT_ALL },
    {  9, 59, 423, 0, L"stripped_cherry_log_top", L"", SWATCH_REPEAT_ALL },
    { 10, 59, 456, 0, L"suspicious_gravel_0", L"", SWATCH_REPEAT_ALL },
    { 11, 59, 456, 0, L"suspicious_gravel_1", L"", SWATCH_REPEAT_ALL },
    { 12, 59, 456, 0, L"suspicious_gravel_2", L"", SWATCH_REPEAT_ALL },
    { 13, 59, 456, 0, L"suspicious_gravel_3", L"", SWATCH_REPEAT_ALL },
    { 14, 59, 456, 0, L"suspicious_sand_0", L"", SWATCH_REPEAT_ALL },
    { 15, 59, 456, 0, L"suspicious_sand_1", L"", SWATCH_REPEAT_ALL },
    {  0, 60, 456, 0, L"suspicious_sand_2", L"", SWATCH_REPEAT_ALL },
    {  1, 60, 456, 0, L"suspicious_sand_3", L"", SWATCH_REPEAT_ALL },
    {  2, 60,  37, 0, L"torchflower", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 60, 457, 0, L"torchflower_crop_stage0", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 60, 457, 0, L"torchflower_crop_stage1", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 60, 170, 0, L"bamboo_block", L"", SWATCH_REPEAT_ALL },
    {  6, 60, 170, 0, L"bamboo_block_top", L"", SWATCH_REPEAT_ALL },
    {  7, 60, 446, 0, L"bamboo_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  8, 60, 446, 0, L"bamboo_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  9, 60, 447, 0, L"bamboo_fence", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 60, 448, 0, L"bamboo_fence_gate", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 60, 448, 0, L"bamboo_fence_gate_particle", L"", SWATCH_REPEAT_ALL }, // This and the next are actually identical currently. Could merge these two into one if we desperately need a slot.
    { 12, 60, 447, 0, L"bamboo_fence_particle", L"", SWATCH_REPEAT_ALL },
    { 13, 60,   5, 0, L"bamboo_mosaic", L"", SWATCH_REPEAT_ALL },
    { 14, 60,   5, 0, L"bamboo_planks", L"", SWATCH_REPEAT_ALL },
    { 15, 60, 451, 0, L"bamboo_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  0, 61, 417, 0, L"decorated_pot_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_CUTOUT_GEOMETRY },
    {  1, 61, 417, 0, L"MW_decorated_pot_base1", L"", SWATCH_REPEAT_ALL },
    {  2, 61, 417, 0, L"MW_decorated_pot_base2", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  3, 61, 417, 0, L"MW_decorated_pot_base3", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  4, 61, 417, 0, L"MW_decorated_pot_base4", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    {  5, 61, 170, 0, L"stripped_bamboo_block", L"", SWATCH_REPEAT_ALL },
    {  6, 61, 170, 0, L"stripped_bamboo_block_top", L"", SWATCH_REPEAT_ALL },
    {  7, 61,  47, 0, L"chiseled_bookshelf_top", L"", SWATCH_REPEAT_ALL },
    {  8, 61,  47, 0, L"chiseled_bookshelf_side", L"", SWATCH_REPEAT_ALL },
    {  9, 61,  47, 0, L"chiseled_bookshelf_empty", L"", SWATCH_REPEAT_ALL },
    { 10, 61, 469, 0, L"copper_bulb", L"", SWATCH_REPEAT_ALL }, // start 1.21
    { 11, 61, 469, 0x8, L"copper_bulb_lit", L"", SWATCH_REPEAT_ALL },
    { 12, 61, 469, 0x8, L"copper_bulb_lit_powered", L"", SWATCH_REPEAT_ALL },
    { 13, 61, 469, 0, L"copper_bulb_powered", L"", SWATCH_REPEAT_ALL },
    { 14, 61, 482, 0, L"copper_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 15, 61, 482, 0, L"copper_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  0, 62, 470, 0, L"copper_grate", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  1, 62, 474, 0, L"copper_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  2, 62, 467, 0, L"crafter_bottom", L"", SWATCH_REPEAT_ALL },
    {  3, 62, 467, 0, L"crafter_east", L"", SWATCH_REPEAT_ALL },
    {  4, 62, 467, 0, L"crafter_east_crafting", L"", SWATCH_REPEAT_ALL },
    {  5, 62, 467, 0, L"crafter_east_triggered", L"", SWATCH_REPEAT_ALL },
    {  6, 62, 467, 0, L"crafter_north", L"", SWATCH_REPEAT_ALL },
    {  7, 62, 467, 0, L"crafter_north_crafting", L"", SWATCH_REPEAT_ALL },
    {  8, 62, 467, 0, L"crafter_south", L"", SWATCH_REPEAT_ALL },
    {  9, 62, 467, 0, L"crafter_south_triggered", L"", SWATCH_REPEAT_ALL },
    { 10, 62, 467, 0, L"crafter_top", L"", SWATCH_REPEAT_ALL },
    { 11, 62, 467, 0, L"crafter_top_crafting", L"", SWATCH_REPEAT_ALL },
    { 12, 62, 467, 0, L"crafter_top_triggered", L"", SWATCH_REPEAT_ALL },
    { 13, 62, 467, 0, L"crafter_west", L"", SWATCH_REPEAT_ALL },
    { 14, 62, 467, 0, L"crafter_west_crafting", L"", SWATCH_REPEAT_ALL },
    { 15, 62, 467, 0, L"crafter_west_triggered", L"", SWATCH_REPEAT_ALL },
    {  0, 63, 388, 0, L"exposed_chiseled_copper", L"", SWATCH_REPEAT_ALL },
    {  1, 63, 469, 0, L"exposed_copper_bulb", L"", SWATCH_REPEAT_ALL },
    {  2, 63, 469, 0x9, L"exposed_copper_bulb_lit", L"", SWATCH_REPEAT_ALL },
    {  3, 63, 469, 0x9, L"exposed_copper_bulb_lit_powered", L"", SWATCH_REPEAT_ALL },
    {  4, 63, 469, 0, L"exposed_copper_bulb_powered", L"", SWATCH_REPEAT_ALL },
    {  5, 63, 475, 0, L"exposed_copper_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  6, 63, 475, 0, L"exposed_copper_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  7, 63, 470, 0, L"exposed_copper_grate", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  8, 63, 483, 0, L"exposed_copper_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  9, 63, 468, 0, L"heavy_core", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 63, 388, 0, L"oxidized_chiseled_copper", L"", SWATCH_REPEAT_ALL },
    { 11, 63, 469, 0, L"oxidized_copper_bulb", L"", SWATCH_REPEAT_ALL },
    { 12, 63, 469, 11, L"oxidized_copper_bulb_lit", L"", SWATCH_REPEAT_ALL },
    { 13, 63, 469, 11, L"oxidized_copper_bulb_lit_powered", L"", SWATCH_REPEAT_ALL },
    { 14, 63, 469, 0, L"oxidized_copper_bulb_powered", L"", SWATCH_REPEAT_ALL },
    { 15, 63, 475, 0, L"oxidized_copper_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  0, 64, 475, 0, L"oxidized_copper_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  1, 64, 470, 0, L"oxidized_copper_grate", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  2, 64, 483, 0, L"oxidized_copper_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  3, 64, 388, 0, L"polished_tuff", L"", SWATCH_REPEAT_ALL },
    {  4, 64, 465, 0, L"trial_spawner_bottom", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  5, 64, 465, 1, L"trial_spawner_side_active", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  6, 64, 465, 1, L"trial_spawner_side_active_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  7, 64, 465, 0, L"trial_spawner_side_inactive", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  8, 64, 465, 0, L"trial_spawner_side_inactive_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  9, 64, 465, 1, L"trial_spawner_top_active", L"", SWATCH_REPEAT_ALL },
    { 10, 64, 465, 1, L"trial_spawner_top_active_ominous", L"", SWATCH_REPEAT_ALL },
    { 11, 64, 465, 1, L"trial_spawner_top_ejecting_reward", L"", SWATCH_REPEAT_ALL },
    { 12, 64, 465, 1, L"trial_spawner_top_ejecting_reward_ominous", L"", SWATCH_REPEAT_ALL },
    { 13, 64, 465, 0, L"trial_spawner_top_inactive", L"", SWATCH_REPEAT_ALL },
    { 14, 64, 465, 0, L"trial_spawner_top_inactive_ominous", L"", SWATCH_REPEAT_ALL },
    { 15, 64, 388, 0, L"tuff_bricks", L"", SWATCH_REPEAT_ALL },
    {  0, 65, 466, 0, L"vault_bottom", L"", SWATCH_REPEAT_ALL },
    {  1, 65, 466, 0, L"vault_bottom_ominous", L"", SWATCH_REPEAT_ALL },
    {  2, 65, 466, 0x18, L"vault_front_ejecting", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  3, 65, 466, 0x18, L"vault_front_ejecting_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  4, 65, 466, 0, L"vault_front_off", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  5, 65, 466, 0, L"vault_front_off_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  6, 65, 466, 0x18, L"vault_front_on", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  7, 65, 466, 0x18, L"vault_front_on_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  8, 65, 466, 0, L"vault_side_off", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  9, 65, 466, 0, L"vault_side_off_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 10, 65, 466, 0x18, L"vault_side_on", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 11, 65, 466, 0x18, L"vault_side_on_ominous", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 12, 65, 466, 0, L"vault_top", L"", SWATCH_REPEAT_ALL },
    { 13, 65, 466, 0, L"vault_top_ominous", L"", SWATCH_REPEAT_ALL },   // non-alphabetic order to keep ominous at +1
    { 14, 65, 466, 0x18, L"vault_top_ejecting", L"", SWATCH_REPEAT_ALL },
    { 15, 65, 466, 0, L"vault_top_ejecting_ominous", L"", SWATCH_REPEAT_ALL },
    {  0, 66, 388, 0, L"weathered_chiseled_copper", L"", SWATCH_REPEAT_ALL },
    {  1, 66, 469, 0, L"weathered_copper_bulb", L"", SWATCH_REPEAT_ALL },
    {  2, 66, 469, 10, L"weathered_copper_bulb_lit", L"", SWATCH_REPEAT_ALL },
    {  3, 66, 469, 10, L"weathered_copper_bulb_lit_powered", L"", SWATCH_REPEAT_ALL },
    {  4, 66, 469, 0, L"weathered_copper_bulb_powered", L"", SWATCH_REPEAT_ALL },
    {  5, 66, 476, 0, L"weathered_copper_door_bottom", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  6, 66, 476, 0, L"weathered_copper_door_top", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    {  7, 66, 470, 0, L"weathered_copper_grate", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  8, 66, 484, 0, L"weathered_copper_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    {  9, 66, 388, 0, L"chiseled_copper", L"", SWATCH_REPEAT_ALL }, // oops, forgot this one
    { 10, 66, 388, 0, L"chiseled_tuff", L"", SWATCH_REPEAT_ALL },
    { 11, 66, 388, 0, L"chiseled_tuff_top", L"", SWATCH_REPEAT_ALL },
    { 12, 66, 388, 0, L"chiseled_tuff_bricks", L"", SWATCH_REPEAT_ALL },
    { 13, 66, 388, 0, L"chiseled_tuff_bricks_top", L"", SWATCH_REPEAT_ALL },
    { 14, 66,  47, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL }, // d'oh, accidentally deleted this one when adding 1.21
    { 15, 66,   0, 0, L"", L"", SWATCH_REPEAT_ALL },
    /* Template for future additions - copy and paste in Notepad++ to avoid it getting rejiggered, and keep inside comment marks to
     * avoid rejiggering (spaces getting removed) by keeping it inside the comment marks when pasting back here:
    {  0, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  1, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  2, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  3, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  4, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  5, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  6, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  7, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  8, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    {  9, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 10, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 11, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 12, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 13, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 14, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    { 15, 67,   0, 0, L"chiseled_bookshelf_occupied", L"", SWATCH_REPEAT_ALL },
    */
};

// There is more than one alternate name, so test more of them
// In good part derived from https://github.com/TheDuckCow/MCprep/blob/master/mcprep_data_refresh.py#L199
// Note that case doesn't matter, since we ignore case on all tests - easier to copy and edit from MCPrep
static const struct {
    const wchar_t* altFilename; // the "yet another alternative" name to search on
    const wchar_t* filename;    // the real name we use, which we search for in the table above
} gTilesAlternates[] = {
    // special sleazy case: mcpacks have grass_side_carried meaning grass_block_side, in which case their grass_side is actually the overlay.
    // When detected, we copy both over and swap the two.
    { L"grass_side_carried", L"grass_block_side_overlay" },

    { L"Acacia_Door", L"acacia_door_bottom" },
    { L"Birch_Door", L"birch_door_bottom" },
    { L"Cactus", L"cactus_side" },
    // there seems to be, in Absolution for example, a separate command block image that is sort of like the front, but is something else...
    //{ L"Command_Block", L"command_block_front" }, // disagree
    { L"Carrots", L"carrots_stage3" },
    //{ L"Campfire", L"campfire_log" }, // I disagree - in Bedrock "campfire" is "campfire_fire"
    { L"Crafting_Table", L"crafting_table_top" },
    { L"Crafting_Table__Cartography_Table", L"cartography_table_top" },
    { L"Crafting_Table__Fletching_Table", L"fletching_table_top" },
    { L"Crafting_Table__Smithing_Table", L"smithing_table_top" },
    { L"Dark_Oak_Door", L"dark_oak_door_bottom" },
    { L"Enchanting_Table", L"enchanting_table_top" },
    { L"Furnace", L"furnace_front_on" }, // assume on? meshswap implication
    { L"Furnace__Blast_Furnace", L"blast_furnace_front_on" }, // assume on? meshswap implication
    { L"Furnace__Loom", L"loom_top" },
    { L"Furnace__Smoker", L"smoker_front_on" }, // assume on? meshswap implication
    { L"Fire", L"fire_0" },
    { L"Grass__Fern", L"fern" }, // single block high
    { L"Grass__Tall_Grass", L"grass" }, // ie tall grass
    { L"Glass_Pane", L"glass_pane_top" },
    { L"Iron_Door", L"iron_door_bottom" },
    { L"Jack_o'Lantern", L"jack_o_lantern" },
    { L"Pumpkin", L"carved_pumpkin" },
    { L"Large_Flowers", L"sunflower_bottom" }, // decide block
    { L"Large_Flowers__1", L"lilac_bottom" },
    { L"Large_Flowers__2", L"tall_grass_bottom" },
    { L"Large_Flowers__3", L"large_fern_bottom" },
    { L"Large_Flowers__4", L"rose_bush_bottom" },
    { L"Large_Flowers__5", L"peony_bottom" },
    { L"Magma_Block", L"magma" },
    { L"Poppy__Allium", L"allium" },
    { L"Poppy__Azure_Bluet", L"azure_bluet" },
    { L"Poppy__Blue_Orchid", L"blue_orchid" },
    { L"Poppy__Orange_Tulip", L"orange_tulip" },
    { L"Poppy__Oxeye_Daisy", L"oxeye_daisy" },
    { L"Poppy__Pink_Tulip", L"pink_tulip" },
    { L"Poppy__Red_Tulip", L"red_tulip" },
    { L"Poppy__White_Tulip", L"white_tulip" },
    { L"Poppy__Wither_Rose", L"wither_rose" },
    { L"Redstone_Lamp_(active)", L"redstone_lamp" },
    { L"Redstone_Lamp_(inactive)", L"redstone_lamp_off" },
    { L"Redstone_Torch_(active)", L"redstone_torch" },
    { L"Redstone_Torch_(inactive)", L"redstone_torch_off" },
    { L"Sapling", L"oak_sapling" },
    { L"Sapling__Acacia_Sapling", L"acacia_sapling" },
    { L"Sapling__Birch_Sapling", L"birch_sapling" },
    { L"Sapling__Dark_Oak_Sapling", L"dark_oak_sapling" },
    { L"Sapling__Jungle_Sapling", L"jungle_sapling" },
    { L"Sapling__Spruce_Sapling", L"spruce_sapling" },
    { L"Spruce_Door", L"spruce_door_bottom" },
    { L"Seagrass", L"tall_seagrass_bottom" },
    { L"Stationary_Lava", L"lava_still" },
    { L"Stationary_Water", L"water_still" },
    { L"Stone_Cutter", L"stonecutter_top" }, // should be a meshswap item evetually
    { L"Sunflower", L"sunflower_bottom" },
    { L"TNT", L"tnt_top" }, // really? not the side?
    { L"Vines", L"vine" },
    { L"Wheat", L"wheat_stage7" },
    { L"Wooden_Door", L"oak_door_bottom" },

    // still more, not in MCPrep
    { L"acacia_door_lower", L"acacia_door_bottom" },    // Absolution
    { L"acacia_door_upper", L"acacia_door_top" },    // Absolution
    { L"birch_door_lower", L"birch_door_bottom" },    // Absolution
    { L"birch_door_upper", L"birch_door_top" },    // Absolution
    { L"blues_stained_glass", L"blue_stained_glass" },    // LunaHD
    { L"crimson_log_side", L"crimson_stem" },    // Smoolistic
    { L"crimson_log_top", L"crimson_stem_top" },    // Smoolistic
    { L"dark_oak_door_lower", L"dark_oak_door_bottom" },    // Absolution
    { L"dark_oak_door_upper", L"dark_oak_door_top" },    // Absolution
    { L"dirt_podzol-side", L"podzol_side" },    // JG-RTX
    { L"dirt_podzol-top", L"podzol_top" },    // JG-RTX
    { L"fletcher_table_side1", L"fletching_table_front" },    // Muddle, JG-RTX
    { L"fletcher_table_side2", L"fletching_table_side" },    // JG-RTX
    { L"fletcher_table_top", L"fletching_table_top" },    // Muddle
    //{ L"grass-side", L"grass_block_side" },    // JG-RTX
    //{ L"grass-top", L"grass_block_top" },    // JG-RTX
    { L"grass_block_side_snowed", L"grass_block_snow" },    // Flows
    { L"grass-tuft", L"grass" },    // JG-RTX
    { L"iron_door_lower", L"iron_door_bottom" },    // Absolution
    { L"iron_door_upper", L"iron_door_top" },    // Absolution
    { L"jungle_door_lower", L"jungle_door_bottom" },    // Absolution
    { L"jungle_door_upper", L"jungle_door_top" },    // Absolution
    //{ L"jungle_wood", L"jungle_planks" },    // Ultimate Immersion - but already has jungle_planks
    { L"luna_birch_leaves", L"birch_leaves" },    // LunaHD
    { L"luna_jungle_leaves", L"jungle_leaves" },    // LunaHD
    { L"mossy_stonebrick", L"mossy_stone_bricks" },    // Flows
    { L"mossy_stone_brick", L"mossy_stone_bricks" },    // JG-RTX
    { L"oak_door_lower", L"oak_door_bottom" },    // Absolution
    { L"oak_door_upper", L"oak_door_top" },    // Absolution
    { L"oak_leave", L"oak_leaves" },    // miejojo128 v1.16
    { L"quartz_block", L"quartz_block_top" },    // Meteor - probably really want quartz_block_side to be copied over, too. Multiply copying? TODO
    { L"quartz_block_lines", L"quartz_pillar" },    // JG-RTX, sort of a guess, but better than quartz_column
    { L"red_sandstone_carved", L"chiseled_red_sandstone" },
    { L"sandstone_carved", L"chiseled_sandstone" },
    { L"seagrass_doubletall_bottom", L"tall_seagrass_bottom" },   // JG-RTX; there's also a "_b" version...
    { L"seagrass_doubletall_bottom_a", L"tall_seagrass_bottom" },   // JG-RTX; there's also a "_b" version...
    { L"seagrass_doubletall_top", L"tall_seagrass_top" },   // JG-RTX; there's also a "_b" version...
    { L"seagrass_doubletall_top_a", L"tall_seagrass_top" },   // JG-RTX; there's also a "_b" version...
    { L"silver_glazed_terracotta", L"light_gray_glazed_terracotta"}, // Ultimate Immersion
    { L"spruce", L"spruce_planks" },    // LunaHD
    { L"spruce_door_lower", L"spruce_door_bottom" },    // Absolution
    { L"spruce_door_upper", L"spruce_door_top" },    // Absolution
    { L"spruce_needles", L"spruce_leaves" },    // Ultimate Immersion
    { L"stonebrick_mossy", L"mossy_stone_bricks" },    // JG-RTX
    { L"stone_andesite_smooth", L"polished_andesite" },    // JG-RTX
    { L"stone_diorite_smooth", L"polished_diorite" },    // JG-RTX
    { L"stonebrick_cracked", L"cracked_stone_bricks" },    // Muddle
    { L"sunflower_top_back", L"sunflower_top" },    // Doku craft - sunflower_top is not what we want
    { L"tallgrass", L"grass" },    // old name - something once used it, I think...
    { L"trip_wire_hook", L"tripwire_hook" },    // Absolution
    { L"wool", L"white_wool" },    // PhotoCraft

    // just plain typos - might as well fix them as I find them
    //{ L"conrflower", L"cornflower" },    // miejojo128 v1.16 - typo, but not the image we want anyway
    { L"diamon_block", L"diamond_block" },    // Absolution
    { L"green_concrete_powde", L"green_concrete_powder" }, // PhotoCraft
    { L"grey_stained_glass", L"gray_stained_glass" },    // Vanilla-Normals-Renewed-master
    { L"grey_stained_glass_pane_top", L"gray_stained_glass_pane_top" },    // Vanilla-Normals-Renewed-master
    { L"light_blue_conctrete", L"light_blue_concrete" },    // Absolution
    { L"megenta_concrete", L"magenta_concrete" },    // Absolution
    { L"netherack", L"netherrack" },    // JG-RTX typo
    { L"packed_ce", L"packed_ice" },    // JG-RTX typo, reported https://github.com/jasonjgardner/jg-rtx/issues/4
    { L"prismrine_brick", L"prismarine_bricks" }, // PhotoCraft

    // tag that denotes end of list for while loop
    { L"", L"" }
};

// tiles we know we don't use
static const wchar_t* gUnneeded[] = {
    L"debug",
    L"debug2",
    L"destroy_stage_0",
    L"destroy_stage_1",
    L"destroy_stage_2",
    L"destroy_stage_3",
    L"destroy_stage_4",
    L"destroy_stage_5",
    L"destroy_stage_6",
    L"destroy_stage_7",
    L"destroy_stage_8",
    L"destroy_stage_9",
    L"fire_layer_1",
    // older names
    L"leaves_birch_opaque",
    L"leaves_jungle_opaque",
    L"leaves_oak_opaque",
    L"leaves_spruce_opaque",
    L"fire_1",
    L"glow_item_frame",
    L"item_frame",
    L"item_frame_front",
    L"itemframe_background", // Absolution
    L"shulker_box", // generic 1.13; specific colors now used per box
    L"soul_fire_1",
    L"structure_block", // only used in inventory, not used when placed: http://minecraft.wiki/w/Structure_Block - we use the other ones of this type

    L"flower_paeonia", // experimental block, never used: https://minecraft.wiki/w/Java_Edition_removed_features#Paeonia

    // this empty string is used to mark the end of this array
    L""
};

