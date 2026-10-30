#!/usr/bin/env python3
"""Portable checks of the Windows map/ZIP and schematic allocation boundaries.

Pass a disposable build directory. This does not replace a native Windows build.
"""
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=True)


def body(path, signature):
    return source_body((ROOT / path).read_text(), signature)


def source_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


common = r'''
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
'''

map_source = common + r'''
struct WorldGuide {};
struct Options {};
using ProgressCallback = void (*)(float, wchar_t*);
static bool gColorsInited = false;
static bool gDarkBlankTiles = false;
static void SetMapDarkBlankTiles(bool dark) { gDarkBlankTiles = dark; }
static int drawCalls, highlightCalls, writeCalls;
static void initColors() { gColorsInited = true; }
static unsigned char* draw(WorldGuide*, int, int, int, int, Options*, ProgressCallback,
                          float, float&, int*, int, int, int& result) {
    assert(!gDarkBlankTiles);
    static unsigned char pixels[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i) {
        pixels[i * 4] = 7; pixels[i * 4 + 1] = 11; pixels[i * 4 + 2] = 13;
    }
    ++drawCalls;
    result = 0;
    return pixels;
}
'''
map_source += body("Win/MinewaysMap.cpp", "bool GetMapImageSize(") + "\n"
map_source += body("Win/MinewaysMap.cpp", "int DrawMapToArray(") + "\n"
map_source += r'''
struct progimage_info { int width, height; std::vector<unsigned char> image_data; };
static double gCurScale = 1;
static WorldGuide gWorldGuide;
static Options gOptions;
static int gTargetDepth, gMinHeight, gMaxHeight = 255, gCurDepth, gMinecraftVersion, gVersionID;
static int gHighlightOn = 1, gHitsFound[3];
static constexpr int MB_OK = 0, MB_ICONERROR = 0, HIGHLIGHT_UNDO_IGNORE = 0;
static void updateProgress(float, wchar_t*) {}
static void FilterMessageBox(void*, const wchar_t*, const wchar_t*, int) {}
static void SetHighlightState(int, int, int, int, int, int, int, int, int, int) { ++highlightCalls; }
static void ClearUnknownBlockNameString() {}
static void checkMapDrawErrorCode(int ret) { assert(ret == 0); }
static void EnsureSuffix(wchar_t* dest, const wchar_t* src, const wchar_t*) { wcscpy(dest, src); }
static int writepng(progimage_info* image, int channels, wchar_t*) {
    assert(channels == 3 && image->image_data.size() == (size_t)image->width * image->height * 3);
    ++writeCalls;
    return 0;
}
static constexpr int MAX_PATH_AND_FILE = 520;
'''
map_source += body("Win/Mineways.cpp", "static bool saveMapFile(int xmin, int zmin, int xmax, int ymax, int zmax, wchar_t* mapFileName)\n{")
map_source += r'''
int main() {
    size_t bytes = 123;
    assert(!GetMapImageSize(0, 1, 1, bytes) && bytes == 0);
    assert(!GetMapImageSize(1, -1, 1, bytes));
    assert(!GetMapImageSize(1, 1, 0, bytes));
    assert(!GetMapImageSize(INT_MAX, INT_MAX, INT_MAX, bytes));
    assert(!GetMapImageSize(65536, 65536, 1, bytes));
    assert(GetMapImageSize(357913941, 1, 1, bytes) && bytes == 1073741823);
    assert(!GetMapImageSize(357913942, 1, 1, bytes));
    unsigned char small[6] = {};
    int before = drawCalls;
    assert(DrawMapToArray(small, sizeof(small), &gWorldGuide, INT_MAX, 0, 70, 255,
                         2, 1, 1, &gOptions, gHitsFound, nullptr, 0, 0) < 0);
    assert(drawCalls == before);
    for (int w : {1, 17, 33}) for (int h : {1, 19}) for (int zoom : {1, 2, 7}) {
        assert(GetMapImageSize(w, h, zoom, bytes));
        for (bool dark : {false, true})
        for (int origin : {INT_MIN, -17, -16, -1, 0, 1, 15, 16, INT_MAX - 32}) {
            gDarkBlankTiles = dark;
            std::vector<unsigned char> pixels(bytes + 32, 0xa5);
            assert(DrawMapToArray(pixels.data() + 16, bytes, &gWorldGuide, origin, origin,
                                 70, 255, w, h, zoom, &gOptions, gHitsFound, nullptr, 0, 0) == 0);
            assert(gDarkBlankTiles == dark);
            for (size_t i = 0; i < bytes; ++i)
                assert(pixels[i + 16] == (i % 3 == 0 ? 13 : i % 3 == 1 ? 11 : 7));
            for (int i = 0; i < 16; ++i)
                assert(pixels[i] == 0xa5 && pixels[bytes + 16 + i] == 0xa5);
            int calls = drawCalls;
            assert(DrawMapToArray(pixels.data(), bytes - 1, &gWorldGuide, 0, 0, 70, 255,
                                 w, h, zoom, &gOptions, gHitsFound, nullptr, 0, 0) < 0);
            assert(drawCalls == calls);
        }
    }
    wchar_t path[] = L"map.png";
    assert(!saveMapFile(INT_MIN, 0, INT_MAX, 70, 1, path));
    assert(!saveMapFile(0, 0, 65535, 70, 65535, path));
    for (double scale : {-1.0, (double)INFINITY, (double)NAN, (double)INT_MAX}) {
        gCurScale = scale;
        assert(!saveMapFile(0, 0, 1, 70, 1, path));
    }
    assert(highlightCalls == 0 && writeCalls == 0);
    for (double scale : {0.0, 0.2, 1.0, 3.0}) {
        gCurScale = scale;
        assert(saveMapFile(3, 4, -2, 70, -5, path));
    }
    assert(highlightCalls == 8 && writeCalls == 4);
}
'''

