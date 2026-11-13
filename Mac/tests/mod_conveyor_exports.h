// Literal source-space oracle: shipped IE12.4.2-194 ModelConveyor.getBaseConveyor,
// length1/HORIZONTAL, walls and corners true. Independent of the importer generator.
namespace ConveyorExportTest {
using namespace NextExportTest;
struct SourceFace {int role;double vertices[4][5];};
static const SourceFace source[]={
    {0,{{15,0,0,15,0},{15,0,16,15,16},{1,0,16,1,16},{1,0,0,1,0}}}, // bottomBelt
    {2,{{16,0,0,16,0},{16,0,16,16,16},{0,0,16,0,16},{0,0,0,0,0}}}, // bottom
    {0,{{0,2,0,0,16},{0,2,16,0,0},{16,2,16,16,0},{16,2,0,16,16}}}, // top
    {1,{{0,0,0,0,0},{0,0,16,0,16},{0,2,16,2,16},{0,2,0,2,0}}}, // bottom[0], bottom[1], top[1], top[0]
    {1,{{16,0,16,0,0},{16,0,0,0,16},{16,2,0,2,16},{16,2,16,2,0}}}, // bottom[2], bottom[3], top[3], top[2]
    {0,{{1,0,0,1,2},{1,2,0,1,0},{15,2,0,15,0},{15,0,0,15,2}}}, // bottomBelt[0], topBelt[0], topBelt[3], bottomBelt[3]
    {2,{{0,0,0,0,2},{0,2,0,0,0},{16,2,0,16,0},{16,0,0,16,2}}}, // bottom[0], top[0], top[3], bottom[3]
    {0,{{15,0,16,15,0},{15,2,16,15,2},{1,2,16,1,2},{1,0,16,1,0}}}, // bottomBelt[1], topBelt[1], topBelt[2], bottomBelt[2]
    {2,{{16,0,16,16,0},{16,2,16,16,2},{0,2,16,0,2},{0,0,16,0,0}}}, // bottom[1], top[1], top[2], bottom[2]
    {2,{{0,3,15,0,1},{0,3,16,0,0},{1,3,16,1,0},{1,3,15,1,1}}}, // wallOuterTop[1], corner[1], cornerBelt[1], wallInnerTop[1]
    {2,{{15,3,15,16,1},{15,3,16,16,0},{16,3,16,15,0},{16,3,15,15,1}}}, // wallOuterTop[2], corner[2], cornerBelt[2], wallInnerTop[2]
    {2,{{1,2,16,1,2},{1,3,16,1,3},{0,3,16,0,3},{0,2,16,0,2}}}, // top[1], corner[1], cornerBelt[1], topBelt[1]
    {2,{{16,2,16,15,2},{16,3,16,15,3},{15,3,16,16,3},{15,2,16,16,2}}}, // top[2], corner[2], cornerBelt[2], topBelt[2]
    {2,{{0,2,15,0,2},{0,3,15,0,3},{1,3,15,1,3},{1,2,15,1,2}}}, // wallOuter[1], wallOuterTop[1], wallInnerTop[1], wallInner[1]
    {2,{{15,2,15,16,2},{15,3,15,16,3},{16,3,15,15,3},{16,2,15,15,2}}}, // wallOuter[2], wallOuterTop[2], wallInnerTop[2], wallInner[2]
    {1,{{0,2,15,0,2},{0,2,16,0,3},{0,3,16,1,3},{0,3,15,1,2}}}, // wallOuter[1], top[1], corner[1], wallOuterTop[1]
    {1,{{1,3,15,16,2},{1,3,16,16,3},{1,2,16,15,3},{1,2,15,15,2}}}, // wallInner[1], topBelt[1], cornerBelt[1], wallInnerTop[1]
    {1,{{16,2,16,15,2},{16,2,15,15,3},{16,3,15,16,3},{16,3,16,16,2}}}, // top[2], wallOuter[2], wallOuterTop[2], corner[2]
    {1,{{15,3,16,1,2},{15,3,15,1,3},{15,2,15,0,3},{15,2,16,0,2}}}, // topBelt[2], wallInner[2], wallInnerTop[2], cornerBelt[2]
    {2,{{16,3,1,0,16},{16,3,0,0,15},{15,3,0,1,15},{15,3,1,1,16}}}, // wallOuterTop[3], corner[3], cornerBelt[3], wallInnerTop[3]
    {2,{{1,3,1,16,16},{1,3,0,16,15},{0,3,0,15,15},{0,3,1,15,16}}}, // wallOuterTop[0], corner[0], cornerBelt[0], wallInnerTop[0]
    {2,{{15,2,0,1,2},{15,3,0,1,3},{16,3,0,0,3},{16,2,0,0,2}}}, // top[3], corner[3], cornerBelt[3], topBelt[3]
    {2,{{0,2,0,15,2},{0,3,0,15,3},{1,3,0,16,3},{1,2,0,16,2}}}, // top[0], corner[0], cornerBelt[0], topBelt[0]
    {2,{{16,2,1,0,2},{16,3,1,0,3},{15,3,1,1,3},{15,2,1,1,2}}}, // wallOuter[3], wallOuterTop[3], wallInnerTop[3], wallInner[3]
    {2,{{1,2,1,16,2},{1,3,1,16,3},{0,3,1,15,3},{0,2,1,15,2}}}, // wallOuter[0], wallOuterTop[0], wallInnerTop[0], wallInner[0]
    {1,{{16,2,1,0,2},{16,2,0,0,3},{16,3,0,1,3},{16,3,1,1,2}}}, // wallOuter[3], top[3], corner[3], wallOuterTop[3]
    {1,{{15,3,1,16,2},{15,3,0,16,3},{15,2,0,15,3},{15,2,1,15,2}}}, // wallInner[3], topBelt[3], cornerBelt[3], wallInnerTop[3]
    {1,{{0,2,0,15,2},{0,2,1,15,3},{0,3,1,16,3},{0,3,0,16,2}}}, // top[0], wallOuter[0], wallOuterTop[0], corner[0]
    {1,{{1,3,0,1,2},{1,3,1,1,3},{1,2,1,0,3},{1,2,0,0,2}}}, // topBelt[0], wallInner[0], wallInnerTop[0], cornerBelt[0]
    {2,{{0,3,1,0,15},{0,3,15,0,1},{1,3,15,1,1},{1,3,1,1,15}}}, // wallOuterTop[0], wallOuterTop[1], wallInnerTop[1], wallInnerTop[0]
    {1,{{0,2,1,2,15},{0,2,15,2,1},{0,3,15,3,1},{0,3,1,3,15}}}, // wallOuter[0], wallOuter[1], wallOuterTop[1], wallOuterTop[0]
    {1,{{1,3,1,3,15},{1,3,15,3,1},{1,2,15,2,1},{1,2,1,2,15}}}, // wallInner[0], wallInner[1], wallInnerTop[1], wallInnerTop[0]
    {2,{{16,3,15,15,15},{16,3,1,15,1},{15,3,1,16,1},{15,3,15,16,15}}}, // wallOuterTop[2], wallOuterTop[3], wallInnerTop[3], wallInnerTop[2]
    {1,{{15,2,1,2,15},{15,2,15,2,1},{15,3,15,3,1},{15,3,1,3,15}}}, // wallInner[3], wallInner[2], wallInnerTop[2], wallInnerTop[3]
    {1,{{16,3,1,3,15},{16,3,15,3,1},{16,2,15,2,1},{16,2,1,2,15}}}, // wallOuter[3], wallOuter[2], wallOuterTop[2], wallOuterTop[3]
};
static Color pixel(int image,int x,int y) {
    const unsigned char blue[]={35,85,235,135,185};
    return {(unsigned char)(16*x),(unsigned char)(16*y),blue[image],255};
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;
    const char* kinds[]={"basic","dropper","vertical"};
    const char* directions[]={"default","north","east","south","west"};
    for(int kind=0;kind<3;++kind)for(int direction=0;direction<5;++direction) {
        Sample sample;sample.state=std::string("immersiveengineering:conveyor_")+kinds[kind];
        if(direction)sample.state+=std::string("[facing=")+directions[direction]+"]";
        sample.label=std::string("conveyor-")+kinds[kind]+"-"+directions[direction];
        for(const auto& literal:source) {
            Face face;face.image=literal.role?literal.role+2:kind;
            for(int corner=0;corner<4;++corner) {
                const auto& p=literal.vertices[corner];P point={p[0],p[1],p[2]};
                if(kind==2)point={point[0],16-point[2],point[1]};
                face.points[corner]=turn(point,0,direction?direction-1:0);
                face.uv[corner]={p[3],p[4]};
            }
            sample.faces.push_back(face);
        }
        result.push_back(sample);
    }
    return result;
}
}
static void checkConveyorDefaultExports(const std::filesystem::path& root) {
    static_assert(sizeof(ConveyorExportTest::source)/sizeof(ConveyorExportTest::source[0])==35,"source quad count");
    checkAnalyticModelExports(root/"conveyor-defaults",ConveyorExportTest::samples(),ConveyorExportTest::pixel);
}
