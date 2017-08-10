/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "stdafx.h"
#include <string.h>

// We know we won't run into names longer than 100 characters. The old code was
// safe, but was also allocating strings all the time - seems slow.
#define MAX_NAME_LENGTH 100


static int skipType(bfFile bf, int type);
static int skipList(bfFile bf);
static int skipCompound(bfFile bf);

// return -1 if error (corrupt file)
int bfread(bfFile bf, void *target, int len)
{
    if (bf.type == BF_BUFFER) {
        memcpy(target, bf.buf + *bf.offset, len);
        *bf.offset += len;
    } else if (bf.type == BF_GZIP) {
        return gzread(bf.gz, target, len);
    }
    return len;
}

// positive number returned is offset, 0 means no movement, -1 means error (corrupt file)
int bfseek(bfFile bf, int offset, int whence)
{
    if (bf.type == BF_BUFFER) {
        if (whence == SEEK_CUR)
            *bf.offset += offset;
        else if (whence == SEEK_SET)
            *bf.offset = offset;
    } else if (bf.type == BF_GZIP) {
        return gzseek(bf.gz, offset, whence);
    }
    return offset;
} 

bfFile newNBT(const wchar_t *filename, int *err)
{
    bfFile ret;
    FILE *fptr;
    ret.type = BF_GZIP;

    *err = _wfopen_s(&fptr,filename, L"rb");
    if (fptr == NULL || *err != 0)
    {
        ret.gz = 0x0;
        return ret;
    }

    ret.gz = gzdopen(_fileno(fptr),"rb");
    ret._offset = 0;
    ret.offset = &ret._offset;
    return ret;
}

static unsigned short readWord(bfFile bf)
{
    unsigned char buf[2];
    bfread(bf, buf, 2);
    return (buf[0]<<8)|buf[1];
}
static unsigned int readDword(bfFile bf)
{
    unsigned char buf[4];
    bfread(bf, buf, 4);
    return (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
}
//static unsigned long long readLong(bfFile bf)
//{
//    int i;
//    union {
//        double f;
//        unsigned long long l;
//    } fl;
//    unsigned char buf[8];
//    bfread(bf,buf,8);
//    fl.l=0;
//    for (i=0;i<8;i++)
//    {
//        fl.l<<=8;
//        fl.l|=buf[i];
//    }
//    return fl.l;
//}
static double readDouble(bfFile bf)
{
    int i;
    union {
        double f;
        unsigned long long l;
    } fl;
    unsigned char buf[8];
    bfread(bf,buf,8);
    fl.l=0;
    for (i=0;i<8;i++)
    {
        fl.l<<=8;
        fl.l|=buf[i];
    }
    return fl.f;
}
static int skipType(bfFile bf,int type)
{
    int len;
    switch (type)
    {
    default:
        // unknown type! That's bad.
        break;
    case 0:
        // perfectly reasonable case: type 0 is end
        return 0;
    case 1: //byte
        return bfseek(bf,1,SEEK_CUR);
    case 2: //short
        return bfseek(bf, 2, SEEK_CUR);
    case 3: //int
        return bfseek(bf, 4, SEEK_CUR);
    case 4: //long
        return bfseek(bf, 8, SEEK_CUR);
    case 5: //float
        return bfseek(bf, 4, SEEK_CUR);
    case 6: //double
        return bfseek(bf, 8, SEEK_CUR);
    case 7: //byte array
        len=readDword(bf);
        return bfseek(bf, len, SEEK_CUR);
    case 8: //string
        len=readWord(bf);
        return bfseek(bf, len, SEEK_CUR);
    case 9: //list
        return skipList(bf);
    case 10: //compound
        return skipCompound(bf);
    case 11: //int array
        len=readDword(bf);
        return bfseek(bf,len*4,SEEK_CUR);
    }
    // reach here only for unknown type, in which case we can't continue
    return -1;
}
static int skipList(bfFile bf)
{
    int len,i;
    unsigned char type;
    if (bfread(bf, &type, 1) < 0) return -1;
    len=readDword(bf);
    int retcode = 1;
    switch (type)
    {
    default:
        return 0;
    case 1: //byte
        return bfseek(bf,len,SEEK_CUR);
    case 2: //short
        return bfseek(bf,len*2,SEEK_CUR);
    case 3: //int
        return bfseek(bf,len*4,SEEK_CUR);
    case 4: //long
        return bfseek(bf,len*8,SEEK_CUR);
    case 5: //float
        return bfseek(bf,len*4,SEEK_CUR);
    case 6: //double
        return bfseek(bf, len * 8, SEEK_CUR);
    case 7: //byte array
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readDword(bf);
            retcode = bfseek(bf, slen, SEEK_CUR);
        }
        return retcode;
    case 8: //string
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readWord(bf);
            retcode = bfseek(bf, slen, SEEK_CUR);
        }
        return retcode;
    case 9: //list
        for (i = 0; (i<len) && (retcode>=0); i++)
            retcode = skipList(bf);
        return retcode;
    case 10: //compound
        for (i = 0; (i<len) && (retcode>=0); i++)
            retcode = skipCompound(bf);
        return retcode;
    case 11: //int array
        for (i = 0; (i<len) && (retcode>=0); i++)
        {
            int slen=readDword(bf);
            retcode = bfseek(bf, slen * 4, SEEK_CUR);
        }
        return retcode;
    }
}
static int skipCompound(bfFile bf)
{
    int len;
    int retcode = 0;
    unsigned char type=0;
    do {
        if (bfread(bf, &type, 1) < 0) return -1;
        if (type)
        {
            len=readWord(bf);
            if (bfseek(bf, len, SEEK_CUR) < 0) return -1;	//skip name
            retcode = skipType(bf, type);
        }
    } while (type && (retcode>=0));
    return retcode;
}

