/*
Copyright (c) 2010, Sean Kasun
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


#include "stdafx.h"
#include <string.h>

// We know we won't run into names longer than 100 characters. The old code was
// safe, but was also allocating strings all the time - seems slow.
#define MAX_NAME_LENGTH 100


static int skipType(bfFile bf, int type);
static int skipList(bfFile bf);
static int skipCompound(bfFile bf);

typedef struct BlockTranslator {
	int hashSum;
	unsigned char blockId;
	unsigned char dataVal;
	char *name;
	unsigned long translateFlags;
} BlockTranslator;

#define NUM_TRANS 594

// our bit shift code reader can read only up to 2^9 entries right now. TODO
#define MAX_PALETTE	512

static bool hashMade = false;

// for another 256 block types, this bit gets set in the dataVal field (we're out of bits with block IDs)
#define HIGH_BIT 0x80

// old data values mixed with new. This works because 0 means empty under both systems, and the high bits (0xff00) are set for all new-style flowers,
// so the old data values 1-13 don't overlap the new ones, which are 16 and higher.
// See nbt for the loop minecraft:red_flower etc. that sets these values.
// COPIED FROM ObjFileManip.cpp, should share someday...
#define RED_FLOWER_FIELD		0x10
#define YELLOW_FLOWER_FIELD		0x20
#define RED_MUSHROOM_FIELD		0x30
#define BROWN_MUSHROOM_FIELD	0x40
#define SAPLING_FIELD			0x50
#define DEADBUSH_FIELD			0x60
#define TALLGRASS_FIELD			0x70
#define CACTUS_FIELD			0x80


// properties and which blocks use them:
// age: AGE_PROP
// attached: TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP
// axis: AXIS_PROP, QUARTZ_PILLAR_PROP
// bites: BITES_PROP
// conditional: COMMAND_BLOCK_PROP
// delay: REPEATER_PROP
// disarmed: TRIPWIRE_PROP
// distance: LEAF_PROP
// down|up: MUSHROOM_PROP, MUSHROOM_STEM_PROP
// east|north|west|south: MUSHROOM_PROP, MUSHROOM_STEM_PROP, WIRE_PROP
// eggs: EGG_PROP
// enabled: HOPPER_PROP
// extended: PISTON_PROP
// face: LEVER_PROP, BUTTON_PROP
// facing: DOOR_PROP, TORCH_PROP, STAIRS_PROP, LEVER_PROP, CHEST_PROP, FURNACE_PROP, FACING_PROP, BUTTON_PROP, SWNE_FACING_PROP, 
//     BED_PROP, DROPPER_PROP, TRAPDOOR_PROP, PISTON_PROP, PISTON_HEAD_PROP, COMMAND_BLOCK_PROP, HOPPER_PROP, OBSERVER_PROP,
//     REPEATER_PROP, COMPARATOR_PROP, HEAD_WALL_PROP
// falling: FLUID_PROP
// half: DOOR_PROP, TALL_FLOWER_PROP, STAIRS_PROP, TRAPDOOR_PROP
// hinge: DOOR_PROP
// in_wall: fence gate TODO
// inverted: DAYLIGHT_PROP
// layers: SNOW_PROP
// level: FLUID_PROP
// lit: TORCH_PROP (redstone only), FURNACE_PROP, REDSTONE_ORE_PROP
// locked: REPEATER_PROP
// mode: STRUCTURE_PROP, COMPARATOR_PROP
// moisture: FARMLAND_PROP
// north|east|south|west[|up]: VINE_PROP, TRIPWIRE_PROP
// occupied: BED_PROP
// open: DOOR_PROP, TRAPDOOR_PROP
// part: BED_PROP
// persistent: LEAF_PROP
// pickles: PICKLE_PROP
// power: WIRE_PROP, WT_PRESSURE_PROP, DAYLIGHT_PROP
// powered: DOOR_PROP, LEVER_PROP, RAIL_PROP, PRESSURE_PROP, BUTTON_PROP, TRAPDOOR_PROP, TRIPWIRE_PROP, TRIPWIRE_HOOK_PROP, REPEATER_PROP, COMPARATOR_PROP
// rotation: STANDING_SIGN_PROP, HEAD_PROP
// shape: STAIRS_PROP, RAIL_PROP
// short: PISTON_HEAD_PROP
// snowy: SNOWY_PROP
// stage: SAPLING_PROP
// triggered: DROPPER_PROP
// type: SLAB_PROP, CHEST_PROP, PISTON_HEAD_PROP
// waterlogged: a bunch... SLAB_PROP, TRAPDOOR_PROP - not stairs? That would be nice...

// various properties of palette entries, can be used to differentiate how to use properties
#define NO_PROP				  0
// axis: x|y|z
#define AXIS_PROP			  1
// snowy: false|true
// note that the snowy prop actually does nothing; put here mostly to keep track; could be deleted
#define SNOWY_PROP			  2
// facing: north|south|east|west
// half: upper|lower
// hinge: left|right
// open: true|false
// powered: true|false
#define DOOR_PROP			  3
// for water and lava
// falling: true|false
// level: 1-7|8 when falling true
#define FLUID_PROP			  4
// saplings
// stage: 0|1 - ignored
#define SAPLING_PROP			  5
// persistent: true|false
// distance: 1-7 - ignored
#define LEAF_PROP			  6
// for sunflowers, etc.
// half: upper|lower
#define TALL_FLOWER_PROP	  7
// down|up|east|north|west|south: true|false
#define MUSHROOM_PROP		  8
#define MUSHROOM_STEM_PROP	  9
// type: top|bottom|double
// waterlogged: true|false - TODO?
#define SLAB_PROP			 10
// facing: north|east|south|west
// lit: true|false - for redstone 
#define TORCH_PROP			 11
// facing: north|east|south|west
// half: top|bottom - note this is different from upper|lower for doors and sunflowers
// shape: straight|inner_left|inner_right|outer_left|outer_right - we ignore, deriving from neighbors
#define STAIRS_PROP			 12
// Redstone wire
// north|south|east|west: none|side|up https://minecraft.gamepedia.com/Redstone#Block_state - we ignore, it's from geometry
// power: 0-15
#define WIRE_PROP		     13
// Lever
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define LEVER_PROP			 14
// facing: north|west|south|east
// type: single|left|right - TODO currently ignored; if we pay attention to it, we'll need to peel off ender chest, as these don't use this
#define CHEST_PROP			 15
// wheat, frosted ice, cactus, sugar cane, etc.
// age: 0-7 or 0-15, (0-3 for frosted ice)
#define AGE_PROP			 16
// moisture: 0-7
#define FARMLAND_PROP		 17
// facing: north|west|south|east - like torches
// lit: true|false 
#define FURNACE_PROP		 18
// used for ladders and wall signs
// facing: north|west|south|east - like torches
#define FACING_PROP			 19
// rotation: 0-15
#define STANDING_SIGN_PROP	 20
// shape: north_south|east_west|north_east|north_west|south_east|south_west|ascending_north|ascending_south|ascending_east|ascending_west
// powered: true|false - all but rail itself
#define RAIL_PROP			 21
// powered: true|false
// may use higher bits for types of pressure plates? - TODO
#define PRESSURE_PROP		 22
// power: 0-15 - really, same as WIRE_PROP, but just in case
#define WT_PRESSURE_PROP	 23
// lit: true|false 
#define REDSTONE_ORE_PROP	 24
// face: floor|ceiling|wall
// facing: north|west|south|east
// powered: true|false
#define BUTTON_PROP			 25
// layers: 1-8
#define SNOW_PROP			 26
// facing: north|south|east|west
// open: true|false
// in_wall: true|false - TODO! If true, the gate is lowered by three pixels, to accommodate attaching more cleanly with normal and mossy Cobblestone Walls
// powered: true|false - ignored
#define FENCE_GATE_PROP		 27
// facing: south|west|north|east
#define SWNE_FACING_PROP	 28
// bites: 0-6
#define BITES_PROP			 29
// facing: south|west|north|east
// occupied: true|false - seems to be occupied only if someone else is occupying it?
// part: head|foot
#define BED_PROP			 30
// facing: down|up|north|south|west|east
// triggered: true|false
#define DROPPER_PROP		 31
// facing: down|up|north|south|west|east
// half: top|bottom
// open: true|false
// powered: true|false
// waterlogged: true|false
#define TRAPDOOR_PROP		 32
// facing: north|east|south|west
#define ANVIL_PROP			 33
// facing: down|up|north|south|west|east
// extended: true|false
#define PISTON_PROP			 34
// facing: down|up|north|south|west|east
// extended: true|false - ignored, don't know what that is (block wouldn't exist otherwise, right?
// type: sticky|normal
// short: true|false - TODO, piston arm is shorter by 4 pixels, https://minecraft.gamepedia.com/Piston#Block_state_2 - not sure how to get this
#define PISTON_HEAD_PROP	 35
// south|west|north|east|up: true|false
#define VINE_PROP			 36
// facing: south|west|north|east
#define END_PORTAL_PROP		 37
// age: 0-3
// facing: north|east|south|west
#define COCOA_PROP			 38
// south|west|north|east|up: true|false - ignored, from context
// attached: true|false
// powered: true|false
// disarmed: true|false
#define TRIPWIRE_PROP		 39
// facing: south|west|north|east
// attached: true|false
// powered: true|false
#define TRIPWIRE_HOOK_PROP	 40
// facing: down|up|north|south|west|east
// conditional: true|false
#define COMMAND_BLOCK_PROP	 41
// inverted: true|false
// power: 0-15
#define DAYLIGHT_PROP		 42
// enabled: true|false
// facing: down|north|south|west|east
#define HOPPER_PROP			 43
// axis: x|y|z
#define QUARTZ_PILLAR_PROP	 44
// facing: down|up|south|north|east|west
#define OBSERVER_PROP		 45
// mode: data|save|load|corner
#define STRUCTURE_PROP		 46
// delay: 1-4
// facing: north|east|south|west
// locked: true|false - ignored
// powered: true|false - ignored
#define REPEATER_PROP		 47
// facing: north|east|south|west
// mode: compare|subtract
// powered: true|false
#define COMPARATOR_PROP		 48
// rotation: 0-15
#define HEAD_PROP			 49
// facing: north|south|east|west
#define HEAD_WALL_PROP		 50
// clear out dataVal, which might get "age" etc. before saving
#define TRULY_NO_PROP		 51
// waterlogged: true|false, 1 HIGH bit
#define WATERLOGGED_PROP	 52
// waterlogged: true|false, 1 HIGH bit
// facing: south|north|west|east - 2 HIGH bits
#define FAN_PROP			 53
// pickles: 1-4
// waterlogged: true|false
#define PICKLE_PROP			 54
// eggs: 1-4
#define EGG_PROP			 55


BlockTranslator BlockTranslations[NUM_TRANS] = {
//hash ID data name flags
// hash is computed once when 1.13 data is first read in.
// first column is high-order bit; second column is "traditional" value, as found in blockInfo.cpp
// and https://minecraft.gamepedia.com/Java_Edition_data_values/Pre-flattening#Block_IDs
{ 0,   0,           0, "air", NO_PROP },
{ 0, 166,           0, "barrier", NO_PROP },
{ 0,   1,           0, "stone", NO_PROP },
{ 0,   1,           1, "granite", NO_PROP },
{ 0,   1,           2, "polished_granite", NO_PROP },
{ 0,   1,           3, "diorite", NO_PROP },
{ 0,   1,           4, "polished_diorite", NO_PROP },
{ 0,   1,           5, "andesite", NO_PROP },
{ 0,   1,           6, "polished_andesite", NO_PROP },
{ 0, 170,           0, "hay_block", AXIS_PROP },
{ 0,   2,           0, "grass_block", SNOWY_PROP }, // has SNOWY_PROP, which we don't actually pay attention to here
{ 0,   3,           0, "dirt", NO_PROP }, // no SNOWY_PROP
{ 0,   3,           1, "coarse_dirt", NO_PROP }, // no SNOWY_PROP
{ 0,   3,           2, "podzol", SNOWY_PROP }, // does indeed have snowy prop
{ 0,   4,           0, "cobblestone", NO_PROP },
{ 0,   5,           0, "oak_planks", NO_PROP },
{ 0,   5,           1, "spruce_planks", NO_PROP },
{ 0,   5,           2, "birch_planks", NO_PROP },
{ 0,   5,           3, "jungle_planks", NO_PROP },
{ 0,   5,           4, "acacia_planks", NO_PROP },
{ 0,   5,           5, "dark_oak_planks", NO_PROP },
{ 0,   6,           0, "oak_sapling", SAPLING_PROP },
{ 0,   6,           1, "spruce_sapling", SAPLING_PROP },
{ 0,   6,           2, "birch_sapling", SAPLING_PROP },
{ 0,   6,           3, "jungle_sapling", SAPLING_PROP },
{ 0,   6,           4, "acacia_sapling", SAPLING_PROP },
{ 0,   6,           5, "dark_oak_sapling", SAPLING_PROP },
{ 0,  64,           0, "oak_door", DOOR_PROP },
{ 0, 193,           0, "spruce_door", DOOR_PROP },
{ 0, 194,           0, "birch_door", DOOR_PROP },
{ 0, 195,           0, "jungle_door", DOOR_PROP },
{ 0, 196,           0, "acacia_door", DOOR_PROP },
{ 0, 197,           0, "dark_oak_door", DOOR_PROP },
{ 0,   7,           0, "bedrock", NO_PROP },
{ 0,   9,           0, "water", FLUID_PROP },
{ 0,  11,           0, "lava", FLUID_PROP },
{ 0,  12,           0, "sand", NO_PROP },
{ 0,  12,           1, "red_sand", NO_PROP },
{ 0,  24,           0, "sandstone", NO_PROP }, // TODO 1.13 check: For normal sandstone the bottom has a cracked pattern. The other types of sandstone have bottom faces same as the tops.
{ 0,  24,           1, "chiseled_sandstone", NO_PROP },
{ 0,  24,           2, "cut_sandstone", NO_PROP }, // aka smooth sandstone
{ 0, 179,           0, "red_sandstone", NO_PROP },
{ 0, 179,           1, "chiseled_red_sandstone", NO_PROP },
{ 0, 179,           2, "cut_red_sandstone", NO_PROP }, // aka smooth red sandstone
{ 0,  13,           0, "gravel", NO_PROP },
{ 0,  14,           0, "gold_ore", NO_PROP },
{ 0,  15,           0, "iron_ore", NO_PROP },
{ 0,  16,           0, "coal_ore", NO_PROP },
{ 0,   5,           0, "oak_wood", NO_PROP },
{ 0,   5,           1, "spruce_wood", NO_PROP },
{ 0,   5,           2, "birch_wood", NO_PROP },
{ 0,   5,           3, "jungle_wood", NO_PROP },
{ 0,   5,           4, "acacia_wood", NO_PROP },
{ 0,   5,           5, "dark_oak_wood", NO_PROP },
{ 0,  17,           0, "oak_log", AXIS_PROP },
{ 0,  17,           1, "spruce_log", AXIS_PROP },
{ 0,  17,           2, "birch_log", AXIS_PROP },
{ 0,  17,           3, "jungle_log", AXIS_PROP },
{ 0, 162,           0, "acacia_log", AXIS_PROP },
{ 0, 162,           1, "dark_oak_log", AXIS_PROP },
{ 0,  18,           0, "oak_leaves", LEAF_PROP },
{ 0,  18,           1, "spruce_leaves", LEAF_PROP },
{ 0,  18,           2, "birch_leaves", LEAF_PROP },
{ 0,  18,           3, "jungle_leaves", LEAF_PROP },
{ 0, 161,           0, "acacia_leaves", LEAF_PROP },
{ 0, 161,           1, "dark_oak_leaves", LEAF_PROP },
{ 0,  32,           0, "dead_bush", NO_PROP },
{ 0,  31,           1, "grass", NO_PROP }, // tall grass
{ 0,  31,           2, "fern", NO_PROP },
{ 0,  19,           0, "sponge", NO_PROP },
{ 0,  19,           1, "wet_sponge", NO_PROP },
{ 0,  20,           0, "glass", NO_PROP },
{ 0,  95,           0, "white_stained_glass", NO_PROP },
{ 0,  95,           1, "orange_stained_glass", NO_PROP },
{ 0,  95,           2, "magenta_stained_glass", NO_PROP },
{ 0,  95,           3, "light_blue_stained_glass", NO_PROP },
{ 0,  95,           4, "yellow_stained_glass", NO_PROP },
{ 0,  95,           5, "lime_stained_glass", NO_PROP },
{ 0,  95,           6, "pink_stained_glass", NO_PROP },
{ 0,  95,           7, "gray_stained_glass", NO_PROP },
{ 0,  95,           8, "light_gray_stained_glass", NO_PROP },
{ 0,  95,           9, "cyan_stained_glass", NO_PROP },
{ 0,  95,          10, "purple_stained_glass", NO_PROP },
{ 0,  95,          11, "blue_stained_glass", NO_PROP },
{ 0,  95,          12, "brown_stained_glass", NO_PROP },
{ 0,  95,          13, "green_stained_glass", NO_PROP },
{ 0,  95,          14, "red_stained_glass", NO_PROP },
{ 0,  95,          15, "black_stained_glass", NO_PROP },
{ 0, 160,           0, "white_stained_glass_pane", NO_PROP },
{ 0, 160,           1, "orange_stained_glass_pane", NO_PROP },
{ 0, 160,           2, "magenta_stained_glass_pane", NO_PROP },
{ 0, 160,           3, "light_blue_stained_glass_pane", NO_PROP },
{ 0, 160,           4, "yellow_stained_glass_pane", NO_PROP },
{ 0, 160,           5, "lime_stained_glass_pane", NO_PROP },
{ 0, 160,           6, "pink_stained_glass_pane", NO_PROP },
{ 0, 160,           7, "gray_stained_glass_pane", NO_PROP },
{ 0, 160,           8, "light_gray_stained_glass_pane", NO_PROP },
{ 0, 160,           9, "cyan_stained_glass_pane", NO_PROP },
{ 0, 160,          10, "purple_stained_glass_pane", NO_PROP },
{ 0, 160,          11, "blue_stained_glass_pane", NO_PROP },
{ 0, 160,          12, "brown_stained_glass_pane", NO_PROP },
{ 0, 160,          13, "green_stained_glass_pane", NO_PROP },
{ 0, 160,          14, "red_stained_glass_pane", NO_PROP },
{ 0, 160,          15, "black_stained_glass_pane", NO_PROP },
{ 0, 102,           0, "glass_pane", NO_PROP },
{ 0,  37,           0, "dandelion", NO_PROP },
{ 0,  38,           0, "poppy", NO_PROP },
{ 0,  38,           1, "blue_orchid", NO_PROP },
{ 0,  38,           2, "allium", NO_PROP },
{ 0,  38,           3, "azure_bluet", NO_PROP },
{ 0,  38,           4, "red_tulip", NO_PROP },
{ 0,  38,           5, "orange_tulip", NO_PROP },
{ 0,  38,           6, "white_tulip", NO_PROP },
{ 0,  38,           7, "pink_tulip", NO_PROP },
{ 0,  38,           8, "oxeye_daisy", NO_PROP },
{ 0, 175,           0, "sunflower", TALL_FLOWER_PROP },
{ 0, 175,           1, "lilac", TALL_FLOWER_PROP },
{ 0, 175,           2, "tall_grass", TALL_FLOWER_PROP },
{ 0, 175,           3, "large_fern", TALL_FLOWER_PROP },
{ 0, 175,           4, "rose_bush", TALL_FLOWER_PROP },
{ 0, 175,           5, "peony", TALL_FLOWER_PROP },
{ 0,  39,           0, "brown_mushroom", NO_PROP },
{ 0, 100,           0, "red_mushroom_block", MUSHROOM_PROP },
{ 0,  99,           0, "brown_mushroom_block", MUSHROOM_PROP },
{ 0, 100,           0, "mushroom_stem", MUSHROOM_STEM_PROP }, // red mushroom block chosen, arbitrarily; either is fine
{ 0,  41,           0, "gold_block", NO_PROP },
{ 0,  42,           0, "iron_block", NO_PROP },
{ 0,  44,           0, "stone_slab", SLAB_PROP },
{ 0,  44,           1, "sandstone_slab", SLAB_PROP },
{ 0, 182,           0, "red_sandstone_slab", SLAB_PROP }, // really, just uses 182 exclusively; sometimes rumored to be 205/0, but not so https://minecraft.gamepedia.com/Java_Edition_data_values#Stone_Slabs
{ 0,  44,           2, "petrified_oak_slab", SLAB_PROP },
{ 0,  44,           3, "cobblestone_slab", SLAB_PROP },
{ 0,  44,           4, "brick_slab", SLAB_PROP },
{ 0,  44,           5, "stone_brick_slab", SLAB_PROP },
{ 0,  44,           6, "nether_brick_slab", SLAB_PROP },
{ 0,  44,           7, "quartz_slab", SLAB_PROP },
{ 0, 126,           0, "oak_slab", SLAB_PROP },
{ 0, 126,           1, "spruce_slab", SLAB_PROP },
{ 0, 126,           2, "birch_slab", SLAB_PROP },
{ 0, 126,           3, "jungle_slab", SLAB_PROP },
{ 0, 126,           4, "acacia_slab", SLAB_PROP },
{ 0, 126,           5, "dark_oak_slab", SLAB_PROP },
{ 0,  45,           0, "bricks", NO_PROP },
{ 0,  46,           0, "tnt", NO_PROP },
{ 0,  47,           0, "bookshelf", NO_PROP },
{ 0,  48,           0, "mossy_cobblestone", NO_PROP },
{ 0,  49,           0, "obsidian", NO_PROP },
{ 0,  50,           0, "torch", TORCH_PROP },
{ 0,  50,           0, "wall_torch", TORCH_PROP },
{ 0,  51,           0, "fire", NO_PROP },
{ 0,  52,           0, "spawner", NO_PROP },
{ 0,  53,           0, "oak_stairs", STAIRS_PROP },
{ 0, 134,           0, "spruce_stairs", STAIRS_PROP },
{ 0, 135,           0, "birch_stairs", STAIRS_PROP },
{ 0, 136,           0, "jungle_stairs", STAIRS_PROP },
{ 0, 163,           0, "acacia_stairs", STAIRS_PROP },
{ 0, 164,           0, "dark_oak_stairs", STAIRS_PROP },
{ 0,  54,           0, "chest", CHEST_PROP },
{ 0, 146,           0, "trapped_chest", CHEST_PROP },
{ 0,  55,           0, "redstone_wire", WIRE_PROP },
{ 0,  56,           0, "diamond_ore", NO_PROP },
{ 0,  93,           0, "repeater", REPEATER_PROP },
{ 0, 149,           0, "comparator", COMPARATOR_PROP },
{ 0, 173,           0, "coal_block", NO_PROP },
{ 0,  57,           0, "diamond_block", NO_PROP },
{ 0,  58,           0, "crafting_table", NO_PROP },
{ 0,  59,           0, "wheat", AGE_PROP },
{ 0,  60,           0, "farmland", FARMLAND_PROP },
{ 0,  61,           0, "furnace", FURNACE_PROP },
{ 0,  63,           0, "sign", STANDING_SIGN_PROP },
{ 0,  68,           0, "wall_sign", FACING_PROP },
{ 0,  65,           0, "ladder", FACING_PROP },
{ 0,  66,           0, "rail", RAIL_PROP },   /* 200 */
{ 0,  27,           0, "powered_rail", RAIL_PROP },
{ 0, 157,           0, "activator_rail", RAIL_PROP },
{ 0,  28,           0, "detector_rail", RAIL_PROP },
{ 0,  67,           0, "cobblestone_stairs", STAIRS_PROP },
{ 0, 128,           0, "sandstone_stairs", STAIRS_PROP },
{ 0, 180,           0, "red_sandstone_stairs", STAIRS_PROP },
{ 0,  69,           0, "lever", LEVER_PROP },
{ 0,  70,           0, "stone_pressure_plate", PRESSURE_PROP },
{ 0,  72,           0, "oak_pressure_plate", PRESSURE_PROP },
{ 0, 147,           0, "light_weighted_pressure_plate", WT_PRESSURE_PROP },
{ 0, 148,           0, "heavy_weighted_pressure_plate", WT_PRESSURE_PROP },
{ 0,  71,           0, "iron_door", DOOR_PROP },
{ 0,  73,           0, "redstone_ore", REDSTONE_ORE_PROP },	// unlit by default
{ 0,  76,           0, "redstone_torch", TORCH_PROP },
{ 0,  76,           0, "redstone_wall_torch", TORCH_PROP },
{ 0,  77,           0, "stone_button", BUTTON_PROP },
{ 0, 143,           0, "oak_button", BUTTON_PROP },
{ 0,  78,           0, "snow", SNOW_PROP },
{ 0, 171,           0, "white_carpet", NO_PROP },
{ 0, 171,           1, "orange_carpet", NO_PROP },
{ 0, 171,           2, "magenta_carpet", NO_PROP },
{ 0, 171,           3, "light_blue_carpet", NO_PROP },
{ 0, 171,           4, "yellow_carpet", NO_PROP },
{ 0, 171,           5, "lime_carpet", NO_PROP },
{ 0, 171,           6, "pink_carpet", NO_PROP },
{ 0, 171,           7, "gray_carpet", NO_PROP },
{ 0, 171,           8, "light_gray_carpet", NO_PROP },
{ 0, 171,           9, "cyan_carpet", NO_PROP },
{ 0, 171,          10, "purple_carpet", NO_PROP },
{ 0, 171,          11, "blue_carpet", NO_PROP },
{ 0, 171,          12, "brown_carpet", NO_PROP },
{ 0, 171,          13, "green_carpet", NO_PROP },
{ 0, 171,          14, "red_carpet", NO_PROP },
{ 0, 171,          15, "black_carpet", NO_PROP },
{ 0,  79,           0, "ice", NO_PROP },
{ 0, 212,           0, "frosted_ice", AGE_PROP },
{ 0, 174,           0, "packed_ice", NO_PROP },
{ 0,  81,           0, "cactus", AGE_PROP },
{ 0,  82,           0, "clay", NO_PROP },
{ 0, 159,           0, "white_terracotta", NO_PROP },
{ 0, 159,           1, "orange_terracotta", NO_PROP },
{ 0, 159,           2, "magenta_terracotta", NO_PROP },
{ 0, 159,           3, "light_blue_terracotta", NO_PROP },
{ 0, 159,           4, "yellow_terracotta", NO_PROP },
{ 0, 159,           5, "lime_terracotta", NO_PROP },
{ 0, 159,           6, "pink_terracotta", NO_PROP },
{ 0, 159,           7, "gray_terracotta", NO_PROP },
{ 0, 159,           8, "light_gray_terracotta", NO_PROP },
{ 0, 159,           9, "cyan_terracotta", NO_PROP },
{ 0, 159,          10, "purple_terracotta", NO_PROP },
{ 0, 159,          11, "blue_terracotta", NO_PROP },
{ 0, 159,          12, "brown_terracotta", NO_PROP },
{ 0, 159,          13, "green_terracotta", NO_PROP },
{ 0, 159,          14, "red_terracotta", NO_PROP },
{ 0, 159,          15, "black_terracotta", NO_PROP },
{ 0, 172,           0, "terracotta", NO_PROP },
{ 0,  83,           0, "sugar_cane", AGE_PROP },
{ 0,  84,           0, "jukebox", NO_PROP },
{ 0,  85,           0, "oak_fence", NO_PROP }, // has props, but we ignore them
{ 0, 188,           0, "spruce_fence", NO_PROP },
{ 0, 189,           0, "birch_fence", NO_PROP },
{ 0, 190,           0, "jungle_fence", NO_PROP },
{ 0, 191,           0, "dark_oak_fence", NO_PROP },
{ 0, 192,           0, "acacia_fence", NO_PROP },
{ 0, 107,           0, "oak_fence_gate", FENCE_GATE_PROP },
{ 0, 183,           0, "spruce_fence_gate", FENCE_GATE_PROP },
{ 0, 184,           0, "birch_fence_gate", FENCE_GATE_PROP },
{ 0, 185,           0, "jungle_fence_gate", FENCE_GATE_PROP },
{ 0, 186,           0, "dark_oak_fence_gate", FENCE_GATE_PROP },
{ 0, 187,           0, "acacia_fence_gate", FENCE_GATE_PROP },
{ 0, 104,           0, "pumpkin_stem", AGE_PROP },
{ 0, 104,           7, "attached_pumpkin_stem", NO_PROP },	// someday could use facing property FACING_PROP to point to correct pumpkin (same with melons) long-term TODO
{ 0,  86,           4, "pumpkin", NO_PROP }, //  uncarved pumpkin, same on all sides - dataVal 4
{ 0,  86,           0, "carved_pumpkin", SWNE_FACING_PROP },	// black carved pumpkin
{ 0,  91,           0, "jack_o_lantern", SWNE_FACING_PROP },
{ 0,  87,           0, "netherrack", NO_PROP },
{ 0,  88,           0, "soul_sand", NO_PROP },
{ 0,  89,           0, "glowstone", NO_PROP },
{ 0,  90,           0, "nether_portal", NO_PROP }, // axis: portal's long edge runs east-west; we don't need this info currently, so ignore it
{ 0,  35,           0, "white_wool", NO_PROP },
{ 0,  35,           1, "orange_wool", NO_PROP },
{ 0,  35,           2, "magenta_wool", NO_PROP },
{ 0,  35,           3, "light_blue_wool", NO_PROP },
{ 0,  35,           4, "yellow_wool", NO_PROP },
{ 0,  35,           5, "lime_wool", NO_PROP },
{ 0,  35,           6, "pink_wool", NO_PROP },
{ 0,  35,           7, "gray_wool", NO_PROP },
{ 0,  35,           8, "light_gray_wool", NO_PROP },
{ 0,  35,           9, "cyan_wool", NO_PROP },
{ 0,  35,          10, "purple_wool", NO_PROP },
{ 0,  35,          11, "blue_wool", NO_PROP },
{ 0,  35,          12, "brown_wool", NO_PROP },
{ 0,  35,          13, "green_wool", NO_PROP },
{ 0,  35,          14, "red_wool", NO_PROP },
{ 0,  35,          15, "black_wool", NO_PROP },
{ 0,  21,           0, "lapis_ore", NO_PROP },
{ 0,  22,           0, "lapis_block", NO_PROP },
{ 0,  23,           0, "dispenser", DROPPER_PROP },
{ 0, 158,           0, "dropper", DROPPER_PROP },
{ 0,  25,           0, "note_block", NO_PROP },	// pitch, powered, instrument - ignored
{ 0,  92,           0, "cake", BITES_PROP },
{ 0,  26,           0, "bed", BED_PROP },
{ 0,  96,           0, "oak_trapdoor", TRAPDOOR_PROP },
{ 0, 167,           0, "iron_trapdoor", TRAPDOOR_PROP },
{ 0,  30,           0, "cobweb", NO_PROP },
{ 0,  98,           0, "stone_bricks", NO_PROP },
{ 0,  98,           1, "mossy_stone_bricks", NO_PROP },
{ 0,  98,           2, "cracked_stone_bricks", NO_PROP },
{ 0,  98,           3, "chiseled_stone_bricks", NO_PROP },
{ 0,  97,           0, "infested_stone", NO_PROP }, // was called "monster egg"
{ 0,  97,           1, "infested_cobblestone", NO_PROP },
{ 0,  97,           2, "infested_stone_bricks", NO_PROP },
{ 0,  97,           3, "infested_mossy_stone_bricks", NO_PROP },
{ 0,  97,           4, "infested_cracked_stone_bricks", NO_PROP },
{ 0,  97,           5, "infested_chiseled_stone_bricks", NO_PROP },
{ 0,  33,           0, "piston", PISTON_PROP },
{ 0,  29,           0, "sticky_piston", PISTON_PROP },
{ 0, 101,           0, "iron_bars", NO_PROP }, // we derive props from situation
{ 0, 103,           0, "melon", NO_PROP },
{ 0, 108,           0, "brick_stairs", STAIRS_PROP },
{ 0, 109,           0, "stone_brick_stairs", STAIRS_PROP },
{ 0, 106,           0, "vine", VINE_PROP },
{ 0, 112,           0, "nether_bricks", NO_PROP },
{ 0, 113,           0, "nether_brick_fence", NO_PROP },
{ 0, 114,           0, "nether_brick_stairs", STAIRS_PROP },
{ 0, 115,           0, "nether_wart", AGE_PROP },
{ 0, 118,           0, "cauldron", NO_PROP }, // level directly translates to dataVal
{ 0, 116,           0, "enchanting_table", NO_PROP },
{ 0, 145,           0, "anvil", ANVIL_PROP },
{ 0, 145,           4, "chipped_anvil", ANVIL_PROP },
{ 0, 145,           8, "damaged_anvil", ANVIL_PROP },
{ 0, 121,           0, "end_stone", NO_PROP },
{ 0, 120,           0, "end_portal_frame", END_PORTAL_PROP },
{ 0, 110,           0, "mycelium", SNOWY_PROP },
{ 0, 111,           0, "lily_pad", NO_PROP },
{ 0, 122,           0, "dragon_egg", NO_PROP },
{ 0, 123,           0, "redstone_lamp", REDSTONE_ORE_PROP }, // goes to 124 when lit
{ 0, 127,           0, "cocoa", COCOA_PROP },
{ 0, 130,           0, "ender_chest", CHEST_PROP }, // TODO: if I fix 1.13 chests (can be next to each other), make this a separate property
{ 0, 129,           0, "emerald_ore", NO_PROP },
{ 0, 133,           0, "emerald_block", NO_PROP },
{ 0, 152,           0, "redstone_block", NO_PROP },
{ 0, 132,           0, "tripwire", TRIPWIRE_PROP },
{ 0, 131,           0, "tripwire_hook", TRIPWIRE_HOOK_PROP },
{ 0, 137,           0, "command_block", COMMAND_BLOCK_PROP },
{ 0, 210,           0, "repeating_command_block", COMMAND_BLOCK_PROP },
{ 0, 211,           0, "chain_command_block", COMMAND_BLOCK_PROP },
{ 0, 138,           0, "beacon", NO_PROP },
{ 0, 139,           0, "cobblestone_wall", NO_PROP },
{ 0, 139,           1, "mossy_cobblestone_wall", NO_PROP },
{ 0, 141,           0, "carrots", NO_PROP },
{ 0, 142,           0, "potatoes", NO_PROP },
{ 0, 151,           0, "daylight_detector", DAYLIGHT_PROP },
{ 0, 153,           0, "nether_quartz_ore", NO_PROP },
{ 0, 154,           0, "hopper", HOPPER_PROP },
{ 0, 155,           0, "quartz_block", AXIS_PROP },
{ 0, 155,           1, "chiseled_quartz_block", AXIS_PROP },
{ 0, 155,           0, "quartz_pillar", QUARTZ_PILLAR_PROP },
{ 0, 156,           0, "quartz_stairs", STAIRS_PROP },
{ 0, 165,           0, "slime_block", NO_PROP },
{ 0, 168,           0, "prismarine", NO_PROP },
{ 0, 168,           1, "prismarine_bricks", NO_PROP },
{ 0, 168,           2, "dark_prismarine", NO_PROP },
{ 0, 169,           0, "sea_lantern", NO_PROP },
{ 0, 198,           0, "end_rod", OBSERVER_PROP },
{ 0, 199,           0, "chorus_plant", NO_PROP },
{ 0, 200,           0, "chorus_flower", NO_PROP },	// uses age
{ 0, 201,           0, "purpur_block", NO_PROP },
{ 0, 202,           0, "purpur_pillar", AXIS_PROP },
{ 0, 203,           0, "purpur_stairs", STAIRS_PROP },
{ 0, 205,           0, "purpur_slab", SLAB_PROP },	// allegedly data value is 1
{ 0, 206,           0, "end_stone_bricks", NO_PROP },
{ 0, 207,           0, "beetroots", AGE_PROP },
{ 0, 208,           0, "grass_path", NO_PROP },
{ 0, 213,           0, "magma_block", NO_PROP },
{ 0, 214,           0, "nether_wart_block", NO_PROP },
{ 0, 215,           0, "red_nether_bricks", NO_PROP },
{ 0, 216,           0, "bone_block", AXIS_PROP },
{ 0, 218,           0, "observer", OBSERVER_PROP },
{ 0, 219,           0, "shulker_box", NO_PROP },
{ 0, 219,           0, "white_shulker_box", NO_PROP },	// same as above, I guess?
{ 0, 220,           0, "orange_shulker_box", NO_PROP },
{ 0, 221,           0, "magenta_shulker_box", NO_PROP },
{ 0, 222,           0, "light_blue_shulker_box", NO_PROP },
{ 0, 223,           0, "yellow_shulker_box", NO_PROP },
{ 0, 224,           0, "lime_shulker_box", NO_PROP },
{ 0, 225,           0, "pink_shulker_box", NO_PROP },
{ 0, 226,           0, "gray_shulker_box", NO_PROP },
{ 0, 227,           0, "light_gray_shulker_box", NO_PROP },
{ 0, 228,           0, "cyan_shulker_box", NO_PROP },
{ 0, 229,           0, "purple_shulker_box", NO_PROP },
{ 0, 230,           0, "blue_shulker_box", NO_PROP },
{ 0, 231,           0, "brown_shulker_box", NO_PROP },
{ 0, 232,           0, "green_shulker_box", NO_PROP },
{ 0, 233,           0, "red_shulker_box", NO_PROP },
{ 0, 234,           0, "black_shulker_box", NO_PROP },
{ 0, 235,           0, "white_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 236,           0, "orange_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 237,           0, "magenta_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 238,           0, "light_blue_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 239,           0, "yellow_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 240,           0, "lime_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 241,           0, "pink_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 242,           0, "gray_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 243,           0, "light_gray_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 244,           0, "cyan_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 245,           0, "purple_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 246,           0, "blue_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 247,           0, "brown_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 248,           0, "green_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 249,           0, "red_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 250,           0, "black_glazed_terracotta", SWNE_FACING_PROP },
{ 0, 251,          15, "black_concrete", NO_PROP },
{ 0, 251,          14, "red_concrete", NO_PROP },
{ 0, 251,          13, "green_concrete", NO_PROP },
{ 0, 251,          12, "brown_concrete", NO_PROP },
{ 0, 251,          11, "blue_concrete", NO_PROP },
{ 0, 251,          10, "purple_concrete", NO_PROP },
{ 0, 251,           9, "cyan_concrete", NO_PROP },
{ 0, 251,           8, "light_gray_concrete", NO_PROP },
{ 0, 251,           7, "gray_concrete", NO_PROP },
{ 0, 251,           6, "pink_concrete", NO_PROP },
{ 0, 251,           5, "lime_concrete", NO_PROP },
{ 0, 251,           4, "yellow_concrete", NO_PROP },
{ 0, 251,           3, "light_blue_concrete", NO_PROP },
{ 0, 251,           2, "magenta_concrete", NO_PROP },
{ 0, 251,           1, "orange_concrete", NO_PROP },
{ 0, 251,           0, "white_concrete", NO_PROP },
{ 0, 252,          15, "black_concrete_powder", NO_PROP },
{ 0, 252,          14, "red_concrete_powder", NO_PROP },
{ 0, 252,          13, "green_concrete_powder", NO_PROP },
{ 0, 252,          12, "brown_concrete_powder", NO_PROP },
{ 0, 252,          11, "blue_concrete_powder", NO_PROP },
{ 0, 252,          10, "purple_concrete_powder", NO_PROP },
{ 0, 252,           9, "cyan_concrete_powder", NO_PROP },
{ 0, 252,           8, "light_gray_concrete_powder", NO_PROP },
{ 0, 252,           7, "gray_concrete_powder", NO_PROP },
{ 0, 252,           6, "pink_concrete_powder", NO_PROP },
{ 0, 252,           5, "lime_concrete_powder", NO_PROP },
{ 0, 252,           4, "yellow_concrete_powder", NO_PROP },
{ 0, 252,           3, "light_blue_concrete_powder", NO_PROP },
{ 0, 252,           2, "magenta_concrete_powder", NO_PROP },
{ 0, 252,           1, "orange_concrete_powder", NO_PROP },
{ 0, 252,           0, "white_concrete_powder", NO_PROP },
{ 0,  34,           0, "piston_head", PISTON_HEAD_PROP },
{ 0,  34,           0, "moving_piston", PISTON_HEAD_PROP },	// not 100% sure that's what this is...
{ 0,  40,           0, "red_mushroom", NO_PROP },
{ 0,  80,           0, "snow_block", NO_PROP },
{ 0, 105,           7, "attached_melon_stem", NO_PROP },	// see pumpking stem - could someday know which way to go from the facing prop
{ 0, 105,           0, "melon_stem", AGE_PROP },
{ 0, 117,           0, "brewing_stand", NO_PROP },	// see has_bottle_0
{ 0, 119,           0, "end_portal", NO_PROP },
{ 0, 140,                        0, "flower_pot", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 0, "potted_oak_sapling", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 1, "potted_spruce_sapling", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 2, "potted_birch_sapling", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 3, "potted_jungle_sapling", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 4, "potted_acacia_sapling", NO_PROP },
{ 0, 140,        SAPLING_FIELD | 5, "potted_dark_oak_sapling", NO_PROP },
{ 0, 140,      TALLGRASS_FIELD | 2, "potted_fern", NO_PROP },
{ 0, 140,  YELLOW_FLOWER_FIELD | 0, "potted_dandelion", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 0, "potted_poppy", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 1, "potted_blue_orchid", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 2, "potted_allium", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 3, "potted_azure_bluet", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 4, "potted_red_tulip", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 5, "potted_orange_tulip", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 6, "potted_white_tulip", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 7, "potted_pink_tulip", NO_PROP },
{ 0, 140,     RED_FLOWER_FIELD | 8, "potted_oxeye_daisy", NO_PROP },
{ 0, 140,   RED_MUSHROOM_FIELD | 0, "potted_red_mushroom", NO_PROP },
{ 0, 140, BROWN_MUSHROOM_FIELD | 0, "potted_brown_mushroom", NO_PROP },
{ 0, 140,       DEADBUSH_FIELD | 0, "potted_dead_bush", NO_PROP },
{ 0, 140,         CACTUS_FIELD | 0, "potted_cactus", NO_PROP },
{ 0, 144,           0, "skeleton_wall_skull", HEAD_WALL_PROP },
{ 0, 144,           0, "skeleton_skull", HEAD_PROP },
{ 0, 144,        1<<4, "wither_skeleton_wall_skull", HEAD_WALL_PROP },
{ 0, 144,        1<<4, "wither_skeleton_skull", HEAD_PROP },
{ 0, 144,        2<<4, "zombie_wall_head", HEAD_WALL_PROP },
{ 0, 144,        2<<4, "zombie_head", HEAD_PROP },
{ 0, 144,        3<<4, "player_wall_head", HEAD_WALL_PROP },
{ 0, 144,        3<<4, "player_head", HEAD_PROP },
{ 0, 144,        4<<4, "creeper_wall_head", HEAD_WALL_PROP },
{ 0, 144,        4<<4, "creeper_head", HEAD_PROP },
{ 0, 144,        5<<4, "dragon_wall_head", HEAD_WALL_PROP },
{ 0, 144,        5<<4, "dragon_head", HEAD_PROP },
{ 0, 209,           0, "end_gateway", NO_PROP },
{ 0, 217,           0, "structure_void", NO_PROP },
{ 0, 255,           0, "structure_block", STRUCTURE_PROP },
// new 1.13 on down
{ 0,   0,           0, "void_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.gamepedia.com/Air
{ 0,   0,           0, "cave_air", NO_PROP },	// consider these air until proven otherwise https://minecraft.gamepedia.com/Air
{ 0, 205,           2, "prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 2, just to be safe (see purpur_slab)
{ 0, 205,           3, "prismarine_brick_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 3
{ 0, 205,           4, "dark_prismarine_slab", SLAB_PROP }, // added to purpur slab and double slab, dataVal 4
{ 0,   1,    HIGH_BIT, "prismarine_stairs", STAIRS_PROP },
{ 0,   2,    HIGH_BIT, "prismarine_brick_stairs", STAIRS_PROP },
{ 0,   3,    HIGH_BIT, "dark_prismarine_stairs", STAIRS_PROP },
{ 0,   4,    HIGH_BIT, "spruce_trapdoor", TRAPDOOR_PROP },
{ 0,   5,    HIGH_BIT, "birch_trapdoor", TRAPDOOR_PROP },
{ 0,   6,    HIGH_BIT, "jungle_trapdoor", TRAPDOOR_PROP },
{ 0,   7,    HIGH_BIT, "acacia_trapdoor", TRAPDOOR_PROP },
{ 0,   8,    HIGH_BIT, "dark_oak_trapdoor", TRAPDOOR_PROP },
{ 0,   9,    HIGH_BIT, "spruce_button", BUTTON_PROP },	// TODO++
{ 0,  10,    HIGH_BIT, "birch_button", BUTTON_PROP },
{ 0,  11,    HIGH_BIT, "jungle_button", BUTTON_PROP },
{ 0,  12,    HIGH_BIT, "acacia_button", BUTTON_PROP },
{ 0,  13,    HIGH_BIT, "dark_oak_button", BUTTON_PROP },
{ 0,  14,    HIGH_BIT, "spruce_pressure_plate", PRESSURE_PROP }, // could stuff these into material bits, but KISS
{ 0,  15,    HIGH_BIT, "birch_pressure_plate", PRESSURE_PROP },
{ 0,  16,    HIGH_BIT, "jungle_pressure_plate", PRESSURE_PROP },
{ 0,  17,    HIGH_BIT, "acacia_pressure_plate", PRESSURE_PROP },
{ 0,  18,    HIGH_BIT, "dark_oak_pressure_plate", PRESSURE_PROP },
{ 0,  19,  HIGH_BIT|0, "stripped_oak_log", AXIS_PROP },
{ 0,  19,  HIGH_BIT|1, "stripped_spruce_log", AXIS_PROP },
{ 0,  19,  HIGH_BIT|2, "stripped_birch_log", AXIS_PROP },
{ 0,  19,  HIGH_BIT|3, "stripped_jungle_log", AXIS_PROP },
{ 0,  20,  HIGH_BIT|0, "stripped_acacia_log", AXIS_PROP },
{ 0,  20,  HIGH_BIT|1, "stripped_dark_oak_log", AXIS_PROP },
{ 0,  21,  HIGH_BIT|0, "stripped_oak_wood", AXIS_PROP },
{ 0,  21,  HIGH_BIT|1, "stripped_spruce_wood", AXIS_PROP },
{ 0,  21,  HIGH_BIT|2, "stripped_birch_wood", AXIS_PROP },
{ 0,  21,  HIGH_BIT|3, "stripped_jungle_wood", AXIS_PROP },
{ 0,  22,  HIGH_BIT|0, "stripped_acacia_wood", AXIS_PROP },
{ 0,  22,  HIGH_BIT|1, "stripped_dark_oak_wood", AXIS_PROP },
{ 0, 176,           0, "white_banner", STANDING_SIGN_PROP },
{ 0,  23,    HIGH_BIT, "orange_banner", STANDING_SIGN_PROP },
{ 0,  24,    HIGH_BIT, "magenta_banner", STANDING_SIGN_PROP },
{ 0,  25,    HIGH_BIT, "light_blue_banner", STANDING_SIGN_PROP },
{ 0,  26,    HIGH_BIT, "yellow_banner", STANDING_SIGN_PROP },
{ 0,  27,    HIGH_BIT, "lime_banner", STANDING_SIGN_PROP },
{ 0,  28,    HIGH_BIT, "pink_banner", STANDING_SIGN_PROP },
{ 0,  29,    HIGH_BIT, "gray_banner", STANDING_SIGN_PROP },
{ 0,  30,    HIGH_BIT, "light_gray_banner", STANDING_SIGN_PROP },
{ 0,  31,    HIGH_BIT, "cyan_banner", STANDING_SIGN_PROP },
{ 0,  32,    HIGH_BIT, "purple_banner", STANDING_SIGN_PROP },
{ 0,  33,    HIGH_BIT, "blue_banner", STANDING_SIGN_PROP },
{ 0,  34,    HIGH_BIT, "brown_banner", STANDING_SIGN_PROP },
{ 0,  35,    HIGH_BIT, "green_banner", STANDING_SIGN_PROP },
{ 0,  36,    HIGH_BIT, "red_banner", STANDING_SIGN_PROP },
{ 0,  37,    HIGH_BIT, "black_banner", STANDING_SIGN_PROP }, // TODO++ colors need to be added
{ 0, 177,           0, "white_wall_banner", FACING_PROP },
{ 0,  38,    HIGH_BIT, "orange_wall_banner", FACING_PROP },
{ 0,  39,    HIGH_BIT, "magenta_wall_banner", FACING_PROP },
{ 0,  40,    HIGH_BIT, "light_blue_wall_banner", FACING_PROP },
{ 0,  41,    HIGH_BIT, "yellow_wall_banner", FACING_PROP },
{ 0,  42,    HIGH_BIT, "lime_wall_banner", FACING_PROP },
{ 0,  43,    HIGH_BIT, "pink_wall_banner", FACING_PROP },
{ 0,  44,    HIGH_BIT, "gray_wall_banner", FACING_PROP },
{ 0,  45,    HIGH_BIT, "light_gray_wall_banner", FACING_PROP },
{ 0,  46,    HIGH_BIT, "cyan_wall_banner", FACING_PROP },
{ 0,  47,    HIGH_BIT, "purple_wall_banner", FACING_PROP },
{ 0,  48,    HIGH_BIT, "blue_wall_banner", FACING_PROP },
{ 0,  49,    HIGH_BIT, "brown_wall_banner", FACING_PROP },
{ 0,  50,    HIGH_BIT, "green_wall_banner", FACING_PROP },
{ 0,  51,    HIGH_BIT, "red_wall_banner", FACING_PROP },
{ 0,  52,    HIGH_BIT, "black_wall_banner", FACING_PROP },
{ 0,  53,    HIGH_BIT, "tall_seagrass", TALL_FLOWER_PROP },
{ 0,  54,    HIGH_BIT, "seagrass", NO_PROP },
{ 0,  55,  HIGH_BIT|0, "smooth_stone", NO_PROP },
{ 0,  55,  HIGH_BIT|1, "smooth_sandstone", NO_PROP },
{ 0,  55,  HIGH_BIT|2, "smooth_red_sandstone", NO_PROP },
{ 0,  55,  HIGH_BIT|3, "smooth_quartz", NO_PROP },
{ 0,  56,    HIGH_BIT, "blue_ice", NO_PROP },
{ 0,  57,    HIGH_BIT, "dried_kelp_block", NO_PROP },
{ 0,  58,  HIGH_BIT|0, "kelp_plant", TRULY_NO_PROP }, // the lower part
{ 0,  58,  HIGH_BIT|1, "kelp", TRULY_NO_PROP }, // the top, growing part; don't care about the age
{ 0,   9,           8, "bubble_column", 0x0 },	// consider as full block of water for now, need to investigate if there's anything to static render (I don't think so...?)
{ 0,  59,  HIGH_BIT|0, "tube_coral_block", NO_PROP },
{ 0,  59,  HIGH_BIT|1, "brain_coral_block", NO_PROP },
{ 0,  59,  HIGH_BIT|2, "bubble_coral_block", NO_PROP },
{ 0,  59,  HIGH_BIT|3, "fire_coral_block", NO_PROP },
{ 0,  59,  HIGH_BIT|4, "horn_coral_block", NO_PROP },
{ 0,  60,  HIGH_BIT|0, "dead_tube_coral_block", NO_PROP },
{ 0,  60,  HIGH_BIT|1, "dead_brain_coral_block", NO_PROP },
{ 0,  60,  HIGH_BIT|2, "dead_bubble_coral_block", NO_PROP },
{ 0,  60,  HIGH_BIT|3, "dead_fire_coral_block", NO_PROP },
{ 0,  60,  HIGH_BIT|4, "dead_horn_coral_block", NO_PROP },
{ 0,  61,  HIGH_BIT|0, "tube_coral", NO_PROP },
{ 0,  61,  HIGH_BIT|1, "brain_coral", NO_PROP },
{ 0,  61,  HIGH_BIT|2, "bubble_coral", NO_PROP },
{ 0,  61,  HIGH_BIT|3, "fire_coral", NO_PROP },
{ 0,  61,  HIGH_BIT|4, "horn_coral", NO_PROP },
{ 0,  62,  HIGH_BIT|0, "tube_coral_fan", WATERLOGGED_PROP },	// here's where we go nuts: using 7 bits (one waterlogged)
{ 0,  62,  HIGH_BIT|1, "brain_coral_fan", WATERLOGGED_PROP },
{ 0,  62,  HIGH_BIT|2, "bubble_coral_fan", WATERLOGGED_PROP },
{ 0,  62,  HIGH_BIT|3, "fire_coral_fan", WATERLOGGED_PROP },
{ 0,  62,  HIGH_BIT|4, "horn_coral_fan", WATERLOGGED_PROP },
{ 0,  63,  HIGH_BIT|0, "dead_tube_coral_fan", WATERLOGGED_PROP },
{ 0,  63,  HIGH_BIT|1, "dead_brain_coral_fan", WATERLOGGED_PROP },
{ 0,  63,  HIGH_BIT|2, "dead_bubble_coral_fan", WATERLOGGED_PROP },
{ 0,  63,  HIGH_BIT|3, "dead_fire_coral_fan", WATERLOGGED_PROP },
{ 0,  63,  HIGH_BIT|4, "dead_horn_coral_fan", WATERLOGGED_PROP },
{ 0,  64,  HIGH_BIT|0, "tube_coral_wall_fan", FAN_PROP },
{ 0,  64,  HIGH_BIT|1, "brain_coral_wall_fan", FAN_PROP },
{ 0,  64,  HIGH_BIT|2, "bubble_coral_wall_fan", FAN_PROP },
{ 0,  64,  HIGH_BIT|3, "fire_coral_wall_fan", FAN_PROP },
{ 0,  64,  HIGH_BIT|4, "horn_coral_wall_fan", FAN_PROP },
{ 0,  65,  HIGH_BIT|0, "dead_tube_coral_wall_fan", FAN_PROP },
{ 0,  65,  HIGH_BIT|1, "dead_brain_coral_wall_fan", FAN_PROP },
{ 0,  65,  HIGH_BIT|2, "dead_bubble_coral_wall_fan", FAN_PROP },
{ 0,  65,  HIGH_BIT|3, "dead_fire_coral_wall_fan", FAN_PROP },
{ 0,  65,  HIGH_BIT|4, "dead_horn_coral_wall_fan", FAN_PROP },
{ 0,  66,    HIGH_BIT, "conduit", NO_PROP },
{ 0,  67,    HIGH_BIT, "sea_pickle", PICKLE_PROP },
{ 0,  68,    HIGH_BIT, "turtle_egg", EGG_PROP },
{ 0,  26,           0, "black_bed", BED_PROP }, // TODO+ bed colors should have separate blocks
{ 0,  26,           0, "red_bed", BED_PROP },
{ 0,  26,           0, "green_bed", BED_PROP },
{ 0,  26,           0, "brown_bed", BED_PROP },
{ 0,  26,           0, "blue_bed", BED_PROP },
{ 0,  26,           0, "purple_bed", BED_PROP },
{ 0,  26,           0, "cyan_bed", BED_PROP },
{ 0,  26,           0, "light_gray_bed", BED_PROP },
{ 0,  26,           0, "gray_bed", BED_PROP },
{ 0,  26,           0, "pink_bed", BED_PROP },
{ 0,  26,           0, "lime_bed", BED_PROP },
{ 0,  26,           0, "yellow_bed", BED_PROP },
{ 0,  26,           0, "light_blue_bed", BED_PROP },
{ 0,  26,           0, "magenta_bed", BED_PROP },
{ 0,  26,           0, "orange_bed", BED_PROP },
{ 0,  26,           0, "white_bed", BED_PROP },
};

#define HASH_SIZE 512
#define HASH_MASK 0x1ff

int HashLists[HASH_SIZE+NUM_TRANS];
int * HashArray[HASH_SIZE];


int computeHash(char *name)
{
	int hashVal = 0;
	while (*name) {
		hashVal += (int)*name++;
	}
	return hashVal;
}

void makeHashTable()
{
	// go through entries and convert to hashes, note how many hashes per array spot
	int hashPerIndex[HASH_SIZE];
	memset(hashPerIndex, 0, 4 * HASH_SIZE);
	int hashStart[HASH_SIZE];
	int i;
	// If we really wanted to super-optimize, we could take the "air" entry out of the hash table,
	// since we special-case that in findIndexFromName. Let's not...
	for (i = 0; i < NUM_TRANS; i++) {
		BlockTranslations[i].hashSum = computeHash(BlockTranslations[i].name);
		hashPerIndex[BlockTranslations[i].hashSum & HASH_MASK]++;
	}
	// OK, have how many per index, so offset appropriately
	int offset = 0;
	for (i = 0; i < HASH_SIZE; i++) {
		hashStart[i] = offset;
		HashArray[i] = &HashLists[offset];
		offset += hashPerIndex[i] + 1;
	}
	if (offset != HASH_SIZE + NUM_TRANS) {
		// this is an error
		return;
	}
	// now populate the hashLists, ending with -1; first just set all to -1
	for (i = 0; i < HASH_SIZE + NUM_TRANS; i++) {
		HashLists[i] = -1;
	}
	for (i = 0; i < NUM_TRANS; i++) {
		int index = BlockTranslations[i].hashSum & HASH_MASK;
		HashLists[hashStart[index]++] = i;
	}
}

int findIndexFromName(char *name)
{
	// quick out: if it's air, just be done
	if (strcmp("air", name) == 0) {
		return 0;
	}
	// get hash number
	int hashNum = computeHash(name);
	int *hl = HashArray[hashNum & HASH_MASK];

	// now compare entries one by one until a match is found
	while (*hl > -1) {
		if (hashNum == BlockTranslations[*hl].hashSum) {
			// OK, do full string compare
			if (strcmp(name, BlockTranslations[*hl].name) == 0) {
				return *hl;
			}
		}
		hl++;
	}
	// fail!
	return -1;
}

// return -1 if error (corrupt file)
int bfread(bfFile bf, void *target, int len)
{
    if (bf.type == BF_BUFFER) {
        memcpy(target, bf.buf + *bf.offset, len);
        *bf.offset += len;
    } else if (bf.type == BF_GZIP) {
        return gzread(bf.gz, target, len);
    }
    return len;
}

// positive number returned is offset, 0 means no movement, -1 means error (corrupt file)
int bfseek(bfFile bf, int offset, int whence)
{
    if (bf.type == BF_BUFFER) {
        if (whence == SEEK_CUR)
            *bf.offset += offset;
        else if (whence == SEEK_SET)
            *bf.offset = offset;
    } else if (bf.type == BF_GZIP) {
        return gzseek(bf.gz, offset, whence);
    }
    return offset;
} 

bfFile newNBT(const wchar_t *filename, int *err)
{
    bfFile ret;
    FILE *fptr;
    ret.type = BF_GZIP;

    *err = _wfopen_s(&fptr,filename, L"rb");
    if (fptr == NULL || *err != 0)
    {
        ret.gz = 0x0;
        return ret;
    }

    ret.gz = gzdopen(_fileno(fptr),"rb");
    ret._offset = 0;
    ret.offset = &ret._offset;
    return ret;
}

static unsigned short readWord(bfFile bf)
{
    unsigned char buf[2];
    bfread(bf, buf, 2);
    return (buf[0]<<8)|buf[1];
}
static unsigned int readDword(bfFile bf)
{
	unsigned char buf[4];
	bfread(bf, buf, 4);
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}
//static unsigned long long readLong(bfFile bf)
//{
//    int i;
//    union {
//        double f;
//        unsigned long long l;
//    } fl;
//    unsigned char buf[8];
//    bfread(bf,buf,8);
//    fl.l=0;
//    for (i=0;i<8;i++)
//    {
//        fl.l<<=8;
//        fl.l|=buf[i];
//    }
//    return fl.l;
//}
static double readDouble(bfFile bf)
{
    int i;
    union {
        double f;
        unsigned long long l;
    } fl;
    unsigned char buf[8];
    bfread(bf,buf,8);
    fl.l=0;
    for (i=0;i<8;i++)
    {
        fl.l<<=8;
        fl.l|=buf[i];
    }
    return fl.f;
}
static int skipType(bfFile bf, int type)
{
    int len;
    switch (type)
    {
    default:
        // unknown type! That's bad.
        break;
    case 0:
        // perfectly reasonable case: type 0 is end
        return 0;
    case 1: //byte
        return bfseek(bf,1,SEEK_CUR);
    case 2: //short
        return bfseek(bf, 2, SEEK_CUR);
    case 3: //int
        return bfseek(bf, 4, SEEK_CUR);
    case 4: //long
        return bfseek(bf, 8, SEEK_CUR);
    case 5: //float
        return bfseek(bf, 4, SEEK_CUR);
    case 6: //double
        return bfseek(bf, 8, SEEK_CUR);
    case 7: //byte array
        len=readDword(bf);
        return bfseek(bf, len, SEEK_CUR);
    case 8: //string
        len=readWord(bf);
        return bfseek(bf, len, SEEK_CUR);
    case 9: //list
        return skipList(bf);
    case 10: //compound
        return skipCompound(bf);
    case 11: //int array
        len=readDword(bf);
        return bfseek(bf,len*4,SEEK_CUR);
	case 12: //long int array
		len = readDword(bf);
		return bfseek(bf, len * 8, SEEK_CUR);
	}
    // reach here only for unknown type, in which case we can't continue
    return -1;
}
static int skipList(bfFile bf)
{
    int len,i;
    unsigned char type;
    if (bfread(bf, &type, 1) < 0) return -1;
    len=readDword(bf);
    int retcode = 1;
    switch (type)
    {
    default:
        return 0;
    case 1: //byte
        return bfseek(bf,len,SEEK_CUR);
    case 2: //short
        return bfseek(bf,len*2,SEEK_CUR);
    case 3: //int
        return bfseek(bf,len*4,SEEK_CUR);
    case 4: //long
        return bfseek(bf,len*8,SEEK_CUR);
    case 5: //float
        return bfseek(bf,len*4,SEEK_CUR);
    case 6: //double
        return bfseek(bf, len * 8, SEEK_CUR);
    case 7: //byte array
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readDword(bf);
            retcode = bfseek(bf, slen, SEEK_CUR);
        }
        return retcode;
    case 8: //string
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readWord(bf);
            retcode = bfseek(bf, slen, SEEK_CUR);
        }
        return retcode;
    case 9: //list
        for (i = 0; (i<len) && (retcode>=0); i++)
            retcode = skipList(bf);
        return retcode;
    case 10: //compound
        for (i = 0; (i<len) && (retcode>=0); i++)
            retcode = skipCompound(bf);
        return retcode;
    case 11: //int array
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readDword(bf);
            retcode = bfseek(bf, slen * 4, SEEK_CUR);
        }
        return retcode;
    }
}
static int skipCompound(bfFile bf)
{
    int len;
    int retcode = 0;
    unsigned char type=0;
    do {
        if (bfread(bf, &type, 1) < 0) return -1;
        if (type)
        {
            len=readWord(bf);
            if (bfseek(bf, len, SEEK_CUR) < 0) return -1;	//skip name
            retcode = skipType(bf, type);
        }
    } while (type && (retcode>=0));
    return retcode;
}

