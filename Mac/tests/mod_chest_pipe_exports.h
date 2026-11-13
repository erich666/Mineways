// Source-space reference for XyCraft cache-zero pipes and closed chest cells.
namespace ChestPipeExportTest {
using namespace NextExportTest;
static P capTurn(P p,int direction) {
    const double x=p[0],y=p[1],z=p[2];
    switch(direction) {
    case 0:return p;case 1:return {x,16-y,16-z};case 2:return {x,16-z,y};
    case 3:return {16-x,16-z,16-y};case 4:return {y,16-z,16-x};default:return {16-y,16-z,x};
    }
}
static std::vector<Sample> pipes() {
    std::vector<Sample> result;
    for(int pose=0;pose<4;++pose) {
        const char* label=pose==0?"plain":pose==1?"turned":pose==2?"rolled":"obj";
        Sample s;s.label=std::string("xycraft-pipe-")+label;
        s.state=pose==3?"fixture:xycraft_pipe_obj":std::string("fixture:xycraft_pipe[pose=")+label+"]";
        s.x=pose==1?1:0;s.y=pose==1?3:0;s.lock=pose==1;
        add(s,{6,6,5},{10,11,10},2,{1,2,12,14},0,0);
        add(s,{6,6,5},{10,11,10},1,{2,3,14,15},1,0);
        if(pose==2)for(auto& face:s.faces)for(auto& p:face.points)p={p[1],16-p[0],p[2]};
        for(int direction=0;direction<6;++direction)for(int part=0;part<(pose==3?1:2);++part) {
            Face face;
            if(pose==3) {
                face.points={{{3,3,5},{11,3,5},{11,3,13},{3,3,13}}};
                face.uv={{{1,14},{14,14},{14,3},{1,3}}};face.image=1;
            } else {
                face=part?box({4,3,6},{6,4,8},5,{3,4,12,15},0,1):box({3,3,5},{11,5,13},0,{1,2,14,13},1,1);
                if(part)for(auto& p:face.points) {
                    const double dx=p[0]-5,dy=p[1]-4,t=sqrt(2.0)-1;
                    p[0]=5+dx-t*dy;p[1]=4+t*dx+dy;
                }
            }
            for(auto& p:face.points)p=capTurn(p,direction);
            s.faces.push_back(face);
        }
        result.push_back(s);
    }
    return result;
}
static void cube(Sample& sample,P low,P size,UV origin,int image,int facing) {
    const double x=low[0],y=low[1],z=low[2],w=size[0],h=size[1],d=size[2],u=origin[0],v=origin[1];
    const P points[]={{x,y,z},{x+w,y,z},{x+w,y+h,z},{x,y+h,z},{x,y,z+d},{x+w,y,z+d},{x+w,y+h,z+d},{x,y+h,z+d}};
    const int corners[][4]={{5,4,0,1},{2,3,7,6},{0,4,7,3},{1,0,3,2},{5,1,2,6},{4,5,6,7}};
    const double rectangles[][4]={{u+d,v,u+d+w,v+d},{u+d+w,v+d,u+d+2*w,v},
        {u,v+d,u+d,v+d+h},{u+d,v+d,u+d+w,v+d+h},{u+d+w,v+d,u+2*d+w,v+d+h},{u+2*d+w,v+d,u+2*d+2*w,v+d+h}};
    for(int side=0;side<6;++side) {
        Face face;face.image=image;const auto& uv=rectangles[side];
        face.uv={{{uv[2]/4,uv[1]/4},{uv[0]/4,uv[1]/4},{uv[0]/4,uv[3]/4},{uv[2]/4,uv[3]/4}}};
        for(int vertex=0;vertex<4;++vertex)face.points[vertex]=turn(points[corners[side][vertex]],0,(facing+2)%4);
        sample.faces.push_back(face);
    }
}
static Sample chest(int tier,int shape,int facing,const std::string& suffix) {
    const char* tiers[]={"wood","copper","iron","gold","diamond","netherite"};
    const char* shapes[]={"single","left","right"};const char* directions[]={"north","east","south","west"};
    Sample sample;sample.state="sophisticatedstorage:"+std::string(tier?tiers[tier]:"")+(tier?"_":"")+"chest"+suffix;
    sample.label=std::string("chest-")+tiers[tier]+"-"+shapes[shape]+"-"+directions[facing];
    const double x=shape==1?0:1,w=shape?15:14,latch=shape==1?0:shape==2?15:7,latchWidth=shape?1:2;
    const int body=2+(tier*3+shape)*2;
    cube(sample,{x,0,1},{w,10,14},{0,19},body,facing);
    cube(sample,{x,9,1},{w,5,14},{0,0},body,facing);
    cube(sample,{latch,7,15},{latchWidth,4,1},{0,0},body+1,facing);
    return sample;
}
static std::vector<Sample> samples() {
    auto result=pipes();const char* shapes[]={"single","left","right"};const char* directions[]={"north","east","south","west"};
    for(int shape=0;shape<3;++shape)for(int facing=0;facing<4;++facing)
        result.push_back(chest(0,shape,facing,std::string("[facing=")+directions[facing]+",type="+shapes[shape]+"]"));
    const char* tiers[]={"wood","copper","iron","gold","diamond","netherite"};
    for(int tier=0;tier<6;++tier) {auto sample=chest(tier,0,0,"");sample.label=std::string("chest-")+tiers[tier]+"-default";result.push_back(sample);}
    auto facing=chest(0,0,1,"[facing=east]");facing.label="chest-partial-facing";result.push_back(facing);
    auto shape=chest(0,1,0,"[type=left]");shape.label="chest-partial-type";result.push_back(shape);
    return result;
}
static Color pixel(int image,int x,int y) {
    if(!image)return {190,70,25,255};
    if(image==1)return {(unsigned char)(16*x),(unsigned char)(16*y),110,255};
    const int value=image-2,tier=value/6,shape=(value/2)%3;const bool latch=value%2;
    if((x<2&&y<2)||(x==5&&y==5))return {(unsigned char)(30+35*tier),210,(unsigned char)(90+40*shape),255};
    if(latch)return {0,0,0,0};
    return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(40+40*shape),255};
}
}
static void checkChestPipeExports(const std::filesystem::path& root) {
    const auto folder=root/"chest-pipe-defaults";
    checkAnalyticModelExports(folder,ChestPipeExportTest::samples(),ChestPipeExportTest::pixel,2e-5);
    unsigned short blocks[2],data[2];
    for(int index=0;index<2;++index) {int type,state;const std::string name=std::string("sophisticatedstorage:chest[facing=south,type=")+(index?"left]":"right]");
        assert(ResolveModState(name,&type,&state)==1);blocks[index]=(unsigned short)type;data[index]=(unsigned short)state;}
    WorldGuide world={};world.type=WORLD_SCHEMATIC_TYPE;world.sch={blocks,data,2,1,1,2,false,true};
    for(bool individual:{true,false}) {
        const auto file=folder/(individual?"chest-double-south.obj":"chest-double-south-mosaic.obj");
        exportWorld(world,file,EXPT_OUTPUT_MATERIALS|EXPT_OUTPUT_TEXTURE_IMAGES|(individual?EXPT_OUTPUT_SEPARATE_TEXTURE_TILES:0),true,false,false,false,false,true);
        const auto faces=readGeometryFaces(file,36);double minX=2,maxX=0;int seamNormals=0;
        for(const auto& face:faces) {bool atSeam=true;double loY=2,hiY=-1,loZ=2,hiZ=-1;
            for(const auto& vertex:face) {minX=(std::min)(minX,vertex.position[0]);maxX=(std::max)(maxX,vertex.position[0]);atSeam=atSeam&&vertex.position[0]==1;
                loY=(std::min)(loY,vertex.position[1]);hiY=(std::max)(hiY,vertex.position[1]);loZ=(std::min)(loZ,vertex.position[2]);hiZ=(std::max)(hiZ,vertex.position[2]);}
            if(atSeam&&loY==7.0/16&&hiY==11.0/16&&loZ==15.0/16&&hiZ==1)seamNormals|=face[0].normal[0]>0?1:2;
        }
        assert(minX==1.0/16&&maxX==31.0/16&&seamNormals==3);
    }
    const auto schematic=folder/"chest-double-south-restored.schem";exportWorld(world,schematic,0);
    auto restored=readSchematic(schematic);assert(stateNames(restored)==stateNames(world));free(restored.sch.blocks);free(restored.sch.data);
}
