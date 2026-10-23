// Include the implementations to test their private loader and parser without opening a window.
#include "../MinewaysFrame.cpp"
#include "../ImportSettings.cpp"
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>

static void parseOption(ImportedSet& settings, const char* command)
{
    char line[100];
    strcpy_s(line, sizeof(line), command);
    assert(InterpretImportLine(line, settings) == INTERPRETER_FOUND_VALID_EXPORT_LINE);
}

static void checkOptions()
{
    for (int type : { FILE_TYPE_WAVEFRONT_ABS_OBJ, FILE_TYPE_WAVEFRONT_REL_OBJ, FILE_TYPE_USD }) {
        ExportFileData data = {};
        data.fileType = type;
        ImportedSet settings;
        settings.pEFD = &data;
        settings.readingModel = false;
        const bool obj = type != FILE_TYPE_USD;
        parseOption(settings, "Individual blocks: true");
        parseOption(settings, "Export separate types: true");
        assert(data.chkSeparateTypes && data.chkIndividualBlocks[type] == !obj);
        parseOption(settings, "Export individual blocks: true");
        assert(data.chkIndividualBlocks[type] && data.chkSeparateTypes == !obj);
        parseOption(settings, "Export separate objects: false");
        assert(!data.chkSeparateTypes && data.chkIndividualBlocks[type]);
        parseOption(settings, "Export separate objects: true");
        parseOption(settings, "Individual blocks: false");
        assert(data.chkSeparateTypes && !data.chkIndividualBlocks[type]);

        data.chkIndividualBlocks[type] = 1;
        settings.processData = false;
        parseOption(settings, "Export separate types: true");
        parseOption(settings, "Individual blocks: true");
        assert(data.chkSeparateTypes && data.chkIndividualBlocks[type]);
        settings.processData = true;
        char invalid[] = "Individual blocks: invalid";
        assert(InterpretImportLine(invalid, settings) == INTERPRETER_FOUND_ERROR);
        assert(data.chkSeparateTypes && data.chkIndividualBlocks[type]);
    }
}

static void appendNumber(std::vector<unsigned char>& bytes, unsigned int value, int width)
{
    for (int shift = (width - 1) * 8; shift >= 0; shift -= 8)
        bytes.push_back((unsigned char)(value >> shift));
}

static void appendTag(std::vector<unsigned char>& bytes, unsigned char type, const char* name)
{
    bytes.push_back(type);
    appendNumber(bytes, (unsigned int)strlen(name), 2);
    bytes.insert(bytes.end(), name, name + strlen(name));
}

static void checkSchematic(int version)
{
    std::vector<unsigned char> bytes;
    appendTag(bytes, 10, "Schematic");
    appendTag(bytes, 3, "Version"); appendNumber(bytes, 2, 4);
    if (version >= 0) {
        appendTag(bytes, 3, "DataVersion"); appendNumber(bytes, version, 4);
    }
    for (const char* name : { "Width", "Height", "Length" }) {
        appendTag(bytes, 2, name); appendNumber(bytes, 1, 2);
    }
    appendTag(bytes, 3, "PaletteMax"); appendNumber(bytes, 1, 4);
    appendTag(bytes, 10, "Palette");
    appendTag(bytes, 3, "minecraft:stone"); appendNumber(bytes, 0, 4);
    bytes.push_back(0);
    appendTag(bytes, 7, "BlockData"); appendNumber(bytes, 1, 4);
    bytes.push_back(0);
    bytes.push_back(0);

    char path[] = "/tmp/mineways-port-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    gzFile file = gzdopen(fd, "wb");
    assert(file);
    assert(gzwrite(file, bytes.data(), (unsigned int)bytes.size()) == (int)bytes.size());
    assert(gzclose(file) == Z_OK);
    wchar_t widePath[256];
    assert(_mwUtf8ToWideBuffer(path, widePath, 256));
    gWorldGuide.type = WORLD_SCHEMATIC_TYPE;
    assert(LoadSchematicFile(widePath, true) == 0);
    assert(gVersionID == (version >= 1519 ? version : 2586));
    assert(gMinHeight == 0 && gMaxHeight == 255);
    assert(gWorldGuide.minHeight == 0 && gWorldGuide.maxHeight == 255);
    assert(gWorldGuide.sch.numBlocks == 1 && gWorldGuide.sch.blocks[0] == BLOCK_STONE);
    ClearLoadedWorldData();
    assert(unlink(path) == 0);
}

