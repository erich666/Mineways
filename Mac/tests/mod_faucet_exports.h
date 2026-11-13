// Dry source bodies: application orientation applies, wrapper transforms and liquid do not.
namespace FaucetExportTest {
using namespace NextExportTest;
static Color pixel(int image,int x,int y) {
    return {(unsigned char)(16*x),(unsigned char)(16*y),(unsigned char)(image?150:70),255};
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;const char* labels[]={"north","east","south","west","turned"};
    for(bool on:{false,true})for(int pose=0;pose<5;++pose) {
        Sample sample;const std::string kind=on?"on":"off";
        sample.state="fixture:faucet_"+kind+"[facing="+(pose==4?"north":labels[pose])+",pose="+(pose==4?"turned]":"plain]");
        sample.label="faucet-"+kind+"-"+labels[pose];sample.x=pose==4?1:0;sample.y=pose==4?3:pose;sample.lock=pose==4;
        const P low=on?P{3,4,2}:P{2,5,3},high=on?P{14,12,13}:P{12,11,14};
        for(int side=0;side<6;++side)add(sample,low,high,side,{1,2,13,15},side==1?1:0,on?1:0);
        result.push_back(sample);
    }
    return result;
}
}
static void checkFaucetDefaultExports(const std::filesystem::path& root) {
    const auto cases=FaucetExportTest::samples();
    for(const auto& sample:cases) {
        int type,data;assert(ResolveModState(sample.state,&type,&data)==1);
        const auto* block=GetModBlock(type,data);assert(block&&block->definition&&block->definition->models.size()==1);
        const auto& elements=block->definition->models[0].Elements();assert(elements.size()==1&&elements[0].faces.size()==6);
        int hinted=0;
        for(const auto& face:elements[0].faces) {
            if(face.direction==2) {assert(face.cullface==2);++hinted;}
            else assert(face.cullface==-1);
        }
        assert(hinted==1);
    }
    checkAnalyticModelExports(root/"faucet-defaults",cases,FaucetExportTest::pixel);
}