// 1 they match, 0 they don't, -1 there was a read error
static int compare(bfFile bf,char *name)
{
    int ret=0;
    int len=readWord(bf);
    char thisName[MAX_NAME_LENGTH];
    if (bfread(bf, thisName, len) < 0)
        return -1;
    thisName[len]=0;
    if (strcmp(thisName,name)==0)
        ret=1;
    return ret;
}

// this finds an element in a composite list.
// it works progressively, so it only finds elements it hasn't come
// across yet.
static int nbtFindElement(bfFile bf,char *name)
{
    for (;;)
    {
        unsigned char type = 0;
        if (bfread(bf, &type, 1) < 0) return 0;
        if (type == 0) return 0;
        int retcode = compare(bf, name);
        if (retcode < 0)
            // failed to read file, so abort
            return 0;
        if (retcode > 0 )
            // found type
            return type;
        retcode = skipType(bf, type);
        if (retcode < 0) {
            // error! corrupt file found
            return -1;
        }
    }
}

// use only least significant half-byte of location, since we know what block we're in
unsigned char mod16(int val)
{
    return (unsigned char)(val & 0xf);
}

// return 0 on error
int nbtGetBlocks(bfFile bf, unsigned char *buff, unsigned char *data, unsigned char *blockLight, unsigned char *biome, BlockEntity *entities, int *numEntities)
{
    int len,nsections;
    int biome_save;
    //int found;

    //Level/Blocks
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Level")!=10)
        return -3;

    // For some reason, on most maps the biome info is before the Sections;
    // on others they're after. So, read biome data, then rewind to find Sections.
    // Format info at http://wiki.vg/Map_Format, though don't trust order.
    biome_save = *bf.offset;
    memset(biome, 0, 16*16);
    if (nbtFindElement(bf,"Biomes")!=7)
        return -4;

    {
        len=readDword(bf); //array length
        if ( bfread(bf,biome,len) < 0 ) return -5;
    }
    if (bfseek(bf, biome_save, SEEK_SET) < 0) return -6; //rewind to start of section

    if (nbtFindElement(bf,"Sections")!= 9)
        return -7;

    {
        unsigned char type=0;
        if (bfread(bf, &type, 1) < 0) return -8;
        if (type != 10)
            return -9;
    }

    memset(buff, 0, 16*16*256);
    memset(data, 0, 16*16*128);
    memset(blockLight, 0, 16*16*128);

    nsections=readDword(bf);
    if (nsections < 0) return -10;

    char thisName[MAX_NAME_LENGTH];

    // number of block sections stacked one atop the other; each is a Y slice
    while (nsections--)
    {	
        unsigned char y;
        int save = *bf.offset;
        if (nbtFindElement(bf,"Y")!=1) //which section of the block stack is this?
            return 0;
        if (bfread(bf, &y, 1) < 0) return -11;
        if (bfseek(bf, save, SEEK_SET) < 0) return -12; //rewind to start of section

        //found=0;
        // read all the arrays in this section
        for (;;)
        {
            int ret=0;
            unsigned char type=0;
            if (bfread(bf, &type, 1) < 0) return -13;
            if (type==0) 
                break;
            len=readWord(bf);
            if (bfread(bf, thisName, len) < 0) return -14;
            thisName[len]=0;
            if (strcmp(thisName,"BlockLight")==0)
            {
                //found++;
                ret=1;
                len=readDword(bf); //array length
                if (bfread(bf, blockLight + 16 * 16 * 8 * y, len) < 0) return -15;
            }
            if (strcmp(thisName,"Blocks")==0)
            {
                //found++;
                ret=1;
                len=readDword(bf); //array length
                if (bfread(bf, buff + 16 * 16 * 16 * y, len) < 0) return -16;
            }
            else if (strcmp(thisName,"Data")==0)
            {
                //found++;
                ret=1;
                len=readDword(bf); //array length
                if (bfread(bf, data + 16 * 16 * 8 * y, len) < 0) return -17;
            }
            if (!ret)
                if (skipType(bf, type) < 0) return -18;
        }
    }
    if (nbtFindElement(bf, "TileEntities") != 9)
        // all done, no TileEntities found
        return 1;

    {
        unsigned char type = 0;
        if (bfread(bf, &type, 1) < 0) return -19;
        if (type != 10)
            return 1;

        // tile entity (aka block entity) found, parse it - get number of sections that follow
        nsections = readDword(bf);

// hack - don't really want to include blockInfo.h down here.
#define BLOCK_HEAD			0x90
#define BLOCK_FLOWER_POT	0x8C

        int numSaved = 0;

        while (nsections--)
        {
            // read all the elements in this section
            bool skipSection = false;
            unsigned char dataType = 0;
            unsigned char dataRot = 0;
            unsigned char dataSkullType = 0;
            unsigned char dataFlowerPotType = 0;
            int dataX = 0;
            int dataY = 0;
            int dataZ = 0;
            int dataData = 0;
            for (;;)
            {
                unsigned char type = 0;
                if (bfread(bf, &type, 1) < 0) return -20;
                if (type == 0) {
                    // end of list, so process data, if any valid data found
                    if (!skipSection) {
                        // save skulls and flowers
                        BlockEntity *pBE = &entities[numSaved];
                        pBE->type = dataType;
                        pBE->y = (unsigned char)dataY;
                        pBE->zx = (mod16(dataZ) << 4) | mod16(dataX);
                        switch (dataType) {
                        case BLOCK_HEAD:
                            pBE->data = (unsigned char)((dataSkullType<<4) | dataRot);
                            break;
                        case BLOCK_FLOWER_POT:
                            pBE->data = (unsigned char)((dataFlowerPotType << 4) | dataData);
                            break;
                        default:
                            // should flag an error!
                            break;
                        }
                        numSaved++;
                    }
                    break;
                }

                // always read name of field
                int len = readWord(bf);
                if (bfread(bf, thisName, len) < 0) return -21;

                // if the id is one we don't care about, skip the rest of the data
                if (skipSection) {
                    if (skipType(bf, type) < 0) return -22;
                }
                else {
                    thisName[len] = 0;			
                    if (strcmp(thisName, "x") == 0 && type == 3)
                    {
                        dataX = readDword(bf);
                    }
                    else if (strcmp(thisName, "y") == 0 && type == 3)
                    {
                        dataY = readDword(bf);
                    }
                    else if (strcmp(thisName, "z") == 0 && type == 3)
                    {
                        dataZ = readDword(bf);
                    }
                    else if (strcmp(thisName, "id") == 0 && type == 8)
                    {
                        len = readWord(bf);
                        char idName[MAX_NAME_LENGTH];
                        if (bfread(bf, idName, len) < 0) return -23;
                        idName[len] = 0;

                        // is it a skull or a flowerpot?
                        if (strcmp(idName, "minecraft:skull") == 0)
                        {
                            dataType = BLOCK_HEAD;
                        }
                        else if (strcmp(idName, "minecraft:flower_pot") == 0)
                        {
                            dataType = BLOCK_FLOWER_POT;
                        }
                        else {
                            skipSection = true;
                        }
                    }
                    else if (strcmp(thisName, "Item") == 0 && type == 8)
                    {
                        len = readWord(bf);
                        char idName[MAX_NAME_LENGTH];
                        if (bfread(bf, idName, len) < 0) return -24;
                        idName[len] = 0;
                        /*
                        Flower Pot Contents
                        Contents		Item			Data
                        empty			air				0
                        poppy			red_flower		0
                        blue orchid		red_flower		1
                        allium			red_flower		2
                        houstonia		red_flower		3
                        red tulip		red_flower		4
                        orange tulip	red_flower		5
                        white tulip		red_flower		6
                        pink tulip		red_flower		7
                        oxeye daisy		red_flower		8
                        dandelion		yellow_flower	0
                        red mushroom	red_mushroom	0
                        brown mushroom	brown_mushroom	0
                        oak sapling		sapling			0
                        spruce sapling	sapling			1
                        birch sapling	sapling			2
                        jungle sapling	sapling			3
                        acacia sapling	sapling			4
                        dark oak sapling	sapling		5
                        dead bush		deadbush		0
                        fern			tallgrass		2
                        cactus			cactus			0
                        */
                        char* potName[] = {
                            "minecraft:air", "minecraft:red_flower", "minecraft:yellow_flower", "minecraft:red_mushroom", "minecraft:brown_mushroom",
                            "minecraft:sapling", "minecraft:deadbush", "minecraft:tallgrass", "minecraft:cactus"};

                        skipSection = true;
                        for (int pot = 0; pot < 9; pot++) {
                            if (strcmp(idName, potName[pot]) == 0) {
                                dataFlowerPotType = (unsigned char)pot;
                                skipSection = false;
                                break;
                            }
                        }
                    }
                    else if (strcmp(thisName, "Rot") == 0 && type == 1)
                    {
                        if (bfread(bf, &dataRot, 1) < 0) return -25;
                    }
                    else if (strcmp(thisName, "SkullType") == 0 && type == 1)
                    {
                        if (bfread(bf, &dataSkullType, 1) < 0) return -26;
                    }
                    else if (strcmp(thisName, "Data") == 0 && type == 3)
                    {
                        dataData = readDword(bf);
                    }
                    else {
                        // unused type, skip it, and skip all rest of object, since it's something we don't care about
                        // (all fields we care about are read above - if we hit one we don't care about, we know we
                        // can ignore the rest).
                        skipType(bf, type);
                        skipSection = true;
                    }
                }
            }
        }

        if (numSaved > 0) {
            *numEntities = numSaved;
        }
    }
    return 1;
}

