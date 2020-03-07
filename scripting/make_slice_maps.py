#!/usr/bin/python3

# The script used to generate the actual commands in make_slice_maps.mwscript
#
# To run, in a command window in this directory do:
#
#    python make_slice_maps.py > my_slices.mwscript
#
# This then generates the file my_slices.mwscript, which you can read in using File | Import Settings in Mineways.

for num in range(0,256):     # iterate between levels 0 and 255, inclusive
    print ("Select maximum height: %d" % num)
    print ("Export map: slice_%d.png" % num)

# do the following to ensure the height goes back to normal, in case the range above is changed
print ()
print ("// Restore selection height of 255")
print ("Select maximum height: 255")