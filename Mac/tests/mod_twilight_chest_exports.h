// Twilight uses the vanilla chest mesh and one complete wood/trap/half sheet per block.
namespace TwilightChestExportTest {
using namespace NextExportTest;
static Sample sample(int family,bool trapped,int shape,int facing,const std::string& suffix,const std::string& label) {
    const char* families[]={"twilight_oak","canopy","mangrove","dark","time","transformation","mining","sorting"};
    Sample s=ChestPipeExportTest::chest(0,shape,facing,"");
    s.state=std::string("twilightforest:")+families[family]+(trapped?"_trapped_chest":"_chest")+suffix;s.label=label;
    for(auto& face:s.faces)face.image=family*6+(trapped?3:0)+shape;
    return s;
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;const char* families[]={"twilight_oak","canopy","mangrove","dark","time","transformation","mining","sorting"};
    for(int family=0;family<8;++family)for(bool trapped:{false,true})
        result.push_back(sample(family,trapped,0,0,"",std::string("twilight-chest-")+families[family]+(trapped?"-trapped-default":"-normal-default")));
    const char* facings[]={"north","east","south","west"};
    for(int shape=1;shape<3;++shape)for(int facing=0;facing<4;++facing) {
        const std::string half=shape==1?"left":"right";
        result.push_back(sample(0,false,shape,facing,"[facing="+std::string(facings[facing])+",type="+half+"]","twilight-chest-"+half+"-"+facings[facing]));
    }
    result.push_back(sample(0,true,1,0,"[facing=north,type=left]","twilight-chest-trapped-left"));
    result.push_back(sample(0,true,2,1,"[facing=east,type=right]","twilight-chest-trapped-right"));
    result.push_back(sample(0,false,0,2,"[facing=south]","twilight-chest-partial-facing"));
    result.push_back(sample(0,false,2,0,"[type=right]","twilight-chest-partial-type"));
    return result;
}
static Color pixel(int image,int x,int y) {
    return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(20+24*(image/6)+3*(image%3)+12*((image%6)/3)),255};
}
}
static void checkTwilightChestExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"twilight-chest-defaults",TwilightChestExportTest::samples(),TwilightChestExportTest::pixel);
}