// 1 they match, 0 they don't, -1 there was a read error
static int compare(bfFile bf, char *name)
{
    int ret=0;
    int len=readWord(bf);
    char thisName[MAX_NAME_LENGTH];
    if (bfread(bf, thisName, len) < 0)
        return -1;
    thisName[len]=0;
    if (strcmp(thisName,name)==0)
        ret=1;
    return ret;
}

// this finds an element in a composite list.
// it works progressively, so it only finds elements it hasn't come
// across yet.
static int nbtFindElement(bfFile bf, char *name)
{
    for (;;)
    {
        unsigned char type = 0;
        if (bfread(bf, &type, 1) < 0) return 0;
        if (type == 0) return 0;
        int retcode = compare(bf, name);
        if (retcode < 0)
            // failed to read file, so abort
            return 0;
        if (retcode > 0 )
            // found type
            return type;
        retcode = skipType(bf, type);
        if (retcode < 0) {
            // error! corrupt file found
            return -1;
        }
    }
}

// use only least significant half-byte of location, since we know what block we're in
unsigned char mod16(int val)
{
    return (unsigned char)(val & 0xf);
}

// return 0 on error
int nbtGetBlocks(bfFile bf, unsigned char *buff, unsigned char *data, unsigned char *blockLight, unsigned char *biome, BlockEntity *entities, int *numEntities, int *mcversion)
{
    int len,nsections;
    int biome_save;
    //int found;

    //Level/Blocks
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Level")!=10)
        return -3;

    // For some reason, on most maps the biome info is before the Sections;
    // on others they're after. So, read biome data, then rewind to find Sections.
    // Format info at http://wiki.vg/Map_Format, though don't trust order.
    biome_save = *bf.offset;
    memset(biome, 0, 16*16);
	int inttype = nbtFindElement(bf, "Biomes");
	bool newFormat = false;
	*mcversion = 12;	// as a guess - not accurate
	if (inttype != 7) {
		// could be new format 1.13
		if (inttype != 11)
			return -4;
		else {
			newFormat = true;
			*mcversion = 13;
			if (!hashMade) {
				makeHashTable();
				hashMade = true;
			}
		}
	}

    len=readDword(bf); //array length
	if (!newFormat) {
		if (bfread(bf, biome, len) < 0) return -5;
	} else {
		// new format 1.13
		unsigned char biomeint[4 * 16 * 16];
		memset(biomeint, 0, 4 * 16 * 16);
		if (bfread(bf, biomeint, 4*len) < 0) return -5;
		// convert to bytes, tough luck if too high (for now)
		for (int i = 0; i < 256; i++) {
			// wild guess as to the biome - looks like the topmost byte right now. TODO
			int grab = 4 * i + 3;
			biome[i] = (biomeint[grab] > 255) ? 255 : (unsigned char)biomeint[grab];
		}
	}

    if (bfseek(bf, biome_save, SEEK_SET) < 0) return -6; //rewind to start of section

    if (nbtFindElement(bf,"Sections")!= 9)
        return -7;

	// does Sections have anything inside of it?
	{
		// get rid of "\n" after "Sections".
		unsigned char uctype=0;
        if (bfread(bf, &uctype, 1) < 0) return -8;
		if (uctype != 10)
            return -9;
    }

    memset(buff, 0, 16*16*256);
    memset(data, 0, 16*16*256);
    memset(blockLight, 0, 16*16*128);

    nsections=readDword(bf);
    if (nsections < 0) return -10;

    char thisName[MAX_NAME_LENGTH];

    // Number of block sections stacked one atop the other, ranging from 1-16; each is a Y slice,
	// each Y slice has 16 vertical layers to it. So each Y slice can have 16*16*16 values in it.
	// The minimum length of a block/data ID is 4 bits. A long integer is 64 bits, so can contain
	// 16 4-bit numbers.
	// With compression down to a minimum of 4 bits per block/data ID, this gives 256 longs,
	// or 8 * 256 bytes.
	// However, it's possible that we could have 256 or more blocks per slice, really, 16*16*16
	// different, unique blocks (remember that "data" matters, too, AFAIK). In such a case, the number
	// of entries in the palette could be 4096 entries, which need 12 bits per entry.
	// This means the array here should be 16*16*16*12/8 (the 8 is bits per byte) = 6144 bytes long.
	unsigned char bigbuff[6144];
	//memset(bigbuff, 0, 256 * 8);

	int ret;
	unsigned char type;
	// read all slices that exist for this vertical block and process each
    while (nsections--)
    {	
        unsigned char y;
        int save = *bf.offset;
        if (nbtFindElement(bf,"Y")!=1) //which section of the block stack is this?
            return 0;
        if (bfread(bf, &y, 1) < 0) return -11;
        if (bfseek(bf, save, SEEK_SET) < 0) return -12; //rewind to start of section

		if (!newFormat) {
			// read all the arrays in this section
			for (;;)
			{
				ret = 0;
				type = 0;
				if (bfread(bf, &type, 1) < 0) return -13;
				if (type == 0)
					break;
				len = readWord(bf);
				if (bfread(bf, thisName, len) < 0) return -14;
				thisName[len] = 0;
				if (strcmp(thisName, "BlockLight") == 0)
				{
					//found++;
					ret = 1;
					len = readDword(bf); //array length
					if (bfread(bf, blockLight + 16 * 16 * 8 * y, len) < 0) return -15;
				}
				else if (strcmp(thisName, "Blocks") == 0)
				{
					//found++;
					ret = 1;
					len = readDword(bf); //array length
					if (bfread(bf, buff + 16 * 16 * 16 * y, len) < 0) return -16;
				}
				else if (strcmp(thisName, "Data") == 0)
				{
					//found++;
					ret = 1;
					len = readDword(bf); //array length
					// transfer the data from 4-bits to 8-bits; needed for 1.13
					unsigned char data4buff[16 * 16 * 8];
					if (bfread(bf, data4buff, len) < 0) return -17;
					unsigned char *din = data4buff;
					unsigned char *dret = &data[16 * 16 * 16 * y];
					for (int id = 0; id < 16 * 16 * 8; id++) {
						// low, high 4-bits saved
						*dret++ = *din & 0xf;
						*dret++ = *din++ >> 4;
					}
				}
				if (!ret)
					if (skipType(bf, type) < 0) return -18;
			}
		}
		else {
			// read all the arrays in this section
			// walk through all elements of each Palette array element
			int dataVal = 0;
			// for doors
			bool half, north, south, east, west, up, down, lit, powered, triggered, extended, attached, disarmed, 
				conditional, inverted, enabled, doubleSlab, mode, waterlogged, in_wall;
			int axis, door_facing, hinge, open, face, rails, occupied, part, dropper_facing, eye, age, delay, sticky, hatch;
			// to avoid Release build warning =but should always be set by code in practice
			int typeIndex = 0;
			half = north = south = east = west = up = down = lit = powered = triggered = extended = attached = disarmed
				= conditional = inverted = enabled = doubleSlab = mode = waterlogged = in_wall = false;
			axis = door_facing = hinge = open = face = rails = occupied = part = dropper_facing = eye = age = delay = sticky = hatch = 0;

			int bigbufflen = 0;
			int entry_index = 0;
			// could theoretically get higher...
			unsigned char paletteBlockEntry[MAX_PALETTE];
			unsigned char paletteDataEntry[MAX_PALETTE];
			for (;;)
			{
				ret = 0;
				type = 0;
				if (bfread(bf, &type, 1) < 0) return -13;
				if (type == 0)
					break;
				len = readWord(bf);
				if (bfread(bf, thisName, len) < 0) return -14;
				thisName[len] = 0;
				if (strcmp(thisName, "BlockLight") == 0)
				{
					ret = 1;
					len = readDword(bf); //array length
					if (bfread(bf, blockLight + 16 * 16 * 8 * y, len) < 0) return -15;
				}
				else if (strcmp(thisName, "BlockStates") == 0)
				{
					ret = 1;
					bigbufflen = readDword(bf); //array length
					if (bigbufflen > 6144)
						return -19;	// TODO make better unique return codes, with names
					// read 8 byte records, so note len is adjusted here from longs (which are 8 bytes long) to the number of bytes to read.
					if (bfread(bf, bigbuff, bigbufflen * 8) < 0) return -16;
				}
				else if (strcmp(thisName, "Palette") == 0)
				{
					ret = 1;

					{
						// get rid of "\n" after "Palette".
						unsigned char uctype = 0;
						if (bfread(bf, &uctype, 1) < 0) return -8;
						if (uctype != 10)
							return -9;
					}

					// walk through entries' names and convert to their block ID
					int nentries = readDword(bf);
					if (nentries <= 0) return -10;	// TODO someday need to clean up these error codes and treat them right
					if (nentries > MAX_PALETTE) return -12;

					char thisBlockName[MAX_NAME_LENGTH];

					// go through entries in Palette
					while (nentries--) {
						// clear, so that NO_PROP doesn't inherit from other blocks, etc.
						dataVal = 0;

						for (;;)
						{
							type = 0;
							if (bfread(bf, &type, 1) < 0) return -13;
							// done walking through subarray?
							if (type == 0)
								break;
							len = readWord(bf);
							if (bfread(bf, thisBlockName, len) < 0) return -14;
							thisBlockName[len] = 0;

							if ( (type == 8) && (strcmp(thisBlockName, "Name") == 0)) {

								len = readWord(bf);
								if (len < MAX_NAME_LENGTH) {
									if (bfread(bf, thisBlockName, len) < 0) return -13;
								}
								else {
									return -14;
								}
								// have to add end of string
								thisBlockName[len] = 0x0;

								// convert name to block value. +10 is to avoid (useless) "minecraft:" string.
								// could be dangerous if len < 10 for whatever reason.
								typeIndex = findIndexFromName(thisBlockName + 10);
								if (typeIndex > -1) {
									paletteBlockEntry[entry_index] = BlockTranslations[typeIndex].blockId;
									paletteDataEntry[entry_index] = BlockTranslations[typeIndex].dataVal;
								} else {
									// unknown type - call it bedrock, by tradition
									paletteBlockEntry[entry_index] = 7;
									paletteDataEntry[entry_index] = 0;
								}
							}
							else if ((type == 10) && (strcmp(thisBlockName, "Properties") == 0)) {
								do {
									if (bfread(bf, &type, 1) < 0) return -1;
									if (type)
									{
										// read token value
										char token[100];
										char value[100];
										len = readWord(bf);
										if (bfread(bf, token, len) < 0) return -1;
										token[len] = 0;
										len = readWord(bf);
										if (bfread(bf, value, len) < 0) return -1;
										value[len] = 0;

										// TODO: I'm guessing that these zillion strcmps that follow are costing a lot of time.
										// Nicer would be to be able to save the token/values away in an array, then get the
										// name (which gets parsed after), then given the name just test the possible tokens needed.

										// ignore. Very common, for grassy blocks, so checked first
										if (strcmp(token, "snowy") == 0) {} // for grassy blocks, podzol, maybe more

										// interpret token value
										// wood axis, quartz block axis AXIS_PROP
										else if (strcmp(token, "axis") == 0) {
											if (strcmp(value, "y") == 0) {
												axis = 0;
											}
											else if (strcmp(value, "x") == 0) {
												axis = 4;
											}
											else if (strcmp(value, "z") == 0) {
												axis = 8;
											}
										}
										// FLUID_PROP
										else if (strcmp(token, "level") == 0) {
											dataVal = atoi(value);
										}
										// STANDING_SIGN_PROP
										else if (strcmp(token, "rotation") == 0) {
											dataVal = atoi(value);
										}
										// SAPLING_PROP
										else if (strcmp(token, "stage") == 0) {
											// 0 or 1, 1 is about to grow into a tree.
											dataVal = 8 * atoi(value);
										}
										// leaves LEAF_PROP
										// there does not seem to be any "check decay" flag 
										else if (strcmp(token, "persistent") == 0) {
											// if true, will decay; if false, will be checked for decay (what?)
											// https://minecraft.gamepedia.com/Leaves#Block_states
											// Instead, I'm guessing persistent means "no decay"
											// https://minecraft.gamepedia.com/Java_Edition_data_values#Leaves
											dataVal = (strcmp(value, "true") == 0) ? 4 : 0;
										}
										// SLAB_PROP
										else if (strcmp(token, "type") == 0) {
											// for mushroom blocks:
											if (strcmp(value, "top") == 0) {
												dataVal = 8;
												doubleSlab = false;
											}
											else if (strcmp(value, "bottom") == 0) {
												dataVal = 0;
												doubleSlab = false;
											}
											else if (strcmp(value, "double") == 0) {
												doubleSlab = true;
											}
											else if (strcmp(value, "sticky") == 0) {
												sticky = 8;
											}
											else if (strcmp(value, "normal") == 0) {
												sticky = false;
											}
											/* chest, but not needed
											else if (strcmp(value, "left") == 0) {
												// chest
												dataVal = 0;
											}
											else if (strcmp(value, "right") == 0) {
												// chest
												dataVal = 0;
											}
											*/
										}
										// SNOW_PROP
										else if (strcmp(token, "layers") == 0) {
											// 1-8, which turns into 0-7
											dataVal = atoi(value) - 1;
										}
										// frosted ice, crops, cocoa (which needs age separate)
										else if (strcmp(token, "age") == 0) {
											// AGE_PROP
											// 0-3
											age = dataVal = atoi(value);
										}
										else if (strcmp(token, "waterlogged") == 0) {
											waterlogged = (strcmp(value, "true") == 0) ? true : false;
											// TODO - https://minecraft.gamepedia.com/Waterlogging - giant pain, I think
										}
										// RAIL_PROP and STAIRS_PROP
										else if (strcmp(token, "shape") == 0) {
											// only matters for rails
											if (strcmp(value, "north_south") == 0) {
												rails = 0;
											}
											else if (strcmp(value, "east_west") == 0) {
												rails = 1;
											}
											else if (strcmp(value, "ascending_east") == 0) {
												rails = 2;
											}
											else if (strcmp(value, "ascending_west") == 0) {
												rails = 3;
											}
											else if (strcmp(value, "ascending_north") == 0) {
												rails = 4;
											}
											else if (strcmp(value, "ascending_south") == 0) {
												rails = 5;
											}
											else if (strcmp(value, "south_east") == 0) {
												rails = 6;
											}
											else if (strcmp(value, "south_west") == 0) {
												rails = 7;
											}
											else if (strcmp(value, "north_west") == 0) {
												rails = 8;
											}
											// really, it's the only thing left, so test is not needed.
											//else if (strcmp(value, "north_east") == 0) {
											else {
												rails = 9;
											}
										}
										// DOOR_PROP, STAIRS_PROP; for TORCH_PROP we set the data value directly
										else if (strcmp(token, "facing") == 0) {
											// door_facing starts at 1: south,west,north,east
											// stairs_facing starts at 0: east,west,south,north - dataVal minus one
											// dropper_facing starts at 0: down,up,north,south,west,east
											// dataVal starts at 1: east,west,south,north
											if (strcmp(value, "south") == 0) {
												door_facing = 1;
												//stairs_facing = 2;
												//chest_facing = 3; - same as 5-stairs_facing
												dropper_facing = 3;
												dataVal = 3;
											}
											else if (strcmp(value, "west") == 0) {
												door_facing = 2;
												//stairs_facing = 1;
												//chest_facing = 4;
												dropper_facing = 4;
												dataVal = 2;
											}
											else if (strcmp(value, "north") == 0) {
												door_facing = 3;
												//stairs_facing = 3;
												//chest_facing = 2;
												dropper_facing = 2;
												dataVal = 4;
											}
											else if (strcmp(value, "east") == 0) {
												door_facing = 0;
												//stairs_facing = 0;
												//chest_facing = 5;
												dropper_facing = 5;
												dataVal = 1;
											}
											// dispenser, dropper
											else if (strcmp(value, "up") == 0) {
												door_facing = 0;
												//chest_facing = 5;
												dropper_facing = 1;
												dataVal = 1;
											}
											else if (strcmp(value, "down") == 0) {
												door_facing = 0;
												//chest_facing = 5;
												dropper_facing = 0;
												dataVal = 1;
											}
										}
										else if (strcmp(token, "half") == 0) {
											// upper for sunflowers, top for stairs. Good job, guys.
											half = ((strcmp(value, "upper") == 0) || strcmp(value, "top") == 0) ? true : false;
											// for sunflowers
											dataVal = half ? 8 : 0;
										}
										// DOOR_PROP only
										else if (strcmp(token, "hinge") == 0) {
											// NOTE: this is flipped from what the docs at https://minecraft.gamepedia.com/Java_Edition_data_values#Door
											// say, they say left is 1, but this works properly. Mojang, I suspect, means the hinge on the inside of
											// the door, vs. outside, or something.
											hinge = (strcmp(value, "right") == 0) ? 1 : 0;
										}
										else if (strcmp(token, "open") == 0) {
											open = (strcmp(value, "true") == 0) ? 4 : 0;
										}
										else if (strcmp(token, "in_wall") == 0) {
											in_wall = (strcmp(value, "true") == 0);
										}
										// lever
										else if (strcmp(token, "face") == 0) {
											if (strcmp(value, "floor") == 0) {
												face = 0;
											}
											else if (strcmp(value, "wall") == 0) {
												face = 1;
											}
											else
												face = 2;
										}
										// also used by lever, powered rails
										else if (strcmp(token, "powered") == 0) {
											powered = (strcmp(value, "true") == 0);
										}
										// WIRE_PROP
										else if (strcmp(token, "power") == 0) {
											dataVal = atoi(value);
										}
										// BITES_PROP
										else if (strcmp(token, "bites") == 0) {
											dataVal = atoi(value);
										}
										// FARMLAND_PROP
										else if (strcmp(token, "moisture") == 0) {
											dataVal = atoi(value);
										}
										// PISTON_PROP and PISTON_HEAD_PROP
										else if (strcmp(token, "extended") == 0) {
											extended = (strcmp(value, "true") == 0);
										}
										// MUSHROOM_PROP and MUSHROOM_STEM_PROP
										else if (strcmp(token, "north") == 0) {
											north = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "south") == 0) {
											south = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "east") == 0) {
											east = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "west") == 0) {
											west = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "up") == 0) {
											up = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "down") == 0) {
											down = (strcmp(value, "true") == 0) ? 2 : 0;
										}
										// redstone
										else if (strcmp(token, "lit") == 0) {
											lit = (strcmp(value, "true") == 0);
										}
										// bed
										else if (strcmp(token, "occupied") == 0) {
											occupied = (strcmp(value, "true") == 0) ? 4 : 0;
										}
										else if (strcmp(token, "part") == 0) {
											part = (strcmp(value, "head") == 0) ? 8 : 0;
										}
										// dropper/dispenser
										else if (strcmp(token, "triggered") == 0) {
											triggered = (strcmp(value, "true") == 0);
										}
										// END_PORTAL_PROP (really, the frame)
										else if (strcmp(token, "eye") == 0) {
											eye = (strcmp(value, "true") == 0) ? 4 : 0;
										}
										// DAYLIGHT_PROP
										else if (strcmp(token, "inverted") == 0) {
											inverted = (strcmp(value, "true") == 0);
										}
										// REPEATER_PROP
										else if (strcmp(token, "delay") == 0) {
											// 1-4
											delay = atoi(value) - 1;
										}
										// HOPPER_PROP
										else if (strcmp(token, "enabled") == 0) {
											enabled = (strcmp(value, "true") == 0);
										}
										// TRIPWIRE_PROP
										else if (strcmp(token, "disarmed") == 0) {
											attached = (strcmp(value, "true") == 0);
										}
										// TRIPWIRE_PROP and TRIPWIRE_HOOK_PROP
										else if (strcmp(token, "attached") == 0) {
											attached = (strcmp(value, "true") == 0);
										}
										// COMMAND_BLOCK_PROP
										else if (strcmp(token, "conditional") == 0) {
											conditional = (strcmp(value, "true") == 0);
										}
										// brewing stand - just happens, no prop
										else if (strcmp(token, "has_bottle_0") == 0) {
											dataVal |= (strcmp(value, "true") == 0) ? 1 : 0;
										}
										else if (strcmp(token, "has_bottle_1") == 0) {
											dataVal |= (strcmp(value, "true") == 0) ? 2 : 0;
										}
										else if (strcmp(token, "has_bottle_2") == 0) {
											dataVal |= (strcmp(value, "true") == 0) ? 4 : 0;
										}
										// STRUCTURE_PROP
										else if (strcmp(token, "mode") == 0) {
											// only matters for rails
											if (strcmp(value, "data") == 0) {
												dataVal = 1;
											}
											else if (strcmp(value, "save") == 0) {
												dataVal = 2;
											}
											else if (strcmp(value, "load") == 0) {
												dataVal = 3;
											}
											else if (strcmp(value, "corner") == 0) {
												dataVal = 4;
											}
											// comparator
											else if (strcmp(value, "subtract") == 0) {
												mode = true;
											}
											else {
												mode = false;
											}
										}
										else if (strcmp(token, "pickles") == 0) {
											dataVal = atoi(value) - 1;
										}
										else if (strcmp(token, "eggs") == 0) {
											dataVal = atoi(value) - 1;
										}
										else if (strcmp(token, "hatch") == 0) {
											hatch = atoi(value);
										}

#ifdef _DEBUG
										else {
											// ignore, not used by Mineways
											if (strcmp(token, "distance") == 0) {} // for leaves, see https://minecraft.gamepedia.com/Leaves
											else if (strcmp(token, "short") == 0) {} // for piston, TODO - what makes this property be true?
											else if (strcmp(token, "locked") == 0) {} // for repeater, ignore
											else {
												// unknown property, let's show it: put a break here and
												// put text in "Actions":
												// token is {token} and value is {value}
												token[0] = token[0];	// here for debug
												value[0] = value[0];	// here for debug
											}
										}
#endif
									}
								} while (type);
							}
							else if (skipType(bf, type) < 0)
								return -18;
						}
						// done, so determine and fold in dataVal
						int tf = BlockTranslations[typeIndex].translateFlags;
						switch ( tf ) {
						case SLAB_PROP:
							// everything is fine if double is false
							if (doubleSlab)	{
								// turn single slabs into double slabs
								paletteBlockEntry[entry_index]--;
							}
							break;
						case AXIS_PROP:
							// will get OR'ed in with type of block later
							dataVal = axis;
							break;
						case TORCH_PROP:
							// if dataVal is not set, i.e. is 0, then set to 5
							if (dataVal == 0)
								dataVal = 5;
							// if this is a redstone torch, use the "lit" property to decide which block
							if (!lit && (paletteBlockEntry[entry_index] == 76)) {
								// turn it off
								paletteBlockEntry[entry_index] = 75;
							}
							break;
						case STAIRS_PROP:
							dataVal = (dataVal-1) | (half ? 4 : 0);
							break;
						case RAIL_PROP:
							dataVal = rails;
							if (!(paletteBlockEntry[entry_index] == 66)) {
								dataVal |= (powered ? 8 : 0);
							}
							break;
						case PRESSURE_PROP:
							dataVal = powered ? 1 : 0;
							break;
						case DOOR_PROP:
							// if upper door, use hinge and powered, else use facing and open
							if (half) {
								// upper
								dataVal = 8 | hinge | (powered ? 2 : 0);
							}
							else {
								// lower
								dataVal = open | door_facing;
							}
							break;
						case LEVER_PROP:
							// which way is face?
							if (face == 0) {
								// floor, can only be 5 or 6, test south or north;
								// but, may need to set dataVal properly.
								// We don't have the full flexibility of 1.13,
								// but rather use 1.12 rules here, that things go
								// south when off, north when on, in order to get
								// south and north orientations.
								switch ((dataVal - 1) + (powered ? 4 : 0)) {
								default:
								case 0: // east
								case 5: // west
									dataVal = 6 | 8;
									break;
								case 1: // west
								case 4: // east
									dataVal = 6;
									break;
								case 2: // south
								case 7: // north
									dataVal = 5 | 8;
									break;
								case 3: // north
								case 6: // south
									dataVal = 5;
									break;
								}
							}
							else if (face == 1) {
								// side
								// surprisingly simple, see switch code that follows.
								dataVal |= (powered ? 8 : 0);
								/*
								switch (stairs_facing) {
								default:
								case 0: // east
								dataVal |= 1;
								break;
								case 1: // west
								dataVal |= 2;
								break;
								case 2: // south
								dataVal |= 3;
								break;
								case 3: // north
								dataVal |= 4;
								break;
								}
								*/
							}
							else {
								// ceiling, can only be 0 or 7
								switch ((dataVal - 1) + (powered ? 4 : 0)) {
								default:
								case 0: // east
								case 5: // west
									dataVal = 0 | 8;
									break;
								case 1: // west
								case 4: // east
									dataVal = 0;
									break;
								case 2: // south
								case 7: // north
									dataVal = 7 | 8;
									break;
								case 3: // north
								case 6: // south
									dataVal = 7;
									break;
								}
							}
							break;
						case CHEST_PROP:
							dataVal = 6 - dataVal;
							// TODO: should export left/right as 2 higher bits. Good times.
							break;
						case FACING_PROP:
							dataVal = 6 - dataVal;
							break;
						case FURNACE_PROP:
							dataVal = 6 - dataVal;
							if (lit) {
								// light furnace
								paletteBlockEntry[entry_index] = 62;
							}
							break;
						case BUTTON_PROP:
							// dataVal is set fron "facing" already, just need face & powered
							if (face == 0) {
								dataVal = 5;
							}
							else if (face == 2) {
								dataVal = 0;
							}
							//else if (face == 1) {
							// not needed, as dataVal should be set just right at this point for walls
							dataVal |= powered ? 8 : 0;
							break;
						case TRAPDOOR_PROP:
							dataVal = (half ? 8 : 0) | (open ? 4 : 0) | (4-dataVal);
							break;
						case TALL_FLOWER_PROP:
							// Top half of sunflowers, etc., have just the 0x8 bit set, not the flower itself.
							// Doesn't matter to Mineways per se, but if we export a schematic, we should make
							// this data the same as Minecraft's.
							dataVal = half ? 0x8 : 0;
							break;
						case REDSTONE_ORE_PROP:
							if (lit) {
								// light redstone ore or redstone lamp
								paletteBlockEntry[entry_index]++;
							}
							break;
						case FENCE_GATE_PROP:
							// strange but true;
							dataVal = (open ? 0x4 : 0x0) | ((door_facing + 3) % 4) | (in_wall ? 0x20 : 0);
							break;
						case SWNE_FACING_PROP:
							// south/west/north/east == 0/1/2/3
							dataVal = (door_facing + 3) % 4;
							break;
						case BED_PROP:
							// south/west/north/east == 0/1/2/3
							dataVal = ((door_facing + 3) % 4) + occupied + part;
							break;
						case DROPPER_PROP:
							// triggered is used to set dataVal
							dataVal = dropper_facing | (triggered ? 8 : 0);
							break;
						case PISTON_PROP:
							// triggered is used to set dataVal
							dataVal = dropper_facing | (extended ? 8 : 0);
							break;
						case PISTON_HEAD_PROP:
							// type is used to set dataVal above to 8 if sticky
							dataVal = dropper_facing | sticky;
							break;
						case VINE_PROP:
							dataVal = (south ? 1 : 0) | (west ? 2 : 0) | (north ? 4 : 0) | (east ? 8 : 0);
							break;
						case COCOA_PROP:
							dataVal = ((door_facing + 3) % 4) + (age << 2);
							break;
						case QUARTZ_PILLAR_PROP:
							// for quartz pillar, change data val based on axis
							switch (axis) {
							default:
							case 0:
								paletteDataEntry[entry_index] = 2;
								break;
							case 4:
								paletteDataEntry[entry_index] = 3;
								break;
							case 8:
								paletteDataEntry[entry_index] = 4;
								break;
							}
							break;
						case REPEATER_PROP:
							dataVal = ((door_facing + 3) % 4) | (delay << 2);
							if (powered) {
								// use active form
								paletteBlockEntry[entry_index]++;
							}
							break;
						case COMPARATOR_PROP:
							dataVal = ((door_facing + 3) % 4) | (mode ? 4 : 0) | (powered ? 8 : 0);
							break;
						case MUSHROOM_STEM_PROP:
							dataVal = up ? 15 : 10;
							break;
						case MUSHROOM_PROP:
							// https://minecraft.gamepedia.com/Java_Edition_data_values#Brown_and_red_mushroom_blocks
							if (north) {
								// 1,2,3,14
								if (south) {
									// 14 - all sides
									dataVal = 14;
								}
								else if (west) {
									dataVal = 1;
								}
								else if (east) {
									dataVal = 3;
								}
								else
									dataVal = 2;
							}
							else {
								// 0,4,5,6,7,8,9 (stem is separate with 10 and 15)
								if (south) {
									// 7,8,9
									if (west) {
										dataVal = 7;
									}
									else if (east) {
										dataVal = 9;
									}
									else
										dataVal = 8;
								}
								else {
									// 0,4,5,6
									if (west) {
										dataVal = 4;
									}
									else if (east) {
										dataVal = 6;
									}
									else if (up) {
										dataVal = 5;
									}
									else
										dataVal = 0;
								}
							}
							break;
						case ANVIL_PROP:
							dataVal = (door_facing + 3) % 4;
							break;
						case HOPPER_PROP:
							dataVal = dropper_facing | (enabled ? 8 : 0);
							break;
						case DAYLIGHT_PROP:
							// change to inverted form if inverted is set
							if (inverted) {
								paletteBlockEntry[entry_index] = 178;
							}
							break;
						case TRIPWIRE_PROP:
							dataVal = (powered ? 1 : 0) | (attached ? 4 : 0) | (disarmed ? 8 : 0);
							break;
						case TRIPWIRE_HOOK_PROP:
							dataVal = ((door_facing + 3) % 4) | (attached ? 4 : 0) | (powered ? 8 : 0);
							break;
						case END_PORTAL_PROP:
							dataVal = ((door_facing + 3) % 4) | (eye ? 4 : 0);
							break;
						case COMMAND_BLOCK_PROP:
							dataVal = dropper_facing | (conditional ? 8:0);
							break;
						case OBSERVER_PROP:
							// note that powered is ignored in 1.12
							dataVal = dropper_facing;
							break;
							// If dataVal is 2-5, rotation is not used (head is on a wall) and high bit of head type is off, else it is put on.
							// 7 | 654 | 3210
							// bit 7 - is bottom four bits 3210 the rotation on floor? If off, put on wall.
							// bits 654 - the head. Hopefully Minecraft won't add more than 8 heads...
							// bits 3210 - depends on bit 7; rotation if on floor, or on which wall (2-5)

						case HEAD_PROP:
							// see BLOCK_HEAD in ObjFileManip.cpp for data layout, which is crazed
							dataVal |= 0x80;	// it's a rotation, so flag it as such
							break;
						case HEAD_WALL_PROP:
							// see BLOCK_HEAD in ObjFileManip.cpp for data layout, which is crazed
							switch (door_facing) { // starts at 1: south, west, north, east
							default:
							case 1: // south
								dataVal = 3;
								break;
							case 2: // west
								dataVal = 4;	// docs say 5, docs are wrong
								break;
							case 3: // north
								dataVal = 2;
								break;
							case 0: // east
								dataVal = 5;	// docs say 4, docs are wrong
								break;
							}
							break;
						case WATERLOGGED_PROP:
							dataVal = (waterlogged ? 0x10 : 0x0);
							break;
						case FAN_PROP:
							// low 3 are the subtype
							// fourth bit empty
							// fifth bit waterlogged
							// 2 bits facing: NESW
							dataVal = (waterlogged ? 0x10 : 0x0) | ((door_facing + 3) % 4) << 5;
							break;
						case PICKLE_PROP:
							dataVal |= (waterlogged ? 0x10 : 0x0);
							break;
						case EGG_PROP:
							dataVal |= (hatch<<2);
							break;
						case TRULY_NO_PROP:
							dataVal = 0x0;
							break;
						case NO_PROP:
						default:
							break;
						}
						paletteDataEntry[entry_index] |= dataVal;
						entry_index++;
					}
				}
				if (!ret)
					if (skipType(bf, type) < 0) return -18;
			}
			// now that we have all the data (I hope - TODO, check that we do), convert bigbuff layer into buff and data values
			// DEBUG: entry_index > 2 - shows just the tiles that are not all a single block type & air
			if (bigbufflen > 0 && entry_index > 0) {
				// compute number of bits
				int bitlength = bigbufflen / 64;
				unsigned long int bitmask = (1<<bitlength) - 1;

				unsigned char *bout = buff + 16 * 16 * 16 * y;
				unsigned char *dout = data + 16 * 16 * 16 * y;
				for (int i = 0; i < 16 * 256; i++) {
					// pull out bits. Here is the lowest bit's index, if the array is thought of as one long string of bits.
					unsigned int bitpull = i * bitlength;
					// which bb should we access for these bits? Divide by 8
					unsigned int bbindex = bitpull >> 3;
					// Have to count from top to bottom 8 bytes of each long long. I suspect if I read the long longs as bytes the order might be right.
					// But, this works.
					bbindex = (bbindex & 0xfff8) + 7 - (bbindex & 0x7);
					unsigned int bbshift = bitpull & 0x7;
					int bits = (bigbuff[bbindex] >> bbshift) & bitmask;
					if (bbshift + bitlength > 8) {
						if (bbindex & 0x7) {
							// one of the middle bytes, not the bottommost one
							bits |= (bigbuff[bbindex - 1] << (8 - bbshift)) & bitmask;
						}
						else {
							// bottommost byte, need to jump to topmost byte of next long long
							bits |= (bigbuff[bbindex + 15] << (8 - bbshift)) & bitmask;
						}
					}
					// for larger, we'll need to check if bbshift + bitlength is > 64 and if so, then pull in higher bits
					if (bits >= entry_index) {
						// TODO reality check - should never reach here; means that a stored index value is greater than any value in the palette.
						bits = entry_index - 1;
					}
					*bout++ = paletteBlockEntry[bits];
					*dout++ = paletteDataEntry[bits];
				}
			}
		}
    }
	if (!newFormat) {
		if (nbtFindElement(bf, "TileEntities") != 9)
			// all done, no TileEntities found
			return 1;

		{
			type = 0;
			if (bfread(bf, &type, 1) < 0) return -19;
			if (type != 10)
				return 1;

			// tile entity (aka block entity) found, parse it - get number of sections that follow
			nsections = readDword(bf);

			// hack - don't really want to include blockInfo.h down here.
#define BLOCK_HEAD			0x90
#define BLOCK_FLOWER_POT	0x8C

			int numSaved = 0;

			while (nsections--)
			{
				// read all the elements in this section
				bool skipSection = false;
				unsigned char dataType = 0;
				unsigned char dataRot = 0;
				unsigned char dataSkullType = 0;
				unsigned char dataFlowerPotType = 0;
				int dataX = 0;
				int dataY = 0;
				int dataZ = 0;
				int dataData = 0;
				for (;;)
				{
					type = 0;
					if (bfread(bf, &type, 1) < 0) return -20;
					if (type == 0) {
						// end of list, so process data, if any valid data found
						if (!skipSection) {
							// save skulls and flowers
							BlockEntity *pBE = &entities[numSaved];
							pBE->type = dataType;
							pBE->y = (unsigned char)dataY;
							pBE->zx = (mod16(dataZ) << 4) | mod16(dataX);
							switch (dataType) {
							case BLOCK_HEAD:
								pBE->data = (unsigned char)((dataSkullType << 4) | dataRot);
								break;
							case BLOCK_FLOWER_POT:
								pBE->data = (unsigned char)((dataFlowerPotType << 4) | dataData);
								break;
							default:
								// should flag an error!
								break;
							}
							numSaved++;
						}
						break;
					}

					// always read name of field
					len = readWord(bf);
					if (bfread(bf, thisName, len) < 0) return -21;

					// if the id is one we don't care about, skip the rest of the data
					if (skipSection) {
						if (skipType(bf, type) < 0) return -22;
					}
					else {
						thisName[len] = 0;
						if (strcmp(thisName, "x") == 0 && type == 3)
						{
							dataX = readDword(bf);
						}
						else if (strcmp(thisName, "y") == 0 && type == 3)
						{
							dataY = readDword(bf);
						}
						else if (strcmp(thisName, "z") == 0 && type == 3)
						{
							dataZ = readDword(bf);
						}
						else if (strcmp(thisName, "id") == 0 && type == 8)
						{
							len = readWord(bf);
							char idName[MAX_NAME_LENGTH];
							if (bfread(bf, idName, len) < 0) return -23;
							idName[len] = 0;

							// is it a skull or a flowerpot?
							if (strcmp(idName, "minecraft:skull") == 0)
							{
								dataType = BLOCK_HEAD;
							}
							else if (strcmp(idName, "minecraft:flower_pot") == 0)
							{
								dataType = BLOCK_FLOWER_POT;
							}
							else {
								skipSection = true;
							}
						}
						else if (strcmp(thisName, "Item") == 0 && type == 8)
						{
							len = readWord(bf);
							char idName[MAX_NAME_LENGTH];
							if (bfread(bf, idName, len) < 0) return -24;
							idName[len] = 0;
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
							*/
							char* potName[] = {
								"minecraft:air", "minecraft:red_flower", "minecraft:yellow_flower", "minecraft:red_mushroom", "minecraft:brown_mushroom",
								"minecraft:sapling", "minecraft:deadbush", "minecraft:tallgrass", "minecraft:cactus" };

							skipSection = true;
							for (int pot = 0; pot < 9; pot++) {
								if (strcmp(idName, potName[pot]) == 0) {
									dataFlowerPotType = (unsigned char)pot;
									skipSection = false;
									break;
								}
							}
						}
						else if (strcmp(thisName, "Rot") == 0 && type == 1)
						{
							if (bfread(bf, &dataRot, 1) < 0) return -25;
						}
						else if (strcmp(thisName, "SkullType") == 0 && type == 1)
						{
							if (bfread(bf, &dataSkullType, 1) < 0) return -26;
						}
						else if (strcmp(thisName, "Data") == 0 && type == 3)
						{
							dataData = readDword(bf);
						}
						else {
							// unused type, skip it, and skip all rest of object, since it's something we don't care about
							// (all fields we care about are read above - if we hit one we don't care about, we know we
							// can ignore the rest).
							skipType(bf, type);
							skipSection = true;
						}
					}
				}
			}

			if (numSaved > 0) {
				*numEntities = numSaved;
			}
		}
	}
    return 1;
}

