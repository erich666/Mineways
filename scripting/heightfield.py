#!/usr/bin/python3

# Simple Python script to output adjacent hunks of a world.
# Version 1.0, Eric Haines, erich@acm.org
#
# Running this generates a .mwscript file that can be read into Mineways to export
# a series of "hunks" from a world. Usually worlds are too large - too much memory -
# to export in one giant OBJ file. This script lets you choose how large each hunk is.
# These hunks use "absolute coordinates" (not centered), so that they can be loaded one
# after the other next to each other in programs such as Blender.
#
# Process:
# Install Python, and install imageio, see
# https://imageio.readthedocs.io/en/stable/user_guide/installation.html
# I found using Conda and "conda install -c conda-forge imageio" to work for me. YMMV.
#
# Edit this file to change the arguments such as the file used in "imread"
# to whatever image file you want to convert. Then run this script in a command window:
#    python heightfield.py > heightfield.mwscript
#
# This creates a file called "heightfield.mwscript" with a bunch of Mineways script commands.
#
# In Mineways load the built-in world called [Block Test World]. Choose File | Import Settings
# and select this heightfield.mwscript file. The script will then run and "secretly" add these
# blocks to the scene. Though you can't see the data imported, it's there, really.
#
# In Mineways maximize the window and set the Depth slider to 64 (the lowest height).
# Hit Control-A - this selects all. Alternately, if you are using a large image, on
# export (see next step) you can set the Box max X and Z values to the size of your image.
#
# Choose File | Export for Rendering (or 3D Printing) and save. You should have an OBJ
# or whatever file full of your heightfield. Note that when you export to a file, the heightfield
# blocks that you added by importing heightfield.mwscript are then automatically cleared out.
# If you want to export the heighfield again, you'll need to File | Import Settings again.
#
# The 'r_bump_map.png' file used here as an example is from (my) USD test:
# https://github.com/usd-wg/assets/tree/main/test_assets/NormalsTextureBiasAndScale

import imageio.v3 as iio
# Name of image file to use as a heightmap.
# Note: don't use GIFs, as they are not RGB - convert to PNG first. I'd convert TIFF and EXR, too.
# Even PNG must be an RGB image, not grayscale, and should be normal precision, 8 bits per channel.
im = iio.imread("r_bump_map.png")

# the lowest and highest levels that the blocks will be in your Mineways export 
lowest = 64
highest = 90

# ---------------------
# Feel free to change the code below, of course 

rows = im.shape[0]
cols = im.shape[1]

for y in range(rows):   # note "y" is rows for the image, but actually Z in Minecraft world terms
    for x in range(cols):
        r,g,b = im[y,x]
        # Simple: add the three to get a height; highest-lowest is maximum height.
        # Note we need to cast the colors to int() as they are unsigned 8-bit ints to start
        h = round((int(r)+int(g)+int(b))*(highest-lowest)/(3*255))
        # A more luma-based function, see https://en.wikipedia.org/wiki/Luma_(video):
        #h = round((0.299*float(r)+0.587*float(g)+0.114*float(b))*(highest-lowest)/255)
        print('Change blocks: to \"grass block\" at ' + str(x) + ',' + str(lowest) + ',' + str(y) + ' to ' + str(x) + ',' + str(h+lowest) + ',' + str(y))
