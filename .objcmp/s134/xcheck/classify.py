"""Classify each spelling-flow entry the compiler does not refuse, and write
.objcmp/s134/CROSSCHECK.md listing every entry with its verdict.

The classes and their reasons are the judgment recorded in this file; the
verdict a class is applied to comes from xcheck.py's measurement."""
import re
from collections import Counter, OrderedDict

CLASSES = OrderedDict([
    ("SEEN", ("The compiler refuses it",
              "Its lookup function carries an allow of `lookup_by_spelling`; with every such allow removed, the compiler under test refuses a site in that function (E0157).  The allow's written reason is why the lookup stands.")),
    ("PY-MODULEPATH", ("Not a lookup: module identities compared (a Python-side limitation)",
                       "The function compares `ModulePath`s with `equals` - module identities.  `spelling-flow.py` reads a `ModulePath` comparison as a text comparison; ruling 219 leaves it so.")),
    ("OUT-FLAG", ("Not a lookup of a program's name: command-line flags (ruling 5)",
                  "A flag or subcommand string is an option string, out of scope for the rules by ruling 5.")),
    ("OUT-CONFIG", ("Not a lookup of a program's name: configuration, manifest, lock-file and vendor keys",
                    "Keys of the build's own files - `cryoconfig` sections and values, dependency and vendor names, cache and object paths, a test's requirement words - name no declaration of a program.")),
    ("OUT-TEXT", ("Not a lookup of a program's name: file paths, messages and literal contents",
                  "Source file paths, diagnostic text and its dedupe, a string literal's contents, a C header's emitted names, the empty string: text that names no declaration.")),
    ("OUT-FIXED", ("Not a lookup of a program's name: the language's own fixed spellings",
                   "Keywords, numeric suffixes, directive and builtin names, the lint's own name, OS atoms, asm dialects, demangler operator codes, the fixed language-module table: spellings the language defines, which no program declares.")),
    ("LOADER-FILE", ("Not a lookup of a program's name: a file, keyed by the path the `loader-path` door answered",
                     "Module discovery's loading and loaded sets and its namespace-by-file map are keyed by a file's lower-cased path, and every such path is the answer of the `loader-path` door `ModuleLoader::resolve_import_path`, which alone turns a written module path (a `SymbolStr` since the scanner interns it) into a file.  The compiler follows the written path to these keys: with the door's marker and the loader's three function allows removed, it refuses `discover_module`, `is_loaded`, `is_loading` and `pop_loading` among 19 sites (`.objcmp/s134/chk-inv1.log`); with them in place it refuses none, because what the door answers is a file, not a spelling.  The Python reads through the door's answer, which ruling 219 leaves it doing.")),
    ("GAP", ("A lookup the compiler does not see",
             "No class applies: a lookup only the Python sees.")),
    ("ALLOWED", ("Not refused, and allowed with a reason",
                 "The function carries an allow whose reason names what it does; removing the allow draws no refusal, because the text it is handed is not a `DeclName` or arrives as a `string`.")),
])

RULES = [
    (r"cli::(ArgumentParser|ParsedArgs|Runner)\b", "OUT-FLAG"),
    (r"cli::commands::FlagKind::resolve", "OUT-FLAG"),
    (r"cli::commands::Executor::resolve$", "OUT-FLAG"),
    (r"cli::commands::Executor::check_annotations", "OUT-TEXT"),
    (r"cli::commands::Executor::(proj_first_unknown_requirement|vendor_)", "OUT-CONFIG"),
    (r"project_config::|deps::|vendor::registry::|sync_vendor_lock|ModuleKeyTable\.lookup", "OUT-CONFIG"),
    (r"ModuleLoader\.(triple_for_lib|note_vendor_import)", "OUT-CONFIG"),
    (r"diag::renderer::|diag::sink::|attach_shadow_import_suggestions|string_cache::|InternTable::new|TypeLoweringPasses::run|bindgen::generator::", "OUT-TEXT"),
    (r"lex::TokenType::from_keyword|Lexer::is_valid_numeric_suffix|directive_processing::|HostOS::is_os_atom|Parser\.parse_asm_block|demangler::|run_auto_import_pass", "OUT-FIXED"),
    (r"ModuleLoader\.(discover_module|is_loaded|is_loading|pop_loading|mark_loaded)$", "LOADER-FILE"),
    (r"ModuleGraph\.(imports|reexport_closure)|Resolver\.get_exports|is_prelude_ns|register_prelude_namespaces|reaching_import_for", "PY-MODULEPATH"),
]


def classify(r):
    verdict, count, rule, kind, keycls, lookup, okind, ofn, otext, reason = r
    if verdict == "REFUSED-WHEN-UNALLOWED":
        return "SEEN"
    if verdict == "ALLOWED-UNSEEN":
        return "ALLOWED"
    if lookup.endswith("CompilerInstance.run_project"):
        return "PY-MODULEPATH" if keycls == "name" else "OUT-CONFIG"
    for pat, cls in RULES:
        if re.search(pat, lookup):
            return cls
    return "UNCLASSIFIED"


rows = [l.rstrip("\n").split("\t") for l in open(".objcmp/s134/xcheck/verdicts.tsv", encoding="utf-8")]
by = OrderedDict((k, []) for k in list(CLASSES) + ["UNCLASSIFIED"])
for r in rows:
    by[classify(r)].append(r)

