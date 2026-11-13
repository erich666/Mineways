// Loader-owned geometry and live material context must remain distinct through export.
static void checkOwnedGeometryExports(const std::filesystem::path& root) {
    using namespace CustomExportTest;
    const auto folder=root/"owned-geometry";std::filesystem::create_directories(folder);
    struct Case {const char* state;const char* label;int kind;size_t faces;};
    const Case cases[]={
        {"fixture:obj_owned[mode=inherited]","obj-owned-inherited",0,1},
        {"fixture:obj_owned[mode=reset]","obj-owned-reset",1,1},
        {"fixture:machine_default[facing=north]","machine-default-north",2,3},
        {"fixture:machine_default[facing=east]","machine-default-east",2,3},
        {"fixture:machine_delegate[facing=north]","machine-delegate-north",3,2},
        {"fixture:machine_delegate[facing=east]","machine-delegate-east",3,2}};
    std::map<std::string,std::vector<GeometryFace>> identities;
    for(const auto& sample:cases) {
        int type,state;assert(ResolveModState(sample.state,&type,&state)==1);
        unsigned short block=(unsigned short)type,value=(unsigned short)state;
        WorldGuide world={};world.type=WORLD_SCHEMATIC_TYPE;world.sch={&block,&value,1,1,1,1,false,true};
        for(bool individual:{true,false}) {
            const auto object=folder/(std::string(sample.label)+(individual?".obj":"-mosaic.obj"));
            exportWorld(world,object,EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE_IMAGES|
                (individual?EXPT_OUTPUT_SEPARATE_TEXTURE_TILES:0));
            const auto faces=readGeometryFaces(object,sample.faces);const auto names=faceMaterials(object);
            const auto maps=materials(object);assert(names.size()==faces.size());
            std::set<std::filesystem::path> textures;
            for(const auto& entry:maps)textures.insert(entry.second);
            assert(textures.size()==(individual&&sample.kind==2?2u:1u));
            for(size_t f=0;f<faces.size();++f) {
                const auto& face=faces[f];assert(face.size()==4);
                std::vector<std::array<double,5>> expected;
                int colorKind=sample.kind;
                if(sample.kind==0)expected={{16,3,4,2,12},{4,3,4,12,12},{4,13,4,12,2},{16,13,4,2,2}};
                else if(sample.kind==1)expected={{3,1,13,2,4},{11,1,13,12,4},{11,9,13,12,14},{3,9,13,2,14}};
                else {
                    const double z=face[0].position[2]*16;
                    if(fabs(z+.008)<1e-6) {
                        expected={{16,0,-.008,0,16},{0,0,-.008,16,16},{0,16,-.008,16,0},{16,16,-.008,0,0}};
                        colorKind=4;
                    } else if(fabs(face[0].normal[1]-1)<1e-6)
                        expected={{1,15,3,1,3},{1,15,16,1,16},{14,15,16,14,16},{14,15,3,14,3}};
                    else expected={{14,2,3,1,15},{1,2,3,14,15},{1,15,3,14,2},{14,15,3,1,2}};
                }
                std::array<std::array<double,2>,4> referenceUV;
                std::set<size_t> matched;
                for(size_t v=0;v<4;++v) {
                    size_t match=expected.size();
                    for(size_t e=0;e<expected.size();++e) {
                        bool same=true;for(int a=0;a<3;++a)same=same&&fabs(face[v].position[a]*16-expected[e][a])<1e-6;
                        if(same)match=e;
                    }
                    assert(match<expected.size());matched.insert(match);
                    referenceUV[v]={expected[match][3]/16,1-expected[match][4]/16};
                    if(individual)for(int a=0;a<2;++a)assert(fabs(face[v].uv[a]-referenceUV[v][a])<1e-6);
                }
                assert(matched.size()==4);
                std::vector<unsigned char> pixels;unsigned width,height;
                assert(maps.count(names[f]));assert(lodepng::decode(pixels,width,height,wide(maps.at(names[f])))==0);
                int checked=0;
                for(int triangle=1;triangle<3;++triangle)for(const auto& weights:{std::array<double,3>{.17,.31,.52},std::array<double,3>{.57,.29,.14}}) {
                    const int ids[]={0,triangle,triangle+1};double u=0,v=0,ru=0,rv=0;
                    for(int i=0;i<3;++i){u+=face[ids[i]].uv[0]*weights[i];v+=face[ids[i]].uv[1]*weights[i];ru+=referenceUV[ids[i]][0]*weights[i];rv+=referenceUV[ids[i]][1]*weights[i];}
                    if(sample.kind<2&&(fabs(ru*16-round(ru*16))<1e-5||fabs((1-rv)*16-round((1-rv)*16))<1e-5))continue;
                    const int x=(int)floor(u*width),y=(int)floor((1-v)*height);
                    assert(x>=0&&x<(int)width&&y>=0&&y<(int)height);
                    std::array<unsigned char,4> color;
                    const auto red=(unsigned char)(16*floor(ru*16)),green=(unsigned char)(16*floor((1-rv)*16));
                    if(colorKind==0)color={red,green,40,255};
                    else if(colorKind==1)color={40,red,green,255};
                    else if(colorKind==4)color={185,105,35,255};
                    else color={55,125,185,255};
                    for(int c=0;c<4;++c)assert(pixels[((size_t)y*width+x)*4+c]==color[c]);
                    ++checked;
                }
                assert(checked>0);
            }
            if(sample.kind>=2) {
                const std::string key=std::to_string(sample.kind)+(individual?"tiles":"mosaic");
                auto found=identities.find(key);
                if(found==identities.end())identities[key]=faces;
                else for(size_t f=0;f<faces.size();++f)for(size_t v=0;v<4;++v) {
                    assert(faces[f][v].position==found->second[f][v].position);
                    assert(faces[f][v].uv==found->second[f][v].uv);
                    assert(faces[f][v].normal==found->second[f][v].normal);
                }
            }
        }
        const auto schematic=folder/(std::string(sample.label)+"-restored.schem");exportWorld(world,schematic,0);
        WorldGuide restored=readSchematic(schematic);assert(stateNames(restored)==stateNames(world));
        free(restored.sch.blocks);free(restored.sch.data);
    }
}
