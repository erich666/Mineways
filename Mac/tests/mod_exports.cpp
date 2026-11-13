// Real exports through the shared core, with original synthetic mod assets.
#include "../../Win/ObjFileManip.cpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <array>

static void progress(float, wchar_t*) {}

static std::string readText(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    assert(stream.good());
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

static int countLines(const std::string& text, const char* prefix)
{
    int count = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) if (line.compare(0, strlen(prefix), prefix) == 0) ++count;
    return count;
}

static std::wstring wide(const std::filesystem::path& path)
{
    wchar_t result[MAX_PATH_AND_FILE];
    assert(_mwUtf8ToWideBuffer(path.u8string().c_str(), result, MAX_PATH_AND_FILE));
    return result;
}

static void exportWorld(WorldGuide& world, const std::filesystem::path& path, int mode,
                        bool detailed = true, bool doubled = false, bool printing = false, bool expectFailure = false,
                        bool expectEmpty = false, bool windowsUsdDefaults = false, bool individual = true,
                        int formatOverride = -1, int rotation = 0, float blockSize = 1000.0f, int firstX = 0)
{
    const bool existingFailureTarget = expectFailure && std::filesystem::exists(path);
    const std::string previousTarget = existingFailureTarget ? readText(path) : std::string();
    ExportFileData settings = {};
    Options options = {};
    const int fileType = formatOverride >= 0 ? formatOverride : path.extension() == ".schematic" ? FILE_TYPE_SCHEMATIC : path.extension() == ".schem" ? FILE_TYPE_SPONGE_SCHEMATIC : path.extension() == ".usda" ? FILE_TYPE_USD : path.extension() == ".stl" ? FILE_TYPE_ASCII_STL : FILE_TYPE_WAVEFRONT_ABS_OBJ;
    settings.fileType = fileType;
    settings.chkCreateModelFiles[fileType] = 1;
    settings.chkIndividualBlocks[fileType] = individual;
    settings.radioScaleByBlock = 1;
    settings.radioRotate0 = rotation == 0; settings.radioRotate90 = rotation == 90;
    settings.radioRotate180 = rotation == 180; settings.radioRotate270 = rotation == 270;
    settings.blockSizeVal[fileType] = blockSize;
    settings.hollowThicknessVal[fileType] = 1000.0f;
    settings.comboModelUnits[fileType] = UNITS_METER;
    settings.chkExportAll = detailed;
    settings.chkDoubledBillboards = doubled;
    settings.chkBlockFacesAtBorders = 1;
    settings.chkTextureRGBA = 1;
    strcpy(settings.tileDirString, "tex");
    options.pEFD = &settings;
    options.saveFilterFlags = BLF_WHOLE | BLF_ALMOST_WHOLE | BLF_STAIRS | BLF_HALF | BLF_MIDDLER |
                              BLF_BILLBOARD | BLF_PANE | BLF_FLATTEN | BLF_FLATTEN_SMALL |
                              BLF_SMALL_MIDDLER | BLF_SMALL_BILLBOARD;
    options.exportFlags = mode | (individual ? EXPT_INDIVIDUAL_BLOCKS : 0);
    if (windowsUsdDefaults) {
        assert(fileType == FILE_TYPE_USD);
        settings.scaleLightsVal = 15.0f;
        settings.scaleEmittersVal = 20.0f;
        settings.chkTextureRGB = settings.chkTextureA = settings.chkTextureRGBA = 1;
        settings.chkMaterialPerFamily = settings.chkMakeGroupsObjects = settings.chkSplitByBlockType = 1;
        settings.radioExportTileTextures[fileType] = 1;
        settings.comboPhysicalMaterial[fileType] = PRINT_MATERIAL_FULL_COLOR_SANDSTONE;
        // initializeViewExportData leaves custom material and MDL disabled for USD.
        options.exportFlags |= EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK |
                               EXPT_OUTPUT_EACH_BLOCK_A_GROUP;
    }
    if (printing) options.exportFlags |= EXPT_3DPRINT;
    if (fileType == FILE_TYPE_SPONGE_SCHEMATIC || fileType == FILE_TYPE_SCHEMATIC) options.exportFlags = 0;
    FileList output = {};
    int biome = -1, biomeIndex = 0, groups = 0;
    std::wstring filename = wide(path), directory = wide(path.parent_path());
    wchar_t terrain[] = L"";
    wchar_t culling[] = L"Standard";
    ClearCache();
    const int result = SaveVolume(filename.data(), fileType, &options, &world, directory.c_str(),
        firstX, 0, 0, world.sch.width - 1, world.sch.height - 1, world.sch.length - 1,
        0, world.sch.height - 1, progress, terrain, culling, &output,
        14, 3, 4189, nullptr, 16, biome, biomeIndex, groups, 0, nullptr);
    for (int i = 0; i < output.count; ++i) free(output.name[i]);
    if (expectEmpty) {
        assert(result == MW_NO_BLOCKS_FOUND);
        assert(!std::filesystem::exists(path));
        return;
    }
    if (expectFailure) {
        assert(result >= MW_BEGIN_ERRORS);
        if (existingFailureTarget) assert(readText(path) == previousTarget);
        else assert(!std::filesystem::exists(path));
        return;
    }
    if (result >= MW_BEGIN_NOTHING_TO_DO) {
        fprintf(stderr, "Export %s returned %d\n", path.u8string().c_str(), result);
        abort();
    }
    assert(std::filesystem::is_regular_file(path));
}

