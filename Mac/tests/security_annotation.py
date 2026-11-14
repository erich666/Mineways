#!/usr/bin/env python3
import os
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
script = root / 'scripting/annotate_map.py'
with tempfile.TemporaryDirectory(prefix='annotation-security-', dir=os.environ.get('TMPDIR')) as temp:
    tmp = Path(temp)
    # On newer Python, isolate the existing imghdr dependency from filename validation.
    if importlib.util.find_spec('imghdr') is None:
        (tmp / 'imghdr.py').write_text("def what(path):\n    return 'png'\n")
    env = dict(os.environ, PYTHONPATH=str(tmp), PYTHONDONTWRITEBYTECODE='1')
    image = tmp / 'map café 日本語.png'
    image.write_bytes(b'\x89PNG\r\n\x1a\n' + b'\x00' * 8 + struct.pack('>II', 2048, 2048))
    def run(input_name, output_name):
        return subprocess.run([sys.executable, str(script), input_name, '0', '0', output_name],
                              env=env, capture_output=True, text=True)
    result = run(str(image), 'annotated café 日本語.png')
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith('magick "' + str(image) + '" ')
    assert result.stdout.rstrip().endswith('"annotated café 日本語.png"')
    image.write_bytes(b'\x89PNG\r\n\x1a\n' + b'\x00' * 8 + struct.pack('>II', 250000, 2))
    lines = run(str(image), 'annotated café 日本語.png').stdout.splitlines()
    assert len(lines) > 1 and all(line.endswith('\"annotated café 日本語.png\"') for line in lines)
    assert all(line.startswith('magick \"annotated café 日本語.png\" ') for line in lines[1:])
    for bad in ('bad&whoami.png', 'bad|whoami.png', 'bad%PATH%.png', 'bad!PATH!.png',
                'bad^name.png', 'bad"name.png', 'bad\ncommand.png', '-option.png', '@file.png', 'file:evil.png'):
        for input_name, output_name in ((str(image), bad), (bad, 'safe.png')):
            result = run(input_name, output_name)
            assert result.returncode != 0 and result.stdout == '', bad
    default = tmp / 'default.py'
    default.write_text(script.read_text().replace("fnameout = 'my_annotated_map.png'", "fnameout = 'bad%PATH%.png'"))
    result = subprocess.run([sys.executable, str(default)], env=env, capture_output=True, text=True)
    assert result.returncode != 0 and result.stdout == ''
print('annotation security tests passed')
