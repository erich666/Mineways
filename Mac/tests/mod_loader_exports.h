// Supplied default material models and disconnected transmitter meshes survive the pack boundary.
static void checkDefaultLoaderExports(const std::filesystem::path& root)
{
    struct Sample { const char* state; size_t faces; bool transmitter; };
    const Sample samples[] = {
        {"fixture:material_block[facing=north]", 2, false},
        {"fixture:material_block[facing=east]", 2, false},
        {"fixture:material_block[facing=up]", 0, false},
        {"fixture:material_multipart[connected=false]", 2, false},
        {"fixture:material_multipart[connected=true]", 4, false},
        {"fixture:transmitter[facing=north]", 6, true},
        {"fixture:transmitter[facing=east]", 6, true},
    };
    const int mode = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES;
    for (size_t index = 0; index < sizeof(samples)/sizeof(samples[0]); ++index) {
        const Sample& sample = samples[index];
        int type, state;
        assert(ResolveModState(sample.state, &type, &state) == 1);
        unsigned short block = (unsigned short)type, value = (unsigned short)state;
        WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
        world.sch = {&block, &value, 1, 1, 1, 1, false, true};
        const std::string label = "loader-" + std::to_string(index);
        const unsigned char color[4] = {sample.transmitter ? (unsigned char)90 : (unsigned char)160,
            sample.transmitter ? (unsigned char)120 : (unsigned char)65,
            sample.transmitter ? (unsigned char)15 : (unsigned char)195, 255};
        for (bool individual : {true, false}) {
            const auto object = root / (label + (individual ? "-tiles.obj" : "-mosaic.obj"));
            exportWorld(world, object, mode | (individual ? EXPT_OUTPUT_SEPARATE_TEXTURE_TILES : 0),
                true, false, false, false, sample.faces == 0);
            if (!sample.faces) continue;
            const auto faces = readGeometryFaces(object, sample.faces);
            for (const auto& face : faces) {
                assert(face.size() == 4);
                if (sample.transmitter) {
                    std::array<double, 3> a, b, cross;
                    for (int axis = 0; axis < 3; ++axis) {
                        a[axis] = face[1].position[axis] - face[0].position[axis];
                        b[axis] = face[2].position[axis] - face[0].position[axis];
                        for (const auto& vertex : face) assert(vertex.position[axis] == .25 || vertex.position[axis] == .75);
                    }
                    VecCross(cross, =, a, X, b);
                    double outward = 0;
                    for (int axis = 0; axis < 3; ++axis) outward += cross[axis] * (face[0].position[axis] - .5);
                    assert(outward > 0);
                }
            }
            std::filesystem::path material = object; material.replace_extension(".mtl");
            std::set<std::string> maps;
            std::istringstream text(readText(material)); std::string line;
            while (std::getline(text, line)) if (line.compare(0, 7, "map_Kd ") == 0) maps.insert(line.substr(7));
            assert(maps.size() == 1); // No outer-wrapper override, connection overlay, or hidden group texture.
            std::vector<unsigned char> pixels; unsigned width, height;
            assert(lodepng::decode(pixels, width, height, wide(root / *maps.begin())) == 0);
            if (individual) {
                for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
                    for (int channel = 0; channel < 4; ++channel) assert(pixels[pixel + channel] == color[channel]);
            } else for (const auto& face : faces) {
                double u = 0, v = 0;
                for (const auto& vertex : face) { u += vertex.uv[0]/face.size(); v += vertex.uv[1]/face.size(); }
                const unsigned x = (unsigned)(u * width), y = (unsigned)((1-v) * height);
                assert(x < width && y < height);
                for (int channel = 0; channel < 4; ++channel) assert(pixels[((size_t)y * width + x) * 4 + channel] == color[channel]);
            }
        }
        const auto schematic = root / (label + ".schem");
        exportWorld(world, schematic, 0);
        WorldGuide restored = readSchematic(schematic);
        assert(stateNames(restored) == stateNames(world));
        free(restored.sch.blocks); free(restored.sch.data);
    }
}