static void checkCubeUVs(const std::string& text)
{
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<double, 2>> uvs;
    std::istringstream stream(text);
    std::string line, material;
    int checked = 0;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string token; fields >> token;
        if (token == "v") {
            std::array<double, 3> point;
            fields >> point[0] >> point[1] >> point[2];
            vertices.push_back(point);
        } else if (token == "vt") {
            std::array<double, 2> uv;
            fields >> uv[0] >> uv[1]; uvs.push_back(uv);
        } else if (token == "usemtl") {
            fields >> material;
        } else if (token == "f") {
            std::vector<std::pair<int, int>> indices;
            bool cube = true;
            while (fields >> token) {
                int vertex, uv, normal;
                assert(sscanf(token.c_str(), "%d/%d/%d", &vertex, &uv, &normal) == 3);
                indices.push_back({vertex - 1, uv - 1});
                if (vertices[vertex - 1][0] > 1.0) cube = false;
            }
            if (!cube) continue;
            assert(indices.size() == 4);
            int side = -1;
            const int axes[] = {1, 1, 2, 2, 0, 0};
            const double positions[] = {0, 1, 0, 1, 0, 1};
            for (int candidate = 0; candidate < 6; ++candidate) {
                bool onPlane = true;
                for (const auto& index : indices)
                    onPlane &= vertices[index.first][axes[candidate]] == positions[candidate];
                if (onPlane) side = candidate;
            }
            assert(side >= 0);
            const char* textureNames[] = {"down", "up", "north", "south", "west", "east"};
            const std::string expectedTexture = std::string("fixture:block/") + textureNames[side];
            size_t textureIndex = 0;
            while (textureIndex < ModTextures().size() && ModTextures()[textureIndex].name != expectedTexture) ++textureIndex;
            assert(textureIndex < ModTextures().size());
            assert(material == "mw_mod_" + std::to_string(textureIndex));
            for (const auto& index : indices) {
                const auto& p = vertices[index.first];
                const auto& uv = uvs[index.second];
                const double wantU[] = {p[0], p[0], 1 - p[0], p[0], p[2], 1 - p[2]};
                const double wantV[] = {p[2], 1 - p[2], p[1], p[1], p[1], p[1]};
                assert(fabs(uv[0] - wantU[side]) < 1e-7 && fabs(uv[1] - wantV[side]) < 1e-7);
            }
            ++checked;
        }
    }
    assert(checked == 6);
}

