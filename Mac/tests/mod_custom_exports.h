// Original custom-loader fixtures verify source geometry and texture directions.
namespace CustomExportTest {
struct Sample { std::string state; int kind, turn, family; size_t faces, textures; };
using ReferencePoint = std::array<double, 3>;
static ReferencePoint turnY(ReferencePoint p, int turns) {
    for (int i = 0; i < turns; ++i) p = {16-p[2], p[1], p[0]};
    return p;
}
static ReferencePoint sourcePoint(const GeometryVertex& vertex, int turns) {
    ReferencePoint p = vertex.position;
    for (double& v : p) v *= 16;
    return turnY(p, (4-turns)%4);
}
static ReferencePoint lanternLocal(ReferencePoint p, bool inverse) {
    const double angle = 22.5 * 3.14159265358979323846 / 180;
    const double c = cos(angle), s = sin(angle);
    const double x = p[0]-7, y = p[1]-5;
    p[0] = 7 + (inverse ? (x*c+y*s)*c : (x*c-y*s)/c);
    p[1] = 5 + (inverse ? (-x*s+y*c)*c : (x*s+y*c)/c);
    return p;
}
static std::vector<ReferencePoint> corners(const Sample& sample) {
    std::vector<ReferencePoint> result;
    auto box = [&](ReferencePoint low, ReferencePoint high, int part) {
        for (int x=0; x<2; ++x) for (int y=0; y<2; ++y) for (int z=0; z<2; ++z) {
            ReferencePoint p = {x ? high[0] : low[0], y ? high[1] : low[1], z ? high[2] : low[2]};
            if (part == 2) p = lanternLocal(p, false);
            if (part) p = {p[2], p[1]+2, 18-p[0]};
            p = turnY(p, sample.turn);
            for (double& v : p) v /= 16;
            result.push_back(p);
        }
    };
    if (sample.kind == 0) box({1,2,3}, {15,14,13}, 0);
    else if (sample.kind == 2) {
        box({1,12,13}, {15,15,16}, 0);
        box({3,1,5}, {11,10,13}, 1);
        box({6,4,8}, {8,6,10}, 2);
    } else box({0,0,0}, {16,16,16}, 0);
    return result;
}
static std::map<std::string, std::filesystem::path> materials(const std::filesystem::path& object) {
    auto material = object; material.replace_extension(".mtl");
    std::istringstream input(readText(material)); std::string line, name;
    std::map<std::string, std::filesystem::path> result;
    while (std::getline(input, line)) {
        if (line.compare(0, 7, "newmtl ") == 0) name = line.substr(7);
        if (line.compare(0, 7, "map_Kd ") == 0) result[name] = object.parent_path() / line.substr(7);
    }
    return result;
}
static std::vector<std::string> faceMaterials(const std::filesystem::path& object) {
    std::istringstream input(readText(object)); std::string line, name;
    std::vector<std::string> result;
    while (std::getline(input,line)) {
        if (line.compare(0,7,"usemtl ")==0) name=line.substr(7);
        if (line.compare(0,2,"f ")==0) result.push_back(name);
    }
    return result;
}
// Determine reference pixels from original model coordinates, independently of exported UVs.
static bool referencePixel(const Sample& sample, const GeometryFace& face, ReferencePoint p,
    std::array<unsigned char,4>& color) {
    ReferencePoint center = {};
    for (const auto& vertex : face) {
        ReferencePoint q=sourcePoint(vertex,sample.turn);
        for (int axis=0;axis<3;++axis) center[axis]+=q[axis]/face.size();
    }
    p=turnY(p,(4-sample.turn)%4);
    if (sample.kind==0) {
        color = fabs(center[2]-3)<1e-5 ? std::array<unsigned char,4>{210,70,35,255} :
            std::array<unsigned char,4>{30,130,195,255}; return true;
    }
    if (sample.kind==1) {
        color = fabs(center[1]-16)<1e-5 ? std::array<unsigned char,4>{180,60,185,255} :
            std::array<unsigned char,4>{95,175,45,255}; return true;
    }
    double u=0,v=0;
    if (sample.kind==2) {
        if (center[1]>12+1e-5) { color={80,110,140,255}; return true; }
        // Undo body Java Y+90 and its (0,2,2) pixel translation.
        p={18-p[2],p[1]-2,p[0]}; center={18-center[2],center[1]-2,center[0]};
        if (fabs(center[0]-3)<1e-5) { u=p[2];v=16-p[1]; }
        else if (fabs(center[0]-11)<1e-5) { u=16-p[2];v=16-p[1]; }
        else if (fabs(center[1]-1)<1e-5) { u=p[0];v=16-p[2]; }
        else if (fabs(center[1]-10)<1e-5) {
            // Explicit up UV [1,2,13,14], face rotation90.
            u=1+12*(p[2]-5)/8;v=2+12*(1-(p[0]-3)/8);
        } else if (fabs(center[2]-5)<1e-5) { u=16-p[0];v=16-p[1]; }
        else if (fabs(center[2]-13)<1e-5) { u=p[0];v=16-p[1]; }
        else {
            p=lanternLocal(p,true);center=lanternLocal(center,true);
            if (fabs(center[2]-8)<1e-5) {u=2+5*(8-p[0])/2;v=3+8*(6-p[1])/2;}
            else { assert(fabs(center[1]-6)<1e-5);u=4+4*(p[0]-6)/2;v=5+8*(p[2]-8)/2; }
        }
        const double tx=u/8,ty=v/8;
        if (fabs(tx-round(tx))<1e-4 || fabs(ty-round(ty))<1e-4) return false;
        const int x=(int)floor(tx),y=(int)floor(ty); assert(x>=0&&x<2&&y>=0&&y<2);
        const unsigned char pixels[4][4]={{210,40,30,255},{35,185,65,255},{45,70,220,255},{235,205,45,255}};
        std::copy(pixels[y*2+x],pixels[y*2+x]+4,color.begin());return true;
    }
    int side=0;
    if (fabs(center[1])<1e-5) {side=0;u=p[0];v=16-p[2];}
    else if(fabs(center[1]-16)<1e-5) {side=1;u=p[0];v=p[2];}
    else if(fabs(center[2])<1e-5) {side=2;u=16-p[0];v=16-p[1];}
    else if(fabs(center[2]-16)<1e-5) {side=3;u=p[0];v=16-p[1];}
    else if(fabs(center[0])<1e-5) {side=4;u=p[2];v=16-p[1];}
    else {assert(fabs(center[0]-16)<1e-5);side=5;u=16-p[2];v=16-p[1];}
    if(fabs(u-round(u))<1e-4||fabs(v-round(v))<1e-4)return false;
    const int x=(int)floor(u),y=(int)floor(v);assert(x>=0&&x<16&&y>=0&&y<16);
    int blue=150;
    if(sample.family==0&&side<2)blue=side==0?40:90;
    else if((sample.family==1||sample.family==2)&&side<2)blue=90;
    else if(sample.family==2)blue=200;
    color={(unsigned char)(x*16),(unsigned char)(y*16),(unsigned char)blue,255};return true;
}
}

