#include "stdafx.h"
#include "ModSupport.h"
#include "lodepng.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

namespace {
struct ModBlock {
    std::string name;
    std::string displayName;
    int type = 0;
    std::vector<ModStateModel> definitions;
    std::map<std::string, int> stateIds;
    std::vector<std::unique_ptr<ModBlockState>> states;
};
struct Pack {
    std::string path;
    std::vector<ModTexture> textures;
    std::vector<ModDimension> dimensions;
    std::vector<std::shared_ptr<const ModGeometry>> geometries;
    std::map<std::string, std::unique_ptr<ModBlock>> blocks;
    std::vector<ModBlock*> byType;
    int typeLimit = NUM_BLOCKS_DEFINED;
    size_t stateCount = 0;
    size_t stateBytes = 0;
};
Pack pack;
std::mutex stateMutex;

bool identifier(const std::string& s, bool path = false) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || (path && c == '/')))
            return false;
    return true;
}
bool parseState(const std::string& input, std::string& name, std::map<std::string, std::string>& props, std::string& canonical) {
    if (input.empty() || input.size() > 1023) return false;
    const size_t lb = input.find('[');
    name = input.substr(0, lb);
    const size_t colon = name.find(':');
    if (colon == std::string::npos || !identifier(name.substr(0, colon)) || !identifier(name.substr(colon + 1), true)) return false;
    props.clear();
    if (lb != std::string::npos) {
        if (input.back() != ']' || lb + 2 >= input.size()) return false;
        const std::string body = input.substr(lb + 1, input.size() - lb - 2);
        size_t start = 0;
        while (start < body.size()) {
            size_t end = body.find(',', start);
            if (end == std::string::npos) end = body.size();
            const std::string item = body.substr(start, end - start);
            const size_t eq = item.find('=');
            if (eq == std::string::npos || !identifier(item.substr(0, eq)) || !identifier(item.substr(eq + 1)) || props.size() >= 64 || !props.emplace(item.substr(0, eq), item.substr(eq + 1)).second)
                return false;
            start = end + 1;
            if (end + 1 == body.size()) return false;
        }
    }
    canonical = name;
    for (const auto& prop : props) canonical += (canonical == name ? "[" : ",") + prop.first + "=" + prop.second;
    if (!props.empty()) canonical += "]";
    return true;
}
void fields(const std::string& line, std::vector<std::string>& result) {
    size_t start = 0, count = 0;
    do {
        size_t end = line.find('\t', start);
        if (count == result.size()) result.emplace_back();
        result[count++].assign(line, start, end - start);
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    result.resize(count);
}
bool number(const std::string& s, double& result, double low, double high) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    result = std::strtod(s.c_str(), &end);
    return !errno && end == s.c_str() + s.size() && std::isfinite(result) && result >= low && result <= high;
}
bool integer(const std::string& s, int& result, int low, int high) {
    double n;
    if (!number(s, n, low, high) || n != std::floor(n)) return false;
    result = (int)n;
    return true;
}
int direction(const std::string& name) {
    const char* names[] = { "down", "up", "north", "south", "west", "east" };
    for (int i = 0; i < 6; ++i) if (name == names[i]) return i;
    return -1;
}
bool contained(const std::filesystem::path& root, const std::filesystem::path& file) {
    auto a = root.begin(), b = file.begin();
    for (; a != root.end(); ++a, ++b) if (b == file.end() || *a != *b) return false;
    return b != file.end();
}
bool boundedUVCells(double u0, double v0, double u1, double v1) {
    if (u0 >= 0 && u0 <= 16 && u1 >= 0 && u1 <= 16 && v0 >= 0 && v0 <= 16 && v1 >= 0 && v1 <= 16) return true;
    const double lowU = (std::min)(u0,u1)/16, highU = (std::max)(u0,u1)/16;
    const double lowV = (std::min)(v0,v1)/16, highV = (std::max)(v0,v1)/16;
    const int columns = lowU == highU ? 1 : (std::max)(1, (int)ceil(highU)-(int)floor(lowU));
    const int rows = lowV == highV ? 1 : (std::max)(1, (int)ceil(highV)-(int)floor(lowV));
    return columns*rows <= MAX_MOD_UV_CELLS;
}
bool readPack(const std::string& path, Pack& next, std::string& error) {
    next.byType.resize(MAX_BLOCK_TYPES, nullptr);
    const auto source = std::filesystem::canonical(std::filesystem::u8path(path));
    const auto root = source.parent_path();
    if (!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source) > 512 * 1024 * 1024) { error = "Manifest is missing or exceeds 512 MiB."; return false; }
    std::ifstream stream(source, std::ios::binary);
    std::string line;
    if (!std::getline(stream, line)) { error = "Cannot read manifest."; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool repeatedUV = line == "MINEWAYS_MOD_PACK\t5";
    const bool zRotation = repeatedUV || line == "MINEWAYS_MOD_PACK\t4";
    const bool polygonGeometry = zRotation || line == "MINEWAYS_MOD_PACK\t3";
    const bool sharedGeometry = polygonGeometry || line == "MINEWAYS_MOD_PACK\t2";
    if (!sharedGeometry && line != "MINEWAYS_MOD_PACK\t1") { error = "Unsupported mod pack format."; return false; }
    std::shared_ptr<ModGeometry> geometry;
    ModStateModel* def = nullptr;
    ModModel* model = nullptr;
    ModElement* element = nullptr;
    size_t lineNumber = 1, elementCount = 0, definitionCount = 0, polygonCount = 0;
    std::set<std::string> selectors;
    std::vector<std::string> f;
    f.reserve(23); // A four-corner polygon has twenty-three fields.
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fail = [&]() { error = "Invalid mod pack record at line " + std::to_string(lineNumber) + "."; return false; };
        if (line.empty()) continue;
        if (line.size() > 16384 || line.find('\0') != std::string::npos) return fail();
        fields(line, f);
        if (f[0] == "dimension") {
            if (def || geometry || f.size() != 5 || next.dimensions.size() >= 256) return fail();
            ModDimension dimension;
            std::string canonical;
            std::map<std::string, std::string> props;
            if (f[1].size() > 255 || !parseState(f[1], dimension.id, props, canonical) || !props.empty() ||
                !integer(f[2], dimension.minY, -2032, 2016) || dimension.minY % 16 ||
                !integer(f[3], dimension.height, 16, 4064) || dimension.height % 16 || dimension.minY + dimension.height > 2032 ||
                !number(f[4], dimension.coordinateScale, 0.000001, 1000000)) return fail();
            for (const auto& old : next.dimensions) if (old.id == dimension.id) return fail();
            next.dimensions.push_back(std::move(dimension));
        }
        else if (f[0] == "texture") {
            int index;
            if (def || geometry || f.size() != 4 || !integer(f[1], index, 0, 131071) || index != (int)next.textures.size()) return fail();
            std::string name, canonical;
            std::map<std::string, std::string> props;
            if (!parseState(f[2], name, props, canonical) || !props.empty()) return fail();
            const auto relative = std::filesystem::u8path(f[3]);
            if (relative.is_absolute() || f[3].find('\\') != std::string::npos) return fail();
            const auto texturePath = std::filesystem::canonical(root / relative);
            if (!contained(root, texturePath) || !std::filesystem::is_regular_file(texturePath) || std::filesystem::file_size(texturePath) > 32 * 1024 * 1024) return fail();
            std::ifstream image(texturePath, std::ios::binary);
            std::vector<unsigned char> bytes(33);
            if (!image.read((char*)bytes.data(), (std::streamsize)bytes.size())) return fail();
            ModTexture texture;
            texture.name = f[2];
            texture.sourcePath = texturePath.u8string();
            LodePNGState png;
            lodepng_state_init(&png);
            const unsigned inspect = lodepng_inspect(&texture.width, &texture.height, &png, bytes.data(), bytes.size());
            lodepng_state_cleanup(&png);
            if (inspect || !texture.width || texture.width > 4096 || texture.width != texture.height) { error = "Textures must be square PNGs, at most 4096 pixels wide."; return false; }
            next.textures.push_back(std::move(texture));
        }
        else if (f[0] == "state") {
            if (def || geometry || f.size() != 4 || ++definitionCount > 1000000) return fail();
            ModStateModel d;
            std::string name;
            if (!parseState(f[1], name, d.properties, d.selector) || name.compare(0, 10, "minecraft:") == 0 || !selectors.insert(d.selector).second || f[2].size() != 6) return fail();
            for (char c : f[2]) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return fail();
            d.color = (unsigned)std::strtoul(f[2].c_str(), nullptr, 16);
            int alpha;
            if (!integer(f[3], alpha, 0, 255)) return fail();
            d.alpha = (unsigned)alpha;
            auto& block = next.blocks[name];
            if (!block) {
                if (next.blocks.size() > 100000) { error = "Mod catalog exceeds 100000 block definitions."; return false; }
                block.reset(new ModBlock);
                block->name = name;
                block->displayName = name.size() > 96 ? name.substr(0, 77) + "..." + name.substr(name.size() - 16) : name;
            }
            block->definitions.push_back(std::move(d));
            def = &block->definitions.back();
            model = nullptr;
            element = nullptr;
        }
        else if (f[0] == "geometry") {
            int index;
            if (!sharedGeometry || def || geometry || f.size() != 2 ||
                !integer(f[1], index, 0, 249999) || index != (int)next.geometries.size()) return fail();
            geometry = std::make_shared<ModGeometry>();
            element = nullptr;
        }
        else if (f[0] == "endgeometry") {
            if (!geometry || f.size() != 1 || (geometry->elements.empty() && geometry->polygons.empty())) return fail();
            for (const auto& e : geometry->elements) if (e.faces.empty()) return fail();
            next.geometries.push_back(geometry);
            geometry.reset();
            element = nullptr;
        }
        else if (f[0] == "invisible") {
            if (!sharedGeometry || !def || f.size() != 1 || def->invisible || !def->models.empty() || def->alpha != 0) return fail();
            def->invisible = true;
            model = nullptr;
            element = nullptr;
        }
        else if (f[0] == "usemodel") {
            ModModel m;
            int index, lock;
            if (!sharedGeometry || !def || def->invisible || (f.size() != 5 && !(zRotation && f.size() == 6)) || def->models.size() >= 256 ||
                !integer(f[1], index, 0, (int)next.geometries.size() - 1) ||
                !integer(f[2], m.x, 0, 270) || !integer(f[3], m.y, 0, 270) ||
                m.x % 90 || m.y % 90 || !integer(f[4], lock, 0, 1) ||
                (f.size() == 6 && (!integer(f[5], m.z, 0, 270) || m.z % 90))) return fail();
            m.geometry = next.geometries[index];
            m.uvlock = lock != 0;
            def->models.push_back(std::move(m));
            model = nullptr;
            element = nullptr;
        }
        else if (f[0] == "model") {
            ModModel m;
            int lock;
            if (!def || def->invisible || (f.size() != 4 && !(zRotation && f.size() == 5)) || def->models.size() >= 256 || !integer(f[1], m.x, 0, 270) || !integer(f[2], m.y, 0, 270) || m.x % 90 || m.y % 90 || !integer(f[3], lock, 0, 1) ||
                (f.size() == 5 && (!integer(f[4], m.z, 0, 270) || m.z % 90))) return fail();
            m.uvlock = lock != 0;
            def->models.push_back(std::move(m));
            model = &def->models.back();
            element = nullptr;
        }
        else if (f[0] == "element") {
            if ((!model && !geometry) || f.size() != 13 || ++elementCount > 4000000) return fail();
            ModElement e;
            double value;
            for (int i = 0; i < 6; ++i) {
                if (!number(f[i + 1], value, -256, 256)) return fail();
                if (i < 3) e.from[i] = (float)value; else e.to[i - 3] = (float)value;
            }
            e.axis = f[7] == "none" ? -1 : f[7] == "x" ? 0 : f[7] == "y" ? 1 : f[7] == "z" ? 2 : -2;
            if (e.axis == -2 || !number(f[8], value, -45, 45)) return fail();
            e.angle = (float)value;
            if (e.axis == -1 && e.angle != 0) return fail();
            for (int i = 0; i < 3; ++i) { if (!number(f[i + 9], value, -256, 256)) return fail(); e.origin[i] = (float)value; }
            int rescale;
            if (!integer(f[12], rescale, 0, 1)) return fail();
            e.rescale = rescale != 0;
            auto& elements = geometry ? geometry->elements : model->elements;
            if (elements.size() >= 2048) return fail();
            elements.push_back(std::move(e));
            element = &elements.back();
        }
        else if (f[0] == "polygon") {
            ModPolygon polygon;
            if (!polygonGeometry || (!model && !geometry) || f.size() < 3 ||
                !integer(f[1], polygon.texture, 0, (int)next.textures.size() - 1) ||
                !integer(f[2], polygon.count, 3, 4) || f.size() != (size_t)(3 + polygon.count * 5) ||
                ++polygonCount > 1000000) return fail();
            for (int vertex = 0; vertex < polygon.count; ++vertex) for (int component = 0; component < 5; ++component) {
                double value;
                if (!number(f[3 + vertex * 5 + component], value, component < 3 || repeatedUV ? -256 : 0, component < 3 || repeatedUV ? 256 : 16)) return fail();
                polygon.vertices[vertex][component] = (float)value;
            }
            double lowU = polygon.vertices[0][3], highU = lowU, lowV = polygon.vertices[0][4], highV = lowV;
            for (int vertex = 1; vertex < polygon.count; ++vertex) {
                lowU = (std::min)(lowU,(double)polygon.vertices[vertex][3]); highU = (std::max)(highU,(double)polygon.vertices[vertex][3]);
                lowV = (std::min)(lowV,(double)polygon.vertices[vertex][4]); highV = (std::max)(highV,(double)polygon.vertices[vertex][4]);
            }
            if (repeatedUV && !boundedUVCells(lowU,lowV,highU,highV)) return fail();
            // A degenerate leading triangle cannot produce a finite face normal.
            double a[3], b[3], cross[3];
            for (int i = 0; i < 3; ++i) { a[i] = (double)polygon.vertices[1][i] - polygon.vertices[0][i]; b[i] = (double)polygon.vertices[2][i] - polygon.vertices[0][i]; }
            cross[0] = a[1]*b[2] - a[2]*b[1]; cross[1] = a[2]*b[0] - a[0]*b[2]; cross[2] = a[0]*b[1] - a[1]*b[0];
            if (cross[0]*cross[0] + cross[1]*cross[1] + cross[2]*cross[2] < 1e-16) return fail();
            auto& polygons = geometry ? geometry->polygons : model->polygons;
            if (polygons.size() >= 16384) return fail();
            polygons.push_back(polygon);
            element = nullptr;
        }
        else if (f[0] == "face") {
            if (!element || f.size() != 9 || element->faces.size() >= 6) return fail();
            ModFace face;
            face.direction = direction(f[1]);
            face.cullface = f[8] == "-" ? -1 : direction(f[8]);
            if (face.direction < 0 || (face.cullface < 0 && f[8] != "-")) return fail();
            for (const auto& old : element->faces) if (old.direction == face.direction) return fail();
            for (int i = 0; i < 4; ++i) { double value; if (!number(f[i + 2], value, repeatedUV ? -256 : 0, repeatedUV ? 256 : 16)) return fail(); face.uv[i] = (float)value; }
            if (repeatedUV && !boundedUVCells(face.uv[0],face.uv[1],face.uv[2],face.uv[3])) return fail();
            if (!integer(f[6], face.rotation, 0, 270) || face.rotation % 90 || !integer(f[7], face.texture, 0, (int)next.textures.size() - 1)) return fail();
            element->faces.push_back(face);
        }
        else if (f[0] == "endstate") {
            if (!def || f.size() != 1 || (def->models.empty() && !def->invisible)) return fail();
            for (const auto& m : def->models) {
                // Shared geometry is immutable and already validated at endgeometry.
                if (m.geometry) continue;
                if (m.elements.empty() && m.polygons.empty()) return fail();
                for (const auto& e : m.elements) if (e.faces.empty()) return fail();
            }
            def = nullptr; model = nullptr; element = nullptr;
        }
        else return fail();
    }
    if (stream.bad() || def || geometry || (next.blocks.empty() && next.dimensions.empty())) { error = "Incomplete or empty mod pack."; return false; }
    next.path = source.u8string();
    return true;
}
}

