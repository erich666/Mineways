// Optional local smoke test using a compiled pack of actual installed mod assets.
#include "../../Win/ObjFileManip.cpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <chrono>

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
                        bool detailed = true, bool doubled = false, bool printing = false, bool expectFailure = false)
{
    ExportFileData settings = {};
    Options options = {};
    const int fileType = path.extension() == ".schematic" ? FILE_TYPE_SCHEMATIC : path.extension() == ".schem" ? FILE_TYPE_SPONGE_SCHEMATIC : path.extension() == ".usda" ? FILE_TYPE_USD : FILE_TYPE_WAVEFRONT_ABS_OBJ;
    settings.fileType = fileType;
    settings.chkCreateModelFiles[fileType] = 1;
    settings.chkIndividualBlocks[fileType] = 1;
    settings.radioRotate0 = settings.radioScaleByBlock = 1;
    settings.blockSizeVal[fileType] = 1000.0f;
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
    options.exportFlags = mode | EXPT_INDIVIDUAL_BLOCKS;
    if (printing) options.exportFlags |= EXPT_3DPRINT;
    if (fileType == FILE_TYPE_SPONGE_SCHEMATIC || fileType == FILE_TYPE_SCHEMATIC) options.exportFlags = 0;
    FileList output = {};
    int biome = -1, biomeIndex = 0, groups = 0;
    std::wstring filename = wide(path), directory = wide(path.parent_path());
    wchar_t terrain[] = L"";
    wchar_t culling[] = L"Standard";
    ClearCache();
    const int result = SaveVolume(filename.data(), fileType, &options, &world, directory.c_str(),
        0, 0, 0, world.sch.width - 1, world.sch.height - 1, world.sch.length - 1,
        0, world.sch.height - 1, progress, terrain, culling, &output,
        14, 3, 4189, nullptr, 16, biome, biomeIndex, groups, 0, nullptr);
    for (int i = 0; i < output.count; ++i) free(output.name[i]);
    if (expectFailure) {
        assert(result >= MW_BEGIN_ERRORS);
        assert(!std::filesystem::exists(path));
        return;
    }
    if (result >= MW_BEGIN_NOTHING_TO_DO) {
        fprintf(stderr, "Export %s returned %d\n", path.u8string().c_str(), result);
        abort();
    }
    assert(std::filesystem::is_regular_file(path));
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


struct ObjStats { size_t vertices = 0, uvs = 0, normals = 0, faces = 0; std::string geometry; };
static ObjStats validateObj(const std::filesystem::path& path)
{
    ObjStats result;
    std::istringstream lines(readText(path));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string tag; fields >> tag;
        if (tag == "v" || tag == "vt" || tag == "vn") {
            double value;
            const int count = tag == "vt" ? 2 : 3;
            for (int i = 0; i < count; ++i) { assert(fields >> value); assert(std::isfinite(value)); }
            if (tag == "v") ++result.vertices;
            else if (tag == "vt") ++result.uvs;
            else ++result.normals;
        } else if (tag == "f") {
            int count = 0;
            std::string entry;
            while (fields >> entry) {
                int vertex, uv, normal;
                assert(sscanf(entry.c_str(), "%d/%d/%d", &vertex, &uv, &normal) == 3);
                assert(vertex > 0 && (size_t)vertex <= result.vertices);
                assert(uv > 0 && (size_t)uv <= result.uvs);
                assert(normal > 0 && (size_t)normal <= result.normals);
                ++count;
            }
            assert(count >= 3); ++result.faces;
        } else continue;
        result.geometry += line + "\n";
    }
    assert(result.vertices && result.uvs && result.faces);
    return result;
}

