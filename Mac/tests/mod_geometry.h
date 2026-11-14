// Synthetic signed-extents models exercise the complete importer-pack export contract.
struct GeometryVertex {
    std::array<double, 3> position, normal;
    std::array<double, 2> uv;
};
using GeometryFace = std::vector<GeometryVertex>;
static std::vector<GeometryFace> readGeometryFaces(const std::filesystem::path& path, size_t expectedFaces = 6)
{
    std::vector<std::array<double, 3>> positions, normals;
    std::vector<std::array<double, 2>> uvs;
    std::vector<GeometryFace> faces;
    std::istringstream input(readText(path));
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line); std::string kind; fields >> kind;
        if (kind == "v" || kind == "vn") {
            std::array<double, 3> value; fields >> value[0] >> value[1] >> value[2];
            for (double component : value) assert(std::isfinite(component));
            (kind == "v" ? positions : normals).push_back(value);
        } else if (kind == "vt") {
            std::array<double, 2> value; fields >> value[0] >> value[1]; uvs.push_back(value);
        } else if (kind == "f") {
            GeometryFace face; std::string item;
            while (fields >> item) {
                int p, t, n; assert(sscanf(item.c_str(), "%d/%d/%d", &p, &t, &n) == 3);
                assert(p > 0 && (size_t)p <= positions.size() && t > 0 && (size_t)t <= uvs.size() && n > 0 && (size_t)n <= normals.size());
                face.push_back({positions[p - 1], normals[n - 1], uvs[t - 1]});
            }
            assert(face.size() == 3 || face.size() == 4);
            std::array<double, 3> a, b, cross;
            for (int axis = 0; axis < 3; ++axis) {
                a[axis] = face[1].position[axis] - face[0].position[axis];
                b[axis] = face[2].position[axis] - face[0].position[axis];
            }
            cross = {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
            double dot = 0;
            for (int axis = 0; axis < 3; ++axis) dot += cross[axis] * face[0].normal[axis];
            assert(dot > 0); // Exported normals must agree with actual signed vertex winding.
            faces.push_back(face);
        }
    }
    assert(faces.size() == expectedFaces);
    return faces;
}

