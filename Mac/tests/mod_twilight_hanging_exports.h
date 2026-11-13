// Twilight entity materials use the independently checked vanilla hanging-sign mesh.
namespace TwilightHangingExportTest {
using namespace NextExportTest;
static Sample sample(int wood,bool wall,int orientation,bool attached,const std::string& label,const std::string& properties) {
    const char* woods[]={"twilight_oak","canopy","mangrove","dark","time","transformation","mining","sorting"};
    auto result=HangingSignExportTest::sample(wall,attached,false,orientation,"unused","");
    result.state="twilightforest:"+std::string(woods[wood])+(wall?"_wall_hanging_sign":"_hanging_sign")+properties;
    result.label=label;
    for(auto& face:result.faces)face.image=wood;
    return result;
}
static std::vector<Sample> samples() {
    const char* woods[]={"twilight_oak","canopy","mangrove","dark","time","transformation","mining","sorting"};
    const char* directions[]={"north","east","south","west"};
    std::vector<Sample> result;
    for(int wood=0;wood<8;++wood) {
        const int rotation=2*wood+1;const bool attached=wood%2!=0;
        const std::string prefix="twilight-hanging-"+std::string(woods[wood]);
        result.push_back(sample(wood,false,rotation,attached,prefix+"-ceiling",
            "[attached="+std::string(attached?"true":"false")+",rotation="+std::to_string(rotation)+"]"));
        result.push_back(sample(wood,true,wood%4,false,prefix+"-wall",std::string("[facing=")+directions[wood%4]+"]"));
    }
    result.push_back(sample(0,false,0,false,"twilight-hanging-default",""));
    result.push_back(sample(0,true,0,false,"twilight-hanging-wall-default",""));
    result.push_back(sample(0,false,7,false,"twilight-hanging-partial-rotation","[rotation=7]"));
    result.push_back(sample(0,false,0,true,"twilight-hanging-partial-attached","[attached=true]"));
    assert(result.size()==20);return result;
}
static Color pixel(int image,int x,int y) {return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(40+24*image),255};}
}
static void checkTwilightHangingExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"twilight-hanging-signs",TwilightHangingExportTest::samples(),TwilightHangingExportTest::pixel,2e-5);
}