n = Counter({k: len(v) for k, v in by.items()})
w = Counter({k: sum(int(r[1]) for r in v) for k, v in by.items()})
out = []
out.append("# Cross-check: the Python spelling-flow list against the compiler's own checks\n")
out.append("Measured on the tree of the commit carrying this file.  "
           "`scripts/spelling-flow.outstanding.tsv` as it stands (ruling 219: the "
           "Python is not changed): %d entries, %d counting each entry's weight.\n" % (len(rows), sum(w.values())))
out.append("## Method\n")
out.append("""* `xcheck/srcindex.py` indexes every function of `compiler/src` (qualified
  name as the list writes it, file, line span, its allow and door marker).
  Control: every allow and door marker in the source lands on an indexed
  function or on a field (`xcheck/idxctl.py`, `idxctl2.py`; one field allow,
  `TypeArena.intern_cache`, whose line ends in a comment, is reported
  unplaced by the field pattern - a field's allow, never stripped); all 118
  lookup functions the list names are indexed (`xcheck/mapcheck.py` over
  `xcheck/lookupfns.txt`, written by `xcheck/lookupfns.py`).
* `xcheck/strip.py` copies the compiler and deletes all 109 allows of
  `lookup_by_spelling` that sit on a function (a field's stays: the
  store-key lint runs before sema and would stop the build).  The compiler
  under test builds the copy: 79 E0157 refusals in 70 functions, every one
  mapped to an indexed function.
* The 28 entries the 133rd's run found only the Python seeing are gone or
  moved: the fixed tables are doors, the display names and linker symbols
  are `DeclName`s, the duplicate checks and the plain-text flow are
  followed, so those entries leave the list, are SEEN, or are in
  LOADER-FILE, whose own inversion is written in its reason.
* `xcheck/xcheck.py`: an entry is SEEN when its lookup function is allowed
  and the stripped build refuses a site in it.  Every other entry is
  classified below by `xcheck/classify.py`, which also writes this file;
  each class carries its reason.
""")
out.append("## Result\n")
out.append("| class | entries | weighted |\n|---|---|---|")
for k in by:
    if n[k] == 0 and k == "UNCLASSIFIED":
        continue
    title = CLASSES[k][0] if k in CLASSES else "unclassified"
    out.append("| %s: %s | %d | %d |" % (k, title, n[k], w[k]))
out.append("")
gaps = [k for k in by if k.startswith("GAP")]
gap_n = sum(n[k] for k in gaps) + n["UNCLASSIFIED"]
gap_w = sum(w[k] for k in gaps) + w["UNCLASSIFIED"]
verdict = "Yes, as a finder of lookups" if gap_n == 0 else "Not yet"
out.append("**Is the Python list redundant?  %s.**  %d entries (%d weighted) are refused by "
           "the compiler; %d (%d) are not lookups of a program's name, compare module "
           "identities the Python reads as text, or key a file the loader's door answered; "
           "%d (%d) are allowed with reasons the compiler does not need.  %d entries (%d "
           "weighted) are lookups only the Python sees.\n" % (
               verdict, n["SEEN"], w["SEEN"],
               sum(n[k] for k in by if k.startswith("OUT") or k in ("PY-MODULEPATH", "LOADER-FILE")),
               sum(w[k] for k in by if k.startswith("OUT") or k in ("PY-MODULEPATH", "LOADER-FILE")),
               n["ALLOWED"], w["ALLOWED"], gap_n, gap_w))
for k, v in by.items():
    if not v:
        continue
    title, why = CLASSES.get(k, ("unclassified", "no class applies"))
    out.append("## %s: %s (%d entries, %d weighted)\n" % (k, title, n[k], w[k]))
    out.append(why + "\n")
    out.append("| n | rule | kind | key | lookup function | origin | origin function | origin text |\n|---|---|---|---|---|---|---|---|")
    for r in sorted(v, key=lambda r: (r[5], r[6], r[8])):
        cells = [c.replace("|", "\\|").replace("compiler::", "") for c in (r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8][:80])]
        out.append("| " + " | ".join(cells) + " |")
    out.append("")
import json
refused = json.load(open(".objcmp/s134/xcheck/refused-fns.json"))
listed = set()
for r in rows:
    listed.add(r[5])
    listed.add(r[5].rsplit(".", 1)[0] + "::" + r[5].rsplit(".", 1)[-1] if "." in r[5] else r[5])
extra = [q for q in refused if q not in listed and (q.rsplit(".", 1)[0] + "::" + q.rsplit(".", 1)[-1]) not in listed]
out.append("## The other direction: functions the compiler refuses that the list does not name (%d)\n" % len(extra))
out.append("With its allow removed, each of these is refused by the compiler; no entry of "
           "the list has it as its lookup function.  The list is therefore no superset "
           "of the compiler's check either.\n")
for q in extra:
    out.append("* `%s`" % q.replace("compiler::", ""))
out.append("")
open(".objcmp/s134/CROSSCHECK.md", "w", encoding="utf-8").write("\n".join(out) + "\n")
print("refused but unlisted:", len(extra))
for k in by:
    print(k, n[k], w[k])