int nbtGetSpawn(bfFile bf, int *x, int *y, int *z)
{
    int len;
    *x=*y=*z=0;
    //Data/SpawnX
    // don't really need this first seek to beginning of file
    //bfseek(bf,0,SEEK_SET);
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"SpawnX")!=3) return -4;
    *x=readDword(bf);

    // Annoyingly, SpawnY can come before SpawnX, so we need to re-find each time.
    // For some reason, seeking to a stored offset does not work.
    // So we seek to beginning of file and find "Data" again.
    if (bfseek(bf, 0, SEEK_SET) < 0) return -5;
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -6; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -7; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -8;
    if (nbtFindElement(bf,"SpawnY")!=3) return -9;
    *y=readDword(bf);

    // We seek to beginning of file and find "Data" again.
    if (bfseek(bf, 0, SEEK_SET) < 0) return -10;
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -11; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -12; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -13;
    if (nbtFindElement(bf,"SpawnZ")!=3) return -14;
    *z=readDword(bf);
    return 0;
}

//  The NBT version of the level, 19133. See http://minecraft.gamepedia.com/Level_format#level.dat_format
int nbtGetFileVersion(bfFile bf, int *version)
{
    *version = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"version")!=3) return -4;
    *version=readDword(bf);
    return 0;
}

