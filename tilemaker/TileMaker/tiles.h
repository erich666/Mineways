// PngXfer.cpp : Defines the entry point for the console application.
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

// types of blocks: tiling, billboard, and sides (which tile only horizontally)
#define SWATCH_REPEAT_ALL                   (SBIT_REPEAT_SIDES|SBIT_REPEAT_TOP_BOTTOM)
#define SWATCH_REPEAT_SIDES_ELSE_CLAMP      (SBIT_REPEAT_SIDES|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
#define SWATCH_TILE_BOTTOM_AND_TOP          SBIT_REPEAT_TOP_BOTTOM
#define SWATCH_CLAMP_BOTTOM_AND_RIGHT       (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT)
#define SWATCH_CLAMP_BOTTOM_AND_TOP         (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
#define SWATCH_CLAMP_ALL_BUT_TOP            (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)
#define SWATCH_CLAMP_ALL                    (SBIT_CLAMP_TOP|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)


#define TOTAL_TILES (20*16)
static struct {
	int txrX;   // column and row, from upper left, of 16x16+ tiles in terrain.png, for top view of block
	int txrY;
	const TCHAR *filename;
	int flags;
} gTiles[]={
	{  0, 0, L"grass_top", SWATCH_REPEAT_ALL },	// tinted by grass.png
	{  1, 0, L"stone", SWATCH_REPEAT_ALL },
	{  2, 0, L"dirt", SWATCH_REPEAT_ALL },
	{  3, 0, L"grass_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// we use this one, not grass_side_overlay, which is grayscale and tinted by grass.png, that's the one we actually use
	{  4, 0, L"planks_oak", SWATCH_REPEAT_ALL },
	{  5, 0, L"stone_slab_side", SWATCH_REPEAT_ALL },
	{  6, 0, L"stone_slab_top", SWATCH_REPEAT_ALL },
	{  7, 0, L"brick", SWATCH_REPEAT_ALL },
	{  8, 0, L"tnt_side", SWATCH_REPEAT_ALL },
	{  9, 0, L"tnt_top", SWATCH_REPEAT_ALL },
	{ 10, 0, L"tnt_bottom", SWATCH_REPEAT_ALL },
	{ 11, 0, L"web", SBIT_CLAMP_BOTTOM },
	{ 12, 0, L"flower_rose", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 0, L"flower_dandelion", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 0, L"portal", SWATCH_REPEAT_ALL },	// really, bluish originally, now it's better
	{ 15, 0, L"sapling_oak", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 1, L"cobblestone", SWATCH_REPEAT_ALL },
	{  1, 1, L"bedrock", SWATCH_REPEAT_ALL },
	{  2, 1, L"sand", SWATCH_REPEAT_ALL },
	{  3, 1, L"gravel", SWATCH_REPEAT_ALL },
	{  4, 1, L"log_oak", SWATCH_REPEAT_ALL },
	{  5, 1, L"log_oak_top", SWATCH_REPEAT_ALL },	// and every other log, we don't separate these out
	{  6, 1, L"iron_block", SWATCH_REPEAT_ALL },
	{  7, 1, L"gold_block", SWATCH_REPEAT_ALL },
	{  8, 1, L"diamond_block", SWATCH_REPEAT_ALL },
	{  9, 1, L"MW_CHEST1", SWATCH_CLAMP_ALL },	// was emerald - taken by chest - TODOTODO we should make these tiles and include them. Give them better names! (e.g. MW_CHEST_LEFT_FRONT)
	{ 10, 1, L"MW_CHEST2", SWATCH_CLAMP_ALL },	// taken by chest
	{ 11, 1, L"MW_CHEST3", SWATCH_CLAMP_ALL },	// taken by chest
	{ 12, 1, L"mushroom_red", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 1, L"mushroom_brown", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 1, L"sapling_jungle", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 15, 1, L"fire_layer_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// input is fire animation strip
	{  0, 2, L"gold_ore", SWATCH_REPEAT_ALL },
	{  1, 2, L"iron_ore", SWATCH_REPEAT_ALL },
	{  2, 2, L"coal_ore", SWATCH_REPEAT_ALL },
	{  3, 2, L"bookshelf", SWATCH_REPEAT_ALL }, // side - top and bottom are oak planks
	{  4, 2, L"cobblestone_mossy", SWATCH_REPEAT_ALL },
	{  5, 2, L"obsidian", SWATCH_REPEAT_ALL },
	{  6, 2, L"MW_WORKSPACE1", SWATCH_REPEAT_SIDES_ELSE_CLAMP }, // was "grass_side_overlay" - we use it for temporary work - grass_side_overlay tinted by grass.png, but we don't use it.
	{  7, 2, L"tallgrass", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8, 2, L"MW_WORKSPACE2", SWATCH_REPEAT_ALL },	// we use it for temporary work - grass?? top grayscale, but we don't use it, nor does Mojang
	{  9, 2, L"MW_CHEST4", SWATCH_CLAMP_ALL }, // was beacon - taken by chest
	{ 10, 2, L"MW_CHEST5", SWATCH_CLAMP_ALL },	// taken by chest
	{ 11, 2, L"crafting_table_top", SWATCH_REPEAT_ALL },
	{ 12, 2, L"furnace_front_off", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 13, 2, L"furnace_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 2, L"dispenser_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 15, 2, L"dispenser_front_vertical", SWATCH_REPEAT_ALL|SBIT_DECAL }, // ADD-IN; instead, input could be second fire animation strip "fire_layer_1" - TODO use both fire tiles?
	{  0, 3, L"sponge", SWATCH_REPEAT_ALL },
	{  1, 3, L"glass", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  2, 3, L"diamond_ore", SWATCH_REPEAT_ALL },
	{  3, 3, L"redstone_ore", SWATCH_REPEAT_ALL },
	{  4, 3, L"leaves_oak", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  5, 3, L"leaves_oak_opaque", SWATCH_REPEAT_ALL },
	{  6, 3, L"stonebrick", SWATCH_REPEAT_ALL },
	{  7, 3, L"deadbush", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8, 3, L"fern", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  9, 3, L"MW_CHEST6", SWATCH_REPEAT_ALL },	// taken by chest
	{ 10, 3, L"MW_CHEST7", SWATCH_REPEAT_ALL },	// taken by chest
	{ 11, 3, L"crafting_table_side", SWATCH_REPEAT_ALL },
	{ 12, 3, L"crafting_table_front", SWATCH_REPEAT_ALL },
	{ 13, 3, L"furnace_front_on", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 3, L"furnace_top", SWATCH_REPEAT_ALL },
	{ 15, 3, L"sapling_spruce", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 4, L"wool_colored_white", SWATCH_REPEAT_ALL },
	{  1, 4, L"mob_spawner", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  2, 4, L"snow", SWATCH_REPEAT_ALL },
	{  3, 4, L"ice", SWATCH_REPEAT_ALL },
	{  4, 4, L"grass_side_snowed", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{  5, 4, L"cactus_top", SWATCH_REPEAT_ALL },
	{  6, 4, L"cactus_side", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  7, 4, L"cactus_bottom", SWATCH_REPEAT_ALL },
	{  8, 4, L"clay", SWATCH_REPEAT_ALL },
	{  9, 4, L"reeds", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{ 10, 4, L"jukebox_side", SWATCH_REPEAT_ALL },	// was noteblock, which is now below
	{ 11, 4, L"jukebox_top", SWATCH_REPEAT_ALL },
	{ 12, 4, L"waterlily", SBIT_CLAMP_BOTTOM },
	{ 13, 4, L"mycelium_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 14, 4, L"mycelium_top", SWATCH_REPEAT_ALL },
	{ 15, 4, L"sapling_birch", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 5, L"torch_on", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1, 5, L"door_wood_upper", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  2, 5, L"door_iron_upper", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  3, 5, L"ladder", SWATCH_REPEAT_ALL },
	{  4, 5, L"trapdoor", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  5, 5, L"iron_bars", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  6, 5, L"farmland_wet", SWATCH_REPEAT_ALL },
	{  7, 5, L"farmland_dry", SWATCH_REPEAT_ALL },
	{  8, 5, L"wheat_stage_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  9, 5, L"wheat_stage_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 10, 5, L"wheat_stage_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 11, 5, L"wheat_stage_3", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12, 5, L"wheat_stage_4", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13, 5, L"wheat_stage_5", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 5, L"wheat_stage_6", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 15, 5, L"wheat_stage_7", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  0, 6, L"lever", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1, 6, L"door_wood_lower", SWATCH_REPEAT_ALL },
	{  2, 6, L"door_iron_lower", SWATCH_REPEAT_ALL },
	{  3, 6, L"redstone_torch_on", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4, 6, L"stonebrick_mossy", SWATCH_REPEAT_ALL },
	{  5, 6, L"stonebrick_cracked", SWATCH_REPEAT_ALL },
	{  6, 6, L"pumpkin_top", SWATCH_REPEAT_ALL },
	{  7, 6, L"netherrack", SWATCH_REPEAT_ALL },
	{  8, 6, L"soul_sand", SWATCH_REPEAT_ALL },
	{  9, 6, L"glowstone", SWATCH_REPEAT_ALL },
	{ 10, 6, L"piston_top_sticky", SWATCH_REPEAT_ALL },
	{ 11, 6, L"piston_top_normal", SWATCH_REPEAT_ALL },
	{ 12, 6, L"piston_side", SWATCH_REPEAT_ALL },
	{ 13, 6, L"piston_bottom", SWATCH_REPEAT_ALL },
	{ 14, 6, L"piston_inner", SWATCH_REPEAT_ALL },
	{ 15, 6, L"melon_stem_disconnected", SBIT_CLAMP_BOTTOM|SBIT_DECAL }, // and pumpkin_stem_disconnected TODO
	{  0, 7, L"rail_normal_turned", SWATCH_CLAMP_BOTTOM_AND_RIGHT },
	{  1, 7, L"wool_colored_black", SWATCH_REPEAT_ALL },
	{  2, 7, L"wool_colored_gray", SWATCH_REPEAT_ALL },
	{  3, 7, L"redstone_torch_off", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4, 7, L"log_spruce", SWATCH_REPEAT_ALL },
	{  5, 7, L"log_birch", SWATCH_REPEAT_ALL },
	{  6, 7, L"pumpkin_side", SWATCH_REPEAT_ALL },
	{  7, 7, L"pumpkin_face_off", SWATCH_REPEAT_ALL },
	{  8, 7, L"pumpkin_face_on", SWATCH_REPEAT_ALL },
	{  9, 7, L"cake_top", SBIT_CLAMP_BOTTOM },
	{ 10, 7, L"cake_side", SBIT_CLAMP_BOTTOM },
	{ 11, 7, L"cake_inner", SBIT_CLAMP_BOTTOM },
	{ 12, 7, L"cake_bottom", SBIT_CLAMP_BOTTOM },
	{ 13, 7, L"mushroom_block_skin_red", SWATCH_REPEAT_ALL },
	{ 14, 7, L"mushroom_block_skin_brown", SWATCH_REPEAT_ALL },
	{ 15, 7, L"melon_stem_connected", SBIT_CLAMP_BOTTOM }, // and pumpkin_stem_connected TODO
	{  0, 8, L"rail_normal", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  1, 8, L"wool_colored_red", SWATCH_REPEAT_ALL },
	{  2, 8, L"wool_colored_pink", SWATCH_REPEAT_ALL },
	{  3, 8, L"repeater_off", SWATCH_REPEAT_ALL },
	{  4, 8, L"leaves_spruce", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  5, 8, L"leaves_spruce_opaque", SWATCH_REPEAT_ALL },
	{  6, 8, L"bed_feet_top", SWATCH_CLAMP_ALL },
	{  7, 8, L"bed_head_top", SWATCH_CLAMP_ALL },
	{  8, 8, L"melon_side", SWATCH_REPEAT_ALL },
	{  9, 8, L"melon_top", SWATCH_REPEAT_ALL },
	{ 10, 8, L"cauldron_top", SWATCH_CLAMP_ALL },
	{ 11, 8, L"cauldron_inner", SWATCH_REPEAT_ALL },
	{ 12, 8, L"noteblock", SWATCH_REPEAT_ALL }, // ADD-IN cake item, unused, now make it matching noteblock
	{ 13, 8, L"mushroom_block_skin_stem", SWATCH_REPEAT_ALL },
	{ 14, 8, L"mushroom_block_inside", SWATCH_REPEAT_ALL },
	{ 15, 8, L"vine", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// grayscale
	{  0, 9, L"lapis_block", SWATCH_REPEAT_ALL },
	{  1, 9, L"wool_colored_green", SWATCH_REPEAT_ALL },
	{  2, 9, L"wool_colored_lime", SWATCH_REPEAT_ALL },
	{  3, 9, L"repeater_on", SWATCH_REPEAT_ALL },
	{  4, 9, L"glass_pane_top", SWATCH_REPEAT_ALL },
	{  5, 9, L"bed_feet_end", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  6, 9, L"bed_feet_side", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  7, 9, L"bed_head_side", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  8, 9, L"bed_head_end", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  9, 9, L"log_jungle", SWATCH_REPEAT_ALL },
	{ 10, 9, L"cauldron_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP|SBIT_DECAL },
	{ 11, 9, L"cauldron_bottom", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{ 12, 9, L"brewing_stand_base", SWATCH_REPEAT_ALL },
	{ 13, 9, L"brewing_stand", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14, 9, L"endframe_top", SWATCH_REPEAT_ALL },
	{ 15, 9, L"endframe_side", SWATCH_REPEAT_ALL },
	{  0,10, L"lapis_ore", SWATCH_REPEAT_ALL },
	{  1,10, L"wool_colored_brown", SWATCH_REPEAT_ALL },
	{  2,10, L"wool_colored_yellow", SWATCH_REPEAT_ALL },
	{  3,10, L"rail_golden", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,10, L"redstone_dust_cross", SWATCH_REPEAT_ALL },
	{  5,10, L"redstone_dust_line", SWATCH_REPEAT_ALL },
	{  6,10, L"enchanting_table_top", SWATCH_REPEAT_ALL },
	{  7,10, L"dragon_egg", SWATCH_REPEAT_ALL },
	{  8,10, L"cocoa_stage_2", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{  9,10, L"cocoa_stage_1", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{ 10,10, L"cocoa_stage_0", SWATCH_CLAMP_ALL|SBIT_DECAL },
	{ 11,10, L"emerald_ore", SWATCH_REPEAT_ALL },
	{ 12,10, L"trip_wire_source", SWATCH_CLAMP_ALL },
	{ 13,10, L"trip_wire", SWATCH_CLAMP_ALL },
	{ 14,10, L"endframe_eye", SWATCH_REPEAT_ALL },
	{ 15,10, L"end_stone", SWATCH_REPEAT_ALL },
	{  0,11, L"sandstone_top", SWATCH_REPEAT_ALL },
	{  1,11, L"wool_colored_blue", SWATCH_REPEAT_ALL },
	{  2,11, L"wool_colored_light_blue", SWATCH_REPEAT_ALL },
	{  3,11, L"rail_golden_powered", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,11, L"MW_REDSTONE_WIRE_CROSS", SWATCH_REPEAT_ALL }, // MANUFACTURED was redstone_dust_cross_overlay - we now use it for a redstone dot
	{  5,11, L"MW_REDSTONE_WIRE_LINE", SWATCH_REPEAT_ALL }, // MANUFACTURED was redstone_dust_line_overlay - do we use it for anything? TODOTODO
	{  6,11, L"enchanting_table_side", SWATCH_CLAMP_ALL_BUT_TOP|SBIT_DECAL },
	{  7,11, L"enchanting_table_bottom", SWATCH_REPEAT_ALL },
	{  8,11, L"command_block", SWATCH_REPEAT_ALL },
	{  9,11, L"itemframe_background", SWATCH_REPEAT_ALL },	// frame around item, unimplemented, TODO
	{ 10,11, L"flower_pot", SWATCH_CLAMP_ALL },
	{ 11,11, L"log_birch_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 12,11, L"log_spruce_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 13,11, L"log_jungle_top", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 14,11, L"pumpkin_stem_disconnected", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN
	{ 15,11, L"pumpkin_stem_connected", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN
	{  0,12, L"sandstone_normal", SWATCH_REPEAT_ALL },
	{  1,12, L"wool_colored_purple", SWATCH_REPEAT_ALL },
	{  2,12, L"wool_colored_magenta", SWATCH_REPEAT_ALL },
	{  3,12, L"rail_detector", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{  4,12, L"leaves_jungle", SWATCH_REPEAT_ALL|SBIT_DECAL },
	{  5,12, L"leaves_jungle_opaque", SWATCH_REPEAT_ALL },
	{  6,12, L"planks_spruce", SWATCH_REPEAT_ALL },
	{  7,12, L"planks_jungle", SWATCH_REPEAT_ALL },
	{  8,12, L"carrots_stage_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_0 in basic game, but can be different in texture packs
	{  9,12, L"carrots_stage_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_1
	{ 10,12, L"carrots_stage_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// also potatoes_stage_2
	{ 11,12, L"carrots_stage_3", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12,12, L"potatoes_stage_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 13,12, L"potatoes_stage_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{ 14,12, L"potatoes_stage_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{ 15,12, L"potatoes_stage_3", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// ADD-IN was water_still duplicate- use for potatoes stages 0-2
	{  0,13, L"sandstone_bottom", SWATCH_REPEAT_ALL },
	{  1,13, L"wool_colored_cyan", SWATCH_REPEAT_ALL },
	{  2,13, L"wool_colored_orange", SWATCH_REPEAT_ALL },
	{  3,13, L"redstone_lamp_off", SWATCH_REPEAT_ALL },
	{  4,13, L"redstone_lamp_on", SWATCH_REPEAT_ALL },
	{  5,13, L"stonebrick_carved", SWATCH_REPEAT_ALL },
	{  6,13, L"planks_birch", SWATCH_REPEAT_ALL },
	{  7,13, L"anvil_base", SWATCH_REPEAT_ALL },
	{  8,13, L"anvil_top_damaged_1", SWATCH_REPEAT_ALL },
	{  9,13, L"MW_ENDER_CHEST_LATCH", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 10,13, L"MW_ENDER_CHEST_SIDE1", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 11,13, L"MW_ENDER_CHEST_SIDE2", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 12,13, L"MW_ENDER_CHEST_SIDE3", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 13,13, L"leaves_birch", SWATCH_REPEAT_ALL|SBIT_DECAL },	// ADD-IN
	{ 14,13, L"leaves_birch_opaque", SWATCH_REPEAT_ALL },	// ADD-IN
	{ 15,13, L"water_still", SWATCH_REPEAT_ALL },
	{  0,14, L"nether_brick", SWATCH_REPEAT_ALL },
	{  1,14, L"wool_colored_silver", SWATCH_REPEAT_ALL },
	{  2,14, L"nether_wart_stage_0", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  3,14, L"nether_wart_stage_1", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,14, L"nether_wart_stage_2", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  5,14, L"sandstone_carved", SWATCH_REPEAT_ALL },
	{  6,14, L"sandstone_smooth", SWATCH_REPEAT_ALL },
	{  7,14, L"anvil_top_damaged_0", SWATCH_REPEAT_ALL },
	{  8,14, L"anvil_top_damaged_2", SWATCH_REPEAT_ALL },
	{  9,14, L"MW_ENDER_CHEST_TOP1", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 10,14, L"MW_ENDER_CHEST_TOP2", SWATCH_REPEAT_ALL },	// was unused, ender chest moved to here
	{ 11,14, L"beacon", SWATCH_REPEAT_ALL|SBIT_DECAL },	// was unused, beacon was moved to here TODOTODO make better beacon
	{ 12,14, L"emerald", SWATCH_REPEAT_ALL },	// was unused, emerald was moved to here
	{ 13,14, L"coal_block", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still TODO duplicate
	{ 14,14, L"comparator_off", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still TODO duplicate
	{ 15,14, L"comparator_on", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still TODO duplicate
	{  0,15, L"MW_FLATTENED_TORCH", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened torch top
	{  1,15, L"MW_FLATTENED_REDSTONE_TORCH_ON", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened redstone torch top, on
	{  2,15, L"MW_FLATTENED_REDSTONE_TORCH_OFF", SWATCH_REPEAT_ALL },	// MANUFACTURED used for flattened redstone torch top, off
	{  3,15, L"MW_REDSTONE_WIRE_ANGLED", SWATCH_REPEAT_ALL },	// MANUFACTURED used for angled redstone wire
	{  4,15, L"MW_REDSTONE_WIRE_THREE_WAY", SWATCH_REPEAT_ALL },	// MANUFACTURED used for three-way redstone wire
	{  5,15, L"daylight_detector_side", SWATCH_REPEAT_ALL },	// destroy, etc. unused
	{  6,15, L"daylight_detector_top", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{  7,15, L"dropper_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN destroy, etc. unused
	{  8,15, L"dropper_front_vertical", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{  9,15, L"hay_block_side", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 10,15, L"hay_block_top", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 11,15, L"hopper_inside", SWATCH_REPEAT_ALL },	// ADD-IN destroy, etc. unused
	{ 12,15, L"hopper_outside", SWATCH_REPEAT_SIDES_ELSE_CLAMP },	// ADD-IN destroy, etc. unused
	{ 13,15, L"hopper_top", SWATCH_CLAMP_ALL },	// ADD-IN destroy, etc. unused
	{ 14,15, L"redstone_block", SWATCH_REPEAT_ALL },	// ADD-IN was lava_still TODO duplicate
	{ 15,15, L"lava_still", SWATCH_REPEAT_ALL },
	// brave new world
	{  0,16, L"hardened_clay_stained_white", SWATCH_REPEAT_ALL },
	{  1,16, L"hardened_clay_stained_orange", SWATCH_REPEAT_ALL },
	{  2,16, L"hardened_clay_stained_magenta", SWATCH_REPEAT_ALL },
	{  3,16, L"hardened_clay_stained_light_blue", SWATCH_REPEAT_ALL },
	{  4,16, L"hardened_clay_stained_yellow", SWATCH_REPEAT_ALL },
	{  5,16, L"hardened_clay_stained_lime", SWATCH_REPEAT_ALL },
	{  6,16, L"hardened_clay_stained_pink", SWATCH_REPEAT_ALL },
	{  7,16, L"hardened_clay_stained_gray", SWATCH_REPEAT_ALL },
	{  8,16, L"hardened_clay_stained_silver", SWATCH_REPEAT_ALL },
	{  9,16, L"hardened_clay_stained_cyan", SWATCH_REPEAT_ALL },
	{ 10,16, L"hardened_clay_stained_purple", SWATCH_REPEAT_ALL },
	{ 11,16, L"hardened_clay_stained_blue", SWATCH_REPEAT_ALL },
	{ 12,16, L"hardened_clay_stained_brown", SWATCH_REPEAT_ALL },
	{ 13,16, L"hardened_clay_stained_green", SWATCH_REPEAT_ALL },
	{ 14,16, L"hardened_clay_stained_red", SWATCH_REPEAT_ALL },
	{ 15,16, L"hardened_clay_stained_black", SWATCH_REPEAT_ALL },
	{  0,17, L"hardened_clay", SWATCH_REPEAT_ALL },
	{  1,17, L"quartz_block_bottom", SWATCH_REPEAT_ALL },
	{  2,17, L"quartz_block_chiseled_top", SWATCH_REPEAT_ALL },
	{  3,17, L"quartz_block_chiseled", SWATCH_REPEAT_ALL },
	{  4,17, L"quartz_block_lines_top", SWATCH_REPEAT_ALL },
	{  5,17, L"quartz_block_lines", SWATCH_REPEAT_ALL },
	{  6,17, L"quartz_block_side", SWATCH_REPEAT_ALL },
	{  7,17, L"quartz_block_top", SWATCH_REPEAT_ALL },
	{  8,17, L"quartz_ore", SWATCH_REPEAT_ALL },
	{  9,17, L"rail_activator_powered", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 10,17, L"rail_activator", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 11,17, L"rail_detector_powered", SWATCH_TILE_BOTTOM_AND_TOP|SBIT_DECAL },
	// 1.7
	{ 12,17, L"ice_packed", SWATCH_REPEAT_ALL },
	{ 13,17, L"red_sand", SWATCH_REPEAT_ALL },
	{ 14,17, L"dirt_podzol_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
	{ 15,17, L"dirt_podzol_top", SWATCH_REPEAT_ALL },
	{  0,18, L"double_plant_sunflower_back", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1,18, L"double_plant_sunflower_front", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  2,18, L"double_plant_sunflower_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  3,18, L"double_plant_sunflower_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,18, L"double_plant_syringa_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },	// lily
	{  5,18, L"double_plant_syringa_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  6,18, L"double_plant_grass_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  7,18, L"double_plant_grass_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8,18, L"double_plant_fern_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{  9,18, L"double_plant_fern_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 10,18, L"double_plant_rose_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },
	{ 11,18, L"double_plant_rose_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 12,18, L"double_plant_paeonia_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP|SBIT_DECAL },	// peony
	{ 13,18, L"double_plant_paeonia_top", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{ 14,18, L"", 0x0 },	// UNUSED
	{ 15,18, L"", 0x0 }, // UNUSED
	{  0,19, L"flower_blue_orchid", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  1,19, L"flower_allium", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  2,19, L"flower_houstonia", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// azure bluet
	{  3,19, L"flower_tulip_red", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  4,19, L"flower_tulip_orange", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  5,19, L"flower_tulip_white", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  6,19, L"flower_tulip_pink", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  7,19, L"flower_oxeye_daisy", SBIT_CLAMP_BOTTOM|SBIT_DECAL },
	{  8,19, L"flower_paeonia", SBIT_CLAMP_BOTTOM|SBIT_DECAL },	// not used currently, but save for now.
	{  9,19, L"", 0x0 },
	{ 10,19, L"", 0x0 },
	{ 11,19, L"", 0x0 },
	{ 12,19, L"", 0x0 },
	{ 13,19, L"", 0x0 },
	{ 14,19, L"", 0x0 },
	{ 15,19, L"", 0x0 },
};
/*
Removed: destroy_stage_0 through 9

Should be added to terrainExt.png, duplicates in basic game but make valid here, adding tiles, and use in code: 
potatoes_stage_0, 1, 2 - done
leaves_birch_opaque - done
leaves_birch - done

log_birch_top 11,11 - done
log_jungle_top 13,11 - done
log_spruce_top 12,11 - done
noteblock - 12, 8 - done
pumpkin_stem_connected 15,11 - done (well, really, TODOTODO for pumpkins and melons)
pumpkin_stem_disconnected 14,11 - done

dispenser_front_vertical - definitely put at 15,2;  - this is for when the dispenser/dropper is vertical up or down (used to only be horizontal) - done

NEW BLOCKS (1.6):
coal_block 13,14 - done
comparator_off 14,14 - done
comparator_on 15,14 - done

daylight_detector_side 5,15 - done
daylight_detector_top 6,15 - done
dropper_front_horizontal 7,15 - done
dropper_front_vertical 8,15 - done

hardened_clay_stained_black 15,16 - done
hardened_clay_stained_blue 11,16
hardened_clay_stained_brown 12,16
hardened_clay_stained_cyan 9,16
hardened_clay_stained_gray 7,16
hardened_clay_stained_green 13,16
hardened_clay_stained_light_blue 3,16
hardened_clay_stained_lime 5,16
hardened_clay_stained_magenta 2,16
hardened_clay_stained_orange 1,16
hardened_clay_stained_pink 6,16
hardened_clay_stained_purple 10,16
hardened_clay_stained_red 14,16
hardened_clay_stained_silver 8,16
hardened_clay_stained_white 0,16
hardened_clay_stained_yellow 4,16

hardened_clay 0,17 - done

hay_block_side 9,15 - done
hay_block_top 10,15 - done

hopper_inside 11,15 - done
hopper_outside 12,15
hopper_top 13,15

quartz_block_bottom 1,17 - done
quartz_block_chiseled_top 2,17
quartz_block_chiseled 3,17
quartz_block_lines_top 4,17
quartz_block_lines 5,17
quartz_block_side 6,17
quartz_block_top 7,17
quartz_ore 8,17 - done
rail_activator_powered 9,17 - done
rail_activator 10,17
rail_detector_powered 11,17

redstone_block 14,15 - done

carpet! - make it get the right color, not be like snow. - done

trapped chest fronts (lock) - done

FIX RAIL COMPOSITE BUG

1.7 stuff, need to add to tilemaker itself, and Mineways:

ice_packed - done

red_sand 13,17 - done

dirt_podzol_side 14,17 - done
dirt_podzol_top 15,17

TODOTODO - need to make a tile where it's merged to grass. Have to merge *each* tile, ugh, and pay attention to data values.
double_plant_fern_bottom
double_plant_fern_top
double_plant_grass_bottom
double_plant_grass_top
double_plant_paeonia_bottom
double_plant_paeonia_top
double_plant_rose_bottom
double_plant_rose_top
double_plant_sunflower_back
double_plant_sunflower_bottom
double_plant_sunflower_front
double_plant_sunflower_top
double_plant_syringa_bottom
double_plant_syringa_top

flower_allium
flower_blue_orchid
flower_houstonia
flower_oxeye_daisy
flower_paeonia

flower_tulip_orange
flower_tulip_pink
flower_tulip_red
flower_tulip_white

ADD PANES

Loose ends:

pumpkin and melon connectors

glass panes

Unused: lava_flow, water_flow - someday maybe add these? TODO

Issues to clean up: location of 7 chest tiles (and add an eighth for the top, and make a latch), 6 ender chest tiles
*/

#endif