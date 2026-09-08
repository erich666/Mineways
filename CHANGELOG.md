# Changelog

All notable changes to this project will be documented in this file.

## [v13.00] - 2026-07-13

### Added
* **Native macOS Port:** This is a native macOS (`wxWidgets` / Cocoa) port of Eric Haines's original Windows-only Mineways repository. All macOS-specific code is housed in the `Mac/` directory, while sharing the identical C++ core.
* **Full Keyboard Shortcuts Parity:** Added a comprehensive accelerator table bringing macOS keyboard shortcuts up to feature parity with the Windows version. You can now use shortcuts for all menu actions (e.g., `Cmd+O` to open, `Cmd+E` to export, numeric keys for zoom levels, etc.).

### Fixed
* **Export Dialog UX Enhancements:** The Export Dialog now seamlessly preserves and reloads your configuration states when switching between file export types (e.g., OBJ vs. USD), mirroring the Windows experience.
* **Code Health & Stability:** Resolved a large number of compiler warnings across the shared C++ core (`blockInfo.cpp`, `MinewaysMap.cpp`, `ObjFileManip.cpp`) to ensure stable, clean CI builds on macOS.
