// tiles.h - defines where each Minecraft texture block is used (if used at all)
//

//#include "targetver.h"

#ifndef TILES_H
#define TILES_H


#define SBIT_REPEAT_SIDES       0x01
#define SBIT_REPEAT_TOP_BOTTOM  0x02
#define SBIT_CLAMP_BOTTOM       0x04
#define SBIT_CLAMP_TOP          0x08
#define SBIT_CLAMP_RIGHT        0x10
#define SBIT_CLAMP_LEFT         0x20

#define SBIT_DECAL				0x40

// special bit: if the tile is a leaf tile, Mineways itself can optionally 
#define SBIT_LEAVES				0x80

// If set, the incoming .png's black pixels should be treated as having an alpha of 0.
// Normally Minecraft textures have alpha set properly, but this is a workaround for those that don't.
#define SBIT_BLACK_ALPHA		0x8000

// types of blocks: tiling, billboard, and sides (which tile only horizontally)
#define SWATCH_REPEAT_ALL                   (SBIT_REPEAT_SIDES|SBIT_REPEAT_TOP_BOTTOM)
#define SWATCH_REPEAT_SIDES_ELSE_CLAMP      (SBIT_REPEAT_SIDES|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
#define SWATCH_TILE_BOTTOM_AND_TOP          SBIT_REPEAT_TOP_BOTTOM
#define SWATCH_CLAMP_BOTTOM_AND_RIGHT       (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT)
#define SWATCH_CLAMP_BOTTOM_AND_TOP         (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
#define SWATCH_CLAMP_ALL_BUT_TOP            (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)
#define SWATCH_CLAMP_ALL                    (SBIT_CLAMP_TOP|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)


