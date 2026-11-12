// More distinct directions than the former fixed 456-normal table can hold.
static void checkModNormalCapacity(const std::filesystem::path& root)
{
    const std::string original = ModPackPath();
    const auto manifest = root / "many-normals.mwpack";
    std::vector<std::array<double, 3>> expected;
    {
        std::ofstream out(manifest);
        out.precision(12);
        out << "MINEWAYS_MOD_PACK\t3\ntexture\t0\tfixture:normals\tpolygon-validation.png\ngeometry\t0\n";
        for (int i = 0; i < 800; ++i) {
            // Fibonacci sphere: well-separated directions, avoiding near-normal matching.
            const double z = 1.0 - 2.0 * (i + 0.5) / 800.0;
            const double angle = i * 2.399963229728653;
            const double radius = sqrt(1.0 - z*z);
            const std::array<double, 3> n = {{radius*cos(angle), radius*sin(angle), z}};
            const std::array<double, 3> a = {{-sin(angle), cos(angle), 0}};
            const std::array<double, 3> b = {{-z*cos(angle), -z*sin(angle), radius}};
            expected.push_back(n);
            out << "polygon\t0\t3\t8\t8\t8\t0\t0";
            for (const auto& v : {a, b}) {
                for (double coordinate : v) out << '\t' << 8 + coordinate * 4;
                out << "\t16\t16";
            }
            out << '\n';
        }
        out << "endgeometry\nstate\tfixture:many_normals\t808080\t255\nusemodel\t0\t0\t0\t0\nendstate\n";
        assert(out.good());
    }
    ClearCache();
    std::string error;
    assert(LoadModPack(manifest.u8string(), error));
    int type, state;
    assert(ResolveModState("fixture:many_normals", &type, &state) == 1);
    unsigned short block = (unsigned short)type, value = (unsigned short)state;
    WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {&block, &value, 1, 1, 1, 1, false, true};
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto output = root / ("many-normals-" + std::to_string(repeat) + ".obj");
        exportWorld(world, output, EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_SWATCHES);
        assert(countLines(readText(output), "vn ") > 456);
        const auto faces = readGeometryFaces(output, 800);
        for (size_t i = 0; i < faces.size(); ++i) {
            assert(faces[i].size() == 3);
            double dot = 0;
            for (int axis = 0; axis < 3; ++axis) dot += faces[i][0].normal[axis] * expected[i][axis];
            assert(dot > .999);
        }
    }
    ClearCache();
    assert(LoadModPack(original, error));
}
