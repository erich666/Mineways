#include "MapRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>

static void shiftPixels(unsigned char* pixels, int width, int height, int dx, int dy)
{
    const size_t rowBytes = (size_t)width * 4;
    for (int row = dy > 0 ? height - 1 : 0;
         row >= 0 && row < height; row += dy > 0 ? -1 : 1) {
        unsigned char* target = pixels + row * rowBytes;
        int source = row - dy;
        if (source < 0 || source >= height) {
            memset(target, 0xff, rowBytes);
            continue;
        }
        memmove(target, pixels + source * rowBytes, rowBytes);
        const size_t clearBytes = (size_t)std::abs(dx) * 4;
        memmove(target + (dx > 0 ? clearBytes : 0),
                target + (dx < 0 ? clearBytes : 0), rowBytes - clearBytes);
        memset(target + (dx < 0 ? rowBytes - clearBytes : 0), 0xff, clearBytes);
    }
}

bool MapRenderer::Render(WorldGuide* world, double x, double z, int depth, int maxY,
                        int width, int height, double zoom, unsigned char* pixels,
                        Options* options, int* hits, int mcVersion, int version, int& error)
{
    error = 0;
    if (!pixels || width <= 0 || height <= 0 || !std::isfinite(zoom) || zoom < 0.0625)
        return false;
    if (world->type == WORLD_UNLOADED_TYPE) {
        memset(pixels, 0xff, (size_t)width * height * 4);
        Reset();
        return false;
    }
    const int colors = GetMapColormap(), highlight = GetMapHighlightID();
    bool sameStyle = m_valid && width == m_width && height == m_height && zoom == m_zoom &&
        depth == m_depth && maxY == m_maxY && world->type == m_type &&
        options->worldType == m_options && colors == m_colors && highlight == m_highlight &&
        options->currentCacheSize == m_cacheSize;
    long long originX, originZ;
    GetMapChunkOrigin(x, z, width, height, zoom, &originX, &originZ);
    if (!sameStyle || x != m_x || z != m_z) {
        ChunkLoaderCancelRequests();
        long long oldX = 0, oldZ = 0;
        if (sameStyle)
            GetMapChunkOrigin(m_x, m_z, width, height, zoom, &oldX, &oldZ);
        const long long dx = originX - oldX, dy = originZ - oldZ;
        m_pan = sameStyle && std::abs(dx) < width && std::abs(dy) < height;
        m_dx = m_pan ? (int)dx : 0; m_dy = m_pan ? (int)dy : 0;
        if (m_pan)
            shiftPixels(pixels, width, height, m_dx, m_dy);
        else
            memset(pixels, 0xff, (size_t)width * height * 4);
        m_x = x; m_z = z; m_zoom = zoom;
        m_width = width; m_height = height; m_depth = depth; m_maxY = maxY;
        m_type = world->type; m_options = options->worldType;
        m_colors = (unsigned short)colors; m_highlight = highlight;
        m_cacheSize = options->currentCacheSize;
        m_valid = true; m_next = 0; m_chunks.clear();

        const int centerX = (int)std::floor(x / 16), centerZ = (int)std::floor(z / 16);
        // Bound the plan by the cache, even when millions of chunks fit on screen.
        int viewMinX, viewMinZ, viewMaxX, viewMaxZ;
        GetMapChunkBounds(x, z, width, height, zoom, &viewMinX, &viewMinZ, &viewMaxX, &viewMaxZ);
        const int viewRadius = max(max(centerX - viewMinX, viewMaxX - centerX),
                                   max(centerZ - viewMinZ, viewMaxZ - centerZ));
        int radius = 0;
        while (radius < viewRadius) {
            int next = radius + 1;
            long long columns = min(centerX + next, viewMaxX) - max(centerX - next, viewMinX) + 1;
            long long rows = min(centerZ + next, viewMaxZ) - max(centerZ - next, viewMinZ) + 1;
            if (columns * rows > options->currentCacheSize * 9LL / 10) break;
            radius = next;
        }
        const int minX = max(centerX - radius, viewMinX), minZ = max(centerZ - radius, viewMinZ);
        const int maxX = min(centerX + radius, viewMaxX), maxZ = min(centerZ + radius, viewMaxZ);
        for (int cz = minZ; cz <= maxZ; ++cz)
            for (int cx = minX; cx <= maxX; ++cx)
                m_chunks.push_back({cx, cz});
        std::stable_sort(m_chunks.begin(), m_chunks.end(), [=](const Chunk& a, const Chunk& b) {
            const long long ax = (long long)a.x - centerX, az = (long long)a.z - centerZ;
            const long long bx = (long long)b.x - centerX, bz = (long long)b.z - centerZ;
            return ax * ax + az * az < bx * bx + bz * bz;
        });
        int left, top, right, bottom, unusedX, unusedZ;
        GetMapChunkRect(x, z, width, height, zoom, minX, minZ, &left, &top, &unusedX, &unusedZ);
        GetMapChunkRect(x, z, width, height, zoom, maxX, maxZ, &unusedX, &unusedZ, &right, &bottom);
        left = clamp(left, 0, width); right = clamp(right, 0, width);
        top = clamp(top, 0, height); bottom = clamp(bottom, 0, height);
        for (int row = 0; row < height; ++row) {
            unsigned char* target = pixels + (size_t)row * width * 4;
            if (row < top || row >= bottom) memset(target, 0x80, (size_t)width * 4);
            else {
                memset(target, 0x80, (size_t)left * 4);
                memset(target + (size_t)right * 4, 0x80, (size_t)(width - right) * 4);
            }
        }
    }

    auto paint = [&](int cx, int cz) {
        int left, top, right, bottom;
        GetMapChunkRect(x, z, width, height, zoom, cx, cz, &left, &top, &right, &bottom);
        left = max(0, left); top = max(0, top);
        right = min(width, right); bottom = min(height, bottom);
        if (left >= right || top >= bottom) return;
        int result = DrawMapStrips(world, x, z, depth, maxY, width, height, zoom,
            pixels, options, hits, nullptr, mcVersion, version, left, top, right, bottom);
        if (result < 0 || error >= 0) error = result < 0 ? result : error | result;
    };
    auto draw = [&](int cx, int cz) {
        paint(cx, cz);
        if (ChunkNeedsWestEdgeRedraw(cx + 1, cz, depth, options)) paint(cx + 1, cz);
    };
    // Cache misses must never perform disk I/O while painting.
    SetNonBlockingDraw(true);
    int doneX[64], doneZ[64], loadError = 0;
    int count = ChunkLoaderCollect(doneX, doneZ, 64, loadError);
    if (loadError != 0) error = loadError;
    for (int i = 0; i < count; ++i) draw(doneX[i], doneZ[i]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    while (m_next < m_chunks.size() && std::chrono::steady_clock::now() < deadline) {
        const Chunk& chunk = m_chunks[m_next];
        void* cached = nullptr;
        if (Cache_Find(chunk.x, chunk.z, &cached)) {
            int left, top, right, bottom;
            GetMapChunkRect(x, z, width, height, zoom, chunk.x, chunk.z, &left, &top, &right, &bottom);
            const bool preserved = m_pan && left >= max(0, m_dx) && top >= max(0, m_dy) &&
                right <= min(width, width + m_dx) && bottom <= min(height, height + m_dy);
            if (!preserved) draw(chunk.x, chunk.z);
        } else if (ChunkLoaderUsable(world)) {
            if (!ChunkLoaderRequest(world, chunk.x, chunk.z, mcVersion, version, options->worldType)) {
                if (!ChunkLoaderBusy()) { error = -1; m_next = m_chunks.size(); }
                break;
            }
        } else {
            int result = PrefetchBlock(world, chunk.x, chunk.z, mcVersion, version, options->worldType);
            if (result < 0 || error >= 0) error = result < 0 ? result : error | result;
            draw(chunk.x, chunk.z);
        }
        ++m_next;
    }
    SetNonBlockingDraw(false);
    bool pending = m_next < m_chunks.size() || ChunkLoaderBusy();
    if (!pending) block_pool_trim();
    return pending;
}
