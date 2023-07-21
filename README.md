# Mineways, a Minecraft mapper and exporter.
**by Eric Haines, erich@acm.org**

**begun 11/14/2011**

**http://mineways.com**

Mineways is an open-source GUI program that exports Minecraft maps into 3D models for use in 3D software, printing and rendering. Mineways supports most Minecraft: Java Edition versions and offers many settings and functionality to customize the map export to your needs.

![Simple example export rendered by spacebanana420](banner.jpg "Simple example export by [spacebanana420](https://github.com/spacebanana420)")

# Download

[Visit the project homepage](http://mineways.com) or [the Releases page](https://github.com/erich666/Mineways/releases) in this repository for executables for Windows and MacOS.

Documentation links, credits, etc are also avaliable in the homepage.
Much of the mapping and UI code is built on the open-source project [Minutor](http://seancode.com/minutor/).

Mineways does not have a Linux build, but it runs well under [WINE](http://www.winehq.org/). See more [here](https://www.realtimerendering.com/erich/minecraft/public/mineways/downloads.html#linuxPlatformHelp).

You usually want the latest version. If you have problems with it, you might try [an older version](https://www.realtimerendering.com/erich/minecraft/public/mineways/mineways.html#versions).

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

# License

Mineways uses the same [license](license.txt) as Minutor.

#

![McUsd: JG-RTX textures, rendered in Omniverse USD Composer](ov_accurate.jpg "McUsd: JG-RTX textures, rendered with Omniverse USD Composer")

_Mineways and [JG-RTX](https://github.com/jasonjgardner/jg-rtx) used to make a [test scene asset](https://github.com/usd-wg/assets) for the USD file format. Rendered with [Omniverse USD Composer](https://www.nvidia.com/en-us/omniverse/apps/create/)._
