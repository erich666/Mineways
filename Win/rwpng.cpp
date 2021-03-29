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
int readpng(progimage_info *im, wchar_t *filename, LodePNGColorType colortype)
{
    //char filename[MAX_PATH];
    //dumb_wcharToChar(wfilename,filename);

    //decode
    unsigned int width, height;
    unsigned int error = lodepng::decode(im->image_data, width, height, filename, colortype);

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

    //the pixels are now in the vector "image", for color+alpha these are 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...

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

// return 0 if no error
int readpngheader(progimage_info* im, wchar_t* filename, LodePNGColorType& colortype)
{
    unsigned int width, height;
    std::vector<unsigned char> buffer;
    lodepng::load_file(buffer, filename);

    colortype = LCT_RGBA;
    unsigned bitdepth = 8;

    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = colortype;
    state.info_raw.bitdepth = bitdepth;
    // reads header and resets other parameters in state->info_png
    state.error = lodepng_inspect(&width, &height, &state, buffer.empty() ? 0 : &buffer[0], (unsigned)buffer.size());
    unsigned int error = state.error;
    colortype = state.info_png.color.colortype;

    lodepng_state_cleanup(&state);

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

    return 0;
}

// from http://lodev.org/lodepng/example_encode.cpp

//Encode from raw pixels to disk with a single function call
//The image argument has width * height RGBA pixels or width * height * channels
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
    else if (channels == 1)
    {
        // 8 bit grayscale
        error = lodepng::encode(filename, im->image_data, (unsigned int)im->width, (unsigned int)im->height, LCT_GREY);
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

progimage_info* allocateGrayscaleImage(progimage_info* source_ptr)
{
    // allocate output image and fill it up
    progimage_info* destination_ptr = new progimage_info();

    destination_ptr->width = source_ptr->width;
    destination_ptr->height = source_ptr->height;
    destination_ptr->image_data.resize(destination_ptr->width * destination_ptr->height * 1 * sizeof(unsigned char), 0x0);

    return destination_ptr;
}

progimage_info* allocateRGBImage(progimage_info* source_ptr)
{
    // allocate output image and fill it up
    progimage_info* destination_ptr = new progimage_info();

    destination_ptr->width = source_ptr->width;
    destination_ptr->height = source_ptr->height;
    destination_ptr->image_data.resize(destination_ptr->width * destination_ptr->height * 3 * sizeof(unsigned char), 0x0);

    return destination_ptr;
}

void copyOneChannel(progimage_info* dst, int channel, progimage_info* src, LodePNGColorType colortype)
{
    int row, col;
    dst->width = src->width;
    dst->height = src->height;
    unsigned char* dst_data = &dst->image_data[0];
    unsigned char* src_data = &src->image_data[0] + channel;
    int channelIncrement = (colortype == LCT_RGB) ? 3 : 4;
    for (row = 0; row < src->height; row++)
    {
        for (col = 0; col < src->width; col++)
        {
            *dst_data++ = *src_data;
            src_data += channelIncrement;
        }
    }
}


// to avoid defining boolean, etc., make this one return 1 if true, 0 if false
int channelEqualsValue(progimage_info* src, int channel, int numChannels, unsigned char value, int ignoreGrayscale)
{
    // look at all data in given channel - all equal to the given value?
    assert(numChannels > 0);
    assert(channel < numChannels);
    assert(ignoreGrayscale ? numChannels > 1 : 1);
    int row, col;
    unsigned char* src_data = &src->image_data[0] + channel;
    for (row = 0; row < src->height; row++)
    {
        for (col = 0; col < src->width; col++)
        {
            if (*src_data != value)
            {
                // do grayscale test?
                if (ignoreGrayscale) {
                    if ((src_data[-channel] == src_data[1 - channel]) && (src_data[1 - channel] == src_data[2 - channel])) {
                        // it's gray, so ignore it (could be a cutout background pixel)
                        src_data += numChannels;
                        continue;
                    }
                }
                return 0;
            }
            src_data += numChannels;
        }
    }
    return 1;
}

void changeValueToValue(progimage_info* src, int channel, int numChannels, unsigned char value, unsigned char newValue)
{
    // if value in channel is equal to input value, change it to the new value
    assert(numChannels > 0);
    assert(channel < numChannels);
    int row, col;
    unsigned char* src_data = &src->image_data[0] + channel;
    for (row = 0; row < src->height; row++)
    {
        for (col = 0; col < src->width; col++)
        {
            if (*src_data == value)
            {
                *src_data = newValue;
            }
            src_data += numChannels;
        }
    }
}