// If this number changes, also change warning #6 in gPopupInfo in Mineways.cpp
#define VERTICAL_TILES 24
#define TOTAL_TILES (VERTICAL_TILES*16)
static struct {
	int txrX;   // column and row, from upper left, of 16x16+ tiles in terrain.png, for top view of block
	int txrY;
	const TCHAR *filename;
    const TCHAR *altFilename;   // older file name seen in some texture packs such as R3D Craft - see https://code.google.com/p/tectonicus/source/browse/trunk/tectonicus/Data/defaultBlockConfig.xml?r=102 for these
	int flags;
} gTiles[]={
	{  0, 0, L"grass_top", L"", SWATCH_REPEAT_ALL },	// tinted by grass.png
	{  1, 0, L"stone", L"", SWATCH_REPEAT_ALL },
	{  2, 0, L"dirt", L"", SWATCH_REPEAT_ALL },
	{  3, 0, L"grass_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// we use this one, not grass_side_overlay, which is grayscale and tinted by grass.png, that's the one we actually use
	{  4, 0, L"planks_oak", L"", SWATCH_REPEAT_ALL },
	{  5, 0, L"stone_slab_side", L"", SWATCH_REPEAT_ALL },
	{  6, 0, L"stone_slab_top", L"", SWATCH_REPEAT_ALL },
	{  7, 0, L"brick", L"", SWATCH_REPEAT_ALL },
	{  8, 0, L"tnt_side", L"", SWATCH_REPEAT_ALL },
	{  9, 0, L"tnt_top", L"", SWATCH_REPEAT_ALL },
	{ 10, 0, L"tnt_bottom", L"", SWATCH_REPEAT_ALL },
	{ 11, 0, L"web", L"", SBIT_CLAMP_BOTTOM },
	{ 12, 0, L"flower_rose", L"rose", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 0, L"flower_dandelion", L"flower", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 0, L"portal", L"Portal", SWATCH_REPEAT_ALL },	// really, bluish originally, now it's better
	{ 15, 0, L"sapling_oak", L"sapling", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 1, L"cobblestone", L"", SWATCH_REPEAT_ALL },
	{  1, 1, L"bedrock", L"", SWATCH_REPEAT_ALL },
	{  2, 1, L"sand", L"", SWATCH_REPEAT_ALL },
	{  3, 1, L"gravel", L"", SWATCH_REPEAT_ALL },
	{  4, 1, L"log_oak", L"tree_side", SWATCH_REPEAT_ALL },
	{  5, 1, L"log_oak_top", L"tree_top", SWATCH_REPEAT_ALL },	// and every other log, we don't separate these out
	{  6, 1, L"iron_block", L"blockIron", SWATCH_REPEAT_ALL },
	{  7, 1, L"gold_block", L"blockGold", SWATCH_REPEAT_ALL },
	{  8, 1, L"diamond_block", L"", SWATCH_REPEAT_ALL },
	{  9, 1, L"MW_CHEST1", L"", SWATCH_CLAMP_ALL },	// was emerald - taken by chest - TODO we should make these tiles and include them. Give them better names! (e.g. MW_CHEST_LEFT_FRONT)
	{ 10, 1, L"MW_CHEST2", L"", SWATCH_CLAMP_ALL },	// taken by chest
	{ 11, 1, L"MW_CHEST3", L"", SWATCH_CLAMP_ALL },	// taken by chest
	{ 12, 1, L"mushroom_red", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 1, L"mushroom_brown", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 1, L"sapling_jungle", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 15, 1, L"fire_layer_0", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// input is fire animation strip
	{  0, 2, L"gold_ore", L"oreGold", SWATCH_REPEAT_ALL },
	{  1, 2, L"iron_ore", L"oreIron", SWATCH_REPEAT_ALL },
	{  2, 2, L"coal_ore", L"oreCoal", SWATCH_REPEAT_ALL },
	{  3, 2, L"bookshelf", L"", SWATCH_REPEAT_ALL }, // side - top and bottom are oak planks
	{  4, 2, L"cobblestone_mossy", L"stoneMoss", SWATCH_REPEAT_ALL },
	{  5, 2, L"obsidian", L"", SWATCH_REPEAT_ALL },
	{  6, 2, L"grass_side_overlay", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP }, // was "grass_side_overlay" - we use it for temporary work - grass_side_overlay tinted by grass.png, but we don't use it.
	{  7, 2, L"tallgrass", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8, 2, L"MW_WORKSPACE1", L"", SWATCH_REPEAT_ALL },	// we use it for temporary work - grass?? top grayscale, but we don't use it, nor does Mojang
	{  9, 2, L"MW_CHEST4", L"", SWATCH_CLAMP_ALL }, // was beacon - taken by chest
	{ 10, 2, L"MW_CHEST5", L"", SWATCH_CLAMP_ALL },	// taken by chest
	{ 11, 2, L"crafting_table_top", L"", SWATCH_REPEAT_ALL },
	{ 12, 2, L"furnace_front_off", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 13, 2, L"furnace_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 2, L"dispenser_front_horizontal", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 15, 2, L"dispenser_front_vertical", L"", SWATCH_REPEAT_ALL|SBIT_DECAL }, // ADD-IN; instead, input could be second fire animation strip "fire_layer_1" - TODO use both fire tiles?
	{  0, 3, L"sponge", L"", SWATCH_REPEAT_ALL },
	{  1, 3, L"glass", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  2, 3, L"diamond_ore", L"oreDiamond", SWATCH_REPEAT_ALL },
	{  3, 3, L"redstone_ore", L"oreRedstone", SWATCH_REPEAT_ALL },
	{  4, 3, L"leaves_oak", L"leaves", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },
	{  5, 3, L"coarse_dirt", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8 - replaced leaves_oak_opaque
	{  6, 3, L"stonebrick", L"", SWATCH_REPEAT_ALL },
	{  7, 3, L"deadbush", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8, 3, L"fern", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  9, 3, L"MW_CHEST6", L"", SWATCH_REPEAT_ALL },	// taken by chest
	{ 10, 3, L"MW_CHEST7", L"", SWATCH_REPEAT_ALL },	// taken by chest
	{ 11, 3, L"crafting_table_side", L"", SWATCH_REPEAT_ALL },
	{ 12, 3, L"crafting_table_front", L"", SWATCH_REPEAT_ALL },
	{ 13, 3, L"furnace_front_on", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 3, L"furnace_top", L"", SWATCH_REPEAT_ALL },
	{ 15, 3, L"sapling_spruce", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 4, L"wool_colored_white", L"cloth_0", SWATCH_REPEAT_ALL },
	{  1, 4, L"mob_spawner", L"mobSpawner", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  2, 4, L"snow", L"", SWATCH_REPEAT_ALL },
	{  3, 4, L"ice", L"", SWATCH_REPEAT_ALL },
	{  4, 4, L"grass_side_snowed", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{  5, 4, L"cactus_top", L"", SWATCH_REPEAT_ALL },
	{  6, 4, L"cactus_side", L"", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  7, 4, L"cactus_bottom", L"", SWATCH_REPEAT_ALL },
	{  8, 4, L"clay", L"", SWATCH_REPEAT_ALL },
	{  9, 4, L"reeds", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{ 10, 4, L"jukebox_side", L"musicBlock", SWATCH_REPEAT_ALL },	// was noteblock, which is now below
	{ 11, 4, L"jukebox_top", L"", SWATCH_REPEAT_ALL },
	{ 12, 4, L"waterlily", L"", SBIT_CLAMP_BOTTOM },
	{ 13, 4, L"mycelium_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 4, L"mycelium_top", L"", SWATCH_REPEAT_ALL },
	{ 15, 4, L"sapling_birch", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 5, L"torch_on", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1, 5, L"door_wood_upper", L"doorWood_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },
	{  2, 5, L"door_iron_upper", L"doorIron_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },
	{  3, 5, L"ladder", L"", SWATCH_REPEAT_ALL },
	{  4, 5, L"trapdoor", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  5, 5, L"iron_bars", L"fenceIron", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  6, 5, L"farmland_wet", L"", SWATCH_REPEAT_ALL },
	{  7, 5, L"farmland_dry", L"", SWATCH_REPEAT_ALL },
	{  8, 5, L"wheat_stage_0", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  9, 5, L"wheat_stage_1", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 10, 5, L"wheat_stage_2", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 11, 5, L"wheat_stage_3", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12, 5, L"wheat_stage_4", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 5, L"wheat_stage_5", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 5, L"wheat_stage_6", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 15, 5, L"wheat_stage_7", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 6, L"lever", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1, 6, L"door_wood_lower", L"doorWood_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{  2, 6, L"door_iron_lower", L"doorIron_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{  3, 6, L"redstone_torch_on", L"redtorch", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4, 6, L"stonebrick_mossy", L"", SWATCH_REPEAT_ALL },
	{  5, 6, L"stonebrick_cracked", L"", SWATCH_REPEAT_ALL },
	{  6, 6, L"pumpkin_top", L"", SWATCH_REPEAT_ALL },
	{  7, 6, L"netherrack", L"hellrock", SWATCH_REPEAT_ALL },
	{  8, 6, L"soul_sand", L"hellsand", SWATCH_REPEAT_ALL },
	{  9, 6, L"glowstone", L"lightgem", SWATCH_REPEAT_ALL },
	{ 10, 6, L"piston_top_sticky", L"", SWATCH_REPEAT_ALL },
	{ 11, 6, L"piston_top_normal", L"piston_top", SWATCH_REPEAT_ALL },
	{ 12, 6, L"piston_side", L"", SWATCH_REPEAT_ALL },
	{ 13, 6, L"piston_bottom", L"", SWATCH_REPEAT_ALL },
	{ 14, 6, L"piston_inner", L"piston_inner_top", SWATCH_REPEAT_ALL },
	{ 15, 6, L"melon_stem_disconnected", L"stem_straight", SBIT_CLAMP_BOTTOM|SBIT_DECAL }, // and pumpkin_stem_disconnected TODO
	{  0, 7, L"rail_normal_turned", L"", SWATCH_CLAMP_BOTTOM_AND_RIGHT },
	{  1, 7, L"wool_colored_black", L"cloth_15", SWATCH_REPEAT_ALL },
	{  2, 7, L"wool_colored_gray", L"cloth_7", SWATCH_REPEAT_ALL },
	{  3, 7, L"redstone_torch_off", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4, 7, L"log_spruce", L"tree_spruce", SWATCH_REPEAT_ALL },
	{  5, 7, L"log_birch", L"tree_birch", SWATCH_REPEAT_ALL },
	{  6, 7, L"pumpkin_side", L"", SWATCH_REPEAT_ALL },
	{  7, 7, L"pumpkin_face_off", L"", SWATCH_REPEAT_ALL },
	{  8, 7, L"pumpkin_face_on", L"", SWATCH_REPEAT_ALL },
	{  9, 7, L"cake_top", L"", SBIT_CLAMP_BOTTOM },
	{ 10, 7, L"cake_side", L"", SBIT_CLAMP_BOTTOM },
	{ 11, 7, L"cake_inner", L"", SBIT_CLAMP_BOTTOM },
	{ 12, 7, L"cake_bottom", L"", SBIT_CLAMP_BOTTOM },
	{ 13, 7, L"mushroom_block_skin_red", L"mushroom_skin_red", SWATCH_REPEAT_ALL },
	{ 14, 7, L"mushroom_block_skin_brown", L"mushroom_skin_brown", SWATCH_REPEAT_ALL },
	{ 15, 7, L"melon_stem_connected", L"stem_bent", SBIT_CLAMP_BOTTOM }, // and pumpkin_stem_connected TODO
	{  0, 8, L"rail_normal", L"", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  1, 8, L"wool_colored_red", L"cloth_14", SWATCH_REPEAT_ALL },
	{  2, 8, L"wool_colored_pink", L"cloth_6", SWATCH_REPEAT_ALL },
	{  3, 8, L"repeater_off", L"", SWATCH_REPEAT_ALL },
	{  4, 8, L"leaves_spruce", L"", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },
	{  5, 8, L"red_sandstone_bottom", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{  6, 8, L"bed_feet_top", L"", SWATCH_CLAMP_ALL },
	{  7, 8, L"bed_head_top", L"", SWATCH_CLAMP_ALL },
	{  8, 8, L"melon_side", L"", SWATCH_REPEAT_ALL },
	{  9, 8, L"melon_top", L"", SWATCH_REPEAT_ALL },
	{ 10, 8, L"cauldron_top", L"", SWATCH_CLAMP_ALL },
	{ 11, 8, L"cauldron_inner", L"", SWATCH_REPEAT_ALL },
	{ 12, 8, L"noteblock", L"musicBlock", SWATCH_REPEAT_ALL }, // ADD-IN cake item, unused, now make it matching noteblock
	{ 13, 8, L"mushroom_block_skin_stem", L"mushroom_skin_stem", SWATCH_REPEAT_ALL },
	{ 14, 8, L"mushroom_block_inside", L"mushroom_inside", SWATCH_REPEAT_ALL },
	{ 15, 8, L"vine", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// grayscale
	{  0, 9, L"lapis_block", L"blockLapis", SWATCH_REPEAT_ALL },
	{  1, 9, L"wool_colored_green", L"cloth_13", SWATCH_REPEAT_ALL },
	{  2, 9, L"wool_colored_lime", L"cloth_5", SWATCH_REPEAT_ALL },
	{  3, 9, L"repeater_on", L"repeater", SWATCH_REPEAT_ALL },
	{  4, 9, L"glass_pane_top", L"", SWATCH_REPEAT_ALL },
	{  5, 9, L"bed_feet_end", L"", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  6, 9, L"bed_feet_side", L"", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  7, 9, L"bed_head_side", L"", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  8, 9, L"bed_head_end", L"", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  9, 9, L"log_jungle", L"", SWATCH_REPEAT_ALL },
	{ 10, 9, L"cauldron_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },
	{ 11, 9, L"cauldron_bottom", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{ 12, 9, L"brewing_stand_base", L"brewingStand_base", SWATCH_REPEAT_ALL },
	{ 13, 9, L"brewing_stand", L"brewingStand", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 9, L"endframe_top", L"", SWATCH_REPEAT_ALL },
	{ 15, 9, L"endframe_side", L"", SWATCH_REPEAT_ALL },
	{  0,10, L"lapis_ore", L"oreLapis", SWATCH_REPEAT_ALL },
	{  1,10, L"wool_colored_brown", L"cloth_12", SWATCH_REPEAT_ALL },
	{  2,10, L"wool_colored_yellow", L"cloth_4", SWATCH_REPEAT_ALL },
	{  3,10, L"rail_golden", L"goldenRail", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,10, L"redstone_dust_cross", L"redstoneDust_cross", SWATCH_REPEAT_ALL },
	{  5,10, L"redstone_dust_line", L"redstoneDust_line", SWATCH_REPEAT_ALL },
	{  6,10, L"enchanting_table_top", L"", SWATCH_REPEAT_ALL },
	{  7,10, L"dragon_egg", L"dragonEgg", SWATCH_REPEAT_ALL },
	{  8,10, L"cocoa_stage_2", L"", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{  9,10, L"cocoa_stage_1", L"", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{ 10,10, L"cocoa_stage_0", L"", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{ 11,10, L"emerald_ore", L"oreEmerald", SWATCH_REPEAT_ALL },
	{ 12,10, L"trip_wire_source", L"tripWireSource", SWATCH_CLAMP_ALL },
	{ 13,10, L"trip_wire", L"tripWire", SWATCH_CLAMP_ALL },
	{ 14,10, L"endframe_eye", L"", SWATCH_REPEAT_ALL },
	{ 15,10, L"end_stone", L"", SWATCH_REPEAT_ALL },
	{  0,11, L"sandstone_top", L"", SWATCH_REPEAT_ALL },
	{  1,11, L"wool_colored_blue", L"cloth_11", SWATCH_REPEAT_ALL },
	{  2,11, L"wool_colored_light_blue", L"cloth_3", SWATCH_REPEAT_ALL },
	{  3,11, L"rail_golden_powered", L"goldenRail_powered", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,11, L"MW_REDSTONE_WIRE_DOT", L"", SWATCH_REPEAT_ALL }, // MANUFACTURED was redstone_dust_cross_overlay, redstoneDust_cross_overlay - we now use it for a redstone dot
	{  5,11, L"log_acacia", L"", 0x0 },	// ADD-IN 1.7.2
	{  6,11, L"enchanting_table_side", L"", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  7,11, L"enchanting_table_bottom", L"", SWATCH_REPEAT_ALL },
	{  8,11, L"command_block", L"commandBlock", SWATCH_REPEAT_ALL },
	{  9,11, L"itemframe_background", L"", SWATCH_REPEAT_ALL },	// frame around item, unimplemented, TODO
	{ 10,11, L"flower_pot", L"flowerPot", SWATCH_CLAMP_ALL },
	{ 11,11, L"log_birch_top", L"tree_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 12,11, L"log_spruce_top", L"tree_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 13,11, L"log_jungle_top", L"tree_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 14,11, L"pumpkin_stem_disconnected", L"stem_straight", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN
	{ 15,11, L"pumpkin_stem_connected", L"stem_bent", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN
	{  0,12, L"sandstone_normal", L"sandstone_side", SWATCH_REPEAT_ALL },
	{  1,12, L"wool_colored_purple", L"cloth_10", SWATCH_REPEAT_ALL },
	{  2,12, L"wool_colored_magenta", L"cloth_2", SWATCH_REPEAT_ALL },
	{  3,12, L"rail_detector", L"detectorRail", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,12, L"leaves_jungle", L"", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },
	{  5,12, L"red_sandstone_carved", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{  6,12, L"planks_spruce", L"", SWATCH_REPEAT_ALL },
	{  7,12, L"planks_jungle", L"", SWATCH_REPEAT_ALL },
	{  8,12, L"carrots_stage_0", L"carrots_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_0 in basic game, but can be different in texture packs
	{  9,12, L"carrots_stage_1", L"carrots_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_1
	{ 10,12, L"carrots_stage_2", L"carrots_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_2
	{ 11,12, L"carrots_stage_3", L"carrots_3", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12,12, L"potatoes_stage_0", L"potatoes_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13,12, L"potatoes_stage_1", L"potatoes_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{ 14,12, L"potatoes_stage_2", L"potatoes_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{ 15,12, L"potatoes_stage_3", L"potatoes_3", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{  0,13, L"sandstone_bottom", L"", SWATCH_REPEAT_ALL },
	{  1,13, L"wool_colored_cyan", L"cloth_9", SWATCH_REPEAT_ALL },
	{  2,13, L"wool_colored_orange", L"cloth_1", SWATCH_REPEAT_ALL },
	{  3,13, L"redstone_lamp_off", L"redstoneLight", SWATCH_REPEAT_ALL },
	{  4,13, L"redstone_lamp_on", L"redstoneLight_lit", SWATCH_REPEAT_ALL },
	{  5,13, L"stonebrick_carved", L"", SWATCH_REPEAT_ALL },
	{  6,13, L"planks_birch", L"", SWATCH_REPEAT_ALL },
	{  7,13, L"anvil_base", L"", SWATCH_REPEAT_ALL },
	{  8,13, L"anvil_top_damaged_1", L"", SWATCH_REPEAT_ALL },
	{  9,13, L"MW_ENDER_CHEST_LATCH", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 10,13, L"MW_ENDER_CHEST_SIDE1", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 11,13, L"MW_ENDER_CHEST_SIDE2", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 12,13, L"MW_ENDER_CHEST_SIDE3", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 13,13, L"leaves_birch", L"", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },	// ADD-IN
	{ 14,13, L"red_sandstone_normal", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 15,13, L"water_still", L"", SWATCH_REPEAT_ALL },
	{  0,14, L"nether_brick", L"netherBrick", SWATCH_REPEAT_ALL },
	{  1,14, L"wool_colored_silver", L"cloth_8", SWATCH_REPEAT_ALL },
	{  2,14, L"nether_wart_stage_0", L"netherStalk_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  3,14, L"nether_wart_stage_1", L"netherStalk_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,14, L"nether_wart_stage_2", L"netherStalk_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  5,14, L"sandstone_carved", L"", SWATCH_REPEAT_ALL },
	{  6,14, L"sandstone_smooth", L"", SWATCH_REPEAT_ALL },
	{  7,14, L"anvil_top_damaged_0", L"anvil_top", SWATCH_REPEAT_ALL },
	{  8,14, L"anvil_top_damaged_2", L"", SWATCH_REPEAT_ALL },
	{  9,14, L"MW_ENDER_CHEST_TOP1", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 10,14, L"MW_ENDER_CHEST_TOP2", L"", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 11,14, L"beacon", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },	// was unused, beacon was moved to here
	{ 12,14, L"emerald_block", L"blockEmerald", SWATCH_REPEAT_ALL },	// was unused, emerald was moved to here
	{ 13,14, L"coal_block", L"blockCoal", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still
	{ 14,14, L"comparator_off", L"comparator", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still
	{ 15,14, L"comparator_on", L"comparator_lit", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still
	{  0,15, L"MW_FLATTENED_TORCH", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened torch top
	{  1,15, L"MW_FLATTENED_REDSTONE_TORCH_ON", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened redstone torch top, on
	{  2,15, L"MW_FLATTENED_REDSTONE_TORCH_OFF", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened redstone torch top, off
	{  3,15, L"MW_REDSTONE_WIRE_ANGLED", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED used for angled redstone wire
	{  4,15, L"MW_REDSTONE_WIRE_THREE_WAY", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED used for three-way redstone wire
	{  5,15, L"daylight_detector_side", L"daylightDetector_side", SWATCH_REPEAT_ALL },	// destroy, etc. unused
	{  6,15, L"daylight_detector_top", L"daylightDetector_top", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{  7,15, L"dropper_front_horizontal", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN destroy, etc. unused
	{  8,15, L"dropper_front_vertical", L"", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{  9,15, L"hay_block_side", L"hayBlock", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 10,15, L"hay_block_top", L"hayBlock_top", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 11,15, L"hopper_inside", L"", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 12,15, L"hopper_outside", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN destroy, etc. unused
	{ 13,15, L"hopper_top", L"", SWATCH_CLAMP_ALL },	// ADD-IN destroy, etc. unused
	{ 14,15, L"redstone_block", L"blockRedstone", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still
	{ 15,15, L"lava_still", L"", SWATCH_REPEAT_ALL },
	// brave new world, off the 256x256 edge
	{  0,16, L"hardened_clay_stained_white", L"", SWATCH_REPEAT_ALL },
	{  1,16, L"hardened_clay_stained_orange", L"", SWATCH_REPEAT_ALL },
	{  2,16, L"hardened_clay_stained_magenta", L"", SWATCH_REPEAT_ALL },
	{  3,16, L"hardened_clay_stained_light_blue", L"", SWATCH_REPEAT_ALL },
	{  4,16, L"hardened_clay_stained_yellow", L"", SWATCH_REPEAT_ALL },
	{  5,16, L"hardened_clay_stained_lime", L"", SWATCH_REPEAT_ALL },
	{  6,16, L"hardened_clay_stained_pink", L"", SWATCH_REPEAT_ALL },
	{  7,16, L"hardened_clay_stained_gray", L"", SWATCH_REPEAT_ALL },
	{  8,16, L"hardened_clay_stained_silver", L"", SWATCH_REPEAT_ALL },
	{  9,16, L"hardened_clay_stained_cyan", L"", SWATCH_REPEAT_ALL },
	{ 10,16, L"hardened_clay_stained_purple", L"", SWATCH_REPEAT_ALL },
	{ 11,16, L"hardened_clay_stained_blue", L"", SWATCH_REPEAT_ALL },
	{ 12,16, L"hardened_clay_stained_brown", L"", SWATCH_REPEAT_ALL },
	{ 13,16, L"hardened_clay_stained_green", L"", SWATCH_REPEAT_ALL },
	{ 14,16, L"hardened_clay_stained_red", L"", SWATCH_REPEAT_ALL },
	{ 15,16, L"hardened_clay_stained_black", L"", SWATCH_REPEAT_ALL },
	{  0,17, L"hardened_clay", L"clayHardened", SWATCH_REPEAT_ALL },
	{  1,17, L"quartz_block_bottom", L"quartzblock_bottom", SWATCH_REPEAT_ALL },
	{  2,17, L"quartz_block_chiseled_top", L"quartzblock_chiseled_top", SWATCH_REPEAT_ALL },
	{  3,17, L"quartz_block_chiseled", L"quartzblock_chiseled", SWATCH_REPEAT_ALL },
	{  4,17, L"quartz_block_lines_top", L"quartzblock_lines_top", SWATCH_REPEAT_ALL },
	{  5,17, L"quartz_block_lines", L"quartzblock_lines", SWATCH_REPEAT_ALL },
	{  6,17, L"quartz_block_side", L"quartzblock_side", SWATCH_REPEAT_ALL },
	{  7,17, L"quartz_block_top", L"quartzblock_top", SWATCH_REPEAT_ALL },
	{  8,17, L"quartz_ore", L"netherquartz", SWATCH_REPEAT_ALL },
	{  9,17, L"rail_activator_powered", L"activatorRail_powered", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 10,17, L"rail_activator", L"activatorRail", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 11,17, L"rail_detector_powered", L"detectorRail_on", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	// 1.7
	{ 12,17, L"ice_packed", L"", SWATCH_REPEAT_ALL },
	{ 13,17, L"red_sand", L"", SWATCH_REPEAT_ALL },
	{ 14,17, L"dirt_podzol_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 15,17, L"dirt_podzol_top", L"", SWATCH_REPEAT_ALL },
	{  0,18, L"double_plant_sunflower_back", L"doublePlant_sunflower_back", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1,18, L"double_plant_sunflower_front", L"doublePlant_sunflower_front", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  2,18, L"double_plant_sunflower_bottom", L"doublePlant_sunflower_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  3,18, L"double_plant_sunflower_top", L"doublePlant_sunflower_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,18, L"double_plant_syringa_bottom", L"doublePlant_syringa_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },	// lily
	{  5,18, L"double_plant_syringa_top", L"doublePlant_syringa_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  6,18, L"double_plant_grass_bottom", L"doublePlant_grass_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  7,18, L"double_plant_grass_top", L"doublePlant_grass_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8,18, L"double_plant_fern_bottom", L"doublePlant_fern_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  9,18, L"double_plant_fern_top", L"doublePlant_fern_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 10,18, L"double_plant_rose_bottom", L"doublePlant_rose_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 11,18, L"double_plant_rose_top", L"doublePlant_rose_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12,18, L"double_plant_paeonia_bottom", L"doublePlant_paeonia_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },	// peony
	{ 13,18, L"double_plant_paeonia_top", L"doublePlant_paeonia_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14,18, L"sapling_acacia", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 15,18, L"sapling_roofed_oak", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// yes, "roofed"
	{  0,19, L"flower_blue_orchid", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1,19, L"flower_allium", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  2,19, L"flower_houstonia", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// azure bluet
	{  3,19, L"flower_tulip_red", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,19, L"flower_tulip_orange", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  5,19, L"flower_tulip_white", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  6,19, L"flower_tulip_pink", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  7,19, L"flower_oxeye_daisy", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8,19, L"flower_paeonia", L"", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// not used currently, but save for now.
	{  9,19, L"leaves_acacia", L"", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },	// ADD-IN 1.7.2
	{ 10,19, L"red_sandstone_smooth", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 11,19, L"leaves_big_oak", L"", SWATCH_REPEAT_ALL|SBIT_DECAL|SBIT_LEAVES },	// ADD-IN 1.7.2
	{ 12,19, L"red_sandstone_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 13,19, L"log_acacia_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
	{ 14,19, L"log_big_oak", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
	{ 15,19, L"log_big_oak_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
	{  0,20, L"glass_white", L"", SWATCH_REPEAT_ALL },
	{  1,20, L"glass_orange", L"", SWATCH_REPEAT_ALL },
	{  2,20, L"glass_magenta", L"", SWATCH_REPEAT_ALL },
	{  3,20, L"glass_light_blue", L"", SWATCH_REPEAT_ALL },
	{  4,20, L"glass_yellow", L"", SWATCH_REPEAT_ALL },
	{  5,20, L"glass_lime", L"", SWATCH_REPEAT_ALL },
	{  6,20, L"glass_pink", L"", SWATCH_REPEAT_ALL },
	{  7,20, L"glass_gray", L"", SWATCH_REPEAT_ALL },
	{  8,20, L"glass_silver", L"", SWATCH_REPEAT_ALL },
	{  9,20, L"glass_cyan", L"", SWATCH_REPEAT_ALL },
	{ 10,20, L"glass_purple", L"", SWATCH_REPEAT_ALL },
	{ 11,20, L"glass_blue", L"", SWATCH_REPEAT_ALL },
	{ 12,20, L"glass_brown", L"", SWATCH_REPEAT_ALL },
	{ 13,20, L"glass_green", L"", SWATCH_REPEAT_ALL },
	{ 14,20, L"glass_red", L"", SWATCH_REPEAT_ALL },
	{ 15,20, L"glass_black", L"", SWATCH_REPEAT_ALL },
	{  0,21, L"glass_pane_top_white", L"", SWATCH_REPEAT_ALL },
	{  1,21, L"glass_pane_top_orange", L"", SWATCH_REPEAT_ALL },
	{  2,21, L"glass_pane_top_magenta", L"", SWATCH_REPEAT_ALL },
	{  3,21, L"glass_pane_top_light_blue", L"", SWATCH_REPEAT_ALL },
	{  4,21, L"glass_pane_top_yellow", L"", SWATCH_REPEAT_ALL },
	{  5,21, L"glass_pane_top_lime", L"", SWATCH_REPEAT_ALL },
	{  6,21, L"glass_pane_top_pink", L"", SWATCH_REPEAT_ALL },
	{  7,21, L"glass_pane_top_gray", L"", SWATCH_REPEAT_ALL },
	{  8,21, L"glass_pane_top_silver", L"", SWATCH_REPEAT_ALL },
	{  9,21, L"glass_pane_top_cyan", L"", SWATCH_REPEAT_ALL },
	{ 10,21, L"glass_pane_top_purple", L"", SWATCH_REPEAT_ALL },
	{ 11,21, L"glass_pane_top_blue", L"", SWATCH_REPEAT_ALL },
	{ 12,21, L"glass_pane_top_brown", L"", SWATCH_REPEAT_ALL },
	{ 13,21, L"glass_pane_top_green", L"", SWATCH_REPEAT_ALL },
	{ 14,21, L"glass_pane_top_red", L"", SWATCH_REPEAT_ALL },
	{ 15,21, L"glass_pane_top_black", L"", SWATCH_REPEAT_ALL },
	{  0,22, L"planks_acacia", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
	{  1,22, L"planks_big_oak", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
	{  2,22, L"iron_trapdoor", L"", SWATCH_REPEAT_ALL|SBIT_DECAL },	// 1.8
	{  3,22, L"slime", L"", SWATCH_REPEAT_ALL },
	{  4,22, L"stone_andesite", L"", SWATCH_REPEAT_ALL },
	{  5,22, L"stone_andesite_smooth", L"", SWATCH_REPEAT_ALL },
	{  6,22, L"stone_diorite", L"", SWATCH_REPEAT_ALL },
	{  7,22, L"stone_diorite_smooth", L"", SWATCH_REPEAT_ALL },
	{  8,22, L"stone_granite", L"", SWATCH_REPEAT_ALL },
	{  9,22, L"stone_granite_smooth", L"", SWATCH_REPEAT_ALL },
	{ 10,22, L"prismarine_bricks", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 11,22, L"prismarine_dark", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 12,22, L"prismarine_rough", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 13,22, L"daylight_detector_inverted_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 14,22, L"sea_lantern", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{ 15,22, L"sponge_wet", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
	{  0,23, L"door_spruce_lower", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	{  1,23, L"door_spruce_upper", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	{  2,23, L"door_birch_lower", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	{  3,23, L"door_birch_upper", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	{  4,23, L"door_jungle_lower", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },	// ADD-IN 1.8
	{  5,23, L"door_jungle_upper", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },	// ADD-IN 1.8
	{  6,23, L"door_acacia_lower", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },	// ADD-IN 1.8
	{  7,23, L"door_acacia_upper", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },	// ADD-IN 1.8
	{  8,23, L"door_dark_oak_lower", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	{  9,23, L"door_dark_oak_upper", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN 1.8
	//{ 10,23, L"banner_upper", L"", SWATCH_CLAMP_ALL|SBIT_BLACK_ALPHA },	// top of banner, squished down from 20 wide
	//{ 11,23, L"banner_lower", L"", SWATCH_CLAMP_ALL|SBIT_BLACK_ALPHA },	// bottom of banner, squished down from 20 wide
	{ 10,23, L"MW_BANNER_UPPER", L"", SWATCH_CLAMP_ALL },	// top of banner, squished down from 20 wide
	{ 11,23, L"MW_BANNER_LOWER", L"", SWATCH_CLAMP_ALL },	// bottom of banner, squished down from 20 wide
	{ 12,23, L"", L"", SWATCH_REPEAT_ALL },
	{ 13,23, L"", L"", SWATCH_REPEAT_ALL },
	{ 14,23, L"", L"", SWATCH_REPEAT_ALL },
	{ 15,23, L"", L"", SWATCH_REPEAT_ALL },
};

// tiles we know we don't use
const TCHAR * gUnneeded[]={
    L"banner_lower",
    L"banner_upper",
    L"chest_top_left",
    L"chest_top_right",
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
    L"lava_flow",
    L"redstone_dust_cross_overlay",
    L"redstone_dust_line_overlay",
    L"water_flow",
    // older names
    L"leaves_birch_opaque",
    L"leaves_jungle_opaque",
    L"leaves_oak_opaque",
    L"leaves_spruce_opaque",
    L"redstoneDust_cross_overlay",
    L"redstoneDust_line_overlay",
    L""
};

/*
Loose ends:

pumpkin and melon connectors

glass panes

Unused: lava_flow, water_flow - someday maybe add these? TODO

Issues to clean up: location of 7 chest tiles (and add an eighth for the top, and make a latch), 6 ender chest tiles
*/

#endif