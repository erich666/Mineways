# Run a script by piping it into Mineways.exe, which is assumed to be in the same directory.
#
# For example, you can run this from the main Mineways directory by;
#
#    python stdin_mode.py
#
# See the docs/scripting.html#code section of the documentation and search for "headless" for more information.

import subprocess
proc = subprocess.Popen(
    ["mineways.exe", "-headless"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    text=True, bufsize=1  # line-buffered
)
print(proc.stdout.readline().strip())  # reads "READY"

# Load a world and export
for cmd in [
    'World: [Block Test World]',
    'Set render type: Wavefront OBJ absolute indices',
    'Selection location min to max: 8, -2, -35 to 143, 319, 130',
    'Export for Rendering: C:/tmp/block_test.obj',
    'Close'
]:
    proc.stdin.write(cmd + '\n')
    proc.stdin.flush()
    response = proc.stdout.readline().strip()
    print(f"{cmd[:40]:40s} -> {response}")

# Wait for Mineways to exit cleanly after the Close command
proc.wait()