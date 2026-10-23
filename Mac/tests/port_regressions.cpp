// Include the implementations to test their private loader and parser without opening a window.
#include "../MinewaysFrame.cpp"
#include "../ImportSettings.cpp"
#include <vector>

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

int main()
{
    checkSchematicBoundaries();
    checkOptions();
    for (int version : { 5023, 3953, 2586, 1343, -1 })
        checkSchematic(version);
    puts("PASS: schematic chunk boundaries, OBJ option order, aliases, USD independence, syntax-only pass, invalid booleans, schematic versions and height bounds");
}