static void checkCustomDefaultExports(const std::filesystem::path& root) {
    using namespace CustomExportTest;
    std::vector<Sample> samples={
        {"fixture:framed_default[facing=north]",0,0,0,3,2},
        {"fixture:framed_default[facing=east]",0,1,0,3,2},
        {"fixture:microchip_default[facing=north]",1,0,0,6,2},
        {"fixture:microchip_default[facing=east]",1,0,0,6,2}};
    const char* directions[]={"north","east","south","west"};
    for(int turn=0;turn<4;++turn)samples.push_back({std::string("amendments:wall_lantern[attachment=block,facing=")+directions[turn]+"]",2,turn,0,10,2});
    const char* families[]={"side_top_bottom","side_vertical","vertical","all_same_texture"};
    for(int family=0;family<4;++family)for(const char* orientation:{"plain","turned"})
        samples.push_back({std::string("fixture:ie_sides_")+families[family]+"[orientation="+orientation+"]",3,0,family,6,family==0?3u:family==3?1u:2u});
    const auto folder=root/"custom-defaults";std::filesystem::create_directories(folder);
    const int mode=EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE_IMAGES;
    std::map<std::string,std::vector<GeometryFace>> fixedReferences;
    for(size_t index=0;index<samples.size();++index) {
        const Sample& sample=samples[index];int type,state;assert(ResolveModState(sample.state,&type,&state)==1);
        unsigned short block=(unsigned short)type,value=(unsigned short)state;
        WorldGuide world={};world.type=WORLD_SCHEMATIC_TYPE;world.sch={&block,&value,1,1,1,1,false,true};
        const auto expectedCorners=corners(sample);
        std::string label;
        if(sample.kind==0||sample.kind==1) label=std::string(sample.kind==0?"framed-":"microchip-")+
            (sample.state.find("facing=east")!=std::string::npos?"east":"north");
        else if(sample.kind==2) label=std::string("lantern-")+directions[sample.turn];
        else label=std::string("ie-sides-")+families[sample.family]+"-"+
            (sample.state.find("orientation=turned")!=std::string::npos?"turned":"plain");
        for(bool individual:{true,false}) {
            const auto object=folder/(label+(individual?".obj":"-mosaic.obj"));
            exportWorld(world,object,mode|(individual?EXPT_OUTPUT_SEPARATE_TEXTURE_TILES:0));
            const auto faces=readGeometryFaces(object,sample.faces);
            const auto names=faceMaterials(object);assert(names.size()==faces.size());
            const auto maps=materials(object);std::set<std::filesystem::path> paths;
            for(const auto& entry:maps)paths.insert(entry.second);
            assert(paths.size()==(individual?sample.textures:1));
            struct Texture {std::vector<unsigned char> pixels;unsigned width,height;};
            std::map<std::filesystem::path,Texture> images;
            for(const auto& path:paths) {auto& im=images[path];assert(lodepng::decode(im.pixels,im.width,im.height,wide(path))==0);}
            for(size_t f=0;f<faces.size();++f) {
                const auto& face=faces[f];assert(face.size()==4);
                for(const auto& vertex:face) {
                    bool matched=false;
                    for(const auto& corner:expectedCorners) {
                        bool same=true;for(int axis=0;axis<3;++axis)same=same&&fabs(vertex.position[axis]-corner[axis])<1e-6;
                        matched=matched||same;
                    }
                    assert(matched);
                }
                auto found=maps.find(names[f]);assert(found!=maps.end());const auto& im=images.at(found->second);
                int checked=0;
                // Interior samples avoid texture seams and cover both triangle halves.
                for(int triangle=1;triangle<3;++triangle)for(const auto& weights: {std::array<double,3>{.17,.31,.52},std::array<double,3>{.57,.29,.14},std::array<double,3>{.23,.58,.19}}) {
                    const int ids[]={0,triangle,triangle+1};ReferencePoint p={};double u=0,v=0;
                    for(int i=0;i<3;++i) {
                        for(int axis=0;axis<3;++axis)p[axis]+=face[ids[i]].position[axis]*16*weights[i];
                        u+=face[ids[i]].uv[0]*weights[i];v+=face[ids[i]].uv[1]*weights[i];
                    }
                    std::array<unsigned char,4> color;
                    if(!referencePixel(sample,face,p,color))continue;
                    const int x=(int)floor(u*im.width),y=(int)floor((1-v)*im.height);
                    assert(x>=0&&x<(int)im.width&&y>=0&&y<(int)im.height);
                    for(int channel=0;channel<4;++channel)assert(im.pixels[((size_t)y*im.width+x)*4+channel]==color[channel]);
                    ++checked;
                }
                assert(checked>0);
            }
            if(sample.kind==1||sample.kind==3) {
                const std::string key=std::to_string(sample.kind)+":"+std::to_string(sample.family)+(individual?"tiles":"mosaic");
                auto found=fixedReferences.find(key);
                if(found==fixedReferences.end())fixedReferences[key]=faces;
                else for(size_t f=0;f<faces.size();++f)for(size_t v=0;v<4;++v) {
                    assert(faces[f][v].position==found->second[f][v].position);
                    assert(faces[f][v].uv==found->second[f][v].uv);
                }
            }
        }
        const auto schematic=folder/(label+"-restored.schem");exportWorld(world,schematic,0);
        WorldGuide restored=readSchematic(schematic);assert(stateNames(restored)==stateNames(world));
        free(restored.sch.blocks);free(restored.sch.data);
    }
}
