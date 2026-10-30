#!/usr/bin/env python3
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

root = Path(__file__).resolve().parents[2]

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

def png(width, height, pixels):
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))

with tempfile.TemporaryDirectory(prefix='png-security-', dir=os.environ.get('TMPDIR')) as temp:
    tmp = Path(temp)
    good, bomb, huge = [tmp / (name + '.png') for name in ('good', 'bomb', 'huge')]
    good.write_bytes(png(1, 1, b'\0\1\2\3\xff'))
    bomb.write_bytes(png(1, 1, bytes(8 * 1024 * 1024)))
    huge.write_bytes(png(100000, 100000, b''))
    exe = tmp / 'png-test'
    subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    '-include', str(root / 'Mac/compat.h'), '-I', str(root / 'Mac'), '-I', str(root / 'Win'),
                    str(root / 'Mac/tests/security_png.cpp'), str(root / 'Win/lodepng.cpp'),
                    str(root / 'Win/rwpng.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(good), str(bomb), str(huge)], check=True)
print('PNG security tests passed')
