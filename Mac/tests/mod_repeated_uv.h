static void checkRepeatedModUV(const std::filesystem::path& root)
{
    const int textured = EXPT_OUTPUT_MATERIALS | EXPT_OUTPUT_TEXTURE_IMAGES;
    size_t pattern = 0;
    while (pattern < ModTextures().size() && ModTextures()[pattern].name != "fixture:block/obj_pattern") ++pattern;
    assert(pattern < ModTextures().size());
    for (const std::string& name : {"uv_repeat", "uv_reversed", "uv_constant", "uv_integer", "uv_point", "uv_local", "uv_polygon"}) {
        const bool polygon = name == "uv_polygon";
        for (bool turned : {false,true}) {
            if (polygon && turned) continue;
            int type, state;
            const std::string selector = "fixture:" + name + (polygon ? "" : std::string("[orientation=") + (turned ? "turned]" : "plain]"));
            assert(ResolveModState(selector,&type,&state) == 1);
            unsigned short block = (unsigned short)type, data = (unsigned short)state;
            WorldGuide world = {}; world.type = WORLD_SCHEMATIC_TYPE;
            world.sch = {&block,&data,1,1,1,1,false,true};
            const std::string label = name + (turned ? "-turned" : "-plain");
            for (bool mosaic : {false,true}) {
                const auto path = root / (label + (mosaic ? "-mosaic.obj" : ".obj"));
                exportWorld(world,path,textured | (mosaic ? 0 : EXPT_OUTPUT_SEPARATE_TEXTURE_TILES));
                const auto faces = readGeometryFaces(path,(size_t)countLines(readText(path),"f "));
                assert(faces.size() > (polygon ? 1 : 6));
                const auto imagePath = mosaic ? root / (path.stem().string()+"-RGBA.png") : root / "tex" / ("mw_mod_"+std::to_string(pattern)+".png");
                std::vector<unsigned char> pixels; unsigned width,height;
                assert(lodepng::decode(pixels,width,height,wide(imagePath)) == 0);
                if (!mosaic) assert(width == 16 && height == 16); // Repetition retains the original texel density.
                double area = 0; int checked = 0;
                for (const auto& face : faces) {
                    for (size_t i = 1; i+1 < face.size(); ++i) {
                        RotationPoint a,b,cross;
                        for (int c = 0; c < 3; ++c) { a[c]=face[i].position[c]-face[0].position[c]; b[c]=face[i+1].position[c]-face[0].position[c]; }
                        cross = {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
                        area += .5*sqrt(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
                    }
                    if (face.size() != 3) continue; // Only the top face repeats on the box fixtures.
                    RotationPoint point = {}; std::array<double,2> uv = {};
                    const double weights[] = {.23,.31,.46};
                    for (int i = 0; i < 3; ++i) {
                        for (int c = 0; c < 3; ++c) point[c] += weights[i]*face[i].position[c];
                        for (int c = 0; c < 2; ++c) uv[c] += weights[i]*face[i].uv[c];
                    }
                    if (turned) point = turnModPoint(point,0,0,270,true);
                    if (name == "uv_local") {
                        const double angle = 22.5*3.14159265358979323846/180;
                        const double x=point[0]-.5,z=point[2]-.5;
                        point[0]=cos(angle)*x-sin(angle)*z+.5; point[2]=sin(angle)*x+cos(angle)*z+.5;
                    }
                    double u,v;
                    if (polygon) { u=2*point[0]-.5; v=2*point[1]-.5; }
                    else {
                        assert(fabs(point[1]-.8125) < 2e-6);
                        const double alongX=(point[0]-.125)/.75,alongZ=(point[2]-.25)/.5;
                        u=name == "uv_point" ? -1 : name == "uv_integer" ? -1+3*alongX : name == "uv_constant" ? 1.5 : name == "uv_reversed" ? 1.5-2*alongX : -.5+2*alongX;
                        v=name == "uv_point" ? -1 : name == "uv_integer" ? 1-alongZ : name == "uv_reversed" ? -.5+2*alongZ : 1.5-2*alongZ;
                        if (turned) { const double oldU=u; u=v; v=1-oldU; }
                    }
                    const bool constantU = name == "uv_point" || (name == "uv_constant" && !turned);
                    const bool constantV = name == "uv_point" || (name == "uv_constant" && turned);
                    if (!(constantU && u >= 0 && u <= 1)) u-=floor(u);
                    if (!(constantV && v >= 0 && v <= 1)) {
                        v-=floor(v);
                        if (constantV && v == 0) v=1; // Source V integer wraps to the image's top edge.
                    }
                    // Avoid varying texel boundaries when comparing rounded OBJ UVs with exact source pixels.
                    if ((!constantU && fabs(u*16-round(u*16)) < .01) || (!constantV && fabs(v*16-round(v*16)) < .01)) continue;
                    assert(uv[0] >= 0 && uv[0] <= 1 && uv[1] >= 0 && uv[1] <= 1);
                    const unsigned imageX=(unsigned)min((double)width-1,floor(uv[0]*width + (constantU ? .02 : 0)));
                    const unsigned imageY=(unsigned)min((double)height-1,floor((1-uv[1])*height + (constantV ? .02 : 0)));
                    const size_t pixel=((size_t)imageY*width+imageX)*4;
                    assert(pixels[pixel] == (unsigned char)(min(15.0,floor(u*16))*16));
                    assert(pixels[pixel+1] == (unsigned char)(min(15.0,floor((1-v)*16))*16));
                    assert(pixels[pixel+2] == 128 && pixels[pixel+3] == 255);
                    ++checked;
                }
                assert(checked > 0);
                assert(fabs(area-(polygon ? sqrt(1.25)/2 : 2.3125)) < 1e-5);
            }
            exportWorld(world,root/(label+".usda"),textured|EXPT_OUTPUT_SEPARATE_TEXTURE_TILES);
            exportWorld(world,root/(label+".stl"),0,true,false,true);
            const auto schematic=root/(label+".schem"); exportWorld(world,schematic,0);
            WorldGuide restored=readSchematic(schematic); assert(stateNames(restored)==stateNames(world));
            free(restored.sch.blocks); free(restored.sch.data);
        }
    }
}