int main(int argc, char** argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: real-mod-pack manifest.mwpack output-directory\n"); return 2; }
    const std::filesystem::path output(argv[2]);
    // Refuse existing output to keep real asset tests separate from user data.
    assert(std::filesystem::create_directory(output));
    SetSeparatorObj(L"/"); SetSeparatorMap(L"/"); nbtInitTables();
    std::string error;
    const auto loadStart = std::chrono::steady_clock::now();
    if (!LoadModPack(argv[1], error)) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
    const double loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - loadStart).count();
    for (const auto& texture : ModTextures()) assert(texture.rgba.empty());
    printf("Catalog loaded in %.3f seconds with zero decoded textures.\n", loadSeconds);
    SetMapPremultipliedColors(0);
    std::map<std::string, std::vector<std::string>> byBlock;
    std::istringstream manifest(readText(argv[1]));
    std::string line;
    size_t selectors = 0;
    while (std::getline(manifest, line)) if (line.compare(0, 6, "state\t") == 0) {
        const std::string state = line.substr(6, line.find('\t', 6) - 6);
        byBlock[state.substr(0, state.find('['))].push_back(state);
        ++selectors;
    }
    assert(!byBlock.empty());
    std::vector<std::string> selected;
    // Sample across the complete pack, with two variants for each selected block.
    const size_t step = (byBlock.size() + 63) / 64;
    size_t index = 0;
    for (const auto& block : byBlock) {
        if (index++ % step) continue;
        selected.push_back(block.second.front());
        if (block.second.size() > 1) selected.push_back(block.second.back());
    }
    const int width = (int)selected.size() * 2 - 1;
    std::vector<unsigned char> blocks(width, 0);
    std::vector<unsigned short> data(width, 0);
    std::ofstream selection(output / "selected-states.txt");
    for (size_t i = 0; i < selected.size(); ++i) {
        int type, state;
        assert(ResolveModState(selected[i], &type, &state) == 1);
        blocks[i * 2] = (unsigned char)(type & 255);
        data[i * 2] = PACK_TYPE_EXT_AND_DATAVAL(type, state);
        selection << GetModBlock(type, state)->canonical << '\n';
    }
    selection.close();
    WorldGuide world = {};
    world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks.data(), data.data(), width, 1, 1, width, false, true};
    world.minHeight = world.maxHeight = 0;
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES |
                     EXPT_OUTPUT_OBJ_MTL_PER_TYPE | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    exportWorld(world, output / "actual.obj", mode);
    const ObjStats geometry = validateObj(output / "actual.obj");
    std::set<std::string> textureFiles;
    std::istringstream material(readText(output / "actual.mtl"));
    while (std::getline(material, line)) if (line.compare(0, 7, "map_Kd ") == 0) textureFiles.insert(line.substr(7));
    assert(!textureFiles.empty());
    size_t checkedTextures = 0;
    for (const auto& textureFile : textureFiles) {
        const std::filesystem::path path = output / textureFile;
        assert(std::filesystem::is_regular_file(path));
        const std::string stem = path.stem().u8string();
        std::vector<unsigned char> image;
        unsigned w, h;
        assert(lodepng::decode(image, w, h, wide(path)) == 0 && w && h);
        // Waterlogged states also emit the built-in water texture.
        if (stem.compare(0, 7, "mw_mod_") != 0) continue;
        const size_t textureIndex = (size_t)std::stoul(stem.substr(7));
        assert(textureIndex < ModTextures().size());
        assert(EnsureModTextures({(int)textureIndex}, error));
        const auto& original = ModTextures()[textureIndex];
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
            for (unsigned channel = 0; channel < 4; ++channel)
                assert(image[((size_t)y * w + x) * 4 + channel] == original.rgba[
                    ((size_t)(y * original.height / h) * original.width + x * original.width / w) * 4 + channel]);
        ++checkedTextures;
    }
    assert(checkedTextures);
    exportWorld(world, output / "repeat.obj", mode);
    assert(validateObj(output / "repeat.obj").geometry == geometry.geometry);
    exportWorld(world, output / "actual.schem", 0);
    WorldGuide roundtrip = readSchematic(output / "actual.schem");
    assert(stateNames(roundtrip) == stateNames(world));
    exportWorld(roundtrip, output / "roundtrip.obj", mode);
    assert(validateObj(output / "roundtrip.obj").geometry == geometry.geometry);
    printf("PASS: %zu loaded blocks, %zu selectors, %zu textures; %zu selected states; %zu vertices, %zu faces, %zu pixel-checked textures; repeated OBJ and exact Sponge roundtrip\n",
           byBlock.size(), selectors, ModTextures().size(), selected.size(), geometry.vertices, geometry.faces, checkedTextures);
    printf("Artifacts: %s\n", output.u8string().c_str());
    ClearCache(); free(roundtrip.sch.blocks); free(roundtrip.sch.data); ClearModPack();
}
