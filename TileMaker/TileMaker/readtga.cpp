/*
Copyright (c) 2021, Eric Haines
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
#include "readtga.h"

#include <assert.h>

#include <iostream>

// calls https://github.com/aseprite/tga

//Decode from disk to raw pixels with a single function call
// return 0 on success
int readtga(progimage_info* im, wchar_t* filename, LodePNGColorType colortype)
{
    im->width = im->height = 0;

    FILE* f;
    _wfopen_s(&f, filename, L"rb");
    tga::StdioFileInterface file(f);
    tga::Decoder decoder(&file);
    tga::Header header;
    if (!decoder.readHeader(header))
        return 102;

    bool match = false;
    int channels_in = header.bytesPerPixel();
    switch (channels_in) {
    case 1:
        if (colortype == LCT_GREY) {
            match = true;
        }
        break;
    case 3:
        if (colortype == LCT_RGB) {
            match = true;
        }
        break;
    case 4:
        if (colortype == LCT_RGBA) {
            match = true;
        }
        break;
    default:
        // unsupported file type, we assume
        return 104;
    }

    im->width = (int)header.width;
    im->height = (int)header.height;

    tga::Image image;
    image.bytesPerPixel = header.bytesPerPixel();
    image.rowstride = header.width * header.bytesPerPixel();

    std::vector<uint8_t> buffer(image.rowstride * header.height);
    image.pixels = &buffer[0];

    if (!decoder.readImage(header, image, nullptr))
        return 103;

    // Optional post-process to fix the alpha channel in
    // some TGA files where alpha=0 for all pixels when
    // it shouldn't.
    decoder.postProcessImage(header, image);

    if (match) {
        im->image_data = buffer;
    }
    else {
        int channels_out = 0;

        // color types don't match - convert from one to the other
        switch (channels_in) {
        case 1:
            // gray to RGB or RGBA
            channels_out = (colortype == LCT_RGB) ? 3 : 4;
            break;
        case 3:
            // RGB to gray or RGBA
            channels_out = (colortype == LCT_RGBA) ? 1 : 4;
            break;
        case 4:
            // RGBA to gray or RGB (ignore alpha, I guess...)
            channels_out = (colortype == LCT_RGB) ? 3 : 1;
            break;
        }
        int num_pixels = header.width * header.height;
        im->image_data.resize(channels_out * num_pixels);
        int i;
        unsigned char* src_data = &buffer[0];
        unsigned char* dst_data = &im->image_data[0];
        switch (channels_in) {
        case 1:
            // gray to RGB or RGBA
            for (i = 0; i < num_pixels; i++) {
                if (channels_out == 4) {
                    *dst_data++ = *src_data;
                }
                *dst_data++ = *src_data;
                *dst_data++ = *src_data;
                *dst_data++ = *src_data++;
            }
            break;
        case 3:
            // RGB to gray or RGBA
            if (channels_out == 4) {
                for (i = 0; i < num_pixels; i++) {
                    *dst_data++ = *src_data++;
                    *dst_data++ = *src_data++;
                    *dst_data++ = *src_data++;
                    *dst_data++ = 255;
                }
            }
            else {
                // RGB to gray? Why? Is the incoming RGB image actually gray?
                assert(0);
                for (i = 0; i < num_pixels; i++) {
                    *dst_data++ = *src_data++;
                    // skip green and blue
                    src_data++;
                    src_data++;
                }
            }
            break;
        case 4:
            // RGBA to gray or RGB (ignore alpha, I guess...)
            if (channels_out == 3) {
                for (i = 0; i < num_pixels; i++) {
                    *dst_data++ = *src_data++;
                    *dst_data++ = *src_data++;
                    *dst_data++ = *src_data++;
                    src_data++; // skip alpha
                }
            }
            else {
                // RGBA to gray? Why? Is the incoming RGB image actually gray?
                assert(0);
                for (i = 0; i < num_pixels; i++) {
                    *dst_data++ = *src_data++;
                    // skip green and blue and alpha
                    src_data++;
                    src_data++;
                    src_data++;
                }
            }
            break;
        }
    }

    return 0;
}

// same as PNG's
//void readtga_cleanup(int mode, progimage_info *im)
//{
//    // mode was important back when libpng was in use
//    if ( mode == 1 )
//    {
//        im->image_data.clear();
//    }
//}

int readtgaheader(progimage_info* im, wchar_t* filename, LodePNGColorType& colortype)
{
    im->width = im->height = 0;

    FILE* f;
    _wfopen_s(&f, filename, L"rb");
    tga::StdioFileInterface file(f);
    tga::Decoder decoder(&file);
    tga::Header header;
    if (!decoder.readHeader(header))
        return 102;

    im->width = (int)header.width;
    im->height = (int)header.height;

    switch (header.bytesPerPixel()) {
    case 1:
        colortype = LCT_GREY;
        break;
    default:
        assert(0);
    case 3:
        colortype = LCT_RGB;
        break;
    case 4:
        colortype = LCT_RGBA;
        break;
    }

    fclose(f);

    return 0;
}

//============================
// Yes, these should be in yet another separate file, but alas

int readImage(progimage_info* im, wchar_t* filename, LodePNGColorType colortype, int imageFileType)
{
    if (imageFileType == 1) {
        return readpng(im, filename, colortype);
    }
    else if (imageFileType == 2) {
        return readtga(im, filename, colortype);
    }
    assert(0);
    // unknown image type
    return 1;
}

void readImage_cleanup(int mode, progimage_info* im)
{
    // same for TGA
    readpng_cleanup(mode, im);
}

int readImageHeader(progimage_info* im, wchar_t* filename, LodePNGColorType& colortype, int imageFileType)
{
    if (imageFileType == 1) {
        return readpngheader(im, filename, colortype);
    }
    else if (imageFileType == 2) {
        return readtgaheader(im, filename, colortype);
    }
    assert(0);
    // unknown image type
    return 999;
}