static void checkExtendedModGeometry(const std::filesystem::path& root)
{
    const auto folder = root / "extended-geometry";
    std::filesystem::create_directories(folder);
    std::vector<unsigned char> pixels;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        pixels.insert(pixels.end(), {(unsigned char)(x * 16), (unsigned char)(y * 16), 73, 255});
    assert(lodepng::encode(wide(folder / "texture.png"), pixels, 16, 16) == 0);
    const char* directions[] = {"down", "up", "north", "south", "west", "east"};
    auto makeState = [&](const std::string& name, const std::array<int, 3>& start, const std::array<int, 3>& end) {
        std::ostringstream out;
        out << "state\tgeometry:" << name << "\t808080\t255\nmodel\t0\t0\t0\nelement";
        for (int v : start) out << '\t' << v;
        for (int v : end) out << '\t' << v;
        out << "\tnone\t0\t-144\t8\t8\t0\n";
        for (int side = 0; side < 6; ++side)
            out << "face\t" << directions[side] << "\t1\t2\t14\t15\t" << side % 4 * 90 << "\t0\t" << directions[side] << '\n';
        return out.str() + "endstate\n";
    };
    const std::string prefix = "MINEWAYS_MOD_PACK\t2\ntexture\t0\tgeometry:pattern\ttexture.png\n";
    std::string manifest = prefix;
    for (int mask = 0; mask < 8; ++mask) {
        std::array<int, 3> start, end;
        for (int axis = 0; axis < 3; ++axis) { start[axis] = mask & (1 << axis) ? 14 : 2; end[axis] = mask & (1 << axis) ? 2 : 14; }
        manifest += makeState("signed_" + std::to_string(mask), start, end);
    }
    manifest += makeState("extended", {-256, -48, -32}, {256, 40, 32});
    // A 64-element model appends 512 vertices, exceeding the old transform heuristic of 400.
    manifest += "state\tgeometry:complex\t808080\t255\nmodel\t0\t90\t0\n";
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        std::ostringstream element;
        element << "element\t" << 2*x << '\t' << 2*y << "\t2\t" << 2*x+1 << '\t' << 2*y+1 << "\t4\tnone\t0\t8\t8\t8\t0\n";
        for (const char* side : directions) element << "face\t" << side << "\t1\t2\t14\t15\t0\t0\t-\n";
        manifest += element.str();
    }
    manifest += "endstate\n";
    const auto path = folder / "manifest.mwpack";
    { std::ofstream out(path); out << manifest; }
    std::string error;
    assert(LoadModPack(path.u8string(), error));
    std::vector<GeometryFace> reference;
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES | EXPT_OUTPUT_SEPARATE_TEXTURE_TILES;
    for (int mask = 0; mask < 9; ++mask) {
        const std::string name = mask == 8 ? "geometry:extended" : "geometry:signed_" + std::to_string(mask);
        int type, state; assert(ResolveModState(name + "[retained=true]", &type, &state) == 1);
        unsigned short block = (unsigned short)type, value = (unsigned short)state;
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {&block, &value, 1, 1, 1, 1, false, true};
        const auto object = folder / ("model_" + std::to_string(mask) + ".obj");
        exportWorld(world, object, mode);
        auto faces = readGeometryFaces(object);
        if (!mask) reference = faces;
        else if (mask < 8) {
            int parity = 1; for (int axis = 0; axis < 3; ++axis) if (mask & (1 << axis)) parity = -parity;
            for (size_t f = 0; f < faces.size(); ++f) for (const auto& vertex : faces[f]) {
                // The writer may cyclically reorder a face. Match its UV corner, not its array index.
                const GeometryVertex* original = nullptr;
                for (const auto& candidate : reference[f])
                    if (fabs(vertex.uv[0] - candidate.uv[0]) < 1e-6 && fabs(vertex.uv[1] - candidate.uv[1]) < 1e-6) original = &candidate;
                assert(original);
                for (int axis = 0; axis < 3; ++axis) {
                    const bool reflected = (mask & (1 << axis)) != 0;
                    assert(fabs(vertex.position[axis] - (reflected ? 1 - original->position[axis] : original->position[axis])) < 1e-6);
                    assert(fabs(vertex.normal[axis] - original->normal[axis] * parity * (reflected ? -1 : 1)) < 1e-6);
                }
            }
        } else {
            double minX = 1e6, maxX = -1e6;
            for (const auto& face : faces) for (const auto& vertex : face) { minX = (std::min)(minX, vertex.position[0]); maxX = (std::max)(maxX, vertex.position[0]); }
            assert(minX == -16 && maxX == 16);
        }
        const auto schematic = folder / ("model_" + std::to_string(mask) + ".schem");
        exportWorld(world, schematic, 0);
        WorldGuide restored = readSchematic(schematic);
        assert(stateNames(restored) == stateNames(world));
        free(restored.sch.blocks); free(restored.sch.data);
    }
    {
        int type, state; assert(ResolveModState("geometry:complex", &type, &state) == 1);
        unsigned short block = (unsigned short)type, value = (unsigned short)state;
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {&block, &value, 1, 1, 1, 1, false, true};
        const auto object = folder / "complex.obj";
        exportWorld(world, object, mode);
        const auto faces = readGeometryFaces(object, 384);
        std::set<std::array<double, 3>> actual, expected;
        for (const auto& face : faces) for (const auto& vertex : face) actual.insert(vertex.position);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            for (int corner = 0; corner < 8; ++corner)
                expected.insert({(16.0 - (corner & 1 ? 4 : 2)) / 16.0,
                    (2.0*y + (corner & 2 ? 1 : 0)) / 16.0,
                    (2.0*x + (corner & 4 ? 1 : 0)) / 16.0});
        assert(actual == expected && actual.size() == 512);
        const auto schematic = folder / "complex.schem";
        exportWorld(world, schematic, 0);
        WorldGuide restored = readSchematic(schematic);
        assert(stateNames(restored) == stateNames(world));
        free(restored.sch.blocks); free(restored.sch.data);
    }
    for (int bound : {-257, 257}) {
        { std::ofstream out(path); out << prefix << makeState("bad", {bound, 0, 0}, {16, 16, 16}); }
        assert(!LoadModPack(path.u8string(), error) && HasModBlock("geometry:extended"));
    }
    ClearCache(); ClearModPack();
}
