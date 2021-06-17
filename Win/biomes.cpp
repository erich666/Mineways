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

#include "biomes.h"

Biome gBiomes[256] = {	// IMPORTANT: do not change 256 size here.
    //    ID    Name             Temperature, rainfall, grass, foliage colors
    //                                                  - note: the colors here are just placeholders, they are computed in the program
    { /*   0 */ "Ocean",					0.5f, 0.5f, 0x92BD59, 0x77AB2F },	// default values of temp and rain
    { /*   1 */ "Plains",					0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*   2 */ "Desert",					2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*   3 */ "Mountains",				0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /*   4 */ "Forest",					0.7f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*   5 */ "Taiga",				   0.25f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*   6 */ "Swamp",					0.8f, 0.9f, 0x92BD59, 0x77AB2F },
    { /*   7 */ "River",					0.5f, 0.5f, 0x92BD59, 0x77AB2F },	// default values of temp and rain
    { /*   8 */ "Nether",					2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*   9 */ "The End",					0.5f, 0.5f, 0x92BD59, 0x77AB2F },	// default values of temp and rain
    { /*  10 */ "Frozen Ocean",				0.0f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  11 */ "Frozen River",				0.0f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  12 */ "Snowy Tundra",				0.0f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  13 */ "Snowy Mountains",			0.0f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  14 */ "Mushroom Fields",			0.9f, 1.0f, 0x92BD59, 0x77AB2F },
    { /*  15 */ "Mushroom Field Shore",		0.9f, 1.0f, 0x92BD59, 0x77AB2F },
    { /*  16 */ "Beach",					0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  17 */ "Desert Hills",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  18 */ "Wooded Hills",				0.7f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  19 */ "Taiga Hills",			   0.25f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  20 */ "Mountain Edge",			0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /*  21 */ "Jungle",				   0.95f, 0.9f, 0x92BD59, 0x77AB2F },
    { /*  22 */ "Jungle Hills",			   0.95f, 0.9f, 0x92BD59, 0x77AB2F },
    { /*  23 */ "Jungle Edge",			   0.95f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  24 */ "Deep Ocean",				0.5f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  25 */ "Stone Shore",				0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /*  26 */ "Snowy Beach",			   0.05f, 0.3f, 0x92BD59, 0x77AB2F },
    { /*  27 */ "Birch Forest",				0.6f, 0.6f, 0x92BD59, 0x77AB2F },
    { /*  28 */ "Birch Forest Hills",		0.6f, 0.6f, 0x92BD59, 0x77AB2F },
    { /*  29 */ "Dark Forest",				0.7f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  30 */ "Snowy Taiga",			   -0.5f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  31 */ "Snowy Taiga Hills",	   -0.5f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  32 */ "Giant Tree Taiga",			0.3f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  33 */ "Giant Tree Taiga Hills",	0.3f, 0.8f, 0x92BD59, 0x77AB2F },
    { /*  34 */ "Wooded Mountains",			0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /*  35 */ "Savanna",					1.2f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  36 */ "Savanna Plateau",			1.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  37 */ "Badlands",					2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  38 */ "Wooded Badlands Plateau",	2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  39 */ "Badlands Plateau",			2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /*  40 */ "Small End Islands",		0.5f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  41 */ "End Midlands",				0.5f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  42 */ "End Highlands",			0.5f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  43 */ "End Barrens",				0.5f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  44 */ "Warm Ocean",				0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  45 */ "Lukewarm Ocean",			0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  46 */ "Cold Ocean",				0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  47 */ "Deep Warm Ocean",			0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  48 */ "Deep Lukewarm Ocean",		0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  49 */ "Deep Cold Ocean",			0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  50 */ "Deep Frozen Ocean",		0.8f, 0.5f, 0x92BD59, 0x77AB2F },
    { /*  51 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  52 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  53 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  54 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  55 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  56 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  57 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  58 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  59 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  60 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  61 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  62 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  63 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  64 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  65 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  66 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  67 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  68 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  69 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  70 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  71 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  72 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  73 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  74 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  75 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  76 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  77 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  78 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  79 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  80 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  81 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  82 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  83 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  84 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  85 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  86 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  87 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  88 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  89 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  90 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  91 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  92 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  93 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  94 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  95 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  96 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  97 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  98 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /*  99 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 100 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 101 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 102 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 103 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 104 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 105 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 106 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 107 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 108 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 109 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 110 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 111 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 112 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 113 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 114 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 115 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 116 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 117 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 118 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 119 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 120 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 121 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 122 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 123 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 124 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 125 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 126 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 127 */ "The Void",            		    0.5f, 0.5f, 0x92BD59, 0x77AB2F },	// default values of temp and rain; also, no height differences
    { /* 128 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 129 */ "Sunflower Plains",			    0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 130 */ "Desert Lakes",					2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 131 */ "Gravelly Mountains",		    0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /* 132 */ "Flower Forest",				0.7f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 133 */ "Taiga Mountains",			   0.25f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 134 */ "Swamp Hills",					0.8f, 0.9f, 0x92BD59, 0x77AB2F },
    { /* 135 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 136 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 137 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 138 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 139 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 140 */ "Ice Spikes",				    0.0f, 0.5f, 0x92BD59, 0x77AB2F },
    { /* 141 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 142 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 143 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 144 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 145 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 146 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 147 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 148 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 149 */ "Modified Jungle",			   0.95f, 0.9f, 0x92BD59, 0x77AB2F },
    { /* 150 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 151 */ "Modified Jungle Edge",		   0.95f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 152 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 153 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 154 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 155 */ "Tall Birch Forest",		    0.6f, 0.6f, 0x92BD59, 0x77AB2F },
    { /* 156 */ "Tall Birch Hills",				0.6f, 0.6f, 0x92BD59, 0x77AB2F },
    { /* 157 */ "Dark Forest Hills",		    0.7f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 158 */ "Snowy Taiga Mountains",	   -0.5f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 159 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 160 */ "Giant Spruce Taiga",		   0.25f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 161 */ "Giant Spruce Taiga Hills",    0.25f, 0.8f, 0x92BD59, 0x77AB2F },
    { /* 162 */ "Gravelly Mountains+",			0.2f, 0.3f, 0x92BD59, 0x77AB2F },
    { /* 163 */ "Shattered Savanna",			1.1f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 164 */ "Shattered Savanna Plateau",    1.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 165 */ "Eroded Badlands",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 166 */ "Modified Wooded Badlands Plateau",    2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 167 */ "Modified Badlands Plateau",    2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 168 */ "Bamboo Jungle",			   0.95f, 0.9f, 0x92BD59, 0x77AB2F },
    { /* 168 */ "Bamboo Jungle Hills",		   0.95f, 0.9f, 0x92BD59, 0x77AB2F },
    { /* 170 */ "Soul Sand Valley",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 171 */ "Crimson Forest",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 172 */ "Warped Forest",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 173 */ "Basalt Deltas",				2.0f, 0.0f, 0x92BD59, 0x77AB2F },
    { /* 174 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 175 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 176 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 177 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 178 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 179 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 180 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 181 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 182 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 183 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 184 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 185 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 186 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 187 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 188 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 189 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 190 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 191 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 192 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 193 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 194 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 195 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 196 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 197 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 198 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 199 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 200 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 201 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 202 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 203 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 204 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 205 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 206 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 207 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 208 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 209 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 210 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 211 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 212 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 213 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 214 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 215 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 216 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 217 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 218 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 219 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 220 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 221 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 222 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 223 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 224 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 225 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 226 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 227 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 228 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 229 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 230 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 231 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 232 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 233 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 234 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 235 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 236 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 237 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 238 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 239 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 240 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 241 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 242 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 243 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 244 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 245 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 246 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 247 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 248 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 249 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 250 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 251 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 252 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 253 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 254 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
    { /* 255 */ "Unknown Biome",				0.8f, 0.4f, 0x92BD59, 0x77AB2F },
};

