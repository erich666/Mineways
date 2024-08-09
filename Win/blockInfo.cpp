/*
Copyright (c) 2014, Eric Haines
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

bool gDebug = false;

MaterialCost gMtlCostTable[MTL_COST_TABLE_SIZE] = {
    // wname                 name                  Currency minWall in meters  minSum in meters    costH  costPerCCM cDens    cDisCCM  costMinimum?  costMachCC maxSizeCM (descending order)
    { L"White & Flexible",   "white & flexible",   L"$", 0.7f * MM_TO_METERS, 7.5f * MM_TO_METERS,  1.50f, 0.28f,    999.0f, 99999.0f,  1.50f * 0.10f,  0.21f,     65.0f,55.0f,35.0f },
    { L"Colored Sandstone",  "colored sandstone",  L"$", 2.0f * MM_TO_METERS, 10.0f * MM_TO_METERS, 3.00f, 0.75f,    999.0f, 99999.0f,  3.00f * 0.134f, 0.0f,      38.0f,25.0f,20.0f },
    { L"Full Color Plastic", "full color plastic", L"$", 0.7f * MM_TO_METERS, 10.0f * MM_TO_METERS, 3.00f, 2.00f,    999.0f, 99999.0f,  3.00f * 0.134f, 0.0f,      15.0f,15.0f,15.0f },
    { L"Colored & Flexible", "colored & flexible", L"$", 0.7f * MM_TO_METERS, 7.5f * MM_TO_METERS,  2.25f, 0.28f,    999.0f, 99999.0f,  2.25f * 0.10f,  0.21f,     65.0f,55.0f,35.0f },
    { L"Brass/Bronze",       "brass/bronze",       L"$", 0.8f * MM_TO_METERS, 3.4f * MM_TO_METERS,  10.0f, 16.0f,    999.0f, 99999.0f, 10.0f * 0.10f,  0.0f,      10.0f, 8.9f, 8.9f },
    { L"Castable Wax",       "castable wax",       L"$", 0.6f * MM_TO_METERS, 12.0f * MM_TO_METERS, 10.0f, 8.0f,     999.0f, 99999.0f, 10.0f * 0.10f,  0.0f,      75.0f,75.0f,50.0f },
    { L"Ceramics",           "ceramics",           L"$", 3.0f * MM_TO_METERS, 120.0f * MM_TO_METERS,6.00f, 0.35f,    999.0f, 99999.0f,  6.00f,        0.0f,      30.0f,22.0f,17.0f },
    { L"Detailed Plastic",   "alumide/detail",	   L"$", 1.0f * MM_TO_METERS, 6.25f * MM_TO_METERS, 2.50f, 2.99f,    999.0f, 99999.0f,  2.50f * 0.10f,  0.0f,      25.0f,25.0f,20.0f },
    { L"Elasto Plastic",     "elasto plastic",     L"$", 0.8f * MM_TO_METERS, 10.0f * MM_TO_METERS, 1.50f, 0.42f,    999.0f, 99999.0f,  1.95f * 0.10f,  0.26f,     30.0f,30.0f,25.0f },
    { L"Frosted Detail",     "frosted detail",     L"$", 0.5f * MM_TO_METERS, 12.0f * MM_TO_METERS, 5.00f, 2.39f,    999.0f, 99999.0f,  5.00f * 0.10f,  0.0f,      28.4f,20.3f,18.4f },
    // make the dialog usable: { L"Gold",               "gold",               0.8f*MM_TO_METERS, 5.4f*MM_TO_METERS,  50.0f,600.0f,    999.0f, 99999.0f, 50.0f *0.10f,  0.0f,      10.0f, 8.9f, 8.9f },
    //{ L"Metallic Plastic",   "metallic plastic",   0.8f*MM_TO_METERS, 7.5f*MM_TO_METERS,  1.50f, 0.56f,    999.0f, 99999.0f,  1.50f*0.10f,  0.32f,     31.0f,23.0f,18.0f },
    // make the dialog usable: { L"Platinum",           "platinum",           0.8f*MM_TO_METERS, 5.4f*MM_TO_METERS,  100.0f,1750.0f,  999.0f, 99999.0f, 100.0f*0.10f,  0.0f,      10.0f, 8.9f, 8.9f },
    { L"Silver",             "silver",             L"$", 0.8f * MM_TO_METERS, 3.4f * MM_TO_METERS,  45.0f, 28.0f,    999.0f, 99999.0f, 45.0f * 0.10f,  0.0f,      10.0f, 8.9f, 8.9f },
    { L"Steel",              "steel",              L"$", 1.0f * MM_TO_METERS, 27.0f * MM_TO_METERS, 6.00f, 5.00f,    999.0f, 99999.0f,  8.00f * 0.10f,  0.0f,      76.2f,39.3f,39.3f },
    // old model, based on surface area: { L"Glazed Ceramics",    "glazed ceramics",    3.0f*MM_TO_METERS, 0.0f,               0.00f, 0.00f,    999.0f, 99999.0f,  1.00f,        0.18f,     30.0f,22.0f,17.0f },
    // I haven't figured out Sculpteo's cost model. Prices tend to be 20% higher, so that's what's here, but I think it also has to do with dimensions, which affects print time.
    // With full-sized rectangular blocks, the costPerCCM is more like 1.05
    { L"Sculpteo Multicolor", "Sculpteo multicolor", L"$", 2.0f * MM_TO_METERS, 0.0f * MM_TO_METERS, 3.00f, 1.20f * 0.75f, 999.0f, 99999.0f, 3.00f,     0.0f,      38.0f,25.0f,20.0f },	// Sculpteo sandstone
    // typical home printer, 1.75 mm PLA: https://hwg.fictiv.com/fabricate/recommended-wall-thickness-for-3d-printing and http://www.fabbaloo.com/blog/2015/9/27/is-it-filament-weight-or-length
    { L"Custom Printer", "custom printer",       L"$", 1.5f * MM_TO_METERS,  0.0f * MM_TO_METERS,  0.00f, 0.03f,   999.0f, 99999.0f,  0.00f,        0.0f,      20.0f,20.0f,20.0f },	// Ultimaker, PLA
};

UnitType gUnitTypeTable[MODELS_UNITS_TABLE_SIZE] = {
    { L"Meters", "meters", 1.0f },
    { L"Centimeters", "centimeters", 100.0f },
    { L"Millimeters", "millimeters", 1000.0f },
    { L"Inches", "inches", 100.0f / 2.54f }
};

// First column "comment" number is from https://minecraft.wiki/w/Java_Edition_data_values/Pre-flattening#Block_IDs through 255, then I just assign numbers as I wish.
// For sub-blocks, see https://minecraft-ids.grahamedgecombe.com/
// Other interesting pages: https://minecraft.wiki/w/Solid_block maximum block height, https://minecraft.wiki/w/Materials material attributes (not all that useful to me, but still)
// The "subtype_mask" (shown as "mtl") field shows which bits are used to determine which type of block it is (vs. orientation or other sub-data about the block). This field gets recalculated by the
// software itself, but putting it here gives a sense of what blocks can be added to with new content.
BlockDefinition gBlockDefinitions[NUM_BLOCKS_DEFINED] = {
    // Ignore the premultiplied colors and alphas - these really are just placeholders, it's color * alpha that sets them when the program starts up.
    // name                               		read_color ralpha color     prem-clr  alpha,     txX,Y   mtl, flags
    { /*   0 */ "Air",                    		0x000000, 0.000f, 0xff7711, 0xff7711, 0.12345f,  13,14, 0x00, BLF_NONE},	//00
    { /*   1 */ "Stone",                  		0x7C7C7C, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 0, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//01
    // Grass block color is from Plains biome color (default terrain in a flat world). Grass and Sunflower should also be changed if this is changed.
    { /*   2 */ "Grass Block", /*output!*/		0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 0, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//02 3,0 side gets turned into 6,2
    { /*   3 */ "Dirt",                   		0x8c6344, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 0, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//03
    { /*   4 */ "Cobblestone",            		0x828282, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//04
    { /*   5 */ "Oak Planks",             		0x9C8149, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//05
    { /*   6 */ "Oak Sapling",                	0x7b9a29, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15, 0, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//06
    { /*   7 */ "Bedrock",                		0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//07
    { /*   8 */ "Water",                  		0x295dfe, 0.535f, 0xff7711, 0xff7711, 0.12345f,   8,26, 0x00, BLF_ALMOST_WHOLE | BLF_TRANSPARENT},	//08
    { /*   9 */ "Stationary Water",       		0x295dfe, 0.535f, 0xff7711, 0xff7711, 0.12345f,  15,13, 0x10, BLF_ALMOST_WHOLE | BLF_TRANSPARENT},	//09
    { /*  10 */ "Lava",                   		0xf56d00, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9,26, 0x00, BLF_ALMOST_WHOLE | BLF_EMITTER},	//0a
    { /*  11 */ "Stationary Lava",        		0xf56d00, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15,15, 0x00, BLF_ALMOST_WHOLE | BLF_EMITTER},	//0b
    { /*  12 */ "Sand",                   		0xDCD0A6, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 1, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//0c/12
    { /*  13 */ "Gravel",                 		0x857b7b, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//0d
    { /*  14 */ "Gold Ore",               		0xfcee4b, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 2, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//0e
    { /*  15 */ "Iron Ore",               		0xbc9980, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 2, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//0f
    { /*  16 */ "Coal Ore",               		0x343434, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 2, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//10
    { /*  17 */ "Oak Log",                		0x695333, 1.000f, 0xff7711, 0xff7711, 0.12345f,   5, 1, 0x13, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR},	//11/17
    // Leaves block color is from Plains biome color (default terrain in a flat world). Acacia Leaves should also be changed if this is changed.
    { /*  18 */ "Oak Leaves",					0x6fac2c, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 3, 0x03, BLF_WHOLE | BLF_CUTOUTS | BLF_LEAF_PART | BLF_MAYWATERLOG},	//12
    { /*  19 */ "Sponge",                 		0xD1D24E, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 3, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//13
    { /*  20 */ "Glass",                  		0xc0f6fe, 0.500f, 0xff7711, 0xff7711, 0.12345f,   1, 3, 0x01, BLF_WHOLE | BLF_CUTOUTS | BLF_FENCE_NEIGHBOR},	//14 - note that BLF_TRANSPARENT is not flagged, 0x00FF, Because glass is either fully on or off, not blended
    { /*  21 */ "Lapis Lazuli Ore",       		0x143880, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,10, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//15
    { /*  22 */ "Block of Lapis Lazuli",     	0x1b4ebb, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 9, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//16
    { /*  23 */ "Dispenser",              		0x6f6f6f, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 3, 0x00, BLF_WHOLE},	//17 14,2 front, 13,2 sides
    { /*  24 */ "Sandstone",              		0xe0d8a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,11, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//18 0,12 side, 0,13 bottom
    { /*  25 */ "Note Block",             		0x342017, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 8, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//19 10,4 side
    { /*  26 */ "Bed",                    		0xff3333, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 8, 0x00, BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT},	//1a
    { /*  27 */ "Powered Rail",           		0xAB0301, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3,11, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_3D_BIT | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_MAYWATERLOG},
    { /*  28 */ "Detector Rail",          		0xCD5E58, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3,12, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_3D_BIT | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE | BLF_OFFSET | BLF_MAYWATERLOG},
    { /*  29 */ "Sticky Piston",          		0x719e60, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 6, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT},	//1d
    { /*  30 */ "Cobweb",                 		0xeeeeee, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11, 0, 0x00, BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//1e
    { /*  31 */ "Short Grass", /* 1.20.3 name*/	0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 2, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//1f/31
    { /*  32 */ "Dead Bush",              		0x946428, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 3, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//20/32
    { /*  33 */ "Piston",                 		0x95774b, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 6, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT},	//21
    { /*  34 */ "Piston Head",            		0x95774b, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11, 6, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT},	//22/34
    { /*  35 */ "Wool",                   		0xEEEEEE, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 4, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//23 - gets converted to colors at end
    { /*  36 */ "Block moved by Piston",  		0x010101, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11, 6, 0x00, BLF_NONE},	//24 (36) - really, nothing...
    { /*  37 */ "Dandelion",              		0xD3DD05, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13, 0, 0x01, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//25
    { /*  38 */ "Poppy",                  		0xCE1A05, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 0, 0x0f, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//26 - 38
    { /*  39 */ "Brown Mushroom",         		0xc19171, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13, 1, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_EMITTER},	//27
    { /*  40 */ "Red Mushroom",           		0xfc5c5d, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 1, 0x03, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//28
    { /*  41 */ "Block of Gold",          		0xfef74e, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//29
    { /*  42 */ "Block of Iron",          		0xeeeeee, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//2a
    { /*  43 */ "Double Stone Slab",      		0xa6a6a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,23, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /*  44 */ "Stone Slab",             		0xa5a5a5, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,23, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG},	//2c/44 - was 6,0, and 5,0 side
    { /*  45 */ "Bricks",                 		0x985542, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 0, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//2d 
    { /*  46 */ "TNT",                    		0xdb441a, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 0, 0x01, BLF_WHOLE},	//2e 7,0 side, 9,0 under
    { /*  47 */ "Bookshelf",              		0x795a39, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x10, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//2f 3,2
    { /*  48 */ "Mossy Cobblestone",      		0x627162, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 2, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//30
    { /*  49 */ "Obsidian",               		0x1b1729, 1.000f, 0xff7711, 0xff7711, 0.12345f,   5, 2, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//31
    { /*  50 */ "Torch",                  		0xfcfc00, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 5, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_EMITTER | BLF_DNE_FLUID},	//32/50 - should be BLF_EMITTER, flatten torches only if sides get flattened, too
    { /*  51 */ "Fire",                   		0xfca100, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* somewhat bogus */ 15, 1, 0x10, BLF_BILLBOARD | BLF_CUTOUTS | BLF_EMITTER | BLF_DNE_FLUID},	//33/51 - no billboard, sadly BLF_CUTOUTS, and soul fire is at 0x10
    { /*  52 */ "Spawner",              		0x254254, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 4, 0x00, BLF_ALMOST_WHOLE | BLF_CUTOUTS},	//34
    { /*  53 */ "Oak Stairs",             		0x9e804f, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },	//35
    { /*  54 */ "Chest",                  		0xa06f23, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 1, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },	//36 (10,1) side; (11,1) front
    { /*  55 */ "Redstone Wire",/*"Dust"*/		0xd60000, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,10, 0x0f, BLF_FLATTEN | BLF_DNE_FLUID | BLF_CUTOUTS | BLF_CONNECTS_REDSTONE | BLF_OFFSET},	//37 - really, 0xfd3200 is lit, we use a "neutral" red here
    { /*  56 */ "Diamond Ore",            		0x5decf5, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 3, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//38
    { /*  57 */ "Block of Diamond",       		0x7fe3df, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8, 1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//39
    { /*  58 */ "Crafting Table",         		0x825432, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11, 2, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//3a - and cartography, fletching, and smithing
    { /*  59 */ "Wheat",                  		0x766615, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15, 5, 0x00, BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//3b
    { /*  60 */ "Farmland",               		0x552F14, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 5, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE},	//3c - 7,5 dry
    { /*  61 */ "Furnace",                		0x767677, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 3, 0x30, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//3d 13,2 side, 12,2 front
    { /*  62 */ "Burning Furnace",        		0x777676, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 3, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER},	//3e 13,2 side, 13,3 front
    { /*  63 */ "Oak Sign",             		0xA58551, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x30, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG},	//3f (63)
    { /*  64 */ "Oak Door",               		0x7e5d2d, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 5, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT}, // 40 1,6 bottom	//40 TODO: BLF_FLATSIDE?
    { /*  65 */ "Ladder",                 		0xaa8651, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 5, 0x00, BLF_FLATTEN_SMALL | BLF_ENTRANCE | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_MAYWATERLOG},	//41
    { /*  66 */ "Rail",                   		0x686868, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 8, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_3D_BIT | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_MAYWATERLOG},	//42 - TODO: doesn't do angled pieces, top to bottom edge
    { /*  67 */ "Cobblestone Stairs",     		0x818181, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 1, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },	//43 (67)
    { /*  68 */ "Wall Sign",              		0xA58551, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x38, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG},	//44
    { /*  69 */ "Lever",                  		0x8a6a3d, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 6, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_FLATTEN_SMALL | BLF_FLATTEN_SMALL | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE},	//45
    { /*  70 */ "Stone Pressure Plate",   		0xa4a4a4, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 0, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE},	//46 (70)
    { /*  71 */ "Iron Door",              		0xb2b2b2, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 5, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT},	//47 (71) 2,6 bottom TODO BLF_FLATSIDE?
    { /*  72 */ "Oak Pressure Plate",     		0x9d7f4e, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE},	//48
    { /*  73 */ "Redstone Ore",           		0x8f0303, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 3, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//49
    { /*  74 */ "Glowing Redstone Ore",   		0x900303, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 3, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER},	// unobtainable normally! https://minecraft.wiki/w/Redstone_Ore#Lit_redstone_ore_%22item%22
    { /*  75 */ "Redstone Torch (inactive)",	0x560000, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 7, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE},	//4b
    { /*  76 */ "Redstone Torch (active)",  	0xfd0000, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 6, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_EMITTER | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE},	//4c should be BLF_EMITTER, 0x00FF, But it makes the whole block glow
    { /*  77 */ "Stone Button",           		0xacacac, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 0, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID},	//4d
    { /*  78 */ "Snow",			           		0xf0fafa, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 4, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID},	//4e
    { /*  79 */ "Ice",                    		0x7dacfe, 0.613f, 0xff7711, 0xff7711, 0.12345f,   3, 4, 0x00, BLF_WHOLE | BLF_TRANSPARENT},	//4f
    { /*  80 */ "Snow Block",              		0xf1fafa, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2, 4, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//50 4,4 side
    { /*  81 */ "Cactus",                 		0x0D6118, 1.000f, 0xff7711, 0xff7711, 0.12345f,   5, 4, 0x00, BLF_ALMOST_WHOLE | BLF_BILLBOARD | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID},	//51 6,4 side - note: the cutouts are not used when "lesser" is off for rendering, 0x00FF, But so it goes.
    { /*  82 */ "Clay",                   		0xa2a7b4, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8, 4, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//52
    { /*  83 */ "Sugar Cane",             		0x72944e, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 4, 0x00, BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID},	//53
    { /*  84 */ "Jukebox",                		0x8a5a40, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11, 4, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	//54 11,3 side
    { /*  85 */ "Fence",                  		0x9f814e, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG},	//55
    { /*  86 */ "Pumpkin",                		0xc07615, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 6, 0x04, BLF_WHOLE},	//56 6,7 side, 7,7 face
    { /*  87 */ "Netherrack",             		0x723a38, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 6, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},   //57
    { /*  88 */ "Soul Sand",              		0x554134, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8, 6, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR},   //58
    { /*  89 */ "Glowstone",              		0xf9d49c, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 6, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER},   //59
    { /*  90 */ "Nether Portal",				0x472272, 0.800f, 0xff7711, 0xff7711, 0.12345f,  14, 0, 0x00, BLF_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_PANE | BLF_TRANSPARENT | BLF_EMITTER | BLF_DNE_FLUID},   //5a/90 - 0xd67fff unpremultiplied
    { /*  91 */ "Jack o'Lantern",				0xe9b416, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 6, 0x00, BLF_WHOLE | BLF_EMITTER},   //5b 6,7 side, 8,7 lit face - truly is not a fence neighbor
    { /*  92 */ "Cake",							0xfffdfd, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 7, 0x3f, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT },   //5c 10,7 side, 11,7 inside, 12,7 under - TODO: not really whole
    { /*  93 */ "Redstone Repeater (inactive)",	0x560000, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 8, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID},   //5d
    { /*  94 */ "Redstone Repeater (active)",	0xee5555, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3, 9, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID},   //5e
    // in 1.7 locked chest was replaced by stained glass block
    //{"Locked Chest",           0xa06f23, 1.000f, 0xa06f23,  9, 1, 0x0F, BLF_WHOLE},   //5f/95 (10,1) side; (11,1) front
    { /*  95 */ "Stained Glass",          		0xEFEFEF, 0.500f, 0xff7711, 0xff7711, 0.12345f,   0,20, 0x0f, BLF_WHOLE | BLF_TRANSPARENT | BLF_FENCE_NEIGHBOR },	//5f/95 - note BLF_CUTOUTS is off, since all pixels are semitransparent
    { /*  96 */ "Oak Trapdoor",           		0x886634, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 5, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   //60/96 - tricky case: could be a flattop, or a flatside. For now, render it
    { /*  97 */ "Infested Stone",   			0x787878, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1, 0, 0x07, BLF_WHOLE },   //61 - was "Monster Egg"
    { /*  98 */ "Stone Bricks",           		0x797979, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 3, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //62
    { /*  99 */ "Brown Mushroom Block", 		0x654b39, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 7, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //63
    { /* 100 */ "Red Mushroom Block",   		0xa91b19, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13, 7, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //64
    { /* 101 */ "Iron Bars",              		0xa3a4a4, 1.000f, 0xff7711, 0xff7711, 0.12345f,   5, 5, 0x00, BLF_PANE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CUTOUTS | BLF_MAYWATERLOG },   //65
    { /* 102 */ "Glass Pane",             		0xc0f6fe, 0.500f, 0xff7711, 0xff7711, 0.12345f,   1, 3, 0x00, BLF_PANE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CUTOUTS | BLF_MAYWATERLOG },   //66
    { /* 103 */ "Melon",                  		0xaead27, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 8, 0x00, BLF_WHOLE },   //67 (8,8) side
    { /* 104 */ "Pumpkin Stem",           		0xE1C71C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,11, 0x0f, BLF_FLATTEN_SMALL | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },   //68/104 15,11 connected TODO
    { /* 105 */ "Melon Stem",             		0xE1C71C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15, 6, 0x0f, BLF_FLATTEN_SMALL | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },   //69/105 15,7 connected TODO
    { /* 106 */ "Vines",                  		0x76AB2F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15, 8, 0x00, BLF_BILLBOARD | BLF_FLATTEN_SMALL | BLF_PANE | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET },   //6a
    { /* 107 */ "Fence Gate",             		0xa88754, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },   // oddly, fence gates do not waterlog
    { /* 108 */ "Brick Stairs",           		0xa0807b, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 0, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },   //6c
    { /* 109 */ "Stone Brick Stairs",     		0x797979, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 3, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },   //6d
    { /* 110 */ "Mycelium",               		0x685d69, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 4, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //6e 13,4 side, 2,0 bottom
    { /* 111 */ "Lily Pad",               		0x217F30, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12, 4, 0x00, BLF_FLATTEN | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET },   //6f
    { /* 112 */ "Nether Brick",           		0x32171c, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,14, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //70/112
    { /* 113 */ "Nether Brick Fence",     		0x241316, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,14, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },   //71
    { /* 114 */ "Nether Brick Stairs",    		0x32171c, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,14, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },   //72
    { /* 115 */ "Nether Wart",            		0x81080a, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,14, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },   //73
    { /* 116 */ "Enchanting Table",       		0xa6701a, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,10, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE},   //74 6,11 side, 7,11 under
    { /* 117 */ "Brewing Stand",          		0x77692e, 1.000f, 0xff7711, 0xff7711, 0.12345f,  /* bogus */ 13, 9, 0x00, BLF_MIDDLER | BLF_TRUE_GEOMETRY | BLF_CUTOUTS | BLF_EMITTER },   //75 13,8 base - no BLF_IMAGE_TEXTURE
    { /* 118 */ "Cauldron",               		0x3b3b3b, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10, 8, 0x0c, BLF_ALMOST_WHOLE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_EMITTER },   //76 - 10,8 top (inside's better), 10,9 side, 11,9 feet TODO: not really whole
    { /* 119 */ "End Portal",             		0x0c0b0b, 0.7f,   0xff7711, 0xff7711, 0.12345f,   8,11, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_EMITTER },   //77 - not really whole, no real texture, make it a portal
    { /* 120 */ "End Portal Frame",       		0x3e6c60, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 9, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },   //78 15,9 side, 15,10 bottom
    { /* 121 */ "End Stone",              		0xdadca6, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15,10, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //79
    { /* 122 */ "Dragon Egg",             		0x1b1729, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,10, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_EMITTER },    //7A - not really whole
    { /* 123 */ "Redstone Lamp (inactive)",		0x9F6D4D, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3,13, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },   //7b
    { /* 124 */ "Redstone Lamp (active)", 		0xf9d49c, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,13, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },    //7c
    { /* 125 */ "Double Oak Slab",		  		0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 126 */ "Oak Slab",				 		0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //7e
    { /* 127 */ "Cocoa"      ,            		0xBE742D, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8,10, 0x00, BLF_SMALL_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },    //7f/127
    { /* 128 */ "Sandstone Stairs",       		0xe0d8a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,11, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //80/128
    { /* 129 */ "Emerald Ore",            		0x7D8E81, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11,10, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },    //81
    { /* 130 */ "Ender Chest",            		0x293A3C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,13, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG | BLF_EMITTER },    //82 - don't really have tiles for this one, added to terrainExt.png
    { /* 131 */ "Tripwire Hook",          		0xC79F63, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,10, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_FLATTEN_SMALL | BLF_DNE_FLUID },    //83 - decal
    { /* 132 */ "Tripwire",               		0x010101, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,10, 0x00, BLF_NONE | BLF_DNE_FLUID},    //84 - sorta redwire decal, 0x00FF, But really it should be invisible, so BLF_NONE. Color 0x8F8F8F
    // alternate { /* 130 */ "Tripwire",               0x8F8F8F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,10, 0x0F, BLF_FLATTOP|BLF_CUTOUTS},    //84 - sorta redwire decal, but really it should be invisible, so BLF_NONE. Color 0x8F8F8F
    { /* 133 */ "Block of Emerald",       		0x53D778, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,14, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },    //85
    { /* 134 */ "Spruce Stairs",          		0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,12, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //86
    { /* 135 */ "Birch Stairs",           		0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,13, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //87
    { /* 136 */ "Jungle Stairs",          		0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,12, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //88
    { /* 137 */ "Command Block",          		0xB28B79, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,24, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	//89
    { /* 138 */ "Beacon",                 		0x9CF2ED, 0.800f, 0xff7711, 0xff7711, 0.12345f,  11,14, 0x00, BLF_ALMOST_WHOLE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_EMITTER },	//8A/138 - it's a whole block sorta, it doesn't attach to fences or block wires
    { /* 139 */ "Cobblestone Wall",       		0x828282, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 1, 0x1f, BLF_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },   //8B
    { /* 140 */ "Flower Pot",             		0x7C4536, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,11, 0xff, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },   //8C
    { /* 141 */ "Carrot",                 		0x056B05, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11,12, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },	//8d/141
    { /* 142 */ "Potato",                 		0x00C01B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  15,12, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },	//8e/142
    { /* 143 */ "Oak Button",             		0x9f8150, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4, 0, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//8f/143
    { /* 144 */ "Mob Head",	              		0xcacaca, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 6, 0x80, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//90/144 - TODO; we use a pumpkin for now...
    { /* 145 */ "Anvil",                  		0x404040, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,13, 0x0c, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT},    // 91/145 - NOTE: the top swatch is not used, the generic side swatch is
    // 1.6
    { /* 146 */ "Trapped Chest",          		0xa06f23, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9, 1, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE | BLF_MAYWATERLOG },	// 92/146
    { /* 147 */ "Light Weighted Pressure Plate",0xEFE140, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7, 1, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	// 93/147 gold
    { /* 148 */ "Heavy Weighted Pressure Plate",0xD7D7D7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 1, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	// 94/148 iron
    { /* 149 */ "Redstone Comparator",    		0xC5BAAD, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,14, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE },   // 95/149 TODO from repeater off
    { /* 150 */ "Redstone Comparator (deprecated)",0xD1B5AA,1.0f, 0xff7711, 0xff7711, 0.12345f,  15,14, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE },   // 96/150 TODO from repeater on
    { /* 151 */ "Daylight Sensor",        		0xBBA890, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,15, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },   // 97/151 TODO from trapdoor
    { /* 152 */ "Block of Redstone",      		0xA81E09, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,15, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_CONNECTS_REDSTONE },	// 98/152
    { /* 153 */ "Nether Quartz Ore",      		0x7A5B57, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8,17, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR }, // 99/153
    { /* 154 */ "Hopper",                 		0x363636, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,15, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT },    // 9A/154 - note that 3d print version is simpler, no indentation, so it's thick enough
    { /* 155 */ "Block of Quartz",        		0xE0DDD7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,17, 0x05, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// 9B/155
    { /* 156 */ "Quartz Stairs",          		0xE1DCD1, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,17, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    // 9C/156
    { /* 157 */ "Activator Rail",         		0x880300, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,17, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_3D_BIT | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_MAYWATERLOG },	// 9D/157
    { /* 158 */ "Dropper",                		0x6E6E6E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14, 3, 0x00, BLF_WHOLE },	// 9E/158
    { /* 159 */ "Colored Terracotta",           0xCEAE9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,16, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// 9F/159
    { /* 160 */ "Stained Glass Pane",     		0xEFEFEF, 0.500f, 0xff7711, 0xff7711, 0.12345f,   0,20, 0x0f, BLF_PANE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_TRANSPARENT | BLF_MAYWATERLOG },	// A0/160 - semitransparent, not a cutout like glass panes are
    { /* 161 */ "Acacia Leaves",				0x6fac2c, 1.000f, 0xff7711, 0xff7711, 0.12345f,  11,19, 0x03, BLF_WHOLE | BLF_CUTOUTS | BLF_LEAF_PART | BLF_MAYWATERLOG },	//A1/161
    { /* 162 */ "Acacia Log", 					0x766F64, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,19, 0x13, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },	//A2/162
    { /* 163 */ "Acacia Stairs",          		0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //A3
    { /* 164 */ "Dark Oak Stairs",        		0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1,22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },    //A4/164
    { /* 165 */ "Slime Block",            		0x787878, 0.700f, 0xff7711, 0xff7711, 0.12345f,   3,22, 0x00, BLF_WHOLE | BLF_TRUE_GEOMETRY | BLF_TRANSPARENT | BLF_FENCE_NEIGHBOR },	// A5/165 - 1.8
    { /* 166 */ "Barrier",                		0xE30000, 0.000f, 0xff7711, 0xff7711, 0.12345f,  14,25, 0x00, BLF_WHOLE | BLF_CUTOUTS | BLF_MAYWATERLOG },	// A6/166 - 1.8 - to make visible, set alpha to 1.0, 1.20.2 - in creative may now waterlog
    { /* 167 */ "Iron Trapdoor",          		0xC0C0C0, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2,22, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },	// A7/167 - 1.8
    { /* 168 */ "Prismarine",             		0x66ADA1, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,22, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// 1.8 add
    { /* 169 */ "Sea Lantern",            		0xD3DBD3, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,22, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },   //59
    { /* 170 */ "Hay Bale",               		0xB5970C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  10,15, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// AA/170
    { /* 171 */ "Carpet",                 		0xEAEDED, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0, 4, 0x1f, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// AB/171
    { /* 172 */ "Terracotta",	         		0x945A41, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,17, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// same as 159, except that it doesn't have names
    { /* 173 */ "Block of Coal",          		0x191919, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,14, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR},	// AD/173
    // 1.7
    { /* 174 */ "Packed Ice",             		0x7dacfe, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,17, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// AE/174 - like ice, but not transparent, and a fence neighbor
    { /* 175 */ "Sunflower",					0x8cbd57, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,18, 0x07, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },	// AF/175 - note color is used to multiply grayscale textures, so don't change it
    { /* 176 */ "Banner",						0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 177 */ "Wall Banner",					0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 178 */ "Inverted Daylight Sensor",		0xBBA890, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,22, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 179 */ "Red Sandstone",          		0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,19, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 180 */ "Red Sandstone Stairs",   		0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,19, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 181 */ "Double Red Sandstone Slab",	0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,19, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 182 */ "Red Sandstone Slab",     		0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,19, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },	//2c/44
    { /* 183 */ "Spruce Fence Gate",      		0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,12, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 184 */ "Birch Fence Gate",       		0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,13, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 185 */ "Jungle Fence Gate",      		0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,12, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 186 */ "Dark Oak Fence Gate",    		0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1,22, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 187 */ "Acacia Fence Gate",      		0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,22, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 188 */ "Spruce Fence",           		0x785836, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,12, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 189 */ "Birch Fence",            		0xD7C185, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6,13, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 190 */ "Jungle Fence",           		0xB1805C, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,12, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 191 */ "Dark Oak Fence",         		0x492F17, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1,22, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 192 */ "Acacia Fence",           		0xBA683B, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,22, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 193 */ "Spruce Door",            		0x7A5A36, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1,23, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 194 */ "Birch Door",             		0xD6CA8C, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3,23, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 195 */ "Jungle Door",            		0xB2825E, 1.000f, 0xff7711, 0xff7711, 0.12345f,   5,23, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 196 */ "Acacia Door",            		0xB16640, 1.000f, 0xff7711, 0xff7711, 0.12345f,   7,23, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 197 */ "Dark Oak Door",          		0x51341A, 1.000f, 0xff7711, 0xff7711, 0.12345f,   9,23, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },	// yes, for this one dark oak really does go after acacia
    { /* 198 */ "End Rod",                		0xE0CFD0, 1.000f, 0xff7711, 0xff7711, 0.12345f,  12,23, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_DNE_FLUID | BLF_EMITTER },
    { /* 199 */ "Chorus Plant",           		0x654765, 1.000f, 0xff7711, 0xff7711, 0.12345f,  13,23, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 200 */ "Chorus Flower",          		0x937E93, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,23, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 201 */ "Purpur Block",           		0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,24, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 202 */ "Purpur Pillar",          		0xAC82AC, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2,24, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 203 */ "Purpur Stairs",          		0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,24, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 204 */ "Double Purpur Slab",     		0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,24, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 205 */ "Purpur Slab",            		0xA77BA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,24, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 206 */ "End Stone Bricks",       		0xE2E8AC, 1.000f, 0xff7711, 0xff7711, 0.12345f,   3,24, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 207 */ "Beetroot Seeds",         		0x6D7F44, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,24, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 208 */ "Dirt Path",             		0x977E48, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8,24, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },    // in 1.16 was Grass Path
    { /* 209 */ "End Gateway",            		0x1A1828, 1.000f, 0xff7711, 0xff7711, 0.12345f,   8,11, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 210 */ "Repeating Command Block",		0x8577B2, 1.000f, 0xff7711, 0xff7711, 0.12345f,  14,24, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 211 */ "Chain Command Block",    		0x8AA59A, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2,25, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 212 */ "Frosted Ice",            		0x81AFFF, 0.613f, 0xff7711, 0xff7711, 0.12345f,   6,25, 0x00, BLF_WHOLE | BLF_TRANSPARENT },
    { /* 213 */ "Magma Block",            		0x9D4E1D, 1.000f, 0xff7711, 0xff7711, 0.12345f,   0,26, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 214 */ "Nether Wart Block",      		0x770C0D, 1.000f, 0xff7711, 0xff7711, 0.12345f,   1,26, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 215 */ "Red Nether Bricks",       		0x470709, 1.000f, 0xff7711, 0xff7711, 0.12345f,   2,26, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 216 */ "Bone Block",             		0xE1DDC9, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,26, 0x13, BLF_WHOLE | BLF_FENCE_NEIGHBOR }, // top location; side is previous tile
    { /* 217 */ "Structure Void",        		0xff0000, 0.000f, 0xff7711, 0xff7711, 0.12345f,   1, 8, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// uses red wool TODO - invisible by default
    { /* 218 */ "Observer",				  		0x6E6E6E, 1.000f, 0xff7711, 0xff7711, 0.12345f,   4,33, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 219 */ "White Shulker Box",      		0xD8DDDE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 27, 0x00, BLF_WHOLE },
    { /* 220 */ "Orange Shulker Box",     		0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 27, 0x00, BLF_WHOLE },
    { /* 221 */ "Magenta Shulker Box",    		0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 27, 0x00, BLF_WHOLE },
    { /* 222 */ "Light Blue Shulker Box", 		0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 27, 0x00, BLF_WHOLE },
    { /* 223 */ "Yellow Shulker Box",     		0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 27, 0x00, BLF_WHOLE },
    { /* 224 */ "Lime Shulker Box",       		0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 27, 0x00, BLF_WHOLE },
    { /* 225 */ "Pink Shulker Box",       		0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 27, 0x00, BLF_WHOLE },
    { /* 226 */ "Gray Shulker Box",       		0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 27, 0x00, BLF_WHOLE },
    { /* 227 */ "Light Grey Shulker Box", 		0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 27, 0x00, BLF_WHOLE },
    { /* 228 */ "Cyan Shulker Box",       		0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 27, 0x00, BLF_WHOLE },
    { /* 229 */ "Purple Shulker Box",     		0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 27, 0x00, BLF_WHOLE },
    { /* 230 */ "Blue Shulker Box",       		0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 27, 0x00, BLF_WHOLE },
    { /* 231 */ "Brown Shulker Box",      		0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 27, 0x00, BLF_WHOLE },
    { /* 232 */ "Green Shulker Box",      		0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 27, 0x00, BLF_WHOLE },
    { /* 233 */ "Red Shulker Box",        		0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 27, 0x00, BLF_WHOLE },
    { /* 234 */ "Black Shulker Box",      		0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 27, 0x00, BLF_WHOLE },
    { /* 235 */ "White Glazed Terracotta",      0xD4DBD7, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 236 */ "Orange Glazed Terracotta",     0xC09A7F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 237 */ "Magenta Glazed Terracotta",    0xD26FC1, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 238 */ "Light Blue Glazed Terracotta", 0x80B3D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 239 */ "Yellow Glazed Terracotta",     0xEDC671, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 240 */ "Lime Glazed Terracotta",       0xB0C84F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 241 */ "Pink Glazed Terracotta",       0xEC9EB7, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 242 */ "Gray Glazed Terracotta",       0x5B6164, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 243 */ "Light Grey Glazed Terracotta", 0x9FACAD, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 244 */ "Cyan Glazed Terracotta",       0x4F8288, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 245 */ "Purple Glazed Terracotta",     0x7633A5, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 246 */ "Blue Glazed Terracotta",       0x324D98, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 247 */ "Brown Glazed Terracotta",      0x896E60, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 248 */ "Green Glazed Terracotta",      0x7F9563, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 249 */ "Red Glazed Terracotta",        0xB8433B, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 250 */ "Black Glazed Terracotta",      0x592225, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 28, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 251 */ "Concrete",               		0xCFD5D6, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 29, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 252 */ "Concrete Powder",        		0xE2E4E4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 30, 0x0f, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 253 */ "Unknown Block",          		0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1,  1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// same as bedrock
    { /* 254 */ "Unknown Block",          		0x565656, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1,  1, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// same as bedrock - BLOCK_FAKE is used here
    { /* 255 */ "Structure Block",        		0x665E5F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 25, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },

    // just to be safe, we don't use 256 and consider it AIR
    //			 name                           read_color ralpha color     prem-clr  alpha,   txX,  Y,  mtl, flags
    { /* 256 */ "Air",                    		0x000000, 0.000f, 0xff7711, 0xff7711, 0.12345f, 13, 14, 0x00, BLF_NONE },
    { /* 257 */ "Prismarine Stairs",      		0x66ADA1, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 258 */ "Prismarine Brick Stairs",		0x68A495, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 259 */ "Dark Prismarine Stairs", 		0x355F4E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 260 */ "Spruce Trapdoor",        		0x674E34, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 34, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 261 */ "Birch Trapdoor",         		0xD3C8A8, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 34, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 262 */ "Jungle Trapdoor",        		0x9D7250, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 34, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 263 */ "Acacia Trapdoor",        		0xA05936, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 34, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 264 */ "Dark Oak Trapdoor",      		0x4E3318, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 35, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 265 */ "Spruce Button",          		0x6B5030, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 12, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 266 */ "Birch Button",           		0x9E7250, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 13, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 267 */ "Jungle Button",          		0xC5B57C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 12, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 268 */ "Acacia Button",          		0xAB5D34, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 22, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 269 */ "Dark Oak Button",        		0x3F2813, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 22, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 270 */ "Spruce Pressure Plate",  		0x6B5030, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 12, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	//48
    { /* 271 */ "Birch Pressure Plate",   		0x9E7250, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 13, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	//48
    { /* 272 */ "Jungle Pressure Plate",  		0xC5B57C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 12, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	//48
    { /* 273 */ "Acacia Pressure Plate",  		0xAB5D34, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 22, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	//48
    { /* 274 */ "Dark Oak Pressure Plate",		0x3F2813, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 22, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },	//48
    { /* 275 */ "Stripped Oak Log",       		0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 34, 0x03, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 276 */ "Stripped Acacia Log",    		0xA85C3B, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 34, 0x03, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 277 */ "Stripped Oak Wood",      		0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 34, 0x03, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 278 */ "Stripped Acacia Wood",   		0xAF5D3C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 34, 0x03, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 279 */ "Orange Banner",          		0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 280 */ "Magenta Banner",         		0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 281 */ "Light Blue Banner",      		0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 282 */ "Yellow Banner",          		0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 283 */ "Lime Banner",            		0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 284 */ "Pink Banner",            		0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 285 */ "Gray Banner",            		0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 286 */ "Light Gray Banner",      		0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 287 */ "Cyan Banner",            		0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 288 */ "Purple Banner",          		0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 289 */ "Blue Banner",            		0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 290 */ "Brown Banner",           		0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 291 */ "Green Banner",           		0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 292 */ "Red Banner",             		0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 293 */ "Black Banner",           		0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY },	// assumed to be like signs in properties, but cannot 3D print (too darn thin)
    { /* 294 */ "Orange Wall Banner",     		0xEB6B0B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 295 */ "Magenta Wall Banner",    		0xAE37A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 296 */ "Light Blue Wall Banner", 		0x32A5D4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 297 */ "Yellow Wall Banner",     		0xF8BD1E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 298 */ "Lime Wall Banner",       		0x65AE17, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 299 */ "Pink Wall Banner",       		0xE77B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 300 */ "Gray Wall Banner",       		0x383B3F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 301 */ "Light Gray Wall Banner", 		0x7E7E75, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 302 */ "Cyan Wall Banner",       		0x147A88, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 303 */ "Purple Wall Banner",     		0x8C618C, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 304 */ "Blue Wall Banner",       		0x2C2E8D, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 305 */ "Brown Wall Banner",      		0x6B4224, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 306 */ "Green Wall Banner",      		0x4F6520, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 307 */ "Red Wall Banner",        		0x8E201F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 308 */ "Black Wall Banner",      		0x1A1A1E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 29, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	// BLF_FLATSIDE removed - too tricky to do, since it spans two block, here and below TODO
    { /* 309 */ "Tall Seagrass",          		0x30790E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 33, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_WATERLOG },
    { /* 310 */ "Seagrass",              		0x3D8B17, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 33, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_WATERLOG },
    { /* 311 */ "Smooth Stone",           		0xA0A0A0, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 23, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 312 */ "Blue Ice",               		0x75A8FD, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 33, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },	// like ice, but not transparent, and a fence neighbor
    { /* 313 */ "Dried Kelp Block",       		0x414534, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 33, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 314 */ "Kelp", /* plant == 0 */  		0x58912E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 33, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_WATERLOG },
    { /* 315 */ "Coral Block",            		0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 35, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 316 */ "Dead Coral Block",       		0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 35, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 317 */ "Coral",                  		0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 35, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 318 */ "Coral Fan",              		0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 35, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 319 */ "Dead Coral Fan",        		0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 36, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 320 */ "Coral Wall Fan",         		0x3257CA, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 35, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 321 */ "Dead Coral Wall Fan",    		0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 36, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 322 */ "Conduit",                		0xA5927A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 36, 0x00, BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG | BLF_EMITTER },
    { /* 323 */ "Sea Pickle",             		0x616B3B, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 33, 0x03, BLF_SMALL_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_DNE_FLUID | BLF_3D_BIT | BLF_MAYWATERLOG | BLF_EMITTER },
    { /* 324 */ "Turtle Egg",             		0xEAE4C2, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 36, 0x00, BLF_SMALL_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_DNE_FLUID | BLF_3D_BIT },
    // 1.14
    { /* 325 */ "Dead Coral",             		0x857E79, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 36, 0x07, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 326 */ "Standing Sign",          		0xA95B33, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 22, 0x30, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },	// acacia and dark oak, sigh
    { /* 327 */ "Sweet Berry Bush",       		0x32613c, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 37, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID }, // does not stop fluid
    { /* 328 */ "Bamboo",                 		0x619324, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 37, 0x00, BLF_SMALL_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY },
    { /* 329 */ "Double Andesite Slab",   		0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 22, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 330 */ "Andesite Slab",          		0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 22, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 331 */ "Jigsaw",			      		0x665E5F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 39, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 332 */ "Composter",              		0x774C27, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 38, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 333 */ "Barrel",                 		0x86643B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 38, 0x00, BLF_WHOLE },
    { /* 334 */ "Stone Cutter",			  		0x7D7975, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 41, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_CUTOUTS | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 335 */ "Grindstone",             		0x8E8E8E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 39, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 336 */ "Lectern",                		0xAF8B55, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 40, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 337 */ "Bell",					  		0xC69E36, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 38, 0x00, BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 338 */ "Lantern",				  		0x846C5A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 37, 0x02, BLF_SMALL_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_EMITTER | BLF_MAYWATERLOG },
    { /* 339 */ "Campfire",               		0xE0B263, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 39, 0x0c, BLF_FLATTEN | BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_EMITTER | BLF_MAYWATERLOG },
    { /* 340 */ "Scaffolding",			  		0xB38D54, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 40, 0x00, BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_MAYWATERLOG },
    // 1.15
    { /* 341 */ "Bee Nest",              		0xD2AD51, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 41, 0x20, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 342 */ "Honey Block",           		0xFBBB41, 0.750f, 0xff7711, 0xff7711, 0.12345f,  3, 42, 0x00, BLF_WHOLE | BLF_TRUE_GEOMETRY | BLF_TRANSPARENT }, // the semi-transparent one
    { /* 343 */ "Honeycomb Block",         		0xE69B35, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 42, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    // 1.16
    { /* 344 */ "Crying Obsidian",         		0x32115C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 45, 0x01, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 345 */ "Respawn Anchor",         		0x5F0EC4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 45, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 346 */ "Crimson Trapdoor",      		0x6C344A, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 43, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 347 */ "Warped Trapdoor",      		0x307B74, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 44, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 348 */ "Crimson Button",          		0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//4d
    { /* 349 */ "Warped Button",          		0x2D6D68, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 44, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//4d
    { /* 350 */ "Polished Blackstone Button",   0x37333D, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 46, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//4d
    { /* 351 */ "Crimson Fence",           		0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 352 */ "Warped Fence",           		0x2D6D68, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 44, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 353 */ "Crimson Fence Gate",      		0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 354 */ "Warped Fence Gate",      		0x2D6D68, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 44, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 355 */ "Crimson Door",            		0x773851, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 43, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 356 */ "Warped Door",            		0x2F817A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 44, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 357 */ "Crimson Pressure Plate",  		0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 358 */ "Warped Pressure Plate",  		0x2D6D68, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 44, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 359 */ "Polished Blackstone Pressure Plate",0x37333D,1.000f,0xff7711,0xff7711,0.12345f, 4, 46, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 360 */ "Double Crimson Slab",   		0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 361 */ "Crimson Slab",				    0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x07, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 362 */ "Soul Torch",					0x64B4B7, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 42, 0x00, BLF_MIDDLER | BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_EMITTER | BLF_DNE_FLUID },	// should be BLF_EMITTER, flatten torches only if sides get flattened, too
    { /* 363 */ "Weeping Vines",		  		0x8D211A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 43, 0x23, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },    // hanging roots may waterlog
    { /* 364 */ "Chain",		          		0x3E4453, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 46, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_CUTOUTS | BLF_MAYWATERLOG },    // removed | BLF_FLATTEN_SMALL - seemed debatable
    { /* 365 */ "Stone Stairs",          		0x7C7C7C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1,  0, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 366 */ "Granite Stairs",          		0xA77562, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 367 */ "Polished Granite Stairs",      0x946251, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 368 */ "Smooth Quartz Stairs",         0xE0DDD7, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 17, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 369 */ "Diorite Stairs",          		0x9B9B9E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 370 */ "Polished Diorite Stairs",      0xC9C9CD, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 371 */ "End Stone Brick Stairs",       0xDBE2A4, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 24, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 372 */ "Andesite Stairs",          	0x7F7F83, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 373 */ "Polished Andesite Stairs",     0x7F7F84, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 22, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 374 */ "Red Nether Brick Stairs",      0x470709, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 26, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 375 */ "Mossy Stone Brick Stairs",     0x767B6E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4,  6, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 376 */ "Mossy Cobblestone Stairs",     0x627162, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4,  2, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 377 */ "Smooth Sandstone Stairs",      0xe0d8a6, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 11, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 378 */ "Smooth Red Sandstone Stairs",  0x964C19, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 19, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 379 */ "Crimson Stairs",          		0x693249, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 43, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 380 */ "Warped Stairs",          		0x2D6D68, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 44, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 381 */ "Blackstone Stairs",          	0x2D282F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 46, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 382 */ "Polished Blackstone Stairs",   0x37333D, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 46, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 383 */ "Polished Blackstone Brick Stairs",0x322E36,1.000f,0xff7711,0xff7711, 0.12345f,  5, 46, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    // 1.17
    { /* 384 */ "Candle",                  	    0xF0C997, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 46, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 385 */ "Lit Candle",                  	0xFFFAF1, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 46, 0x30, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_EMITTER },    // note lit candles should never waterlog, as they'd go out
    { /* 386 */ "White Candle",            		0xD3DCDC, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 47, 0x0f, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 387 */ "Lit White Candle",            	0xF9E6B6, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 48, 0x30, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_EMITTER },    // note lit candles should never waterlog, as they'd go out
    { /* 388 */ "Block of Amethyst",            0x8C67C2, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 49, 0x3f, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 389 */ "Small Amethyst Bud",   		0xA982CD, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 49, 0x03, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_EMITTER | BLF_MAYWATERLOG },    // could be a BLF_BILLBOARD?
    { /* 390 */ "Pointed Dripstone",            0x846A5B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 49, 0x00, BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_MAYWATERLOG },	// NODO: could maybe make some true geometry instead for 3D printing, but it's iffy and a lot of work
    { /* 391 */ "Cut Copper Stairs",            0xC16D53, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 392 */ "Exposed Cut Copper Stairs",    0x9E7B67, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 393 */ "Weathered Cut Copper Stairs",  0x6F936E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 394 */ "Oxidized Cut Copper Stairs",   0x529D81, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 395 */ "Lightning Rod",                0xC67155, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 50, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_DNE_FLUID | BLF_CONNECTS_REDSTONE | BLF_MAYWATERLOG },
    { /* 396 */ "(unused)" /* "Flower Pot" */,  0x7C4536, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 11, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },   //8C
    { /* 397 */ "Double Cut Copper Slab",   	0xC16D53, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 50, 0x17, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_LAME_WATERLOG | BLF_MAYWATERLOG }, // bizarrely, may waterlog, though pretty pointless; important for instancing
    { /* 398 */ "Cut Copper Slab",              0xC16D53, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 50, 0x17, BLF_HALF | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },	//2c/44 - was 6,0, and 5,0 side
    { /* 399 */ "Waxed Cut Copper Stairs",      0xC16D53, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 400 */ "(unused)" /* "Mob Head" */,    0xcacaca, 1.000f, 0xff7711, 0xff7711, 0.12345f,   6, 6, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },	//90/144 - TODO; we use a pumpkin for now...
    { /* 401 */ "Waxed Exposed Cut Copper Stairs",    0x9E7B67, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 402 */ "Waxed Weathered Cut Copper Stairs",  0x6F936E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 403 */ "Waxed Oxidized Cut Copper Stairs",   0x529D81, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 50, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 404 */ "Cave Vines",                   0x5D722A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 50, 0x01, BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 405 */ "Cave Vines lit", /*berries*/   0x73762C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 51, 0x01, BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_EMITTER },  // note that "lit" has to be there, as it needs to be a different name than above
    { /* 406 */ "Spore Blossom",                0xD566A5, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 51, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 407 */ "Azalea",		    			0x677E30, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 51, 0x01, BLF_ALMOST_WHOLE | BLF_BILLBOARD | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 408 */ "Big Dripleaf",					0x739234, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 52, 0x01, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 409 */ "Small Dripleaf",				0x617A2E, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 52, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 410 */ "Glow Lichen",           		0x72847A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 52, 0x00, BLF_BILLBOARD | BLF_FLATTEN_SMALL | BLF_PANE | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_EMITTER | BLF_MAYWATERLOG },
    { /* 411 */ "Sculk Sensor",            		0x144856, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 52, 0x04, BLF_HALF | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE | BLF_EMITTER | BLF_MAYWATERLOG },
    { /* 412 */ "Cobbled Deepslate Stairs",     0x515153, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 53, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 413 */ "Polished Deepslate Stairs",    0x4C4C4C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 53, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 414 */ "Deepslate Brick Stairs",       0x4B4B4B, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 53, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 415 */ "Deepslate Tile Stairs",        0x39393A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 53, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 416 */ "Mangrove Log",          		0x68342B, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 54, 0x11, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 417 */ "Decorated Pot",                0x905142, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 61, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },
    { /* 418 */ "Mangrove Door",            	0x723430, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 54, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 419 */ "Mangrove Trapdoor",      		0x70332C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 55, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 420 */ "Mangrove Propagule",           0x63B156, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 55, 0x00, BLF_FLATTEN | BLF_SMALL_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 421 */ "Mangrove Roots",				0x4C3D27, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 55, 0x00, BLF_WHOLE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY },
    { /* 422 */ "Muddy Mangrove Roots",		    0x473C2F, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 55, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 423 */ "Stripped Mangrove Log",   	    0x6D2D2C, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 54, 0x01, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 424 */ "Stripped Mangrove Wood",   	0x783730, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 54, 0x01, BLF_WHOLE | BLF_TRUNK_PART | BLF_FENCE_NEIGHBOR },
    { /* 425 */ "Mangrove Stairs",              0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 426 */ "Mud Brick Stairs",             0x8B6950, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 55, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 427 */ "Mangrove Sign",          		0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x30, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },
    { /* 428 */ "Mangrove Wall Sign",           0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x18, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 429 */ "Mangrove Pressure Plate",   	0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 430 */ "Mangrove Button",        		0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 431 */ "Mangrove Fence",           	0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 432 */ "Mangrove Fence Gate",      	0x773932, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 55, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 433 */ "Sculk Shrieker",              	0xC7CDAA, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 56, 0x00, BLF_ALMOST_WHOLE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 434 */ "Sculk Vein",           		0x0B3C45, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 56, 0x00, BLF_BILLBOARD | BLF_FLATTEN_SMALL | BLF_PANE | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET | BLF_MAYWATERLOG },
    { /* 435 */ "Frogspawn",               		0x4D5581, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 54, 0x00, BLF_FLATTEN | BLF_CUTOUTS | BLF_DNE_FLUID | BLF_OFFSET },
    { /* 436 */ "Ochre Froglight",         		0xFBF6D2, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 55, 0x03, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 437 */ "Mangrove Leaves",				0x6fac2c, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 54, 0x01, BLF_WHOLE | BLF_CUTOUTS | BLF_LEAF_PART | BLF_MAYWATERLOG },
        // 1.20, though I snuck Decorated Pot in a slot above
    { /* 438 */ "Cherry Button",                0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 57, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 439 */ "Cherry Door",                  0xE0ACA7, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 57, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 440 */ "Cherry Fence",                 0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 57, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 441 */ "Cherry Fence Gate",            0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 57, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE },
    { /* 442 */ "Cherry Pressure Plate",        0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 57, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 443 */ "Cherry Stairs",                0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 57, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 444 */ "Cherry Trapdoor",              0xE3B4AE, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 57, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 445 */ "Bamboo Button",                0xC4AF52, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 60, 0x00, BLF_FLATTEN_SMALL | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID },
    { /* 446 */ "Bamboo Door",                  0xC2AE53, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 60, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 447 */ "Bamboo Fence",                 0xD0BC5B, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 60, 0x00, BLF_MIDDLER | BLF_FENCE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 448 */ "Bamboo Fence Gate",            0xD1BC5A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 60, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FENCE_GATE | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE }, // TODOTODO someday use 10, 60 for bamboo fence gate texture
    { /* 449 */ "Bamboo Pressure Plate",        0xC4AF52, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 60, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_CONNECTS_REDSTONE },
    { /* 450 */ "Bamboo Stairs",                0xC4AF52, 1.000f, 0xff7711, 0xff7711, 0.12345f, 14, 60, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 451 */ "Bamboo Trapdoor",              0xC9B560, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 60, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 452 */ "Bamboo Mosaic Stairs",         0xC0AC4F, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 60, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 453 */ "Pink Petals",              	0xF7B9DC, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 57, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 454 */ "Pitcher Crop",					0x7D9BC2, 1.000f, 0xff7711, 0xff7711, 0.12345f,  5, 58, 0x00, BLF_FLATTEN | BLF_TRUE_GEOMETRY | BLF_BILLBOARD | BLF_3D_BIT | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 455 */ "Sniffer Egg",            		0xBC4E3A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 58, 0x00, BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 456 */ "Suspicious Gravel",            0x837F7E, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 59, 0x04, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 457 */ "Torchflower Crop",				0xDE8B25, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 60, 0x00, BLF_FLATTEN | BLF_BILLBOARD | BLF_CUTOUTS | BLF_DNE_FLUID },
    { /* 458 */ "Oak Wall Hanging Sign",        0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 34, 0x3c, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 459 */ "Oak Hanging Sign",             0xB29157, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 34, 0x20, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 460 */ "Birch Hanging Sign",           0xC5B077, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 34, 0x20, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 461 */ "Acacia Hanging Sign",          0xAF5D3C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  4, 34, 0x20, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 462 */ "Crimson Hanging Sign",         0x8A3A5A, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 43, 0x20, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 463 */ "Mangrove Hanging Sign",        0x783730, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 54, 0x20, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 464 */ "Bamboo Hanging Sign",          0xC4AF52, 1.000f, 0xff7711, 0xff7711, 0.12345f, 11, 60, 0x00, BLF_SMALL_MIDDLER | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG },
    { /* 465 */ "Trial Spawner",           		0x767677, 1.000f, 0xff7711, 0xff7711, 0.12345f, 13, 64, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 466 */ "Vault",           		        0x404E56, 1.000f, 0xff7711, 0xff7711, 0.12345f, 12, 65, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },
    { /* 467 */ "Crafter",           		    0x777272, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 62, 0x00, BLF_WHOLE | BLF_FENCE_NEIGHBOR },
    { /* 468 */ "Heavy Core",              		0x5B606A, 1.000f, 0xff7711, 0xff7711, 0.12345f,  9, 63, 0x00, BLF_MIDDLER | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_DNE_FLUID | BLF_MAYWATERLOG | BLF_EMITTER },
    { /* 469 */ "Copper Bulb",           		0xA81E09, 1.000f, 0xff7711, 0xff7711, 0.12345f, 10, 61, 0x0F, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_EMITTER },   // note that 0x8 is included, for lit
    { /* 470 */ "Copper Grate",                 0xC26D51, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 62, 0x07, BLF_WHOLE | BLF_FENCE_NEIGHBOR | BLF_CUTOUTS | BLF_MAYWATERLOG },
    { /* 471 */ "Tuff Stairs",                  0x6F6F69, 1.000f, 0xff7711, 0xff7711, 0.12345f,  7, 49, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 472 */ "Polished Tuff Stairs",         0x636965, 1.000f, 0xff7711, 0xff7711, 0.12345f,  3, 64, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 473 */ "Tuff Brick Stairs",            0x656962, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 64, 0x00, BLF_STAIRS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_3D_BIT_GLUE | BLF_MAYWATERLOG },
    { /* 474 */ "Copper Trapdoor",      		0xC16C52, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 62, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 475 */ "Exposed Copper Trapdoor",      0xA47E6A, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 63, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 476 */ "Weathered Copper Trapdoor",    0x6E9B72, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 66, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 477 */ "Oxidized Copper Trapdoor",     0x55A486, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 64, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 478 */ "Waxed Copper Trapdoor",      	0xC16C52, 1.000f, 0xff7711, 0xff7711, 0.12345f,  1, 62, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 479 */ "Waxed Exposed Copper Trapdoor",0xA47E6A, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 63, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 480 */ "Waxed Weathered Copper Trapdoor",0x6E9B72, 1.000f, 0xff7711, 0xff7711, 0.12345f,  8, 66, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 481 */ "Waxed Oxidized Copper Trapdoor",0x55A486, 1.000f, 0xff7711, 0xff7711, 0.12345f,  2, 64, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_FLATTEN | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT | BLF_MAYWATERLOG },   // tricky case: could be a flattop, or a flatside. For now, render it
    { /* 482 */ "Copper Door",      		    0xC36F55, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 61, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 483 */ "Exposed Copper Door",          0xA87C6C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 63, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 484 */ "Weathered Copper Door",        0x6F9972, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 66, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 485 */ "Oxidized Copper Door",         0x54A387, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 64, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 486 */ "Waxed Copper Door",      	    0xC36F55, 1.000f, 0xff7711, 0xff7711, 0.12345f, 15, 61, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },  // identical to above four, just names differ, ugh
    { /* 487 */ "Waxed Exposed Copper Door",    0xA87C6C, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 63, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 488 */ "Waxed Weathered Copper Door",  0x6F9972, 1.000f, 0xff7711, 0xff7711, 0.12345f,  6, 66, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },
    { /* 489 */ "Waxed Oxidized Copper Door",   0x54A387, 1.000f, 0xff7711, 0xff7711, 0.12345f,  0, 64, 0x00, BLF_MIDDLER | BLF_ENTRANCE | BLF_CUTOUTS | BLF_TRUE_GEOMETRY | BLF_3D_BIT },

    // Important note: 396 is skipped, it's the BLOCK_FLOWER_POT, also skip 400, BLOCK_HEAD. Nicer still would be to redo the code for those two blocks (and redo IDBlock() method) so that we don't use up all 8 bits
};

