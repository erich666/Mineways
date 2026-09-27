# Native Apple Silicon (ARM64) build for macOS

- **Issue Number**: #165
- **URL**: https://github.com/erich666/Mineways/issues/165
- **Created by**: wbreiler

## Description

With macOS 27 "Golden Gate" now in developer preview, this will be the **last macOS version with full Rosetta 2 support** — Apple has confirmed Rosetta 2 translation for general Intel-only apps goes away starting with the following release.

Mineways currently appears to ship Intel-only macOS builds, which means it's been relying on Rosetta 2 to run on Apple Silicon Macs. Given that:

- The first Apple Silicon Mac (M1) shipped almost 6 years ago
- macOS 27 is the last version where Intel binaries will run via Rosetta 2 without extra steps
- macOS 28 (expected fall 2027) is expected to drop general Rosetta 2 support entirely

...it'd be a good time to start working toward a native ARM64 build (or a universal binary covering both x86_64 and arm64), so the app doesn't stop working for Mac users somewhere down the line.

Happy to help test on Apple Silicon hardware if that's useful.

## Comments

### Comment 1 by erich666

Thanks for your detailed rundown of upcoming macOS changes. Mineways is mainly a Windows program, and for the Mac "port" I've relied on some person far away, Hypersun_pro/Froxcey, to make it. They occasionally do so. My whole "Mineways on Mac" doc section [is here](https://www.realtimerendering.com/erich/minecraft/public/mineways/downloads.html#macPlatformHelp).

If you have any better ways to do things, great. I do have access to a Mac (my wife's), but I'm the opposite of an expert.

