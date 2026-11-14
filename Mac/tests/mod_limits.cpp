// Boundary exports use original test textures and every representable block ID.
#define main regular_mod_exports_main
#include "mod_exports.cpp"
#undef main

static std::string geometryText(const std::filesystem::path& path)
{
    std::istringstream stream(readText(path));
    std::string line, result;
    while (std::getline(stream, line))
        if (line.compare(0, 2, "v ") == 0 || line.compare(0, 3, "vt ") == 0 ||
            line.compare(0, 3, "vn ") == 0 || line.compare(0, 2, "f ") == 0 || line.compare(0, 7, "usemtl ") == 0)
            result += line + "\n";
    return result;
}

static WorldGuide makeWorld(std::vector<unsigned short>& blocks, std::vector<unsigned short>& data)
{
    WorldGuide world = {};
    world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks.data(), data.data(), (int)blocks.size(), 1, 1, (int)blocks.size(), false, true};
    world.minHeight = world.maxHeight = 0;
    return world;
}

static void checkMosaic(const std::filesystem::path& object, size_t expectedBlocks)
{
    auto material = object; material.replace_extension(".mtl");
    std::istringstream mtl(readText(material));
    std::string line, texture;
    while (std::getline(mtl, line)) if (line.compare(0, 7, "map_Kd ") == 0) { texture = line.substr(7); break; }
    assert(!texture.empty());
    std::vector<unsigned char> pixels;
    unsigned width = 0, height = 0;
    assert(lodepng::decode(pixels, width, height, wide(object.parent_path() / texture)) == 0);
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<double, 2>> coordinates;
    std::istringstream obj(readText(object));
    size_t faces = 0;
    while (std::getline(obj, line)) {
        std::istringstream fields(line); std::string kind; fields >> kind;
        if (kind == "v") { std::array<double, 3> p; fields >> p[0] >> p[1] >> p[2]; vertices.push_back(p); }
        else if (kind == "vt") {
            std::array<double, 2> uv; fields >> uv[0] >> uv[1];
            assert(uv[0] >= 0 && uv[0] <= 1 && uv[1] >= 0 && uv[1] <= 1); coordinates.push_back(uv);
        } else if (kind == "f") {
            std::string item; double x = 0, u = 0, v = 0; int count = 0;
            while (fields >> item) {
                int vi, ti, ni; assert(sscanf(item.c_str(), "%d/%d/%d", &vi, &ti, &ni) == 3);
                assert(vi > 0 && (size_t)vi <= vertices.size() && ti > 0 && (size_t)ti <= coordinates.size());
                x += vertices[vi - 1][0]; u += coordinates[ti - 1][0]; v += coordinates[ti - 1][1]; ++count;
            }
            assert(count == 4);
            const int index = (int)floor(x / count / 2);
            assert(index >= 0 && (size_t)index < expectedBlocks);
            const unsigned px = (std::min)(width - 1, (unsigned)(u / count * width));
            const unsigned py = (std::min)(height - 1, (unsigned)((1 - v / count) * height));
            const size_t offset = ((size_t)py * width + px) * 4;
            assert(pixels[offset] == (index & 255) && pixels[offset + 1] == (index >> 8));
            assert(pixels[offset + 2] == 73 && pixels[offset + 3] == 255);
            ++faces;
        }
    }
    assert(faces == expectedBlocks * 6);
}

static void checkWideMap(WorldGuide& world)
{
    Options options = {};
    options.currentCacheSize = 1000;
    Change_Cache_Size(options.currentCacheSize);
    std::vector<unsigned char> actual(12 * 3 * 4), expected(actual.size());
    int hits[4] = {};
    ClearCache();
    assert(DrawMap(&world, 4.5, 0.5, 0, 0, 12, 3, 1.0,
                   actual.data(), &options, hits, nullptr, 21, 3953) >= 0);
    std::vector<unsigned short> referenceBlocks(world.sch.blocks, world.sch.blocks + world.sch.numBlocks);
    std::vector<unsigned short> referenceData(referenceBlocks.size(), 0);
    for (auto& type : referenceBlocks) if (type) type = NUM_BLOCKS_DEFINED;
    WorldGuide reference = makeWorld(referenceBlocks, referenceData);
    ClearCache();
    assert(DrawMap(&reference, 4.5, 0.5, 0, 0, 12, 3, 1.0,
                   expected.data(), &options, hits, nullptr, 21, 3953) >= 0);
    assert(actual == expected);
    std::fill(referenceBlocks.begin(), referenceBlocks.end(), 0);
    ClearCache();
    assert(DrawMap(&reference, 4.5, 0.5, 0, 0, 12, 3, 1.0,
                   expected.data(), &options, hits, nullptr, 21, 3953) >= 0);
    assert(actual != expected);  // A matching empty map must not satisfy this test.
    for (unsigned short type : {4095, 4096, 32767, 32768, 65535})
        assert(GetBlockDataColor(type, 0) == GetBlockDataColor(NUM_BLOCKS_DEFINED, 0));
    ClearCache();
}