// From Version, not version, see http://minecraft.gamepedia.com/Level_format#level.dat_format at bottom
// This is a newer tag for 1.9 and on, older worlds do not have them
int nbtGetFileVersionId(bfFile bf, int *versionId)
{
    *versionId = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, "Data") != 10) return -3;
    if (nbtFindElement(bf, "Version") != 10) return -4;
    if (nbtFindElement(bf, "Id") != 3) return -5;
    *versionId = readDword(bf);
    return 0;
}

int nbtGetFileVersionName(bfFile bf, char *versionName, int stringLength)
{
    *versionName = '\0'; // initialize to empty string
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, "Data") != 10) return -3;
    if (nbtFindElement(bf, "Version") != 10) return -4;
    if (nbtFindElement(bf, "Name") != 8) return -5;
    len = readWord(bf);
    if (len < stringLength) {
        if (bfread(bf, versionName, len) < 0) return -6;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(bf, versionName, stringLength - 1) < 0) return -7;	// save last position for string terminator
        if (bfseek(bf, len - stringLength + 1, SEEK_CUR) < 0) return -8;
        len = stringLength - 1;
    }
    versionName[len] = 0;
    return 0;
}

int nbtGetLevelName(bfFile bf, char *levelName, int stringLength)
{
    *levelName = '\0'; // initialize to empty string
    int len;
    //Data/levelName
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    // 8 means a string
    if (nbtFindElement(bf,"LevelName")!=8) return -4;
    len=readWord(bf);
    if (len < stringLength) {
        if (bfread(bf, levelName, len) < 0) return -5;
    }
    else {
        // string too long; read some, discard the rest
        if (bfread(bf, levelName, stringLength - 1) < 0) return -6;	// save last position for string terminator
        if (bfseek(bf, len - stringLength + 1, SEEK_CUR) < 0) return -7;
        len = stringLength - 1;
    }
    levelName[len] = 0;
    return 0;
}

