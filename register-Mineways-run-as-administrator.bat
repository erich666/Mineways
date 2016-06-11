@rem Set file associations for Mineways to get used when opening Mineways script files.
set program="%~dp0%mineways.exe"

assoc .mwscript=Mineways
ftype Mineways=%program% "%%1"

pause
