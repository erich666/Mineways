@rem export_and_annotate.bat - Make the annotation map commands file magick_annotate_map.bat, and run it
@rem 
@rem This is a simple script to run annotate_map.py and then create the map from the commands it generated.
@rem See the top of annotate_map.py for instructions.

@rem See annotate_map.py and how to set up the command line arguments:
python annotate_map.py > magick_annotate_map.bat
.\magick_annotate_map.bat
