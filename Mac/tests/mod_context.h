// Bounded context reads exercise the actual chunk and Sponge readers.
static void contextInt(Bytes& bytes, int value) {
    const uint32_t bits = (uint32_t)value;
    for (int shift : {24,16,8,0}) bytes.push_back((unsigned char)(bits >> shift));
}
static void contextIntTag(Bytes& bytes, const std::string& name, int value) {
    bytes.push_back(3); stringValue(bytes,name); contextInt(bytes,value);
}
static void contextPosition(Bytes& bytes, const std::string& name, int x, int y, int z) {
    bytes.push_back(11); stringValue(bytes,name); contextInt(bytes,3);
    contextInt(bytes,x); contextInt(bytes,y); contextInt(bytes,z);
}
static Bytes contextEntity(int format, int x, int y, int z, const std::string& id,
    const Bytes& payload) {
    Bytes bytes;
    stringTag(bytes,format == 0 ? "id" : "Id",id);
    if (format == 0) {
        contextIntTag(bytes,"x",x); contextIntTag(bytes,"y",y); contextIntTag(bytes,"z",z);
    } else contextPosition(bytes,"Pos",x,y,z);
    if (format == 2) { bytes.push_back(10); stringValue(bytes,"Data"); }
    bytes.insert(bytes.end(),payload.begin(),payload.end());
    if (format == 2) bytes.push_back(0);
    bytes.push_back(0); return bytes;
}
static void contextList(Bytes& bytes, const std::string& name, const std::vector<Bytes>& records) {
    bytes.push_back(9); stringValue(bytes,name); bytes.push_back(10); contextInt(bytes,(int)records.size());
    for (const auto& record : records) bytes.insert(bytes.end(),record.begin(),record.end());
}
static Bytes contextSponge(bool v2, const std::string& state, const std::vector<Bytes>& records, bool entities = true) {
    Bytes bytes = {10}; stringValue(bytes,v2 ? "Schematic" : "");
    if (!v2) { bytes.push_back(10); stringValue(bytes,"Schematic"); }
    // Put the block container before the dimensions to exercise deferred reads.
    if (!v2) { bytes.push_back(10); stringValue(bytes,"Blocks"); }
    bytes.push_back(10); stringValue(bytes,"Palette");
    contextIntTag(bytes,state,0); contextIntTag(bytes,"minecraft:air",1); bytes.push_back(0);
    bytes.push_back(7); stringValue(bytes,v2 ? "BlockData" : "Data"); contextInt(bytes,2);
    bytes.push_back(0); bytes.push_back(1);
    if (entities) contextList(bytes,"BlockEntities",records);
    if (!v2) bytes.push_back(0);
    for (const char* name : {"Width","Height","Length"}) {
        bytes.push_back(2); stringValue(bytes,name); word(bytes,std::string(name)=="Width" ? 2 : 1);
    }
    bytes.push_back(0); if (!v2) bytes.push_back(0);
    return bytes;
}
static bool contextReadSponge(Bytes bytes, int* resultType = nullptr, int* resultData = nullptr) {
    bfFile file=buffer(bytes); file.offset=&file._offset;
    int w,h,l,version; unsigned short *blocks=nullptr,*data=nullptr;
    const int result=nbtGetSpongeSchematic(&file,&w,&h,&l,&blocks,&data,&version);
    if (result) {
        assert(w==2 && h==1 && l==1);
        if (resultType) *resultType=blocks[0];
        if (resultData) *resultData=data[0];
    } else assert(!blocks && !data);
    free(blocks); free(data); return result!=0;
}
static void testModContext(const std::filesystem::path& path) {
    const std::string geometry="element\t0\t0\t0\t16\t16\t16\tnone\t0\t8\t8\t8\t0\nface\tup\t0\t0\t16\t16\t0\t0\t-\n";
    const std::string prefix="MINEWAYS_MOD_PACK\t6\ntexture\t0\tlab:block/stone\ttextures/0.png\ngeometry\t0\n"+geometry+"endgeometry\n";
    const std::string split="state\tfixture:split\t808080\t255\nsplitpolicy\tdummy\tfixture:split\t0\t0\t0\t1\n";
    const std::string manifest=prefix+split+"usemodel\t0\t0\t0\t0\n"
        "part\t0\t0\t0\nemptypart\t0\t1\t0\nendstate\n"
        "state\tfixture:machine\t808080\t255\nsplitpolicy\tposition\tfixture:machine_master\t1\t1\t1\t3\nusemodel\t0\t0\t0\t0\n"
        "part\t0\t0\t0\nemptypart\t1\t0\t0\nendstate\n";
    writeFile(path,manifest);
    std::string error; assert(LoadModPack(path.u8string(),error));
    // Split parts must reference validated shared geometry; only emptypart can be empty.
    for (const auto& invalid : std::vector<std::string>{
            "model\t0\t0\t0\npart\t0\t0\t0\n",
            "model\t0\t0\t0\nelement\t0\t0\t0\t16\t16\t16\tnone\t0\t8\t8\t8\t0\npart\t0\t0\t0\n",
            "model\t0\t0\t0\n"+geometry+"part\t0\t0\t0\n",
            "usemodel\t0\t0\t0\t0\npart\t0\t0\t0\nemptypart\t0\t0\t0\n"}) {
        writeFile(path,prefix+split+invalid+"endstate\n");
        assert(!LoadModPack(path.u8string(),error));
    }
    writeFile(path,manifest);
    int type,state; assert(ResolveModState("fixture:split",&type,&state)==1 && ModBlockNeedsContext(type,state));
    Bytes dummy0,dummy1,position;
    contextIntTag(dummy0,"dummy",0); contextIntTag(dummy1,"dummy",1);
    contextPosition(position,"posInMB",2,1,1);
    for (bool v2 : {true,false}) {
        const int format=v2 ? 1 : 2;
        int actualType,actualData;
        assert(contextReadSponge(contextSponge(v2,"fixture:split",{},false),&actualType,&actualData));
        assert(ModBlockNeedsContext(actualType,actualData));
        const Bytes first=contextEntity(format,0,0,0,"fixture:split",dummy0);
        assert(contextReadSponge(contextSponge(v2,"fixture:split",{first}),&actualType,&actualData));
        assert(actualType==type && !ModBlockNeedsContext(type,actualData));
        assert(GetModBlock(type,actualData)->canonical=="fixture:split");
        const auto* resolved=GetModBlock(type,actualData);
        assert(resolved->definition->models[0].geometry &&
            resolved->definition->models[0].geometry==resolved->sourceDefinition->models[0].geometry);
        assert(resolved->definition->models[0].elements.empty() && resolved->definition->models[0].polygons.empty());
        const int visibleState=actualData;
        assert(contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,0,0,0,"fixture:split",dummy1)}),&actualType,&actualData));
        assert(actualData!=visibleState && GetModBlock(type,actualData)->definition->invisible);
        assert(GetModBlock(type,actualData)->context.hasDummy && GetModBlock(type,actualData)->context.dummy==1);
        Bytes unknownPart; contextIntTag(unknownPart,"dummy",2);
        assert(contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,0,0,0,"fixture:split",unknownPart)}),&actualType,&actualData));
        assert(ModBlockNeedsContext(type,actualData) && GetModBlock(type,actualData)->context.dummy==2);
        assert(GetModBlock(type,actualData)->canonical=="fixture:split");
        assert(!contextReadSponge(contextSponge(v2,"fixture:split",{first,first})));
        for (int outside : {-1,2,INT_MAX})
            assert(!contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,outside,0,0,"fixture:split",dummy0)})));
        assert(!contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,0,0,0,"fixture:other",dummy0)})));
        Bytes malformed={1}; stringValue(malformed,"dummy"); malformed.push_back(0);
        assert(!contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,0,0,0,"fixture:split",malformed)})));
        // An unrelated mod may legitimately use a byte named dummy.
        assert(contextReadSponge(contextSponge(v2,"fixture:split",{first,contextEntity(format,1,0,0,"other:entity",malformed)})));
        Bytes duplicate=dummy0; duplicate.insert(duplicate.end(),dummy0.begin(),dummy0.end());
        assert(!contextReadSponge(contextSponge(v2,"fixture:split",{contextEntity(format,0,0,0,"fixture:split",duplicate)})));
        assert(contextReadSponge(contextSponge(v2,"fixture:machine",{contextEntity(format,0,0,0,"fixture:machine_dummy",position)}),&actualType,&actualData));
        assert(GetModBlock(actualType,actualData)->definition->invisible);
        assert(GetModBlock(actualType,actualData)->context.position[0]==2);
        assert(contextReadSponge(contextSponge(v2,"fixture:machine",{contextEntity(format,0,0,0,"fixture:machine_master",{})}),&actualType,&actualData));
        assert(!GetModBlock(actualType,actualData)->definition->invisible && !ModBlockNeedsContext(actualType,actualData));
        Bytes invalidPosition; invalidPosition.push_back(11); stringValue(invalidPosition,"posInMB"); contextInt(invalidPosition,2); contextInt(invalidPosition,0); contextInt(invalidPosition,0);
        assert(!contextReadSponge(contextSponge(v2,"fixture:machine",{contextEntity(format,0,0,0,"fixture:machine_dummy",invalidPosition)})));
        // Truncation must release partially allocated output arrays.
        const Bytes complete=contextSponge(v2,"fixture:split",{first});
        for (size_t size=0; size<complete.size(); ++size) {
            Bytes truncated(complete.begin(),complete.begin()+size);
            assert(!contextReadSponge(truncated));
        }
    }
    const auto chunk=[&](const std::vector<Bytes>& records) {
        Bytes bytes={10,0,0}; contextIntTag(bytes,"xPos",-2); contextIntTag(bytes,"zPos",3);
        bytes.push_back(9); stringValue(bytes,"sections"); bytes.push_back(10); contextInt(bytes,1);
        bytes.push_back(1); stringValue(bytes,"Y"); bytes.push_back((unsigned char)-4);
        bytes.push_back(10); stringValue(bytes,"block_states");
        bytes.push_back(9); stringValue(bytes,"palette"); bytes.push_back(8); contextInt(bytes,1); stringValue(bytes,"fixture:split");
        bytes.push_back(0); bytes.push_back(0);
        contextList(bytes,"block_entities",records); bytes.push_back(0); return bytes;
    };
    const auto readChunk=[&](Bytes bytes, bool valid) {
        bfFile file=buffer(bytes); file.offset=&file._offset;
        std::vector<unsigned short> grid(4096),data(4096);
        Bytes light(2048),biomes(256); BlockEntity entities[256]; int count=0,highest=EMPTY_MAX_HEIGHT; char unknown[256]={};
        const int result=nbtGetBlocks(&file,grid.data(),data.data(),light.data(),biomes.data(),entities,&count,26,-64,-49,highest,unknown,0);
        assert((result>=0)==valid);
        if (valid) {
            const int index=1*256+1*16+1;
            assert(grid[index]==type && !ModBlockNeedsContext(type,data[index]));
            assert(GetModBlock(type,data[index])->context.hasDummy);
            assert(ModBlockNeedsContext(grid[0],data[0]));
        }
    };
    const Bytes entity=contextEntity(0,-31,-63,49,"fixture:split",dummy0);
    readChunk(chunk({entity}),true);
    readChunk(chunk({entity,entity}),false);
    readChunk(chunk({contextEntity(0,-15,-63,49,"fixture:split",dummy0)}),false);
    readChunk(chunk({contextEntity(0,-31,-65,49,"fixture:split",dummy0)}),false);
    // Reject excessive declared counts before allocation or reading any entities.
    Bytes oversized={10}; contextInt(oversized,MOD_CONTEXT_MAX_ENTITIES+1);
    bfFile input=buffer(oversized); input.offset=&input._offset;
    unsigned short ignored=0;
    assert(!modReadEntityList(&input,1,&ignored,&ignored,32767,1,32767,0,0,0));
    // Bound skipped payloads as well as the small context fields we retain.
    Bytes oversizedPayload=dummy0; oversizedPayload.push_back(7); stringValue(oversizedPayload,"unused");
    contextInt(oversizedPayload,MOD_CONTEXT_MAX_BYTES);
    oversizedPayload.resize(oversizedPayload.size()+MOD_CONTEXT_MAX_BYTES,0);
    assert(!contextReadSponge(contextSponge(false,"fixture:split",{contextEntity(2,0,0,0,"fixture:split",oversizedPayload)})));
    ClearModPack();
    puts("PASS: bounded chunk and Sponge context reads, negative coordinates, identity and malformed records");
}
