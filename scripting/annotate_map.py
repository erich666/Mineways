#!/usr/bin/python3

# Annotate a map with coordinates and regions. Read the top of this file for how to set up to use it.
# Version 1.00 - by Eric Haines

# You need to install Python 3 and ImageMagick. You need to run this script from
# the command window. Here's how to get a command window:
# https://www.computerhope.com/issues/chusedos.htm
#
# For Python 3, you can just open a command window and type: Python
# Windows 10 will go find the install for you. Do it. You'll then probably
# need to start your window again, so that it then knows where to find Python.
#
# For ImageMagick, go to https://imagemagick.org/script/download.php and find
# the Windows 64-bit version (scroll down, choose the one on the top of the list).
# Install, and start a new command window.

# Once it's all there, it's easy enough to run this script.
# If you want to do a simple experiment, do this:
#   * Run Mineways, then open the world you want to map
#   * Select: "File | Import Settings" and then read in the file "export_map.mwscript"
#   * Running that script file should create an image called "my_world_map.png"
#   * Double-click on the file "export_and_annotate.bat"
# You should now see a new file, "my_annotated_map.png". If you don't, make sure the map
# file "my_world_map.png", "annotate_map.py", and "export_and_annotate.bat" are all in the
# same directory. Things still not running? Try the step by step process below, which also
# shows how to change the map size and how to feed this new map size into the annotator.

# 1) Export your map from Mineways. Write down the world location of the upper
#    left-hand corner of the map you exported. You can use the "File | Export Map" command in
#    Mineways and save to "my_world_map.pong". Or, using scripting, you could edit the provided
#    "export_map.mwscript" file:
#       Zoom: 1
#       Selection location: -1024, 0, -512 to 1023, 255, 511
#       Export Map: my_world_map.png
#
#    To use this script, run Mineways, then select "File | Import Settings" and read
#    in "export_map.mwscript". Doing so should produce a map file "my_world_map.png".
#    You'll need to note the upper left corner of the map, which is -1024 -512. If you're clever,
#    you could put these values in the file name, e.g., "my_world_map_-1024_-512.png" so
#    that you won't forget where you scribbled them down.
#
# 2) Run this Python 3 script. You put your output map "my_world_map.png" in the same directory as
#    this script file "annotate_map.py". Then go to your command window in the same directory
#    and type:
#        python annotate_map.py my_world_map.png -1024 -512 my_annotated_map.png > magick_annotate_map.bat
#
#    This makes a set of ImageMagick commands which you'll run in the next step.
#    Tip: if you will always run with the same names and location, you can put these all inside
#    this file (the one you're reading, "annotate_map.py"). Put the values in the code starting at
#    "default arguments" below. Once done and saved, then all you have to do on the command line is:
#        python annotate_map.py > magick_annotate_map.bat
#
# 3) The previous step creates a file in the same directory, called "magick_annotate_map.bat". Run this by
#    simply double-clicking on the file in your file explorer, or typing in your same command window:
#        .\magick_annotate_map.bat
#    The advantage of running it in the command window is that you can see any errors produced.
#
# Running "magick_annotate_map.bat" will chunk along and eventually produce "my_annotated_map.png" - enjoy!
#
# If you find yourself doing these three steps again and again, you could automate the process
# to a single .bat file you then run by double-clicking it. Put into "map_and_grid.bat":
#    <your path to >\mineways.exe -m load_world.mwscript export_map.mwscript close.mwscript
#    python annotate_map.py my_world_map.png -1024 -512 my_annotated_map.png > magick_annotate_map.bat
#    .\magick_annotate_map.bat
#
# That first line runs Mineways minimized. You will need to edit "load_world.mwscript" and put
# your world's name in place of "My Cool World" - this name is the right-column name in the
# list of worlds that Mineways' "File | Open World" shows. You can also edit export_map.mwscript
# to export whatever area you want, as before.
#
# Once all these files are set up, just double-click "map_and_grid.bat" or run it from the command line.

import struct
import imghdr
import math
import sys

# default arguments - if you always run the script the same way, put your values here.
filename = 'my_world_map.png'
fnameout = 'my_annotated_map.png'
# put the x and z coordinates of the upper left corner of your map here:
image_start = (-1024,-1024)

# if you don't want to simply print out the commands but want to directly run then, set this to True:
run_commands = True

# if you don't like the range going from 0 to 512, that it should really be 0 to 511, change this to 511:
maxrange = 511

# check command line arguments - all or none
if (len(sys.argv) != 1) and (len(sys.argv) != 5):
    raise ValueError('usage: input_map.png upper-left-x upper-left-z output_map.png')

