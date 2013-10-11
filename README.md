#Mineways, a Minecraft mapper and exporter.
**by Eric Haines, erich@acm.org**

**begun 11/14/2011**

**http://vokselia.com**

[Visit the project homepage](http://mineways.com/index.htm) for documentation links, credits, etc.
[Here is a link to minutor where most of the original source code came from.](http://seancode.com/minutor/)

You need to pick the right version of Mineways, depending on your version of Minecraft:

* **Version 2.x** works on Minecraft 1.2 worlds and after (Anvil format).
* **Version 1.x** works on Minecraft 1.1 worlds and earlier (McRegion format).

All 3 platform source files are here:

* **MinewaysMap/** contains the map generating code used by all 3 versions. (in C)
* **Win/** contains the Windows UI (in C++)
* **OSX/** contains the OSX UI (in ObjC).
* **./** contains the GTK UI. (in C)

Compiling
--------------

* Windows - (You can compile this with Visual Studio Express 2010)
Open Minewayswin.sln in Visual C++, switch the target to Release, compile the solution to
		`generate Mineways.exe`

Sorry, other platforms are not directly supported, though Mineways runs fine under [WINE](http://www.winehq.org/) and we also provide a Mac-specific version.

If you want to work on the mapping part of this program on another platform, see [Minutor](http://seancode.com/minutor/), which *is* supported on Mac and Linux.