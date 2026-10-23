#pragma once
#include "stdafx.h"
#include <vector>

// Owns only the view plan. ChunkLoader owns workers; the GUI thread owns cache and pixels.
class MapRenderer {
public:
    void Reset() { m_valid = false; m_chunks.clear(); m_next = 0; }
    bool Render(WorldGuide* world, double x, double z, int depth, int maxY,
                int width, int height, double zoom, unsigned char* pixels,
                Options* options, int* hits, int mcVersion, int version, int& error);

private:
    struct Chunk { int x, z; };
    std::vector<Chunk> m_chunks;
    size_t m_next = 0;
    bool m_valid = false, m_pan = false;
    double m_x = 0, m_z = 0, m_zoom = 0;
    int m_width = 0, m_height = 0, m_depth = 0, m_maxY = 0;
    int m_type = 0, m_options = 0, m_highlight = 0, m_cacheSize = 0;
    unsigned short m_colors = 0;
    int m_dx = 0, m_dy = 0;
};
