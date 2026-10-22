/*
Copyright (c) 2011, Ryan Hitchman
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

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

/*

Region File Format

Concept: The minimum unit of storage on hard drives is 4KB. 90% of Minecraft
chunks are smaller than 4KB. 99% are smaller than 8KB. Write a simple
container to store chunks in single files in runs of 4KB sectors.

Each region file represents a 32x32 group of chunks. The conversion from
chunk number to region number is floor(coord / 32); a chunk at (30, -3)
would be in region (0, -1), and one at (70, -30) would be at (3, -1).
Region files are named "r.x.z.mca", where x and z are the region coordinates.

A region file begins with an 8KB header that describes where chunks are stored
in the file and when they were last modified. A 4-byte big-endian integer
represents sector offsets and sector counts. The chunk offset for a chunk
located at (x, z) begins at byte 4*(x+z*32) in the file. The bottom byte of
the chunk offset indicates the number of sectors the chunk takes up,and
the top 3 bytes represent the sector number of the chunk. Given a chunk
offset o, the chunk data begins at byte 4096*(o/256) and takes up at
most 4096*(o%256) bytes. A chunk cannot exceed 1MB in size. A chunk offset
of 0 indicates a missing chunk.

The 4-byte big-endian modification time for a chunk (x,z) begins at byte
4096+4*(x+z*32) in the file. The time is stored as the number of seconds
since Jan 1, 1970 that the chunk was last written (aka Unix Time).

Chunk data begins with a 4-byte big-endian integer representing the chunk data
length in bytes, not counting the length field. The length must be smaller than
4096 times the number of sectors. The next byte is a version number, to allow
backwards-compatible updates to how chunks are encoded.

A version number of 1 is never used, for obscure historical reasons.

A version number of 2 represents a deflated (zlib compressed) NBT file. The
deflated data is the chunk length - 1.

*/

#include "stdafx.h"

#define CHUNK_DEFLATE_MAX (1024 * 1024)  // 1MB limit for compressed chunks
// had to kick this up due to F Seaworld 1.18 world test
#define CHUNK_INFLATE_MAX (20 * 1024 * 1024) // 20MB limit for inflated chunks

static z_stream strm;
static int strm_initialized = 0;

// Region files are kept open, up to REGION_FILE_CACHE_SIZE of them, each with its 4 KB table of where its chunks are: opening
// a file and reading its table for every chunk took longer than reading and decompressing the chunks themselves. A file that
// doesn't exist is remembered, too, e.g. the "entities" file of a region without entities. Minecraft may be saving the world
// while Mineways reads it, which can move chunks within a file, so a file's table is read again when its size or last-written
// time changes, or when a chunk read from it doesn't check out, and a missing file is looked for again after a few seconds.
// regionCleanup() closes them all.
#define REGION_FILE_CACHE_SIZE 32
#define REGION_TABLE_SIZE 4096
#define REGION_MISSING_RECHECK_MS 5000

typedef struct RegionFile {
    wchar_t name[256];
    bool used;
    bool exists;
    HANDLE h;
    unsigned long long size;        // the file's size and last-written time when its table was read
    unsigned long long writeTime;
    DWORD checkedTick;              // when a missing file was last looked for
    unsigned int lastUse;
    unsigned char table[REGION_TABLE_SIZE]; // each chunk's location: a 3-byte 4 KB sector offset and a 1-byte sector count
} RegionFile;
static RegionFile gRegionFiles[REGION_FILE_CACHE_SIZE];
static unsigned int gRegionFileUse = 0;

// Get the file's size and last-written time, to tell if it has changed.
static bool regionFileStamp(HANDLE h, unsigned long long* size, unsigned long long* writeTime)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info))
        return false;
    *size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    *writeTime = ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    return true;
}

