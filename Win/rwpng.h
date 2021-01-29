/*
Copyright (c) 2011, Eric Haines
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

#include "lodepng.h"

typedef struct _progimage_info {
    std::vector<unsigned char> image_data; //the raw pixels
    int width = 0;
    int height = 0;
} progimage_info;

int readpng(progimage_info *mainprog_ptr, wchar_t *filename, LodePNGColorType colortype /*= LCT_RGBA*/);
void readpng_cleanup(int free_image_data, progimage_info *mainprog_ptr);
int readpngheader(progimage_info* im, wchar_t* filename, LodePNGColorType& colortype);

int writepng(progimage_info *mainprog_ptr, int channels, wchar_t *filename);
void writepng_cleanup(progimage_info *mainprog_ptr);

progimage_info* allocateGrayscaleImage(progimage_info* source_ptr);
progimage_info* allocateRGBImage(progimage_info* source_ptr);
void copyOneChannel(progimage_info* dst, int channel, progimage_info* src, LodePNGColorType colortype);

int channelEqualsValue(progimage_info* src, int channel, int numChannels, unsigned char value, int ignoreGrayscale);
void changeValueToValue(progimage_info* src, int channel, int numChannels, unsigned char value, unsigned char newValue);