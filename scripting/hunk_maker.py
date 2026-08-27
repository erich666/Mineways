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
# Install Python, then edit this file to change the arguments at the top of this file
# to whatever area in your world you want to export and run it in a command window
# (Do NOT run it in Powershell, which causes problems, use CMD instead. See
# https://www.reddit.com/r/mineways/comments/1vxwr9n/error_exporting_hunks_for_rendering/ ):
#    cd [paste in the directory here on your machine that contains hunk_maker.py]
#    python hunk_maker.py > hunks.mwscript
#
# This creates a file called "hunks.mwscript". In Mineways you then load your world,
# optionally set your terrain file, and then choose File | Import Settings and select
# this hunks.mwscript file. The script will then run and export a bunch of OBJ and MTL
# files where you specified, along with block textures in the "texture" directory.

# The file name of the output script, put in the same directory as hunk_maker.py
output_script = 'hunks.mwscript'

# The output file prefix when the script is run in Mineways. For example, without changes,
# the first model file created by Mineways would be c:/temp/hunk_x-200_z-200.obj
# Forward slashes are preferred; note that if you use
# backslashes, they have to be "doubled": 'c:\\temp\\hunk'
file = 'c:/temp/hunk'

# Starting world location. Note that in Mineways you can see map coordinates for your mouse
# in the lower left corner of the screen, e.g. "-29,311"
xstart = -200
zstart = -200

# Ending world location
xend = 200
zend = 200

# How big is a hunk? For example, '100' means exporting 100x100. You can certainly go much larger,
# depending on your machine's memory capabilities.
sz = 100

# Y height extents. You may want to change these; 0-255 is the maximum, but people
# often don't want all the underground blocks, so a ystart = 50 is usually fine (62 is "sea level")
# For 1.17 the valid range is -64 to 319
ystart = 0
yend = 255

with open(output_script, 'w', encoding='utf-8', newline='\n') as f:
    # These optional commands temporarily suppress errors etc. and instead send them to a log file:
    f.write("Show informational: script\n")
    f.write("Show warning: script\n")
    f.write("Show error: script\n")
    f.write("Save log file: c:/temp/script_run.log\n")
    # Here's where to put the settings you want for your output files:
    f.write("Set render type: Wavefront OBJ absolute indices\n")
    f.write("File type: Export tiles for textures to directory texture\n")
    f.write("Center model: NO\n")
    f.write("Create block faces at the borders: NO\n")

    # Now loop, selecting and exporting. Done!
    x = xstart
    while x < xend:
        z = zstart
        while z < zend:
            s = f'Selection location: {x}, {ystart}, {z} to {x+sz-1}, {yend}, {z+sz-1}\n'
            f.write(s)
            s = f'Export for rendering: {file}_x{x}_z{z}.obj\n'
            f.write(s)
            z += sz
        x += sz

print(f"Successfully generated {output_script} - in Mineways execute it by using File | Import Settings")