#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

struct ModDimension {
    std::string id;
    int minY = 0;
    int height = 256;
    double coordinateScale = 1.0;
};

struct ModTexture {
    std::string name;
    unsigned width = 0;
    unsigned height = 0;
    bool opaque = false;
    std::string sourcePath;
    std::vector<unsigned char> rgba;
};

struct ModFace {
    // down, up, north, south, west, east
    int direction = 0;
    float uv[4] = {};
    int rotation = 0;
    int texture = 0;
    int cullface = -1;
};

struct ModElement {
    float from[3] = {};
    float to[3] = {};
    // none = -1, x = 0, y = 1, z = 2
    int axis = -1;
    float angle = 0;
    float origin[3] = {};
    bool rescale = false;
    std::vector<ModFace> faces;
};

struct ModPolygon {
    int texture = 0;
    int count = 0;
    // Position in model pixels, then UV in texture pixels (v down).
    float vertices[4][5] = {};
};

struct ModGeometry {
    std::vector<ModElement> elements;
    std::vector<ModPolygon> polygons;
};

struct ModModel {
    int x = 0;
    int y = 0;
    int z = 0; // AE2 blockstate roll, applied before X and Y.
    bool uvlock = false;
    std::vector<ModElement> elements;
    std::vector<ModPolygon> polygons;
    std::shared_ptr<const ModGeometry> geometry;
    const std::vector<ModElement>& Elements() const { return geometry ? geometry->elements : elements; }
    const std::vector<ModPolygon>& Polygons() const { return geometry ? geometry->polygons : polygons; }
};

struct ModStateModel {
    std::string selector;
    std::map<std::string, std::string> properties;
    unsigned color = 0;
    unsigned alpha = 255;
    bool invisible = false;
    std::vector<ModModel> models;
};

struct ModBlockState {
    std::string canonical;
    const ModStateModel* definition = nullptr;
    bool waterlogged = false;
};

// Load/clear only after stopping chunk workers and unloading the current world.
// Failed loads leave the current pack intact. Paths are UTF-8.
bool LoadModPack(const std::string& manifestPath, std::string& error);
void ClearModPack();
bool ModPackLoaded();
// Decode only the textures needed by an export. Failure leaves the decoded cache unchanged.
bool EnsureModTextures(const std::vector<int>& indices, std::string& error);
void ReleaseModTextures();
const std::string& ModPackPath();
const std::vector<ModTexture>& ModTextures();
const std::vector<ModDimension>& ModDimensions();
int ModBlockTypeLimit();
bool IsModBlock(int type);
bool ModBlockWaterlogged(int type, int dataVal);
bool HasModBlock(const std::string& name);
// Returns 1 for a supported state, 0 for an unknown selector, -1 for invalid/capacity errors.
int ResolveModState(const std::string& state, int* type, int* dataVal);
// State objects stay valid until the pack changes. Lookup/interning is thread safe.
const ModBlockState* GetModBlock(int type, int dataVal);
bool CanonicalModState(const std::string& state, std::string& canonical);
