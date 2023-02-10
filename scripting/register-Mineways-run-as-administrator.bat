@rem Sets file associations for Mineways to get used when opening Mineways script files.
@rem Change the path below to wherever your mineways.exe is located.
@rem You then need to this .bat file with "Run as administrator", e.g., right-click on file and choose this option.
@rem Setting this up is handy for making a script of defaults you like: double-click on the script
@rem (such as startup.mwscript) and Mineways will then start with these defaults.

assoc .mwscript=Mineways
ftype Mineways=C:\Users\erich\Downloads\mineways1009\mineways\mineways.exe "%%1"

pause