static void checkWideCache()
{
    WorldBlock* block = block_alloc(0, 31);
    assert(block);
    for (size_t i = 0; i < 8192; ++i) {
        block->grid[i] = (unsigned short)((i & 1) ? 65535 : 4096);
        block->data[i] = 0x8c0;
    }
    memset(block->light, 0xa5, 4096);
    block->maxFilledHeight = 15;
    MinimizeCacheBlocks(true);
    block_realloc(block);
    assert(block->heightAlloc == 16);
    for (size_t i = 0; i < 4096; ++i) {
        assert(block->grid[i] == ((i & 1) ? 65535 : 4096));
        assert(block->data[i] == 0x8c0);
        assert(block->light[i / 2] == 0xa5);
    }
    assert(Cache_BytesPerChunk(384) - Cache_BytesPerChunk(1) == (384 - 1) * 256 * 9 / 2);
    block_free(block); block_pool_trim();
    MinimizeCacheBlocks(false);
}

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path root(argv[2]);
    assert(std::filesystem::create_directory(root));
    SetSeparatorObj(L"/"); SetSeparatorMap(L"/"); nbtInitTables(); SetMapPremultipliedColors(0);
    checkWideCache();
    BlockInstance low = {}, high = {};
    low.hash = makeInstanceHash(32767, 0); high.hash = makeInstanceHash(65535, 257);
    assert(instanceUSDCompare(nullptr, &low, &low) == 0);
    assert(instanceUSDCompare(nullptr, &low, &high) < 0 && instanceUSDCompare(nullptr, &high, &low) > 0);
    static_assert(MAX_OUTPUT_FILES >= 6 + 5 * (TOTAL_TILES + 4096), "Output list must fit all selected texture channels");
    std::string error;
    assert(LoadModPack(argv[1], error));
    assert(ModTextures().size() == 4097);
    // Activate the entire uint16 range, including previously forbidden low-byte-zero IDs.
    for (int expected = NUM_BLOCKS_DEFINED; expected <= 65535; ++expected) {
        int type = 0, data = -1;
        assert(ResolveModState("wide:block_" + std::to_string(expected - NUM_BLOCKS_DEFINED), &type, &data) == 1);
        assert(type == expected && data == 0);
    }
    std::vector<unsigned short> blocks(9, 0), data(9, 0);
    const int boundaries[] = {4095, 4096, 32767, 32768, 65535};
    for (int i = 0; i < 5; ++i) blocks[i * 2] = (unsigned short)boundaries[i];
    int lastType = 0, lastState = 0;
    for (int state = 1; state <= 257; ++state) {
        assert(ResolveModState("wide:block_64974[extra=" + std::to_string(state) + "]", &lastType, &lastState) == 1);
        assert(lastType == 65535 && lastState == state);
    }
    data[8] = (unsigned short)lastState;
    WorldGuide world = makeWorld(blocks, data);
    checkWideMap(world);
    const int textured = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    exportWorld(world, root / "wide.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(countLines(readText(root / "wide.obj"), "f ") == 30);
    exportWorld(world, root / "wide.usda", EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES);
    const std::string library = readText(root / "wide_materials" / "BlockLibrary.usda");
    for (int i = 0; i < 5; ++i)
        assert(library.find("def Xform \"Block_" + std::to_string(boundaries[i]) + "_" +
                           std::to_string(i == 4 ? 257 : 0) + "\"") != std::string::npos);
    assert(countLines(library, "    def Xform ") == 5);
    fprintf(stderr, "Boundary USD: Windows individual-texture defaults begin\n");
    exportWorld(world, root / "wide-windows.usda", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES,
                true, false, false, false, false, true);
    fprintf(stderr, "Boundary USD: Windows individual-texture defaults complete\n");
    const std::string windowsLibrary = readText(root / "wide-windows_materials" / "BlockLibrary.usda");
    assert(countLines(windowsLibrary, "    def Xform ") == 5);
    for (int i = 0; i < 5; ++i)
        assert(windowsLibrary.find("def Xform \"Block_" + std::to_string(boundaries[i]) + "_" +
                                  std::to_string(i == 4 ? 257 : 0) + "\"") != std::string::npos);
    exportWorld(world, root / "wide.schem", 0);
    WorldGuide roundtrip = readSchematic(root / "wide.schem");
    assert(stateNames(world) == stateNames(roundtrip));
    for (int i = 0; i < 5; ++i) assert(roundtrip.sch.blocks[i * 2] == boundaries[i]);
    assert(roundtrip.sch.data[8] == 257);
    exportWorld(roundtrip, root / "wide-repeat.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(geometryText(root / "wide.obj") == geometryText(root / "wide-repeat.obj"));
    free(roundtrip.sch.blocks); free(roundtrip.sch.data);
    checkNeutralPBR(4096, root);

    blocks.assign(4096 * 2 - 1, 0); data.assign(blocks.size(), 0);
    for (size_t i = 0; i < 4096; ++i) blocks[i * 2] = (unsigned short)(NUM_BLOCKS_DEFINED + i);
    world = makeWorld(blocks, data);
    exportWorld(world, root / "tiles.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(countLines(readText(root / "tiles.obj"), "f ") == 4096 * 6);
    assert(countLines(readText(root / "tiles.obj"), "vt ") > 32767);
    for (int i = 0; i < 4096; ++i) {
        std::vector<unsigned char> pixels; unsigned width, height;
        assert(lodepng::decode(pixels, width, height, wide(root / "tex" / ("mw_mod_" + std::to_string(i) + ".png"))) == 0);
        assert(width == 16 && height == 16);
        for (size_t p = 0; p < pixels.size(); p += 4)
            assert(pixels[p] == (i & 255) && pixels[p + 1] == (i >> 8) && pixels[p + 2] == 73 && pixels[p + 3] == 255);
    }
    exportWorld(world, root / "tiles-repeat.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(geometryText(root / "tiles.obj") == geometryText(root / "tiles-repeat.obj"));
    exportWorld(world, root / "mosaic.obj", textured);
    checkMosaic(root / "mosaic.obj", 4096);
    exportWorld(world, root / "tiles.schem", 0);
    roundtrip = readSchematic(root / "tiles.schem");
    assert(stateNames(world) == stateNames(roundtrip));
    exportWorld(roundtrip, root / "tiles-roundtrip.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(geometryText(root / "tiles.obj") == geometryText(root / "tiles-roundtrip.obj"));
    free(roundtrip.sch.blocks); free(roundtrip.sch.data);
    // Solid swatches have a separate allocation path from image textures. Reverse type order
    // so dense material slots cannot accidentally match the block IDs or atlas indices.
    for (size_t i = 0; i < 4096; ++i) blocks[i * 2] = (unsigned short)(NUM_BLOCKS_DEFINED + 4095 - i);
    world = makeWorld(blocks, data);
    exportWorld(world, root / "solid-many.usda", EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES);
    const std::string solidLibrary = readText(root / "solid-many_materials" / "BlockLibrary.usda");
    assert(countLines(solidLibrary, "    def Xform ") == 4096);
    assert(solidLibrary.find("def Xform \"Block_4656_0\"") != std::string::npos);
    for (size_t i = 0; i < 4096; ++i) blocks[i * 2] = (unsigned short)(NUM_BLOCKS_DEFINED + i);
    blocks.push_back(0); blocks.push_back((unsigned short)(NUM_BLOCKS_DEFINED + 4096));
    data.resize(blocks.size(), 0); world = makeWorld(blocks, data);
    exportWorld(world, root / "over-limit.obj", textured | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES, true, false, false, true);
    ClearCache(); ClearModPack();
    puts("PASS: all uint16 IDs active, boundary OBJ/USD/Sponge identity, 4096 textures/pixels/UV/PBR, repeated exports, explicit 4097 failure");
    return 0;
}
