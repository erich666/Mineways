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
#define CHUNK_INFLATE_MAX (1024 * 2048) // 2MB limit for inflated chunks

#define RERROR(x) if(x) { PortaClose(regionFile); return 0; }

static int regionPrepareBuffer(bfFile & bf, wchar_t* directory, int cx, int cz)
{
    wchar_t filename[256];
    PORTAFILE regionFile;
#ifdef WIN32
    DWORD br;
#endif
    static unsigned char buf[CHUNK_DEFLATE_MAX];
    static unsigned char out[CHUNK_INFLATE_MAX];

    int sectorNumber, offset, chunkLength;

    int status;

    static z_stream strm;
    static int strm_initialized = 0;

    // open the region file - note we get the new mca 1.2 file type here!
    swprintf_s(filename, 256, L"%sregion/r.%d.%d.mca", directory, cx >> 5, cz >> 5);

    regionFile = PortaOpen(filename);
    if (regionFile == INVALID_HANDLE_VALUE)
        return 0;

    // seek to the chunk offset
    RERROR(PortaSeek(regionFile, 4 * ((cx & 31) + (cz & 31) * 32)));

    // get the chunk offset
    RERROR(PortaRead(regionFile, buf, 4));

    sectorNumber = buf[3]; // how many 4096B sectors the chunk takes up
    offset = (buf[0] << 16) | (buf[1] << 8) | buf[2]; // 4KB sector the chunk is in

    RERROR(offset == 0); // an empty chunk

    RERROR(PortaSeek(regionFile, 4096 * offset));

    RERROR(sectorNumber * 4096 > CHUNK_DEFLATE_MAX);

    // read chunk in one shot
    // this is faster than reading the header and data separately
    RERROR(PortaRead(regionFile, buf, 4096 * sectorNumber));

    chunkLength = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

    // sanity check chunk size
    RERROR(chunkLength > sectorNumber * 4096 || chunkLength > CHUNK_DEFLATE_MAX);

    // only handle zlib-compressed chunks (v2)
    RERROR(buf[4] != 2);

    PortaClose(regionFile);

    // decompress chunk


    if (!strm_initialized) {
        // we re-use dynamically allocated memory
        strm.zalloc = (alloc_func)NULL;
        strm.zfree = (free_func)NULL;
        strm.opaque = NULL;
        inflateInit(&strm);
        strm_initialized = 1;
    }

    strm.next_out = out;
    strm.avail_out = CHUNK_INFLATE_MAX;
    strm.avail_in = chunkLength - 1;
    strm.next_in = buf + 5;

    inflateReset(&strm);
    status = inflate(&strm, Z_FINISH); // decompress in one step

    if (status != Z_STREAM_END) // error inflating (not enough space?)
        return 0;

    // the uncompressed chunk data is now in "out", with length strm.avail_out

    bf.type = BF_BUFFER;
    bf.buf = out;
    bf._offset = 0;
    bf.offset = &bf._offset;

    // all's fine
    return 1;
}

// directory: the base world directory, e.g. "/home/ryan/.minecraft/saves/World1/" - note the trailing "/" is in place
// cx, cz: the chunk's x and z offset
// block: a 32KB buffer to write block data into
// blockLight: a 16KB buffer to write block light into (not skylight)
//
// returns 1 on success, 0 on error or nothing found
int regionGetBlocks(wchar_t* directory, int cx, int cz, unsigned char* block, unsigned char* data, unsigned char* blockLight, unsigned char* biome, BlockEntity* entities, int* numEntities, int mcVersion, int minHeight, int maxHeight, int & mfsHeight, char* unknownBlock)
{
    bfFile bf;

    if (regionPrepareBuffer(bf, directory, cx, cz) == 0) {
        // failed
        return 0;
    }

    return nbtGetBlocks(&bf, block, data, blockLight, biome, entities, numEntities, mcVersion, minHeight, maxHeight, mfsHeight, unknownBlock);
}

int regionTestHeights(wchar_t* directory, int& minHeight, int& maxHeight, int mcVersion, int cx, int cz)
{
    bfFile bf;

    if (regionPrepareBuffer(bf, directory, cx, cz) == 0) {
        // failed
        return 0;
    }

    return nbtGetHeights(&bf, minHeight, maxHeight, mcVersion);
}

