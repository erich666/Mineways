// Original OBJ mesh: a sloped triangle and a separate quad, with per-corner UVs.
static void checkModPolygons(const std::filesystem::path& root)
{
    const int modes[] = {EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES,
                         EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES,
                         EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES};
    std::vector<GeometryFace> reference;
    for (int turned = 0; turned < 2; ++turned) {
        int type, state;
        assert(ResolveModState(std::string("fixture:obj_mesh[orientation=") + (turned ? "turned" : "plain") + ",retained=true]", &type, &state) == 1);
        const auto* definition = GetModBlock(type, state)->definition;
        assert(definition->models.size() == 1 && definition->models[0].Polygons().size() == 2);
        unsigned short block = (unsigned short)type, value = (unsigned short)state;
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {&block, &value, 1, 1, 1, 1, false, true};
        for (int mode = 0; mode < 3; ++mode) {
            const auto path = root / ("polygon-" + std::to_string(turned) + "-" + std::to_string(mode) + ".obj");
            exportWorld(world, path, modes[mode]);
            auto faces = readGeometryFaces(path, 2);
            assert(faces[0].size() == 3 && faces[1].size() == 4);
            if (mode == 0) {
                if (!turned) reference = faces;
                for (size_t f = 0; f < faces.size(); ++f) for (const auto& vertex : faces[f]) {
                    const GeometryVertex* original = nullptr;
                    for (const auto& candidate : reference[f])
                        if (fabs(vertex.uv[0]-candidate.uv[0]) < 1e-6 && fabs(vertex.uv[1]-candidate.uv[1]) < 1e-6) original = &candidate;
                    assert(original);
                    double normalDot = 0;
                    for (int axis = 0; axis < 3; ++axis) {
                        const int from = turned ? (axis+1)%3 : axis;
                        assert(fabs(vertex.position[axis] - original->position[from]) < 1e-6);
                        normalDot += vertex.normal[axis] * original->normal[from];
                    }
                    // The existing resolver reuses normal directions within a dot-product tolerance.
                    assert(normalDot > .999);
                }
                // The explicit source triangle UV corners survive flip_v and exporter v conversion.
                std::set<std::array<double, 2>> uv;
                for (const auto& vertex : faces[0]) uv.insert(vertex.uv);
                const std::set<std::array<double, 2>> expected = {{{.125, .25}}, {{.875, .25}}, {{.5, .75}}};
                assert(uv == expected);
            }
        }
        exportWorld(world, root / ("polygon-" + std::to_string(turned) + ".usda"), modes[0]);
        const auto schematic = root / ("polygon-" + std::to_string(turned) + ".schem");
        exportWorld(world, schematic, 0);
        WorldGuide restored = readSchematic(schematic);
        assert(stateNames(restored) == stateNames(world));
        free(restored.sch.blocks); free(restored.sch.data);
    }
    const std::string originalPack = ModPackPath();
    // Invalid v3 geometry must fail without replacing the loaded registry.
    const auto path = root / "bad-polygon.mwpack";
    const std::string texture = std::filesystem::path(ModPackPath()).parent_path().u8string() + "/textures/0000.png";
    std::filesystem::copy_file(texture, root / "polygon-validation.png", std::filesystem::copy_options::overwrite_existing);
    const std::string prefix = "MINEWAYS_MOD_PACK\t3\ntexture\t0\tfixture:validation\tpolygon-validation.png\ngeometry\t0\n";
    for (const std::string& record : {
        "polygon\t0\t2\t0\t0\t0\t0\t0\t1\t0\t0\t1\t0",
        "polygon\t0\t3\t0\t0\t0\t0\t0\t0\t0\t0\t1\t0\t0\t0\t0\t0\t1",
        "polygon\t0\t3\t257\t0\t0\t0\t0\t1\t0\t0\t1\t0\t0\t1\t0\t0\t1",
        "polygon\t0\t3\t0\t0\t0\t17\t0\t1\t0\t0\t1\t0\t0\t1\t0\t0\t1",
        "polygon\t1\t3\t0\t0\t0\t0\t0\t1\t0\t0\t1\t0\t0\t1\t0\t0\t1"}) {
        { std::ofstream out(path); out << prefix << record << "\nendgeometry\nstate\tfixture:bad\t808080\t255\nusemodel\t0\t0\t0\t0\nendstate\n"; }
        std::string error;
        assert(!LoadModPack(path.u8string(), error) && HasModBlock("fixture:obj_mesh"));
    }
    // Distinct source vertices can collapse after conversion to float world coordinates.
    // This must report an export error before the normal resolver sees zero area.
    {
        std::ofstream out(path);
        out << prefix << "polygon\t0\t3\t0\t0\t0\t0\t0\t0.0002\t0\t0\t16\t0\t0\t0.0002\t0\t0\t16\n"
            << "endgeometry\nstate\tfixture:tiny\t808080\t255\nusemodel\t0\t0\t0\t0\nendstate\n";
    }
    std::string error;
    ClearCache();
    assert(LoadModPack(path.u8string(), error));
    int type, state; assert(ResolveModState("fixture:tiny", &type, &state) == 1);
    std::vector<unsigned short> blocks(1025, 0), values(1025, 0);
    blocks.front() = blocks.back() = (unsigned short)type;
    values.front() = values.back() = (unsigned short)state;
    WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks.data(), values.data(), 1025, 1, 1, 1025, false, true};
    exportWorld(world, root / "collapsed-polygon.obj", modes[0], true, false, false, true);
    // Expanded UV clipping must also reject a wholly collapsed source face,
    // rather than silently omit the block after skipping zero-area clip slivers.
    {
        std::ofstream out(path);
        out << "MINEWAYS_MOD_PACK\t5\ntexture\t0\tfixture:validation\tpolygon-validation.png\ngeometry\t0\n"
            << "polygon\t0\t3\t0\t0\t0\t-8\t-8\t0.0002\t0\t0\t24\t-8\t0\t0.0002\t0\t-8\t24\n"
            << "endgeometry\nstate\tfixture:tiny\t808080\t255\nusemodel\t0\t0\t0\t0\nendstate\n";
    }
    assert(LoadModPack(path.u8string(),error));
    assert(ResolveModState("fixture:tiny",&type,&state)==1);
    blocks.front()=blocks.back()=(unsigned short)type; values.front()=values.back()=(unsigned short)state;
    exportWorld(world,root/"collapsed-repeated-polygon.obj",modes[0],true,false,false,true);
    ClearCache();
    assert(LoadModPack(originalPack, error));

}
