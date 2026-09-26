"""Count the written reasons `residue_classify.py` carries per class, and how
many of them carry a command that could check them (condition three).

    python scripts/ns-migration/8.354/classifier_reasons.py
"""
import collections
import importlib.util
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
CLASSIFY = os.path.join(os.path.dirname(HERE), "residue_classify.py")

spec = importlib.util.spec_from_file_location("residue_classify", CLASSIFY)
rc = importlib.util.module_from_spec(spec)
sys.path.insert(0, os.path.dirname(HERE))
spec.loader.exec_module(rc)

# A reason that could check itself names a command to run.
COMMAND = re.compile(r"`(?:git|grep|python3?|make|rg|awk)\b")
by_class = collections.Counter()
with_command = collections.Counter()
for _method, (cls, reason) in rc.CLASS_OF_METHOD.items():
    by_class[cls] += 1
    if COMMAND.search(reason):
        with_command[cls] += 1
for cls in sorted(by_class):
    print("%s\t%d reasons\t%d with a command" % (cls, by_class[cls], with_command[cls]))
print("justified (J) and outside (N): %d reasons, %d with a command"
      % (by_class["J"] + by_class["N"], with_command["J"] + with_command["N"]))
