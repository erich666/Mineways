#pragma once
#include <wx/wx.h>

// wxPanel that renders the Minecraft map via DrawMap() into a pixel buffer,
// then blits it using wxClientDC.
class MapPanel : public wxPanel {
public:
    MapPanel(wxWindow* parent);
    ~MapPanel();

    void RedrawMap();      // call DrawMap() + Refresh()
    void ResizeBuffer(int w, int h);

private:
    int m_w = 0, m_h = 0;
    unsigned char* m_rgbBuf = nullptr;  // reused RGB conversion buffer, resized alongside m_w/m_h

    // left-drag (pan) state
    bool   m_dragging = false;
    double m_dragStartCX = 0, m_dragStartCZ = 0;
    wxPoint m_dragStart{0,0};

    // right-drag (selection) state
    bool   m_selecting = false;

    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnLeftUp(wxMouseEvent&);
    void OnMouseMove(wxMouseEvent&);
    void OnRightDown(wxMouseEvent&);
    void OnRightUp(wxMouseEvent&);
    void OnMouseWheel(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);

    wxDECLARE_EVENT_TABLE();
};
