# Changelog

All notable changes to this project will be documented in this file.

## [v13.01] - 2026-09-12

### Added
* Support for Minecraft 26.2 cinnabar and sulfur blocks.

### Fixed
* Correct culling of special-case blocks with multiple IDs.
* Correct stair state round-tripping and heights above 256 in Sponge schematics.
* Improve schematic selection, spawn, and Jump to Player behavior.
* Fix macOS region paths so world chunks load correctly.
* Prevent OBJ tile export crashes when no external terrain file is configured.
* Harden macOS world loading, Import Settings, Culling Scheme persistence, exports, ZIP/NBT parsing, and failure recovery.
* Improve script encoding checks, documentation, terrain sets, and error messages.

## [v13.00] - 2026-07-13

### Added
* **Native macOS Port:** This is a native macOS (`wxWidgets` / Cocoa) port of Eric Haines's original Windows-only Mineways repository. All macOS-specific code is housed in the `Mac/` directory, while sharing the identical C++ core.
* **Full Keyboard Shortcuts Parity:** Added a comprehensive accelerator table bringing macOS keyboard shortcuts up to feature parity with the Windows version. You can now use shortcuts for all menu actions (e.g., `Cmd+O` to open, `Cmd+E` to export, numeric keys for zoom levels, etc.).

### Fixed
* **Export Dialog UX Enhancements:** The Export Dialog now seamlessly preserves and reloads your configuration states when switching between file export types (e.g., OBJ vs. USD), mirroring the Windows experience.
* **Code Health & Stability:** Resolved a large number of compiler warnings across the shared C++ core (`blockInfo.cpp`, `MinewaysMap.cpp`, `ObjFileManip.cpp`) to ensure stable, clean CI builds on macOS.
