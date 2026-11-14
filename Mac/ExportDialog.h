#pragma once
#include <wx/wx.h>
#include "stdafx.h"   // ExportFileData (Win32 shims route to compat.h on non-WIN32)

// Seed an ExportFileData with Windows' "Export for Rendering" / "Export for
// 3D Printing" default presets (Win/Mineways.cpp initializeViewExportData /
// initializePrintExportData). Also available as buttons inside the dialog.
void InitViewExportData(ExportFileData& efd);
void InitPrintExportData(ExportFileData& efd);

// Full export options dialog, operating directly on the shared ExportFileData
// struct (same contract as Windows' ExportPrint.cpp) plus a separate output
// path buffer (Windows keeps the save-as path outside ExportFileData too).
// Returns wxID_OK or wxID_CANCEL; on wxID_OK, efd and outputPath are updated
// in place with validated values.
int doExportDialog(wxWindow* parent, ExportFileData& efd,
                   wchar_t* outputPath, size_t outputPathLen,
                   int selMinX, int selMinY, int selMinZ,
                   int selMaxX, int selMaxY, int selMaxZ);
