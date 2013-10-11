// PngXfer.cpp : Defines the entry point for the console application.
//

#include "rwpng.h"


#ifdef DEBUG
#  define Trace(x)  {fprintf x ; fflush(stderr); fflush(stdout);}
#else
#  define Trace(x)  ;
#endif


int writepngheader(progimage_info *mainprog_ptr);

/* prototypes for public functions in writepng.c */

void writepng_version_info(void);

int writepng_init(progimage_info *mainprog_ptr);

int writepng_encode_image(progimage_info *mainprog_ptr);

int writepng_encode_row(progimage_info *mainprog_ptr);

int writepng_encode_finish(progimage_info *mainprog_ptr);

static void writepng_error_handler(png_structp png_ptr, png_const_charp msg);

/* prototypes for public functions in readpng.c */

void readpng_version_info(void);

int readpng_init(FILE *infile, int *pWidth, int *pHeight, int *pBitDepth, int *pColorType, png_structp *ppng_ptr, png_infop *pinfo_ptr);

int readpng_get_bgcolor(int bit_depth, int color_type, unsigned char *red, unsigned char *green, unsigned char *blue, png_structp png_ptr, png_infop info_ptr);

unsigned char *readpng_get_image(int height, int bit_depth, int color_type, double display_exponent, int *pChannels,
	int *pRowbytes, png_structp png_ptr, png_infop info_ptr);



/* future versions of libpng will provide this macro: */
#ifndef png_jmpbuf
#  define png_jmpbuf(png_ptr)   ((png_ptr)->jmpbuf)
#endif

int readpng(progimage_info *mainprog_ptr, wchar_t *filename)
{
	int err;
	int channels, rowbytes;
	int rc;

	mainprog_ptr->infile = NULL;
	err = _wfopen_s(&mainprog_ptr->infile,filename, L"rb");
	if (mainprog_ptr->infile == NULL )
		return 1;
	if ( err != 0 )
	{
		return 1;
	}

	rc = readpng_init(mainprog_ptr->infile, &mainprog_ptr->width, &mainprog_ptr->height,
		&mainprog_ptr->bit_depth,&mainprog_ptr->color_type,&mainprog_ptr->png_ptr,&mainprog_ptr->info_ptr);

	if ( rc != 0 )
	{
		switch (rc) {
		case 1:
			//fprintf(stderr, PROGNAME
			//	":  [%s] is not a PNG file: incorrect signature\n",
			//	filename);
			break;
		case 2:
			//fprintf(stderr, PROGNAME
			//	":  [%s] has bad IHDR (libpng longjmp)\n", filename);
			break;
		case 4:
			//fprintf(stderr, PROGNAME ":  insufficient memory\n");
			break;
		default:
			//fprintf(stderr, PROGNAME
			//	":  unknown readpng_init() error\n");
			break;
		}
		fclose(mainprog_ptr->infile);
		return rc;
	}

	mainprog_ptr->image_data = readpng_get_image(mainprog_ptr->height, mainprog_ptr->bit_depth, mainprog_ptr->color_type, 2.2, &channels, &rowbytes, mainprog_ptr->png_ptr, mainprog_ptr->info_ptr);

    fclose(mainprog_ptr->infile);

	return 0;
}

