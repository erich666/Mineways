// Native parser regressions. Build this with Win/nbt.cpp, blockInfo.cpp, biomes.cpp, and Mac/compat.h.
#include "../../Win/stdafx.h"
#include <vector>
#include <string>
#include <chrono>

using Bytes = std::vector<unsigned char>;

static void number(Bytes& bytes, uint64_t value, int width)
{
    for (int shift = (width - 1) * 8; shift >= 0; shift -= 8)
        bytes.push_back((unsigned char)(value >> shift));
}

static void tag(Bytes& bytes, unsigned char type, const char* name)
{
    bytes.push_back(type);
    number(bytes, strlen(name), 2);
    bytes.insert(bytes.end(), name, name + strlen(name));
}

static void stringTag(Bytes& bytes, const char* name, const char* value)
{
    tag(bytes, 8, name);
    number(bytes, strlen(value), 2);
    bytes.insert(bytes.end(), value, value + strlen(value));
}

static void real(Bytes& bytes, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    number(bytes, bits, 8);
}

static void real(Bytes& bytes, float value)
{
    unsigned int bits;
    memcpy(&bits, &value, sizeof(bits));
    number(bytes, bits, 4);
}

// Exercise the same API through memory and compressed-file readers.
template <typename Check>
static void readers(Bytes bytes, const std::string& directory, Check check)
{
    int offset = 0;
    bfFile buffer = {};
    buffer.type = BF_BUFFER;
    buffer.buf = bytes.data();
    buffer.buflen = (int)bytes.size();
    buffer.offset = &offset;
    check(&buffer);

    std::string path = directory + "/nbt-fixture.gz";
    gzFile output = gzopen(path.c_str(), "wb");
    assert(output);
    assert(gzwrite(output, bytes.data(), (unsigned int)bytes.size()) == (int)bytes.size());
    assert(gzclose(output) == Z_OK);
    bfFile compressed = {};
    compressed.type = BF_GZIP;
    compressed.gz = gzopen(path.c_str(), "rb");
    compressed.skipBytesRemaining = (uint64_t)256 * 1024 * 1024;
    assert(compressed.gz);
    check(&compressed);
    assert(gzclose(compressed.gz) == Z_OK);
    assert(remove(path.c_str()) == 0);
}

static Bytes sponge(int version, const Bytes& data, bool deferred = false, int width = 1, int paletteIndex = 0)
{
    Bytes bytes;
    tag(bytes, 10, version == 2 ? "Schematic" : "");
    if (version == 3) tag(bytes, 10, "Schematic");
    tag(bytes, 3, "Version"); number(bytes, version, 4);
    auto dimensions = [&] {
        tag(bytes, 2, "Width"); number(bytes, width, 2);
        tag(bytes, 2, "Height"); number(bytes, 1, 2);
        tag(bytes, 2, "Length"); number(bytes, 1, 2);
    };
    if (!deferred) dimensions();
    if (version == 3) tag(bytes, 10, "Blocks");
    tag(bytes, 10, "Palette");
    tag(bytes, 3, "minecraft:stone"); number(bytes, paletteIndex, 4);
    bytes.push_back(0);
    tag(bytes, 7, version == 2 ? "BlockData" : "Data");
    number(bytes, data.size(), 4);
    bytes.insert(bytes.end(), data.begin(), data.end());
    if (version == 3) bytes.push_back(0);
    if (deferred) dimensions();
    bytes.push_back(0);
    if (version == 3) bytes.push_back(0);
    return bytes;
}

