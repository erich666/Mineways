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

#pragma once

#include <stdlib.h>

void PrecomputeBiomeColors();
int ComputeBiomeColor(int biome, int elevation, int isGrass);
int BiomeSwampRiverColor(int color);


#define FOREST_BIOME				 4
#define SWAMPLAND_BIOME				 6
#define FOREST_HILLS_BIOME			18
#define BIRCH_FOREST_BIOME			27
#define BIRCH_FOREST_HILLS_BIOME	28
#define DARK_FOREST_BIOME			29

#define BADLANDS_BIOME					37
#define WOODED_BADLANDS_PLATEAU_BIOME	38
#define BADLANDS_PLATEAU_BIOME			39

#define WOODED_BADLANDS_BIOME       52

#define MANGROVE_SWAMP_BIOME        53   

#define CHERRY_GROVE_BIOME          55

// higher values theoretically possible if Bedrock conversion went bad, but this is the realistic value
#define MAX_VALID_BIOME_ID          182

typedef struct Biome {
    const char* name;
    float temperature;
    float rainfall; // now called "downfall" elsewhere
    unsigned int grass;	// r,g,b, NOT multiplied by alpha
    unsigned int foliage;	// r,g,b, NOT multiplied by alpha
    int hashSum;    // for lowercase name found in world, what's its hash sum?
    char* lcName;  // lowercase, with "_" for space
} Biome;

extern Biome gBiomes[];
