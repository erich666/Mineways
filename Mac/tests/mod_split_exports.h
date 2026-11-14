// Saved IE offsets select different model fragments without changing palette identity.
static void checkSplitModExports(const std::filesystem::path& root)
{
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    for (const char* fixture : {"fixture:ie_split", "fixture:ie_split_position"}) {
        int type, base;
        const std::string canonical = std::string(fixture) + "[retained=true]";
        assert(ResolveModState(canonical, &type, &base) == 1);
        assert(GetModBlock(type, base)->contextMissing);
        const auto& source = *GetModBlock(type, base)->sourceDefinition;
        const bool positionPolicy = source.splitPolicy == ModSplitPolicy::Position;
        const std::string label = positionPolicy ? "ie-position" : "ie-dummy";
        unsigned short blocks[3] = {(unsigned short)type, (unsigned short)type, (unsigned short)type};
        unsigned short data[3];
        for (int y = 0; y < 3; ++y) {
            ModBlockContext context;
            context.entityId = source.masterEntityId;
            if (positionPolicy) {
                context.entityId.replace(context.entityId.size() - 7, 7, "_dummy");
                context.hasPosition = true;
                for (int axis = 0; axis < 3; ++axis) context.position[axis] = source.master[axis];
                context.position[1] += y;
            } else {
                context.hasDummy = true;
                context.dummy = y;
            }
            int resolved;
            assert(ResolveModContext(type, base, context, &resolved) == 1);
            data[y] = (unsigned short)resolved;
            assert(GetModBlock(type, resolved)->canonical == canonical);
        }
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {blocks, data, 1, 3, 1, 3, false, true};
        world.maxHeight = 2;
        const auto obj = root / (label + ".obj");
        exportWorld(world, obj, mode);
        const auto faces = readGeometryFaces(obj, 3);
        double area = 0;
        for (const auto& face : faces) {
            assert(face.size() == 4);
            for (const auto& vertex : face) {
                assert(fabs(vertex.position[2] - .25) < 1e-6);
                assert(fabs(vertex.uv[0] - (vertex.position[0] - .125) / .75) < 1e-5);
                assert(fabs(vertex.uv[1] - vertex.position[1] / 3) < 1e-5);
            }
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                const auto& a = face[0].position;
                const auto& b = face[i].position;
                const auto& c = face[i + 1].position;
                area += fabs((b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])) / 2;
            }
        }
        assert(fabs(area - 2.25) < 1e-6);
        exportWorld(world, root / (label + ".usda"), mode);
        const auto schematic = root / (label + ".schem");
        exportWorld(world, schematic, 0);
        WorldGuide restored = readSchematic(schematic);
        assert(stateNames(restored) == stateNames(world));
        for (int y = 0; y < 3; ++y) {
            const auto& expected = GetModBlock(type, data[y])->context;
            const auto* state = GetModBlock(restored.sch.blocks[y], restored.sch.data[y]);
            assert(state && !state->contextMissing);
            const auto& actual = state->context;
            assert(actual.entityId == expected.entityId && actual.hasDummy == expected.hasDummy && actual.dummy == expected.dummy);
            assert(actual.hasPosition == expected.hasPosition);
            for (int axis = 0; axis < 3; ++axis) assert(actual.position[axis] == expected.position[axis]);
        }
        const auto after = root / (label + "-restored.obj");
        exportWorld(restored, after, mode);
        const auto roundtrip = readGeometryFaces(after, 3);
        for (size_t f = 0; f < faces.size(); ++f) for (size_t v = 0; v < faces[f].size(); ++v) {
            assert(faces[f][v].position == roundtrip[f][v].position);
            assert(faces[f][v].uv == roundtrip[f][v].uv);
        }
        ClearCache();
        free(restored.sch.blocks); free(restored.sch.data);
        if (!positionPolicy) {
            const int count = 257 * 256;
            std::vector<unsigned short> manyBlocks(count, (unsigned short)type), manyData(count, data[0]);
            WorldGuide large = {}; large.type = WORLD_SCHEMATIC_TYPE;
            large.sch = {manyBlocks.data(), manyData.data(), 257, 1, 256, count, false, true};
            const auto target = root / "ie-context-limit.schem";
            std::ofstream(target) << "existing schematic must survive";
            exportWorld(large, target, 0, true, false, false, true);
        }
        unsigned short missing = (unsigned short)base;
        world.sch = {blocks, &missing, 1, 1, 1, 1, false, true};
        world.maxHeight = 0;
        exportWorld(world, root / (label + "-missing.obj"), mode, true, false, false, true);
        assert(std::string(ModExportError()).find("needs saved block-entity data") != std::string::npos);
        exportWorld(world, root / (label + "-missing.schem"), 0);
        assert(!*ModExportError());
    }
}