typedef struct BiomeCorner
{
    int red;
    int green;
    int blue;
} BiomeCorner;

static BiomeCorner grassCorners[3] =
{
    { 191, 183,  85 },	// lower left, temperature starts at 1.0 on left
    { 128, 180, 151 },	// lower right
    {  71, 205,  51 }	// upper left
};

static BiomeCorner foliageCorners[3] =
{
    { 174, 164,  42 },	// lower left, temperature starts at 1.0 on left
    {  96, 161, 123 },	// lower right
    {  26, 191,  0 }	// upper left
};

// NOTE: elevation is number of meters above a height of 64. If elevation is < 64, pass in 0.
int BiomeColor(float temperature, float rainfall, int elevation, BiomeCorner corners[3])
{
    // get UVs
    temperature = clamp(temperature - (float)elevation * 0.00166667f, 0.0f, 1.0f);
    // crank it up: temperature = clamp(temperature - (float)elevation*0.166667f,0.0f,1.0f);
    rainfall = clamp(rainfall, 0.0f, 1.0f);
    rainfall *= temperature;

    // UV is essentially temperature, rainfall

    // lambda values for barycentric coordinates
    float lambda[3];
    lambda[0] = temperature - rainfall;
    lambda[1] = 1.0f - temperature;
    lambda[2] = rainfall;

    float red = 0.0f, green = 0.0f, blue = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        red += lambda[i] * corners[i].red;
        green += lambda[i] * corners[i].green;
        blue += lambda[i] * corners[i].blue;
    }

    int r = (int)clamp(red, 0.0f, 255.0f);
    int g = (int)clamp(green, 0.0f, 255.0f);
    int b = (int)clamp(blue, 0.0f, 255.0f);

    return (r << 16) | (g << 8) | b;
}