static void checkSponge(const std::string& directory)
{
    for (int version : { 2, 3 }) {
        for (bool deferred : { false, true }) {
            readers(sponge(version, { 0 }, deferred), directory, [](bfFile* input) {
                int width, height, length, dataVersion;
                unsigned char* blocks;
                unsigned short* data;
                assert(nbtGetSpongeSchematic(input, &width, &height, &length, &blocks, &data, &dataVersion) == 1);
                assert(width == 1 && height == 1 && length == 1);
                assert(blocks && data && blocks[0] == BLOCK_STONE && data[0] == 0);
                free(blocks); free(data);
            });
            // UINT32_MAX, INT32_MIN, oversized positive, truncation, and 32-bit overflow.
            for (const Bytes& invalid : { Bytes{ 0xff, 0xff, 0xff, 0xff, 0x0f },
                Bytes{ 0x80, 0x80, 0x80, 0x80, 0x08 }, Bytes{ 1 }, Bytes{ 0x80 },
                Bytes{ 0x80, 0x80, 0x80, 0x80, 0x10 }, Bytes{ 0x80, 0x80, 0x80, 0x80, 0x80 } }) {
                readers(sponge(version, invalid, deferred), directory, [](bfFile* input) {
                    int width, height, length, dataVersion;
                    unsigned char* blocks = nullptr;
                    unsigned short* data = nullptr;
                    assert(nbtGetSpongeSchematic(input, &width, &height, &length, &blocks, &data, &dataVersion) == 0);
                    assert(blocks == nullptr && data == nullptr);
                    assert(width == 0 && height == 0 && length == 0);
                });
            }
        }
        Bytes multibyte;
        for (int i = 0; i < 129; i++) { multibyte.push_back(0x80); multibyte.push_back(1); }
        readers(sponge(version, multibyte, false, 129, 128), directory, [](bfFile* input) {
            int width, height, length, dataVersion;
            unsigned char* blocks;
            unsigned short* data;
            assert(nbtGetSpongeSchematic(input, &width, &height, &length, &blocks, &data, &dataVersion) == 1);
            assert(width == 129 && height == 1 && length == 1);
            for (int i = 0; i < width; i++) assert(blocks[i] == BLOCK_STONE && data[i] == 0);
            free(blocks); free(data);
        });
        // Two encoded bytes do not suffice for two voxels if the first varint takes both.
        readers(sponge(version, { 0x80, 0 }, false, 2), directory, [](bfFile* input) {
            int width, height, length, dataVersion;
            unsigned char* blocks;
            unsigned short* data;
            assert(nbtGetSpongeSchematic(input, &width, &height, &length, &blocks, &data, &dataVersion) == 0);
            assert(!blocks && !data);
        });
    }
}

static Bytes legacy(const std::vector<std::string>& fields, unsigned char type = 7)
{
    Bytes bytes;
    tag(bytes, 10, "Schematic");
    for (const auto& field : fields) {
        tag(bytes, type, field.c_str()); number(bytes, 1, 4);
        bytes.push_back(field == "Blocks" ? BLOCK_STONE : 0x80);
    }
    bytes.push_back(0);
    return bytes;
}

static void checkLegacy(const std::string& directory)
{
    for (const auto& fields : { std::vector<std::string>{ "Blocks", "Data" },
        std::vector<std::string>{ "Data", "Blocks" } }) {
        readers(legacy(fields), directory, [](bfFile* input) {
            unsigned char blocks = 0xaa;
            unsigned short data = 0xffff;
            assert(nbtGetSchematicBlocksAndData(input, 1, &blocks, &data) == 1);
            assert(blocks == BLOCK_STONE && data == 0x80);
        });
    }
    for (const auto& fields : { std::vector<std::string>{ "Blocks", "Blocks" },
        std::vector<std::string>{ "Data", "Data" }, std::vector<std::string>{ "Blocks" },
        std::vector<std::string>{ "Data" }, std::vector<std::string>{ "Blocks", "Data", "Blocks" } }) {
        readers(legacy(fields), directory, [](bfFile* input) {
            unsigned char blocks;
            unsigned short data;
            assert(nbtGetSchematicBlocksAndData(input, 1, &blocks, &data) != 1);
        });
    }
    readers(legacy({ "Blocks", "Data" }, 3), directory, [](bfFile* input) {
        unsigned char blocks;
        unsigned short data;
        assert(nbtGetSchematicBlocksAndData(input, 1, &blocks, &data) != 1);
    });
    Bytes truncated = legacy({ "Blocks", "Data" });
    truncated.resize(truncated.size() - 2);
    readers(truncated, directory, [](bfFile* input) {
        unsigned char blocks;
        unsigned short data;
        assert(nbtGetSchematicBlocksAndData(input, 1, &blocks, &data) != 1);
    });
}

