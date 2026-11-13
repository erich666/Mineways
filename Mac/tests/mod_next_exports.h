// Analytic source geometry and pixels for static power, Twilight, and storage adapters.
namespace NextExportTest {
using P = std::array<double,3>;
using UV = std::array<double,2>;
using Color = std::array<unsigned char,4>;
struct Face { std::array<P,4> points; std::array<UV,4> uv; int image; };
struct Sample {std::string state,label;std::vector<Face> faces;int x=0,y=0;bool lock=false;};
static P turn(P p,int x,int y) {
    for(int i=0;i<x;++i)p={p[0],p[2],16-p[1]};
    for(int i=0;i<y;++i)p={16-p[2],p[1],p[0]};
    return p;
}
static UV project(int side,P p) {
    switch(side) {
    case 0:return {p[0],16-p[2]};case 1:return {p[0],p[2]};
    case 2:return {16-p[0],16-p[1]};case 3:return {p[0],16-p[1]};
    case 4:return {p[2],16-p[1]};default:return {16-p[2],16-p[1]};
    }
}
static P unproject(int side,UV uv) {
    switch(side) {
    case 0:return {uv[0],0,16-uv[1]};case 1:return {uv[0],16,uv[1]};
    case 2:return {16-uv[0],16-uv[1],0};case 3:return {uv[0],16-uv[1],16};
    case 4:return {0,16-uv[1],uv[0]};default:return {16,16-uv[1],16-uv[0]};
    }
}
static int transformedSide(int side,int x,int y) {
    const P p=turn(unproject(side,{8,8}),x,y);
    for(int s=0;s<6;++s)if(unproject(s,{8,8})==p)return s;
    assert(false);return -1;
}
static Face box(P lo,P hi,int side,std::array<double,4> uv,int rotation,int image) {
    const double a=lo[0],b=hi[0],c=lo[1],d=hi[1],e=lo[2],f=hi[2];
    const std::array<P,4> points[]={
        {{{a,c,f},{a,c,e},{b,c,e},{b,c,f}}},{{{a,d,e},{a,d,f},{b,d,f},{b,d,e}}},
        {{{b,c,e},{a,c,e},{a,d,e},{b,d,e}}},{{{a,c,f},{b,c,f},{b,d,f},{a,d,f}}},
        {{{a,c,e},{a,c,f},{a,d,f},{a,d,e}}},{{{b,c,f},{b,c,e},{b,d,e},{b,d,f}}}};
    Face result;result.points=points[side];result.image=image;
    for(int i=0;i<4;++i) {
        UV q=project(side,result.points[i]), low=project(side,lo), high=project(side,hi);
        for(int k=0;k<2;++k)q[k]=(q[k]-(std::min)(low[k],high[k]))/fabs(high[k]-low[k]);
        for(int r=0;r<rotation;++r)q={q[1],1-q[0]};
        result.uv[i]={uv[0]+q[0]*(uv[2]-uv[0]),uv[1]+q[1]*(uv[3]-uv[1])};
    }
    return result;
}
static void add(Sample& s,P lo,P hi,int side,std::array<double,4> uv,int rotation,int image) {
    Face f=box(lo,hi,side,uv,rotation,image);
    for(int v=0;v<4;++v) {
        f.points[v]=turn(f.points[v],s.x,s.y);
        if(s.lock)f.uv[v]=project(transformedSide(side,s.x,s.y),turn(unproject(side,f.uv[v]),s.x,s.y));
    }
    s.faces.push_back(f);
}
static Color pixel(int image,int x,int y) {
    switch(image) {
    case 0:return {(unsigned char)(16*x),(unsigned char)(16*y),70,255};
    case 1:return {(unsigned char)(16*x),(unsigned char)(16*y),70,128};
    case 2:return {(unsigned char)(16*x),(unsigned char)(16*y),40,255};
    case 3:return {(unsigned char)(16*x),(unsigned char)(16*y),90,255};
    case 4:case 5:if(x<8)return {200,(unsigned char)(image==5?0:160),80,255};return {(unsigned char)(16*x),(unsigned char)(16*y),40,255};
    case 6:return {50,100,150,255};case 7:return {170,90,20,255};case 8:return {90,200,130,255};
    case 9:return {200,76,22,192};case 10:return {96,36,10,192};default:return {20,180,240,255};
    }
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;
    for(bool transparent:{false,true})for(const char* orientation:{"plain","turned"}) {
        Sample s;const std::string kind=transparent?"transparent":"opaque";
        s.state="fixture:pipe_"+kind+"[orientation="+orientation+"]";s.label="pipe-"+kind+"-"+orientation;
        for(int side=0;side<6;++side) {
            Face f=box({5,5,5},{11,11,11},side,{5,5,11,11},0,transparent?1:0);s.faces.push_back(f);
            if(transparent) {std::reverse(f.points.begin(),f.points.end());for(int v=0;v<4;++v)f.uv[v]=project(side^1,f.points[v]);s.faces.push_back(f);}
        }
        result.push_back(s);
    }
    const char* facing[]={"north","east","up"};
    for(int orientation=0;orientation<3;++orientation) {
        Sample s;s.state=std::string("fixture:energy_cube[facing=")+facing[orientation]+"]";s.label=std::string("energy-")+facing[orientation];
        s.x=orientation==2?3:0;s.y=orientation==1?1:0;s.lock=orientation==1;
        add(s,{2,3,4},{14,13,12},2,{1,2,13,12},1,2);
        for(int side=0;side<6;++side)add(s,{6,6,6},{10,10,10},side,{3,4,9,10},0,3);
        result.push_back(s);
    }
    for(int kind=0;kind<3;++kind) {
        Sample s;s.state=kind==2?"twilightforest:pink_castle_door":std::string("fixture:twilight_connected[pose=")+(kind==0?"plain]":"turned]");
        s.label=kind==2?"twilight-pink":kind==0?"twilight-plain":"twilight-turned";
        s.x=kind==1?1:0;s.y=kind==1?3:0;s.lock=kind==1;
        for(int side=0;side<6;++side)add(s,{0,0,0},{16,16,16},side,{0,0,16,16},0,kind==2?5:4);
        result.push_back(s);
    }
    {
        Sample s;s.state="fixture:twilight_forcefield[down=false,east=true,north=true,south=false,up=false,west=false]";
        s.label="force-ne";
        add(s,{7,6,0},{9,10,5},2,{1,4,12,15},0,2);
        add(s,{9,6,5},{16,10,11},1,{0,2,15,13},0,2);
        result.push_back(s);
    }
    for(int kind=0;kind<3;++kind) {
        Sample s;s.state=kind==2?"fixturestorage:barrel[facing=up,flat_top=true,open=false]":std::string("fixturestorage:barrel[facing=north,flat_top=false,open=")+(kind==1?"true]":"false]");
        s.label=kind==2?"barrel-flat":kind==1?"barrel-open":"barrel-closed";s.x=kind==2?3:0;
        for(int part=0;part<2;++part) {const double top=part?15:kind==2?16:kind==1?10:14;const int image=part?8:kind==1?7:6;
            add(s,{1,0,2},{15,top,14},2,{1,2,15,14},0,image);add(s,{1,0,2},{15,top,14},1,{2,1,14,15},1,image);}
        result.push_back(s);
    }
    for(bool equipped:{false,true})for(int turn=0;turn<2;++turn) {
        Sample s;const std::string flag=equipped?"true":"false";
        s.state="fixturestorage:backpack[battery="+flag+",facing="+(turn?"east":"north")+",left_tank="+flag+",right_tank="+flag+"]";
        s.label=std::string("backpack-")+(equipped?"equipped":"default")+(turn?"-east":"-north");s.y=turn;
        const int indices[]={0,equipped?5:1,equipped?6:2,equipped?7:3,4};
        for(int index:indices) {const double low=(double)index;
            add(s,{low,1,2},{low+1,13,14},2,{1,2,15,14},0,9);add(s,{low,1,2},{low+1,13,14},3,{1,2,15,14},0,10);add(s,{low,1,2},{low+1,13,14},1,{2,1,14,15},0,11);}
        result.push_back(s);
    }
    for(const auto& sample:result)if(sample.label=="force-ne") {
        Sample partial=sample;partial.state="fixture:twilight_forcefield[east=true,north=true]";partial.label="force-partial";
        result.push_back(partial);break;
    }
    {
        Sample s;s.state="fixturestorage:backpack[battery=true,facing=north]";s.label="backpack-partial";
        for(int index:{0,1,2,7,4}) {const double low=(double)index;
            add(s,{low,1,2},{low+1,13,14},2,{1,2,15,14},0,9);
            add(s,{low,1,2},{low+1,13,14},3,{1,2,15,14},0,10);
            add(s,{low,1,2},{low+1,13,14},1,{2,1,14,15},0,11);
        }
        result.push_back(s);
    }
    return result;
}
}
static void checkNextDefaultExports(const std::filesystem::path& root) {
    using namespace NextExportTest;
    const auto folder=root/"next-defaults";std::filesystem::create_directories(folder);
    for(const auto& sample:samples()) {
        int type,state;assert(ResolveModState(sample.state,&type,&state)==1);
        unsigned short block=(unsigned short)type,value=(unsigned short)state;
        WorldGuide world={};world.type=WORLD_SCHEMATIC_TYPE;world.sch={&block,&value,1,1,1,1,false,true};
        for(bool individual:{true,false}) {
            const auto object=folder/(sample.label+(individual?".obj":"-mosaic.obj"));
            exportWorld(world,object,EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE_IMAGES|(individual?EXPT_OUTPUT_SEPARATE_TEXTURE_TILES:0),true,false,false,false,false,true);
            const auto faces=readGeometryFaces(object,sample.faces.size());const auto names=CustomExportTest::faceMaterials(object);const auto maps=CustomExportTest::materials(object);
            assert(names.size()==faces.size());std::set<size_t> used;
            for(size_t f=0;f<faces.size();++f) {
                const auto& face=faces[f];assert(face.size()==4);size_t match=sample.faces.size();std::array<int,4> ids={};
                for(size_t e=0;e<sample.faces.size();++e)if(!used.count(e)) {
                    const auto& expected=sample.faces[e];std::array<int,4> candidate={};bool same=true;
                    for(int v=0;v<4;++v) {candidate[v]=-1;for(int p=0;p<4;++p) {
                        bool close=true;for(int a=0;a<3;++a)close=close&&fabs(face[v].position[a]*16-expected.points[p][a])<1e-6;
                        if(close)candidate[v]=p;
                    }same=same&&candidate[v]>=0;}
                    if(same) {const P a=expected.points[0],b=expected.points[1],c=expected.points[2];P ab,ac;
                        for(int k=0;k<3;++k){ab[k]=b[k]-a[k];ac[k]=c[k]-a[k];}
                        const P normal={ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
                        double dot=0;for(int k=0;k<3;++k)dot+=normal[k]*face[0].normal[k];same=dot>0;}
                    if(same){match=e;ids=candidate;break;}
                }
                if(match==sample.faces.size())fprintf(stderr,"unmatched %s face%zu\n",sample.label.c_str(),f);
                assert(match<sample.faces.size());used.insert(match);const auto& expected=sample.faces[match];
                if(individual)for(int v=0;v<4;++v){assert(fabs(face[v].uv[0]-expected.uv[ids[v]][0]/16)<1e-6);assert(fabs(face[v].uv[1]-(1-expected.uv[ids[v]][1]/16))<1e-6);}
                assert(maps.count(names[f]));std::vector<unsigned char> pixels;unsigned width,height;
                assert(lodepng::decode(pixels,width,height,wide(maps.at(names[f])))==0);int checked=0;
                for(int triangle=1;triangle<3;++triangle)for(const auto& weights:{std::array<double,3>{.17,.31,.52},std::array<double,3>{.57,.29,.14},std::array<double,3>{.23,.58,.19}}) {
                    const int corners[]={0,triangle,triangle+1};double u=0,v=0,ru=0,rv=0;
                    for(int k=0;k<3;++k){u+=face[corners[k]].uv[0]*weights[k];v+=face[corners[k]].uv[1]*weights[k];ru+=expected.uv[ids[corners[k]]][0]*weights[k];rv+=expected.uv[ids[corners[k]]][1]*weights[k];}
                    if(expected.image<6&&(fabs(ru-round(ru))<1e-5||fabs(rv-round(rv))<1e-5))continue;
                    const int x=(int)floor(u*width),y=(int)floor((1-v)*height);assert(x>=0&&x<(int)width&&y>=0&&y<(int)height);
                    const Color color=pixel(expected.image,(int)floor(ru),(int)floor(rv));
                    for(int channel=0;channel<4;++channel)if(pixels[((size_t)y*width+x)*4+channel]!=color[channel]) {
                        fprintf(stderr,"pixel mismatch %s face%zu image%d at %g,%g channel%d: %d != %d\n",sample.label.c_str(),f,expected.image,ru,rv,channel,pixels[((size_t)y*width+x)*4+channel],color[channel]);assert(false);}
                    ++checked;
                }
                assert(checked>0);
            }
            assert(used.size()==sample.faces.size());
            const auto schematic=folder/(sample.label+(individual?"-restored.schem":"-mosaic-restored.schem"));exportWorld(world,schematic,0);
            WorldGuide restored=readSchematic(schematic);assert(stateNames(restored)==stateNames(world));free(restored.sch.blocks);free(restored.sch.data);
        }
    }
}