zip_prefix = body("Win/XZip.cpp", "ZRESULT TZip::Add(").split("// now open whatever was our input source:")[0]
zip_prefix += "++openCalls; return ZR_OK;\n}"
zip_source = common + r'''
#define MAX_PATH 260
using DWORD = unsigned long;
using ZRESULT = int;
using HZIP = void*;
constexpr int ZIP_FILENAME = 1, ZIP_FOLDER = 2, ZIP_MEMORY = 3;
constexpr int ZR_OK = 0, ZR_ARGS = 1, ZR_FAILED = 2, ZR_ENDED = 3, ZR_ZMODE = 4;
constexpr int DEFLATE = 8, STORE = 0;
static int openCalls, lasterrorZ;
static bool HasZipSuffix(const char*) { return false; }
struct TZip {
    bool oerr = false, hasputcen = false;
    ZRESULT Add(const char*, void*, unsigned int, DWORD);
};
struct TZipHandleData { int flag; TZip* zip; };
#ifdef _UNICODE
using TCHAR = wchar_t;
using LPCWSTR = const wchar_t*;
constexpr int CP_ACP = 0;
// A deterministic DBCS substitute checks the wrapper's encoded byte contract.
static int WideCharToMultiByte(int, int, LPCWSTR src, int, char* dest, int capacity, void*, void*) {
    int count = 0;
    for (; *src; ++src) {
        int width = *src > 127 ? 2 : 1;
        if (count + width >= capacity) return 0;
        for (int i = 0; i < width; ++i) dest[count++] = 'x';
    }
    dest[count++] = 0;
    return count;
}
#else
using TCHAR = char;
#endif
'''
zip_source += zip_prefix + "\n" + body("Win/XZip.cpp", "ZRESULT ZipAdd(")
zip_source += r'''
int main() {
    TZip zip;
    TZipHandleData handle{2, &zip};
    for (const char* name : {"mesh.obj", "textures\\tile.png"})
        assert(zip.Add(name, nullptr, 0, ZIP_MEMORY) == ZR_OK);
    int calls = openCalls;
    assert(zip.Add(nullptr, nullptr, 0, ZIP_MEMORY) == ZR_ARGS);
    assert(zip.Add("", nullptr, 0, ZIP_MEMORY) == ZR_ARGS);
    assert(zip.Add(std::string(260, 'a').c_str(), nullptr, 0, ZIP_MEMORY) == ZR_ARGS);
    assert(zip.Add(std::string(259, 'a').c_str(), nullptr, 0, ZIP_FOLDER) == ZR_ARGS);
    assert(openCalls == calls);
    assert(zip.Add(std::string(259, 'a').c_str(), nullptr, 0, ZIP_MEMORY) == ZR_OK);
    assert(zip.Add((std::string(258, 'a') + "/").c_str(), nullptr, 0, ZIP_FOLDER) == ZR_OK);
    assert(zip.Add(std::string(258, 'a').c_str(), nullptr, 0, ZIP_FOLDER) == ZR_OK);
    assert(ZipAdd(&handle, nullptr, nullptr, 0, ZIP_FILENAME) == ZR_ARGS);
#ifdef _UNICODE
    assert(ZipAdd(&handle, L"mesh.obj", nullptr, 0, ZIP_FILENAME) == ZR_OK);
    assert(ZipAdd(&handle, std::wstring(129, 0x3000).c_str(), nullptr, 0, ZIP_FILENAME) == ZR_OK);
    calls = openCalls;
    assert(ZipAdd(&handle, std::wstring(130, 0x3000).c_str(), nullptr, 0, ZIP_FILENAME) == ZR_ARGS);
    assert(ZipAdd(&handle, std::wstring(600, L'a').c_str(), nullptr, 0, ZIP_FILENAME) == ZR_ARGS);
#else
    assert(ZipAdd(&handle, "mesh.obj", nullptr, 0, ZIP_FILENAME) == ZR_OK);
    calls = openCalls;
    assert(ZipAdd(&handle, std::string(600, 'a').c_str(), nullptr, 0, ZIP_FILENAME) == ZR_ARGS);
#endif
    assert(openCalls == calls);
}
'''

