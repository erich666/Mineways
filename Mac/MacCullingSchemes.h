#pragma once
#include <wx/wx.h>

// Mac-side entry point for the Culling Scheme manager dialog.
// Called from MinewaysFrame; opens the scheme list and editor.
void doCullingSchemesMac(wxWindow* parent);

// Returns the name of the last scheme selected (or L"Standard").
const wchar_t* getSelectedCullingSchemeW();

// Select a saved scheme by name ("Standard" or empty = clear to default), matching
// Windows' commandLoadCullingScheme. Returns false if no scheme with that name exists.
bool SelectCullingSchemeByName(const wxString& name);
