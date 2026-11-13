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

// Test-only part suffix selects saved context without changing the Minecraft state.
static void resolveSample(const std::string& sample, int& type, int& data)
{
    const auto suffix = sample.find("|part=");
    std::string canonical = sample.substr(0, suffix);
    assert(ResolveModState(canonical, &type, &data) == 1);
    const auto* state = GetModBlock(type, data);
    if (!state->contextMissing && suffix == std::string::npos) return;
    const auto& source = *state->sourceDefinition;
    // Asset selectors omit properties consumed by Java offset providers. These are
    // explicit synthetic test states, never inferred production-world defaults.
    const auto addProperty = [&](const char* name, const char* value) {
        const std::string property = std::string(name) + "=";
        if (canonical.find("[" + property) != std::string::npos || canonical.find("," + property) != std::string::npos) return;
        if (!canonical.empty() && canonical.back() == ']') {
            canonical.pop_back(); canonical += "," + property + value + "]";
        } else canonical += "[" + property + value + "]";
    };
    if (source.splitPolicy == ModSplitPolicy::Post) {
        assert(suffix == std::string::npos);
        addProperty("post_slave", "0"); addProperty("horizontal_offset", "none");
        assert(ResolveModState(canonical, &type, &data) == 1);
        assert(!GetModBlock(type, data)->contextMissing);
        return;
    }
    if (source.splitPolicy == ModSplitPolicy::SlaveX || source.splitPolicy == ModSplitPolicy::SlaveY ||
        source.splitPolicy == ModSplitPolicy::SlaveZ || source.splitPolicy == ModSplitPolicy::Meter) {
        assert(suffix == std::string::npos);
        addProperty("multiblockslave", "false");
        assert(ResolveModState(canonical, &type, &data) == 1);
        assert(!GetModBlock(type, data)->contextMissing);
        return;
    }
    assert(source.splitPolicy == ModSplitPolicy::Dummy || source.splitPolicy == ModSplitPolicy::Position);
    int part[3] = {};
    if (suffix != std::string::npos) {
        assert(sscanf(sample.c_str() + suffix + 6, "%d,%d,%d", &part[0], &part[1], &part[2]) == 3);
    } else {
        const auto model = std::find_if(source.models.begin(), source.models.end(), [](const ModModel& m) {
            return !m.Elements().empty() || !m.Polygons().empty();
        });
        assert(model != source.models.end());
        std::copy(model->part, model->part + 3, part);
    }
    ModBlockContext context;
    context.entityId = source.masterEntityId;
    if (source.splitPolicy == ModSplitPolicy::Dummy) {
        context.hasDummy = true;
        context.dummy = part[1];
    } else {
        assert(context.entityId.size() > 7 && context.entityId.compare(context.entityId.size()-7, 7, "_master") == 0);
        context.entityId.replace(context.entityId.size()-7, 7, "_dummy");
        context.hasPosition = true;
        for (int i = 0; i < 3; ++i) context.position[i] = part[i] + source.master[i];
        if (canonical.find("mirrored=true") != std::string::npos)
            context.position[0] = source.splitSizeX - context.position[0] - 1;
    }
    assert(ResolveModContext(type, data, context, &data) == 1);
    assert(!GetModBlock(type, data)->contextMissing);
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "Usage: real-mod-pack manifest.mwpack output-directory [extra-state ...]\n"); return 2; }
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
    // Activate the complete installed catalog, not just the later render sample. This
    // exercises IDs above 4095 and 32767 with actual assets before any export.
    int expectedType = NUM_BLOCKS_DEFINED;
    for (const auto& block : byBlock) {
        int type = 0, state = -1;
        assert(ResolveModState(block.second.front(), &type, &state) == 1);
        assert(type == expectedType++ && state == 0);
    }
    printf("Activated %zu actual block names; highest internal ID %d.\n", byBlock.size(), expectedType - 1);
    std::vector<std::string> selected;
    // Sample across the complete pack, with two variants for each selected block.
    const size_t step = (byBlock.size() + 63) / 64;
    size_t index = 0;
    for (const auto& block : byBlock) {
        if (index++ % step) continue;
        selected.push_back(block.second.front());
        if (block.second.size() > 1) selected.push_back(block.second.back());
    }
    const char* targets[] = {
        "handcrafted:acacia_bench", "handcrafted:blue_bowl", "botanypots:black_concrete_botany_pot",
        "bibliocraft:black_fancy_gold_lamp", "refinedstorage:cable", "cabletiers:black_creative_constructor",
        "bibliocraft:acacia_table", "bibliocraft:acacia_bookcase", "xycraft_machines:machine_base",
        "xycraft_override:smooth_polished_tuff", "advancedperipherals:block_reader",
        "waystones:blackstone_waystone", "refurbished_furniture:acacia_desk",
        "modern_industrialization:assembler",
        "modern_industrialization:bronze_furnace",
        "modern_industrialization:configurable_tank",
        "extended_industrialization:large_steel_crate",
        "industrialization_overdrive:multi_processing_array",
        "refinedstorage:disk_interface",
        "refinedstorage:disk_drive",
        "cabletiers:black_creative_disk_interface",
        "extendedae:ex_drive",
        "supplementaries:awning",
        "supplementaries:awning_red",
        "create:crushing_wheel",
        "create:large_water_wheel",
        "neovitae:raw_spiritus_crystal",
        "xycraft_machines:collector",
        "xycraft_machines:energy_selector",
        "xycraft_machines:stake",
        "allthecompressed:xychorium_storage_blue_1x",
        "allthecompressed:xychorium_storage_red_9x",
        "xycraft_world:aluminum_bricks_cloud_blue",
        "xycraft_world:xychorium_storage_blue",
        "xycraft_world:lamp_pillar_blue",
        "xycraft_world:lamp_lantern_blue",
        "framedblocks:framed_glowing_cube",
        "framedblocks:framed_glowing_item_frame",
        "framedblocks:framed_item_frame",
        "framedblocks:framed_lever",
        "framedblocks:framed_redstone_torch",
        "framedblocks:framed_redstone_wall_torch",
        "framedblocks:framed_soul_torch",
        "framedblocks:framed_soul_wall_torch",
        "framedblocks:framed_torch",
        "framedblocks:framed_wall_torch",
        "mekanism:qio_redstone_adapter",
        "pneumaticcraft:advanced_liquid_compressor",
        "pneumaticcraft:assembly_platform",
        "pneumaticcraft:charging_station",
        "pneumaticcraft:creative_compressor",
        "pneumaticcraft:display_shelf",
        "pneumaticcraft:display_table",
        "pneumaticcraft:drill_pipe",
        "pneumaticcraft:electrostatic_compressor",
        "pneumaticcraft:elevator_base",
        "pneumaticcraft:etching_tank",
        "pneumaticcraft:fluid_mixer",
        "pneumaticcraft:flux_compressor",
        "pneumaticcraft:gas_lift",
        "pneumaticcraft:heat_pipe",
        "pneumaticcraft:heat_sink",
        "pneumaticcraft:huge_tank",
        "pneumaticcraft:kerosene_lamp",
        "pneumaticcraft:large_tank",
        "pneumaticcraft:liquid_compressor",
        "pneumaticcraft:medium_tank",
        "pneumaticcraft:plastic_brick_black",
        "pneumaticcraft:plastic_brick_blue",
        "pneumaticcraft:plastic_brick_brown",
        "pneumaticcraft:plastic_brick_cyan",
        "pneumaticcraft:plastic_brick_gray",
        "pneumaticcraft:plastic_brick_green",
        "pneumaticcraft:plastic_brick_light_blue",
        "pneumaticcraft:plastic_brick_light_gray",
        "pneumaticcraft:plastic_brick_lime",
        "pneumaticcraft:plastic_brick_magenta",
        "pneumaticcraft:plastic_brick_orange",
        "pneumaticcraft:plastic_brick_pink",
        "pneumaticcraft:plastic_brick_purple",
        "pneumaticcraft:plastic_brick_red",
        "pneumaticcraft:plastic_brick_white",
        "pneumaticcraft:plastic_brick_yellow",
        "pneumaticcraft:pneumatic_door_base",
        "pneumaticcraft:pneumatic_dynamo",
        "pneumaticcraft:refinery",
        "pneumaticcraft:refinery_output",
        "pneumaticcraft:reinforced_chest",
        "pneumaticcraft:small_tank",
        "pneumaticcraft:smart_chest",
        "pneumaticcraft:smooth_plastic_brick_black",
        "pneumaticcraft:smooth_plastic_brick_blue",
        "pneumaticcraft:smooth_plastic_brick_brown",
        "pneumaticcraft:smooth_plastic_brick_cyan",
        "pneumaticcraft:smooth_plastic_brick_gray",
        "pneumaticcraft:smooth_plastic_brick_green",
        "pneumaticcraft:smooth_plastic_brick_light_blue",
        "pneumaticcraft:smooth_plastic_brick_light_gray",
        "pneumaticcraft:smooth_plastic_brick_lime",
        "pneumaticcraft:smooth_plastic_brick_magenta",
        "pneumaticcraft:smooth_plastic_brick_orange",
        "pneumaticcraft:smooth_plastic_brick_pink",
        "pneumaticcraft:smooth_plastic_brick_purple",
        "pneumaticcraft:smooth_plastic_brick_red",
        "pneumaticcraft:smooth_plastic_brick_white",
        "pneumaticcraft:smooth_plastic_brick_yellow",
        "pneumaticcraft:tag_workbench",
        "pneumaticcraft:thermopneumatic_processing_plant",
        "pneumaticcraft:uv_light_box",
        "pneumaticcraft:vacuum_pump",
        "pneumaticcraft:vacuum_trap",
        "refinedstorage:creative_portable_grid",
        "refinedstorage:portable_grid",
        "pneumaticcraft:advanced_pressure_tube",
        "pneumaticcraft:pressure_tube",
        "pneumaticcraft:reinforced_pressure_tube",
        "sophisticatedstorage:copper_shulker_box",
        "sophisticatedstorage:diamond_shulker_box",
        "sophisticatedstorage:gold_shulker_box",
        "sophisticatedstorage:iron_shulker_box",
        "sophisticatedstorage:netherite_shulker_box",
        "sophisticatedstorage:shulker_box",
        "mekanism:advanced_energy_cube",
        "mekanism:basic_energy_cube",
        "mekanism:creative_energy_cube",
        "mekanism:elite_energy_cube",
        "mekanism:ultimate_energy_cube",
        "moderndynamics:ev_cable",
        "moderndynamics:fluid_pipe",
        "moderndynamics:hv_cable",
        "moderndynamics:item_pipe",
        "moderndynamics:lv_cable",
        "moderndynamics:mv_cable",
        "moderndynamics:superconductor_cable",
        "sophisticatedbackpacks:backpack",
        "sophisticatedbackpacks:copper_backpack",
        "sophisticatedbackpacks:diamond_backpack",
        "sophisticatedbackpacks:gold_backpack",
        "sophisticatedbackpacks:iron_backpack",
        "sophisticatedbackpacks:netherite_backpack",
        "sophisticatedstorage:barrel",
        "sophisticatedstorage:copper_barrel",
        "sophisticatedstorage:diamond_barrel",
        "sophisticatedstorage:gold_barrel",
        "sophisticatedstorage:iron_barrel",
        "sophisticatedstorage:netherite_barrel",
        "twilightforest:arctic_fur_block",
        "twilightforest:auroralized_glass",
        "twilightforest:blue_castle_door",
        "twilightforest:blue_force_field",
        "twilightforest:green_force_field",
        "twilightforest:orange_force_field",
        "twilightforest:pink_castle_door",
        "twilightforest:pink_force_field",
        "twilightforest:violet_castle_door",
        "twilightforest:violet_force_field",
        "twilightforest:yellow_castle_door",
        "xycraft_machines:buffer",
        "xycraft_machines:pipe_item_sorter",
        "xycraft_machines:pipe_item_filter",
        "xycraft_machines:engineering_table",
        "xycraft_machines:extractor",
        "xycraft_machines:isolator",
        "xycraft_machines:nitrogen_extractor",
        "xycraft_override:bricks_red",
        "immersiveengineering:chute_aluminum",
        "immersiveengineering:chute_colored_red",
        "immersiveengineering:lantern",
        "immersiveengineering:cagelamp",
        "immersiveengineering:balloon",
        "immersiveengineering:siren",
        "immersiveengineering:logic_unit",
        "immersiveengineering:fluid_pipe",
        "immersiveengineering:alu_slope",
        "immersiveengineering:steel_slope",
        "immersiveengineering:breaker_switch",
        "immersiveengineering:redstone_breaker",
        "immersiveengineering:redstone_timer",
        "immersiveengineering:connector_probe",
        "immersiveengineering:connector_redstone",
        "immersiveengineering:connector_structural",
        "immersiveengineering:floodlight",
        "immersiveengineering:turret_gun",
        "immersiveengineering:turret_chem",
        "sophisticatedstorage:acacia_storage_connector",
        "sophisticatedstorage:bamboo_storage_connector",
        "sophisticatedstorage:birch_storage_connector",
        "sophisticatedstorage:cherry_storage_connector",
        "sophisticatedstorage:controller",
        "sophisticatedstorage:crimson_storage_connector",
        "sophisticatedstorage:dark_oak_storage_connector",
        "sophisticatedstorage:jungle_storage_connector",
        "sophisticatedstorage:limited_barrel_1",
        "sophisticatedstorage:limited_barrel_2",
        "sophisticatedstorage:limited_barrel_3",
        "sophisticatedstorage:limited_barrel_4",
        "sophisticatedstorage:limited_copper_barrel_1",
        "sophisticatedstorage:limited_copper_barrel_2",
        "sophisticatedstorage:limited_copper_barrel_3",
        "sophisticatedstorage:limited_copper_barrel_4",
        "sophisticatedstorage:limited_diamond_barrel_1",
        "sophisticatedstorage:limited_diamond_barrel_2",
        "sophisticatedstorage:limited_diamond_barrel_3",
        "sophisticatedstorage:limited_diamond_barrel_4",
        "sophisticatedstorage:limited_gold_barrel_1",
        "sophisticatedstorage:limited_gold_barrel_2",
        "sophisticatedstorage:limited_gold_barrel_3",
        "sophisticatedstorage:limited_gold_barrel_4",
        "sophisticatedstorage:limited_iron_barrel_1",
        "sophisticatedstorage:limited_iron_barrel_2",
        "sophisticatedstorage:limited_iron_barrel_3",
        "sophisticatedstorage:limited_iron_barrel_4",
        "sophisticatedstorage:limited_netherite_barrel_1",
        "sophisticatedstorage:limited_netherite_barrel_2",
        "sophisticatedstorage:limited_netherite_barrel_3",
        "sophisticatedstorage:limited_netherite_barrel_4",
        "sophisticatedstorage:mangrove_storage_connector",
        "sophisticatedstorage:oak_storage_connector",
        "sophisticatedstorage:spruce_storage_connector",
        "sophisticatedstorage:storage_input",
        "sophisticatedstorage:storage_io",
        "sophisticatedstorage:storage_link",
        "sophisticatedstorage:storage_output",
        "sophisticatedstorage:warped_storage_connector",
        "herbsandharvest:cake_block",
        "merrymaking:kinara",
        "merrymaking:topper",
        "supplementaries:crank",
        "supplementaries:rope_tomatoes",
        "supplementaries:stick_tomatoes",
        "securitycraft:reinforced_scaffolding",
        "amendments:forbidden_arcanus/wall_deorum_lantern",
        "amendments:forbidden_arcanus/wall_deorum_soul_lantern",
        "amendments:wall_lantern",
        "amendments:wall_soul_lantern",
        "functionalstorage:compacting_framed_drawer",
        "functionalstorage:framed_1",
        "functionalstorage:framed_2",
        "functionalstorage:framed_4",
        "functionalstorage:framed_controller_extension",
        "functionalstorage:framed_fluid_1",
        "functionalstorage:framed_fluid_2",
        "functionalstorage:framed_fluid_4",
        "functionalstorage:framed_simple_compacting_drawer",
        "functionalstorage:framed_storage_controller",
        "immersiveengineering:capacitor_creative",
        "immersiveengineering:capacitor_hv",
        "immersiveengineering:capacitor_lv",
        "immersiveengineering:capacitor_mv",
        "immersiveengineering:fluid_placer",
        "immersiveengineering:fluid_pump",
        "immersiveengineering:metal_barrel",
        "immersiveengineering:wooden_barrel",
        "little_big_redstone:black_microchip",
        "little_big_redstone:blue_microchip",
        "little_big_redstone:brown_microchip",
        "little_big_redstone:cyan_microchip",
        "little_big_redstone:gray_microchip",
        "little_big_redstone:green_microchip",
        "little_big_redstone:light_blue_microchip",
        "little_big_redstone:light_gray_microchip",
        "little_big_redstone:lime_microchip",
        "little_big_redstone:magenta_microchip",
        "little_big_redstone:orange_microchip",
        "little_big_redstone:pink_microchip",
        "little_big_redstone:purple_microchip",
        "little_big_redstone:red_microchip",
        "little_big_redstone:white_microchip",
        "little_big_redstone:yellow_microchip",
        "enderio:alloy_smelter",
        "enderio:crafter",
        "enderio:drain",
        "enderio:enchanter",
        "enderio:fluid_tank",
        "enderio:impulse_hopper",
        "enderio:niard",
        "enderio:painting_machine",
        "enderio:powered_spawner",
        "enderio:pressurized_fluid_tank",
        "enderio:sag_mill",
        "enderio:slice_and_splice",
        "enderio:soul_binder",
        "enderio:soul_engine",
        "enderio:stirling_generator",
        "enderio:vat",
        "enderio:wired_charger",
        "enderio:wireless_charger",
        "refurbished_furniture:andesite_stepping_stones",
        "refurbished_furniture:computer",
        "refurbished_furniture:dark_ceiling_light",
        "refurbished_furniture:dark_range_hood",
        "refurbished_furniture:deepslate_stepping_stones",
        "refurbished_furniture:diorite_stepping_stones",
        "refurbished_furniture:frying_pan",
        "refurbished_furniture:granite_stepping_stones",
        "refurbished_furniture:light_ceiling_light",
        "refurbished_furniture:light_range_hood",
        "refurbished_furniture:stone_stepping_stones",
        "minecolonies:blockminecoloniesrack",
        "mekanism:advanced_logistical_transporter",
        "mekanism:advanced_mechanical_pipe",
        "mekanism:advanced_pressurized_tube",
        "mekanism:advanced_thermodynamic_conductor",
        "mekanism:advanced_universal_cable",
        "mekanism:basic_logistical_transporter",
        "mekanism:basic_mechanical_pipe",
        "mekanism:basic_pressurized_tube",
        "mekanism:basic_thermodynamic_conductor",
        "mekanism:basic_universal_cable",
        "mekanism:diversion_transporter",
        "mekanism:elite_logistical_transporter",
        "mekanism:elite_mechanical_pipe",
        "mekanism:elite_pressurized_tube",
        "mekanism:elite_thermodynamic_conductor",
        "mekanism:elite_universal_cable",
        "mekanism:restrictive_transporter",
        "mekanism:ultimate_logistical_transporter",
        "mekanism:ultimate_mechanical_pipe",
        "mekanism:ultimate_pressurized_tube",
        "mekanism:ultimate_thermodynamic_conductor",
        "mekanism:ultimate_universal_cable"
    };
    for (const char* name : targets) {
        auto block = byBlock.find(name);
        if (block == byBlock.end()) continue;
        for (const auto* state : {&block->second.front(), &block->second.back()})
            if (std::find(selected.begin(), selected.end(), *state) == selected.end()) selected.push_back(*state);
    }
    for (const auto& block : byBlock) {
        int type, data;
        assert(ResolveModState(block.second.front(), &type, &data) == 1);
        if (GetModBlock(type, data)->sourceDefinition->splitPolicy == ModSplitPolicy::None &&
            block.first.compare(0, 17, "domum_ornamentum:") != 0 &&
            block.first.compare(0, 8, "morered:") != 0) continue;
        for (const auto* state : {&block.second.front(), &block.second.back()})
            if (std::find(selected.begin(), selected.end(), *state) == selected.end()) selected.push_back(*state);
    }
    for (int argument = 3; argument < argc; ++argument)
        if (std::find(selected.begin(), selected.end(), argv[argument]) == selected.end()) selected.push_back(argv[argument]);
    // A compact grid preserves small source details in the exporter's float coordinates.
    const char* wideRow = getenv("MINEWAYS_TEST_WIDE_ROW");
    const int columns = wideRow && strcmp(wideRow, "1") == 0 ? (int)selected.size() : min(32, (int)selected.size());
    const int width = columns * 2 - 1;
    const int length = (((int)selected.size() + columns - 1) / columns) * 2 - 1;
    std::vector<unsigned short> blocks(width * length, 0);
    std::vector<unsigned short> data(width * length, 0);
    std::ofstream selection(output / "selected-states.txt");
    for (size_t i = 0; i < selected.size(); ++i) {
        int type, state;
        resolveSample(selected[i], type, state);
        const size_t cell = (i % columns) * 2 + (i / columns) * 2 * width;
        blocks[cell] = (unsigned short)type;
        data[cell] = PACK_TYPE_EXT_AND_DATAVAL(type, state);
        selection << GetModBlock(type, state)->canonical << '\n';
    }
    selection.close();
    WorldGuide world = {};
    world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks.data(), data.data(), width, 1, length, width * length, false, true};
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
    return 0;
}
