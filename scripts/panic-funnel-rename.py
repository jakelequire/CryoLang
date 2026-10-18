#!/usr/bin/env python3
"""One-off codemod: the runtime panic funnel `__cryo_panic_at` is renamed
`__cryo_panic` in every tracked text file that names it, except the pins and
the resolution rules' appendix (whose ruling names both on purpose).

Run from the repo root: python scripts/panic-funnel-rename.py
Prints each file it rewrote and the count of names replaced in it.
"""

import subprocess
from pathlib import Path

OLD = "__cryo_panic_at"
NEW = "__cryo_panic"
SKIP = {"bin/cryo", "bin/cryo.exe", "docs/resolution-rules.md"}

files = subprocess.run(["git", "grep", "-l", "-I", OLD], capture_output=True, text=True,
                       check=True).stdout.split()
total = 0
for name in files:
    if name in SKIP:
        continue
    path = Path(name)
    data = path.read_bytes()
    count = data.count(OLD.encode())
    path.write_bytes(data.replace(OLD.encode(), NEW.encode()))
    total += count
    print(f"{count:4d}  {name}")
print(f"{total} name(s) in {len(files) - len(SKIP & set(files))} file(s)")
