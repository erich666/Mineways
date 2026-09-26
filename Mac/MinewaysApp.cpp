// MinewaysApp.cpp — wxApp entry point for the macOS / wxWidgets build
#include <clocale>
#include <wx/wx.h>
#include "MinewaysFrame.h"

class MinewaysApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        // wcstombs/mbstowcs throughout the port need a non-"C" locale to
        // round-trip non-ASCII paths (accented/CJK world or export folder names).
        setlocale(LC_ALL, "");
        wxInitAllImageHandlers();
        auto* frame = new MinewaysFrame(nullptr);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MinewaysApp);
