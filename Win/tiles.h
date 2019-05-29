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

// if tile is a cutout, note this, as it should be "bled" outwards before output when rendering
#define SBIT_DECAL				0x40
// if tile is cutout geometry, note this so it's bled out for 3D printing and rendering
#define SBIT_CUTOUT_GEOMETRY	0x80
// if tile has full transparency for some other reason, e.g., it's an overlay, tag it here so that we know that's the case 
#define SBIT_ALPHA_OVERLAY		0x100

// special bit: if the tile is a leaf tile, Mineways itself can optionally make it solid
#define SBIT_LEAVES				0x200

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
// Repeat top and bottom is for cactus sides and rails
#define SWATCH_TILE_BOTTOM_AND_TOP          SBIT_REPEAT_TOP_BOTTOM
// Bottom and right is for the curved rail
#define SWATCH_CLAMP_BOTTOM_AND_RIGHT       (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT)
// Bottom and top clamp only (no repeat) for double-height (two block high) plants, kelp, tall sea grass
#define SWATCH_CLAMP_BOTTOM_AND_TOP         (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_TOP)
// Clamp bottom and sides for bed and enchanting table
#define SWATCH_CLAMP_ALL_BUT_TOP            (SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)
// Clamp all is normally used for "geometric" cutout tiles SBIT_CUTOUT_GEOMETRY where just a part of the tile is selected. For 3D printing
// and for interpolation, you want to have "invisible" texels off the edges to be clamp copied so that they are properly interpolated.
#define SWATCH_CLAMP_ALL                    (SBIT_CLAMP_TOP|SBIT_CLAMP_BOTTOM|SBIT_CLAMP_RIGHT|SBIT_CLAMP_LEFT)