allocation_source = common + r'''
constexpr int WORLD_UNLOADED_TYPE = 0, WORLD_LEVEL_TYPE = 1, WORLD_TEST_BLOCK_TYPE = 2;
constexpr int WORLD_SCHEMATIC_TYPE = 3, NUM_BLOCKS_DEFINED = 561;
struct Schematic {
    unsigned char* blocks; unsigned short* data;
    int width, height, length, numBlocks; bool repeat;
};
struct WorldGuide { int type, minHeight, maxHeight; Schematic sch; };
struct WorldBlock {
    unsigned char* grid; unsigned short* data; unsigned char* light;
    unsigned char biome[256]; int heightAlloc, maxFilledSectionHeight, maxFilledHeight;
    explicit WorldBlock(int height) : heightAlloc(height) {
        grid = new unsigned char[height * 256];
        data = new unsigned short[height * 256];
        light = new unsigned char[height * 128];
    }
    ~WorldBlock() { delete[] grid; delete[] data; delete[] light; }
};
struct LoadContext { bool onLoaderThread = false, outOfMemory = false; };
static int failures, allocations, cacheClears, requestedMin, requestedMax;
static WorldBlock* block_alloc(int low, int high) {
    ++allocations; requestedMin = low; requestedMax = high;
    if (failures-- > 0) return nullptr;
    return new WorldBlock(high - low + 1);
}
static void Cache_Empty() { ++cacheClears; }
'''
allocation_prefix = body("Win/MinewaysMap.cpp", "static WorldBlock* loadBlock(WorldGuide* pWorldGuide, int cx, int cz, int mcVersion, int versionID, int& retCode, LoadContext& lc)\n{")
allocation_source += allocation_prefix.split("// always set")[0] + "return block;\n}\n"
allocation_source += body("Win/MinewaysMap.cpp", "int createBlockFromSchematic(WorldGuide* pWorldGuide, int cx, int cz, WorldBlock* block)\n{")
allocation_source += r'''
int main() {
    std::vector<unsigned char> blocks(600, 1);
    std::vector<unsigned short> data(600, 2);
    WorldGuide guide{WORLD_SCHEMATIC_TYPE, 0, 255, {blocks.data(), data.data(), 1, 600, 1, 600, false}};
    int result;
    for (int fail : {0, 1}) for (bool repeat : {false, true}) {
        failures = fail; allocations = cacheClears = 0;
        LoadContext context;
        guide.sch.repeat = repeat;
        WorldBlock* block = loadBlock(&guide, 0, 0, 0, 0, result, context);
        assert(block && block->heightAlloc == 600 && requestedMin == 0 && requestedMax == 599);
        assert(allocations == fail + 1 && cacheClears == fail);
        assert(createBlockFromSchematic(&guide, 0, 0, block) == 1);
        assert(block->grid[599 * 256] == 1 && block->data[599 * 256] == 2);
        delete block;
    }
    failures = 2; allocations = cacheClears = 0;
    LoadContext context;
    assert(!loadBlock(&guide, 0, 0, 0, 0, result, context));
    assert(allocations == 2 && cacheClears == 1);
    failures = 1; allocations = cacheClears = 0;
    context.onLoaderThread = true;
    assert(!loadBlock(&guide, 0, 0, 0, 0, result, context));
    assert(context.outOfMemory && allocations == 1 && cacheClears == 0);
    guide.type = WORLD_LEVEL_TYPE; guide.minHeight = -64; guide.maxHeight = 319;
    context = {}; failures = 1; allocations = cacheClears = 0;
    WorldBlock* block = loadBlock(&guide, 0, 0, 0, 0, result, context);
    assert(block && block->heightAlloc == 384 && requestedMin == -64 && requestedMax == 319);
    assert(allocations == 2 && cacheClears == 1);
    delete block;
}
'''

