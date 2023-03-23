@rem - Quietly open Mineways and export a map, then annotate it. See annotate_map.py. You need to have Python and ImageMagick https://imagemagick.org installed and in your path. See the annotate_map.py file for how to install these.
@rem   You'll also likely want to change the load_world.mwscript and export_map.mwscript to load your world
@rem   and select what area you want to export to a PNG.

@rem   This line creates a map, in this case named my_world_map.png - change export_map.mwscript to change the area
@rem   exported and what the PNG is named:
..\Mineways.exe -m load_world.mwscript export_map.mwscript close.mwscript

@rem   This line makes a .bat file to run ImageMagick to annotate the map with coordinates. Optional! The annotate_map.py
@rem   program takes as inputs the map image file and the upper left corner's coordinates (i.e., lowest X and Z values,
@rem   that you put in export_map.mwscript).
python annotate_map.py my_world_map.png -1024 -512 my_annotated_map.png > magick_annotate_map.bat

@rem   This line then runs the .bat file produced, using ImageMagic to annotate the PNG produced.
.\magick_annotate_map.bat
