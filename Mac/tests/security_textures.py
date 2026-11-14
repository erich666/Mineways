#!/usr/bin/env python3
"""Compile the actual texture helpers with sanitizers in a disposable directory."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'TileMaker/TileMaker/TileMaker.cpp').read_text()
grid = (root / 'TileMaker/TileMaker/tilegrid.cpp').read_text()

def function(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "tga.h"
struct progimage_info { std::vector<unsigned char> image_data; int width, height; };
''' + function(source, 'static bool copyPNG(progimage_info* dst, progimage_info* src)\n{') + '\n' + function(source, 'static bool rotateTile(progimage_info &tile, int channels)\n{') + '\n' + function(source, 'static bool rotateTileIfHorizontal(progimage_info &tile)\n{') + r'''
struct FileRecord { bool exists; };
#define TOTAL_CATEGORIES 2
#define TOTAL_DECORATED_POT_TILES 1
struct DecoratedPotGrid { int decoratedPotCount, totalCategories, totalTiles; int categories[2]; FileRecord pr[2]; };
''' + function(grid, 'void deleteDecoratedPotFromGrid(DecoratedPotGrid* ppg, int category, int fullIndex)\n{') + r'''
static progimage_info img(int w, int h, unsigned char fill=1) { return {std::vector<unsigned char>((size_t)w*h*4, fill),w,h}; }
int main() {
  for (int direction=0; direction<4; direction++) {
    tga::Header h{}; h.width=3; h.height=2; h.imageDescriptor=(direction & 1 ? 0x10 : 0) | (direction & 2 ? 0x20 : 0);
    unsigned char pixels[6]{}; tga::Image im{pixels,1,3}; tga::details::ImageIterator it(h,im);
    for (int i=1;i<=6;i++) assert(it.putPixel<unsigned char>(i)==(i==6));
    for (int y=0;y<2;y++) for (int x=0;x<3;x++) {
      int sx=(direction&1) ? 2-x : x, sy=(direction&2) ? y : 1-y;
      assert(pixels[y*3+x]==sy*3+sx+1);
    }
    tga::details::ImageIterator read(h,im);
    for (int i=1;i<=6;i++) assert(read.getPixel<unsigned char>()==i);
  }
  auto small=img(4,3), dst=img(4,2); assert(!copyPNG(&dst,&small));
  dst=img(4,3); assert(copyPNG(&dst,&small)); small.image_data.pop_back(); assert(!copyPNG(&dst,&small)); small=img(4,3);
  dst=img(8,5); assert(!copyPNG(&dst,&small)); dst=img(8,6); assert(copyPNG(&dst,&small));
  auto large=img(8,4,9); dst=img(4,4,0); assert(copyPNG(&dst,&large));
  assert(dst.image_data[0]==9 && dst.image_data[4*4*2]==0);
  large=img(8,9); assert(!copyPNG(&dst,&large));
  auto malformed=img(3,4); assert(!rotateTile(malformed,4));
  auto strip=img(2,4,0); for(int i=0;i<8;i++) strip.image_data[i*4]=(unsigned char)(i+1);
  assert(rotateTile(strip,4)); unsigned char expected[]={2,4,1,3,6,8,5,7};
  for(int i=0;i<8;i++) assert(strip.image_data[i*4]==expected[i]);
  auto line=img(3,6,0); for(int x=0;x<3;x++) line.image_data[(3+x)*4+3]=255; assert(rotateTileIfHorizontal(line));
  auto one=img(1,1,0); assert(!rotateTileIfHorizontal(one));
  auto odd=img(3,3,0); for(int i=0;i<9;i++) odd.image_data[i*4]=(unsigned char)i;
  assert(rotateTile(odd,4)); assert(odd.image_data[4*4]==4);
  DecoratedPotGrid g{2,2,1,{1,1},{{true},{true}}};
  deleteDecoratedPotFromGrid(&g,1,99); assert(g.decoratedPotCount==2);
  deleteDecoratedPotFromGrid(&g,1,1); assert(g.decoratedPotCount==1 && !g.pr[1].exists && g.pr[0].exists);
}
'''
with tempfile.TemporaryDirectory(prefix='textures-', dir=os.environ.get('TMPDIR')) as tmp:
    path = Path(tmp) / 'test.cpp'
    path.write_text(harness)
    exe = Path(tmp) / 'test'
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined', '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g', '-I',str(root/'TileMaker/TileMaker'),str(path),str(root/'TileMaker/TileMaker/image_iterator.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('texture security regression tests passed')
