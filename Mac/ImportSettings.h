#pragma once
#include <wx/wx.h>

// Ctrl+I "Import Settings": re-imports export settings from the '#'-comment header
// Mineways writes into every exported OBJ/USD/VRML/schematic file (Win/ObjFileManip.cpp's
// writeStatistics), or runs a Mineways batch script (a superset of that header syntax plus
// action commands) — ported from Win/Mineways.cpp's importSettings/importModelFile/
// readAndExecuteScript/interpretImportLine/interpretScriptLine.
//
// Not ported (documented, not silently ignored — an unrecognized line is reported as a
// syntax error like any other): Sketchfab publish commands (no Mac Sketchfab integration
// at all yet), mouse-button remapping ("Set mouse order"/"Reset mouse" — a Windows input
// convention), "Custom printer" (defines new physical-material cost entries), block-name
// "Translate:" aliasing for third-party texture packs, "Chunk size:", "Set unknown block
// ID:", log-file management ("Save log file:"), and the whole "Change blocks:" sub-language
// for programmatic block editing before export — each of these is a substantial feature in
// its own right, not incidental to "re-import my settings".
//
// Returns true on success (errorOut may still carry non-fatal warnings). Returns false on
// failure, with errorOut set to a human-readable message.
bool ImportSettingsFile(wxWindow* parent, const wxString& path, wxString& errorOut);
