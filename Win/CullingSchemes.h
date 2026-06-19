/*
Copyright (c) 2026, Eric Haines
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

// Culling Schemes: a user-defined list of block names to hide from the map view and from
// .obj / .schem export. Parallel to the Color Schemes system; stored separately in the registry
// and selectable independently — the two schemes stack (a block is hidden if either system
// hides it). The list of selectable names mirrors BlockTranslations[] in nbt.cpp:
// every Minecraft block-state name Mineways recognizes appears in the editor with a checkbox.

#pragma once

// Storage: one byte per BlockTranslations[] entry, indicating whether the corresponding name
// is culled. NUM_TRANS is declared in nbt.cpp and used to size BlockTranslations; we mirror it
// here as a static cap so the registry binary blob has a fixed sizeof. Bump this if NUM_TRANS
// grows past it.
#define NUM_CULL_ENTRIES 1200

typedef struct
{
    int id;
    wchar_t name[255];
    unsigned char culled[NUM_CULL_ENTRIES];	// per-BlockTranslations bool; 0=show, 1=hide
} CullingScheme;

class CullingManager
{
public:
    CullingManager();
    ~CullingManager();
    static void Init(CullingScheme* cs);	// initializes a scheme with all blocks shown (culled=0)
    void create(CullingScheme* cs);	// creates a scheme, inits it, and saves it
    int next(int id, CullingScheme* cs);	// enumerate schemes (mirror of ColorManager::next)
    void load(CullingScheme* cs);	// load by id
    void copy(CullingScheme* cs, CullingScheme* csSource);
    void save(CullingScheme* cs);
    void remove(int id);
private:
    HKEY key;
};

void doCullingSchemes(HINSTANCE hInst, HWND hWnd);
wchar_t* getSelectedCullingScheme();

// Active-scheme application. After the menu handler loads a scheme (or "Standard" = none),
// it calls applyCullingScheme() with the scheme's `culled[]` array (or NULL to clear). The
// implementation walks BlockTranslations[] once and populates a fast (type, dataVal)-keyed
// lookup that the map renderer and the .obj/.schem export code consult.
void applyCullingScheme(const unsigned char* culled);

// Returns true if (type, dataVal) should be culled per the currently-active Culling Scheme.
// Both pipelines (map render and OBJ/schem export) call this. Returns false if no scheme is
// active, so the overhead is one branch per voxel in the no-culling case.
bool isBlockCulled(int type, int dataVal);