# if all are available, suck them in
if len(sys.argv) == 5:
    filename = sys.argv[1]
    image_start = (int(sys.argv[2]), int(sys.argv[3]))
    fnameout = sys.argv[4]


def get_image_size(fname):
    '''Determine the image type of fhandle and return its size.
    from draco'''
    with open(fname, 'rb') as fhandle:
        head = fhandle.read(24)
        if len(head) != 24:
            return
        if imghdr.what(fname) == 'png':
            check = struct.unpack('>i', head[4:8])[0]
            if check != 0x0d0a1a0a:
                return
            width, height = struct.unpack('>ii', head[16:24])
        elif imghdr.what(fname) == 'gif':
            width, height = struct.unpack('<HH', head[6:10])
        elif imghdr.what(fname) == 'jpeg':
            try:
                fhandle.seek(0) # Read 0xff next
                size = 2
                ftype = 0
                while not 0xc0 <= ftype <= 0xcf:
                    fhandle.seek(size, 1)
                    byte = fhandle.read(1)
                    while ord(byte) == 0xff:
                        byte = fhandle.read(1)
                    ftype = ord(byte)
                    size = struct.unpack('>H', fhandle.read(2))[0] - 2
                # We are at a SOFn block
                fhandle.seek(1, 1)  # Skip `precision' byte.
                height, width = struct.unpack('>HH', fhandle.read(4))
            except Exception: #IGNORE:W0703
                return
        else:
            return
        return width, height
        
def check_output(outstring, fname, fnameout, format):
    if len(outstring) > 8000:   # really it's 8192 maximum command length, so we assume no command > 192 on its own
        outstring += fnameout
        print (outstring)
        outstring = 'magick ' + fnameout + ' ' + format + ' '
    return outstring

##############
# main program

outstring = 'magick '
outstring += filename + ' '

image_size = get_image_size(filename)

#print ('Image size: ' + str(image_size[0]) + ', ' + str(image_size[1]))

# determine start row and column and counts for grid lines
gridstartx = math.ceil( image_start[0] / 512 )
startx = 512 * gridstartx - image_start[0]
gridstarty = math.ceil( image_start[1] / 512 )
starty = 512 * gridstarty - image_start[1]

# output grid lines
format = '-strokewidth 2 -fill white -stroke black '
outstring += format
# vertical lines
for num in range(startx,image_size[0]-1,512):
    outstring += '-draw \"line '
    outstring += str(num)
    outstring += ',0 '
    outstring += str(num)
    outstring += ','
    outstring += str(image_size[1]-1)
    outstring += '\" '
    outstring = check_output(outstring,filename,fnameout,format)

# horizontal lines
for num in range(starty,image_size[1]-1,512):
    outstring += '-draw \"line 0,'
    outstring += str(num)
    outstring += ' '
    outstring += str(image_size[0]-1)
    outstring += ','
    outstring += str(num)
    outstring += '\" '
    outstring = check_output(outstring,filename,fnameout,format)

# output text rectangles
for y in range(starty,image_size[1]-1,512):
    gridy = math.floor((y+image_start[1])/512)
    for x in range(startx,image_size[0]-1,512):
        gridx = math.floor((x+image_start[0])/512)
        # find approximate length of string - just a copy of the "output text" outstring below
        txttst = str(gridx*512) + ',' + str(gridy*512) + '  ' + str(gridx*512+maxrange) + ',' + str(gridy*512+maxrange)
        txttst += ' / r.' + str(gridx) + '.' + str(gridy)
        outstring += '-draw \"rectangle '
        outstring += str(x)
        outstring += ','
        outstring += str(y)
        outstring += ' '
        outstring += str(x+len(txttst)*6)   # 6 is the approximate pixels per character
        outstring += ','
        outstring += str(y+29)
        outstring += '\" '
        outstring = check_output(outstring,filename,fnameout,format)

# output text
format = '-strokewidth 1 '
outstring += format
for y in range(starty,image_size[1]-1,512):
    gridy = math.floor((y+image_start[1])/512)
    for x in range(startx,image_size[0]-1,512):
        gridx = math.floor((x+image_start[0])/512)
        outstring += '-draw "text '
        outstring += str(x+7)
        outstring += ','
        outstring += str(y+19)
        outstring += ' \''
        outstring += str(gridx*512)
        outstring += ','
        outstring += str(gridy*512)
        outstring += '  '
        outstring += str(gridx*512+maxrange)
        outstring += ','
        outstring += str(gridy*512+maxrange)
        # MCA grid coordinates
        outstring += ' / r.'
        outstring += str(gridx)
        outstring += '.'
        outstring += str(gridy)
        outstring += '\'\" '
        outstring = check_output(outstring,filename,fnameout,format)

outstring += fnameout
print (outstring)


