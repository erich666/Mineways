// AE2 applies negative right-handed Z, X, then Y quarter turns about the block center.
using RotationPoint = std::array<double, 3>;
static RotationPoint turnModPoint(RotationPoint p, int x, int y, int z, bool position)
{
    if (position) for (double& value : p) value -= .5;
    const double pi = 3.14159265358979323846;
    for (const auto& rotation : {std::pair<int,int>{2,z}, {0,x}, {1,y}}) {
        const int a = (rotation.first + 1) % 3, b = (rotation.first + 2) % 3;
        const double c = cos(rotation.second * pi / 180), s = sin(rotation.second * pi / 180);
        const double first = p[a];
        p[a] = c * first + s * p[b]; p[b] = -s * first + c * p[b];
    }
    if (position) for (double& value : p) value += .5;
    return p;
}
static std::array<double, 2> lockedModUV(const GeometryVertex& vertex, int x, int y, int z)
{
    // Face coordinate bases, independent of the vertex positions or box dimensions.
    const RotationPoint normals[] = {{{-1,0,0}}, {{0,-1,0}}, {{0,0,-1}}, {{1,0,0}}, {{0,1,0}}, {{0,0,1}}};
    const RotationPoint origins[] = {{{0,1,0}}, {{0,0,1}}, {{1,1,0}}, {{1,1,1}}, {{0,1,0}}, {{0,1,1}}};
    const RotationPoint uAxes[] = {{{0,0,1}}, {{1,0,0}}, {{-1,0,0}}, {{0,0,-1}}, {{1,0,0}}, {{1,0,0}}};
    const RotationPoint vAxes[] = {{{0,-1,0}}, {{0,0,-1}}, {{0,-1,0}}, {{0,-1,0}}, {{0,0,1}}, {{0,-1,0}}};
    auto side = [&](const RotationPoint& normal) {
        for (int i = 0; i < 6; ++i) {
            double dot = 0; for (int a = 0; a < 3; ++a) dot += normal[a] * normals[i][a];
            if (dot > .999) return i;
        }
        assert(false); return 0;
    };
    const int source = side(vertex.normal), destination = side(turnModPoint(vertex.normal, x, y, z, false));
    RotationPoint point;
    for (int a = 0; a < 3; ++a) point[a] = origins[source][a] + vertex.uv[0]*uAxes[source][a] + (1-vertex.uv[1])*vAxes[source][a];
    point = turnModPoint(point, x, y, z, true);
    double u = 0, v = 0;
    for (int a = 0; a < 3; ++a) { u += (point[a]-origins[destination][a])*uAxes[destination][a]; v += (point[a]-origins[destination][a])*vAxes[destination][a]; }
    return {u, 1-v};
}
static void checkModRotations(const std::filesystem::path& root)
{
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    auto exportState = [&](const std::string& model, int x, int y, int z, bool lock) {
        const std::string selector = "fixture:" + model + "[locked=" + (lock ? "true" : "false") + ",x=" + std::to_string(x) + ",y=" + std::to_string(y) + ",z=" + std::to_string(z) + "]";
        int type, state; assert(ResolveModState(selector, &type, &state) == 1);
        unsigned short block = (unsigned short)type, data = (unsigned short)state;
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {&block, &data, 1, 1, 1, 1, false, true};
        const std::string label = model + "-" + std::to_string(x) + "-" + std::to_string(y) + "-" + std::to_string(z) + "-" + std::to_string(lock);
        const auto path = root / (label + ".obj");
        exportWorld(world, path, mode);
        if (z == 90 && x == 0 && y == 0) {
            const auto schematic = root / (label + ".schem");
            exportWorld(world, schematic, 0);
            WorldGuide restored = readSchematic(schematic);
            assert(stateNames(restored) == stateNames(world));
            free(restored.sch.blocks); free(restored.sch.data);
            exportWorld(world, root / (label + ".usda"), mode);
            exportWorld(world, root / (label + "-mosaic.obj"), EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES);
        }
        return readGeometryFaces(path, model == "ae2_polygon" ? 2 : 6);
    };
    // A source east face becomes a bottom face under Z90. The east neighbor must
    // not incorrectly hide it through the legacy Y-only culling calculation.
    int cullType, cullState; assert(ResolveModState("fixture:ae2_cull", &cullType, &cullState) == 1);
    unsigned short adjacentBlocks[] = {(unsigned short)cullType, BLOCK_STONE};
    unsigned short adjacentData[] = {(unsigned short)cullState, 0};
    WorldGuide adjacent = {}; adjacent.type = WORLD_SCHEMATIC_TYPE;
    adjacent.sch = {adjacentBlocks, adjacentData, 2, 1, 1, 2, false, true};
    const auto neighborPath = root / "ae2-neighbor.obj";
    exportWorld(adjacent, neighborPath, mode, true, false, false, false, false, false, false);
    std::istringstream neighborInput(readText(neighborPath));
    std::string line; bool imported = false; int importedFaces = 0;
    while (std::getline(neighborInput, line)) {
        if (line.rfind("usemtl ",0) == 0) imported = line.rfind("usemtl mw_mod_",0) == 0;
        if (imported && line.rfind("f ",0) == 0) ++importedFaces;
    }
    assert(importedFaces == 6);
    for (const std::string& model : {"ae2_rotation", "ae2_local", "ae2_polygon"}) {
        const auto original = exportState(model, 0, 0, 0, false);
        std::set<std::array<int, 9>> orientations;
        for (int x : {0,90,180,270}) for (int y : {0,90,180,270}) for (int z : {0,90,180,270}) {
            std::array<int,9> matrix = {};
            for (int a = 0; a < 3; ++a) {
                RotationPoint axis = {}; axis[a] = 1;
                axis = turnModPoint(axis,x,y,z,false);
                for (int b = 0; b < 3; ++b) matrix[3*a+b] = (int)lround(axis[b]);
            }
            if (!orientations.insert(matrix).second) continue;
            for (bool locked : {false,true}) {
                if (locked && model != "ae2_rotation") continue;
                const auto actual = exportState(model,x,y,z,locked);
                for (size_t f = 0; f < original.size(); ++f) for (const auto& vertex : original[f]) {
                    const auto expected = turnModPoint(vertex.position,x,y,z,true);
                    const auto normal = turnModPoint(vertex.normal,x,y,z,false);
                    const auto uv = locked ? lockedModUV(vertex,x,y,z) : vertex.uv;
                    const GeometryVertex* match = nullptr;
                    for (const auto& candidate : actual[f]) {
                        double difference = 0;
                        for (int a = 0; a < 3; ++a) difference += fabs(candidate.position[a]-expected[a]);
                        if (difference < 3e-6) match = &candidate;
                    }
                    assert(match);
                    double dot = 0; for (int a = 0; a < 3; ++a) dot += match->normal[a]*normal[a];
                    assert(dot > .999);
                    assert(fabs(match->uv[0]-uv[0]) < 1e-6 && fabs(match->uv[1]-uv[1]) < 1e-6);
                }
            }
        }
        assert(orientations.size() == 24);
    }
}
