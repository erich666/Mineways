This directory contains a bunch of scripts, Python 3 files, and batch files as examples of automating various Mineways functions. Some can be run as-is, others need to be edited first in order to load your world.

Open a file in a text editor to read what it does, and modify it as you wish. Note:
  *.mwscript - a Mineways scripting file, executed by using "File | Import Settings" in Mineways
  *.bat - a Windows batch file, a set of commands to run Mineways or other programs
  *.py - a Python 3 script, usually for generating a Mineways script or Windows batch file

See http://mineways.com/scripting.html for documentation of scripting commands.

FILES:

_README.txt - This file

annotate_map.py - Annotate a map with coordinates and regions. Read the top of this file for how to set up to use it.

close.mwscript - A trivial little script to close Mineways

custom_printer_defaults.mwscript - For custom 3D printing; shows how to set various 3D printing values.

diamond_posts.mwscript - shows how to temporarily (on export only) place diamond block posts in your world.

export_and_annotate.bat - Make the annotation map commands file magick_annotate_map.bat, and run it

export_map.mwscript - Given that you have already loaded a world manually, select an area and export it

hunk_maker.py - a simple Python script to make a Mineways .mwscript file that will export a series of separate large, square area models.

hunks_output.mwscript - A sample hunks output script, for exporting four rendering models next to each other on the map

load_world.mwscript - A simple script to load your world. You'll need to edit it before you run this one.

make_map_tiles.mwscript - An example of exporting a set of maps, each 1000 x 1000 pixels

make_slice_maps.py - A Python 3 script used to generate a Mineways script for exporting a series of maps at different depths. How the map_slices.mwscript and map_slices_reversed.mwscript scripts were generated.

map_slices.mwscript - An example of exporting a set of maps showing cutaway layers

map_slices_reversed.mwscript - An example of exporting a set of maps showing cutaway layers, output in top to bottom order

mineways_annotate_map.bat - Quietly open Mineways and export a map, then annotate it. See annotate_map.py. You'll need to edit this one before running it.

render_defaults.mwscript - The script commands for setting a default Wavefront OBJ render export, for reference.

simple_export.mwscript - A simple rendering model export script

sketchfab.mwscript - How to upload to Sketchfab in a script

startup.mwscript - A sample startup script for Mineways, called by mineways.bat in the directory above. Edit to your own preferences.
