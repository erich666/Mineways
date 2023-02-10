#!/usr/bin/python3

# Simple Python script to generate an exportable heightfield from a bitmap. The heightmap
# is generated from an image (PNG or JPEG; make sure it's in RGB format, even though it's
# typically an all-gray image).
#
# Version 1.1, Eric Haines, erich@acm.org
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
# You can call this .mwscript file whatever you like.
#
# In Mineways choose File | Import Settings and select this heightfield.mwscript file.
# The script will then run. First it opens the built-in world called [Block Test World],
# then selects the volume where the heightfield is and adds its blocks to the scene.
# Though you can't see the data imported, it's there, really.
#
# You can adjust the volume
#
# Choose File | Export for Rendering (or 3D Printing) and save. You should have an OBJ
# or whatever file full of your heightfield. Note that when you export to a file, the heightfield
# blocks that you added by importing heightfield.mwscript are then automatically cleared out.
# If you want to export the heighfield again, you'll need to File | Import Settings again.
#
# The 'r_bump_map.png' file used here as an example is from (my) USD test:
# https://github.com/usd-wg/assets/tree/main/test_assets/NormalsTextureBiasAndScale

import imageio.v3 as iio

# USER VARIABLES - you'll likely want to change these values.
# Name of image file to use as a heightmap.
# Note: don't use GIFs, as they are not RGB - convert to PNG first. I'd convert TIFF and EXR, too.
# Even PNG must be an RGB image, not grayscale, and should be normal precision, 8 bits per channel.
im = iio.imread("r_bump_map.png")

# The upper-left corner of where to place the heightmap
xstart = 0
zstart = 0

# The lowest and highest levels that the blocks will be in your Mineways export, mapped from black through white
lowest = 64
highest = 90

# If you find you want to reload the .mwscript produced (e.g., you want to adjust the location and
# re-import the heightmap) before exporting, uncomment this next line, which will clear out previous
# imports.
print('Clear change block commands')

# Dummy world to import blocks into. This world has no content above height 0, so is a good one to
# use for loading and exporting data such as this heightmap.
# Comment out this line if you want to load the heightmap blocks to a different world.
print('Minecraft world: [Block Test World]')

# Size of image file - don't touch these. Needed to set the volume selection, below.
rows = im.shape[0]
cols = im.shape[1]

# Select the volume where the heightfield data is located. This will then show you where this volume
# is located when you import the Mineways script file produced. You can change or comment out this
# line, or manually change the selection volume after the Mineways script is loaded.
print('Selection location min to max: ' + str(xstart) + ', ' + str(lowest) + ', ' + str(zstart) + ' to ' + str(xstart+cols-1) + ', ' + str(highest) + ', ' + str(zstart+rows-1))

# End of user variables ---------------------

# Feel free to change the code below, of course 
for z in range(rows):   # note "z" is rows for the image, more typically "y" in an image
    for x in range(cols):
        r,g,b = im[z,x]
        # Simple: add the three to get a height; highest-lowest is maximum height.
        # Note we need to cast the colors to int() as they are unsigned 8-bit ints to start
        h = round((int(r)+int(g)+int(b))*(highest-lowest)/(3*255))
        # A more luma-based function, see https://en.wikipedia.org/wiki/Luma_(video):
        #h = round((0.299*float(r)+0.587*float(g)+0.114*float(b))*(highest-lowest)/255)
        print('Change blocks: to \"grass block\" at ' + str(xstart+x) + ',' + str(lowest) + ',' + str(zstart+z) + ' to ' + str(xstart+x) + ',' + str(h+lowest) + ',' + str(zstart+z))

# You could also entirely automate this process by adding lines at the end of the Python script such as:
#print('Export for Rendering: height.obj')
#print('Close')
#
# and have Mineways run the script by (on a command line or .bat file):
#mineways.exe heightfield.mwscript