int writepng(progimage_info *mainprog_ptr, wchar_t *filename)
{
	int err;
	int r;
	unsigned char *image_data;

	mainprog_ptr->outfile = NULL;
	err = _wfopen_s(&mainprog_ptr->outfile,filename, L"wb");
	if (mainprog_ptr->outfile == NULL )
		return 1;
	if ( err != 0 )
	{
		return 1;
	}

	mainprog_ptr->gamma = 0.0;
	mainprog_ptr->have_time = 0;
	mainprog_ptr->modtime;
	mainprog_ptr->interlaced = PNG_INTERLACE_NONE;
	mainprog_ptr->have_bg = 0;
	mainprog_ptr->bg_red;
	mainprog_ptr->bg_green;
	mainprog_ptr->bg_blue;
	//unsigned char **row_pointers;	// needed only for interlaced output
	//mainprog_ptr->row_pointers = (unsigned char **)malloc(256*sizeof(unsigned char *));
	mainprog_ptr->have_text = TEXT_TITLE|TEXT_AUTHOR|TEXT_DESC;
	mainprog_ptr->title = "Minecraft tile texture";
	mainprog_ptr->author;
	mainprog_ptr->desc;
	mainprog_ptr->copyright;
	mainprog_ptr->email;
	mainprog_ptr->url;
	//mainprog.jmpbuf;

	// whatever image data is in image_data will be written out.
	image_data = mainprog_ptr->image_data;

	writepngheader(mainprog_ptr);

	//pImg = image_data;
	for ( r = 0; r < mainprog_ptr->height; r++ )
	{
		//mainprog.row_pointers[r] = &mainprog.image_data[r*256*4];
		mainprog_ptr->image_data = &image_data[r*mainprog_ptr->width*4];
		writepng_encode_row(mainprog_ptr);
	}

	// restore original image data pointer.
	mainprog_ptr->image_data = image_data;

	//writepng_encode_image(mainprog_ptr);
    writepng_encode_finish(mainprog_ptr);

	fclose(mainprog_ptr->outfile);

	return 0;
}

/*-------------------------------------------------------------------------------------------------------*/

void readpng_version_info(void)
{
    fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, png_libpng_ver);
    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
      ZLIB_VERSION, zlib_version);
}


/* return value = 0 for success, 1 for bad sig, 2 for bad IHDR, 4 for no mem */

int readpng_init(FILE *infile, int *pWidth, int *pHeight, int *pBitDepth, int *pColorType, png_structp *ppng_ptr, png_infop *pinfo_ptr)
{
    unsigned char sig[8];
	png_uint_32 width, height;
	int bit_depth, color_type;
	png_structp png_ptr;
	png_infop info_ptr;


    /* first do a quick check that the file really is a PNG image; could
     * have used slightly more general png_sig_cmp() function instead */

    fread(sig, 1, 8, infile);
    if (png_sig_cmp(sig, 0, 8))
        return 1;   /* bad signature */


    /* could pass pointers to user-defined error handlers instead of NULLs: */

    *ppng_ptr = png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        return 4;   /* out of memory */

    *pinfo_ptr = info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return 4;   /* out of memory */
    }


    /* we could create a second info struct here (end_info), but it's only
     * useful if we want to keep pre- and post-IDAT chunk info separated
     * (mainly for PNG-aware image editors and converters) */


    /* setjmp() must be called in every function that calls a PNG-reading
     * libpng function */

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return 2;
    }


    png_init_io(png_ptr, infile);
    png_set_sig_bytes(png_ptr, 8);  /* we already read the 8 signature bytes */

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */


    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
      NULL, NULL, NULL);
    *pWidth = width;
    *pHeight = height;
	*pBitDepth = bit_depth;
	*pColorType = color_type;


    /* OK, that's all we need for now; return happy */

    return 0;
}




/* returns 0 if succeeds, 1 if fails due to no bKGD chunk, 2 if libpng error;
 * scales values to 8-bit if necessary */

int readpng_get_bgcolor(int bit_depth, int color_type, unsigned char *red, unsigned char *green, unsigned char *blue, png_structp png_ptr, png_infop info_ptr)
{
    png_color_16p pBackground;


    /* setjmp() must be called in every function that calls a PNG-reading
     * libpng function */

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return 2;
    }


    if (!png_get_valid(png_ptr, info_ptr, PNG_INFO_bKGD))
        return 1;

    /* it is not obvious from the libpng documentation, but this function
     * takes a pointer to a pointer, and it always returns valid red, green
     * and blue values, regardless of color_type: */

    png_get_bKGD(png_ptr, info_ptr, &pBackground);


    /* however, it always returns the raw bKGD data, regardless of any
     * bit-depth transformations, so check depth and adjust if necessary */

    if (bit_depth == 16) {
        *red   = pBackground->red   >> 8;
        *green = pBackground->green >> 8;
        *blue  = pBackground->blue  >> 8;
    } else if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        if (bit_depth == 1)
            *red = *green = *blue = pBackground->gray? 255 : 0;
        else if (bit_depth == 2)
            *red = *green = *blue = (unsigned char)((255/3) * pBackground->gray);
        else /* bit_depth == 4 */
            *red = *green = *blue = (unsigned char)((255/15) * pBackground->gray);
    } else {
        *red   = (unsigned char)pBackground->red;
        *green = (unsigned char)pBackground->green;
        *blue  = (unsigned char)pBackground->blue;
    }

    return 0;
}




