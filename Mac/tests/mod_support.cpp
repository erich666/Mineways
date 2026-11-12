// Include the reader to exercise actual palette parsing as well as the public registry API.
#include "../../Win/nbt.cpp"
#include "../../Win/lodepng.h"
#include <filesystem>
#include <fstream>
#include <thread>

using Bytes = std::vector<unsigned char>;
static void word(Bytes& b, int n) { b.push_back((unsigned char)(n >> 8)); b.push_back((unsigned char)n); }
static void stringValue(Bytes& b, const std::string& s) { word(b, (int)s.size()); b.insert(b.end(), s.begin(), s.end()); }
static void stringTag(Bytes& b, const std::string& name, const std::string& value) { b.push_back(8); stringValue(b, name); stringValue(b, value); }
static bfFile buffer(Bytes& bytes) {
    bfFile file = {};
    file.type = BF_BUFFER;
    file.buf = bytes.data();
    file.buflen = (int)bytes.size();
    return file;
}
static std::string cube(const std::string& selector, const std::string& color = "abcdef") {
    std::string s = "state\t" + selector + "\t" + color + "\t255\nmodel\t0\t0\t0\nelement\t0\t0\t0\t16\t16\t16\tnone\t0\t8\t8\t8\t0\n";
    for (const char* side : { "down", "up", "north", "south", "west", "east" }) s += "face\t" + std::string(side) + "\t0\t0\t16\t16\t0\t0\t" + side + "\n";
    return s + "endstate\n";
}
static void writeFile(const std::filesystem::path& path, const std::string& text) { std::ofstream file(path, std::ios::binary); file << text; assert(file.good()); }
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::u8path(argv[1]);
    std::filesystem::create_directories(dir / "textures");
    Bytes png, pixels(16 * 16 * 4, 255);
    assert(!lodepng::encode(png, pixels, 16, 16));
    { std::ofstream file(dir / "textures/0.png", std::ios::binary); file.write((const char*)png.data(), png.size()); }
    const std::string prefix = "MINEWAYS_MOD_PACK\t1\ntexture\t0\tlab:block/stone\ttextures/0.png\n";
    const std::string longName = "lab:" + std::string(1019, 'a');
    const std::string manifest = prefix + cube(longName) + cube("lab:stone") + cube("other:stone", "123456") + cube("lab:machine[facing=north]") + cube("lab:machine[facing=south]");
    const auto path = dir / "pack.mwmod";
    writeFile(path, manifest);
    std::string error;
    assert(LoadModPack(path.u8string(), error));
    assert(error.empty() && ModTextures().size() == 1);
    int stone, state, other, otherState;
    assert(ResolveModState("lab:stone[z=1,a=2]", &stone, &state) == 1);
    assert(ResolveModState("other:stone", &other, &otherState) == 1 && other != stone);
    assert(IsModBlock(stone) && !IsModBlock(1));
    assert(gBlockDefinitions[stone].read_color == 0xabcdef);
    assert((gBlockDefinitions[stone].flags & BLF_WHOLE) != 0);
    char canonical[1024];
    assert(spongeBuildBlockStateString(stone, state, canonical, sizeof(canonical)) > 0);
    assert(std::string(canonical) == "lab:stone[a=2,z=1]");
    int type2, state2;
    assert(ResolveModState(longName, &type2, &state2) == 1);
    assert(strlen(gBlockDefinitions[type2].name) <= 96);
    char longOutput[1024];
    assert(spongeBuildBlockStateString(type2, state2, longOutput, sizeof(longOutput)) == 1023 && longName == longOutput);
    assert(spongeParseStateString(canonical, &type2, &state2) && type2 == stone && state2 == state);
    assert(ResolveModState("lab:stone[a=1,a=2]", &type2, &state2) == -1);
    assert(ResolveModState("lab:machine[facing=east]", &type2, &state2) == 0);
    assert(ResolveModState("lab:machine[waterlogged=false,facing=south]", &type2, &state2) == 1);
    assert(GetModBlock(type2, state2)->definition->selector == "lab:machine[facing=south]");
    assert(!ModBlockWaterlogged(type2, state2));
    assert(ResolveModState("lab:machine[waterlogged=true,facing=south]", &type2, &state2) == 1);
    assert(ModBlockWaterlogged(type2, PACK_TYPE_EXT_AND_DATAVAL(type2, state2)));
    for (bool direct : { false, true }) {
        for (const char* id : { "minecraft:overworld", "minecraft:the_nether", "minecraft:the_end", "lab:friend", "lab:nether_sky" }) {
            Bytes player = {10, 0, 0};
            if (!direct) { player.push_back(10); stringValue(player, "Data"); player.push_back(10); stringValue(player, "Player"); }
            stringTag(player, "Dimension", id);
            player.insert(player.end(), direct ? 1 : 3, 0);
            bfFile input = buffer(player); input.offset = &input._offset;
            char actual[256]; int dimension = -1;
            assert((direct ? nbtGetDimensionDirect(&input, &dimension, actual, sizeof(actual)) : nbtGetDimension(&input, &dimension, actual, sizeof(actual))) == 0);
            assert(std::string(actual) == id);
            assert(dimension == (std::string(id) == "minecraft:the_nether" ? 1 : std::string(id) == "minecraft:the_end" ? 2 : 0));
            input._offset = 0;
            assert((direct ? nbtGetDimensionDirect(&input, &dimension, actual, 2) : nbtGetDimension(&input, &dimension, actual, 2)) < 0 && actual[0] == 0);
        }
    }
    for (int legacy : {-1, 0, 1}) {
        Bytes player = {10, 0, 0, 3}; stringValue(player, "Dimension");
        uint32_t value = (uint32_t)legacy;
        for (int shift = 24; shift >= 0; shift -= 8) player.push_back((unsigned char)(value >> shift));
        player.push_back(0);
        bfFile input = buffer(player); input.offset = &input._offset;
        char actual[256]; int dimension = -1;
        assert(nbtGetDimensionDirect(&input, &dimension, actual, sizeof(actual)) == 0);
        assert(dimension == (legacy == -1 ? 1 : legacy == 1 ? 2 : 0));
    }
    for (bool propsFirst : { false, true }) {
        Bytes palette = { 10, 0, 0, 0, 1 };
        auto properties = [&]() { palette.push_back(10); stringValue(palette, "Properties"); stringTag(palette, "facing", "mod_specific_value"); stringTag(palette, "foo", "bar"); palette.push_back(0); };
        if (propsFirst) properties();
        stringTag(palette, "Name", "lab:stone");
        if (!propsFirst) properties();
        palette.push_back(0);
        bfFile file = buffer(palette); file.offset = &file._offset;
        unsigned char types[1]; unsigned short data[1]; int entries = 0, result = 0;
        assert(readPalette(result, &file, 21, types, data, entries, nullptr, 0) == 0);
        assert(entries == 1 && result == 0 && BLOCK_TYPE_FROM_GRID_DATA(types[0], data[0]) == stone);
        assert(spongeBuildBlockStateString(stone, BLOCK_DATAVAL_FROM_DATA(data[0]), canonical, sizeof(canonical)) > 0);
        assert(std::string(canonical) == "lab:stone[facing=mod_specific_value,foo=bar]");
    }
    Bytes strings = { 8, 0, 0, 0, 1 }; stringValue(strings, "lab:stone");
    bfFile file = buffer(strings); file.offset = &file._offset;
    unsigned char types[1]; unsigned short data[1]; int entries = 0, result = 0;
    assert(readPalette(result, &file, 26, types, data, entries, nullptr, 0) == 0 && entries == 1);
    // Omitted default properties and unsupported selectors use the unknown fallback,
    // without guessing a model or losing neighboring supported entries.
    for (bool stringsOnly : {false, true}) {
        Bytes mixed = {(unsigned char)(stringsOnly ? 8 : 10), 0, 0, 0, 2};
        if (stringsOnly) {
            stringValue(mixed, "lab:machine");
            stringValue(mixed, "lab:stone");
        } else {
            stringTag(mixed, "Name", "lab:machine");
            mixed.push_back(10); stringValue(mixed, "Properties");
            stringTag(mixed, "facing", "mod_specific_value");
            mixed.push_back(0); mixed.push_back(0);
            stringTag(mixed, "Name", "lab:stone"); mixed.push_back(0);
        }
        bfFile input = buffer(mixed); input.offset = &input._offset;
        unsigned char mixedTypes[2]; unsigned short mixedData[2];
        int count = 0, status = 0; char unknown[MAX_PATH_AND_FILE] = {};
        assert(readPalette(status, &input, 26, mixedTypes, mixedData, count, unknown, 1) == 0);
        assert(count == 2 && (status & NBT_WARNING_NAME_NOT_FOUND));
        assert(BLOCK_TYPE_FROM_GRID_DATA(mixedTypes[0], mixedData[0]) == 1);
        assert(BLOCK_TYPE_FROM_GRID_DATA(mixedTypes[1], mixedData[1]) == stone);
        assert(strstr(unknown, "lab:machine") && strstr(unknown, "unsupported state"));
    }
    for (const char* unsupported : {"lab:machine", "lab:machine[facing=invalid]"}) {
        int status = -1;
        assert(!spongeParseStateString(unsupported, &type2, &state2, &status));
        assert(type2 == BLOCK_AIR && state2 == 0 && status == 0);
    }
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&]() {
        for (int j = 0; j < 100; ++j) { int t, d; assert(ResolveModState("lab:stone[shared=true]", &t, &d) == 1); assert(GetModBlock(t, d)->canonical == "lab:stone[shared=true]"); }
    });
    for (auto& thread : threads) thread.join();
    assert(blockTransIndexFor(stone, state) == -1);
    // More than 255 distinct states must survive the packed format and schematic round trip.
    for (int i = 0; i < 300; ++i) {
        const std::string input = "lab:stone[n=" + std::to_string(i) + "]";
        assert(ResolveModState(input, &type2, &state2) == 1);
        const auto packed = PACK_TYPE_EXT_AND_DATAVAL(type2, state2);
        assert(BLOCK_TYPE_FROM_GRID_DATA(type2 & 255, packed) == type2);
        assert(!ModBlockWaterlogged(type2, packed));
        assert(spongeBuildBlockStateString(type2, BLOCK_DATAVAL_FROM_DATA(packed), canonical, sizeof(canonical)) > 0 && input == canonical);
    }
    assert(state2 > 255);
    const std::string originalPath = ModPackPath();
    for (const auto& invalid : { prefix + cube("lab:stone") + cube("lab:stone"), prefix + "state\tlab:broken\tzzzzzz\t255\n", std::string("MINEWAYS_MOD_PACK\t99\n"), std::string("MINEWAYS_MOD_PACK\t1\ntexture\t0\tlab:stone\t../outside.png\n") + cube("lab:stone") }) {
        writeFile(path, invalid);
        assert(!LoadModPack(path.u8string(), error) && !error.empty());
        assert(ModPackPath() == originalPath && GetModBlock(stone, state));
    }
    size_t textureCapacity = 0;
    for (const auto& tile : gTilesTable)
        if ((!tile.filename || !*tile.filename) && tile.spanX == 0 && tile.spanY == 0) ++textureCapacity;
    std::string tooManyTextures = "MINEWAYS_MOD_PACK\t1\n";
    for (size_t i = 0; i <= textureCapacity; ++i) tooManyTextures += "texture\t" + std::to_string(i) + "\tlab:texture_" + std::to_string(i) + "\ttextures/0.png\n";
    writeFile(path, tooManyTextures + cube("lab:new"));
    assert(!LoadModPack(path.u8string(), error) && HasModBlock("lab:stone"));
    assert(error.find("atlas capacity") != std::string::npos);
    int added = 0;
    for (; added < 5000; ++added) if (ResolveModState("lab:stone[limit=" + std::to_string(added) + "]", &type2, &state2) < 0) break;
    assert(added < 4096);
    std::string many = prefix;
    for (int i = 0; i < 230; ++i) many += cube("lab:block_" + std::to_string(i));
    writeFile(path, many);
    assert(LoadModPack(path.u8string(), error));
    for (int i = 0; i < 230; ++i) {
        assert(ResolveModState("lab:block_" + std::to_string(i), &type2, &state2) == 1);
        assert((type2 & 255) != 0);
    }
    assert(type2 > 768 && !IsModBlock(768));
    bool aggregateRejected = false;
    for (int i = 0; i < 230 && !aggregateRejected; ++i) {
        for (int j = 0; j < 600; ++j) {
            if (ResolveModState("lab:block_" + std::to_string(i) + "[n=" + std::to_string(j) + "]", &type2, &state2) < 0) { aggregateRejected = true; break; }
        }
    }
    assert(aggregateRejected);
    many = prefix;
    for (int i = 0; i < 3600; ++i) many += cube("lab:overflow_" + std::to_string(i));
    writeFile(path, many);
    assert(!LoadModPack(path.u8string(), error));
    assert(HasModBlock("lab:block_229") && !HasModBlock("lab:overflow_0"));
    ClearModPack();
    assert(ModBlockTypeLimit() == NUM_BLOCKS_DEFINED && !IsModBlock(stone) && ModTextures().empty());
    writeFile(path, "MINEWAYS_MOD_PACK\t1\ndimension\tlab:moon\t-128\t512\t0.5\n");
    assert(LoadModPack(path.u8string(), error));
    assert(ModDimensions().size() == 1 && ModTextures().empty() && ModBlockTypeLimit() == NUM_BLOCKS_DEFINED);
    assert(ModDimensions()[0].id == "lab:moon" && ModDimensions()[0].minY == -128 && ModDimensions()[0].height == 512 && ModDimensions()[0].coordinateScale == 0.5);
    for (const auto& record : { "dimension\tlab:bad\t-129\t512\t1", "dimension\tlab:bad\t0\t512\tnan", "dimension\tlab:bad\t0\t16\t0", "dimension\tlab:bad\t2000\t512\t1", "dimension\tlab:bad\t0\t17\t1" }) {
        writeFile(path, "MINEWAYS_MOD_PACK\t1\n" + std::string(record) + "\n");
        assert(!LoadModPack(path.u8string(), error) && ModDimensions()[0].id == "lab:moon");
    }
    ClearModPack();
    assert(ModDimensions().empty());
    puts("PASS: mod pack validation, namespace isolation, chunk palettes, state round trips, concurrency and capacity");
}