static void checkNeutralPBR(size_t selectedCount = SIZE_MAX, const std::filesystem::path& output = {})
{
    for (int format : {FILE_TYPE_WAVEFRONT_ABS_OBJ, FILE_TYPE_USD}) {
        memset(&gModel, 0, sizeof(gModel));
        gModel.tileSize = 16;
        gModel.swatchSize = 18;
        gModel.swatchesPerRow = 64;
        std::vector<int> used;
        for (size_t i = 0; i < ModTextures().size() && i < selectedCount; ++i) used.push_back((int)i);
        assert(prepareModTextures(format, used));
        progimage_info normal = {}, roughness = {};
        normal.width = roughness.width = gModel.swatchesPerRow * gModel.swatchSize;
        normal.height = roughness.height = ((exportTileCount() + gModel.swatchesPerRow - 1) / gModel.swatchesPerRow) * gModel.swatchSize;
        normal.image_data.assign((size_t)normal.width * normal.height * 3, 17);
        roughness.image_data.assign((size_t)roughness.width * roughness.height, 17);
        copyModTextures(&normal, CATEGORY_NORMALS);
        copyModTextures(&roughness, CATEGORY_ROUGHNESS);
        for (int index : used) {
            const int swatch = gModTextureSwatches[index];
            const int left = (swatch % gModel.swatchesPerRow) * gModel.swatchSize;
            const int top = (swatch / gModel.swatchesPerRow) * gModel.swatchSize;
            // Include the clamped borders, which mosaic UVs can sample.
            for (int y = 0; y < gModel.swatchSize; ++y) for (int x = 0; x < gModel.swatchSize; ++x) {
                const size_t pixel = (size_t)(top + y) * normal.width + left + x;
                assert(normal.image_data[pixel * 3] == 128 && normal.image_data[pixel * 3 + 1] == 128 && normal.image_data[pixel * 3 + 2] == 255);
                assert(roughness.image_data[pixel] == (format == FILE_TYPE_USD ? 255 : 0));
            }
        }
        if (!output.empty()) {
            const auto file = output / ("neutral-normal-" + std::to_string(format) + ".png");
            std::wstring filename = wide(file);
            FileList files = {};
            gOutputFileList = &files;
            assert(writeTileFromCategoryInput(filename.data(), TOTAL_TILES, CATEGORY_NORMALS) == 0);
            assert(files.count == 1 && filename == files.name[0]);
            free(files.name[0]);
            gOutputFileList = nullptr;
            std::vector<unsigned char> pixels; unsigned width, height;
            assert(lodepng::decode(pixels, width, height, filename) == 0);
            assert(width == 16 && height == 16);
            for (size_t i = 0; i < pixels.size(); i += 4)
                assert(pixels[i] == 128 && pixels[i + 1] == 128 && pixels[i + 2] == 255 && pixels[i + 3] == 255);
        }
        restoreModTextureSlots();
    }
    memset(&gModel, 0, sizeof(gModel));
    gTotalInputTextures = 1;
}

static WorldGuide readSchematic(const std::filesystem::path& path)
{
    WorldGuide world = {};
    world.type = WORLD_SCHEMATIC_TYPE;
    world.sch.isSponge = true;
    std::wstring filename = wide(path);
    int error = 0, version = 0;
    bfFile nbt = newNBT(filename.c_str(), &error);
    assert(!error);
    assert(nbtGetSpongeSchematic(&nbt, &world.sch.width, &world.sch.height, &world.sch.length,
                                &world.sch.blocks, &world.sch.data, &version) == 1);
    nbtClose(&nbt);
    world.sch.numBlocks = world.sch.width * world.sch.height * world.sch.length;
    world.maxHeight = world.sch.height - 1;
    return world;
}

static std::vector<std::string> stateNames(const WorldGuide& world)
{
    std::vector<std::string> states;
    for (int i = 0; i < world.sch.numBlocks; ++i) {
        int type = BLOCK_TYPE_FROM_GRID_DATA(world.sch.blocks[i], world.sch.data[i]);
        int data = BLOCK_DATAVAL_FROM_DATA(world.sch.data[i]);
        const auto* state = GetModBlock(type, data);
        if (state) states.push_back(state->canonical);
    }
    return states;
}

#include "mod_geometry.h"
#include "mod_polygons.h"
#include "mod_precision.h"
#include "mod_rotations.h"
#include "mod_repeated_uv.h"
#include "mod_normals.h"
#include "mod_split_exports.h"