static void initializeBlockDefinition(ModBlock& block) {
    const ModStateModel& first = block.definitions.front();
    auto& d = gBlockDefinitions[block.type];
    d.name = block.displayName.c_str();
    d.read_color = d.color = first.color;
    d.read_alpha = d.alpha = (float)first.alpha / 255.0f;
    d.pcolor = ((unsigned)(((first.color >> 16) & 255) * d.alpha) << 16) | ((unsigned)(((first.color >> 8) & 255) * d.alpha) << 8) | (unsigned)((first.color & 255) * d.alpha);
    d.txrX = 1; d.txrY = 0;
    // Texture pixels are lazy. Conservative coverage avoids hiding neighbors before alpha is verified.
    d.flags = BLF_ALMOST_WHOLE | BLF_TRUE_GEOMETRY | BLF_3D_BIT;
    bool cube = true;
    for (const auto& definition : block.definitions) {
        if (definition.models.size() != 1 || definition.models[0].Elements().size() != 1 || !definition.models[0].Polygons().empty()) { cube = false; break; }
        const auto& element = definition.models[0].Elements()[0];
        if (element.angle != 0 || element.faces.size() != 6) { cube = false; break; }
        for (int axis = 0; axis < 3; ++axis)
            if (element.from[axis] != 0 || element.to[axis] != 16) cube = false;
    }
    // Neighbor connection geometry does not depend on whether alpha has been decoded.
    if (cube) d.flags |= BLF_FENCE_NEIGHBOR;
}

