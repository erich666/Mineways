// MapPanel.cpp — wxPanel that displays the Mineways map
#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include "MapPanel.h"
#include "MinewaysFrame.h"
#include "stdafx.h"   // block info types (for IDBlock)

// Globals owned by MinewaysFrame.cpp
extern double& GetCurX();
extern double& GetCurZ();
extern double& GetCurScale();
extern float&  GetMinZoom();
extern BOOL    IsLoaded();
extern unsigned char* GetMapBits();
extern int     GetMapWidth();
extern int     GetMapHeight();
extern int     GetMinHeight();
extern int     GetMaxHeight();
extern int&        GetCurDepth();
extern int&        GetTargetDepth();
extern BOOL&       GetHighlightOn();
extern int&        GetStartHiX();
extern int&        GetStartHiZ();
extern Options*    GetOptions();
extern WorldGuide* GetWorldGuide();
extern const char* QueryBlock(int bx, int by, int* mx, int* my, int* mz, int* type, int* dataVal, int* biome);
extern void    RedrawMapIntoBuffer();
extern MinewaysFrame* gFrame;   // ponytail: fine as extern, single instance

wxBEGIN_EVENT_TABLE(MapPanel, wxPanel)
    EVT_PAINT(MapPanel::OnPaint)
    EVT_SIZE(MapPanel::OnSize)
    EVT_LEFT_DOWN(MapPanel::OnLeftDown)
    EVT_LEFT_UP(MapPanel::OnLeftUp)
    EVT_MOTION(MapPanel::OnMouseMove)
    EVT_RIGHT_DOWN(MapPanel::OnRightDown)
    EVT_RIGHT_UP(MapPanel::OnRightUp)
    EVT_MOUSEWHEEL(MapPanel::OnMouseWheel)
    EVT_KEY_DOWN(MapPanel::OnKeyDown)
wxEND_EVENT_TABLE()

MapPanel::MapPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

MapPanel::~MapPanel() { free(m_rgbBuf); }

void MapPanel::ResizeBuffer(int w, int h)
{
    if (w == m_w && h == m_h) return;
    m_w = w; m_h = h;
    free(m_rgbBuf);
    m_rgbBuf = (w > 0 && h > 0) ? (unsigned char*)malloc((size_t)w * h * 3) : nullptr;
    if (gFrame) gFrame->OnMapPanelSize(w, h);
}

void MapPanel::RedrawMap()
{
    RedrawMapIntoBuffer();
    Refresh(false);
}

void MapPanel::OnSize(wxSizeEvent& e)
{
    wxSize sz = e.GetSize();
    ResizeBuffer(sz.GetWidth(), sz.GetHeight());
    RedrawMap();
    e.Skip();
}

void MapPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    unsigned char* bits = GetMapBits();
    int w = GetMapWidth(), h = GetMapHeight();
    if (!bits || w <= 0 || h <= 0) {
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        return;
    }
    // wxImage expects packed RGB (no alpha channel; we have RGBA).
    // Convert into m_rgbBuf, a member buffer resized alongside the panel (not reallocated
    // every paint). static_data=true tells wxImage not to take/free ownership of it; the
    // buffer only needs to outlive the wxBitmap(img) copy below.
    if (!m_rgbBuf || w != m_w || h != m_h) { dc.Clear(); return; }
    unsigned char* rgb = m_rgbBuf;
    for (int i = 0, j = 0; i < w * h * 4; i += 4, j += 3) {
        rgb[j]   = bits[i];
        rgb[j+1] = bits[i+1];
        rgb[j+2] = bits[i+2];
    }
    wxImage img(w, h, rgb, true /*static: caller-owned buffer*/);
    dc.DrawBitmap(wxBitmap(img), 0, 0, false);
}

// ─── Mouse handling ─────────────────────────────────────────────────────────
void MapPanel::OnLeftDown(wxMouseEvent& e)
{
    m_dragging = true;
    m_dragStart = e.GetPosition();
    m_dragStartCX = GetCurX();
    m_dragStartCZ = GetCurZ();
    CaptureMouse();
    SetFocus();
}

