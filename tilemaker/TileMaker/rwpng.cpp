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

#include "stdafx.h"
#include "rwpng.h"

#include <assert.h>

#include <iostream>

// from http://lodev.org/lodepng/example_decode.cpp

//Decode from disk to raw pixels with a single function call
// return 0 on success
int readpng(progimage_info *im, wchar_t *filename)
{
    //char filename[MAX_PATH];
    //dumb_wcharToChar(wfilename,filename);

    //decode
    unsigned int width, height;
    unsigned int error = lodepng::decode(im->image_data, width, height, filename);

    //if there's an error, display it
    if (error)
    {
        im->width = 0;
        im->height = 0;
        //std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
        return (int)error;
    }

    im->width = (int)width;
    im->height = (int)height;

    //the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...

    return 0;
}

void readpng_cleanup(int mode, progimage_info *im)
{
    // mode was important back when libpng was in use
    if ( mode == 1 )
    {
        im->image_data.clear();
    }
}

// from http://lodev.org/lodepng/example_encode.cpp

//Encode from raw pixels to disk with a single function call
//The image argument has width * height RGBA pixels or width * height * 4 bytes
// return 0 on success
int writepng(progimage_info *im, int channels, wchar_t *filename)
{
    //char filename[MAX_PATH];
    //dumb_wcharToChar(wfilename,filename);

    //Encode the image, depending on type
    unsigned int error = 1;	// 1 means didn't reach lodepng
    if ( channels == 4 )
    {
        // 32 bit RGBA, the default
        error = lodepng::encode(filename, im->image_data, (unsigned int)im->width, (unsigned int)im->height, LCT_RGBA );
    }
    else if ( channels == 3 )
    {
        // 24 bit RGB
        error = lodepng::encode(filename, im->image_data, (unsigned int)im->width, (unsigned int)im->height, LCT_RGB );
    }
    else
    {
        assert(0);
    }

    //if there's an error, display it
    if (error)
    {
        //std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
        return (int)error;
    }

    return 0;
}


void writepng_cleanup(progimage_info *im)
{
    im->image_data.clear();
}


