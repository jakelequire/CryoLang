"""Copy the 134th's cross-check scripts into .objcmp/s135/xcheck, pointing
every path at this session's directory and the strip log at this session's
run."""
import os
src_dir = "C:/Programming/apps/CryoLang/.objcmp/s134/xcheck"
dst_dir = "C:/Programming/apps/CryoLang/.objcmp/s135/xcheck"
os.makedirs(dst_dir, exist_ok=True)
for f in ["srcindex.py", "xcheck.py", "classify.py", "idxctl.py", "idxctl2.py", "lookupfns.py", "mapcheck.py"]:
    t = open(os.path.join(src_dir, f), encoding="utf-8").read()
    t = t.replace(".objcmp/s134/strip/strip-u2.log", ".objcmp/s135/chk-strip-final.log")
    t = t.replace(".objcmp/s134/", ".objcmp/s135/")
    open(os.path.join(dst_dir, f), "w", encoding="utf-8").write(t)
    print("copied", f)