static Bytes entities(unsigned int count = 1)
{
    Bytes bytes;
    tag(bytes, 10, "");
    tag(bytes, 9, "Entities"); bytes.push_back(10); number(bytes, count, 4);
    return bytes;
}

static void position(Bytes& bytes)
{
    tag(bytes, 11, "block_pos"); number(bytes, 3, 4);
    number(bytes, -2, 4); number(bytes, 70, 4); number(bytes, 3, 4);
}

static void checkEntities(const std::string& directory)
{
    Bytes valid = entities();
    stringTag(valid, "id", "minecraft:cushion");
    stringTag(valid, "color", "red");
    position(valid);
    tag(valid, 9, "Pos"); valid.push_back(6); number(valid, 3, 4);
    real(valid, -2.0); real(valid, 70.5); real(valid, 3.0);
    tag(valid, 9, "Rotation"); valid.push_back(5); number(valid, 2, 4);
    real(valid, 90.0f); real(valid, 0.0f);
    tag(valid, 10, "unused"); tag(valid, 3, "value"); number(valid, 5, 4); valid.push_back(0);
    valid.push_back(0); valid.push_back(0);
    readers(valid, directory, [](bfFile* input) {
        CushionEntity result = {};
        assert(nbtGetCushions(input, &result, 1) == 1);
        assert(result.x == -2 && result.y == 70 && result.z == 3 && result.posY == 70.5);
        assert(result.dataVal == (14 | (1 << 4)));
    });
    for (const char* field : { "block_pos", "Pos", "Rotation" }) {
        for (unsigned int count : { 0u, 1u, 0x7fffffffu, 0xffffffffu }) {
            Bytes invalid = entities();
            stringTag(invalid, "id", "minecraft:cushion");
            tag(invalid, strcmp(field, "block_pos") == 0 ? 11 : 9, field);
            if (strcmp(field, "block_pos") != 0) invalid.push_back(strcmp(field, "Pos") == 0 ? 6 : 5);
            number(invalid, count, 4);
            readers(invalid, directory, [](bfFile* input) {
                CushionEntity result = {};
                assert(nbtGetCushions(input, &result, 1) == 0);
            });
        }
    }
    for (const char* field : { "Pos", "Rotation" }) {
        Bytes invalid = entities();
        stringTag(invalid, "id", "minecraft:cushion"); position(invalid);
        tag(invalid, 9, field); invalid.push_back(0); number(invalid, 3, 4);
        invalid.push_back(0); invalid.push_back(0);
        readers(invalid, directory, [](bfFile* input) {
            CushionEntity result = {};
            assert(nbtGetCushions(input, &result, 1) == 0);
        });
    }
    readers(entities(0xffffffffu), directory, [](bfFile* input) {
        CushionEntity result = {};
        assert(nbtGetCushions(input, &result, 1) == 0);
    });
    // A shared budget also bounds repeated fields and unknown nested values.
    Bytes exhausted = entities();
    for (int i = 0; i < 100001; i++) stringTag(exhausted, "id", "minecraft:pig");
    stringTag(exhausted, "id", "minecraft:cushion"); position(exhausted);
    exhausted.push_back(0); exhausted.push_back(0);
    readers(exhausted, directory, [](bfFile* input) {
        CushionEntity result = {};
        assert(nbtGetCushions(input, &result, 1) == 0);
    });
    for (size_t size = 0; size < valid.size(); size++) {
        Bytes truncated(valid.begin(), valid.begin() + size);
        readers(truncated, directory, [](bfFile* input) {
            CushionEntity result = {};
            int count = nbtGetCushions(input, &result, 1);
            assert(count == 0 || count == 1);
        });
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    const auto start = std::chrono::steady_clock::now();
    if (argc == 2 || strcmp(argv[2], "sponge") == 0) checkSponge(argv[1]);
    if (argc == 2 || strcmp(argv[2], "legacy") == 0) checkLegacy(argv[1]);
    if (argc == 2 || strcmp(argv[2], "entities") == 0) checkEntities(argv[1]);
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
    puts("PASS: Sponge v2/v3 indices and deferred fields, legacy tag completeness, bounded entity arrays, buffer/gzip readers");
}