/* display_exponent == LUT_exponent * CRT_exponent */

unsigned char *readpng_get_image(int height, int bit_depth, int color_type, double display_exponent, int *pChannels, int *pRowbytes, png_structp png_ptr, png_infop info_ptr)
{
    double  gamma;
	unsigned char *image_data;
    int  i, rowbytes;
    png_bytepp  row_pointers = NULL;


    /* setjmp() must be called in every function that calls a PNG-reading
     * libpng function */

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }


    /* expand palette images to RGB, low-bit-depth grayscale images to 8 bits,
     * transparency chunks to full alpha channel; strip 16-bit-per-sample
     * images to 8 bits per sample; and convert grayscale to RGB[A] */

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_expand(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_expand(png_ptr);
#ifdef PNG_READ_16_TO_8_SUPPORTED
    if (bit_depth == 16)
#  ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
        png_set_scale_16(png_ptr);
#  else
        png_set_strip_16(png_ptr);
#  endif
#endif
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);


    /* unlike the example in the libpng documentation, we have *no* idea where
     * this file may have come from--so if it doesn't have a file gamma, don't
     * do any correction ("do no harm") */

    if (png_get_gAMA(png_ptr, info_ptr, &gamma))
        png_set_gamma(png_ptr, display_exponent, gamma);


    /* all transformations have been registered; now update info_ptr data,
     * get rowbytes and channels, and allocate image memory */

    png_read_update_info(png_ptr, info_ptr);

    *pRowbytes = rowbytes = (int)png_get_rowbytes(png_ptr, info_ptr);
    *pChannels = (int)png_get_channels(png_ptr, info_ptr);

    if ((image_data = (unsigned char *)malloc(rowbytes*height)) == NULL) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    if ((row_pointers = (png_bytepp)malloc(height*sizeof(png_bytep))) == NULL) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        free(image_data);
        image_data = NULL;
        return NULL;
    }

    Trace((stderr, "readpng_get_image:  channels = %d, rowbytes = %ld, height = %ld\n",
        *pChannels, rowbytes, height));


    /* set the individual row_pointers to point at the correct offsets */

    for (i = 0;  i < height;  ++i)
        row_pointers[i] = image_data + i*rowbytes;


    /* now we can go ahead and just read the whole image */

    png_read_image(png_ptr, row_pointers);


    /* and we're done!  (png_read_end() can be omitted if no processing of
     * post-IDAT text/time/etc. is desired) */

    free(row_pointers);
    row_pointers = NULL;

    png_read_end(png_ptr, NULL);

    return image_data;
}


void readpng_cleanup(int free_image_data, progimage_info *mainprog_ptr)
{
    if (free_image_data && mainprog_ptr->image_data) {
        free(mainprog_ptr->image_data);
        mainprog_ptr->image_data = NULL;
    }

    if (mainprog_ptr->png_ptr && mainprog_ptr->info_ptr) {
        png_destroy_read_struct(&mainprog_ptr->png_ptr, &mainprog_ptr->info_ptr, NULL);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
    }
}


