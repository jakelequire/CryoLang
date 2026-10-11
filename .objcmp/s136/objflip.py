"""Control for objcmp.py: copy one project's object tree and flip one byte
inside one object's .text section.  usage: objflip.py <src-tree> <dst-tree> <rel-object>"""
import os, re, shutil, subprocess, sys
src, dst, rel = sys.argv[1], sys.argv[2], sys.argv[3]
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)
p = os.path.join(dst, rel)
h = subprocess.run(["objdump", "-h", p], capture_output=True, text=True).stdout
for line in h.splitlines():
    f = line.split()
    if len(f) >= 6 and f[1] == ".text":
        size, off = int(f[2], 16), int(f[5], 16)
        break
data = bytearray(open(p, "rb").read())
data[off + size // 2] ^= 0x01
open(p, "wb").write(data)
print("flipped", rel, "at", off + size // 2)
