// A real IE chunk-loader fragment formerly collapsed after adding a wide block offset.
static void checkModPrecision(const std::filesystem::path& root)
{
    const std::string originalPack = ModPackPath();
    const auto folder = root / "precision";
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(std::filesystem::path(originalPack).parent_path() / "textures/0000.png",
        folder / "texture.png", std::filesystem::copy_options::overwrite_existing);
    const auto manifest = folder / "manifest.mwpack";
    {
        std::ofstream out(manifest);
        out << "MINEWAYS_MOD_PACK\t3\ntexture\t0\tprecision:pattern\ttexture.png\ngeometry\t0\n"
            << "polygon\t0\t3\t4\t-0.000048\t-0.000032\t10.75\t10.5"
            << "\t4.000048\t15.999984\t-0.000192\t10.75\t8.5"
            << "\t4.000047995392386\t15.999984\t0\t10.749976002015831\t8.5\n"
            << "endgeometry\nstate\tprecision:thin\t808080\t255\nusemodel\t0\t0\t90\t0\nendstate\n";
    }
    ClearCache();
    std::string error;
    assert(LoadModPack(manifest.u8string(), error));
    int type = 0, state = 0;
    assert(ResolveModState("precision:thin[retained=true]", &type, &state) == 1);
    std::vector<unsigned short> blocks(4097), values(4097);
    blocks.front() = blocks.back() = (unsigned short)type;
    values.front() = values.back() = (unsigned short)state;
    WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
    world.sch = {blocks.data(), values.data(), 4097, 1, 1, 4097, false, true};
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    const ModPolygon& polygon = GetModBlock(type, state)->definition->models[0].Polygons()[0];
    std::vector<GeometryFace> reference;
    for (int turn : {0, 90}) for (bool mosaic : {false, true}) {
        const auto object = folder / (std::string(mosaic ? "mosaic" : "tiles") + std::to_string(turn) + ".obj");
        const double scale = turn ? .25 : 1.0;
        exportWorld(world, object, mosaic ? mode & ~EXPT_OUTPUT_SEPARATE_TEXTURE_TILES : mode,
            true, false, false, false, false, false, true, -1, turn, (float)(scale * 1000));
        const auto faces = readGeometryFaces(object, 2);
        for (const auto& face : faces) for (int corner = 1; corner < 3; ++corner) {
            double expected[3];
            const auto& a = polygon.vertices[0]; const auto& b = polygon.vertices[corner];
            expected[0] = -((double)b[Z] - a[Z]) / 16;
            expected[1] = ((double)b[Y] - a[Y]) / 16;
            expected[2] = ((double)b[X] - a[X]) / 16;
            if (turn) { const double x = expected[X]; expected[X] = -expected[Z]; expected[Z] = x; }
            for (int axis = 0; axis < 3; ++axis)
                assert(fabs((face[corner].position[axis] - face[0].position[axis]) - expected[axis] * scale) < 1e-11);
            for (int axis = 0; axis < 2; ++axis) assert(face[corner].uv[axis] == faces.front()[corner].uv[axis]);
        }
        if (!turn && !mosaic) reference = faces;
    }
    const auto stl = folder / "thin.stl";
    exportWorld(world, stl, 0, true, false, true);
    std::vector<std::array<double, 3>> stlVertices, objVertices;
    std::istringstream stlText(readText(stl)); std::string line;
    while (std::getline(stlText, line)) {
        std::istringstream fields(line); std::string tag; fields >> tag;
        if (tag == "vertex") { std::array<double, 3> p; fields >> p[0] >> p[1] >> p[2]; stlVertices.push_back(p); }
    }
    for (const auto& face : reference) for (const auto& vertex : face) objVertices.push_back(vertex.position);
    std::sort(stlVertices.begin(), stlVertices.end()); std::sort(objVertices.begin(), objVertices.end());
    assert(stlVertices == objVertices);

    // Float formats reject the wide thin face before replacing existing files.
    for (const auto& format : std::vector<std::pair<int, const char*>>{
            {FILE_TYPE_USD, "thin.usda"}, {FILE_TYPE_VRML2, "thin.wrl"},
            {FILE_TYPE_BINARY_MAGICS_STL, "binary.stl"}, {FILE_TYPE_BINARY_VISCAM_STL, "viscam.stl"}}) {
        const auto path = folder / format.second;
        const int outputMode = format.first == FILE_TYPE_USD ? mode :
            format.first == FILE_TYPE_VRML2 ? mode & ~EXPT_OUTPUT_SEPARATE_TEXTURE_TILES : 0;
        { std::ofstream out(path); out << "existing output must survive\n"; }
        exportWorld(world, path, outputMode, true, false, false, true, false, false, false, format.first);
        assert(std::string(ModExportError()).find("cannot preserve a thin face") != std::string::npos);
        // These formats still accept the same source geometry near the local origin.
        WorldGuide small = world; small.sch.width = small.sch.numBlocks = 1;
        exportWorld(small, path, outputMode, true, false, false, false, false, false, false, format.first);
        if (format.first == FILE_TYPE_BINARY_MAGICS_STL || format.first == FILE_TYPE_BINARY_VISCAM_STL)
            assert(std::filesystem::file_size(path) == 134); // 80-byte header, count, one 50-byte triangle.
    }
    // Existing USD instancing keeps the thin mesh near its local origin.
    exportWorld(world, folder / "instanced.usda", mode);
    const std::string instances = readText(folder / "instanced.usda");
    assert(instances.find("PointInstancer") != std::string::npos);
    assert(instances.find("(4096, 0, 0)") != std::string::npos);
    const std::string library = readText(folder / "instanced_materials/BlockLibrary.usda");
    const size_t pointsAt = library.find("point3f[] points = [");
    assert(pointsAt != std::string::npos);
    std::string points = library.substr(pointsAt + strlen("point3f[] points = ["));
    points.resize(points.find(']'));
    for (char& c : points) if (c == '(' || c == ')' || c == ',') c = ' ';
    std::istringstream pointValues(points);
    double local[3][3], a[3], b[3], cross[3];
    for (auto& vertex : local) for (double& value : vertex) assert(pointValues >> value);
    for (int axis = 0; axis < 3; ++axis) {
        a[axis] = local[1][axis] - local[0][axis];
        b[axis] = local[2][axis] - local[0][axis];
    }
    VecCross(cross, =, a, X, b);
    assert(VecDot(cross, cross) > 1e-11); // Prototype retains the actual thin triangle.

    const auto schematic = folder / "thin.schem";
    exportWorld(world, schematic, 0);
    WorldGuide restored = readSchematic(schematic);
    assert(stateNames(restored) == stateNames(world));
    exportWorld(restored, folder / "restored.obj", mode);
    assert(readGeometryFaces(folder / "restored.obj", 2).size() == 2);
    free(restored.sch.blocks); free(restored.sch.data);
    // Adjacent starts exercise both an odd world position and an odd world-to-box offset.
    for (const int origin : {20000001, 20000002}) {
        std::vector<unsigned short> distantBlocks((size_t)origin + 2), distantValues((size_t)origin + 2);
        distantBlocks[origin] = distantBlocks[origin + 1] = (unsigned short)type;
        distantValues[origin] = distantValues[origin + 1] = (unsigned short)state;
        WorldGuide distant = {}; distant.type = WORLD_SCHEMATIC_TYPE;
        distant.sch = {distantBlocks.data(), distantValues.data(), origin + 2, 1, 1, origin + 2, false, true};
        std::vector<GeometryFace> distantFaces;
        for (int turn : {0, 90}) {
            const auto path = folder / ("global" + std::to_string(turn) + ".obj");
            const double scale = turn ? .25 : 1;
            exportWorld(distant, path, mode, true, false, false, false, false, false, false, -1, turn, (float)(scale * 1000), origin);
            const auto faces = readGeometryFaces(path, 2);
            for (size_t f = 0; f < faces.size(); ++f) for (int corner = 0; corner < 3; ++corner) {
                const auto& v = polygon.vertices[corner];
                double expected[3] = {origin + (double)f + (16.0 - v[Z]) / 16.0, (double)v[Y] / 16.0, (double)v[X] / 16.0};
                if (turn) { const double x = expected[X]; expected[X] = -expected[Z]; expected[Z] = x; }
                for (int axis = 0; axis < 3; ++axis) assert(fabs(faces[f][corner].position[axis] - expected[axis] * scale) < 1e-8);
            }
            if (!turn) distantFaces = faces;
        }
        const auto distantSTL = folder / "global.stl";
        exportWorld(distant, distantSTL, 0, true, false, true, false, false, false, false,
            FILE_TYPE_ASCII_STL, 0, 1000, origin);
        std::vector<std::array<double, 3>> distantSTLVertices, distantOBJVertices;
        std::istringstream distantSTLText(readText(distantSTL));
        while (std::getline(distantSTLText, line)) {
            std::istringstream fields(line); std::string tag; fields >> tag;
            if (tag == "vertex") { std::array<double, 3> p; assert(fields >> p[0] >> p[1] >> p[2]); distantSTLVertices.push_back(p); }
        }
        for (const auto& face : distantFaces) for (const auto& vertex : face) distantOBJVertices.push_back(vertex.position);
        std::sort(distantSTLVertices.begin(), distantSTLVertices.end());
        std::sort(distantOBJVertices.begin(), distantOBJVertices.end());
        assert(distantSTLVertices == distantOBJVertices);
        for (const auto& format : std::vector<std::pair<int, const char*>>{
                {FILE_TYPE_USD, "global.usda"}, {FILE_TYPE_VRML2, "global.wrl"},
                {FILE_TYPE_BINARY_MAGICS_STL, "global-binary.stl"}, {FILE_TYPE_BINARY_VISCAM_STL, "global-viscam.stl"}}) {
            const auto path = folder / format.second;
            { std::ofstream out(path); out << "keep existing global output\n"; }
            exportWorld(distant, path, 0, true, false, false, true, false, false, false,
                format.first, 0, 1000, origin);
            assert(std::string(ModExportError()).find("cannot preserve a thin face") != std::string::npos);
        }
        const auto distantInstances = folder / "global-instances.usda";
        { std::ofstream out(distantInstances); out << "keep exact instance positions\n"; }
        exportWorld(distant, distantInstances, mode, true, false, false, true, false, false, true,
            FILE_TYPE_USD, 0, 1000, origin);
        assert(std::string(ModExportError()).find("instance positions cannot preserve") != std::string::npos);
    }
    ClearCache(); assert(LoadModPack(originalPack, error));
}