int nbtGetSpawn(bfFile bf, int *x, int *y, int *z)
{
    int len;
    *x=*y=*z=0;
    //Data/SpawnX
    // don't really need this first seek to beginning of file
    //bfseek(bf,0,SEEK_SET);
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"SpawnX")!=3) return -4;
    *x=readDword(bf);

    // Annoyingly, SpawnY can come before SpawnX, so we need to re-find each time.
    // For some reason, seeking to a stored offset does not work.
    // So we seek to beginning of file and find "Data" again.
    if (bfseek(bf, 0, SEEK_SET) < 0) return -5;
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -6; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -7; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -8;
    if (nbtFindElement(bf,"SpawnY")!=3) return -9;
    *y=readDword(bf);

    // We seek to beginning of file and find "Data" again.
    if (bfseek(bf, 0, SEEK_SET) < 0) return -10;
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -11; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -12; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -13;
    if (nbtFindElement(bf,"SpawnZ")!=3) return -14;
    *z=readDword(bf);
    return 0;
}

//  The NBT version of the level, 19133. See http://minecraft.gamepedia.com/Level_format#level.dat_format
int nbtGetFileVersion(bfFile bf, int *version)
{
    *version = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"version")!=3) return -4;
    *version=readDword(bf);
    return 0;
}

