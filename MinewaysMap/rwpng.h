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

// PngXfer.cpp : Defines the entry point for the console application.
//

//#include "targetver.h"

#ifndef RWPNG_H
#define RWPNG_H

#include <stdio.h>
#include <tchar.h>

#include "zlib.h"
#include "png.h"
#include <stdlib.h>

#define TEXT_TITLE    0x01
#define TEXT_AUTHOR   0x02
#define TEXT_DESC     0x04
#define TEXT_COPY     0x08
#define TEXT_EMAIL    0x10
#define TEXT_URL      0x20

#define TEXT_TITLE_OFFSET        0
#define TEXT_AUTHOR_OFFSET      72
#define TEXT_COPY_OFFSET     (2*72)
#define TEXT_EMAIL_OFFSET    (3*72)
#define TEXT_URL_OFFSET      (4*72)
#define TEXT_DESC_OFFSET     (5*72)

typedef struct _progimage_info {
    double gamma;
    int width;
    int height;
    time_t modtime;
    FILE *infile;
    FILE *outfile;
    png_struct *png_ptr;
    png_info *info_ptr;
    unsigned char *image_data;
    unsigned char **row_pointers;
    char *title;
    char *author;
    char *desc;
    char *copyright;
    char *email;
    char *url;
    int color_type; //PNG_COLOR_TYPE_RGB_ALPHA
    int bit_depth;
    int interlaced;
    int have_bg;
    int have_time;
    int have_text;
    jmp_buf jmpbuf;
    unsigned char bg_red;
    unsigned char bg_green;
    unsigned char bg_blue;
} progimage_info;

int readpng(progimage_info *mainprog_ptr, wchar_t *filename);
void readpng_cleanup(int free_image_data, progimage_info *mainprog_ptr);

int writepng(progimage_info *mainprog_ptr, int channels, wchar_t *filename);
void writepng_cleanup(progimage_info *mainprog_ptr);

#endif