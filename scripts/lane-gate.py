#!/usr/bin/env python3
"""Pin the resolution-lane SURFACE against a committed golden.

docs/name-resolution.md §7.2 mechanism 5 requires one `resolve_path(segments,
ns, scope)` and three locks to keep it one.  Two of those locks cannot hold on
their own:

  * privatizing the per-kind lookups stops a direct call from `sema`, but not
    someone adding a NEW public wrapper beside them;
  * deleting a string-keyed entity lookup stops that one, but not a helper
    being reintroduced under another name.

Only a ratchet catches growth.  This is that ratchet: a committed golden, drift
fails, and `--update` re-pins deliberately.

WHAT IS PINNED
--------------
Each count is broken down per file so a failure names what moved.

THREE RULES, EACH DERIVED FROM A DEFINITION, NONE FROM A LIST OF NAMES.

  1. WHICH TYPES ARE STORES.  A store is a type that OWNS A MAP: any `type`
     block in the tree with a field of the tree's one map type, `HashMap<K,
     V>` (or `HashSet<T>`, its set form; none today), read from the tree on
     every run.  The key type is not consulted: no map in this tree is
     declared `HashMap<SymbolStr, ...>` - a name keys a map by its interned
     id, `u32`, and a `u64` is two of them packed, a type id or a source
     position - so the key's spelling cannot tell a name-keyed table from a
     position-keyed one, and the rule does not try.  Instead EVERY map owner
     must be placed: as a store, with the rows its reads and writes land in
     (STORES below), or as an exclusion with the reason it holds no
     declaration under a name (EXCLUDED below: a table keyed by a source
     position, a linker symbol, a file path, a directive kind; a pass's own
     scope of the locals it just bound).  A map owner in neither is REFUSED,
     as is a listed type that owns no map or lives in another file - a stale
     entry is the list drifting from the tree.  `TypeUtils` is the one store
     with no map, sema's funnel in front of the index, and is marked as such.
     A gate that watched ONE store found new surface on every audit for
     eleven rounds, because a reader on any other store was invisible by
     construction; a gate whose stores were a hand-written list of seven read
     OK over a new map-keyed store added to the context, because the list
     never asked the tree.

  2. WHICH METHODS ARE NAME-KEYED.  Any method a store declares whose
     signature mentions a KEY TYPE - `SymbolStr` or `string` - as a
     parameter (the caller hands a name in and gets an answer) or as the
     return type (the caller hands an answer in and gets a name back, the
     same boundary crossed the other way).  The set is read from the tree on
     every run: the `type struct` block's inline methods AND every
     `implement [struct] <Store> { ... }` block in any file, because Cryo
     lets a method be added to a type from another file and a parser that
     read only the struct block was blind to one.  A gate that pinned
     readers by a NAME PATTERN (`lookup_*`) was blind to a reader called
     anything else, and the tree held twenty-four such calls under a gate
     that read OK; a gate that matched the key type as the one token
     `SymbolStr` was blind to a reader keyed by `string`, and the index
     already took `string` parameters.  The signature is the one thing a
     name-keyed reader cannot be written without.

  3. WHICH STORE A CALL REACHES.  By the receiver's DECLARED TYPE, not its
     spelling: `this` is the enclosing type, a local or parameter is what
     its annotation says, and each `.field` is what the field's declaration
     says, all read from the tree.  A receiver rule written as spellings
     (`di`, `*.decl_index`) fails closed on a new spelling but has to grow a
     new spelling for every alias, and with eight stores the same-named
     methods on OTHER types (`this.scopes.lookup_local`, `this.functions.
     register`) would each need one too.  A receiver whose type cannot be
     read is a hard failure, because a call this gate cannot place is one
     it cannot pin and a silently dropped one reads as progress.

The calls are SPLIT BY THE STORE that answers them and by READ vs WRITE:

  * LOOKUP -- answered by the `DeclarationIndex` under one of the four
    per-kind names mechanism 5 gives (`lookup_type`, `lookup_func_type`,
    `lookup_global`, `lookup_method_return`; `lookup_func_return` read a
    second map written in lockstep with the signature's and is deleted).  This is
    the lane surface, and the only one of the rows that should fall.
  * LOOKUP_OTHER -- answered by the `DeclarationIndex` under any OTHER
    name-crossing method that READS (`&this`, or a static): the registry's
    entry accessors, the visibility and reachability questions, the global
    and extern tables, the intrinsic owner.  A caller can leave the pinned
    LOOKUP row by switching to one of these, and the pinned row then falls,
    which reads as exactly the progress this gate was built to distinguish
    from regrowth.
  * REGISTER -- a name-keyed WRITE to the `DeclarationIndex` (`mut &this`):
    every registrar that is handed a key the CALLER derived from a
    declaration it holds.  The migration's other half - the key derived at
    registration and nowhere else - falls here, and a registrar that grows a
    second name-keyed door is caught the same way a reader is.
  * LOOKUP_ROUTED -- answered by a `TypeUtils` wrapper, any name-crossing
    method that type declares.  Already at the destination, so it RISES as
    LOOKUP falls and is not a target; it is pinned because a new wrapper is
    exactly the regrowth this gate exists to catch, and a wrapper under a
    fifth name was one the four-name rule could not see.
  * LOOKUP_LOCAL -- a name from any store's set called on a receiver whose
    declared type is NOT a store: a type's OWN same-named method over its
    own symbol map or scope stack (`move_check`, `drop_insertion`, sema's
    `scopes`).  Not a lane site, and no migration can remove one, so it is
    a FLOOR: driving LOOKUP to zero is reachable, driving the total to zero
    never was.  It is also the control on rule 3: a store misplaced as a
    local moves this row.
  * ARENA_READ / ARENA_WRITE -- the arena's name-keyed reads (the reverse
    maps: `get_qualified_name`, `template_key_of`, the display formatters)
    and its creators (`create_struct(qualified_name, module)`,
    `add_name_alias`, `reserve_spec_names`).  A creator is where a declared
    type is keyed by the spelling the caller minted for it.  The arena has
    no written-name read: `lookup_by_name` was the index's lane on a second
    store (type resolution asked it at 17 sites, seven a qualified-miss→bare
    retry, under a gate that read OK) and is deleted; a name lookup added to
    the arena under any spelling lands in ARENA_READ.
  * REGISTRY_READ / REGISTRY_WRITE -- the `GenericRegistry`: templates,
    inherent impl blocks, trait declarations and trait-impl heads.  Its keys
    are canonical strings derived from stamps (`trait_id`, `target_key`), so
    most of this surface is bucket B rather than spelling; it is pinned
    because it was the store no row enumerated, and a reader on it - the
    first-match scan `find_trait_defining_method` among them - was invisible.
  * GRAPH_READ / GRAPH_WRITE -- the `ModuleGraph`: a module asked by its
    namespace or its path, the re-export closure, the owner-key → file map.
  * CONST_READ / CONST_WRITE -- the `ConstantTable`: constants and enums
    registered under their qualified name; reads go by stamp (`ConstEval`)
    and are expected to stay at zero.
  * DEFAULT_READ / DEFAULT_WRITE -- the `DefaultRegistry`: the default-
    expansion pass's table of all-default generic templates, keyed by the
    template's BARE leaf, built on the pass's stack and threaded through it
    as a parameter.  A store that lives in one file has its whole surface in
    that file, and a store's own calls are not counted (see below), so both
    rows read 0: what the gate pins is that the store EXISTS and that no
    other stage reaches it, not how its own pass reads it.
  * REENTRY -- calls to `get_resolver()` outside the driver, and ANY
    name-keyed `Resolver` method reached through a `Resolver`-typed
    receiver outside the resolver's own directory and the driver
    (`instance.cryo`, `compilation_context.cryo`, which own it and move its
    cursor between modules).  The monomorphizer swaps the resolver's module
    that way, and a gate reading `get_resolver()` alone reported 3 over a
    tree holding 6; a gate reading the three cursor-move names alone read 6
    over a tree holding 12, because type resolution asks `is_ambiguous(name)`
    by spelling and no list named it.  §7.2's corollary: name resolution is
    a pass, not a service, and a stage that can call back into the resolver
    will.  A resolver called from `sema` no longer has the writer's imports
    in hand, so it answers from a string -- which is the mechanical origin
    of B1.

Every row is asserted exactly, in both directions.  For LOOKUP and REENTRY an
increase is the regrowth this exists to catch, and a decrease is progress that
still fails, because a silently-tolerated decrease leaves the old higher number
as the bound and lets a later regression climb back to it unnoticed.  A row
that is a destination or a floor has no preferred direction and is asserted for
the same reason: an unexplained move is what the gate is for, and it is the
reason `--update` exists rather than a tolerance.

WHY THIS GATE IS SOURCE-DERIVED AND HAS NO PER-HOST SECTIONS
------------------------------------------------------------
A gate that measures a BUILD has numbers that move with which stdlib modules
the host compiles, and needs a `[host:...]` section per host.  This gate
counts CALL SITES IN THE SOURCE.  The same tree gives the same answer on every
host, so a per-host split would encode a dimension that does not exist and
would let one host's re-pin hide another's regression.  It also means this gate
needs no compiler, no stdlib, and no successful link -- it runs on a fresh
clone in under a second, which is what makes it usable as a pre-commit check.

THREE THINGS A NAIVE GREP GETS WRONG, ALL OBSERVED HERE
--------------------------------------------------------
  * THE RECEIVER.  Matching `.lookup_type(` matches the NAME, so a call
    already routed through `TypeUtils` counted exactly like a raw index call,
    and a `lookup_type(&this, name)` over a local scope stack counted as one
    too though it never touches the index.  A single total therefore could not
    say what it was a total OF, and a migration measured against it partly
    rewarded renaming.  Placement by declared type is what fixes that; an
    unplaceable receiver is a hard failure, because a call this gate cannot
    classify is one it cannot pin.
  * COMMENTED-OUT CALLS.  `instance.cryo` carries a commented `//
    ctx.get_resolver();`.  Counting it pins 9 re-entries where 8 exist, so
    deleting a real call and leaving the comment would read as progress while
    uncommenting it would read as clean.  Lines whose first non-space
    characters are `//` are skipped, and a trailing `//` comment is cut before
    matching.
  * THE OWNER'S OWN CALLS.  `decl_index.cryo` defines the index's methods and
    calls them internally; `type_utils.cryo` does the same for the funnel's.
    Those are not the surface this gate is about -- the surface is what OTHER
    stages reach for -- so each defining file is excluded from its own set
    (and only its own: the funnel's calls INTO the index are counted).
    Excluding it by name is safe here precisely because it is the definition
    site, not a special case about some caller.

A row that reaches zero is DELETED from the golden rather than pinned at 0, so
the golden reads as the live surface rather than a graveyard; a file reappearing
is then an added row, which fails the same way an increase does.

Usage:
    python3 scripts/lane-gate.py [--update] [--names] [--src DIR --golden FILE]

`--names` prints the derived sets and exits, so what the rule swept up can be
read rather than inferred.  `--src`/`--golden` point the gate at another tree
and golden; `scripts/lane-gate-selftest.py` uses them to drive every rule
through a throwaway tree in both directions.

Exit codes: 0 match (or golden updated); 1 drift.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "compiler", "src")
DEFAULT_GOLDEN = os.path.join(ROOT, "tests", "lane-baseline.txt")

# The four per-kind lookups §7.2 mechanism 5 names: the LOOKUP row.  They are
# the only names this gate holds as a list, and the list decides which ROW a
# call lands in, never whether it is counted.
LOOKUPS = (
    "lookup_type",
    "lookup_func_type",
    "lookup_global",
    "lookup_method_return",
)

# The types whose presence in a signature makes a method name-keyed.  Matched
# as whole tokens: `SymbolStr[]` and `string*` count, `StringBuilder` does not.
KEY_TYPES = ("SymbolStr", "string")
KEY_RE = re.compile(r"\b(?:%s)\b" % "|".join(KEY_TYPES))


class Store(object):
    """One declaration-holding type: where it is defined, which rows its
    reads and writes land in, and which files are its own machinery.

    `defn` is matched on the path relative to compiler/src, not on the
    basename: a basename exclusion is a rule about a NAME, and a second
    decl_index.cryo anywhere in the tree would have its calls silently
    dropped.  `owners` are further files whose calls are the store's own
    (the resolver's driving pass and the driver that moves its cursor); a
    directory owner ends in `/`.
    """

    def __init__(self, defn, read, write, owners=(), funnel=False):
        self.defn = defn
        self.read = read
        self.write = write
        self.owners = tuple(owners)
        # A funnel owns no map: it is a store because every name-keyed method
        # it declares is a wrapper over one that does.  The one exemption from
        # rule 1's "a store owns a map", stated per store rather than inferred.
        self.funnel = funnel

    def owns(self, rel):
        if rel == self.defn:
            return True
        for o in self.owners:
            if o.endswith("/") and rel.startswith(o):
                return True
            if rel == o:
                return True
        return False


# Rule 1's two tables.  Together they must name EVERY type in the tree that
# owns a map, each in the file it is declared in, and nothing else; `scan`
# refuses the tree otherwise.  The map is what makes a type a candidate; the
# tables only say which side of the line each candidate falls on, and a
# candidate the tables do not mention is the gate's question to whoever added
# it, never a silent OK.
#
# Stores: each holds declarations under a name, and each has the rows its
# reads and writes land in.
STORES = {
    "DeclarationIndex": Store("compiler/decl_index.cryo", "LOOKUP_OTHER", "REGISTER"),
    "TypeUtils":        Store("compiler/sema/type_utils.cryo", "LOOKUP_ROUTED", "LOOKUP_ROUTED",
                              funnel=True),
    "TypeArena":        Store("compiler/types/arena.cryo", "ARENA_READ", "ARENA_WRITE"),
    "GenericRegistry":  Store("compiler/types/generic_registry.cryo", "REGISTRY_READ", "REGISTRY_WRITE"),
    "ModuleGraph":      Store("compiler/module_graph.cryo", "GRAPH_READ", "GRAPH_WRITE"),
    "ConstantTable":    Store("compiler/const_table.cryo", "CONST_READ", "CONST_WRITE"),
    "Resolver":         Store("compiler/resolver/resolver.cryo", "REENTRY", "REENTRY",
                              owners=("compiler/resolver/", "compiler/instance.cryo",
                                      "compiler/compilation_context.cryo")),
    "DefaultRegistry":  Store("compiler/passes/default_expansion.cryo", "DEFAULT_READ", "DEFAULT_WRITE"),
}
INDEX_TYPE = "DeclarationIndex"
ARENA_TYPE = "TypeArena"


class Excluded(object):
    """A map owner that is not a store, with the file it is declared in and
    the reason its map holds no declaration under a name."""

    def __init__(self, defn, reason):
        self.defn = defn
        self.reason = reason


# Not stores, each with the reason.  Four kinds of reason recur: the key is a
# SOURCE POSITION (a span's file, line and column packed to a `u64`); the key
# is an OUTPUT name - a linker symbol codegen minted, or a spec key built from
# a stamp - reached after resolution has answered; the key is a FILE PATH or
# a DIRECTIVE KIND, which name no declaration; or the map is a pass's OWN RIB,
# the locals it bound while walking a body, keyed by the binding's name - the
# population the LOOKUP_LOCAL row counts calls on, and no lane.
EXCLUDED = {
    "InternTable":       Excluded("compiler/resolver/intern_table.cryo",
                                  "the string<->SymbolStr boundary itself: holds strings, not declarations"),
    "Scope":             Excluded("compiler/resolver/scope.cryo",
                                  "the resolver's rib; reached through Resolver, whose asks from outside its pass are REENTRY"),
    "ResolutionMap":     Excluded("compiler/resolver/resolution_map.cryo",
                                  "the resolver's answers keyed by SOURCE POSITION (ResolutionMap::make_key(span))"),
    "ModuleLoader":      Excluded("compiler/module_loader.cryo",
                                  "discovery's namespace -> file path table, read once per import before the graph exists; the graph it builds is the store"),
    "Monomorphizer":     Excluded("compiler/mono/monomorphizer.cryo",
                                  "spec bookkeeping keyed by a mangled symbol, a stamp derivation"),
    "MonoState":         Excluded("compiler/mono/state.cryo",
                                  "spec bookkeeping keyed by a mangled symbol, a stamp derivation"),
    "SemaState":         Excluded("compiler/sema/state.cryo",
                                  "sema's own rib of locals by binding name, and a closure-spec key built from a DefId"),
    "MoveChecker":       Excluded("compiler/passes/move_check.cryo",
                                  "the pass's own rib of locals by binding name"),
    "DeadCodeChecker":   Excluded("compiler/passes/dead_code.cryo",
                                  "use flags keyed by SOURCE POSITION (ResolutionMap::make_key(span))"),
    "FunctionRegistry":  Excluded("compiler/codegen/state/function_registry.cryo",
                                  "LLVM handles keyed by the LINKER SYMBOL codegen minted"),
    "GlobalRegistry":    Excluded("compiler/codegen/state/global_registry.cryo",
                                  "LLVM handles keyed by the LINKER SYMBOL codegen minted"),
    "TypeMapperCache":   Excluded("compiler/codegen/type_map.cryo",
                                  "LLVM types keyed by TypeRef id"),
    "DiagRenderer":      Excluded("compiler/diag/renderer.cryo",
                                  "source files keyed by FILE PATH"),
    "DiagnosticSink":    Excluded("compiler/diag/sink.cryo",
                                  "rendered diagnostics deduplicated by their text"),
    "Runner":            Excluded("CLI/_module.cryo",
                                  "the CLI's command table, keyed by the subcommand typed"),
}

REENTRY_RE = re.compile(r"\bget_resolver\s*\(\s*\)")
# The driver legitimately owns the resolver and may ask for it.
REENTRY_OWNERS = STORES["Resolver"].owners

# The one door that turns a name into a resolution answer, and the one that
# turns an answer back into a name.  `DefId`'s field is private, so the literal
# cannot be written outside the type and every crossing goes through these two.
DEFID_MINT_RE = re.compile(r"DefId::of_definition\s*\(")
# The leading dot is what keeps the MODULE `compiler::resolver::qualified_name`
# out: a module is reached with `::` and an import names it bare, so neither
# can match, while a receiver can only be a value.
DEFID_UNWRAP_RE = re.compile(r"\.qualified_name\s*\(")

# A ResolutionContext being told which module its annotations were WRITTEN
# in.  Every writer is a place where a stage re-resolves syntax away from the
# pass that walked it, and the module it hands over is what decides which
# same-leaf declaration a bare name binds to: a home taken from the ambient
# cursor binds a name to whichever module the compiler is standing in.  The
# definition line has no receiver, so `.set_home_module(` matches calls only.
HOME_WRITE_RE = re.compile(r"\.set_home_module\s*\(")

# Every counted population, in the order they are rendered and compared.
KINDS = ("LOOKUP", "LOOKUP_OTHER", "REGISTER", "LOOKUP_ROUTED", "LOOKUP_LOCAL",
         "ARENA_READ", "ARENA_WRITE",
         "REGISTRY_READ", "REGISTRY_WRITE", "GRAPH_READ", "GRAPH_WRITE",
         "CONST_READ", "CONST_WRITE", "DEFAULT_READ", "DEFAULT_WRITE",
         "REENTRY", "HOME_WRITE", "DEFID_MINT", "DEFID_UNWRAP")


def strip_comment(line):
    """Drop a `//` comment tail, so a call named only in prose is not counted.

    Deliberately naive about `//` inside a string literal: no such line exists
    in this tree, and a gate that silently counted one would be worse than one
    that fails loudly when it appears.
    """
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
METHOD_HEAD_RE = re.compile(r"^    (static\s+)?([a-z_][a-z_0-9]*)\s*(?:<[^>]*>)?\s*\(")
TYPE_HEAD_RE = re.compile(r"^type\s+(?:struct|class|union|enum)\s+([A-Za-z_][A-Za-z_0-9]*)")
# `implement<T> struct Foo<T> {` / `implement Foo {` / `implement trait Tr for Foo {`:
# `this` inside the block is the type after `for` if there is one, else the
# target.  A trait impl's methods belong to the target too, but a trait
# method's signature is the trait's and is not a store's own surface, so only
# the inherent form contributes to a set (see store_methods).
IMPL_HEAD_RE = re.compile(
    r"^implement(?:\s*<[^>]*>)?\s+(?:trait\s+[^\s{]+\s+for\s+)?"
    r"(?:(?:struct|class|union|enum)\s+)?([A-Za-z_][A-Za-z_0-9]*)")
INHERENT_IMPL_RE = re.compile(
    r"^implement(?:\s*<[^>]*>)?\s+(?:(?:struct|class|union|enum)\s+)?"
    r"([A-Za-z_][A-Za-z_0-9]*)(?:\s*<[^>]*>)?\s*\{")
FIELD_RE = re.compile(r"^    ([a-z_][a-z_0-9]*)\s*:\s*&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*([A-Za-z_][A-Za-z_0-9]*)")
# A field whose type is the tree's map type (or its set form), qualified or
# not: `name_index: HashMap<u32, i64>;`, `commands: hashmap::HashMap<string,
# Command>;`.  Rule 1's candidate test.  The key is captured for `--names`
# and for nothing else.
MAP_FIELD_RE = re.compile(
    r"^    (?:public\s+|private\s+)?([a-z_][a-z_0-9]*)\s*:\s*"
    r"(?:[a-z_][a-z_0-9]*::)*(HashMap|HashSet)\s*<\s*([^,>]+)")
RETURN_RE = re.compile(r"\)\s*->\s*&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*([A-Za-z_][A-Za-z_0-9]*)")
# A receiver: segments joined by `.`, each an identifier optionally followed
# by `()` (a zero-argument accessor, placed by its declared return type) or
# `[...]` (an index, placed by the element type).  Anything else in receiver
# position is refused.
SEGMENT = r"[A-Za-z_][A-Za-z_0-9]*(?:\(\)|\[[^\[\]]*\])?"
RECEIVER = r"(%s(?:\.%s)*)" % (SEGMENT, SEGMENT)


class Tree(object):
    """Every .cryo file under `src`, read once, with the two maps rule 3
    needs: each declared type's fields by name, and each file's top-level
    block heads (which type `this` is on a given line)."""

    def __init__(self, src):
        self.src = src
        self.files = {}
        self.fields = {}
        self.returns = {}
        self.blocks = {}
        # {type_name: [(relpath, field, key_type)]}: every map-owning type
        # block, with the file it was found in.  A type declared in two files
        # keeps both, and rule 1 refuses it.
        self.map_owners = {}
        for dirpath, _dirs, names in os.walk(src):
            for fname in sorted(names):
                if not fname.endswith(".cryo"):
                    continue
                full = os.path.join(dirpath, fname)
                rel = os.path.relpath(full, src).replace(os.sep, "/")
                with open(full, "r", encoding="utf-8", errors="replace") as fh:
                    lines = fh.read().split("\n")
                self.files[rel] = lines
                self.blocks[rel] = self.scan_blocks(lines, rel)
        self.rels = sorted(self.files)

    def scan_blocks(self, lines, rel):
        """[(first_line_index, type_name)] for every top-level type or impl
        block, in order; fills `fields` for `type` blocks, `map_owners` for
        the `type` blocks with a map field, and `returns` for every method
        head at depth 1 of either."""
        heads = []
        current = None
        depth = 0
        for i, raw in enumerate(lines):
            code = STRING_RE.sub('""', strip_comment(raw))
            m = TYPE_HEAD_RE.match(code)
            if m is None:
                m = IMPL_HEAD_RE.match(code)
            if m is not None and depth == 0:
                current = m.group(1)
                heads.append((i, current))
                self.fields.setdefault(current, {})
                self.returns.setdefault(current, {})
                is_type = TYPE_HEAD_RE.match(code) is not None
            elif current is not None and depth == 1:
                mh = METHOD_HEAD_RE.match(code)
                if mh is not None:
                    head = code
                    j = i
                    while "{" not in head and j + 1 < len(lines):
                        j += 1
                        head += " " + STRING_RE.sub('""', strip_comment(lines[j])).strip()
                    r = RETURN_RE.search(head[:head.index("{")] if "{" in head else head)
                    if r is not None:
                        self.returns[current][mh.group(2)] = r.group(1)
                elif is_type:
                    f = FIELD_RE.match(code)
                    if f is not None:
                        self.fields[current][f.group(1)] = f.group(2)
                    mf = MAP_FIELD_RE.match(code)
                    if mf is not None:
                        self.map_owners.setdefault(current, []).append(
                            (rel, mf.group(1), mf.group(2), mf.group(3).strip()))
            for ch in code:
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
        return heads

    def enclosing_type(self, rel, lineno):
        """The type `this` names at 1-based `lineno` in `rel`, or None."""
        found = None
        for start, name in self.blocks[rel]:
            if start < lineno:
                found = name
            else:
                break
        return found

    def local_type(self, rel, lineno, name):
        """What `name` was last declared as before `lineno`: a parameter, a
        `const`/`mut` binding or a field of the enclosing type, whichever
        annotation is nearest above.  A struct-literal initializer
        (`arena: arena,`) yields a lowercase token that names no type and is
        skipped for the next match up.  None when nothing declares it."""
        pat = re.compile(r"\b%s\s*:\s*&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*([A-Za-z_][A-Za-z_0-9]*)"
                         % re.escape(name))
        lines = self.files[rel]
        i = lineno - 1
        while i >= 0:
            code = STRING_RE.sub('""', strip_comment(lines[i]))
            for m in pat.finditer(code):
                ty = m.group(1)
                if ty[0].isupper() or ty in self.fields:
                    return ty
            i -= 1
        return None

    def receiver_type(self, rel, lineno, receiver):
        """The declared type of a dotted receiver, walked segment by
        segment: a field by its declaration, `x()` by the method's declared
        return type, `x[...]` by the element type (the declaration regexes
        drop `[]`, so an array field already names its element).  None where
        any hop is unknown."""
        segs = re.findall(SEGMENT, receiver)
        ty = None
        for n, seg in enumerate(segs):
            call = seg.endswith(")")
            name = re.match(r"[A-Za-z_][A-Za-z_0-9]*", seg).group(0)
            if n == 0:
                if name == "this":
                    ty = self.enclosing_type(rel, lineno)
                    if call:
                        return None
                elif call:
                    return None
                else:
                    ty = self.local_type(rel, lineno, name)
            else:
                if ty is None:
                    return None
                table = self.returns if call else self.fields
                ty = table.get(ty, {}).get(name)
        return ty


def block_methods(lines, start):
    """{name: "read" | "write" | "static"} for every method declared at depth
    1 of the block opening at `start` whose head mentions a KEY TYPE.

    The head is everything from the method's name to the `{` that opens its
    body, joined across lines.  A method is a WRITE when its receiver is
    `mut &this`, STATIC when it has none, and a READ otherwise.
    """
    found = {}
    depth = 0
    entered = False
    i = start
    while i < len(lines):
        code = STRING_RE.sub('""', strip_comment(lines[i]))
        m = METHOD_HEAD_RE.match(code)
        if m and depth == 1:
            head = code
            j = i
            while "{" not in head or head.count("(") > head.count(")"):
                j += 1
                if j == len(lines):
                    raise SystemExit("lane-gate: unterminated method head at line %d" % (i + 1))
                head += " " + STRING_RE.sub('""', strip_comment(lines[j])).strip()
            head = head[:head.index("{")]
            if KEY_RE.search(head):
                if m.group(1):
                    found[m.group(2)] = "static"
                elif "mut &this" in head:
                    found[m.group(2)] = "write"
                else:
                    found[m.group(2)] = "read"
        for ch in code:
            if ch == "{":
                depth += 1
                entered = True
            elif ch == "}":
                depth -= 1
        if entered and depth == 0:
            break
        i += 1
    return found


def store_methods(tree, type_name, defn):
    """Rule 2 for one store: its `type struct` block in `defn` plus every
    inherent `implement` block naming it in ANY file.

    Refuses rather than returning an empty set: a parser that finds no
    struct, or a struct with no name-crossing method, has not measured the
    tree, and a gate fed an empty set would report OK over every call.
    """
    if defn not in tree.files:
        raise SystemExit("lane-gate: no %s under %s" % (defn, tree.src))
    head_re = re.compile(r"^type struct %s\b" % re.escape(type_name))
    struct_at = [i for i, l in enumerate(tree.files[defn]) if head_re.match(l)]
    if not struct_at:
        raise SystemExit("lane-gate: no `type struct %s` in %s" % (type_name, defn))
    found = block_methods(tree.files[defn], struct_at[0])
    for rel in tree.rels:
        lines = tree.files[rel]
        for i, line in enumerate(lines):
            m = INHERENT_IMPL_RE.match(strip_comment(line))
            if m is not None and m.group(1) == type_name:
                found.update(block_methods(lines, i))
    if not found:
        raise SystemExit("lane-gate: `%s` declares no method whose signature "
                         "mentions %s; the parser has not measured the tree"
                         % (type_name, " or ".join(KEY_TYPES)))
    return found


def place_map_owners(tree):
    """Rule 1: every map owner the tree holds is a store or an exclusion, in
    the file the table names, and every table entry owns a map.

    Refuses with every problem listed rather than the first, so one run over
    a drifted tree names everything that moved.  A map owner in neither
    table is the gate asking whoever added it which side of the line it is
    on; a table entry with no map behind it is the table drifting from the
    tree, which is how a hand-written list of stores read OK over a tree
    holding one more.
    """
    problems = []
    for name in sorted(tree.map_owners):
        rels = sorted(set(rel for rel, _f, _m, _k in tree.map_owners[name]))
        if name in STORES:
            want = STORES[name].defn
            side = "STORES"
        elif name in EXCLUDED:
            want = EXCLUDED[name].defn
            side = "EXCLUDED"
        else:
            fields = ", ".join("%s: %s<%s>" % (f, m, k) for _r, f, m, k in tree.map_owners[name])
            problems.append(
                "  `%s` (%s) owns a map (%s) and is in neither STORES nor EXCLUDED:\n"
                "      a type that owns a map holds something under a key; say which -\n"
                "      a store, with the rows its reads and writes land in, or an\n"
                "      exclusion, with the reason its key names no declaration"
                % (name, ", ".join(rels), fields))
            continue
        if rels != [want]:
            problems.append("  `%s` is listed in %s at %s but declared with a map in %s"
                            % (name, side, want, ", ".join(rels)))
    for name, st in STORES.items():
        if st.defn not in tree.files:
            problems.append("  no %s under %s" % (st.defn, tree.src))
        elif st.funnel:
            if name in tree.map_owners:
                problems.append("  `%s` is marked a funnel (no map of its own) but owns one" % name)
        elif name not in tree.map_owners:
            problems.append("  `%s` is listed in STORES but owns no map: a stale entry" % name)
    for name, ex in EXCLUDED.items():
        if ex.defn not in tree.files:
            problems.append("  no %s under %s (`%s` is listed in EXCLUDED)" % (ex.defn, tree.src, name))
        elif name not in tree.map_owners:
            problems.append("  `%s` is listed in EXCLUDED but owns no map: a stale exclusion" % name)
    if problems:
        raise SystemExit("lane-gate: rule 1 - the map owners and the tables disagree:\n"
                         + "\n".join(problems))


def scan(src):
    """Return ({kind: {relpath: count}}, unplaced, {store: set}) over `src`."""
    tree = Tree(src)
    place_map_owners(tree)
    sets = {name: store_methods(tree, name, st.defn) for name, st in STORES.items()}
    # Control on the parser: the LOOKUP row is the four names, and they are
    # declared on the index.  A parser that cannot see them cannot see the
    # row it is asked to pin.
    missing = [n for n in LOOKUPS if n not in sets[INDEX_TYPE]]
    if missing:
        raise SystemExit("lane-gate: %s does not declare %s as name-crossing; "
                         "the parser has not measured the tree"
                         % (STORES[INDEX_TYPE].defn, ", ".join(missing)))
    names = sorted(set().union(*sets.values()), key=len, reverse=True)
    alt = "|".join(names)
    # Any call to a set name: a dotted receiver, a `Type::` static, or neither
    # (which is a call this gate cannot place and refuses).  A definition line
    # has no `.` or `::` before the name, so it is not a call.
    seen_re = re.compile(r"(?:\.|::)\s*(?:%s)\s*\(" % alt)
    dotted_re = re.compile(r"%s\.(%s)\s*\(" % (RECEIVER, alt))
    static_re = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)::(%s)\s*\(" % alt)

    def row_for(store_name, name, rel):
        """The row a call to `name` on `store_name` from `rel` lands in, or
        None when it is the store's own call or not in its name-keyed set."""
        st = STORES[store_name]
        kind = sets[store_name].get(name)
        if kind is None or st.owns(rel):
            return None
        if store_name == INDEX_TYPE and name in LOOKUPS:
            return "LOOKUP"
        return st.write if kind == "write" else st.read

    found = {k: {} for k in KINDS}
    unplaced = []
    for rel in tree.rels:
        tally = {k: 0 for k in KINDS}
        for lineno, raw in enumerate(tree.files[rel], 1):
            line = strip_comment(raw)
            if not line.strip():
                continue
            seen = len(seen_re.findall(line))
            accounted = 0
            for m in dotted_re.finditer(line):
                accounted += 1
                recv, name = m.group(1), m.group(2)
                ty = tree.receiver_type(rel, lineno, recv)
                if ty is None:
                    unplaced.append((rel, lineno, recv))
                elif ty in STORES:
                    row = row_for(ty, name, rel)
                    if row is not None:
                        tally[row] += 1
                else:
                    # A same-named method on some other type: not the
                    # surface, but counted so a misplaced store shows.
                    tally["LOOKUP_LOCAL"] += 1
            for m in static_re.finditer(line):
                accounted += 1
                owner, name = m.group(1), m.group(2)
                if owner in STORES:
                    if sets[owner].get(name) == "static":
                        row = row_for(owner, name, rel)
                        if row is not None:
                            tally[row] += 1
                else:
                    tally["LOOKUP_LOCAL"] += 1
            # A match the receiver patterns did not reach at all - a call
            # on something other than a dotted name. Reported once, not
            # once per match, so the count is of CALLS and not of checks.
            for _ in range(seen - accounted):
                unplaced.append((rel, lineno, "<no simple receiver>"))
            if not STORES["Resolver"].owns(rel):
                tally["REENTRY"] += len(REENTRY_RE.findall(line))
            tally["HOME_WRITE"] += len(HOME_WRITE_RE.findall(line))
            tally["DEFID_MINT"] += len(DEFID_MINT_RE.findall(line))
            tally["DEFID_UNWRAP"] += len(DEFID_UNWRAP_RE.findall(line))
        for kind in KINDS:
            if tally[kind]:
                found[kind][rel] = tally[kind]
    return found, unplaced, sets, tree.map_owners


HEADER = [
    "# Resolution-lane surface baseline -- docs/name-resolution.md §7.2 mechanism 5.",
    "#",
    "# ASSERTED: both totals and every per-file row.",
    "#",
    "# A store is a type that owns a map, read from the tree: every map owner",
    "# is a store with rows or an exclusion with a reason, and one in neither",
    "# refuses the run; plus TypeUtils (sema's funnel). A name-keyed method is one a store",
    "# declares - inline or in an `implement` block in any file - whose",
    "# signature mentions SymbolStr or string, read from the tree on every run:",
    "# not a list of names, so a reader added under any spelling, in any file,",
    "# on any store, is inside the rule as soon as it is declared. A call is",
    "# placed by its receiver's DECLARED TYPE (this, a local's annotation, a",
    "# field's declaration), never by the receiver's spelling.",
    "#",
    "# LOOKUP         answered by the DeclarationIndex, under one of the four",
    "#                names mechanism 5 gives. The lane surface; falls.",
    "# LOOKUP_OTHER   answered by the DeclarationIndex under ANY OTHER name-",
    "#                crossing READ (entry accessors, visibility, reachability,",
    "#                the global and extern tables, a static key parser). A",
    "#                surface pinned at four names is one a caller can leave by",
    "#                switching names - which reads as progress on the row that",
    "#                is watched.",
    "# REGISTER       a name-keyed WRITE to the DeclarationIndex: a registrar",
    "#                handed a key the CALLER derived from a declaration it",
    "#                holds. Falls as registration takes the DefId instead.",
    "# LOOKUP_ROUTED  answered by a TypeUtils wrapper, any name-crossing method",
    "#                that type declares. Already at the destination, so it",
    "#                RISES as LOOKUP falls. Pinned because a new wrapper is the",
    "#                regrowth this gate exists to catch, and one under a sixth",
    "#                name was invisible to a four-name rule.",
    "# LOOKUP_LOCAL   a set name called on a receiver whose declared type is",
    "#                not a store: a type's OWN same-named method over its own",
    "#                symbol map or scope stack. Not a lane site; no migration",
    "#                removes one. A FLOOR, and the control on placement: a",
    "#                store misplaced as a local moves this row.",
    "# ARENA_READ     the arena's name-keyed reads: reverse maps and the",
    "#                display formatters.",
    "# ARENA_WRITE    the arena's creators and aliases, keyed by the spelling",
    "#                the caller minted for a declared type.",
    "# REGISTRY_READ  the GenericRegistry's name-keyed reads: templates, inherent",
    "#                impl blocks, trait declarations, trait-impl heads. Keys are",
    "#                canonical strings off stamps (bucket B, not spelling); pinned",
    "#                because it was the store no row enumerated.",
    "# REGISTRY_WRITE the GenericRegistry's registrars.",
    "# GRAPH_READ     the ModuleGraph asked by namespace or path.",
    "# GRAPH_WRITE    a name-keyed write to the ModuleGraph (none today).",
    "# CONST_READ     a name-keyed read of the ConstantTable (reads go by stamp).",
    "# CONST_WRITE    a constant or enum registered under its qualified name.",
    "# DEFAULT_READ   the DefaultRegistry (default expansion's all-default",
    "#                templates by bare leaf, built on the pass's stack) asked",
    "#                by name from outside its own file - 0, since the store",
    "#                and its only reader share the file.",
    "# DEFAULT_WRITE  the DefaultRegistry registered into from outside its file.",
    "# REENTRY  get_resolver() outside the driver, and ANY name-keyed Resolver",
    "#          method on a Resolver-typed receiver outside compiler/resolver/ and",
    "#          the driver. Name resolution is a PASS, not a service: a resolver",
    "#          called from sema no longer holds the writer's imports, so it",
    "#          answers from a string. That is where B1 comes from.",
    "# HOME_WRITE    ResolutionContext::set_home_module() calls -- every place a",
    "#               stage re-resolves syntax and says which module WROTE it.",
    "#               The module handed over decides which same-leaf declaration",
    "#               a bare name binds to, so a writer that hands over the",
    "#               ambient cursor binds by where the compiler stands. A new",
    "#               writer is a new such decision and must be placed.",
    "# DEFID_MINT    DefId::of_definition() -- where a name BECOMES a resolution",
    "#               answer. Legitimate only where the referent is known for a",
    "#               reason other than the spelling in front of it: a resolver",
    "#               that just walked to the declaration, or a read of the",
    "#               declaration index.",
    "# DEFID_UNWRAP  DefId::qualified_name() -- where an answer becomes a name",
    "#               again. Legitimate where the output IS text (a diagnostic, a",
    "#               mangled symbol); a re-keyed lookup here has re-derived what",
    "#               it was handed, and should take the DefId instead.",
    "#",
    "# Those two exist because Cryo's visibility is scoped to the declaring TYPE",
    "# rather than to a module: a constructor private enough to exclude codegen",
    "# excludes the resolver too, so DefId cannot be made mintable by the resolver",
    "# ALONE. The private field still buys one named door in each direction, and",
    "# these rows are what make the traffic through them countable.",
    "#",
    "# Every row is asserted exactly, both directions. For LOOKUP and REENTRY an",
    "# increase is regrowth and a decrease is progress that must still be re-pinned",
    "# with `make lane-check ARGS=--update`, because a tolerated decrease leaves",
    "# the old number as the ceiling. A destination or a floor row has no preferred",
    "# direction and is pinned so that an unexplained move fails.",
    "#",
    "# Source-derived, so there are no per-host sections: the same tree gives the",
    "# same answer everywhere, and splitting by host would let one host's re-pin",
    "# hide another's regression.",
]


def render(counts):
    lines = list(HEADER)
    for kind in KINDS:
        rows = counts[kind]
        lines.append("")
        lines.append("[%s]" % kind)
        lines.append("TOTAL %d" % sum(rows.values()))
        lines.append("")
        for rel in sorted(rows):
            lines.append("%-52s %d" % (rel, rows[rel]))
    return "\n".join(lines) + "\n"


def parse_golden(path):
    if not os.path.exists(path):
        return None
    counts = {k: {} for k in KINDS}
    totals = {}
    kind = None
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                kind = line[1:-1]
                continue
            if kind is None:
                continue
            parts = line.rsplit(None, 1)
            if len(parts) != 2:
                continue
            name, value = parts[0].strip(), parts[1]
            try:
                value = int(value)
            except ValueError:
                continue
            if name == "TOTAL":
                totals[kind] = value
            else:
                counts.setdefault(kind, {})[name] = value
    return counts, totals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="rewrite the golden from the current measurement")
    ap.add_argument("--names", action="store_true",
                    help="print the definition-derived name sets and exit")
    ap.add_argument("--src", default=DEFAULT_SRC,
                    help="the compiler source tree to measure (default: compiler/src)")
    ap.add_argument("--golden", default=DEFAULT_GOLDEN,
                    help="the golden to compare against (default: tests/lane-baseline.txt)")
    args = ap.parse_args()

    counts, unplaced, sets, owners = scan(args.src)
    if args.names:
        print("map owners (%d): rule 1's population, each placed" % len(owners))
        for label in sorted(owners):
            side = "STORE" if label in STORES else "excluded: " + EXCLUDED[label].reason
            print("  %-20s %s" % (label, side))
            for _rel, field, kind, key in owners[label]:
                print("      %s: %s<%s, ...>" % (field, kind, key))
        for label in STORES:
            names = sets[label]
            print("%s (%d):" % (label, len(names)))
            for name in sorted(names):
                print("  %-6s %s" % (names[name], name))
        return 0
    if unplaced:
        sys.stderr.write(
            "lane-gate: %d name-keyed call(s) could not be placed by receiver.\n"
            "A call this gate cannot classify is a call it cannot pin, and a\n"
            "silently dropped one reads as progress. The receiver's declared\n"
            "type must be readable from the tree (an annotated local or\n"
            "parameter, a declared field, or `this`).\n"
            % len(unplaced))
        for rel, lineno, recv in unplaced:
            sys.stderr.write("  %s:%d  receiver %s\n" % (rel, lineno, recv))
        return 1
    live_totals = {k: sum(v.values()) for k, v in counts.items()}
    if args.update:
        with open(args.golden, "w", encoding="utf-8", newline=chr(10)) as fh:
            fh.write(render(counts))
        print("lane-gate: golden updated -- "
              + ", ".join("%s = %d" % (k, live_totals[k]) for k in KINDS))
        return 0

    parsed = parse_golden(args.golden)
    if parsed is None:
        sys.stderr.write(
            "lane-gate: no golden at %s.\n"
            "An unpinned surface is unmeasured, and this gate must never report\n"
            "OK for a number nobody committed. Create it with:\n"
            "    make lane-check ARGS=--update\n"
            "and commit it. NOTE: .gitignore ignores *.txt repo-wide, so the\n"
            "golden needs an explicit `!tests/lane-baseline.txt` negation or CI\n"
            "fails on a fresh clone with exactly this message.\n" % args.golden)
        return 1
    gold_counts, gold_totals = parsed

    problems = []
    for kind in KINDS:
        if kind not in gold_totals:
            problems.append("  %s: golden has no TOTAL line" % kind)
            continue
        if gold_totals[kind] != live_totals[kind]:
            direction = ("INCREASE -- a lane regrew"
                         if live_totals[kind] > gold_totals[kind]
                         else "decrease -- progress; re-pin deliberately")
            problems.append("  %s TOTAL %d -> %d  (%s)"
                            % (kind, gold_totals[kind], live_totals[kind], direction))
        for rel in sorted(set(gold_counts[kind]) | set(counts[kind])):
            was = gold_counts[kind].get(rel, 0)
            now = counts[kind].get(rel, 0)
            if was != now:
                problems.append("    %-8s %-46s %d -> %d" % (kind, rel, was, now))

    if problems:
        sys.stderr.write("lane-gate: DRIFT against %s\n" % os.path.relpath(args.golden, ROOT))
        sys.stderr.write("\n".join(problems) + "\n")
        sys.stderr.write(
            "\nAn increase means a per-kind lookup or a resolver re-entry was added:\n"
            "route it through the primitive instead. A decrease is progress -- re-pin\n"
            "with `make lane-check ARGS=--update` and commit the golden with the\n"
            "change that moved it.\n")
        return 1

    print("lane-gate: OK -- "
          + ", ".join("%s = %d (%d files)" % (k, live_totals[k], len(counts[k]))
                      for k in KINDS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
