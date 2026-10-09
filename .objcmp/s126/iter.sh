#!/bin/bash
# One round of the deletion fixpoint: regenerate candidates from the facts and
# keep.txt, delete them all from a clean compiler/src, type-check the
# compiler, and list the names the check could not find.  Leaves the tree
# with the deletions applied.  usage: iter.sh <round>
set -u
cd "$(dirname "$0")/../.."
R=$1
S=.objcmp/s126
git checkout -- compiler/src
python $S/dead.py $S/dead$R.tsv
python $S/delete.py $S/dead$R.cand.tsv clean colliding
( cd compiler && CRYO_STDLIB=C:/Programming/apps/CryoLang/stdlib CRYO_CC=gcc \
    ../$S/bin/cryo-h0.exe check src/main.cryo > ../$S/chk-r$R.log 2>&1 )
tail -2 $S/chk-r$R.log
grep -a "^error" $S/chk-r$R.log | sed -E 's/^error\[([A-Z0-9]+)\].*/\1/' | sort | uniq -c
grep -a "^error\[E0233\]\|^error\[E0201\]\|^error\[E0502\]\|^error\[E0203\]\|^error\[E0204\]\|^error\[E0230\]" $S/chk-r$R.log \
    | sed -E 's/.*`([A-Za-z_:]+)`.*/\1/' | sort -u