// From Version, not version, see http://minecraft.gamepedia.com/Level_format#level.dat_format at bottom
// This is a newer tag for 1.9 and on, older worlds do not have them
int nbtGetFileVersionId(bfFile bf, int *versionId)
{
    *versionId = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, "Data") != 10) return -3;
    if (nbtFindElement(bf, "Version") != 10) return -4;
    if (nbtFindElement(bf, "Id") != 3) return -5;
    *versionId = readDword(bf);
    return 0;
}

int nbtGetFileVersionName(bfFile bf, char *versionName, int stringLength)
{
    *versionName = '\0'; // initialize to empty string
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, "Data") != 10) return -3;
    if (nbtFindElement(bf, "Version") != 10) return -4;
    if (nbtFindElement(bf, "Name") != 8) return -5;
    len = readWord(bf);
    if (len < stringLength) {
        if (bfread(bf, versionName, len) < 0) return -6;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(bf, versionName, stringLength - 1) < 0) return -7;	// save last position for string terminator
        if (bfseek(bf, len - stringLength + 1, SEEK_CUR) < 0) return -8;
        len = stringLength - 1;
    }
    versionName[len] = 0;
    return 0;
}

int nbtGetLevelName(bfFile bf, char *levelName, int stringLength)
{
    *levelName = '\0'; // initialize to empty string
    int len;
    //Data/levelName
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    // 8 means a string
    if (nbtFindElement(bf,"LevelName")!=8) return -4;
    len=readWord(bf);
    if (len < stringLength) {
        if (bfread(bf, levelName, len) < 0) return -5;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(bf, levelName, stringLength - 1) < 0) return -6;	// save last position for string terminator
        if (bfseek(bf, len - stringLength + 1, SEEK_CUR) < 0) return -7;
        len = stringLength - 1;
    }
    levelName[len] = 0;
    return 0;
}