void MapPanel::OnLeftUp(wxMouseEvent& e)
{
    if (m_dragging) { m_dragging = false; ReleaseMouse(); }
    e.Skip();
}

void MapPanel::OnMouseMove(wxMouseEvent& e)
{
    wxPoint pos = e.GetPosition();
    if (m_dragging) {
        double scale = GetCurScale();
        GetCurX() = m_dragStartCX - (pos.x - m_dragStart.x) / scale;
        GetCurZ() = m_dragStartCZ - (pos.y - m_dragStart.y) / scale;
        RedrawMap();
    } else if (m_selecting) {
        int mx, my, mz, type, dataVal, biome;
        QueryBlock(pos.x, pos.y, &mx, &my, &mz, &type, &dataVal, &biome);
        int x0 = GetStartHiX(), z0 = GetStartHiZ();
        SetHighlightState(TRUE,
                          wxMin(x0,mx), GetTargetDepth(), wxMin(z0,mz),
                          wxMax(x0,mx), GetCurDepth(),    wxMax(z0,mz),
                          GetMinHeight(), GetMaxHeight(), HIGHLIGHT_UNDO_IGNORE);
        RedrawMap();
    } else if (IsLoaded()) {
        int mx, my, mz, type, dataVal, biome;
        const char* label = QueryBlock(pos.x, pos.y, &mx, &my, &mz, &type, &dataVal, &biome);
        if (gFrame && label)
            gFrame->UpdateStatusBar(mx, mz, my, label, type, dataVal, biome);
    }
    e.Skip();
}

void MapPanel::OnRightDown(wxMouseEvent& e)
{
    if (!IsLoaded()) { e.Skip(); return; }
    wxPoint pos = e.GetPosition();
    int mx, my, mz, type, dataVal, biome;
    QueryBlock(pos.x, pos.y, &mx, &my, &mz, &type, &dataVal, &biome);
    // Start selection rectangle at this world position
    GetStartHiX() = mx;
    GetStartHiZ() = mz;
    GetHighlightOn() = TRUE;
    SetHighlightState(TRUE, mx, GetTargetDepth(), mz, mx, GetCurDepth(), mz,
                      GetMinHeight(), GetMaxHeight(), HIGHLIGHT_UNDO_IGNORE);
    m_selecting = true;
    CaptureMouse();
    SetFocus();
}

void MapPanel::OnRightUp(wxMouseEvent& e)
{
    if (m_selecting) { m_selecting = false; ReleaseMouse(); }
    e.Skip();
}

void MapPanel::OnMouseWheel(wxMouseEvent& e)
{
    double& scale = GetCurScale();
    double factor = (e.GetWheelRotation() > 0) ? 1.1 : (1.0 / 1.1);
    scale = wxMax((double)GetMinZoom(), wxMin(40.0, scale * factor));
    RedrawMap();
}

void MapPanel::OnKeyDown(wxKeyEvent& e)
{
    double step = 8.0 / GetCurScale();
    bool changed = true;
    switch (e.GetKeyCode()) {
    case WXK_LEFT:  GetCurX() -= step; break;
    case WXK_RIGHT: GetCurX() += step; break;
    case WXK_UP:    GetCurZ() -= step; break;
    case WXK_DOWN:  GetCurZ() += step; break;
    case WXK_SPACE: {
        // If a selection is active, snap bottom depth to minimum solid height in selection
        int on, minx, miny, minz, maxx, maxy, maxz;
        GetHighlightState(&on, &minx, &miny, &minz, &maxx, &maxy, &maxz, GetMinHeight());
        if (on && IsLoaded()) {
            int newDepth = GetMinimumSelectionHeight(
                const_cast<WorldGuide*>(GetWorldGuide()),
                const_cast<Options*>(GetOptions()),
                minx, minz, maxx, maxz,
                GetMinHeight(), GetMaxHeight(),
                true, !e.ShiftDown(), maxy);
            GetTargetDepth() = newDepth;
            if (gFrame) gFrame->UpdateBottomSlider(newDepth);
        }
        break;
    }
    default: e.Skip(); changed = false; break;
    }
    if (changed) RedrawMap();
}
