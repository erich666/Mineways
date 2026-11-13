// MinewaysApp.cpp — wxApp entry point for the macOS / wxWidgets build
#include <clocale>
#include <wx/wx.h>
#include "MinewaysFrame.h"
#include "ImportSettings.h"
#include <wx/log.h>

class MinewaysApp : public wxApp {
public:
    bool OnInit() override {
        gMacHeadless = argc > 1 && wxString(argv[1]) == "-headless";
        if (gMacHeadless) {
            wxLog::SetActiveTarget(new wxLogStderr());
            if (argc != 3) {
                fprintf(stderr, "Usage: Mineways -headless script.mwscript\n");
                return false;
            }
        } else if (!(argc == 2 && !wxString(argv[1]).StartsWith("-")) && !wxApp::OnInit()) return false;
        // wcstombs/mbstowcs throughout the port need a non-"C" locale to
        // round-trip non-ASCII paths (accented/CJK world or export folder names).
        setlocale(LC_ALL, "");
        wxInitAllImageHandlers();
        auto* frame = new MinewaysFrame(nullptr);
        SetTopWindow(frame);
        if (!gMacHeadless) {
            frame->Show();
            if (argc == 2 && !wxString(argv[1]).StartsWith("-")) {
                wxString message;
                if (!ImportSettingsFile(frame, wxString(argv[1]), message) || !message.empty())
                    wxMessageBox(message, "Import Settings", wxOK | wxICON_INFORMATION, frame);
            }
        }
        return true;
    }
    int OnRun() override {
        if (!gMacHeadless) return wxApp::OnRun();
        wxString message;
        bool ok = ImportSettingsFile(GetTopWindow(), wxString(argv[2]), message);
        if (!message.empty()) fprintf(stderr, "%s\n", message.utf8_str().data());
        delete GetTopWindow();
        return ok ? 0 : 1;
    }
};

wxIMPLEMENT_APP(MinewaysApp);
