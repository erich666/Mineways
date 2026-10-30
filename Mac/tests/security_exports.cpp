// Exercise the shared export boundaries without starting either GUI.
#include "../../Win/ObjFileManip.cpp"
#include <limits>

static ExportFileData settings;
static Options options;
static BoxCell cells[125];

static void resetModel()
{
    memset(&settings, 0, sizeof(settings));
    memset(&options, 0, sizeof(options));
    memset(&gModel, 0, sizeof(gModel));
    memset(&gStats, 0, sizeof(gStats));
    memset(cells, 0, sizeof(cells));
    settings.fileType = FILE_TYPE_WAVEFRONT_ABS_OBJ;
    settings.radioScaleByBlock = 1;
    settings.blockSizeVal[settings.fileType] = 1.0f;
    settings.hollowThicknessVal[settings.fileType] = 1.0f;
    options.pEFD = &settings;
    gModel.options = &options;
    gModel.scale = 0.001f;
    gModel.blockCount = 27;
    gPhysMtl = 0;
    for (int axis = 0; axis < 3; ++axis) {
        gBoxSize[axis] = 5;
        gSolidBox.min[axis] = 1;
        gSolidBox.max[axis] = 3;
        gAirBox.min[axis] = 0;
        gAirBox.max[axis] = 4;
    }
    gBoxSizeYZ = 25;
    gBoxSizeXYZ = 125;
    gBoxData = cells;
    for (int x = 1; x <= 3; ++x)
        for (int y = 1; y <= 3; ++y)
            for (int z = 1; z <= 3; ++z)
                cells[BOX_INDEX(x, y, z)].type = BLOCK_STONE;
}

static void checkHollow()
{
    for (float value : { -1.0f, 0.0f, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN() }) {
        resetModel();
        settings.hollowThicknessVal[settings.fileType] = value;
        assert(hollowBottomOfModel() == MW_CANNOT_PARSE_IMPORT_FILE);
        assert(gModel.blockCount == 27);
        resetModel();
        settings.blockSizeVal[settings.fileType] = value;
        assert(determineScaleAndHollowAndMelt() == MW_CANNOT_PARSE_IMPORT_FILE);
    }
    resetModel();
    settings.hollowThicknessVal[settings.fileType] = (std::numeric_limits<float>::max)();
    assert(hollowBottomOfModel() == MW_NO_ERROR && gModel.blockCount == 27);
    for (int superHollow : { 0, EXPT_SUPER_HOLLOW_BOTTOM }) {
        resetModel();
        options.exportFlags = EXPT_HOLLOW_BOTTOM | superHollow;
        assert(determineScaleAndHollowAndMelt() == MW_NO_ERROR);
        assert(gModel.blockCount == 25 && gStats.blocksHollowed == 2);
        assert(cells[BOX_INDEX(2, 1, 2)].type == BLOCK_AIR);
        assert(cells[BOX_INDEX(2, 3, 2)].type == BLOCK_STONE);
    }
}

static void checkDirectories()
{
    for (const char* path : { "", "tex", "my textures", "caf\xc3\xa9" })
        assert(validTextureDirectory(path));
    for (const char* path : { ".", "..", "../tex", "..\\tex", "/tmp", "C:tex",
                             "tex/child", "tex\\child", "tex.", "tex ", "tex\n", "tex\"" }) {
        resetModel();
        strcpy(settings.tileDirString, path);
        options.exportFlags = EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
        int biome = 0, biomeIndex = 0, groups = 0;
        // Invalid settings must fail before progress callbacks, world reads, or filesystem writes.
        assert(SaveVolume(nullptr, settings.fileType, &options, nullptr, nullptr,
                          0, 0, 0, 0, 0, 0, 0, 255, nullptr, nullptr, nullptr, nullptr,
                          0, 0, 0, nullptr, 0, biome, biomeIndex, groups, 0, nullptr)
               == MW_CANNOT_CREATE_DIRECTORY);
    }
    char unterminated[MAX_PATH];
    memset(unterminated, 'a', sizeof(unterminated));
    assert(!validTextureDirectory(unterminated));
}

static void checkChangeBlocks()
{
    for (bool arrayMode : { false, true })
    for (unsigned short type : { (unsigned short)BLOCK_STONE, (unsigned short)255,
                                 (unsigned short)256, (unsigned short)513, (unsigned short)4095 })
    for (unsigned short data : { (unsigned short)0, (unsigned short)15, (unsigned short)16,
                                 (unsigned short)31, (unsigned short)32, (unsigned short)4095 }) {
        resetModel();
        gSolidBox.max[X] = gSolidBox.max[Y] = gSolidBox.max[Z] = 1;
        BoxCell& cell = cells[BOX_INDEX(1, 1, 1)];
        cell.type = type;
        cell.data = data;
        unsigned short mask[256];
        for (auto& bits : mask) bits = 0xffff;
        ChangeBlockCommand command = {};
        command.useFromArray = arrayMode;
        command.fromDataBitsArray = mask;
        command.simpleFromTypeEnd = 255;
        command.simpleFromDataBits = 0xffff;
        command.intoType = BLOCK_AIR;
        assert(applyChangeBlockCommand(&command));
        assert(cell.type == (type < 256 && data < 16 ? BLOCK_AIR : type));
    }
}

static void checkDirectoryLinks(const char* tempDirectory)
{
    std::string pattern = std::string(tempDirectory) + "/export-directories-XXXXXX";
    std::vector<char> root(pattern.begin(), pattern.end());
    root.push_back(0);
    assert(mkdtemp(root.data()));
    std::string ordinary = std::string(root.data()) + "/tex";
    std::string outside = std::string(root.data()) + "/outside";
    std::string link = std::string(root.data()) + "/linked";
    assert(mkdir(outside.c_str(), 0700) == 0);
    assert(symlink(outside.c_str(), link.c_str()) == 0);
    wchar_t wide[MAX_PATH_AND_FILE];
    assert(_mwUtf8ToWideBuffer(ordinary.c_str(), wide, MAX_PATH_AND_FILE));
    assert(createExportDirectory(wide));
    assert(createExportDirectory(wide));
    assert(_mwUtf8ToWideBuffer((link + "/").c_str(), wide, MAX_PATH_AND_FILE));
    assert(!createExportDirectory(wide));
    assert(unlink(link.c_str()) == 0);
    assert(symlink("missing-target", link.c_str()) == 0);
    assert(!createExportDirectory(wide));
    assert(unlink(link.c_str()) == 0);
    assert(rmdir(ordinary.c_str()) == 0);
    assert(rmdir(outside.c_str()) == 0);
    assert(rmdir(root.data()) == 0);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    checkHollow();
    checkDirectories();
    checkDirectoryLinks(argv[1]);
    checkChangeBlocks();
    gBoxData = nullptr;
    puts("PASS: shared export settings, hollow geometry, texture directories, and block masks");
}
