"""Check Mac version generation and rejection of mismatched release versions."""

import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
header_text = (ROOT / "Win/stdafx.h").read_text()


def version_part(name):
    return int(re.search(r"^#define MINEWAYS_" + name + r"_VERSION\s+(\d+)", header_text, re.M)[1])


major, minor = version_part("MAJOR"), version_part("MINOR")
expected = f"{major}.{minor:02d}"
with (ROOT / "Mac/Info.plist").open("rb") as file:
    plist = plistlib.load(file)
for key in ("CFBundleShortVersionString", "CFBundleVersion"):
    assert plist[key] == expected, f"{key} must match Windows/upstream {expected}"

with tempfile.TemporaryDirectory(prefix="mineways-version-") as temp:
    root = Path(temp)
    (root / "Mac").mkdir()
    (root / "Win").mkdir()
    shutil.copyfile(ROOT / "Mac/Makefile", root / "Mac/Makefile")
    source = root / "Win/stdafx.h"
    source.write_text(header_text)
    generated = root / "Mac/.MinewaysVersion.h"
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "MINEWAYS_VERSION"):
        env.pop(key, None)

    def generate(override=None):
        command = ["make", "--no-print-directory", "-s", ".MinewaysVersion.h"]
        if override is not None:
            command.append("MINEWAYS_VERSION=" + override)
        return subprocess.run(command, cwd=root / "Mac", env=env, capture_output=True, text=True)

    def check_generated(want_major, want_minor):
        result = generate()
        assert result.returncode == 0, result.stderr
        text = generated.read_text()
        assert f'#define MAC_MINEWAYS_VERSION_STRING "{want_major}.{want_minor:02d}"' in text
        assert f"#define MAC_MINEWAYS_VERSION_MAJOR {want_major}\n" in text
        assert f"#define MAC_MINEWAYS_VERSION_MINOR {want_minor}\n" in text

    check_generated(major, minor)
    before = generated.stat().st_mtime_ns
    assert generate(expected).returncode == 0
    assert generated.stat().st_mtime_ns == before
    for invalid in ("13.0", f"{major}.{minor + 1:02d}", expected + ".1", "v" + expected, ""):
        result = generate(invalid)
        assert result.returncode != 0 and "must match Windows/upstream" in result.stderr
        assert generated.stat().st_mtime_ns == before
    if minor < 10:
        result = generate(f"{major}.{minor}")
        assert result.returncode != 0 and "must match Windows/upstream" in result.stderr

    # Future upstream bumps must update the generated header without editing the Makefile.
    for next_minor in (0, 8, 9, 10):
        changed = re.sub(r"(^#define MINEWAYS_MAJOR_VERSION\s+)\d+",
                         lambda match: match[1] + str(major + 1), header_text, flags=re.M)
        changed = re.sub(r"(^#define MINEWAYS_MINOR_VERSION\s+)\d+",
                         lambda match: match[1] + str(next_minor), changed, flags=re.M)
        source.write_text(changed)
        check_generated(major + 1, next_minor)

print(f"PASS: Windows/macOS version {expected}, matching plist, exact tag versions, future bumps, and stable generated header")

