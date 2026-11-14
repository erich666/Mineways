// Vanilla spruce SignModel source coordinates, with the canvas renderer's pose and64x32 UV sheet.
namespace CanvasExportTest {
using namespace NextExportTest;
static Sample sample(bool wall,bool red,int orientation,bool useDefault=false) {
    Sample s;s.state=std::string("farmersdelight:")+(red?"red_":"")+(wall?"canvas_wall_sign":"canvas_sign");
    const char* facings[]={"north","east","south","west"};
    if(!useDefault)s.state+=wall?std::string("[facing=")+facings[orientation]+"]":"[rotation="+std::to_string(orientation)+"]";
    s.label=std::string("canvas-")+(wall?"wall-":"standing-")+(red?"red-":"blank-")+(useDefault?"default":wall?facings[orientation]:std::to_string(orientation));
    const P source[2][8]={
        {{-12,-14,-1},{12,-14,-1},{12,-2,-1},{-12,-2,-1},{-12,-14,1},{12,-14,1},{12,-2,1},{-12,-2,1}},
        {{-1,-2,-1},{1,-2,-1},{1,12,-1},{-1,12,-1},{-1,-2,1},{1,-2,1},{1,12,1},{-1,12,1}}};
    const int corners[][4]={{5,4,0,1},{2,3,7,6},{0,4,7,3},{1,0,3,2},{5,1,2,6},{4,5,6,7}};
    const double rectangles[2][6][4]={
        {{2,0,26,2},{26,2,50,0},{0,2,2,14},{2,2,26,14},{26,2,28,14},{28,2,52,14}},
        {{2,14,4,16},{4,16,6,14},{0,16,2,30},{2,16,4,30},{4,16,6,30},{6,16,8,30}}};
    const int wallTurns[]={2,3,0,1};
    const double theta=(wall?wallTurns[orientation]*4:orientation)*acos(-1.0)/8;
    const double cosine=cos(theta),sine=sin(theta),scale=0.6666666865348816;
    for(int part=0;part<(wall?1:2);++part)for(int side=0;side<6;++side) {
        Face face;face.image=red?1:0;const auto& uv=rectangles[part][side];
        face.uv={{{uv[2]/4,uv[1]/2},{uv[0]/4,uv[1]/2},{uv[0]/4,uv[3]/2},{uv[2]/4,uv[3]/2}}};
        for(int vertex=0;vertex<4;++vertex) {
            const auto& p=source[part][corners[side][vertex]];
            const double x=scale*p[0],y=-scale*p[1]-(wall?5:0),z=-scale*p[2]-(wall?7:0);
            face.points[vertex]={8+cosine*x-sine*z,8+y,8+sine*x+cosine*z};
        }
        s.faces.push_back(face);
    }
    return s;
}
static std::vector<Sample> samples() {
    std::vector<Sample> result={sample(false,false,0,true)};
    for(int angle=0;angle<16;++angle)result.push_back(sample(false,false,angle));
    result.push_back(sample(false,true,3));result.push_back(sample(true,false,0,true));
    for(int facing=0;facing<4;++facing)result.push_back(sample(true,false,facing));
    result.push_back(sample(true,true,1));return result;
}
static Color pixel(int image,int x,int y) {
    // The64x32 source normalizes to a square, then uses the native16px terrain resolution.
    return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(image?180:60),255};
}
}
static void checkCanvasDefaultExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"canvas-defaults",CanvasExportTest::samples(),CanvasExportTest::pixel,2e-5);
}
