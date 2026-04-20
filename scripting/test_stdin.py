import subprocess
proc = subprocess.Popen(
    ["mineways.exe", "-headless"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    text=True, bufsize=1  # line-buffered
)
print(proc.stdout.readline().strip())  # reads "READY"