// Read the file's table of chunk locations. A file too short to hold the whole table, e.g. an empty one, has no chunks.
static void readRegionTable(RegionFile* rf)
{
    DWORD br = 0;
    if (SetFilePointer(rf->h, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(rf->h, rf->table, REGION_TABLE_SIZE, &br, NULL) || br < REGION_TABLE_SIZE)
        memset(rf->table, 0, REGION_TABLE_SIZE);
    if (!regionFileStamp(rf->h, &rf->size, &rf->writeTime))
        rf->size = rf->writeTime = 0;
}

static void openRegionFile(RegionFile* rf)
{
    // FILE_SHARE_DELETE, too, so that the world can still be deleted or renamed while Mineways has the file open
    rf->h = CreateFileW(rf->name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    rf->exists = (rf->h != INVALID_HANDLE_VALUE);
    rf->checkedTick = GetTickCount();
    if (rf->exists)
        readRegionTable(rf);
}

// Find the region file in the cache, or open it, replacing the least recently used one, and make sure its table is current.
static RegionFile* getRegionFile(const wchar_t* filename)
{
    RegionFile* rf = NULL;
    RegionFile* victim = &gRegionFiles[0];
    for (int i = 0; i < REGION_FILE_CACHE_SIZE; i++) {
        RegionFile* e = &gRegionFiles[i];
        if (e->used && wcscmp(e->name, filename) == 0) {
            rf = e;
            break;
        }
        if (!e->used || (victim->used && e->lastUse < victim->lastUse))
            victim = e;
    }
    if (rf == NULL) {
        rf = victim;
        if (rf->used && rf->exists)
            CloseHandle(rf->h);
        wcscpy_s(rf->name, 256, filename);
        rf->used = true;
        openRegionFile(rf);
    }
    else if (!rf->exists) {
        // Minecraft may have made it since
        if (GetTickCount() - rf->checkedTick >= REGION_MISSING_RECHECK_MS)
            openRegionFile(rf);
    }
    else {
        // has Minecraft changed the file since its table was read?
        unsigned long long size, writeTime;
        if (!regionFileStamp(rf->h, &size, &writeTime) || size != rf->size || writeTime != rf->writeTime)
            readRegionTable(rf);
    }
    rf->lastUse = ++gRegionFileUse;
    return rf;
}

// subdirectory is "region" for the blocks, "entities" for the entities (1.17 on)
static int regionPrepareBuffer(bfFile & bf, wchar_t* directory, const wchar_t* subdirectory, int cx, int cz)
{
    wchar_t filename[256];
    static unsigned char buf[CHUNK_DEFLATE_MAX];
    static unsigned char out[CHUNK_INFLATE_MAX];

    // the .mca file for the chunk - note we get the new mca 1.2 file type here!
    swprintf_s(filename, 256, L"%s%s/r.%d.%d.mca", directory, subdirectory, cx >> 5, cz >> 5);

    RegionFile* rf = getRegionFile(filename);
    // the .mca doesn't actually exist: no data -> nothing to do, but don't flag an error
    if (!rf->exists)
        return 0;

    if (!strm_initialized) {
        // we re-use dynamically allocated memory
        strm.zalloc = (alloc_func)NULL;
        strm.zfree = (free_func)NULL;
        strm.opaque = NULL;
        if (inflateInit(&strm) != Z_OK)
            return ERROR_INFLATE;
        strm_initialized = 1;
    }

    // If the chunk doesn't check out, the file may have changed since its table was read: read the table again and try once more.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0)
            readRegionTable(rf);

        const unsigned char* entry = rf->table + 4 * ((cx & 31) + (cz & 31) * 32);
        int sectorNumber = entry[3]; // how many 4096B sectors the chunk takes up
        int offset = (entry[0] << 16) | (entry[1] << 8) | entry[2]; // 4KB sector the chunk is in
        if (offset == 0)
            return 0; // an empty chunk
        if (sectorNumber * 4096 > CHUNK_DEFLATE_MAX)
            return 0;

        // read the chunk in one shot. The last chunk in a file may not fill its last sector.
        DWORD br = 0;
        if (SetFilePointer(rf->h, 4096 * offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
            !ReadFile(rf->h, buf, 4096 * sectorNumber, &br, NULL) || br < 5)
            continue;

        int chunkLength = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

        // sanity check chunk size: it must all have been read
        if (chunkLength < 1 || chunkLength > CHUNK_DEFLATE_MAX || 4 + chunkLength > (int)br)
            continue;

        // only handle zlib-compressed chunks (v2)
        if (buf[4] != 2)
            continue;

        // decompress chunk
        strm.next_out = out;
        strm.avail_out = CHUNK_INFLATE_MAX;
        strm.avail_in = chunkLength - 1;
        strm.next_in = buf + 5;

        inflateReset(&strm);
        int status = inflate(&strm, Z_FINISH); // decompress in one step

        if (status != Z_STREAM_END) {
            // error inflating (not enough space? Increase CHUNK_INFLATE_MAX, ugh) - or a stale table
            if (attempt > 0)
                return ERROR_INFLATE;
            continue;
        }

        // the uncompressed chunk data is now in "out", with length strm.avail_out

        bf.type = BF_BUFFER;
        bf.buf = out;
        bf.buflen = CHUNK_INFLATE_MAX - (int)strm.avail_out;
        bf._offset = 0;
        bf.offset = &bf._offset;

        // all's fine
        return 1;
    }
    // the chunk's data doesn't check out
    return 0;
}

// directory: the base world directory, e.g. "/home/ryan/.minecraft/saves/World1/" - note the trailing "/" is in place
// cx, cz: the chunk's x and z offset
// block: a 32KB buffer to write block data into
// blockLight: a 16KB buffer to write block light into (not skylight)
//
// returns 1 on success, 0 on error or nothing found
int regionGetBlocks(wchar_t* directory, int cx, int cz, unsigned char* block, unsigned short* data, unsigned char* blockLight, unsigned char* biome, BlockEntity* entities, int* numEntities, int mcVersion, int minHeight, int maxHeight, int & mfsHeight, char* unknownBlock, int unknownBlockID)
{
    bfFile bf;

    int errCode = regionPrepareBuffer(bf, directory, L"region", cx, cz);
    if (errCode <= 0) {
        // failed
        return errCode < 0 ? ERROR_INFLATE : 0;
    }

    return nbtGetBlocks(&bf, block, data, blockLight, biome, entities, numEntities, mcVersion, minHeight, maxHeight, mfsHeight, unknownBlock, unknownBlockID);
}

int regionTestHeights(wchar_t* directory, int& minHeight, int& maxHeight, int mcVersion, int cx, int cz)
{
    bfFile bf;

    int errCode = regionPrepareBuffer(bf, directory, L"region", cx, cz);
    if (errCode <= 0) {
        // failed
        return errCode < 0 ? ERROR_INFLATE : 0;
    }

    return nbtGetHeights(&bf, minHeight, maxHeight, mcVersion);
}

int regionGetCushions(wchar_t* directory, int cx, int cz, CushionEntity* cushions, int maxCushions)
{
    bfFile bf;

    int errCode = regionPrepareBuffer(bf, directory, L"entities", cx, cz);
    if (errCode <= 0) {
        // no entities for this chunk, or an error
        return 0;
    }

    return nbtGetCushions(&bf, cushions, maxCushions);
}

void regionCleanup()
{
    // close the region files: the world is closing, or changing
    for (int i = 0; i < REGION_FILE_CACHE_SIZE; i++) {
        if (gRegionFiles[i].used && gRegionFiles[i].exists)
            CloseHandle(gRegionFiles[i].h);
        gRegionFiles[i].used = false;
    }

    if (strm_initialized) {
        inflateEnd(&strm);
        strm_initialized = 0;
    }
}