int main(int argc, char** argv)
{
    assert(argc == 3);
    const std::filesystem::path root(argv[2]);
    std::filesystem::create_directories(root);
    SetSeparatorObj(L"/");
    SetSeparatorMap(L"/");
    nbtInitTables();
    SetMapPremultipliedColors(0);
    std::string error;
    assert(LoadModPack(argv[1], error));
    assert(ModDimensions().size() == 1);
    assert(ModDimensions()[0].id == "fixture:moon" && ModDimensions()[0].minY == -64);
    assert(ModDimensions()[0].height == 384 && ModDimensions()[0].coordinateScale == 2.0);
    checkNeutralPBR(SIZE_MAX, root);
    int lampType, off, on;
    assert(ResolveModState("fixture:lamp[lit=false]", &lampType, &off) == 1);
    assert(ResolveModState("fixture:lamp[lit=true]", &lampType, &on) == 1);
    assert(GetBlockDataColor(lampType, off) == GetModBlock(lampType, off)->definition->color);
    assert(GetBlockDataColor(lampType, on) == GetModBlock(lampType, on)->definition->color);
    assert(GetBlockDataColor(lampType, off) != GetBlockDataColor(lampType, on));
    assert(GetBlockDataColor(lampType, PACK_TYPE_EXT_AND_DATAVAL(lampType, on)) == GetModBlock(lampType, on)->definition->color);
    int cube, cubeState, panel, panelState, wedge, wedgeState;
    for (int i = 0; i <= 64; ++i) {
        const std::string state = "fixture:cube[extra=" + std::to_string(i) + "]";
        assert(ResolveModState(state, &cube, &cubeState) == 1);
        if (cubeState == 64) break;
    }
    assert(cubeState == 64);
    assert(!ModBlockWaterlogged(cube, cubeState));
    int firstCubeState;
    assert(ResolveModState("fixture:cube[extra=0]", &cube, &firstCubeState) == 1);
    assert(ResolveModState("fixture:wedge[extra=retained,facing=east]", &wedge, &wedgeState) == 1);
    assert(ResolveModState("fixture:panel", &panel, &panelState) == 1);
    unsigned short blocks[7] = {(unsigned short)cube, 0, (unsigned short)wedge, 0, (unsigned short)panel, 0, (unsigned short)cube};
    unsigned short data[7] = {PACK_TYPE_EXT_AND_DATAVAL(cube, firstCubeState), 0, PACK_TYPE_EXT_AND_DATAVAL(wedge, wedgeState), 0,
                             PACK_TYPE_EXT_AND_DATAVAL(panel, panelState), 0, PACK_TYPE_EXT_AND_DATAVAL(cube, cubeState)};
    WorldGuide world = {};
    world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks, data, 7, 1, 1, 7, false, true};
    world.minHeight = world.maxHeight = 0;
    const int textures = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_OBJ_MTL_PER_TYPE;
    exportWorld(world, root / "individual.obj", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    const std::string individual = readText(root / "individual.obj");
    assert(countLines(individual, "f ") == 19);
    checkCubeUVs(individual);
    const std::string materials = readText(root / "individual.mtl");
    std::set<std::string> maps;
    std::istringstream materialStream(materials);
    std::string line;
    while (std::getline(materialStream, line)) if (line.compare(0, 7, "map_Kd ") == 0) maps.insert(line.substr(7));
    assert(maps.size() == 7);
    for (const std::string& path : maps) assert(std::filesystem::is_regular_file(root / path));
    std::vector<int> usedTextures;
    for (const auto& map : maps) {
        const std::string stem = std::filesystem::path(map).stem().string();
        if (stem.compare(0, 7, "mw_mod_") == 0) usedTextures.push_back(std::stoi(stem.substr(7)));
    }
    assert(usedTextures.size() == 7);
    assert(EnsureModTextures(usedTextures, error));
    for (int i : usedTextures) {
        std::vector<unsigned char> image;
        unsigned width, height;
        const std::wstring file = wide(root / "tex" / ("mw_mod_" + std::to_string(i) + ".png"));
        assert(lodepng::decode(image, width, height, file) == 0);
        assert(width == 16 && height == 16);
        const ModTexture& source = ModTextures()[i];
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
            for (int channel = 0; channel < 4; ++channel)
                assert(image[(y * width + x) * 4 + channel] ==
                    source.rgba[((y * source.height / height) * source.width + x * source.width / width) * 4 + channel]);
    }
    exportWorld(world, root / "mosaic.obj", textures);
    assert(countLines(readText(root / "mosaic.obj"), "f ") == 19);
    exportWorld(world, root / "plain.obj", 0);
    assert(countLines(readText(root / "plain.obj"), "f ") == 19);
    exportWorld(world, root / "solid.obj", EXPT_OUTPUT_MATERIALS);
    assert(countLines(readText(root / "solid.obj"), "f ") == 19);
    exportWorld(world, root / "double.obj", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES, true, true);
    assert(countLines(readText(root / "double.obj"), "f ") == 20);
    exportWorld(world, root / "coarse.obj", 0, false);
    assert(countLines(readText(root / "coarse.obj"), "f ") > 0);
    exportWorld(world, root / "printing.obj", 0, true, false, true);
    assert(countLines(readText(root / "printing.obj"), "f ") == 18);
    int wetType, wetState;
    assert(ResolveModState("fixture:wedge[facing=east,waterlogged=true]", &wetType, &wetState) == 1);
    assert(ModBlockWaterlogged(wetType, wetState));
    unsigned short wetBlocks[] = {(unsigned short)wetType};
    unsigned short wetData[] = {PACK_TYPE_EXT_AND_DATAVAL(wetType, wetState)};
    WorldGuide wetWorld = world;
    wetWorld.sch = {wetBlocks, wetData, 1, 1, 1, 1, false, true};
    exportWorld(wetWorld, root / "waterlogged.obj", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(countLines(readText(root / "waterlogged.obj"), "f ") > 6);
    exportWorld(world, root / "legacy.schematic", 0, true, false, false, true);
    exportWorld(world, root / "first.schem", 0);
    WorldGuide first = readSchematic(root / "first.schem");
    assert(stateNames(first) == stateNames(world));
    exportWorld(first, root / "second.schem", 0);
    WorldGuide second = readSchematic(root / "second.schem");
    assert(stateNames(first) == stateNames(second));
    assert(readText(root / "first.schem") == readText(root / "second.schem"));
    for (int i = 65; cubeState <= 256; ++i) {
        const std::string state = "fixture:cube[extra=" + std::to_string(i) + "]";
        assert(ResolveModState(state, &cube, &cubeState) == 1);
    }
    assert(cubeState > 255);
    data[6] = PACK_TYPE_EXT_AND_DATAVAL(cube, cubeState);
    exportWorld(world, root / "instances.usda", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    const std::string usd = readText(root / "instances.usda");
    assert(usd.find("#usda") != std::string::npos);
    const std::string library = readText(root / "instances_materials" / "BlockLibrary.usda");
    assert(library.find("def Xform \"Block_" + std::to_string(cube) + "_" + std::to_string(cubeState) + "\"") != std::string::npos);
    assert(countLines(library, "    def Xform ") == 4);
    std::string longName = "fixture:";
    for (int i = 0; i < 60; ++i) longName += "long/";
    longName += "cube[property=retained]";
    int longType, longState;
    assert(longName.size() > 256 && ResolveModState(longName, &longType, &longState) == 1);
    unsigned short longBlock[] = {(unsigned short)longType};
    unsigned short longData[] = {PACK_TYPE_EXT_AND_DATAVAL(longType, longState)};
    WorldGuide longWorld = world;
    longWorld.sch = {longBlock, longData, 1, 1, 1, 1, false, true};
    exportWorld(longWorld, root / "long.obj", EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK | EXPT_OUTPUT_OBJ_MTL_PER_TYPE);
    assert(countLines(readText(root / "long.obj"), "f ") == 6);
    exportWorld(longWorld, root / "long.schem", 0);
    WorldGuide longRead = readSchematic(root / "long.schem");
    assert(stateNames(longRead) == stateNames(longWorld));
    ClearCache();
    free(longRead.sch.blocks); free(longRead.sch.data);
    unsigned short lampBlocks[] = {(unsigned short)lampType, 0, (unsigned short)lampType};
    unsigned short lampData[] = {PACK_TYPE_EXT_AND_DATAVAL(lampType, off), 0, PACK_TYPE_EXT_AND_DATAVAL(lampType, on)};
    WorldGuide lampWorld = world;
    lampWorld.sch = {lampBlocks, lampData, 3, 1, 1, 3, false, true};
    exportWorld(lampWorld, root / "lamp.obj", EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_OBJ_SEPARATE_TYPES | EXPT_OUTPUT_OBJ_MATERIAL_PER_BLOCK | EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_OBJ_SPLIT_BY_BLOCK_TYPE);
    assert(countLines(readText(root / "lamp.mtl"), "newmtl ") == 2);
    ClearCache();
    free(first.sch.blocks); free(first.sch.data);
    free(second.sch.blocks); free(second.sch.data);
    ClearModPack();
    // A transparent top/map color must not discard visible side geometry.
    std::string zeroAlpha = readText(argv[1]);
    const size_t stateStart = zeroAlpha.find("state\tfixture:cube\t");
    assert(stateStart != std::string::npos);
    const size_t stateEnd = zeroAlpha.find('\n', stateStart);
    const size_t alphaStart = zeroAlpha.rfind('\t', stateEnd) + 1;
    zeroAlpha.replace(alphaStart, stateEnd - alphaStart, "0");
    const auto zeroPath = std::filesystem::path(argv[1]).parent_path() / "zero-map-alpha.mwpack";
    { std::ofstream manifest(zeroPath, std::ios::binary); manifest << zeroAlpha; assert(manifest.good()); }
    assert(LoadModPack(zeroPath.u8string(), error));
    assert(ResolveModState("fixture:cube", &cube, &cubeState) == 1);
    assert(GetModBlock(cube, cubeState)->definition->alpha == 0);
    unsigned short zeroBlocks[] = {(unsigned short)cube};
    unsigned short zeroData[] = {PACK_TYPE_EXT_AND_DATAVAL(cube, cubeState)};
    WorldGuide zeroWorld = {};
    zeroWorld.type = WORLD_SCHEMATIC_TYPE;
    zeroWorld.sch = {zeroBlocks, zeroData, 1, 1, 1, 1, false, true};
    exportWorld(zeroWorld, root / "zero-alpha-textured.obj", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    exportWorld(zeroWorld, root / "zero-alpha-plain.obj", 0);
    exportWorld(zeroWorld, root / "zero-alpha-solid.obj", EXPT_OUTPUT_MATERIALS);
    for (const char* filename : {"zero-alpha-textured.obj", "zero-alpha-plain.obj", "zero-alpha-solid.obj"})
        assert(countLines(readText(root / filename), "f ") == 6);
    ClearCache(); ClearModPack();
    // Invisible registered blocks survive Sponge round trips without adding render geometry or bounds.
    const auto invisiblePath = std::filesystem::path(argv[1]).parent_path() / "invisible.mwpack";
    { std::ofstream manifest(invisiblePath, std::ios::binary);
      manifest << readText(argv[1]) << "state\tfixture:invisible\t000000\t0\ninvisible\nendstate\n";
      assert(manifest.good()); }
    assert(LoadModPack(invisiblePath.u8string(), error));
    int invisibleType, invisibleState;
    assert(ResolveModState("fixture:invisible[extra=retained]", &invisibleType, &invisibleState) == 1);
    unsigned short invisibleBlocks[] = {(unsigned short)invisibleType};
    unsigned short invisibleData[] = {PACK_TYPE_EXT_AND_DATAVAL(invisibleType, invisibleState)};
    WorldGuide invisibleWorld = {};
    invisibleWorld.type = WORLD_SCHEMATIC_TYPE;
    invisibleWorld.sch = {invisibleBlocks, invisibleData, 1, 1, 1, 1, false, true};
    exportWorld(invisibleWorld, root / "invisible.obj", 0, true, false, false, false, true);
    exportWorld(invisibleWorld, root / "invisible.schem", 0);
    WorldGuide invisibleRead = readSchematic(root / "invisible.schem");
    assert(stateNames(invisibleRead) == stateNames(invisibleWorld));
    exportWorld(invisibleRead, root / "invisible-roundtrip.obj", 0, true, false, false, false, true);
    assert(ResolveModState("fixture:cube", &cube, &cubeState) == 1);
    unsigned short mixedBlocks[] = {(unsigned short)invisibleType, 0, (unsigned short)cube};
    unsigned short mixedData[] = {PACK_TYPE_EXT_AND_DATAVAL(invisibleType, invisibleState), 0,
                                 PACK_TYPE_EXT_AND_DATAVAL(cube, cubeState)};
    WorldGuide mixedWorld = invisibleWorld;
    mixedWorld.sch = {mixedBlocks, mixedData, 3, 1, 1, 3, false, true};
    exportWorld(mixedWorld, root / "invisible-mixed.obj", textures | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
    assert(countLines(readText(root / "invisible-mixed.obj"), "f ") == 6);
    exportWorld(mixedWorld, root / "invisible-mixed.schem", 0);
    WorldGuide mixedRead = readSchematic(root / "invisible-mixed.schem");
    assert(stateNames(mixedRead) == stateNames(mixedWorld));
    ClearCache();
    free(invisibleRead.sch.blocks); free(invisibleRead.sch.data);
    free(mixedRead.sch.blocks); free(mixedRead.sch.data);
    checkSplitModExports(root);
    checkModPolygons(root);
    checkModPrecision(root);
    checkModRotations(root);
    checkRepeatedModUV(root);
    checkModNormalCapacity(root);
    ClearModPack();
    checkExtendedModGeometry(root);
    puts("PASS: mod UVs/pixels, signed geometry/alpha, OBJ/USD modes, states above 255, long names, dimensions, and schematic round trip");
    return 0;
}
