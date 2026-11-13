// Delegated source models retain their own textures and the renderer's orientation policy.
namespace DataExportTest {
using namespace NextExportTest;
static Color pixel(int image,int x,int y) {
    const Color base[]={{{80,100,120,255}},{{20,170,210,255}},{{70,80,90,255}},{{30,220,80,255}}};
    Color result=base[image];result[0]+=(unsigned char)(3*(x/4));result[1]+=(unsigned char)(3*(y/4));return result;
}
static std::vector<Sample> samples() {
    std::vector<Sample> result;
    const char* facings[]={"up","down","north","south","east","west"};
    const int turns[][2]={{0,0},{2,0},{3,2},{3,0},{3,3},{3,1}};
    for(bool online:{false,true})for(int direction=0;direction<6;++direction) {
        Sample sample;sample.state=std::string("fixturedata:qio[active=")+(online?"true":"false")+",facing="+facings[direction]+"]";
        sample.label=std::string("qio-")+(online?"online-":"offline-")+facings[direction];
        sample.x=turns[direction][0];sample.y=turns[direction][1];
        for(int side=0;side<6;++side)add(sample,{2,3,4},{11,13,15},side,{1,2,13,15},0,online?1:0);
        result.push_back(sample);
    }
    const char* states[]={"refinedstorage:portable_grid", "refinedstorage:portable_grid[active=true,facing=north]",
        "refinedstorage:portable_grid[active=false,facing=east]", "refinedstorage:portable_grid[active=true,facing=up_west]",
        "refinedstorage:portable_grid[active=false,facing=down_east]", "refinedstorage:creative_portable_grid[active=true,facing=west]"};
    const char* labels[]={"portable-default","portable-active-north","portable-inactive-east","portable-active-up-west",
                          "portable-inactive-down-east","portable-creative-active-west"};
    const int portableTurns[][2]={{0,0},{0,0},{0,1},{3,1},{1,1},{0,3}};
    for(int index=0;index<6;++index) {
        Sample sample;sample.state=states[index];sample.label=labels[index];
        sample.x=portableTurns[index][0];sample.y=portableTurns[index][1];sample.lock=true;
        const bool active=index==1||index==3||index==5;
        for(int side=0;side<6;++side)add(sample,{2,3,4},{11,13,15},side,{1,2,13,15},0,active?3:2);
        result.push_back(sample);
    }
    Sample active=result[13];active.state="refinedstorage:portable_grid[active=true]";active.label="portable-partial-active";
    Sample facing=result[14];facing.state="refinedstorage:portable_grid[facing=east]";facing.label="portable-partial-facing";
    result.push_back(active);result.push_back(facing);
    return result;
}
}
static void checkDataDefaultExports(const std::filesystem::path& root) {
    checkAnalyticModelExports(root/"data-defaults",DataExportTest::samples(),DataExportTest::pixel);
}
