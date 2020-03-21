@rem - Quietly open Mineways and export a map, then annotate it. See annotate_map.py. You'll need to edit this one before running it.
@rem   You'll also likely want to change the load_world.mwscript, export_map.mwscript, etc.
@rem   I'm sure this path does not work for you:
C:\Users\erich\Documents\GitHub\Mineways\x64\Release\Mineways.exe -m load_world.mwscript export_map.mwscript close.mwscript
python annotate_map.py my_world_map.png -1024 -1024 my_annotated_map.png > magick_annotate_map.bat
.\magick_annotate_map.bat
