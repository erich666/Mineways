#!/usr/bin/python3

# A Python 3 script used to generate a Mineways script for exporting a series of maps at different depths.
# How the map_slices.mwscript and map_slices_reversed.mwscript scripts were generated.
#
# To run, in a command window in this directory do:
#
#    python make_slice_maps.py > my_map_slices.mwscript
#
# This then generates the file my_slices.mwscript, which you can read in using File | Import Settings in Mineways.

# if you want to reverse the order of the maps, i.e., slice 0 will be at height 255, set "reverse" to True:
reverse = False

for num in range(0,256):     # iterate between levels 0 and 255, inclusive
    print ("Select maximum height: %d" % num)
    slice = (255-num) if reverse else num;
    print ("Export map: slice_%d.png" % slice )

# do the following to ensure the height goes back to normal, in case the range above is changed
print ()
print ("// Restore selection height of 255")
print ("Select maximum height: 255")