// If this number changes, also change warning #7 in gPopupInfo (see TerrainExt.png in that message) in Mineways.cpp
#define VERTICAL_TILES 42
#define TOTAL_TILES (VERTICAL_TILES*16)
static struct {
    int txrX;   // column and row, from upper left, of 16x16+ tiles in terrain.png, for top view of block
    int txrY;
    const TCHAR *filename;
    const TCHAR *altFilename;   // new 1.13 name
    int flags;
} gTiles[] = {
    { 0, 0, L"grass_block_top", L"grass_top", SWATCH_REPEAT_ALL },	// tinted by grass.png
    { 1, 0, L"stone", L"", SWATCH_REPEAT_ALL },
    { 2, 0, L"dirt", L"", SWATCH_REPEAT_ALL },
    { 3, 0, L"grass_block_side", L"grass_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 4, 0, L"oak_planks", L"planks_oak", SWATCH_REPEAT_ALL },
    { 5, 0, L"stone_slab_side", L"", SWATCH_REPEAT_ALL },
    { 6, 0, L"stone_slab_top", L"", SWATCH_REPEAT_ALL },
    { 7, 0, L"bricks", L"brick", SWATCH_REPEAT_ALL },
    { 8, 0, L"tnt_side", L"", SWATCH_REPEAT_ALL },
    { 9, 0, L"tnt_top", L"", SWATCH_REPEAT_ALL },
    { 10, 0, L"tnt_bottom", L"", SWATCH_REPEAT_ALL },
    { 11, 0, L"cobweb", L"web", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 0, L"poppy", L"flower_rose", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 0, L"dandelion", L"flower_dandelion", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 0, L"nether_portal", L"portal", SWATCH_REPEAT_ALL },	// really, bluish originally, now it's better
    { 15, 0, L"oak_sapling", L"sapling_oak", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 1, L"cobblestone", L"", SWATCH_REPEAT_ALL },
    { 1, 1, L"bedrock", L"", SWATCH_REPEAT_ALL },
    { 2, 1, L"sand", L"", SWATCH_REPEAT_ALL },
    { 3, 1, L"gravel", L"", SWATCH_REPEAT_ALL },
    { 4, 1, L"oak_log", L"log_oak", SWATCH_REPEAT_ALL },
    { 5, 1, L"oak_log_top", L"log_oak_top", SWATCH_REPEAT_ALL },	// and every other log, we don't separate these out
    { 6, 1, L"iron_block", L"", SWATCH_REPEAT_ALL },
    { 7, 1, L"gold_block", L"", SWATCH_REPEAT_ALL },
    { 8, 1, L"diamond_block", L"", SWATCH_REPEAT_ALL },
    { 9, 1, L"MW_CHEST_TOP", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest. Find chests in \assets\minecraft\textures\entity\chest and include in blocks\chest subdirectory
    { 10, 1, L"MW_CHEST_SIDE", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11, 1, L"MW_CHEST_FRONT", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 12, 1, L"red_mushroom", L"mushroom_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 1, L"brown_mushroom", L"mushroom_brown", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 1, L"jungle_sapling", L"sapling_jungle", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 1, L"fire_0", L"fire_layer_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// input is fire animation strip
    { 0, 2, L"gold_ore", L"", SWATCH_REPEAT_ALL },
    { 1, 2, L"iron_ore", L"", SWATCH_REPEAT_ALL },
    { 2, 2, L"coal_ore", L"", SWATCH_REPEAT_ALL },
    { 3, 2, L"bookshelf", L"", SWATCH_REPEAT_ALL }, // side - top and bottom are oak planks
    { 4, 2, L"mossy_cobblestone", L"cobblestone_mossy", SWATCH_REPEAT_ALL },
    { 5, 2, L"obsidian", L"", SWATCH_REPEAT_ALL },
    { 6, 2, L"grass_block_side_overlay", L"grass_side_overlay", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_ALPHA_OVERLAY }, // was "grass_side_overlay" - we use it for temporary work - grass_side_overlay tinted by grass.png, but we don't use it.
    { 7, 2, L"grass", L"tallgrass", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 8, 2, L"MW_WORKSPACE1", L"", SWATCH_REPEAT_ALL },	// we use it for temporary work - grass?? top grayscale, but we don't use it, nor does Mojang
    { 9, 2, L"MW_DCHEST_FRONT_LEFT", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY }, // was beacon - taken by chest
    { 10, 2, L"MW_DCHEST_FRONT_RIGHT", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11, 2, L"crafting_table_top", L"", SWATCH_REPEAT_ALL },
    { 12, 2, L"furnace_front", L"furnace_front_off", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 13, 2, L"furnace_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 14, 2, L"dispenser_front", L"dispenser_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 15, 2, L"dispenser_front_vertical", L"", SWATCH_REPEAT_ALL }, // ADD-IN; instead, input could be second fire animation strip "fire_layer_1" - TODO use both fire tiles?
    { 0, 3, L"sponge", L"", SWATCH_REPEAT_ALL },
    { 1, 3, L"glass", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 2, 3, L"diamond_ore", L"", SWATCH_REPEAT_ALL },
    { 3, 3, L"redstone_ore", L"", SWATCH_REPEAT_ALL },
    { 4, 3, L"oak_leaves", L"leaves_oak", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    { 5, 3, L"coarse_dirt", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8 - replaced leaves_oak_opaque
    { 6, 3, L"stone_bricks", L"stonebrick", SWATCH_REPEAT_ALL },
    { 7, 3, L"dead_bush", L"deadbush", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 8, 3, L"fern", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 9, 3, L"MW_DCHEST_BACK_LEFT", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 10, 3, L"MW_DCHEST_BACK_RIGHT", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// taken by chest
    { 11, 3, L"crafting_table_side", L"", SWATCH_REPEAT_ALL },
    { 12, 3, L"crafting_table_front", L"", SWATCH_REPEAT_ALL },
    { 13, 3, L"furnace_front_on", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 14, 3, L"furnace_top", L"", SWATCH_REPEAT_ALL },
    { 15, 3, L"spruce_sapling", L"sapling_spruce", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 4, L"white_wool", L"wool_colored_white", SWATCH_REPEAT_ALL },
    { 1, 4, L"spawner", L"mob_spawner", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 2, 4, L"snow", L"", SWATCH_REPEAT_ALL },
    { 3, 4, L"ice", L"", SWATCH_REPEAT_ALL },
    { 4, 4, L"grass_block_snow", L"grass_side_snowed", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 5, 4, L"cactus_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 6, 4, L"cactus_side", L"", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL | SBIT_CUTOUT_GEOMETRY },	// weird one: cutout, but also for 3D printing it's geometry
    { 7, 4, L"cactus_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 8, 4, L"clay", L"", SWATCH_REPEAT_ALL },
    { 9, 4, L"sugar_cane", L"reeds", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 10, 4, L"jukebox_side", L"", SWATCH_REPEAT_ALL },	// was noteblock, which is now below
    { 11, 4, L"jukebox_top", L"", SWATCH_REPEAT_ALL },
    { 12, 4, L"lily_pad", L"waterlily", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 4, L"mycelium_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 14, 4, L"mycelium_top", L"", SWATCH_REPEAT_ALL },
    { 15, 4, L"birch_sapling", L"sapling_birch", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 5, L"torch", L"torch_on", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 1, 5, L"oak_door_top", L"door_wood_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 2, 5, L"iron_door_top", L"door_iron_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 3, 5, L"ladder", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 4, 5, L"oak_trapdoor", L"trapdoor", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 5, 5, L"iron_bars", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 6, 5, L"farmland_moist", L"farmland_wet", SWATCH_REPEAT_ALL },
    { 7, 5, L"farmland", L"farmland_dry", SWATCH_REPEAT_ALL },
    { 8, 5, L"wheat_stage0", L"wheat_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 9, 5, L"wheat_stage1", L"wheat_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 5, L"wheat_stage2", L"wheat_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11, 5, L"wheat_stage3", L"wheat_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 5, L"wheat_stage4", L"wheat_stage_4", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 5, L"wheat_stage5", L"wheat_stage_5", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 5, L"wheat_stage6", L"wheat_stage_6", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 5, L"wheat_stage7", L"wheat_stage_7", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 6, L"lever", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 1, 6, L"oak_door_bottom", L"door_wood_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 2, 6, L"iron_door_bottom", L"door_iron_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 3, 6, L"redstone_torch", L"redstone_torch_on", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 4, 6, L"mossy_stone_bricks", L"stonebrick_mossy", SWATCH_REPEAT_ALL },
    { 5, 6, L"cracked_stone_bricks", L"stonebrick_cracked", SWATCH_REPEAT_ALL },
    { 6, 6, L"pumpkin_top", L"", SWATCH_REPEAT_ALL },
    { 7, 6, L"netherrack", L"", SWATCH_REPEAT_ALL },
    { 8, 6, L"soul_sand", L"", SWATCH_REPEAT_ALL },
    { 9, 6, L"glowstone", L"", SWATCH_REPEAT_ALL },
    { 10, 6, L"piston_top_sticky", L"", SWATCH_REPEAT_ALL },
    { 11, 6, L"piston_top", L"piston_top_normal", SWATCH_REPEAT_ALL },
    { 12, 6, L"piston_side", L"", SWATCH_REPEAT_ALL },
    { 13, 6, L"piston_bottom", L"", SWATCH_REPEAT_ALL },
    { 14, 6, L"piston_inner", L"", SWATCH_REPEAT_ALL },
    { 15, 6, L"melon_stem", L"melon_stem_disconnected", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 7, L"rail_corner", L"rail_normal_turned", SWATCH_CLAMP_BOTTOM_AND_RIGHT | SBIT_DECAL },
    { 1, 7, L"black_wool", L"wool_colored_black", SWATCH_REPEAT_ALL },
    { 2, 7, L"gray_wool", L"wool_colored_gray", SWATCH_REPEAT_ALL },
    { 3, 7, L"redstone_torch_off", L"redstone_torch_off", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 4, 7, L"spruce_log", L"log_spruce", SWATCH_REPEAT_ALL },
    { 5, 7, L"birch_log", L"log_birch", SWATCH_REPEAT_ALL },
    { 6, 7, L"pumpkin_side", L"", SWATCH_REPEAT_ALL },
    { 7, 7, L"carved_pumpkin", L"pumpkin_face_off", SWATCH_REPEAT_ALL },
    { 8, 7, L"jack_o_lantern", L"pumpkin_face_on", SWATCH_REPEAT_ALL },
    { 9, 7, L"cake_top", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 10, 7, L"cake_side", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 11, 7, L"cake_inner", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 12, 7, L"cake_bottom", L"", SBIT_CLAMP_BOTTOM | SBIT_CUTOUT_GEOMETRY },
    { 13, 7, L"red_mushroom_block", L"mushroom_block_skin_red", SWATCH_REPEAT_ALL },
    { 14, 7, L"brown_mushroom_block", L"mushroom_block_skin_brown", SWATCH_REPEAT_ALL },
    { 15, 7, L"attached_melon_stem", L"melon_stem_connected", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 8, L"rail", L"rail_normal", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 1, 8, L"red_wool", L"wool_colored_red", SWATCH_REPEAT_ALL },
    { 2, 8, L"pink_wool", L"wool_colored_pink", SWATCH_REPEAT_ALL },
    { 3, 8, L"repeater", L"repeater_off", SWATCH_REPEAT_ALL },
    { 4, 8, L"spruce_leaves", L"leaves_spruce", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    { 5, 8, L"red_sandstone_bottom", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 6, 8, L"MW_bed_feet_top", L"bed_feet_top", SWATCH_CLAMP_ALL },
    { 7, 8, L"MW_bed_head_top", L"bed_head_top", SWATCH_CLAMP_ALL },
    { 8, 8, L"melon_side", L"", SWATCH_REPEAT_ALL },
    { 9, 8, L"melon_top", L"", SWATCH_REPEAT_ALL },
    { 10, 8, L"cauldron_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 8, L"cauldron_inner", L"", SWATCH_REPEAT_ALL },
    { 12, 8, L"note_block", L"noteblock", SWATCH_REPEAT_ALL },
    { 13, 8, L"mushroom_stem", L"mushroom_block_skin_stem", SWATCH_REPEAT_ALL },
    { 14, 8, L"mushroom_block_inside", L"", SWATCH_REPEAT_ALL },
    { 15, 8, L"vine", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// grayscale
    { 0, 9, L"lapis_block", L"", SWATCH_REPEAT_ALL },
    { 1, 9, L"green_wool", L"wool_colored_green", SWATCH_REPEAT_ALL },
    { 2, 9, L"lime_wool", L"wool_colored_lime", SWATCH_REPEAT_ALL },
    { 3, 9, L"repeater_on", L"", SWATCH_REPEAT_ALL },
    { 4, 9, L"glass_pane_top", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 5, 9, L"MW_bed_feet_end", L"bed_feet_end", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    { 6, 9, L"MW_bed_feet_side", L"bed_feet_side", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    { 7, 9, L"MW_bed_head_side", L"bed_head_side", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    { 8, 9, L"MW_bed_head_end", L"bed_head_end", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    { 9, 9, L"jungle_log", L"log_jungle", SWATCH_REPEAT_ALL },
    { 10, 9, L"cauldron_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_CUTOUT_GEOMETRY },
    { 11, 9, L"cauldron_bottom", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 9, L"brewing_stand_base", L"", SWATCH_REPEAT_ALL },
    { 13, 9, L"brewing_stand", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 9, L"end_portal_frame_top", L"endframe_top", SWATCH_REPEAT_ALL },
    { 15, 9, L"end_portal_frame_side", L"endframe_side", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 0, 10, L"lapis_ore", L"", SWATCH_REPEAT_ALL },
    { 1, 10, L"brown_wool", L"wool_colored_brown", SWATCH_REPEAT_ALL },
    { 2, 10, L"yellow_wool", L"wool_colored_yellow", SWATCH_REPEAT_ALL },
    { 3, 10, L"powered_rail", L"rail_golden", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 4, 10, L"redstone_dust_line0", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// vertical (runs north-south)
    { 5, 10, L"redstone_dust_line1", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// horizontal, rotated
    { 6, 10, L"enchanting_table_top", L"", SWATCH_REPEAT_ALL },
    { 7, 10, L"dragon_egg", L"", SWATCH_REPEAT_ALL },
    { 8, 10, L"cocoa_stage2", L"cocoa_stage_2", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 9, 10, L"cocoa_stage1", L"cocoa_stage_1", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 10, L"cocoa_stage0", L"cocoa_stage_0", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 10, L"emerald_ore", L"", SWATCH_REPEAT_ALL },
    { 12, 10, L"tripwire_hook", L"trip_wire_source", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 10, L"tripwire", L"trip_wire", SWATCH_CLAMP_ALL | SBIT_DECAL },
    { 14, 10, L"end_portal_frame_eye", L"endframe_eye", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 10, L"end_stone", L"", SWATCH_REPEAT_ALL },
    { 0, 11, L"sandstone_top", L"", SWATCH_REPEAT_ALL },
    { 1, 11, L"blue_wool", L"wool_colored_blue", SWATCH_REPEAT_ALL },
    { 2, 11, L"light_blue_wool", L"wool_colored_light_blue", SWATCH_REPEAT_ALL },
    { 3, 11, L"powered_rail_on", L"rail_golden_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 4, 11, L"redstone_dust_dot", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 5, 11, L"acacia_log", L"log_acacia", 0x0 },	// ADD-IN 1.7.2
    { 6, 11, L"enchanting_table_side", L"", SWATCH_CLAMP_ALL_BUT_TOP | SBIT_CUTOUT_GEOMETRY },
    { 7, 11, L"enchanting_table_bottom", L"", SWATCH_REPEAT_ALL },
    { 8, 11, L"MW_END_PORTAL", L"", SWATCH_REPEAT_ALL },    // custom - the 3D effect seen through the end portal
    { 9, 11, L"item_frame", L"itemframe_background", SWATCH_REPEAT_ALL },	// frame around item, unimplemented, TODO
    { 10, 11, L"flower_pot", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 11, L"birch_log_top", L"log_birch_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 12, 11, L"spruce_log_top", L"log_spruce_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 13, 11, L"jungle_log_top", L"log_jungle_top", SWATCH_REPEAT_ALL },	// ADD-IN
    { 14, 11, L"pumpkin_stem", L"pumpkin_stem_disconnected", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// ADD-IN
    { 15, 11, L"attached_pumpkin_stem", L"pumpkin_stem_connected", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// ADD-IN
    { 0, 12, L"sandstone", L"sandstone_normal", SWATCH_REPEAT_ALL },
    { 1, 12, L"purple_wool", L"wool_colored_purple", SWATCH_REPEAT_ALL },
    { 2, 12, L"magenta_wool", L"wool_colored_magenta", SWATCH_REPEAT_ALL },
    { 3, 12, L"detector_rail", L"rail_detector", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 4, 12, L"jungle_leaves", L"leaves_jungle", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },
    { 5, 12, L"chiseled_red_sandstone", L"red_sandstone_carved", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 6, 12, L"spruce_planks", L"planks_spruce", SWATCH_REPEAT_ALL },
    { 7, 12, L"jungle_planks", L"planks_jungle", SWATCH_REPEAT_ALL },
    { 8, 12, L"carrots_stage0", L"carrots_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_0 in basic game, but can be different in texture packs
    { 9, 12, L"carrots_stage1", L"carrots_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_1
    { 10, 12, L"carrots_stage2", L"carrots_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// also potatoes_stage_2
    { 11, 12, L"carrots_stage3", L"carrots_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 12, L"potatoes_stage0", L"potatoes_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 13, 12, L"potatoes_stage1", L"potatoes_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 12, L"potatoes_stage2", L"potatoes_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 12, L"potatoes_stage3", L"potatoes_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 0, 13, L"sandstone_bottom", L"", SWATCH_REPEAT_ALL },
    { 1, 13, L"cyan_wool", L"wool_colored_cyan", SWATCH_REPEAT_ALL },
    { 2, 13, L"orange_wool", L"wool_colored_orange", SWATCH_REPEAT_ALL },
    { 3, 13, L"redstone_lamp", L"redstone_lamp_off", SWATCH_REPEAT_ALL },
    { 4, 13, L"redstone_lamp_on", L"", SWATCH_REPEAT_ALL },
    { 5, 13, L"chiseled_stone_bricks", L"stonebrick_carved", SWATCH_REPEAT_ALL },
    { 6, 13, L"birch_planks", L"planks_birch", SWATCH_REPEAT_ALL },
    { 7, 13, L"anvil", L"anvil_base", SWATCH_REPEAT_ALL },
    { 8, 13, L"chipped_anvil_top", L"anvil_top_damaged_1", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 9, 13, L"MW_ENDER_CHEST_LATCH", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 13, L"MW_ENDER_CHEST_TOP", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 13, L"MW_ENDER_CHEST_SIDE", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 13, L"MW_ENDER_CHEST_FRONT", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 13, L"birch_leaves", L"leaves_birch", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },	// ADD-IN
    { 14, 13, L"red_sandstone", L"red_sandstone_normal", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 15, 13, L"water_still", L"", SWATCH_REPEAT_ALL },
    { 0, 14, L"nether_bricks", L"nether_brick", SWATCH_REPEAT_ALL },
    { 1, 14, L"light_gray_wool", L"wool_colored_silver", SWATCH_REPEAT_ALL },
    { 2, 14, L"nether_wart_stage0", L"nether_wart_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 3, 14, L"nether_wart_stage1", L"nether_wart_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 4, 14, L"nether_wart_stage2", L"nether_wart_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 5, 14, L"chiseled_sandstone", L"sandstone_carved", SWATCH_REPEAT_ALL },
    { 6, 14, L"cut_sandstone", L"sandstone_smooth", SWATCH_REPEAT_ALL },
    { 7, 14, L"anvil_top", L"anvil_top_damaged_0", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 8, 14, L"damaged_anvil_top", L"anvil_top_damaged_2", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 9, 14, L"MW_DCHEST_TOP_LEFT", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// was unused, ender chest moved to here
    { 10, 14, L"MW_DCHEST_TOP_RIGHT", L"", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },	// was unused, ender chest moved to here
    { 11, 14, L"beacon", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// was unused, beacon was moved to here
    { 12, 14, L"emerald_block", L"", SWATCH_REPEAT_ALL },	// was unused, emerald was moved to here
    { 13, 14, L"coal_block", L"", SWATCH_REPEAT_ALL },
    { 14, 14, L"comparator", L"comparator_off", SWATCH_REPEAT_ALL },
    { 15, 14, L"comparator_on", L"", SWATCH_REPEAT_ALL },
    { 0, 15, L"", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened torch top; not used in rendering, but 3D printing uses for composites for torches from above
    { 1, 15, L"", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened redstone torch top, on; not used in rendering, but 3D printing uses for composites for torches from above
    { 2, 15, L"", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for flattened redstone torch top, off; not used in rendering, but 3D printing uses for composites for torches from above
    { 3, 15, L"", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for angled redstone wire
    { 4, 15, L"", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },	// MANUFACTURED used for three-way redstone wire
    { 5, 15, L"daylight_detector_side", L"", SWATCH_REPEAT_ALL },	// destroy, etc. unused
    { 6, 15, L"daylight_detector_top", L"", SWATCH_REPEAT_ALL },
    { 7, 15, L"dropper_front", L"dropper_front_horizontal", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 8, 15, L"dropper_front_vertical", L"", SWATCH_REPEAT_ALL },
    { 9, 15, L"hay_block_side", L"", SWATCH_REPEAT_ALL },
    { 10, 15, L"hay_block_top", L"", SWATCH_REPEAT_ALL },
    { 11, 15, L"hopper_inside", L"", SWATCH_REPEAT_ALL },
    { 12, 15, L"hopper_outside", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 13, 15, L"hopper_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 15, L"redstone_block", L"", SWATCH_REPEAT_ALL },
    { 15, 15, L"lava_still", L"", SWATCH_REPEAT_ALL },
    // brave new world, off the 256x256 edge
    { 0, 16, L"white_terracotta", L"hardened_clay_stained_white", SWATCH_REPEAT_ALL },
    { 1, 16, L"orange_terracotta", L"hardened_clay_stained_orange", SWATCH_REPEAT_ALL },
    { 2, 16, L"magenta_terracotta", L"hardened_clay_stained_magenta", SWATCH_REPEAT_ALL },
    { 3, 16, L"light_blue_terracotta", L"hardened_clay_stained_light_blue", SWATCH_REPEAT_ALL },
    { 4, 16, L"yellow_terracotta", L"hardened_clay_stained_yellow", SWATCH_REPEAT_ALL },
    { 5, 16, L"lime_terracotta", L"hardened_clay_stained_lime", SWATCH_REPEAT_ALL },
    { 6, 16, L"pink_terracotta", L"hardened_clay_stained_pink", SWATCH_REPEAT_ALL },
    { 7, 16, L"gray_terracotta", L"hardened_clay_stained_gray", SWATCH_REPEAT_ALL },
    { 8, 16, L"light_gray_terracotta", L"hardened_clay_stained_silver", SWATCH_REPEAT_ALL },
    { 9, 16, L"cyan_terracotta", L"hardened_clay_stained_cyan", SWATCH_REPEAT_ALL },
    { 10, 16, L"purple_terracotta", L"hardened_clay_stained_purple", SWATCH_REPEAT_ALL },
    { 11, 16, L"blue_terracotta", L"hardened_clay_stained_blue", SWATCH_REPEAT_ALL },
    { 12, 16, L"brown_terracotta", L"hardened_clay_stained_brown", SWATCH_REPEAT_ALL },
    { 13, 16, L"green_terracotta", L"hardened_clay_stained_green", SWATCH_REPEAT_ALL },
    { 14, 16, L"red_terracotta", L"hardened_clay_stained_red", SWATCH_REPEAT_ALL },
    { 15, 16, L"black_terracotta", L"hardened_clay_stained_black", SWATCH_REPEAT_ALL },
    { 0, 17, L"terracotta", L"hardened_clay", SWATCH_REPEAT_ALL },
    { 1, 17, L"quartz_block_bottom", L"", SWATCH_REPEAT_ALL },
    { 2, 17, L"chiseled_quartz_block_top", L"quartz_block_chiseled_top", SWATCH_REPEAT_ALL },
    { 3, 17, L"chiseled_quartz_block", L"quartz_block_chiseled", SWATCH_REPEAT_ALL },
    { 4, 17, L"quartz_pillar_top", L"quartz_block_lines_top", SWATCH_REPEAT_ALL },
    { 5, 17, L"quartz_pillar", L"quartz_block_lines", SWATCH_REPEAT_ALL },
    { 6, 17, L"quartz_block_side", L"", SWATCH_REPEAT_ALL },
    { 7, 17, L"quartz_block_top", L"", SWATCH_REPEAT_ALL },
    { 8, 17, L"nether_quartz_ore", L"quartz_ore", SWATCH_REPEAT_ALL },
    { 9, 17, L"activator_rail", L"rail_activator_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 10, 17, L"activator_rail_on", L"rail_activator", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    { 11, 17, L"detector_rail_on", L"rail_detector_powered", SWATCH_TILE_BOTTOM_AND_TOP | SBIT_DECAL },
    // 1.7
    { 12, 17, L"packed_ice", L"ice_packed", SWATCH_REPEAT_ALL },
    { 13, 17, L"red_sand", L"", SWATCH_REPEAT_ALL },
    { 14, 17, L"podzol_side", L"dirt_podzol_side", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 15, 17, L"podzol_top", L"dirt_podzol_top", SWATCH_REPEAT_ALL },
    { 0, 18, L"sunflower_back", L"double_plant_sunflower_back", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 1, 18, L"sunflower_front", L"double_plant_sunflower_front", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 2, 18, L"sunflower_bottom", L"double_plant_sunflower_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 3, 18, L"sunflower_top", L"double_plant_sunflower_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 4, 18, L"lilac_bottom", L"double_plant_syringa_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },	// lily
    { 5, 18, L"lilac_top", L"double_plant_syringa_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 6, 18, L"tall_grass_bottom", L"double_plant_grass_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 7, 18, L"tall_grass_top", L"double_plant_grass_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 8, 18, L"large_fern_bottom", L"double_plant_fern_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 9, 18, L"large_fern_top", L"double_plant_fern_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 10, 18, L"rose_bush_bottom", L"double_plant_rose_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 11, 18, L"rose_bush_top", L"double_plant_rose_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 12, 18, L"peony_bottom", L"double_plant_paeonia_bottom", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },	// peony
    { 13, 18, L"peony_top", L"double_plant_paeonia_top", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 14, 18, L"acacia_sapling", L"sapling_acacia", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 15, 18, L"dark_oak_sapling", L"sapling_roofed_oak", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// yes, "roofed"
    { 0, 19, L"blue_orchid", L"flower_blue_orchid", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 1, 19, L"allium", L"flower_allium", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 2, 19, L"azure_bluet", L"flower_houstonia", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// azure bluet
    { 3, 19, L"red_tulip", L"flower_tulip_red", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 4, 19, L"orange_tulip", L"flower_tulip_orange", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 5, 19, L"white_tulip", L"flower_tulip_white", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 6, 19, L"pink_tulip", L"flower_tulip_pink", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 7, 19, L"oxeye_daisy", L"flower_oxeye_daisy", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 8, 19, L"seagrass", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// 1.13 - was flower_paeonia - no longer used TODO
    { 9, 19, L"acacia_leaves", L"leaves_acacia", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },	// ADD-IN 1.7.2
    { 10, 19, L"cut_red_sandstone", L"red_sandstone_smooth", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 11, 19, L"dark_oak_leaves", L"leaves_big_oak", SWATCH_REPEAT_ALL | SBIT_DECAL | SBIT_LEAVES },	// ADD-IN 1.7.2
    { 12, 19, L"red_sandstone_top", L"", SWATCH_REPEAT_ALL },	// ADD-IN 1.8
    { 13, 19, L"acacia_log_top", L"log_acacia_top", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 14, 19, L"dark_oak_log", L"log_big_oak", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 15, 19, L"dark_oak_log_top", L"log_big_oak_top", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 0, 20, L"white_stained_glass", L"glass_white", SWATCH_REPEAT_ALL },
    { 1, 20, L"orange_stained_glass", L"glass_orange", SWATCH_REPEAT_ALL },
    { 2, 20, L"magenta_stained_glass", L"glass_magenta", SWATCH_REPEAT_ALL },
    { 3, 20, L"light_blue_stained_glass", L"glass_light_blue", SWATCH_REPEAT_ALL },
    { 4, 20, L"yellow_stained_glass", L"glass_yellow", SWATCH_REPEAT_ALL },
    { 5, 20, L"lime_stained_glass", L"glass_lime", SWATCH_REPEAT_ALL },
    { 6, 20, L"pink_stained_glass", L"glass_pink", SWATCH_REPEAT_ALL },
    { 7, 20, L"gray_stained_glass", L"glass_gray", SWATCH_REPEAT_ALL },
    { 8, 20, L"light_gray_stained_glass", L"glass_silver", SWATCH_REPEAT_ALL },
    { 9, 20, L"cyan_stained_glass", L"glass_cyan", SWATCH_REPEAT_ALL },
    { 10, 20, L"purple_stained_glass", L"glass_purple", SWATCH_REPEAT_ALL },
    { 11, 20, L"blue_stained_glass", L"glass_blue", SWATCH_REPEAT_ALL },
    { 12, 20, L"brown_stained_glass", L"glass_brown", SWATCH_REPEAT_ALL },
    { 13, 20, L"green_stained_glass", L"glass_green", SWATCH_REPEAT_ALL },
    { 14, 20, L"red_stained_glass", L"glass_red", SWATCH_REPEAT_ALL },
    { 15, 20, L"black_stained_glass", L"glass_black", SWATCH_REPEAT_ALL },
    { 0, 21, L"white_stained_glass_pane_top", L"glass_pane_top_white", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 1, 21, L"orange_stained_glass_pane_top", L"glass_pane_top_orange", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 2, 21, L"magenta_stained_glass_pane_top", L"glass_pane_top_magenta", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 3, 21, L"light_blue_stained_glass_pane_top", L"glass_pane_top_light_blue", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 4, 21, L"yellow_stained_glass_pane_top", L"glass_pane_top_yellow", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 5, 21, L"lime_stained_glass_pane_top", L"glass_pane_top_lime", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 6, 21, L"pink_stained_glass_pane_top", L"glass_pane_top_pink", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 7, 21, L"gray_stained_glass_pane_top", L"glass_pane_top_gray", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 8, 21, L"light_gray_stained_glass_pane_top", L"glass_pane_top_silver", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 9, 21, L"cyan_stained_glass_pane_top", L"glass_pane_top_cyan", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 10, 21, L"purple_stained_glass_pane_top", L"glass_pane_top_purple", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 11, 21, L"blue_stained_glass_pane_top", L"glass_pane_top_blue", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 12, 21, L"brown_stained_glass_pane_top", L"glass_pane_top_brown", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 21, L"green_stained_glass_pane_top", L"glass_pane_top_green", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 14, 21, L"red_stained_glass_pane_top", L"glass_pane_top_red", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 15, 21, L"black_stained_glass_pane_top", L"glass_pane_top_black", SWATCH_REPEAT_ALL | SBIT_CUTOUT_GEOMETRY },
    { 0, 22, L"acacia_planks", L"planks_acacia", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    { 1, 22, L"dark_oak_planks", L"planks_big_oak", SWATCH_REPEAT_ALL },	// ADD-IN 1.7.2
    // 1.8
    { 2, 22, L"iron_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
    { 3, 22, L"slime_block", L"slime", SWATCH_REPEAT_ALL },
    { 4, 22, L"andesite", L"stone_andesite", SWATCH_REPEAT_ALL },
    { 5, 22, L"polished_andesite", L"stone_andesite_smooth", SWATCH_REPEAT_ALL },
    { 6, 22, L"diorite", L"stone_diorite", SWATCH_REPEAT_ALL },
    { 7, 22, L"polished_diorite", L"stone_diorite_smooth", SWATCH_REPEAT_ALL },
    { 8, 22, L"granite", L"stone_granite", SWATCH_REPEAT_ALL },
    { 9, 22, L"polished_granite", L"stone_granite_smooth", SWATCH_REPEAT_ALL },
    { 10, 22, L"prismarine_bricks", L"", SWATCH_REPEAT_ALL },
    { 11, 22, L"dark_prismarine", L"prismarine_dark", SWATCH_REPEAT_ALL },
    { 12, 22, L"prismarine", L"prismarine_rough", SWATCH_REPEAT_ALL },
    { 13, 22, L"daylight_detector_inverted_top", L"", SWATCH_REPEAT_ALL },
    { 14, 22, L"sea_lantern", L"", SWATCH_REPEAT_ALL },
    { 15, 22, L"wet_sponge", L"sponge_wet", SWATCH_REPEAT_ALL },
    { 0, 23, L"spruce_door_bottom", L"door_spruce_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 1, 23, L"spruce_door_top", L"door_spruce_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 2, 23, L"birch_door_bottom",  L"door_birch_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 3, 23, L"birch_door_top", L"door_birch_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 4, 23, L"jungle_door_bottom", L"door_jungle_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 5, 23, L"jungle_door_top", L"door_jungle_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 6, 23, L"acacia_door_bottom", L"door_acacia_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 7, 23, L"acacia_door_top", L"door_acacia_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_DECAL },
    { 8, 23, L"dark_oak_door_bottom", L"door_dark_oak_lower", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 9, 23, L"dark_oak_door_top", L"door_dark_oak_upper", SWATCH_REPEAT_SIDES_ELSE_CLAMP },
    { 10, 23, L"smooth_stone", L"", SWATCH_REPEAT_ALL },	// now reused for 1.14 - was top of banner; NOTE: this looks a heckuva lot like "stone_slab_top" - which gets used? TODOTODO
    { 11, 23, L"smooth_stone_slab_side", L"", SWATCH_REPEAT_ALL },	// now reused for 1.14 - was bottom of banner; NOTE: this looks a heckuva lot like "stone_slab_side" - which gets used? TODOTODO
    { 12, 23, L"end_rod", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 23, L"chorus_plant", L"", SWATCH_REPEAT_ALL },
    { 14, 23, L"chorus_flower", L"", SWATCH_REPEAT_ALL },
    { 15, 23, L"chorus_flower_dead", L"", SWATCH_REPEAT_ALL },
    { 0, 24, L"purpur_block", L"", SWATCH_REPEAT_ALL },
    { 1, 24, L"purpur_pillar", L"", SWATCH_REPEAT_ALL },
    { 2, 24, L"purpur_pillar_top", L"", SWATCH_REPEAT_ALL },
    { 3, 24, L"end_stone_bricks", L"end_bricks", SWATCH_REPEAT_ALL },
    { 4, 24, L"beetroots_stage0", L"beetroots_stage_0", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 5, 24, L"beetroots_stage1", L"beetroots_stage_1", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 6, 24, L"beetroots_stage2", L"beetroots_stage_2", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 7, 24, L"beetroots_stage3", L"beetroots_stage_3", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 8, 24, L"grass_path_top", L"", SWATCH_REPEAT_ALL },
    { 9, 24, L"grass_path_side", L"", SWATCH_REPEAT_SIDES_ELSE_CLAMP | SBIT_ALPHA_OVERLAY },
    { 10, 24, L"command_block_front", L"", SWATCH_REPEAT_ALL },
    { 11, 24, L"command_block_back", L"", SWATCH_REPEAT_ALL },  // also "commandBlock", but no room...
    { 12, 24, L"command_block_side", L"", SWATCH_REPEAT_ALL },
    { 13, 24, L"command_block_conditional", L"", SWATCH_REPEAT_ALL },
    { 14, 24, L"repeating_command_block_front", L"", SWATCH_REPEAT_ALL },
    { 15, 24, L"repeating_command_block_back", L"", SWATCH_REPEAT_ALL },
    { 0, 25, L"repeating_command_block_side", L"", SWATCH_REPEAT_ALL },
    { 1, 25, L"repeating_command_block_conditional", L"", SWATCH_REPEAT_ALL },
    { 2, 25, L"chain_command_block_front", L"", SWATCH_REPEAT_ALL },
    { 3, 25, L"chain_command_block_back", L"", SWATCH_REPEAT_ALL },
    { 4, 25, L"chain_command_block_side", L"", SWATCH_REPEAT_ALL },
    { 5, 25, L"chain_command_block_conditional", L"", SWATCH_REPEAT_ALL },
    { 6, 25, L"frosted_ice_0", L"", SWATCH_REPEAT_ALL },
    { 7, 25, L"frosted_ice_1", L"", SWATCH_REPEAT_ALL },
    { 8, 25, L"frosted_ice_2", L"", SWATCH_REPEAT_ALL },
    { 9, 25, L"frosted_ice_3", L"", SWATCH_REPEAT_ALL },
    { 10, 25, L"structure_block_corner", L"", SWATCH_REPEAT_ALL },
    { 11, 25, L"structure_block_data", L"", SWATCH_REPEAT_ALL },
    { 12, 25, L"structure_block_load", L"", SWATCH_REPEAT_ALL },
    { 13, 25, L"structure_block_save", L"", SWATCH_REPEAT_ALL },
    { 14, 25, L"barrier", L"MW_barrier", SWATCH_CLAMP_ALL | SBIT_DECAL },
    { 15, 25, L"water_overlay", L"", SWATCH_REPEAT_ALL },    // 1.9 - water looks like this through glass.
    { 0, 26, L"magma", L"", SWATCH_REPEAT_ALL },
    { 1, 26, L"nether_wart_block", L"", SWATCH_REPEAT_ALL },
    { 2, 26, L"red_nether_bricks", L"red_nether_brick", SWATCH_REPEAT_ALL },
    { 3, 26, L"bone_block_side", L"", SWATCH_REPEAT_ALL },
    { 4, 26, L"bone_block_top", L"", SWATCH_REPEAT_ALL },
    { 5, 26, L"redstone_dust_overlay", L"", SWATCH_REPEAT_ALL | SBIT_ALPHA_OVERLAY },	// could use alternate name such as redstone_dust_cross_overlay if old texture pack, but Modern HD does weird stuff with it
    { 6, 26, L"", L"", SWATCH_REPEAT_ALL },	// MANUFACTURED 4 way redstone wire - reserved
    { 7, 26, L"MW_CHEST_LATCH", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 8, 26, L"water_flow", L"", SWATCH_REPEAT_ALL },	// special: double-wide
    { 9, 26, L"lava_flow", L"", SWATCH_REPEAT_ALL },		// special: double-wide
    { 10, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_VERT_OFF
    { 11, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_HORIZ_OFF
    { 12, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_DOT_OFF
    { 13, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_ANGLED_2_OFF
    { 14, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_3_OFF
    { 15, 26, L"", L"", SWATCH_CLAMP_ALL | SBIT_DECAL },	// MANUFACTURED REDSTONE_WIRE_4_OFF
    { 0, 27, L"white_shulker_box", L"shulker_top_white", SWATCH_REPEAT_ALL },
    { 1, 27, L"orange_shulker_box", L"shulker_top_orange", SWATCH_REPEAT_ALL },
    { 2, 27, L"magenta_shulker_box", L"shulker_top_magenta", SWATCH_REPEAT_ALL },
    { 3, 27, L"light_blue_shulker_box", L"shulker_top_light_blue", SWATCH_REPEAT_ALL },
    { 4, 27, L"yellow_shulker_box", L"shulker_top_yellow", SWATCH_REPEAT_ALL },
    { 5, 27, L"lime_shulker_box", L"shulker_top_lime", SWATCH_REPEAT_ALL },
    { 6, 27, L"pink_shulker_box", L"shulker_top_pink", SWATCH_REPEAT_ALL },
    { 7, 27, L"gray_shulker_box", L"shulker_top_gray", SWATCH_REPEAT_ALL },
    { 8, 27, L"light_gray_shulker_box", L"shulker_top_silver", SWATCH_REPEAT_ALL },
    { 9, 27, L"cyan_shulker_box", L"shulker_top_cyan", SWATCH_REPEAT_ALL },
    { 10, 27, L"purple_shulker_box", L"shulker_top_purple", SWATCH_REPEAT_ALL },
    { 11, 27, L"blue_shulker_box", L"shulker_top_blue", SWATCH_REPEAT_ALL },
    { 12, 27, L"brown_shulker_box", L"shulker_top_brown", SWATCH_REPEAT_ALL },
    { 13, 27, L"green_shulker_box", L"shulker_top_green", SWATCH_REPEAT_ALL },
    { 14, 27, L"red_shulker_box", L"shulker_top_red", SWATCH_REPEAT_ALL },
    { 15, 27, L"black_shulker_box", L"shulker_top_black", SWATCH_REPEAT_ALL },
    { 0, 28, L"white_glazed_terracotta", L"glazed_terracotta_white", SWATCH_REPEAT_ALL },
    { 1, 28, L"orange_glazed_terracotta", L"glazed_terracotta_orange", SWATCH_REPEAT_ALL },
    { 2, 28, L"magenta_glazed_terracotta", L"glazed_terracotta_magenta", SWATCH_REPEAT_ALL },
    { 3, 28, L"light_blue_glazed_terracotta", L"glazed_terracotta_light_blue", SWATCH_REPEAT_ALL },
    { 4, 28, L"yellow_glazed_terracotta", L"glazed_terracotta_yellow", SWATCH_REPEAT_ALL },
    { 5, 28, L"lime_glazed_terracotta", L"glazed_terracotta_lime", SWATCH_REPEAT_ALL },
    { 6, 28, L"pink_glazed_terracotta", L"glazed_terracotta_pink", SWATCH_REPEAT_ALL },
    { 7, 28, L"gray_glazed_terracotta", L"glazed_terracotta_gray", SWATCH_REPEAT_ALL },
    { 8, 28, L"light_gray_glazed_terracotta", L"glazed_terracotta_silver", SWATCH_REPEAT_ALL },
    { 9, 28, L"cyan_glazed_terracotta", L"glazed_terracotta_cyan", SWATCH_REPEAT_ALL },
    { 10, 28, L"purple_glazed_terracotta", L"glazed_terracotta_purple", SWATCH_REPEAT_ALL },
    { 11, 28, L"blue_glazed_terracotta", L"glazed_terracotta_blue", SWATCH_REPEAT_ALL },
    { 12, 28, L"brown_glazed_terracotta", L"glazed_terracotta_brown", SWATCH_REPEAT_ALL },
    { 13, 28, L"green_glazed_terracotta", L"glazed_terracotta_green", SWATCH_REPEAT_ALL },
    { 14, 28, L"red_glazed_terracotta", L"glazed_terracotta_red", SWATCH_REPEAT_ALL },
    { 15, 28, L"black_glazed_terracotta", L"glazed_terracotta_black", SWATCH_REPEAT_ALL },
    { 0, 29, L"white_concrete", L"concrete_white", SWATCH_REPEAT_ALL },
    { 1, 29, L"orange_concrete", L"concrete_orange", SWATCH_REPEAT_ALL },
    { 2, 29, L"magenta_concrete", L"concrete_magenta", SWATCH_REPEAT_ALL },
    { 3, 29, L"light_blue_concrete", L"concrete_light_blue", SWATCH_REPEAT_ALL },
    { 4, 29, L"yellow_concrete", L"concrete_yellow", SWATCH_REPEAT_ALL },
    { 5, 29, L"lime_concrete", L"concrete_lime", SWATCH_REPEAT_ALL },
    { 6, 29, L"pink_concrete", L"concrete_pink", SWATCH_REPEAT_ALL },
    { 7, 29, L"gray_concrete", L"concrete_gray", SWATCH_REPEAT_ALL },
    { 8, 29, L"light_gray_concrete", L"concrete_silver", SWATCH_REPEAT_ALL },
    { 9, 29, L"cyan_concrete", L"concrete_cyan", SWATCH_REPEAT_ALL },
    { 10, 29, L"purple_concrete", L"concrete_purple", SWATCH_REPEAT_ALL },
    { 11, 29, L"blue_concrete", L"concrete_blue", SWATCH_REPEAT_ALL },
    { 12, 29, L"brown_concrete", L"concrete_brown", SWATCH_REPEAT_ALL },
    { 13, 29, L"green_concrete", L"concrete_green", SWATCH_REPEAT_ALL },
    { 14, 29, L"red_concrete", L"concrete_red", SWATCH_REPEAT_ALL },
    { 15, 29, L"black_concrete", L"concrete_black", SWATCH_REPEAT_ALL },
    { 0, 30, L"white_concrete_powder", L"concrete_powder_white", SWATCH_REPEAT_ALL },
    { 1, 30, L"orange_concrete_powder", L"concrete_powder_orange", SWATCH_REPEAT_ALL },
    { 2, 30, L"magenta_concrete_powder", L"concrete_powder_magenta", SWATCH_REPEAT_ALL },
    { 3, 30, L"light_blue_concrete_powder", L"concrete_powder_light_blue", SWATCH_REPEAT_ALL },
    { 4, 30, L"yellow_concrete_powder", L"concrete_powder_yellow", SWATCH_REPEAT_ALL },
    { 5, 30, L"lime_concrete_powder", L"concrete_powder_lime", SWATCH_REPEAT_ALL },
    { 6, 30, L"pink_concrete_powder", L"concrete_powder_pink", SWATCH_REPEAT_ALL },
    { 7, 30, L"gray_concrete_powder", L"concrete_powder_gray", SWATCH_REPEAT_ALL },
    { 8, 30, L"light_gray_concrete_powder", L"concrete_powder_silver", SWATCH_REPEAT_ALL },
    { 9, 30, L"cyan_concrete_powder", L"concrete_powder_cyan", SWATCH_REPEAT_ALL },
    { 10, 30, L"purple_concrete_powder", L"concrete_powder_purple", SWATCH_REPEAT_ALL },
    { 11, 30, L"blue_concrete_powder", L"concrete_powder_blue", SWATCH_REPEAT_ALL },
    { 12, 30, L"brown_concrete_powder", L"concrete_powder_brown", SWATCH_REPEAT_ALL },
    { 13, 30, L"green_concrete_powder", L"concrete_powder_green", SWATCH_REPEAT_ALL },
    { 14, 30, L"red_concrete_powder", L"concrete_powder_red", SWATCH_REPEAT_ALL },
    { 15, 30, L"black_concrete_powder", L"concrete_powder_black", SWATCH_REPEAT_ALL },
    { 0, 31, L"shulker_side_white", L"", SWATCH_REPEAT_ALL },    // optional tiles - BD Craft has them, for example
    { 1, 31, L"shulker_side_orange", L"", SWATCH_REPEAT_ALL },
    { 2, 31, L"shulker_side_magenta", L"", SWATCH_REPEAT_ALL },
    { 3, 31, L"shulker_side_light_blue", L"", SWATCH_REPEAT_ALL },
    { 4, 31, L"shulker_side_yellow", L"", SWATCH_REPEAT_ALL },
    { 5, 31, L"shulker_side_lime", L"", SWATCH_REPEAT_ALL },
    { 6, 31, L"shulker_side_pink", L"", SWATCH_REPEAT_ALL },
    { 7, 31, L"shulker_side_gray", L"", SWATCH_REPEAT_ALL },
    { 8, 31, L"shulker_side_silver", L"", SWATCH_REPEAT_ALL },
    { 9, 31, L"shulker_side_cyan", L"", SWATCH_REPEAT_ALL },
    { 10, 31, L"shulker_side_purple", L"", SWATCH_REPEAT_ALL },
    { 11, 31, L"shulker_side_blue", L"", SWATCH_REPEAT_ALL },
    { 12, 31, L"shulker_side_brown", L"", SWATCH_REPEAT_ALL },
    { 13, 31, L"shulker_side_green", L"", SWATCH_REPEAT_ALL },
    { 14, 31, L"shulker_side_red", L"", SWATCH_REPEAT_ALL },
    { 15, 31, L"shulker_side_black", L"", SWATCH_REPEAT_ALL },
    { 0, 32, L"shulker_bottom_white", L"", SWATCH_REPEAT_ALL },    // optional tiles - BD Craft has them, for example
    { 1, 32, L"shulker_bottom_orange", L"", SWATCH_REPEAT_ALL },
    { 2, 32, L"shulker_bottom_magenta", L"", SWATCH_REPEAT_ALL },
    { 3, 32, L"shulker_bottom_light_blue", L"", SWATCH_REPEAT_ALL },
    { 4, 32, L"shulker_bottom_yellow", L"", SWATCH_REPEAT_ALL },
    { 5, 32, L"shulker_bottom_lime", L"", SWATCH_REPEAT_ALL },
    { 6, 32, L"shulker_bottom_pink", L"", SWATCH_REPEAT_ALL },
    { 7, 32, L"shulker_bottom_gray", L"", SWATCH_REPEAT_ALL },
    { 8, 32, L"shulker_bottom_silver", L"", SWATCH_REPEAT_ALL },
    { 9, 32, L"shulker_bottom_cyan", L"", SWATCH_REPEAT_ALL },
    { 10, 32, L"shulker_bottom_purple", L"", SWATCH_REPEAT_ALL },
    { 11, 32, L"shulker_bottom_blue", L"", SWATCH_REPEAT_ALL },
    { 12, 32, L"shulker_bottom_brown", L"", SWATCH_REPEAT_ALL },
    { 13, 32, L"shulker_bottom_green", L"", SWATCH_REPEAT_ALL },
    { 14, 32, L"shulker_bottom_red", L"", SWATCH_REPEAT_ALL },
    { 15, 32, L"shulker_bottom_black", L"", SWATCH_REPEAT_ALL },
    { 0, 33, L"observer_back", L"", SWATCH_REPEAT_ALL },
    { 1, 33, L"observer_back_on", L"observer_back_lit", SWATCH_REPEAT_ALL },
    { 2, 33, L"observer_front", L"", SWATCH_REPEAT_ALL },
    { 3, 33, L"observer_side", L"", SWATCH_REPEAT_ALL },
    { 4, 33, L"observer_top", L"", SWATCH_REPEAT_ALL },   // alternate name is Sphax BD Craft
    { 5, 33, L"MW_SHULKER_SIDE", L"", SWATCH_REPEAT_ALL },      // TEMPLATE for sides and bottoms of shulker boxes
    { 6, 33, L"MW_SHULKER_BOTTOM", L"", SWATCH_REPEAT_ALL },    // TEMPLATE for sides and bottoms of shulker boxes
    { 7, 33, L"dried_kelp_top", L"", SWATCH_REPEAT_ALL },  // 1.13 starts here
    { 8, 33, L"dried_kelp_side", L"", SWATCH_REPEAT_ALL },
    { 9, 33, L"dried_kelp_bottom", L"", SWATCH_REPEAT_ALL },
    { 10, 33, L"kelp", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
    { 11, 33, L"kelp_plant", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 12, 33, L"sea_pickle", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
    { 13, 33, L"blue_ice", L"", SWATCH_REPEAT_ALL },
    { 14, 33, L"tall_seagrass_bottom", L"", SWATCH_CLAMP_BOTTOM_AND_TOP | SBIT_DECAL },
    { 15, 33, L"tall_seagrass_top", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 0, 34, L"stripped_oak_log", L"", SWATCH_REPEAT_ALL },
	{ 1, 34, L"stripped_spruce_log", L"", SWATCH_REPEAT_ALL },
	{ 2, 34, L"stripped_birch_log", L"", SWATCH_REPEAT_ALL },
	{ 3, 34, L"stripped_jungle_log", L"", SWATCH_REPEAT_ALL },
	{ 4, 34, L"stripped_acacia_log", L"", SWATCH_REPEAT_ALL },
	{ 5, 34, L"stripped_dark_oak_log", L"", SWATCH_REPEAT_ALL },
	{ 6, 34, L"stripped_oak_log_top", L"", SWATCH_REPEAT_ALL },
	{ 7, 34, L"stripped_spruce_log_top", L"", SWATCH_REPEAT_ALL },
	{ 8, 34, L"stripped_birch_log_top", L"", SWATCH_REPEAT_ALL },
	{ 9, 34, L"stripped_jungle_log_top", L"", SWATCH_REPEAT_ALL },
	{ 10, 34, L"stripped_acacia_log_top", L"", SWATCH_REPEAT_ALL },
	{ 11, 34, L"stripped_dark_oak_log_top", L"", SWATCH_REPEAT_ALL },
	{ 12, 34, L"spruce_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
	{ 13, 34, L"birch_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
	{ 14, 34, L"jungle_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
	{ 15, 34, L"acacia_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
	{ 0, 35, L"dark_oak_trapdoor", L"", SWATCH_REPEAT_ALL | SBIT_DECAL },
	{ 1, 35, L"dead_tube_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 2, 35, L"dead_brain_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 3, 35, L"dead_bubble_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 4, 35, L"dead_fire_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 5, 35, L"dead_horn_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 6, 35, L"tube_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 7, 35, L"brain_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 8, 35, L"bubble_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 9, 35, L"fire_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 10, 35, L"horn_coral_block", L"", SWATCH_REPEAT_ALL },
	{ 11, 35, L"tube_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 12, 35, L"brain_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 13, 35, L"bubble_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 14, 35, L"fire_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 15, 35, L"horn_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 0, 36, L"tube_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 1, 36, L"brain_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 2, 36, L"bubble_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 3, 36, L"fire_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 4, 36, L"horn_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 5, 36, L"dead_tube_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 6, 36, L"dead_brain_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 7, 36, L"dead_bubble_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 8, 36, L"dead_fire_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 9, 36, L"dead_horn_coral_fan", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 10, 36, L"turtle_egg", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 11, 36, L"turtle_egg_slightly_cracked", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 12, 36, L"turtle_egg_very_cracked", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 13, 36, L"conduit", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 14, 36, L"dead_tube_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 15, 36, L"dead_brain_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 0, 37, L"dead_bubble_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 1, 37, L"dead_fire_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 2, 37, L"dead_horn_coral", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 3, 37, L"cornflower", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 4, 37, L"lily_of_the_valley", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 5, 37, L"wither_rose", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 6, 37, L"bamboo_large_leaves", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 7, 37, L"bamboo_singleleaf", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 8, 37, L"bamboo_small_leaves", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 9, 37, L"bamboo_stage0", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// X decal
	{ 10, 37, L"bamboo_stalk", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },	// geometry
	{ 11, 37, L"lantern", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 12, 37, L"sweet_berry_bush_stage0", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 13, 37, L"sweet_berry_bush_stage1", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 14, 37, L"sweet_berry_bush_stage2", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 15, 37, L"sweet_berry_bush_stage3", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 0, 38, L"barrel_bottom", L"", SWATCH_REPEAT_ALL },
	{ 1, 38, L"barrel_side", L"", SWATCH_REPEAT_ALL },
	{ 2, 38, L"barrel_top", L"", SWATCH_REPEAT_ALL },
	{ 3, 38, L"barrel_top_open", L"", SWATCH_REPEAT_ALL },
	{ 4, 38, L"bell_bottom", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 5, 38, L"bell_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 6, 38, L"bell_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 7, 38, L"blast_furnace_front", L"", SWATCH_REPEAT_ALL },
	{ 8, 38, L"blast_furnace_front_on", L"", SWATCH_REPEAT_ALL },
	{ 9, 38, L"blast_furnace_side", L"", SWATCH_REPEAT_ALL },
	{ 10, 38, L"blast_furnace_top", L"", SWATCH_REPEAT_ALL },
	{ 11, 38, L"composter_bottom", L"", SWATCH_REPEAT_ALL },
	{ 12, 38, L"composter_compost", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 13, 38, L"composter_ready", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 14, 38, L"composter_side", L"", SWATCH_REPEAT_ALL },
	{ 15, 38, L"composter_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 0, 39, L"campfire_fire", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 1, 39, L"campfire_log", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 2, 39, L"campfire_log_lit", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },	// TODOTODO not sure about this one
	{ 3, 39, L"cartography_table_side1", L"", SWATCH_REPEAT_ALL },
	{ 4, 39, L"cartography_table_side2", L"", SWATCH_REPEAT_ALL },
	{ 5, 39, L"cartography_table_side3", L"", SWATCH_REPEAT_ALL },
	{ 6, 39, L"cartography_table_top", L"", SWATCH_REPEAT_ALL },
	{ 7, 39, L"fletching_table_front", L"", SWATCH_REPEAT_ALL },
	{ 8, 39, L"fletching_table_side", L"", SWATCH_REPEAT_ALL },
	{ 9, 39, L"fletching_table_top", L"", SWATCH_REPEAT_ALL },
	{ 10, 39, L"grindstone_pivot", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 11, 39, L"grindstone_round", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 12, 39, L"grindstone_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 13, 39, L"jigsaw_bottom", L"", SWATCH_REPEAT_ALL },
	{ 14, 39, L"jigsaw_side", L"", SWATCH_REPEAT_ALL },
	{ 15, 39, L"jigsaw_top", L"", SWATCH_REPEAT_ALL },
	{ 0, 40, L"lectern_base", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 1, 40, L"lectern_front", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 2, 40, L"lectern_sides", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 3, 40, L"lectern_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 4, 40, L"loom_bottom", L"", SWATCH_REPEAT_ALL },
	{ 5, 40, L"loom_front", L"", SWATCH_REPEAT_ALL },
	{ 6, 40, L"loom_side", L"", SWATCH_REPEAT_ALL },
	{ 7, 40, L"loom_top", L"", SWATCH_REPEAT_ALL },
	{ 8, 40, L"scaffolding_bottom", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 9, 40, L"scaffolding_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 10, 40, L"scaffolding_top", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 11, 40, L"smoker_bottom", L"", SWATCH_REPEAT_ALL },
	{ 12, 40, L"smoker_front", L"", SWATCH_REPEAT_ALL },
	{ 13, 40, L"smoker_front_on", L"", SWATCH_REPEAT_ALL },
	{ 14, 40, L"smoker_side", L"", SWATCH_REPEAT_ALL },
	{ 15, 40, L"smoker_top", L"", SWATCH_REPEAT_ALL },
	{ 0, 41, L"smithing_table_bottom", L"", SWATCH_REPEAT_ALL },
	{ 1, 41, L"smithing_table_front", L"", SWATCH_REPEAT_ALL },
	{ 2, 41, L"smithing_table_side", L"", SWATCH_REPEAT_ALL },
	{ 3, 41, L"smithing_table_top", L"", SWATCH_REPEAT_ALL },
	{ 4, 41, L"stonecutter_bottom", L"", SWATCH_REPEAT_ALL },
	{ 5, 41, L"stonecutter_saw", L"", SBIT_CLAMP_BOTTOM | SBIT_DECAL },
	{ 6, 41, L"stonecutter_side", L"", SWATCH_CLAMP_ALL | SBIT_CUTOUT_GEOMETRY },
	{ 7, 41, L"stonecutter_top", L"", SWATCH_REPEAT_ALL },
	{ 8, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 9, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 10, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 11, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 12, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 13, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 14, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
	{ 15, 41, L"", L"", SWATCH_REPEAT_ALL }, // Unused
};


// tiles we know we don't use
const TCHAR * gUnneeded[] = {
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
    L"structure_block", // only used in inventory, not used when placed: http://minecraft.gamepedia.com/Structure_Block - we use the other ones of this type
    // older names
    L"leaves_birch_opaque",
    L"leaves_jungle_opaque",
    L"leaves_oak_opaque",
    L"leaves_spruce_opaque",
	L"fire_1",
	L"shulker_box", // generic 1.13; specific colors now used per box
	// this empty string is used to mark the end of this array
    L""
};

#endif