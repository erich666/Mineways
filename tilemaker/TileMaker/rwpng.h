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
	int channels;
	int rowbytes;
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

int writepng(progimage_info *mainprog_ptr, wchar_t *filename);
void writepng_cleanup(progimage_info *mainprog_ptr);

#endif