static void checkSchematicBoundaries()
{
    for (int size : { 16, 17, 32 }) {
        WorldGuide guide = {};
        guide.type = WORLD_SCHEMATIC_TYPE;
        guide.maxHeight = 255;
        guide.sch.width = guide.sch.length = size;
        guide.sch.height = 8;
        guide.sch.numBlocks = size * size * 8;
        std::vector<unsigned char> blocks(guide.sch.numBlocks, BLOCK_STONE);
        std::vector<unsigned short> data(guide.sch.numBlocks, 0);
        guide.sch.blocks = blocks.data();
        guide.sch.data = data.data();
        const int outside = (size + 15) / 16;
        const int positions[][2] = {
            { 0, 0 }, { outside - 1, outside - 1 },
            { outside, 0 }, { 0, outside }, { outside, outside },
            { -1, 0 }, { 0, -1 }
        };
        for (const auto& position : positions) {
            int result = 0;
            WorldBlock* block = LoadBlock(&guide, position[0], position[1],
                DATA_VERSION_TO_RELEASE_NUMBER(5023), 5023, result);
            const bool inside = position[0] >= 0 && position[0] < outside &&
                position[1] >= 0 && position[1] < outside;
            assert((block != nullptr) == inside);
            if (inside) {
                assert(block->grid[0] == BLOCK_STONE);
                const int lastX = (size - 1) % 16;
                const int lastZ = (size - 1) % 16;
                assert(block->grid[(7 * 16 + lastZ) * 16 + lastX] == BLOCK_STONE);
            }
            block_free(block);
        }
    }
    block_pool_trim();
}


static void writeWorldFixture(const std::filesystem::path& path, const char* block)
{
    std::filesystem::create_directories(path / "region");
    std::vector<unsigned char> chunk;
    appendTag(chunk, 10, "");
    appendTag(chunk, 3, "DataVersion"); appendNumber(chunk, 3953, 4);
    appendTag(chunk, 9, "sections"); chunk.push_back(10); appendNumber(chunk, 1, 4);
    appendTag(chunk, 1, "Y"); chunk.push_back(0);
    appendTag(chunk, 10, "block_states");
    appendTag(chunk, 9, "palette"); chunk.push_back(10); appendNumber(chunk, 1, 4);
    appendTag(chunk, 8, "Name"); appendNumber(chunk, (unsigned int)strlen(block), 2);
    chunk.insert(chunk.end(), block, block + strlen(block));
    chunk.push_back(0); chunk.push_back(0); chunk.push_back(0); chunk.push_back(0);
    std::vector<unsigned char> compressed(compressBound(chunk.size()));
    uLongf length = compressed.size();
    assert(compress(compressed.data(), &length, chunk.data(), chunk.size()) == Z_OK);
    std::vector<unsigned char> region(8192, 0);
    for (int i = 0; i < 1024; ++i) { region[i * 4 + 2] = 2; region[i * 4 + 3] = 1; }
    appendNumber(region, (unsigned int)length + 1, 4); region.push_back(2);
    region.insert(region.end(), compressed.begin(), compressed.begin() + length);
    for (int x = -1; x <= 0; ++x) for (int z = -1; z <= 0; ++z) {
        FILE* file = fopen((path / "region" / ("r." + std::to_string(x) + "." + std::to_string(z) + ".mca")).c_str(), "wb");
        assert(file);
        assert(fwrite(region.data(), 1, region.size(), file) == region.size());
        assert(fclose(file) == 0);
    }
}

static WorldGuide fixtureGuide(const std::filesystem::path& path)
{
    WorldGuide world = {};
    world.type = WORLD_LEVEL_TYPE;
    world.minHeight = -64; world.maxHeight = 319;
    assert(_mwUtf8ToWideBuffer(path.c_str(), world.world, MAX_PATH_AND_FILE));
    SetDimensionDirectory(&world, 0);
    return world;
}