bool CanonicalModState(const std::string& state, std::string& canonical) {
    std::string name;
    std::map<std::string, std::string> props;
    return parseState(state, name, props, canonical);
}
bool LoadModPack(const std::string& path, std::string& error) {
    error.clear();
    Pack next;
    try { if (!readPack(path, next, error)) return false; }
    catch (const std::exception& e) { error = std::string("Cannot load mod pack: ") + e.what(); return false; }
    std::lock_guard<std::mutex> lock(stateMutex);
    for (int i = NUM_BLOCKS_DEFINED; i < MAX_BLOCK_TYPES; ++i) gBlockDefinitions[i] = {};
    pack = std::move(next);
    return true;
}
void ClearModPack() {
    std::lock_guard<std::mutex> lock(stateMutex);
    pack = Pack();
    for (int i = NUM_BLOCKS_DEFINED; i < MAX_BLOCK_TYPES; ++i) gBlockDefinitions[i] = {};
}
bool ModPackLoaded() { return !pack.path.empty(); }
const std::string& ModPackPath() { return pack.path; }
const std::vector<ModTexture>& ModTextures() { return pack.textures; }
const std::vector<ModDimension>& ModDimensions() { return pack.dimensions; }
int ModBlockTypeLimit() { std::lock_guard<std::mutex> lock(stateMutex); return pack.typeLimit; }
bool IsModBlock(int type) { if (type < NUM_BLOCKS_DEFINED || type >= MAX_BLOCK_TYPES) return false; std::lock_guard<std::mutex> lock(stateMutex); return type < pack.typeLimit && pack.byType[type] != nullptr; }
bool HasModBlock(const std::string& name) { return pack.blocks.find(name) != pack.blocks.end(); }
static int resolveModState(const std::string& state, int* type, int* dataVal) {
    if (!type || !dataVal) return -1;
    std::string name, canonical;
    std::map<std::string, std::string> props;
    if (!parseState(state, name, props, canonical)) return -1;
    const auto found = pack.blocks.find(name);
    if (found == pack.blocks.end()) return 0;
    std::lock_guard<std::mutex> lock(stateMutex);
    ModBlock& block = *found->second;
    const auto old = block.stateIds.find(canonical);
    if (old != block.stateIds.end()) { *type = block.type; *dataVal = old->second; return 1; }
    const ModStateModel* selected = nullptr;
    for (const auto& definition : block.definitions) {
        bool matches = true;
        for (const auto& p : definition.properties) {
            const auto value = props.find(p.first);
            if (value == props.end() || value->second != p.second) { matches = false; break; }
        }
        if (matches && (!selected || definition.properties.size() > selected->properties.size())) selected = &definition;
    }
    if (!selected) return 0;
    int newType = block.type;
    if (!newType) {
        newType = pack.typeLimit;
        if (newType >= MAX_BLOCK_TYPES) return -1;
    }
    // shortcut: the existing chunk format allows 4096 states per block; widen it before exceeding this limit.
    if (block.states.size() >= 4096 || pack.stateCount >= 131072 || pack.stateBytes + canonical.size() * 2 > 64 * 1024 * 1024) return -1;
    std::unique_ptr<ModBlockState> result(new ModBlockState);
    result->canonical = canonical;
    result->definition = selected;
    const auto waterlogged = props.find("waterlogged");
    result->waterlogged = waterlogged != props.end() && waterlogged->second == "true";
    const int index = (int)block.states.size();
    if (block.states.size() == block.states.capacity()) block.states.reserve(block.states.capacity() ? block.states.capacity() * 2 : 16);
    block.stateIds.emplace(canonical, index);
    block.states.push_back(std::move(result));
    ++pack.stateCount;
    pack.stateBytes += canonical.size() * 2;
    if (!block.type) {
        block.type = newType;
        initializeBlockDefinition(block);
        pack.byType[newType] = &block;
        pack.typeLimit = newType + 1;
    }
    *type = block.type;
    *dataVal = index;
    return 1;
}
int ResolveModState(const std::string& state, int* type, int* dataVal) {
    try { return resolveModState(state, type, dataVal); }
    catch (const std::bad_alloc&) { return -1; }
}
const ModBlockState* GetModBlock(int type, int dataVal) {
    if (type < NUM_BLOCKS_DEFINED || type >= MAX_BLOCK_TYPES || dataVal < 0) return nullptr;
    std::lock_guard<std::mutex> lock(stateMutex);
    if (type >= pack.typeLimit || !pack.byType[type]) return nullptr;
    const ModBlock& block = *pack.byType[type];
    return (size_t)dataVal < block.states.size() ? block.states[dataVal].get() : nullptr;
}

