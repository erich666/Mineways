// LocationDialog.cpp — wx replacement for Win/Location.cpp
#include <wx/wx.h>
#include "LocationDialog.h"

static int gX = 0, gZ = 0;

void getLocationData(int& x, int& z) { x = gX; z = gZ; }
void setLocationData(int x, int z)   { gX = x; gZ = z; }

int doLocation(wxWindow* parent)
{
    wxDialog dlg(parent, wxID_ANY, "Go To Location",
                 wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

    wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 6, 12);

    auto* xCtrl = new wxTextCtrl(&dlg, wxID_ANY, wxString::Format("%d", gX));
    auto* zCtrl = new wxTextCtrl(&dlg, wxID_ANY, wxString::Format("%d", gZ));
    grid->Add(new wxStaticText(&dlg, wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(xCtrl, 1, wxEXPAND);
    grid->Add(new wxStaticText(&dlg, wxID_ANY, "Z:"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(zCtrl, 1, wxEXPAND);
    grid->AddGrowableCol(1);

    top->Add(grid, 0, wxEXPAND | wxALL, 12);
    top->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    dlg.SetSizerAndFit(top);
    dlg.CentreOnParent();

    if (dlg.ShowModal() != wxID_OK) return 0;

    long x, z;
    if (!xCtrl->GetValue().ToLong(&x) || !zCtrl->GetValue().ToLong(&z)) {
        wxMessageBox("X and Z must be integers.", "Invalid input", wxOK | wxICON_ERROR, parent);
        return 0;
    }
    gX = (int)x; gZ = (int)z;
    return 1;
}