int BiomeGrassColor(float temperature, float rainfall, int elevation)
{
    return BiomeColor(temperature, rainfall, elevation, grassCorners);
}

int BiomeFoliageColor(float temperature, float rainfall, int elevation)
{
    return BiomeColor(temperature, rainfall, elevation, foliageCorners);
}

void PrecomputeBiomeColors()
{
    for (int biome = 0; biome < 256; biome++)
    {
        gBiomes[biome].grass = ComputeBiomeColor(biome, 0, 1);
        gBiomes[biome].foliage = ComputeBiomeColor(biome, 0, 0);
    }
}

// elevation == 0 means for precomputed colors and for elevation off
// or 64 high or below. 
int ComputeBiomeColor(int biome, int elevation, int isGrass)
{
    switch (biome)
    {
    case SWAMPLAND_BIOME:
        // the fefefe makes it so that carries are copied to the low bit,
        // then their magic "go to green" color offset is added in, then
        // divide by two gives a carry that will nicely go away.
        // old method:
        //color = BiomeGrassColor( gBiomes[biome].temperature, gBiomes[biome].rainfall );
        //gBiomes[biome].grass = ((color & 0xfefefe) + 0x4e0e4e) / 2;
        //color = BiomeFoliageColor( gBiomes[biome].temperature, gBiomes[biome].rainfall );
        //gBiomes[biome].foliage = ((color & 0xfefefe) + 0x4e0e4e) / 2;

        // new method:
        // yes, it's hard-wired in. It actually varies with temperature:
        //         return temperature < -0.1D ? 0x4c763c : 0x6a7039;
        // where temperature is varied by PerlinNoise, but I haven't recreated the
        // PerlinNoise function yet. Rich green vs. sickly swamp brown. I'm going with brown.
        return 0x6a7039;

        // These are actually perfectly normal. Only sub-type 3, roofed forest, is different.
        //case FOREST_BIOME:	// forestType 0
        //case FOREST_HILLS_BIOME:	// forestType 0
        //case BIRCH_FOREST_BIOME:	// forestType 2
        //case BIRCH_FOREST_HILLS_BIOME:	// forestType 2
        //	break;

    case ROOFED_FOREST_BIOME:	// forestType 3
        if (isGrass)
        {
            int color = BiomeGrassColor(gBiomes[biome].temperature, gBiomes[biome].rainfall, elevation);
            // the fefefe makes it so that carries are copied to the low bit,
            // then their magic "go to green" color offset is added in, then
            // divide by two gives a carry that will nicely go away.
            return ((color & 0xfefefe) + 0x28340a) / 2;
        }
        else
        {
            return BiomeFoliageColor(gBiomes[biome].temperature, gBiomes[biome].rainfall, elevation);
        }

    case MESA_BIOME:
    case MESA_PLATEAU_F_BIOME:
    case MESA_PLATEAU_BIOME:
        // yes, it's hard-wired
        return isGrass ? 0x90814d : 0x9e814d;

    default:
        return isGrass ? BiomeGrassColor(gBiomes[biome].temperature, gBiomes[biome].rainfall, elevation) :
            BiomeFoliageColor(gBiomes[biome].temperature, gBiomes[biome].rainfall, elevation);
    }
}

int BiomeSwampRiverColor(int color)
{
    int r = (int)((color >> 16) & 0xff);
    int g = (int)((color >> 8) & 0xff);
    int b = (int)color & 0xff;

    // swamp color modifier is 0xE0FFAE
    r = (r * 0xE0) / 255;
    // does nothing: g=(g*0xFF)/255;
    b = (b * 0xAE) / 255;
    color = (r << 16) | (g << 8) | b;

    return color;
}