bool ModBlockWaterlogged(int type, int dataVal) {
    const ModBlockState* state = GetModBlock(type, dataVal & DATAVAL_MASK);
    return state && state->waterlogged;
}

bool EnsureModTextures(const std::vector<int>& indices, std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(stateMutex);
    try {
        std::map<int, std::vector<unsigned char>> decoded;
        size_t decodedBytes = 0;
        for (const auto& texture : pack.textures) decodedBytes += texture.rgba.size();
        const auto root = std::filesystem::u8path(pack.path).parent_path();
        for (int index : indices) {
            if (index < 0 || (size_t)index >= pack.textures.size()) { error = "Invalid mod texture index."; return false; }
            const auto& texture = pack.textures[index];
            if (!texture.rgba.empty() || decoded.count(index)) continue;
            decodedBytes += (size_t)texture.width * texture.height * 4;
            if (decodedBytes > 256 * 1024 * 1024) { error = "Selected decoded mod textures exceed 256 MiB."; return false; }
            const auto path = std::filesystem::canonical(std::filesystem::u8path(texture.sourcePath));
            if (!contained(root, path) || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 32 * 1024 * 1024) { error = "Mod texture path changed or exceeds the file limit."; return false; }
            std::ifstream image(path, std::ios::binary);
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(image)), std::istreambuf_iterator<char>());
            unsigned width, height;
            LodePNGState png;
            lodepng_state_init(&png);
            const unsigned inspect = lodepng_inspect(&width, &height, &png, bytes.data(), bytes.size());
            lodepng_state_cleanup(&png);
            if (inspect || width != texture.width || height != texture.height) { error = "Changed PNG texture dimensions: " + texture.name; return false; }
            auto& rgba = decoded[index];
            if (lodepng::decode(rgba, width, height, bytes) || width != texture.width || height != texture.height) { error = "Invalid or changed PNG texture: " + texture.name; return false; }
        }
        for (auto& entry : decoded) {
            auto& texture = pack.textures[entry.first];
            texture.rgba.swap(entry.second);
            texture.opaque = true;
            for (size_t i = 3; i < texture.rgba.size(); i += 4) if (texture.rgba[i] != 255) { texture.opaque = false; break; }
        }
        return true;
    }
    catch (const std::exception& e) { error = std::string("Cannot decode selected mod textures: ") + e.what(); return false; }
}
void ReleaseModTextures() {
    std::lock_guard<std::mutex> lock(stateMutex);
    for (auto& texture : pack.textures) std::vector<unsigned char>().swap(texture.rgba);
}
