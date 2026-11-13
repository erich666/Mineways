// Independent literal HangingSignModel parts, including both faces of each chain.
namespace HangingSignExportTest {
using namespace NextExportTest;
static void part(Sample& sample,const P& lo,const P& hi,const double rectangles[6][4],
                 double yaw,int image,double localYaw=0,double offsetX=0,double offsetY=0) {
    const P points[]={{lo[0],lo[1],lo[2]},{hi[0],lo[1],lo[2]},{hi[0],hi[1],lo[2]},{lo[0],hi[1],lo[2]},
                      {lo[0],lo[1],hi[2]},{hi[0],lo[1],hi[2]},{hi[0],hi[1],hi[2]},{lo[0],hi[1],hi[2]}};
    const int corners[][4]={{5,4,0,1},{2,3,7,6},{0,4,7,3},{1,0,3,2},{5,1,2,6},{4,5,6,7}};
    for(int side=0;side<6;++side) {
        if(lo[2]==hi[2] && side!=3 && side!=5)continue;
        Face face;face.image=image;const auto& uv=rectangles[side];
        face.uv={{{uv[2]/4,uv[1]/2},{uv[0]/4,uv[1]/2},{uv[0]/4,uv[3]/2},{uv[2]/4,uv[3]/2}}};
        for(int v=0;v<4;++v) {
            const auto& p=points[corners[side][v]];
            const double localX=cos(localYaw)*p[0]+sin(localYaw)*p[2]+offsetX;
            const double localZ=sin(localYaw)*p[0]-cos(localYaw)*p[2];
            face.points[v]={8+cos(yaw)*localX-sin(yaw)*localZ,10-p[1]-offsetY,
                            8+sin(yaw)*localX+cos(yaw)*localZ};
        }
        sample.faces.push_back(face);
    }
}
static Sample sample(bool wall,bool attached,bool red,int orientation,const std::string& label,const std::string& properties) {
    Sample result;result.label="hanging-"+label;
    result.state=std::string("farmersdelight:")+(red?"red_":"")+(wall?"wall_hanging_canvas_sign":"hanging_canvas_sign")+properties;
    const int wallTurns[]={2,3,0,1};
    const double yaw=(wall?wallTurns[orientation]*4:orientation)*acos(-1.0)/8;
    const int image=red?1:0;
    const double board[6][4]={{2,12,16,14},{16,14,30,12},{0,14,2,24},{2,14,16,24},{16,14,18,24},{18,14,32,24}};
    part(result,{-7,0,-1},{7,10,1},board,yaw,image);
    if(wall) {
        const double plank[6][4]={{4,0,20,4},{20,4,36,0},{0,4,4,6},{4,4,20,6},{20,4,24,6},{24,4,40,6}};
        part(result,{-8,-6,-2},{8,-4,2},plank,yaw,image);
    }
    if(attached&&!wall) {
        const double chain[6][4]={{},{},{},{14,6,26,12},{},{26,6,38,12}};
        part(result,{-6,-6,0},{6,0,0},chain,yaw,image);
    } else {
        const double negative[6][4]={{},{},{},{0,6,3,12},{},{3,6,6,12}};
        const double positive[6][4]={{},{},{},{6,6,9,12},{},{9,6,12,12}};
        for(double x:{-5.0,5.0}) {
            part(result,{-1.5,0,0},{1.5,6,0},negative,yaw,image,-0.7853981852531433,x,-6);
            part(result,{-1.5,0,0},{1.5,6,0},positive,yaw,image,0.7853981852531433,x,-6);
        }
    }
    assert(result.faces.size()==(wall?20u:attached?8u:14u));return result;
}
static std::vector<Sample> samples() {
    std::vector<Sample> result={sample(false,false,false,0,"default","")};
    for(bool attached:{false,true})for(int angle=0;angle<16;++angle) {
        const std::string value=attached?"true":"false";
        result.push_back(sample(false,attached,false,angle,std::string(attached?"attached-":"detached-")+std::to_string(angle),
            "[attached="+value+",rotation="+std::to_string(angle)+"]"));
    }
    result.push_back(sample(false,false,false,7,"partial-rotation","[rotation=7]"));
    result.push_back(sample(false,true,false,0,"partial-attached","[attached=true]"));
    result.push_back(sample(false,true,true,3,"red-3","[attached=true,rotation=3]"));
    result.push_back(sample(true,false,false,0,"wall-default",""));
    const char* directions[]={"north","east","south","west"};
    for(int facing=0;facing<4;++facing)result.push_back(sample(true,false,false,facing,std::string("wall-")+directions[facing],
        std::string("[facing=")+directions[facing]+"]"));
    result.push_back(sample(true,false,true,1,"wall-red-east","[facing=east]"));
    assert(result.size()==42);return result;
}
static Color pixel(int image,int x,int y) {return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(image?210:90),255};}
}
static void checkHangingSignExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"hanging-signs",HangingSignExportTest::samples(),HangingSignExportTest::pixel,2e-5);
}
namespace TwilightSignExportTest {
using namespace NextExportTest;
static std::vector<Sample> samples() {
    const char* woods[]={"twilight_oak","canopy","mangrove","dark","time","transformation","mining","sorting"};
    const char* directions[]={"north","east","south","west"};
    std::vector<Sample> result;
    for(int wood=0;wood<8;++wood)for(bool wall:{false,true}) {
        const int orientation=wall?wood%4:(wood*2+1)%16;
        auto sample=CanvasExportTest::sample(wall,false,orientation);
        const std::string name=wall&&wood==0?"twilight":woods[wood];
        sample.state="twilightforest:"+name+(wall?"_wall_sign[facing="+std::string(directions[orientation])+ "]":"_sign[rotation="+std::to_string(orientation)+"]");
        sample.label="twilight-sign-"+std::string(woods[wood])+(wall?"-wall":"-standing");
        for(auto& face:sample.faces)face.image=wood;
        result.push_back(sample);
    }
    for(bool wall:{false,true}) {
        auto sample=CanvasExportTest::sample(wall,false,0,true);
        sample.state=wall?"twilightforest:twilight_wall_sign":"twilightforest:twilight_oak_sign";
        sample.label=wall?"twilight-wall-default":"twilight-sign-default";
        result.push_back(sample);
    }
    return result;
}
static Color pixel(int image,int x,int y) {return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(30+24*image),255};}
}
static void checkTwilightSignExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"twilight-signs",TwilightSignExportTest::samples(),TwilightSignExportTest::pixel,2e-5);
}