/*---------------------------------------------------------------------------

   wpng - simple PNG-writing program                             writepng.h

  ---------------------------------------------------------------------------

      Copyright (c) 1998-2007 Greg Roelofs.  All rights reserved.

      This software is provided "as is," without warranty of any kind,
      express or implied.  In no event shall the author or contributors
      be held liable for any damages arising in any way from the use of
      this software.

      The contents of this file are DUAL-LICENSED.  You may modify and/or
      redistribute this software according to the terms of one of the
      following two licenses (at your option):


      LICENSE 1 ("BSD-like with advertising clause"):

      Permission is granted to anyone to use this software for any purpose,
      including commercial applications, and to alter it and redistribute
      it freely, subject to the following restrictions:

      1. Redistributions of source code must retain the above copyright
         notice, disclaimer, and this list of conditions.
      2. Redistributions in binary form must reproduce the above copyright
         notice, disclaimer, and this list of conditions in the documenta-
         tion and/or other materials provided with the distribution.
      3. All advertising materials mentioning features or use of this
         software must display the following acknowledgment:

            This product includes software developed by Greg Roelofs
            and contributors for the book, "PNG: The Definitive Guide,"
            published by O'Reilly and Associates.


      LICENSE 2 (GNU GPL v2 or later):

      This program is free software; you can redistribute it and/or modify
      it under the terms of the GNU General Public License as published by
      the Free Software Foundation; either version 2 of the License, or
      (at your option) any later version.

      This program is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License for more details.

      You should have received a copy of the GNU General Public License
      along with this program; if not, write to the Free Software Foundation,
      Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  ---------------------------------------------------------------------------*/


int writepngheader(progimage_info *mainprog_ptr)
{
	png_structp  png_ptr;       /* note:  temporary variables! */
	png_infop  info_ptr;
	int interlace_type;
    /* could also replace libpng warning-handler (final NULL), but no need: */

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      writepng_error_handler, NULL);
    if (!png_ptr)
        return 4;   /* out of memory */

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return 4;   /* out of memory */
    }


    /* setjmp() must be called in every function that calls a PNG-writing
     * libpng function, unless an alternate error handler was installed--
     * but compatible error handlers must either use longjmp() themselves
     * (as in this program) or some other method to return control to
     * application code, so here we go: */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return 2;
    }


    /* make sure outfile is (re)opened in BINARY mode */

    png_init_io(png_ptr, mainprog_ptr->outfile);


    /* set the compression levels--in general, always want to leave filtering
     * turned on (except for palette images) and allow all of the filters,
     * which is the default; want 32K zlib window, unless entire image buffer
     * is 16K or smaller (unknown here)--also the default; usually want max
     * compression (NOT the default); and remaining compression flags should
     * be left alone */

    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
/*
    >> this is default for no filtering; Z_FILTERED is default otherwise:
    png_set_compression_strategy(png_ptr, Z_DEFAULT_STRATEGY);
    >> these are all defaults:
    png_set_compression_mem_level(png_ptr, 8);
    png_set_compression_window_bits(png_ptr, 15);
    png_set_compression_method(png_ptr, 8);
 */


    /* set the image parameters appropriately */

    interlace_type = mainprog_ptr->interlaced? PNG_INTERLACE_ADAM7 :
                                               PNG_INTERLACE_NONE;

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
      mainprog_ptr->bit_depth, mainprog_ptr->color_type, interlace_type,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (mainprog_ptr->gamma > 0.0)
        png_set_gAMA(png_ptr, info_ptr, mainprog_ptr->gamma);

    if (mainprog_ptr->have_bg) {   /* we know it's RGBA, not gray+alpha */
        png_color_16  background;

        background.red = mainprog_ptr->bg_red;
        background.green = mainprog_ptr->bg_green;
        background.blue = mainprog_ptr->bg_blue;
        png_set_bKGD(png_ptr, info_ptr, &background);
    }

    if (mainprog_ptr->have_time) {
        png_time  modtime;

        png_convert_from_time_t(&modtime, mainprog_ptr->modtime);
        png_set_tIME(png_ptr, info_ptr, &modtime);
    }

    if (mainprog_ptr->have_text) {
        png_text  text[6];
        int  num_text = 0;

        if (mainprog_ptr->have_text & TEXT_TITLE) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Title";
            text[num_text].text = mainprog_ptr->title;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_AUTHOR) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Author";
            text[num_text].text = mainprog_ptr->author;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_DESC) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Description";
            text[num_text].text = mainprog_ptr->desc;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_COPY) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Copyright";
            text[num_text].text = mainprog_ptr->copyright;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_EMAIL) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "E-mail";
            text[num_text].text = mainprog_ptr->email;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_URL) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "URL";
            text[num_text].text = mainprog_ptr->url;
            ++num_text;
        }
        png_set_text(png_ptr, info_ptr, text, num_text);
    }


    /* write all chunks up to (but not including) first IDAT */

    png_write_info(png_ptr, info_ptr);


    /* if we wanted to write any more text info *after* the image data, we
     * would set up text struct(s) here and call png_set_text() again, with
     * just the new data; png_set_tIME() could also go here, but it would
     * have no effect since we already called it above (only one tIME chunk
     * allowed) */


    /* set up the transformations:  for now, just pack low-bit-depth pixels
     * into bytes (one, two or four pixels per byte) */

    png_set_packing(png_ptr);