//void nbtGetRandomSeed(bfFile bf,long long *seed)
//{
//    int len;
//    *seed=0;
//    //Data/RandomSeed
//    bfseek(bf,1,SEEK_CUR); //skip type
//    len=readWord(bf); //name length
//    bfseek(bf,len,SEEK_CUR); //skip name ()
//    if (nbtFindElement(bf,"Data")!=10) return;
//    if (nbtFindElement(bf,"RandomSeed")!=4) return;
//    *seed=readLong(bf);
//}
int nbtGetPlayer(bfFile bf, int *px, int *py, int *pz)
{
    int len;
    *px=*py=*pz=0;
    //Data/Player/Pos
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"Player")!=10) return -4;
    if (nbtFindElement(bf,"Pos")!=9) return -5;
    if (bfseek(bf, 5, SEEK_CUR) < 0) return -6; //skip subtype and num items
    *px=(int)readDouble(bf);
    *py=(int)readDouble(bf);
    *pz=(int)readDouble(bf);
    return 0;
}

//////////// schematic
//  http://minecraft.gamepedia.com/Schematic_file_format
// return 0 if not found, 1 if all is well
int nbtGetSchematicWord(bfFile bf, char *field, int *value)
{
    *value = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, field) != 2) return -3;
    *value = readWord(bf);
    return 1;
}

// return 1 on success
int nbtGetSchematicBlocksAndData(bfFile bf, int numBlocks, unsigned char *schematicBlocks, unsigned char *schematicBlockData)
{
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()

    int found = 0;
    while (found < 2)
    {
        int ret = 0;
        unsigned char type = 0;
        if (bfread(bf, &type, 1) < 0) return -3;
        if (type == 0)
            break;
        len = readWord(bf);
        char thisName[MAX_NAME_LENGTH];
        if (bfread(bf, thisName, len) < 0) return -4;
        thisName[len] = 0;
        if (strcmp(thisName, "Blocks") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(bf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(bf, schematicBlocks, len) < 0) return -5;
            found++;
        }
        else if (strcmp(thisName, "Data") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(bf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(bf, schematicBlockData, len) < 0) return -6;
            found++;
        }
        if (!ret)
            if (skipType(bf, type) < 0) return -7;
    }
    // Did we find two things? If so, return 1, success
    return ((found == 2) ? 1 : -8);
}


void nbtClose(bfFile bf)
{
    if (bf.type == BF_GZIP)
        gzclose(bf.gz);
}
