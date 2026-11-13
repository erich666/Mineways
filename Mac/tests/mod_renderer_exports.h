// Independent source-space oracles for initialized tubes and closed shulker renderers.
namespace RendererExportTest {
using namespace NextExportTest;
static P outer(P p,int pose) {
    if(pose==1)return turn(p,1,3);
    if(pose==2)return {p[1],16-p[0],p[2]};
    return p;
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;
    const int orientations[][2]={{2,0},{0,0},{1,0},{1,2},{1,3},{1,1}};
    for(int pose=0;pose<3;++pose) {
        const char* name=pose==0?"plain":pose==1?"turned":"rolled";
        Sample sample;sample.state=std::string("fixture:pressure_tube[pose=")+name+"]";
        sample.label=std::string("tube-")+name;
        for(const auto& orientation:orientations)for(int part=0;part<2;++part) {
            const int side=part?5:1;
            Face face=part?box({4,11,6},{6,12,8},side,{3,4,12,15},0,0):
                           box({3,10,5},{11,12,13},side,{1,2,14,13},1,0);
            auto transform=[&](P point){return outer(turn(point,orientation[0],orientation[1]),pose);};
            const P normalPoint=transform(unproject(side,{8,8}));int target=-1;
            for(int direction=0;direction<6;++direction)if(normalPoint==unproject(direction,{8,8}))target=direction;
            assert(target>=0);
            for(int vertex=0;vertex<4;++vertex) {
                P point=face.points[vertex];
                if(part) {
                    // A positive 22.5 degree Z turn with rescale has tan(22.5)=sqrt(2)-1.
                    const double dx=point[0]-5,dy=point[1]-11,tangent=sqrt(2.0)-1;
                    point[0]=5+dx-tangent*dy;point[1]=11+tangent*dx+dy;
                }
                face.points[vertex]=transform(point);
                if(pose==1)face.uv[vertex]=project(target,transform(unproject(side,face.uv[vertex])));
            }
            sample.faces.push_back(face);
        }
        result.push_back(sample);
    }
    const char* facings[]={"default","down","up","north","south","west","east"};
    const char* tiers[]={"base","copper","netherite"};
    const int corners[][4]={{5,4,0,1},{2,3,7,6},{0,4,7,3},{1,0,3,2},{5,1,2,6},{4,5,6,7}};
    for(int tier=0;tier<3;++tier)for(int direction=0;direction<7;++direction) {
        Sample sample;sample.state="sophisticatedstorage:"+std::string(tier?tiers[tier]:"")+(tier?"_":"")+"shulker_box";
        if(direction)sample.state+=std::string("[facing=")+facings[direction]+"]";
        sample.label=std::string("shulker-")+tiers[tier]+"-"+facings[direction];
        for(int part=0;part<2;++part) {
            const double low=part?-16:-8,high=part?-4:0,v=part?0:28,h=part?12:8;
            const P points[]={{-8,low,-8},{8,low,-8},{8,high,-8},{-8,high,-8},
                              {-8,low,8},{8,low,8},{8,high,8},{-8,high,8}};
            const double rectangles[][4]={{16,v,32,v+16},{32,v+16,48,v},{0,v+16,16,v+16+h},
                                          {16,v+16,32,v+16+h},{32,v+16,48,v+16+h},{48,v+16,64,v+16+h}};
            for(int side=0;side<6;++side) {
                Face face;face.image=tier+1;const auto& uv=rectangles[side];
                face.uv={{{uv[2]/4,uv[1]/4},{uv[0]/4,uv[1]/4},{uv[0]/4,uv[3]/4},{uv[2]/4,uv[3]/4}}};
                for(int vertex=0;vertex<4;++vertex) {
                    const P point=points[corners[side][vertex]];
                    const double a=point[0],b=-point[1]-8,c=-point[2];P q;
                    switch(direction) {
                    case 2:q={a,b,c};break;
                    case 3:q={-a,-c,-b};break;
                    case 4:q={a,-c,b};break;
                    case 5:q={-b,-c,a};break;
                    case 6:q={b,-c,-a};break;
                    default:q={a,-b,-c};break;
                    }
                    for(int axis=0;axis<3;++axis)face.points[vertex][axis]=8+0.9994999766349792*q[axis];
                }
                sample.faces.push_back(face);
            }
        }
        result.push_back(sample);
    }
    return result;
}
static Color pixel(int image,int x,int y) {
    if(!image)return {(unsigned char)(16*x),(unsigned char)(16*y),90,255};
    // Native terrain tiles use16px nearest-neighbor samples of the64px entity sheet.
    if(image>1&&x==5&&y==5)return {(unsigned char)(image==2?55:195),220,180,255};
    return {(unsigned char)(16*x),(unsigned char)(16*y),60,255};
}
}
static void checkRendererDefaultExports(const std::filesystem::path& root) {
    const auto folder=root/"renderer-defaults";
    const auto cases=RendererExportTest::samples();
    checkAnalyticModelExports(folder,cases,RendererExportTest::pixel,2e-5);
    // The tier marker is one native texel and can fall between interior UV samples.
    // Check that texel directly, in addition to face geometry and sampled UV pixels.
    for(const auto& sample:cases)if(sample.label.find("shulker-")==0) {
        const auto maps=CustomExportTest::materials(folder/(sample.label+".obj"));assert(maps.size()==1);
        std::vector<unsigned char> pixels;unsigned width,height;
        assert(lodepng::decode(pixels,width,height,wide(maps.begin()->second))==0);
        assert(width==16&&height==16);const auto expected=RendererExportTest::pixel(sample.faces[0].image,5,5);
        for(int channel=0;channel<4;++channel)assert(pixels[(5*width+5)*4+channel]==expected[channel]);
    }
}
