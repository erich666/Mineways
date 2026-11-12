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
    std::map<std::string, std::unique_ptr<ModBlock>> blocks;
    ModBlock* byType[MAX_BLOCK_TYPES] = {};
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
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    size_t start = 0;
    do {
        size_t end = line.find('\t', start);
        result.push_back(line.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return result;
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
bool fullCube(const ModStateModel& def, const std::vector<ModTexture>& textures) {
    if (def.alpha != 255 || def.models.size() != 1 || def.models[0].elements.size() != 1) return false;
    const ModElement& element = def.models[0].elements[0];
    if (element.angle != 0 || element.faces.size() != 6) return false;
    for (int i = 0; i < 3; ++i) if (element.from[i] != 0 || element.to[i] != 16) return false;
    for (const ModFace& face : element.faces) {
        if (!textures[face.texture].opaque) return false;
    }
    return true;
}
bool readPack(const std::string& path, Pack& next, std::string& error) {
    const auto source = std::filesystem::canonical(std::filesystem::u8path(path));
    const auto root = source.parent_path();
    if (!std::filesystem::is_regular_file(source) || std::filesystem::file_size(source) > 64 * 1024 * 1024) { error = "Manifest is missing or exceeds 64 MiB."; return false; }
    std::ifstream stream(source, std::ios::binary);
    std::string line;
    if (!std::getline(stream, line)) { error = "Cannot read manifest."; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "MINEWAYS_MOD_PACK\t1") { error = "Unsupported mod pack format."; return false; }
    ModStateModel* def = nullptr;
    ModModel* model = nullptr;
    ModElement* element = nullptr;
    size_t lineNumber = 1, decodedBytes = 0, elementCount = 0, definitionCount = 0;
    std::set<std::string> selectors;
    size_t textureCapacity = 0;
    for (const auto& tile : gTilesTable)
        if ((!tile.filename || !*tile.filename) && tile.spanX == 0 && tile.spanY == 0) ++textureCapacity;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fail = [&]() { error = "Invalid mod pack record at line " + std::to_string(lineNumber) + "."; return false; };
        if (line.empty()) continue;
        if (line.size() > 16384 || line.find('\0') != std::string::npos) return fail();
        const auto f = fields(line);
        if (f[0] == "dimension") {
            if (def || f.size() != 5 || next.dimensions.size() >= 256) return fail();
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
            if (def || f.size() != 4 || !integer(f[1], index, 0, 4095) || index != (int)next.textures.size()) return fail();
            if (next.textures.size() >= textureCapacity) { error = "Mod pack exceeds the terrain atlas capacity of " + std::to_string(textureCapacity) + " additional textures."; return false; }
            std::string name, canonical;
            std::map<std::string, std::string> props;
            if (!parseState(f[2], name, props, canonical) || !props.empty()) return fail();
            const auto relative = std::filesystem::u8path(f[3]);
            if (relative.is_absolute() || f[3].find('\\') != std::string::npos) return fail();
            const auto texturePath = std::filesystem::canonical(root / relative);
            if (!contained(root, texturePath) || !std::filesystem::is_regular_file(texturePath) || std::filesystem::file_size(texturePath) > 32 * 1024 * 1024) return fail();
            std::ifstream image(texturePath, std::ios::binary);
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(image)), std::istreambuf_iterator<char>());
            ModTexture texture;
            texture.name = f[2];
            LodePNGState png;
            lodepng_state_init(&png);
            const unsigned inspect = lodepng_inspect(&texture.width, &texture.height, &png, bytes.data(), bytes.size());
            lodepng_state_cleanup(&png);
            if (inspect || !texture.width || texture.width > 4096 || texture.width != texture.height) { error = "Textures must be square PNGs, at most 4096 pixels wide."; return false; }
            decodedBytes += (size_t)texture.width * texture.height * 4;
            if (decodedBytes > 256 * 1024 * 1024) { error = "Decoded textures exceed 256 MiB."; return false; }
            if (lodepng::decode(texture.rgba, texture.width, texture.height, bytes)) { error = "Invalid PNG texture."; return false; }
            for (size_t i = 3; i < texture.rgba.size(); i += 4) if (texture.rgba[i] != 255) { texture.opaque = false; break; }
            next.textures.push_back(std::move(texture));
        }
        else if (f[0] == "state") {
            if (def || f.size() != 4 || ++definitionCount > 65536) return fail();
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
                while ((next.typeLimit & 255) == 0) ++next.typeLimit;
                if (next.typeLimit >= MAX_BLOCK_TYPES) { error = "Mod block count exceeds the 12-bit block capacity."; return false; }
                block.reset(new ModBlock);
                block->name = name;
                block->displayName = name.size() > 96 ? name.substr(0, 77) + "..." + name.substr(name.size() - 16) : name;
                block->type = next.typeLimit++;
                next.byType[block->type] = block.get();
            }
            block->definitions.push_back(std::move(d));
            def = &block->definitions.back();
            model = nullptr;
            element = nullptr;
        }
        else if (f[0] == "model") {
            ModModel m;
            int lock;
            if (!def || f.size() != 4 || def->models.size() >= 256 || !integer(f[1], m.x, 0, 270) || !integer(f[2], m.y, 0, 270) || m.x % 90 || m.y % 90 || !integer(f[3], lock, 0, 1)) return fail();
            m.uvlock = lock != 0;
            def->models.push_back(std::move(m));
            model = &def->models.back();
            element = nullptr;
        }
        else if (f[0] == "element") {
            if (!model || f.size() != 13 || ++elementCount > 65536) return fail();
            ModElement e;
            double value;
            for (int i = 0; i < 6; ++i) {
                if (!number(f[i + 1], value, -16, 32)) return fail();
                if (i < 3) e.from[i] = (float)value; else e.to[i - 3] = (float)value;
            }
            for (int i = 0; i < 3; ++i) if (e.from[i] > e.to[i]) return fail();
            e.axis = f[7] == "none" ? -1 : f[7] == "x" ? 0 : f[7] == "y" ? 1 : f[7] == "z" ? 2 : -2;
            if (e.axis == -2 || !number(f[8], value, -45, 45)) return fail();
            e.angle = (float)value;
            if (e.axis == -1 && e.angle != 0) return fail();
            for (int i = 0; i < 3; ++i) { if (!number(f[i + 9], value, -16, 32)) return fail(); e.origin[i] = (float)value; }
            int rescale;
            if (!integer(f[12], rescale, 0, 1)) return fail();
            e.rescale = rescale != 0;
            model->elements.push_back(std::move(e));
            element = &model->elements.back();
        }
        else if (f[0] == "face") {
            if (!element || f.size() != 9 || element->faces.size() >= 6) return fail();
            ModFace face;
            face.direction = direction(f[1]);
            face.cullface = f[8] == "-" ? -1 : direction(f[8]);
            if (face.direction < 0 || (face.cullface < 0 && f[8] != "-")) return fail();
            for (const auto& old : element->faces) if (old.direction == face.direction) return fail();
            for (int i = 0; i < 4; ++i) { double value; if (!number(f[i + 2], value, 0, 16)) return fail(); face.uv[i] = (float)value; }
            if (!integer(f[6], face.rotation, 0, 270) || face.rotation % 90 || !integer(f[7], face.texture, 0, (int)next.textures.size() - 1)) return fail();
            element->faces.push_back(face);
        }
        else if (f[0] == "endstate") {
            if (!def || f.size() != 1 || def->models.empty()) return fail();
            for (const auto& m : def->models) {
                if (m.elements.empty()) return fail();
                for (const auto& e : m.elements) if (e.faces.empty()) return fail();
            }
            def = nullptr; model = nullptr; element = nullptr;
        }
        else return fail();
    }
    if (stream.bad() || def || (next.blocks.empty() && next.dimensions.empty())) { error = "Incomplete or empty mod pack."; return false; }
    next.path = source.u8string();
    for (auto& entry : next.blocks) {
        ModBlock& block = *entry.second;
        const ModStateModel& first = block.definitions.front();
        std::unique_ptr<ModBlockState> initial(new ModBlockState);
        initial->canonical = first.selector;
        initial->definition = &first;
        const auto waterlogged = first.properties.find("waterlogged");
        initial->waterlogged = waterlogged != first.properties.end() && waterlogged->second == "true";
        block.stateIds.emplace(first.selector, 0);
        block.states.push_back(std::move(initial));
        ++next.stateCount;
        next.stateBytes += first.selector.size() * 2;
    }
    return true;
}
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
    for (auto& entry : pack.blocks) {
        ModBlock& block = *entry.second;
        const ModStateModel& first = block.definitions.front();
        auto& d = gBlockDefinitions[block.type];
        d.name = block.displayName.c_str();
        d.read_color = d.color = first.color;
        d.read_alpha = d.alpha = (float)first.alpha / 255.0f;
        d.pcolor = ((unsigned)(((first.color >> 16) & 255) * d.alpha) << 16) | ((unsigned)(((first.color >> 8) & 255) * d.alpha) << 8) | (unsigned)((first.color & 255) * d.alpha);
        d.txrX = 1; d.txrY = 0;
        bool whole = true;
        for (const auto& definition : block.definitions) whole = whole && fullCube(definition, pack.textures);
        d.flags = (whole ? BLF_WHOLE | BLF_FENCE_NEIGHBOR : BLF_ALMOST_WHOLE) | BLF_TRUE_GEOMETRY | BLF_3D_BIT;
    }
    return true;
}
void ClearModPack() {
    std::lock_guard<std::mutex> lock(stateMutex);
    pack = Pack();
    for (int i = NUM_BLOCKS_DEFINED; i < MAX_BLOCK_TYPES; ++i) gBlockDefinitions[i] = {};
}
const std::string& ModPackPath() { return pack.path; }
const std::vector<ModTexture>& ModTextures() { return pack.textures; }
const std::vector<ModDimension>& ModDimensions() { return pack.dimensions; }
int ModBlockTypeLimit() { return pack.typeLimit; }
bool IsModBlock(int type) { return type >= NUM_BLOCKS_DEFINED && type < MAX_BLOCK_TYPES && pack.byType[type] != nullptr; }
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
    *type = block.type;
    *dataVal = index;
    return 1;
}
int ResolveModState(const std::string& state, int* type, int* dataVal) {
    try { return resolveModState(state, type, dataVal); }
    catch (const std::bad_alloc&) { return -1; }
}
const ModBlockState* GetModBlock(int type, int dataVal) {
    if (!IsModBlock(type) || dataVal < 0) return nullptr;
    std::lock_guard<std::mutex> lock(stateMutex);
    const ModBlock& block = *pack.byType[type];
    return (size_t)dataVal < block.states.size() ? block.states[dataVal].get() : nullptr;
}

bool ModBlockWaterlogged(int type, int dataVal) {
    const ModBlockState* state = GetModBlock(type, dataVal & DATAVAL_MASK);
    return state && state->waterlogged;
}
