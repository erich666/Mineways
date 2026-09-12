@rem Mineways startup scripting example.

@rem Starts Mineways, first making the starting window larger and running whatever commands are in the scripting/startup.mwscript file.

@rem These command-line options can be changed: make the window a different size, add more scripts to run, make Mineways look in
@rem a different directory for your worlds at startup, etc. Edit the last line in this file as you like!

@rem See http://mineways.com/scripting.html#clo for more information and options.

@echo off
if not exist "mineways.exe" (
    echo.
    echo ERROR: mineways.exe was not found in this folder.
	echo I'm guessing you've downloaded the Github repository, not the distribution.
    echo Please download Mineways from http://www.realtimerendering.com/erich/minecraft/public/mineways/
    echo.
    pause
    exit /b 1
)

mineways.exe -w 700 700 scripting/startup.mwscript