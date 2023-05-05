# Mineways, a Minecraft mapper and exporter.
**by Eric Haines, erich@acm.org**

**begun 11/14/2011**

**http://mineways.com**

Mineways is an open source GUI program that exports Minecraft maps into 3D models for use in 3D software, printing and rendering.

Mineways supports most Minecraft versions and offers many settings and functionality to customize the map export to your needs.

# Download

[Visit the project homepage](http://mineways.com) for executables for Windows and MacOS, documentation links, credits, etc.
Much of the mapping and UI code is built on the open-source project [Minutor](http://seancode.com/minutor/).

Mineways does not have a Linux build, but it runs well under WINE

There are different major versions of Mineways. You usually want the latest version; version 1.15 can be used on older worlds.
* **Version 6.** works on Minecraft 1.2 to 1.13 and beyond, but is 64-bit only.
* **Version 5.** works on Minecraft 1.2 to 1.9 and beyond, adds scripting language.
* **Version 4.** works on Minecraft 1.2 to 1.9-ish and beyond, 64-bit for large exports.
* **Version 3.** works on Minecraft 1.2 to 1.7 worlds (Anvil, stair geometry changes, and new texturing).
* **Version 2.x** works on Minecraft 1.2 worlds to 1.4 (Anvil format).
* **Version 1.x** works on Minecraft 1.1 worlds and earlier (McRegion format).

# Compiling

Source files are here:

* **Win/** contains the Windows version of Mineways (in C++).
* **TileMaker/** contains the TileMaker for Mineways, which takes the individual block textures and forms a terrainExt.png file for use by Mineways. This allows you to replace any terrain textures with your own custom tiles.

## Windows
You can compile Mineways with Visual Studio Community 2022
- Install the "Desktop development with C++" workload
- Go to "Individual components", search on "MFC", and choose "C++ MFC for latest v143 build tools (x86 & x64)".
- Open Mineways.sln in Visual C++, switch the target to Release and x64, compile the solution to `generate Mineways.exe`

Sorry, other platforms are not directly supported, though Mineways runs fine under [WINE](http://www.winehq.org/) and we also provide a Mac-specific version.

If you want to work on the mapping part of this program on another platform, see [Minutor](http://seancode.com/minutor/), which *is* supported on Mac and Linux.

License
-------------

Mineways uses the same [license](license.txt) as Minutor.