/*  png_set_shift(png_ptr, &sig_bit);  to scale low-bit-depth values */


    /* make sure we save our pointers for use in writepng_encode_image() */

    mainprog_ptr->png_ptr = png_ptr;
    mainprog_ptr->info_ptr = info_ptr;


    /* OK, that's all we need to do for now; return happy */

	return 0;
}






/* returns 0 for success, 2 for libpng (longjmp) problem */

int writepng_encode_image(progimage_info *mainprog_ptr)
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;


    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return 2;
    }


    /* and now we just write the whole image; libpng takes care of interlacing
     * for us */

    png_write_image(png_ptr, mainprog_ptr->row_pointers);


    /* since that's it, we also close out the end of the PNG file now--if we
     * had any text or time info to write after the IDATs, second argument
     * would be info_ptr, but we optimize slightly by sending NULL pointer: */

    png_write_end(png_ptr, NULL);

    return 0;
}





/* returns 0 if succeeds, 2 if libpng problem */

int writepng_encode_row(progimage_info *mainprog_ptr)  /* NON-interlaced only! */
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;


    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return 2;
    }


    /* image_data points at our one row of image data */

    png_write_row(png_ptr, mainprog_ptr->image_data);

    return 0;
}





/* returns 0 if succeeds, 2 if libpng problem */

int writepng_encode_finish(progimage_info *mainprog_ptr)   /* NON-interlaced! */
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;


    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return 2;
    }


    /* close out PNG file; if we had any text or time info to write after
     * the IDATs, second argument would be info_ptr: */

    png_write_end(png_ptr, NULL);

    return 0;
}





void writepng_cleanup(progimage_info *mainprog_ptr)
{
	if (mainprog_ptr->png_ptr && mainprog_ptr->info_ptr) {
		png_destroy_write_struct(&mainprog_ptr->png_ptr, &mainprog_ptr->info_ptr);
		mainprog_ptr->png_ptr = NULL;
		mainprog_ptr->info_ptr = NULL;
	}
}





static void writepng_error_handler(png_structp png_ptr, png_const_charp msg)
{
    progimage_info  *mainprog_ptr;

    /* This function, aside from the extra step of retrieving the "error
     * pointer" (below) and the fact that it exists within the application
     * rather than within libpng, is essentially identical to libpng's
     * default error handler.  The second point is critical:  since both
     * setjmp() and longjmp() are called from the same code, they are
     * guaranteed to have compatible notions of how big a jmp_buf is,
     * regardless of whether _BSD_SOURCE or anything else has (or has not)
     * been defined. */

    fprintf(stderr, "writepng libpng error: %s\n", msg);
    fflush(stderr);

    mainprog_ptr = (progimage_info *)(png_ptr);
    if (mainprog_ptr == NULL) {         /* we are completely hosed now */
        fprintf(stderr,
          "writepng severe error:  jmpbuf not recoverable; terminating.\n");
        fflush(stderr);
        exit(99);
    }

    /* Now we have our data structure we can use the information in it
     * to return control to our own higher level code (all the points
     * where 'setjmp' is called in this file.)  This will work with other
     * error handling mechanisms as well - libpng always calls png_error
     * when it can proceed no further, thus, so long as the error handler
     * is intercepted, application code can do its own error recovery.
     */
    longjmp(mainprog_ptr->jmpbuf, 1);
}

