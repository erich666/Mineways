@rem You need to run as administrator, e.g., right-click on file and choose this option.
echo NOTE: this batch file doesn't quite work, I'm not sure why... Help appreciated!
echo Sets file associations for Mineways to get used when opening Mineways script files.
echo In other words, when you double-click on an *.mwscript file, Mineways will run.
echo This is handy for making a script of defaults you like, for when starting up Mineways;
echo double-click on the script and Mineways will then start with these defaults.
set program="%~dp0%..\mineways.exe"

assoc .mwscript=Mineways
ftype Mineways=..\%program% "%%1"

pause