//void nbtGetRandomSeed(bfFile bf,long long *seed)
//{
//    int len;
//    *seed=0;
//    //Data/RandomSeed
//    bfseek(bf,1,SEEK_CUR); //skip type
//    len=readWord(bf); //name length
//    bfseek(bf,len,SEEK_CUR); //skip name ()
//    if (nbtFindElement(bf,"Data")!=10) return;
//    if (nbtFindElement(bf,"RandomSeed")!=4) return;
//    *seed=readLong(bf);
//}
int nbtGetPlayer(bfFile bf, int *px, int *py, int *pz)
{
    int len;
    *px=*py=*pz=0;
    //Data/Player/Pos
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len=readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf,"Data")!=10) return -3;
    if (nbtFindElement(bf,"Player")!=10) return -4;
    if (nbtFindElement(bf,"Pos")!=9) return -5;
    if (bfseek(bf, 5, SEEK_CUR) < 0) return -6; //skip subtype and num items
    *px=(int)readDouble(bf);
    *py=(int)readDouble(bf);
    *pz=(int)readDouble(bf);
    return 0;
}

//////////// schematic
//  http://minecraft.gamepedia.com/Schematic_file_format
// return 0 if not found, 1 if all is well
int nbtGetSchematicWord(bfFile bf, char *field, int *value)
{
    *value = 0x0; // initialize
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()
    if (nbtFindElement(bf, field) != 2) return -3;
    *value = readWord(bf);
    return 1;
}

// return 1 on success
int nbtGetSchematicBlocksAndData(bfFile bf, int numBlocks, unsigned char *schematicBlocks, unsigned char *schematicBlockData)
{
    int len;
    //Data/version
    if (bfseek(bf, 1, SEEK_CUR) < 0) return -1; //skip type
    len = readWord(bf); //name length
    if (bfseek(bf, len, SEEK_CUR) < 0) return -2; //skip name ()

    int found = 0;
    while (found < 2)
    {
        int ret = 0;
        unsigned char type = 0;
        if (bfread(bf, &type, 1) < 0) return -3;
        if (type == 0)
            break;
        len = readWord(bf);
        char thisName[MAX_NAME_LENGTH];
        if (bfread(bf, thisName, len) < 0) return -4;
        thisName[len] = 0;
        if (strcmp(thisName, "Blocks") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(bf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(bf, schematicBlocks, len) < 0) return -5;
            found++;
        }
        else if (strcmp(thisName, "Data") == 0)
        {
            //found++;
            ret = 1;
            len = readDword(bf); //array length
            // check that array is the right size
            if (len != numBlocks)
                return 0;
            if (bfread(bf, schematicBlockData, len) < 0) return -6;
            found++;
        }
        if (!ret)
            if (skipType(bf, type) < 0) return -7;
    }
    // Did we find two things? If so, return 1, success
    return ((found == 2) ? 1 : -8);
}


void nbtClose(bfFile bf)
{
    if (bf.type == BF_GZIP)
        gzclose(bf.gz);
}
