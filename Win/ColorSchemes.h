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

typedef struct
{
    int id;
    wchar_t name[255];
    unsigned int colors[NUM_BLOCKS_CS];   // RGBA, unmultiplied by A
} ColorScheme;

class ColorManager
{
public:
    ColorManager();
    ~ColorManager();
    static void Init(ColorScheme* cs); // initializes a colorscheme with default colors
    void create(ColorScheme* cs); //creates a colorscheme, inits it and saves it
    int next(int id, ColorScheme* cs); //enumerate colorschemes
    void load(ColorScheme* cs); //loads a color scheme (id must be set)
    void copy(ColorScheme* cs, ColorScheme* csSource); // copy from source to cs; must have loaded both previously
    void save(ColorScheme* cs); //saves a color scheme
    void remove(int id); //remove a color scheme
    static unsigned int blockColor(int type); // get color to store for a block
private:
    HKEY key;
};

void doColorSchemes(HINSTANCE hInst, HWND hWnd);
wchar_t* getSelectedColorScheme();   // LB_ERR if nothing selected on exit