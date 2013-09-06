#Mineways, a Minecraft mapper and exporter.
**by Eric Haines, erich@acm.org**

**begun 11/14/2011**

**http://vokselia.com**

[Visit the project homepage](http://mineways.com/index.htm) for documentation links, credits, etc.
[Here is a link to minatour where most of the original source code came from.](http://seancode.com/minutor/)

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
Run make_installer.bat to make the Mineways.msi installer.
(This part requires you to install WiX)

* OSX -
	Open Mineways.xcodeproj in XCode. Compile the solution to
		generate Mineways.app
	Run DiskUtility to create a dmg.  Put app in dmg.  :)

* Linux -
Run:
```$ make```
```$ sudo make install```
Linux .deb creation -
	first you need to create a pbuilder environment for Lucid (10.04 LTS).

	```$ pbuilder-dist lucid create```

	if you're on amd64 and want to create an i386 version, do this too:

	```$ pbuilder-dist lucid i386 create```

	once that's done, you won't need to do that again, except maybe
	to run pbuilder-dist lucid update  to install new patches
	Next we need to build the tar.gz and .dsc

	```$ cd Mineways```
	```$ debuild -S -us```

	(`-us` means don't sign the .dsc)
	Finally we want to compile the .dsc and tar.gz into a .deb

	```$ cd ..```
	```$ pbuilder-dist lucid build *.dsc```

	and if we want to do it for i386 on amd64:

	```$ pbuilder-dist lucid i386 build *.dsc```