zip_cleanup_source = common + r'''
#define _T(text) L##text
#define _countof(array) (sizeof(array) / sizeof(array[0]))
#define swprintf_s swprintf
using DWORD = unsigned long;
using HZIP = void*;
constexpr int ZR_OK = 0, ZIP_FILENAME = 1, MAX_PATH_AND_FILE = 520;
constexpr int MW_BEGIN_ERRORS = 1 << 11, MW_CANNOT_WRITE_TO_FILE = 1 << 12;
constexpr int MB_OK = 0, MB_ICONERROR = 0;
struct ExportData { int fileType = 0; int chkCreateModelFiles[1] = {}, chkCreateZip[1] = {1}; };
static ExportData exportData;
static ExportData* gpEFD = &exportData;
static wchar_t mainFile[] = L"mesh.obj", materialFile[] = L"mesh.mtl", tileFile[] = L"textures/tile.png";
struct FileList { int count; wchar_t* name[3]; };
static FileList outputFileList{3, {mainFile, materialFile, tileFile}};
static int failAdd, addCalls, sourceDeletes, directoryDeletes, closeCalls;
static bool failClose, failCreate, closed;
static void (*updateProgress)(float, wchar_t*) = nullptr;
static HZIP CreateZip(const wchar_t*, int, int) { return failCreate ? nullptr : &exportData; }
static int ZipAdd(HZIP zip, const wchar_t*, const wchar_t*, int, int) {
    assert(!closed);
    ++addCalls;
    return !zip || addCalls == failAdd ? 1 : ZR_OK;
}
static int CloseZip(HZIP zip) { ++closeCalls; closed = true; return !zip || failClose ? 1 : ZR_OK; }
static void DeleteFile(const wchar_t* path) {
    if (std::wstring(path).find(L".zip") != std::wstring::npos) return;
    assert(closed && !failClose && !failCreate && failAdd == 0 && addCalls == outputFileList.count);
    ++sourceDeletes;
}
static void RemoveDirectory(const wchar_t*) { assert(sourceDeletes == 3); ++directoryDeletes; }
static DWORD GetLastError() { return 1; }
static void FilterMessageBox(void*, const wchar_t*, const wchar_t*, int) {}
static const wchar_t* removePath(const wchar_t* path) { return path; }
static wchar_t* RemoveGivenPath(wchar_t* path, const wchar_t*) { return path; }
static void StripLastString(const wchar_t* path, wchar_t* directory, wchar_t* filename) {
    const wchar_t* slash = wcsrchr(path, L'/');
    if (slash) { wcsncpy(directory, path, slash - path); directory[slash - path] = 0; }
    else directory[0] = 0;
    wcscpy(filename, slash ? slash + 1 : path);
}
static void wcscpy_s(wchar_t* dest, size_t, const wchar_t* src) { wcscpy(dest, src); }
'''
zip_cleanup_source += "static int ordinaryZip() { int retCode = outputFileList.count;\n"
zip_cleanup_source += body("Win/Mineways.cpp", "if (gpEFD->chkCreateZip[gpEFD->fileType] && (outputFileList.count > 0))")
zip_cleanup_source += "return retCode;\n}\n"
sketch_function = body("Win/Mineways.cpp", "static int processSketchfabExport(PublishSkfbData* skfbPData, wchar_t* objFileName, wchar_t* terrainFileName, wchar_t* cullSchemeSelected)\n{")
zip_cleanup_source += "static int sketchZip() { int errCode = 0; wchar_t wcZip[MAX_PATH_AND_FILE];\n"
zip_cleanup_source += source_body(sketch_function, "if (errCode < MW_BEGIN_ERRORS)")
zip_cleanup_source += "return errCode >= MW_BEGIN_ERRORS ? 1 : 0;\n}\n"
zip_cleanup_source += r'''
int main() {
    for (bool sketch : {false, true}) for (int scenario = 0; scenario < 5; ++scenario) {
        failAdd = scenario == 0 ? 3 : 0;
        failClose = scenario == 1;
        failCreate = scenario == 2;
        exportData.chkCreateModelFiles[0] = scenario == 4;
        closed = false;
        addCalls = sourceDeletes = directoryDeletes = closeCalls = 0;
        int result = sketch ? sketchZip() : ordinaryZip();
        assert(closeCalls == 1);
        if (scenario < 3) {
            assert(sketch ? result == 1 : (result & MW_CANNOT_WRITE_TO_FILE) != 0);
            assert(sourceDeletes == 0 && directoryDeletes == 0);
        } else {
            assert(sketch ? result == 0 : result == outputFileList.count);
            assert(sourceDeletes == (scenario == 3 ? 3 : 0));
            assert(directoryDeletes == (!sketch && scenario == 3 ? 1 : 0));
        }
    }
}
'''

for name, source, extra in (("map", map_source, []), ("zip_ansi", zip_source, []),
                            ("zip_unicode", zip_source, ["-D_UNICODE"]),
                            ("allocation_retry", allocation_source, []),
                            ("zip_cleanup", zip_cleanup_source, [])):
    cpp = OUT / (name + ".cpp")
    exe = OUT / name
    cpp.write_text(source)
    subprocess.run(["clang++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", *extra, str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print(name + ": PASS", flush=True)