static void checkMapPerformance()
{
    const wchar_t* separator = L"/";
    SetSeparatorMap(separator);
    char temp[] = "/tmp/mineways-map-tests-XXXXXX";
    assert(mkdtemp(temp));
    std::filesystem::path root(temp);
    writeWorldFixture(root / "stone", "minecraft:stone");
    writeWorldFixture(root / "gold", "minecraft:gold_block");
    WorldGuide stone = fixtureGuide(root / "stone"), gold = fixtureGuide(root / "gold");
    const int version = 3953, mc = DATA_VERSION_TO_RELEASE_NUMBER(version);
    int result = 0;
    // A real region read validates the fixture before testing scheduling.
    WorldBlock* block = LoadBlock(&stone, 0, 0, mc, version, result);
    assert(block && block->grid[64 * 256] == BLOCK_STONE);
    block_free(block);
    for (int cycle = 0; cycle < 8; ++cycle) {
        for (int i = 0; i < 256; ++i)
            assert(ChunkLoaderRequest(&stone, i % 16, i / 16, mc, version, 0));
        // CloseAll must join running reads and discard all uncollected results.
        CloseAll();
        assert(!ChunkLoaderBusy());
        assert(ChunkLoaderRequest(&gold, 0, 0, mc, version, 0));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (ChunkLoaderBusy() && std::chrono::steady_clock::now() < deadline) {
            int x[64], z[64];
            ChunkLoaderCollect(x, z, 64, result);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(!ChunkLoaderBusy());
        void* cached = nullptr;
        assert(Cache_Find(0, 0, &cached) && cached);
        assert(((WorldBlock*)cached)->grid[64 * 256] == 41);
        CloseAll();
    }
    MapRenderer renderer;
    Options options = gOptions;
    options.currentCacheSize = 1000;
    Change_Cache_Size(options.currentCacheSize);
    options.worldType = 0;
    options.moreExportMemory = false;
    constexpr int width = 384, height = 128;
    std::vector<unsigned char> incremental(width * height * 4), reference(incremental.size());
    int hits[4] = {};
    for (double zoom : { 0.5, 1.0, 1.1, 16.0 })
    for (double center : { 8.0, 9.25, -0.75, -17.0, 40.5 }) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool pending;
        do {
            pending = renderer.Render(&stone, center, center, 383, 319, width, height, zoom,
                incremental.data(), &options, hits, mc, version, result);
            assert(result >= 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (pending && std::chrono::steady_clock::now() < deadline);
        assert(!pending);
        assert(DrawMap(&stone, center, center, 383, 319, width, height, zoom,
            reference.data(), &options, hits, nullptr, mc, version) >= 0);
        assert(incremental == reference);
    }
    // A style change must invalidate the retained pixels, too.
    options.worldType = MAP_GRID;
    while (renderer.Render(&stone, 40.5, 40.5, 383, 319, width, height, 1.0,
               incremental.data(), &options, hits, mc, version, result)) {}
    DrawMap(&stone, 40.5, 40.5, 383, 319, width, height, 1.0,
        reference.data(), &options, hits, nullptr, mc, version);
    assert(incremental == reference);
    // Even the widest view has a finite cache-sized plan.
    renderer.Reset();
    int passes = 0;
    while (renderer.Render(&stone, 0, 0, 383, 319, width, height, 0.0625,
               incremental.data(), &options, hits, mc, version, result)) {
        assert(++passes < 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CloseAll();
    block_pool_trim();
    std::filesystem::remove_all(root);
}

int main()
{
    checkMapPerformance();
    checkSchematicBoundaries();
    checkOptions();
    for (int version : { 5023, 3953, 2586, 1343, -1 })
        checkSchematic(version);
    puts("PASS: loader cancellation/world switching, incremental panning/style parity, bounded wide views, schematic chunk boundaries, OBJ option order, aliases, USD independence, syntax-only pass, invalid booleans, schematic versions and height bounds");
}
