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

  1. WHICH TYPES ARE STORES.  A store is a type that OWNS A MAP: any type
     the tree declares with a field of the tree's one map type, `HashMap<K,
     V>` (or `HashSet<T>`, its set form; none today), as the compiler's
     declaration records report each field's resolved type - so a qualified
     spelling, an alias and a field declared past a brace inside a character
     literal are the field they are.  The key type is not consulted: no map in this tree is
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

  2. WHICH METHODS ARE NAME-KEYED.  Any method of a store whose signature
     mentions a KEY TYPE - `SymbolStr`, `string` or `ModulePath` - as a
     parameter (the caller hands a name in and gets an answer) or as the
     return type (the caller hands an answer in and gets a name back, the
     same boundary crossed the other way), generic arguments included.  The
     signature is the one the compiler bound the call to, as its facts
     render it (`cryo build --emit=facts`, `.facts/compiler.facts`), so a
     method added in an `implement` block in any file is the same method,
     and `std::collections::string::String` is not `string`: a type is read
     by the last segment of its path.  A gate that pinned readers by a NAME
     PATTERN (`lookup_*`) was blind to a reader called anything else, and
     the tree held twenty-four such calls under a gate that read OK; a gate
     that matched the key type as the one token `SymbolStr` was blind to a
     reader keyed by `string`.  The signature is the one thing a name-keyed
     reader cannot be written without.

     One thing a name-keyed READ can be written without is a method: the
     door's loop, written at the caller (`for (i) if
     (st.methods[i].name.equals(n))`), is the same read and mentions no
     signature.  Rule 1c (SCANNED_ARRAYS, `facts_scans`) reads that shape
     from the compiler's report of each comparison of keys - `==` / `!=`
     and a key type's `equals` / `eq` - where one operand is read off an
     element of an array that the innermost loop around the comparison
     reads, and places every scanned array as a TABLE or as DATA, refusing
     one in neither.  Read from source text, the rule missed a table whose
     element is a `Pair`, an array of interned ids compared with a key's
     `.id`, an array held in a `string[]` local or parameter, a key read
     through two locals; it counted a read by position outside any loop as
     a scan, and took the wrong side of a comparison of two elements.

  3. WHICH STORE A CALL REACHES.  The declaration the compiler bound the
     call to: every call sema resolves is a `call` record carrying its
     callee's linker symbol and rendered declaration, and a call is a
     store's when that declaration is a method of the store's type (the
     symbol names a member, docs/cryo-mangling-spec.md; the rendered owner
     is the store's path).  Receivers are not read at all: a local of any
     spelling, an accessor's return, an indexed field, a cast, a call split
     over two lines, or one on a line where a string holds `//`, is placed
     as the call it is.  Each of the last two was invisible to the pattern
     reader this replaced - it counted neither and reported OK.  A call sema
     left unpinned whose method is one a row counts is a hard failure,
     because a call this gate cannot place is one it cannot pin and a
     silently dropped one reads as progress.  Every store must be reached
     by at least one call in the facts, or the reader is refused: a store
     whose path it renders differently from the compiler would count zero.

The calls are SPLIT BY THE STORE that answers them and by READ vs WRITE:

  * LOOKUP -- answered by the `DeclarationIndex` under one of the per-kind
    names mechanism 5 gives (`lookup_type`, `lookup_func_type`,
    `lookup_method_return`; `lookup_func_return` read a second map written
    in lockstep with the signature's and is deleted, and `lookup_global`
    read a bare-leaf map that was last-write-wins across modules and is
    deleted: a global is read from its `(leaf, namespace)` slot).  This is
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
  * LOOKUP_LOCAL -- a call to a method of a type that is NOT a store, named
    like a name-keyed store method some call reaches: a type's OWN
    same-named method over its own symbol map or scope stack (`move_check`,
    `drop_insertion`, sema's `scopes`).  Not a lane site, and no migration
    can remove one, so it is a FLOOR: driving LOOKUP to zero is reachable,
    driving the total to zero never was.  It is also a control on rule 3:
    a store the reader stopped recognising moves this row.
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
    registered under their qualified name.  The reader, `ConstEval`, takes
    the STAMP off the node and enters the table by that declaration's
    canonical name - `by_qualified` under `DefId::qualified_name()`, a
    module-qualified constant under the stamped module's name and the
    written leaf, an enum's variant under the stamped enum's name - so a
    spelling never picks the entry; it shares the store's file, so its four
    reads are counted nowhere here.  CONST_READ counts the rest: today the
    key MINTED at a constant's declaration for its registration (the write
    side, in `name_resolution`) and a literal's text parsed to a value.
  * (The default-expansion pass kept a table of all-default templates keyed
    by the template's BARE leaf on its own stack, threaded through it as a
    parameter.  Rule 1's first run over the tree found it - the store the
    hand-written list had missed - and it had two rows here for one commit.
    It is deleted: the pass asks the generic registry by the annotation's
    stamp, `template_of(def)`, which mentions no key type and is no surface.
    A store whose only reader shares its file has both rows at 0 by
    construction, since a store's own calls are not counted; what such rows
    pin is that the store EXISTS, and that it is the tree's question to
    answer, not the list's.)
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

WHAT THE COUNT READS
--------------------
The calls come from the compiler's facts for the compiler project, so the
gate needs a built compiler and fresh facts (`make facts`; `check-fast`
refreshes them when stale, and a missing or stale file is refused).  So do
the inline scans rule 1c places, and what is ABOUT declarations: the same
facts carry a record per type, field, function and parameter, from which
the gate reads which types own a map or an array of names and which of
their methods take a name (rules 1 and 1b), the file a scanned array's
owner is declared in and the class that declares an inherited array (rule
1c's placement), and whether an identity type's construction is sealed.
The tree itself is read only for which files exist.  A declaration the
host's configuration prunes (`![config(linux)]` on Windows) has no record,
so the gate sees the host's half of the tree.

The facts are what THIS HOST compiled: code gated to another operating
system (`![target(...)]`) is not in them.  The golden has no per-host
sections because the two hosts give the same counts file for file - measured
with facts built for the other OS's triple (`cryo build --emit=facts
--target=<triple>`) - and a store call added inside gated code would be the
first thing to make them differ.  A file nothing imports is never compiled
and has no facts either; every file of the tree that holds a call is in them.

THINGS A PATTERN OVER SOURCE GOT WRONG, ALL OBSERVED HERE
---------------------------------------------------------
  * THE RECEIVER.  Matching `.lookup_type(` matches the NAME, so a call
    already routed through `TypeUtils` counted exactly like a raw index call,
    and a `lookup_type(&this, name)` over a local scope stack counted as one
    too though it never touches the index.  The declaration the call reached
    is what fixes that.
  * A CALL IT COULD NOT SEE.  A call whose `(` stood on the next line, or one
    after a string holding `//` on its line (read as a comment), matched no
    pattern and was neither counted nor refused.
  * A SET BUILT FROM DECLARATIONS.  `LOOKUP_LOCAL` counted `ScopeManager.
    lookup_local` because the resolver DECLARES a `lookup_local` - which
    nothing calls.  The stores' method sets are now the methods calls reach.
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
    python3 scripts/lane-gate.py [--update] [--names] [--row KIND | --rows] [--src DIR --golden FILE] [--facts FILE]

`--row KIND` prints one bucket's LIVE total and `--rows` the number of
buckets, both read from the tree and neither from the golden: a ledger row
that cites a bucket asks the gate, not a recorded file, so a stale golden
cannot satisfy it.
`--names` prints the derived sets and exits, so what the rule swept up can be
read rather than inferred.  `--src`/`--golden`/`--facts` point the gate at
another tree, golden and facts; `scripts/lane-gate-selftest.py` uses them to
drive every rule through a throwaway tree in both directions.

Exit codes: 0 match (or golden updated); 1 drift.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "compiler", "src")
DEFAULT_GOLDEN = os.path.join(ROOT, "tests", "lane-baseline.txt")

# The per-kind lookups §7.2 mechanism 5 names: the LOOKUP row.  They are
# the only names this gate holds as a list, and the list decides which ROW a
# call lands in, never whether it is counted.  `lookup_global` was the
# fourth: the bare-leaf global map it read (last-write-wins across every
# module declaring a leaf) is deleted, and a global is read from its
# `(leaf, namespace)` slot by a stamp.
LOOKUPS = (
    "lookup_type",
    "lookup_func_type",
    "lookup_method_return",
)

# The types whose presence in a signature makes a method name-keyed.  Matched
# as whole tokens: `SymbolStr[]` and `string*` count, `StringBuilder` does not.
#
# `ModulePath` is a module's canonical path in a type of its own, so a module
# door refuses a leaf; it is still a path the graph looks a module up BY, and
# typing the parameter changes what the door accepts, not what kind of read it
# is.  Counted here so that giving a door the type moves no pinned number:
# whether a door taking one leaves D32's population is a ruling, not a
# side effect of the rename.
KEY_TYPES = ("SymbolStr", "string", "ModulePath")


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

# Rule 1b's table: every type that owns NO map, owns an array of names, and
# declares a method taking a name (see `place_owners`), with the file it is
# declared in and the reason its array holds no declaration a stage looks up
# by that name.  The reasons recur: the array is an AST node's OWN WRITTEN
# SEGMENTS or names (resolution stamps them; nothing looks a declaration up
# in them); a pass's OWN RIB of the locals it bound or minted (LOOKUP_LOCAL's
# population); FILE PATHS, FLAGS or TEXTS; a DISPLAY kept beside the symbol
# that is the key; or a member table inside its own owner, asked by the
# member's leaf from the owner - the one shape that is name-keyed by design.
# An entry is REFUSED when the tree no longer has the type as a candidate (a
# stale exclusion), and a candidate in neither table is refused as a map
# owner is.  A type that owns a map is rule 1's, whatever arrays it also
# owns, and is not listed here.
EXCLUDED_ARRAYS = {
    "ASTTypeSubstituter": Excluded("compiler/AST/substituter.cryo",
                                   "the type arguments' DISPLAYS, index-aligned with the symbols the substitution is keyed by; written into the clone, never searched"),
    "AsmBlockStmtNode":   Excluded("compiler/AST/statement.cryo",
                                   "the asm block's own written clobber names, TEXTS handed to the backend"),
    "AsyncLower":         Excluded("compiler/sema/async_lower.cryo",
                                   "the lowering's OWN RIB: the frame locals it carries across suspends, by the binding names it minted or was handed"),
    "BindingCapture":     Excluded("compiler/sema/async_lower.cryo",
                                   "the lowering's OWN RIB: the captured binding names of one lambda body"),
    "BindingRename":      Excluded("compiler/sema/async_lower.cryo",
                                   "the lowering's OWN RIB: original binding names beside the fresh ones it minted"),
    "DefTable":           Excluded("compiler/resolver/res.cryo",
                                   "the definition table: every array is indexed by a DefId's position and none is searched; `register` takes the LEAF a declaration was written with, to compose its path, and no method finds an entry by name"),
    "ClassDeclNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "ClassType":          Excluded("compiler/types/user_defined.cryo",
                                   "the class's own fields and methods (`FieldInfo[]`, `MethodInfo[]`), asked by the member's leaf off the type in hand: a member table inside its owner"),
    "Command":            Excluded("CLI/_module.cryo",
                                   "one CLI command's declared arguments: FLAGS by the spelling typed"),
    "CompilationContext": Excluded("compiler/compilation_context.cryo",
                                   "include paths, FILE PATHS; the stores the context carries by pointer are placed on their own"),
    "DiagSink":           Excluded("compiler/codegen/state/diag_sink.cryo",
                                   "codegen's record of the functions whose bodies it stripped, by the LINKER SYMBOL it minted (utils/diag_sink.cryo declares an unrelated type of the same name with no array)"),
    "Diagnostic":         Excluded("compiler/diag/diagnostic.cryo",
                                   "a diagnostic's labels, children and suggestions: TEXTS"),
    "DropInserter":       Excluded("compiler/passes/drop_insertion.cryo",
                                   "the pass's OWN RIB: the bindings it tracks and the drop flags it minted, with their spans (FILE PATHS); its `lookup_type` is over its own locals"),
    "EnumDeclNode":       Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "EnumType":           Excluded("compiler/types/user_defined.cryo",
                                   "the enum's own variants and methods (`EnumVariantInfo[]`, `MethodInfo[]`), asked by the member's leaf off the type in hand: a member table inside its owner"),
    "ExternBlockNode":    Excluded("compiler/AST/declaration.cryo",
                                   "the extern block's own written include paths, FILE PATHS"),
    "FunctionDeclNode":   Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "ImplBlockNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the impl's own `This::Member` bindings and derived parameter DISPLAYS, asked by the member's leaf from inside the block that declared them: a member table inside its owner"),
    "ImportDeclNode":     Excluded("compiler/AST/declaration.cryo",
                                   "the import's own WRITTEN SEGMENTS and item names; resolution stamps them"),
    "Importer":           Excluded("compiler/bindgen/importer.cryo",
                                   "the C importer's own bookkeeping of the C spellings it has emitted, before any Cryo declaration exists to look up"),
    "LambdaExprNode":     Excluded("compiler/AST/expression.cryo",
                                   "the lambda's own captured names as WRITTEN; sema binds them"),
    "Lockfile":           Excluded("compiler/deps/lockfile.cryo",
                                   "the dependency lockfile's packages and vendored libraries by name: FILE-level records, FILE PATHS and hashes"),
    "ModuleDeclNode":     Excluded("compiler/AST/declaration.cryo",
                                   "the module declaration's own WRITTEN SEGMENTS"),
    "ModuleInfo":         Excluded("compiler/module_graph.cryo",
                                   "the graph's per-module record of NAMESPACE symbols (module identities, not declarations); asked through ModuleGraph, whose rows are the surface"),
    "ModuleKeyTable":     Excluded("compiler/build_manifest.cryo",
                                   "the build manifest's module keys by source FILE PATH"),
    "ParsedArgs":         Excluded("CLI/_module.cryo",
                                   "the command line's FLAGS by the spelling typed"),
    "Parser":             Excluded("compiler/parser/parser.cryo",
                                   "the parser's pending `static_assert` items, TEXTS, beside the doc-comment texts `ParserBase` carries"),
    "ProgramNode":        Excluded("compiler/AST/node.cryo",
                                   "the program's own written namespace name and its `static_assert` messages, TEXTS"),
    "ProjectConfig":      Excluded("compiler/project_config.cryo",
                                   "cryoconfig's link flags and source roots, FILE PATHS and FLAGS"),
    "QualifiedName":      Excluded("compiler/resolver/qualified_name.cryo",
                                   "the segments of ONE path, the string algebra a name is spelled with; holds no second declaration to choose between"),
    "ResolutionContext":  Excluded("compiler/types/resolver.cryo",
                                   "the `This::Member` bindings of the impl being resolved, asked by the member's leaf from inside it: a member table inside its owner; the generic bindings beside it are keyed by SymbolID"),
    "StringCache":        Excluded("compiler/codegen/state/string_cache.cryo",
                                   "LLVM string constants deduplicated by their TEXT"),
    "StructDeclNode":     Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node; the binding accessors are the C importer's, written onto the node"),
    "StructType":         Excluded("compiler/types/user_defined.cryo",
                                   "the struct's own fields and methods (`FieldInfo[]`, `MethodInfo[]`), asked by the member's leaf off the type in hand: a member table inside its owner"),
    "TemplateEntry":      Excluded("compiler/types/generic_registry.cryo",
                                   "the registry's row: its parameters' DISPLAYS beside `param_syms`, the key; the spelling compares call_resolver still makes over them are the residue the arena keyed by symbol retires"),
    "TraitDeclNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the trait's own associated-type declarations (`assoc_types`), asked by the member's leaf from the trait node: a member table inside its owner; its methods, base traits and generic parameters are WRITTEN members resolution stamps"),
    "TraitType":          Excluded("compiler/types/user_defined.cryo",
                                   "the trait's own associated-type and method names, asked by the member's leaf from the trait: a member table inside its owner"),
    "UnionDeclNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "VendorEntry":        Excluded("compiler/vendor/registry.cryo",
                                   "a vendored library's headers, include dirs and defines: FILE PATHS and FLAGS"),
    "VendorRegistry":     Excluded("compiler/vendor/registry.cryo",
                                   "the vendored libraries by key and name: FILE PATHS and FLAGS, as `VendorEntry` is"),
}

TABLE = "table"
DATA = "data"
IDENTITY = "identity"

# The types a row is keyed by when it is keyed by what a declaration IS
# rather than how it is spelled.  Only an IDENTITY entry's array is read
# for them: a compare of two `TypeRef`s anywhere else is ordinary code.
IDENTITY_TYPES = ("DefId", "TypeRef")


class Scanned(object):
    """Rule 1c's placement of one array a caller SCANS INLINE - `for (..) {
    if (rows[i].name.equals(n)) .. }` written at the call site instead of
    behind a door.  `defn` is the owner's declaring file, `elem` the
    element type the scan compares a key-typed field of, `kind` TABLE when
    the array is a member table (the declarations of an owner in hand, by
    their leaf - D32's population, exactly as a call to the owner's door
    would be) or DATA when what is compared is a text, a flag, a file path
    or a triple, which names no declaration.

    IDENTITY is a store's table whose rows are keyed by an identity
    (IDENTITY_TYPES) and scanned by it.  The entry stays watched rather
    than being deleted when its key stops being a name: the array must
    still be scanned by an identity-typed field, or the entry is stale,
    and a scan of it by a NAME is refused outright - where an array in no
    entry could be placed as a TABLE by adding one, the identity entry
    has to be changed first, which is the regression made visible."""

    def __init__(self, defn, elem, kind, reason):
        self.defn = defn
        self.elem = elem
        self.kind = kind
        self.reason = reason


# Every array the tree scans inline, placed.  A scan is the door's body
# written at the caller: `StructType::get_method` is `for (i) if
# (this.methods[i].name.equals(name))`, and a caller that writes that loop
# over `st.methods` itself has read the same table by the same key with no
# method call for rule 2 to see.  The rule reads the compiler's facts
# (`facts_scans`): a comparison of keys one of whose operands is read off an
# element the innermost loop reads, however many locals and members it
# passed through.  Every array so scanned must be here, labelled by the
# class that declares it (`local.<Element>` for one held in a local), and an
# entry nothing scans is stale.  The count of such scans was 0 by
# construction under rules 1-1b: a grep for `methods[i].name.equals(` found
# 11 of them in the compiler while the residue read OK.
SCANNED_ARRAYS = {
    # -- the arena's member tables: the doors' own bodies, and callers that
    #    re-wrote a door's loop with a predicate of their own --
    "StructType.fields":       Scanned("compiler/types/user_defined.cryo", "FieldInfo", TABLE,
                                       "a struct's fields by leaf off the `StructType` in hand"),
    "StructType.methods":      Scanned("compiler/types/user_defined.cryo", "MethodInfo", TABLE,
                                       "a struct's methods by leaf off the `StructType` in hand"),
    "ClassType.fields":        Scanned("compiler/types/user_defined.cryo", "FieldInfo", TABLE,
                                       "a class's fields by leaf off the `ClassType` in hand"),
    "ClassType.methods":       Scanned("compiler/types/user_defined.cryo", "MethodInfo", TABLE,
                                       "a class's methods by leaf off the `ClassType` in hand"),
    "EnumType.variants":       Scanned("compiler/types/user_defined.cryo", "EnumVariantInfo", TABLE,
                                       "an enum's variants by leaf off the `EnumType` in hand"),
    "EnumType.methods":        Scanned("compiler/types/user_defined.cryo", "MethodInfo", TABLE,
                                       "an enum's methods by leaf off the `EnumType` in hand"),
    "TraitType.required_methods": Scanned("compiler/types/user_defined.cryo", "MethodInfo", TABLE,
                                          "a trait's required methods by leaf off the `TraitType` in hand"),
    # -- the declarations' own member arrays, read by leaf from other files:
    #    the same table as the arena's, on the AST side --
    "TraitDeclNode.assoc_types":  Scanned("compiler/AST/declaration.cryo", "AssocTypeDeclNode", TABLE,
                                          "a trait's associated types by leaf off the trait node in hand"),
    "TraitDeclNode.methods":      Scanned("compiler/AST/declaration.cryo", "FunctionDeclNode", TABLE,
                                          "a trait's methods by leaf off the trait node in hand"),
    "ImplBlockNode.methods":      Scanned("compiler/AST/declaration.cryo", "MethodNode", TABLE,
                                          "an impl's methods by leaf off the impl node in hand"),
    "ImplBlockNode.generic_params": Scanned("compiler/AST/declaration.cryo", "GenericParamNode", TABLE,
                                            "an impl's generic parameters by leaf off the impl node in hand"),
    "StructDeclNode.fields":      Scanned("compiler/AST/declaration.cryo", "FieldDeclNode", TABLE,
                                          "a struct declaration's fields by leaf off the node in hand"),
    "StructDeclNode.methods":     Scanned("compiler/AST/declaration.cryo", "MethodNode", TABLE,
                                          "a struct declaration's methods by leaf off the node in hand"),
    "UnionDeclNode.fields":       Scanned("compiler/AST/declaration.cryo", "FieldDeclNode", TABLE,
                                          "a union declaration's fields by leaf off the node in hand"),
    "UnionDeclNode.methods":      Scanned("compiler/AST/declaration.cryo", "MethodNode", TABLE,
                                          "a union declaration's methods by leaf off the node in hand"),
    "ClassDeclNode.fields":       Scanned("compiler/AST/declaration.cryo", "FieldDeclNode", TABLE,
                                          "a class declaration's fields by leaf off the node in hand"),
    "ClassDeclNode.methods":      Scanned("compiler/AST/declaration.cryo", "MethodNode", TABLE,
                                          "a class declaration's methods by leaf off the node in hand"),
    "FunctionDeclNode.parameters": Scanned("compiler/AST/declaration.cryo", "VarDeclNode", TABLE,
                                           "a function's parameters by leaf off the function node in hand"),
    "ExternBlockNode.functions":  Scanned("compiler/AST/declaration.cryo", "FunctionDeclNode", TABLE,
                                          "an extern block's functions by leaf off the block in hand"),
    # -- a pass's rib of generic-parameter NODES, asked by spelling --
    # -- more declaration tables on the AST side, and the written members
    #    of a literal and a destructure --
    "EnumDeclNode.variants":      Scanned("compiler/AST/declaration.cryo", "EnumVariantNode", TABLE,
                                          "an enum declaration's variants by leaf off the node in hand"),
    "ImplBlockNode.assoc_binding_names": Scanned("compiler/AST/declaration.cryo", "SymbolStr", TABLE,
                                                 "the impl's own `This::Member` bindings by the member's leaf (the door `lookup_assoc_binding`'s body)"),
    "LambdaExprNode.captured_names": Scanned("compiler/AST/expression.cryo", "SymbolStr", TABLE,
                                             "the lambda's captured names, asked whether one is captured (its own accessor's body)"),
    "StructLiteralNode.field_inits": Scanned("compiler/AST/expression.cryo", "FieldInit", TABLE,
                                             "a struct literal's written initializers by the field's leaf"),
    "DestructureDeclNode.bindings": Scanned("compiler/AST/declaration.cryo", "DestructureBinding", TABLE,
                                            "a destructure's bindings by the source field's leaf (which binding takes a field) or the local's"),
    "ModuleInfo.written_imported": Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                           "a module's imported namespaces as the loader records them, deduplicated as each is recorded: module paths as text"),
    "ModuleInfo.written_reexports": Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                            "a module's re-exported namespaces as the loader records them, deduplicated as each is recorded: module paths as text"),
    "ModuleInfo.written_submodules": Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                             "a module's submodules as the loader records them, deduplicated as each is recorded: module paths as text"),
    "ModuleInfo.reexports":       Scanned("compiler/module_graph.cryo", "ModulePath", TABLE,
                                          "a module's re-exported modules, by identity"),
    # -- LOCAL tables: a local or parameter annotated as an array of records
    #    and scanned; the owner is out of view, the element says what it is --
    "local.MethodNode":           Scanned(None, "MethodNode", TABLE,
                                          "an impl's or a declaration's methods, held in a local, by leaf"),
    "local.MethodInfo":           Scanned(None, "MethodInfo", TABLE,
                                          "an arena type's methods, held in a local, by leaf"),
    "local.FieldInfo":            Scanned(None, "FieldInfo", TABLE,
                                          "a struct's or class's fields, held by reference in a local, by leaf"),
    "local.GenericParamNode":     Scanned(None, "GenericParamNode", TABLE,
                                          "a declaration's written generic parameters, held in a local, by leaf"),
    "local.VTableSlot":           Scanned(None, "VTableSlot", TABLE,
                                          "a class's vtable slots, held in a local, by the method's leaf"),
    "local.NegDiag":              Scanned(None, "NegDiag", DATA,
                                          "the negative test runner's expected diagnostics matched by code and severity: TEXTS"),
    "local.EmitJob":              Scanned(None, "EmitJob", DATA,
                                          "an emit job's error TEXT, asked whether it is set"),
    "local.ModulePath":           Scanned(None, "ModulePath", DATA,
                                          "a function's own worklist of modules (`imi_visible`), asked whether it already holds one: a dedupe set"),
    "local.SymbolStr":            Scanned(None, "SymbolStr", DATA,
                                          "a function's own scratch list of names (`seen`, `results`, `tie_traits`), asked whether it already holds one: a dedupe set"),
    "local.string":               Scanned(None, "string", DATA,
                                          "a function's own list of TEXTS - command-line arguments, requirement names, "
                                          "keyword and flag tables, build-manifest keys, emitted C names, did-you-mean "
                                          "candidates - asked whether it holds one"),
    "local.NegExpect":            Scanned(None, "NegExpect", DATA,
                                          "the negative test runner's expectations matched by code and severity: TEXTS"),
    "local.LockedVendor":         Scanned(None, "LockedVendor", DATA,
                                          "the lockfile's vendored libraries held in a local, by name: FILE-level records"),
    # -- ribs, texts and file paths the compiler reports scanned whose element
    #    is a pair or an interned id, which the owners' placement never
    #    listed as arrays of names --
    "Scope.ambig_name_ids":       Scanned("compiler/resolver/scope.cryo", "SymbolStr", DATA,
                                          "the resolver's rib: the ids of the names one scope binds ambiguously"),
    "Scope.overloads":            Scanned("compiler/resolver/scope.cryo", "Pair", DATA,
                                          "the resolver's rib: one scope's overloaded bindings by name id"),
    "SemaState.param_ids":        Scanned("compiler/sema/state.cryo", "SymbolStr", DATA,
                                          "sema's own rib: the parameter names of the body being checked, by id"),
    "SemaState.payload_binding_ids": Scanned("compiler/sema/state.cryo", "SymbolStr", DATA,
                                             "sema's own rib: the names a match arm's payload binds, by id"),
    "SemaState.alias_binding_ids": Scanned("compiler/sema/state.cryo", "SymbolStr", DATA,
                                           "sema's own rib: the names an alias binding introduces, by id"),
    "ValueTable.persistent_keys": Scanned("compiler/codegen/state/value_table.cryo", "SymbolStr", DATA,
                                          "codegen's own rib: the bindings whose values persist across a scope, by name id"),
    "StringCache.entries":        Scanned("compiler/codegen/state/string_cache.cryo", "Pair", DATA,
                                          "codegen's string constants by their TEXT"),
    "ModuleLoader.loading":       Scanned("compiler/module_loader.cryo", "Pair", DATA,
                                          "the files discovery is loading, by FILE PATH, each with whether it is still in progress"),
    "ParsedArgs.args":            Scanned("CLI/_module.cryo", "Pair", DATA,
                                          "the command line's options by the flag typed, and their values: TEXTS"),
    # -- the ribs, texts, file paths and C spellings rule 1b already placed
    #    on their owners, scanned inline --
    "AsyncLower.frame_locals":    Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: the frame locals it carries across suspends"),
    "BindingCapture.names":       Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: one lambda body's captured names"),
    "BindingRename.orig":         Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: original binding names beside the fresh ones it minted"),
    "PollSm.frame_names":         Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: the poll state machine's frame slots by the names it minted"),
    "RenameCtx.orig":             Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: the names a rename pass replaces"),
    "RebindCtx.names":            Scanned("compiler/sema/async_lower.cryo", "SymbolStr", DATA,
                                          "the lowering's OWN RIB: a finished `poll` body's own declarations, scope by scope, read once to give each local use the binding its scope declares - the answer codegen reads the same body by"),
    "DiagSink.stripped_func_names": Scanned("compiler/codegen/state/diag_sink.cryo", "string", DATA,
                                            "the functions whose bodies codegen stripped, by the LINKER SYMBOL it minted"),
    "DiagnosticSink.vendor_files": Scanned("compiler/diag/sink.cryo", "string", DATA,
                                           "the vendored files whose diagnostics are demoted: FILE PATHS"),
    "Importer.ec_names":          Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's bookkeeping of the enum-constant spellings it emitted"),
    "Importer.mac_names":         Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's bookkeeping of the macro spellings it emitted"),
    "Importer.seen_names":        Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's bookkeeping of the C spellings it has seen"),
    "Importer.struct_names":      Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's bookkeeping of the struct tags it emitted"),
    "Importer.type_names":        Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's bookkeeping of the typedef spellings it emitted"),
    "Importer.type_decl_tags":    Scanned("compiler/bindgen/importer.cryo", "SymbolStr", DATA,
                                          "the C importer's own type declarations by the C TAG it emitted each for: "
                                          "the header's namespace, which libclang hands over as text and no Cryo lookup reaches"),
    "CompilationContext.c_ref_alias": Scanned("compiler/compilation_context.cryo", "ModulePath", DATA,
                                          "which C import made each pending record reference: the import's alias "
                                          "module, compared by that import to find its own"),
    "Lockfile.packages":          Scanned("compiler/deps/lockfile.cryo", "LockedDep", DATA,
                                          "the lockfile's packages by name: FILE-level records"),
    "Lockfile.vendor":            Scanned("compiler/deps/lockfile.cryo", "LockedVendor", DATA,
                                          "the lockfile's vendored libraries by name: FILE-level records"),
    "ModuleKeyTable.names":       Scanned("compiler/build_manifest.cryo", "string", DATA,
                                          "the build manifest's module keys by source FILE PATH"),
    "ModuleLoader.loaded_paths":  Scanned("compiler/module_loader.cryo", "string", DATA,
                                          "the files already loaded: FILE PATHS"),
    "ModuleLoader.used_vendor_keys": Scanned("compiler/module_loader.cryo", "string", DATA,
                                             "the vendored libraries a build used, by key: FLAGS"),
    "PhaseArtifacts.object_files": Scanned("compiler/artifacts.cryo", "string", DATA,
                                           "object FILE PATHS kept for the link"),
    "QualifiedName.parts":        Scanned("compiler/resolver/qualified_name.cryo", "SymbolStr", DATA,
                                          "the segments of ONE path, the string algebra a name is spelled with"),
    # -- the stores' own rows, scanned only in their own files --
    "GenericRegistry.entries":    Scanned("compiler/types/generic_registry.cryo", "TemplateEntry", TABLE,
                                          "the registry's template rows by name and module"),
    "GenericRegistry.trait_heads": Scanned("compiler/types/generic_registry.cryo", "TraitImplHead", IDENTITY,
                                           "the registry's trait-impl heads by target type and trait identity"),
    "ModuleGraph.modules":        Scanned("compiler/module_graph.cryo", "ModuleInfo", TABLE,
                                          "the graph's modules by namespace"),
    "DeclarationIndex.module_global_keys": Scanned("compiler/decl_index.cryo", "SymbolStr", TABLE,
                                                   "the index's module-level globals by their leaf's id"),
    "DeclarationIndex.module_global_files": Scanned("compiler/decl_index.cryo", "SymbolStr", TABLE,
                                                    "the index's module-level globals by their declaring source file's id"),
    "DeclarationIndex.module_global_declared_in": Scanned("compiler/decl_index.cryo", "ModulePath", TABLE,
                                                          "the index's module-level globals by their declaring module, by identity"),
    "DeclarationIndex.overload_func_owner": Scanned("compiler/decl_index.cryo", "SymbolStr", TABLE,
                                                    "the index's overload entries by their owner's id"),
    "DeclarationIndex.overload_func_mangled": Scanned("compiler/decl_index.cryo", "SymbolStr", TABLE,
                                                      "the index's overload entries by their link symbol's id"),
    "DeclarationIndex.prelude_modules": Scanned("compiler/decl_index.cryo", "ModulePath", TABLE,
                                                "the prelude's modules, by identity"),
    "ResolutionContext.assoc_bindings": Scanned("compiler/types/resolver.cryo", "Pair", TABLE,
                                                "the impl's `This::Member` bindings by the member's leaf, inside its own door"),
    # -- texts, flags, file paths and triples: no declaration --
    "Diagnostic.labels":          Scanned("compiler/diag/diagnostic.cryo", "SpanLabel", DATA,
                                          "a diagnostic's labels: message TEXTS and span FILE PATHS"),
    "DirectiveNode.args":         Scanned("compiler/AST/pattern.cryo", "DirectiveArg", DATA,
                                          "a directive's arguments: spellings the language fixes (`packed`, `intel`), TEXTS"),
    "ModuleLoader.vendor_pins":   Scanned("compiler/module_loader.cryo", "VendorPin", DATA,
                                          "the project's vendored-library pins by library name and target triple: FILE-level records"),
    "ProgramNode.static_asserts": Scanned("compiler/AST/node.cryo", "StaticAssertItem", DATA,
                                          "`static_assert` messages, TEXTS"),
    "VendorEntry.cache":          Scanned("compiler/vendor/registry.cryo", "VendorCacheItem", DATA,
                                          "a vendored library's built artifacts by target triple: FILE PATHS"),
    "VendorRegistry.entries":     Scanned("compiler/vendor/registry.cryo", "VendorEntry", DATA,
                                          "the vendored libraries by key and name: FILE PATHS and FLAGS"),
}

# The types whose construction is sealed.  A member field is private to the
# module that declares its type, so a literal of one of these can be written
# only in that module - and only while one of its fields IS private.  Fields
# are public by default, so the seal is a property of the field list: a
# refactor that drops the last private field reopens construction to every
# module, with no error and no change at any caller.  Each entry: the type's
# definition path -> why nothing outside its module may build one.
SEALED_TYPES = {
    "compiler::resolver::res::DefId":
        "a definition's identity, a position in the DefTable: built anywhere, an "
        "in-range number silently names some real definition - one nothing "
        "registered for its holder, or one its holder never resolved to",
    "compiler::resolver::res::DefTable":
        "the table every DefId indexes: with its arrays public, any module could "
        "append an entry or rewrite a path, and an existing id would then name "
        "something its registrar never declared",
    "compiler::decl_index::OverloadId":
        "a signature's position in the index's registry: built anywhere, it names "
        "whichever entry sits at the position it was given",
    "compiler::resolver::symbol_id::SymbolID":
        "a resolver symbol's position in the symbol arena: rebuilt from a stored "
        "number, it names whichever symbol sits there, whatever was stored",
    "compiler::module_graph::ModulePath":
        "a module's identity: built from a spelling, it names a module the graph "
        "may never have registered, and every lookup keyed by it answers for text",
    "compiler::types::type_ref::TypeRef":
        "a type's handle in the arena: built from a number, it names whichever type "
        "sits at that position in whichever arena reads it",
}

# A private field keeps a type's LITERAL inside its module; it does not stop
# that module from publishing a door that builds one.  A public static that
# takes an argument and returns the sealed type is such a door - it turns
# whatever value it is handed into an identity - so on an identity type it is
# refused as surely as a public field, wherever it is declared: in the type's
# own block or in an `implement` block of it.  A parameterless one
# (`invalid()`, `none()`) names a fixed sentinel and is not a mint.
#
# The types listed here are STORES, not identities: their public constructor
# makes a new, empty store, not a value naming an existing entry, so the rule
# does not apply.  A second store can still hand out ids that collide with the
# first's positions; that hole is the store's, and is not closed by this gate.
SEALED_STORES = {"compiler::resolver::res::DefTable"}


def sealed_declarations(facts):
    """{path: (declared at, [private fields], [public minting statics])} for
    each SEALED_TYPES entry the compiler declared, from its declaration
    records: the `type` record, the `field` records marked private, and the
    `fn` records of a public static that takes a parameter and returns the
    type (the compiler's resolved return, so `This` or an alias is the same
    type).  An entry with no `type` record is absent from the answer."""
    found, private, mints = {}, {}, {}
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15 or f[12] not in SEALED_TYPES:
                continue
            if f[0] == "type":
                found[f[12]] = "%s:%s" % (f[1], f[2])
            elif f[0] == "field" and f[6] == "private":
                private.setdefault(f[12], []).append(f[13])
            elif (f[0] == "fn" and f[7] == "static" and f[6] == "public"
                  and int(f[4]) > 0 and f[5] == f[12]):
                mints.setdefault(f[12], []).append("%s (%s:%s)" % (f[13], f[1], f[2]))
    return {p: (at, private.get(p, []), mints.get(p, [])) for p, at in found.items()}


def check_sealed_types(facts):
    """Every SEALED_TYPES entry is declared, keeps at least one private field,
    and - unless it is a store - publishes no static that builds one from an
    argument, as the compiler's declaration records report it.  Refuses with
    every problem listed."""
    problems = []
    decls = sealed_declarations(facts)
    for path, reason in sorted(SEALED_TYPES.items()):
        if path not in decls:
            problems.append("  `%s` is listed in SEALED_TYPES but the compiler declares no such type: "
                            "a stale entry" % path)
            continue
        at, private, mints = decls[path]
        if not private:
            problems.append("  `%s` (%s) has no private field, so any module can write its literal and\n"
                            "      its construction is no longer sealed; it must stay sealed because it is\n"
                            "      %s" % (path, at, reason))
        if path not in SEALED_STORES:
            for mint in mints:
                problems.append("  `%s` (%s) has a public static `%s` taking an argument and returning it:\n"
                                "      any module can build one from any value through it, so its construction\n"
                                "      is not sealed; it must stay sealed because it is %s" % (path, at, mint, reason))
    if problems:
        raise SystemExit("lane-gate: sealed types - an identity type is constructible outside its module:\n"
                         + "\n".join(problems))


# The three single doors a row counts, each by the declaration the compiler
# bound the call to: (owner type's path, method).
#
# The resolver handed out to a stage.  The driver legitimately owns the
# resolver and may ask for it (`STORES["Resolver"].owners`).
REENTRY_DOOR = ("compiler::compilation_context::CompilationContext", "get_resolver")

# The door that turns an identity back into a name: `DefTable::path_of`.  A
# `DefId` holds only a position, so its path is a lookup in the table and
# every such crossing is this call.  (No row counts the other direction: only
# `DefTable::register` builds an id, the language refuses a literal outside
# `res.cryo`, and `register` takes a parent id and a leaf - it has no door
# that turns a path into an existing id.)
DEFID_PATH_DOOR = ("compiler::resolver::res::DefTable", "path_of")

# A ResolutionContext being told which module its annotations were WRITTEN
# in.  Every writer is a place where a stage re-resolves syntax away from the
# pass that walked it, and the module it hands over is what decides which
# same-leaf declaration a bare name binds to: a home taken from the ambient
# cursor binds a name to whichever module the compiler is standing in.  The
# method does not exist today; a call to one declared again lands here.
HOME_WRITE_DOOR = ("compiler::types::resolver::ResolutionContext", "set_home_module")

# Every counted population, in the order they are rendered and compared.
KINDS = ("LOOKUP", "LOOKUP_OTHER", "REGISTER", "LOOKUP_ROUTED", "LOOKUP_LOCAL",
         "ARENA_READ", "ARENA_WRITE",
         "REGISTRY_READ", "REGISTRY_WRITE", "GRAPH_READ", "GRAPH_WRITE",
         "CONST_READ", "CONST_WRITE",
         "REENTRY", "HOME_WRITE", "DEFID_PATH")


# Rule 1c's receiver: segments joined by `.`, each an identifier optionally
# followed by `()` (a zero-argument accessor, placed by its declared return
# type) or `[...]` (an index, placed by the element type).
# The owner a scanned array is placed under when it is held in a local or a
# parameter: `local.<Element>`.
LOCAL = "local"

# The facts' renderings a scan is read from (`sema/call_facts.cryo`'s header).
FACTS_LOCAL_AT = re.compile(r"^local:(?:mut )?[A-Za-z_0-9]+(?:@(\d+))?=")
FACTS_ELEMENT = re.compile(r"^element(?:@(\d+))?:")
# A key type's own comparison, and an identity's, as the facts render the callee.
FACTS_KEY_EQUALS = re.compile(
    r"^(compiler::resolver::symbol_str::SymbolStr|string|compiler::module_graph::ModulePath)"
    r"\.(equals|eq)\(")
FACTS_IDENTITY_EQUALS = re.compile(
    r"^compiler::(?:types::type_ref::(TypeRef)|resolver::res::(DefId))\.(?:equals|eq)\(")


def bare_type(t):
    """`compiler::ast::declaration::MethodNode*` -> `MethodNode`."""
    t = t.strip().lstrip("&").replace("mut ", "").strip().rstrip("*").strip()
    depth = 0
    out = []
    for ch in t:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return "".join(out).rsplit("::", 1)[-1]


def scanned_element(prov, depth, compared):
    """(owner, array, element type) of the array element the value in `prov` is
    read off, when the INNERMOST loop around the record reads it - the
    element indexed by that loop's counter, or bound to a local declared at
    its depth; None otherwise: no element, a value that passed through a
    call first, or an element the innermost loop does not read (the source
    of the key being searched for, not the array being searched).

    The owner is the type of the value the array is a member of, as the
    compiler reports it (the receiver's static type), or `local` with the
    element type as the array for an array held in a local or parameter or
    returned by a call (the owner out of view).  The element type is the type the compared member
    is read off, or `compared` when the element is itself the key.

    A record outside every loop (depth 0) searches nothing: `argv[1] ==
    "--help"` reads one element by position."""
    if depth == 0:
        return None
    segs = prov.split("<-")
    cur = depth
    prev = None
    for seg in segs:
        src = seg
        m = FACTS_LOCAL_AT.match(src)
        if m:
            if m.group(1) is not None:
                cur = int(m.group(1))
            src = src[m.end():]
        if src.startswith("call:"):
            return None
        m = FACTS_ELEMENT.match(src)
        if m is None:
            prev = src
            continue
        if m.group(1) is not None:
            cur = int(m.group(1))
        if cur != depth:
            return None
        if prev is not None and prev.startswith("field:"):
            elem = bare_type(prev[len("field:"):].rsplit(".", 1)[0])
        else:
            elem = bare_type(compared)
        inner = src[m.end():]
        m2 = FACTS_LOCAL_AT.match(inner)
        if m2:
            inner = inner[m2.end():]
        if inner.startswith("field:"):
            owner, field = inner[len("field:"):].split("<-", 1)[0].rsplit(".", 1)
            return (bare_type(owner), field, elem)
        return (LOCAL, elem, elem)
    return None


def require_facts(facts):
    if not os.path.isfile(facts):
        raise SystemExit("lane-gate: no facts at %s; run `make facts`" % facts)


def element_chain(prov):
    """The provenance from the nearest array element on - which element a
    value is read off - or None when there is none."""
    segs = prov.split("<-")
    for i, seg in enumerate(segs):
        m = FACTS_LOCAL_AT.match(seg)
        src = seg[m.end():] if m else seg
        if src.startswith("call:"):
            return None
        if FACTS_ELEMENT.match(src):
            return "<-".join([src] + segs[i + 1:])
    return None


def facts_scans(tree, facts):
    """Rule 1c's reads, from the compiler's facts: ([(rel, line, label,
    elem, key)] compared by a KEY, [(rel, line, label, elem, "-")] compared
    by an IDENTITY), `key` the other operand's provenance.  A comparison
    whose two operands are both elements the innermost loop reads (a join)
    is a read of each.  A key comparison is a `cmp` record or the `arg` record of a
    key type's `equals`/`eq`, either operand read off an element (two
    members of one element compared are one read); an identity comparison
    is the `call` record of `TypeRef`/`DefId` `equals`/`eq`, whose record
    carries the receiver's provenance and not the argument's.

    A label is `Owner.array`, the owner the class that DECLARES the array
    (a field read through a derived class's value is its base's), or
    `local.Elem`."""
    require_facts(facts)
    by_lower = {rel.lower(): rel for rel in tree.rels}
    names, identities = [], []

    def label(hit):
        owner, field, elem = hit
        if owner != LOCAL:
            owner = tree.declaring_type(owner, field)
        return ("%s.%s" % (owner, field), elem)
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15:
                raise SystemExit("lane-gate: %s: a record with %d fields, not 15; the facts "
                                 "format has changed and this reader has not" % (facts, len(f)))
            p = f[1][4:] if f[1].startswith("src/") else None
            rel = by_lower.get(p.lower()) if p is not None else None
            if rel is None:
                continue
            kind, lineno, depth = f[0], int(f[2]), int(f[14])
            if kind == "cmp" or (kind == "arg" and FACTS_KEY_EQUALS.match(f[12])):
                ops = [f[6], f[7]]
                found = [scanned_element(op, depth, f[5]) for op in ops]
                if found[0] is not None and element_chain(ops[0]) == element_chain(ops[1]):
                    found = [None, found[1]]
                for side, hit in enumerate(found):
                    if hit is not None:
                        names.append((rel, lineno) + label(hit) + (ops[1 - side],))
            elif kind == "call" and FACTS_IDENTITY_EQUALS.match(f[12]):
                m = FACTS_IDENTITY_EQUALS.match(f[12])
                hit = scanned_element(f[7], depth, m.group(1) or m.group(2))
                if hit is not None:
                    identities.append((rel, lineno) + label(hit) + ("-",))
    return names, identities


def place_inline_scans(tree, facts):
    """Rule 1c: every array the compiler reports a scan of (`facts_scans`)
    is in SCANNED_ARRAYS, as a TABLE or as DATA with its reason, with the
    element the facts give, and - unless it is held in a local - its owner
    declared in the file the entry names and declaring the array; every
    entry is still scanned somewhere.  An IDENTITY entry's array is scanned
    by an identity and never by a name.  Refuses with every problem listed.
    Returns the name scans."""
    scans, by_identity = facts_scans(tree, facts)
    problems = []
    seen = {}
    for rel, lineno, label, elem, _key in scans:
        seen.setdefault(label, []).append((rel, lineno, elem))
    for label, sites in sorted(seen.items()):
        entry = SCANNED_ARRAYS.get(label)
        rel, lineno, _elem = sites[0]
        if entry is None:
            problems.append(
                "  `%s` (%s[]) is scanned inline by a key (%s:%d, %d site%s) and is not in\n"
                "      SCANNED_ARRAYS: a loop comparing a record's name is the same read as the owner's\n"
                "      door; say which - a TABLE of declarations by leaf, or DATA with the reason the\n"
                "      field compared names no declaration"
                % (label, sites[0][2], rel, lineno, len(sites), "" if len(sites) == 1 else "s"))
            continue
        elems = sorted({e for _r, _l, e in sites})
        if elems != [entry.elem]:
            problems.append("  `%s` is listed in SCANNED_ARRAYS with element `%s` but the compiler reports `%s[]`"
                            % (label, entry.elem, "[]`, `".join(elems)))
        owner, field = label.split(".", 1)
        if owner == LOCAL:
            continue
        declared = sorted(tree.declared_in.get(owner, ()))
        if entry.defn not in declared or field not in tree.fields.get(owner, {}):
            problems.append("  `%s` is listed in SCANNED_ARRAYS at %s but `%s` is declared in %s%s"
                            % (label, entry.defn, owner, ", ".join(declared) or "no file",
                               "" if field in tree.fields.get(owner, {}) else " without a field `%s`" % field))
    identity = {}
    for rel, lineno, label, _elem, _key in by_identity:
        identity.setdefault(label, []).append((rel, lineno))
    for label, entry in sorted(SCANNED_ARRAYS.items()):
        if entry.kind == IDENTITY:
            if label in seen:
                rel, lineno, _elem = seen[label][0]
                problems.append(
                    "  `%s` is listed in SCANNED_ARRAYS as keyed by identity (%s) but is scanned by a name\n"
                    "      (%s:%d, %d site%s): compare the row's identity, or change the entry's kind\n"
                    "      and say why its key went back to a spelling"
                    % (label, " / ".join(IDENTITY_TYPES), rel, lineno, len(seen[label]),
                       "" if len(seen[label]) == 1 else "s"))
            if label not in identity:
                problems.append("  `%s` is listed in SCANNED_ARRAYS as keyed by identity but nothing in the tree scans it"
                                " by an identity (%s): a stale entry" % (label, " / ".join(IDENTITY_TYPES)))
            continue
        if label not in seen:
            problems.append("  `%s` is listed in SCANNED_ARRAYS but nothing in the tree scans it inline: a stale entry"
                            % label)
    if problems:
        raise SystemExit("lane-gate: rule 1c - the inline scans and the table disagree:\n"
                         + "\n".join(problems))
    return scans


def short_type(text):
    """The declared name a rendered type is spelled by - its path's last
    segment, with any reference, pointer, array and generic arguments
    stripped: `&compiler::ast::node::ASTNode*[]` is `ASTNode`,
    `std::collections::hashmap::HashMap<u32, i64, ...>` is `HashMap`."""
    head = text.lstrip("&").split("<", 1)[0]
    return head.rstrip("*[] ").rsplit("::", 1)[-1]


def written_type(text):
    """A rendered type with every path shortened to its last segment and no
    spaces, as a listing names it: `Pair<SymbolStr,TypeRef>`."""
    return re.sub(r"(?:[A-Za-z_][A-Za-z_0-9]*::)+", "", text).replace(" ", "")


def generic_args(text):
    """The top-level generic arguments of a rendered type, or []."""
    if "<" not in text:
        return []
    inner = text[text.index("<") + 1:text.rindex(">")]
    args, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    args.append(cur.strip())
    return args


def receiver_of(role, symbol):
    """How a declared method takes its receiver: a STATIC by its role; a
    method whose linker symbol opens its parameters with `$m` takes `mut
    &this`, a WRITE (docs/cryo-mangling-spec.md); any other receiver is a
    READ."""
    if role == "static":
        return "static"
    params = symbol.split("$F", 1)[1] if "$F" in symbol else ""
    return "write" if params.startswith("$m") else "read"


class Tree(object):
    """What placement needs from the tree's declarations, as the compiler
    reports them: the `type`, `field`, `fn` and `param` records
    `cryo build --emit=facts` writes for every declaration under `src`
    (`sema/call_facts.cryo`'s header).  Each declared type's fields by name,
    the files it is declared in, each class's base, and the populations
    rules 1 and 1b place.  A type is named by its path's last segment, as
    the tables name it.

    The compiler answers what each field's and parameter's type IS - an
    alias, a qualified spelling, a wrapped signature and a member added in
    an `implement` block in another file are the one declaration they
    resolve to - so no rule here reads a declaration's text."""

    def __init__(self, src, facts):
        self.src = src
        self.files = set()
        for dirpath, _dirs, names in os.walk(src):
            for fname in names:
                if fname.endswith(".cryo"):
                    full = os.path.join(dirpath, fname)
                    self.files.add(os.path.relpath(full, src).replace(os.sep, "/"))
        self.rels = sorted(self.files)
        by_lower = {rel.lower(): rel for rel in self.rels}
        self.fields = {}
        # {type_name: {field: key}}: the key each field's type carries, `-`
        # for none, for the record test.
        self.field_keys = {}
        # {type_name: {relpath}}: the files the compiler declares a struct,
        # class, union or enum of the name in.
        self.declared_in = {}
        # {type_name: [(relpath, field, map kind, key type)]}: every type
        # with a field of the tree's map type or its set form.  A type
        # declared in two files keeps both, and rule 1 refuses it.
        self.map_owners = {}
        # {type_name: [(relpath, field, element)]}: every type with a field
        # that is an array of names, rule 1b's candidates before the
        # signature test.
        self.array_owners = {}
        # {type_name: [(relpath, field, element)]}: every type with a field
        # that is an array of some named type or of pointers to one;
        # `record_owners` keeps the ones whose element carries a key.
        self.typed_arrays = {}
        # {class_name: base_name}: a class's base, so a field the compiler
        # reports on the receiver's type is named by the class declaring it.
        self.bases = {}
        # {type_name: {method: [param (type, key)]}}: each method and static
        # declared on the type - in its own block or an inherent `implement`
        # block, never a trait's impl, whose signature is the trait's.
        self.methods = {}
        # {type_name: {method: "read" | "write" | "static"}}: how each method
        # takes its receiver, as its linker symbol encodes it.
        self.method_kinds = {}
        require_facts(facts)
        records = []
        rel_of_type = {}
        with open(facts, encoding="utf-8") as fh:
            for line in fh:
                f = line.rstrip("\n").split("\t")
                if len(f) != 15 or f[0] not in ("type", "field", "fn", "param"):
                    continue
                if not f[1].startswith("src/"):
                    continue
                rel = by_lower.get(f[1][len("src/"):].lower())
                if rel is None:
                    raise SystemExit("lane-gate: the facts declare in %s, which is not under %s; "
                                     "run `make facts`" % (f[1], src))
                records.append((rel, f))
                if f[0] == "type" and f[7] in ("struct", "class", "union", "enum"):
                    name = short_type(f[12])
                    rel_of_type[f[12]] = rel
                    self.declared_in.setdefault(name, set()).add(rel)
                    self.fields.setdefault(name, {})
                    if f[5] not in ("-", "?"):
                        self.bases[name] = short_type(f[5])
        methods, roles = {}, {}
        for rel, f in records:
            owner = short_type(f[12])
            if f[0] == "field":
                if f[12] not in rel_of_type:
                    continue
                rel = rel_of_type[f[12]]
                field, ty = f[13], f[5]
                self.fields.setdefault(owner, {})[field] = short_type(ty)
                self.field_keys.setdefault(owner, {})[field] = f[9]
                if short_type(ty) in ("HashMap", "HashSet"):
                    args = generic_args(ty)
                    self.map_owners.setdefault(owner, []).append(
                        (rel, field, short_type(ty), written_type(args[0]) if args else "?"))
                if ty.endswith("[]"):
                    elem = ty[:-2]
                    if Tree.names_array(elem):
                        self.array_owners.setdefault(owner, []).append((rel, field, written_type(elem)))
                    elif "<" not in elem and short_type(elem)[:1].isupper() and not elem.endswith("**"):
                        self.typed_arrays.setdefault(owner, []).append((rel, field, short_type(elem)))
            elif f[0] == "fn" and f[7] in ("method", "static") and not f[8].startswith("C$tr$"):
                methods.setdefault((f[12], f[13], f[8]), [])
                roles[(f[12], f[13], f[8])] = f[7]
            elif f[0] == "param" and f[7] in ("method", "static") and not f[8].startswith("C$tr$"):
                methods.setdefault((f[12], f[13].split(":", 1)[0], f[8]), []).append((f[5], f[9]))
        for (path, name, symbol), params in methods.items():
            self.methods.setdefault(short_type(path), {}).setdefault(name, []).extend(params)
            self.method_kinds.setdefault(short_type(path), {})[name] = receiver_of(
                roles.get((path, name, symbol), "method"), symbol)

    @staticmethod
    def names_array(elem):
        """Whether an array's element is a name - a `string` or a
        `SymbolStr` - or a pair headed by one, the shape a linear name-keyed
        table takes when it stores an answer beside each name."""
        if elem.endswith("*"):
            return False
        if elem == "string" or ("<" not in elem and short_type(elem) == "SymbolStr"):
            return True
        args = generic_args(elem)
        return short_type(elem) == "Pair" and bool(args) and (
            args[0] == "string" or short_type(args[0]) == "SymbolStr")

    def declaring_type(self, owner, field):
        """The class along `owner`'s bases that declares `field`; `owner`
        when none does (a type this tree does not declare)."""
        t, seen = owner, set()
        while t is not None and t not in seen:
            if field in self.fields.get(t, {}):
                return t
            seen.add(t)
            t = self.bases.get(t)
        return owner

    def record_owners(self):
        """{type_name: [(relpath, field, element)]}: the typed arrays whose
        element is a type this tree declares with a field carrying a key -
        a record with a spelling in it, so an array of them is a table the
        owner can search by that spelling."""
        out = {}
        for owner, arrays in self.typed_arrays.items():
            for rel, field, elem in arrays:
                if any(k in KEY_TYPES for k in self.field_keys.get(elem, {}).values()):
                    out.setdefault(owner, []).append((rel, field, elem))
        return out


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


def name_taking_methods(tree, type_name):
    """Rule 1b's signature test for one type: the names of its methods and
    statics - declared in its own block or in an inherent `implement` block
    in any file, as the compiler records them - with a parameter whose type
    carries a KEY TYPE by value or by reference.  An array of keys handed in
    is a table, not a key.  A constructor builds the table rather than
    asking it, and a trait impl's method has the trait's signature, not the
    type's own surface (as `store_methods` has it); neither is read."""
    found = set()
    for name, params in tree.methods.get(type_name, {}).items():
        if any(key in KEY_TYPES and not ty.endswith("[]") for ty, key in params):
            found.add(name)
    return found


def array_candidates(tree):
    """Rule 1b's population: {type_name: (rels, fields, methods)} for every
    type block that owns NO map, owns an array of names or an array of
    records carrying a key-typed field, and declares a method taking a name.
    A map owner is rule 1's whatever else it owns; an array owner with no
    name-taking method is never asked by name, so its array is data, not a
    table."""
    arrays = {}
    for owners in (tree.array_owners, tree.record_owners()):
        for name, entries in owners.items():
            arrays.setdefault(name, []).extend(entries)
    out = {}
    for name in sorted(arrays):
        if name in tree.map_owners:
            continue
        methods = name_taking_methods(tree, name)
        if not methods:
            continue
        rels = sorted(set(rel for rel, _f, _e in arrays[name]))
        fields = ["%s: %s[]" % (f, e) for _r, f, e in arrays[name]]
        out[name] = (rels, fields, sorted(methods))
    return out


def place_array_owners(tree):
    """Rule 1b: every owner of an array of names, or of an array of records
    carrying a key-typed field, that owns no map and takes a name is a store
    or an array exclusion, in the file the table names, and every array
    exclusion is still such a candidate.

    The map rule cannot reach a table that is an array: a linear search over
    `SymbolStr[]` answers "which declaration is spelled X" as a map does, and
    the parser's `ident <` tables and the substitution chain's parameter
    lists were both this shape.  Nor can the name-array test reach a table
    of RECORDS: `StructType.fields: FieldInfo[]` searched by
    `fields[i].name.equals(name)` is the member table of every struct in the
    program, and it was in neither rule.  Refuses with every problem listed.
    """
    problems = []
    candidates = array_candidates(tree)
    for name in sorted(candidates):
        rels, fields, methods = candidates[name]
        if name in STORES:
            want = STORES[name].defn
            side = "STORES"
        elif name in EXCLUDED_ARRAYS:
            want = EXCLUDED_ARRAYS[name].defn
            side = "EXCLUDED_ARRAYS"
        else:
            problems.append(
                "  `%s` (%s) owns an array of names or records (%s), takes a name (%s) and is in\n"
                "      neither STORES nor EXCLUDED_ARRAYS: a linear search over an array of names\n"
                "      is a table; say which - a store, with the rows its reads and writes land\n"
                "      in, or an exclusion, with the reason the array holds no declaration a\n"
                "      stage looks up by that name"
                % (name, ", ".join(rels), ", ".join(fields), ", ".join(methods)))
            continue
        if rels != [want]:
            problems.append("  `%s` is listed in %s at %s but declared with an array of names in %s"
                            % (name, side, want, ", ".join(rels)))
    for name, ex in EXCLUDED_ARRAYS.items():
        if ex.defn not in tree.files:
            problems.append("  no %s under %s (`%s` is listed in EXCLUDED_ARRAYS)" % (ex.defn, tree.src, name))
        elif name in tree.map_owners:
            problems.append("  `%s` is listed in EXCLUDED_ARRAYS but owns a map: rule 1's tables place it" % name)
        elif name not in candidates:
            problems.append("  `%s` is listed in EXCLUDED_ARRAYS but owns no array of names or records, or takes no name: a stale exclusion" % name)
    if problems:
        raise SystemExit("lane-gate: rule 1b - the array owners and the tables disagree:\n"
                         + "\n".join(problems))


def store_path(name, st):
    """A store's type path as the compiler renders it: its module is its
    file's path under the source root."""
    return "::".join(st.defn[:-len(".cryo")].split("/")) + "::" + name


def callee_of(text, symbol):
    """(owner type path, method, receiver) of a call the compiler bound to a
    method - `read` (`&this`), `write` (`mut &this`) or `static` - read off
    its rendered declaration and its linker symbol; None for a free function.

    The symbol says whether the callee is a member at all (its path names a
    member with `-`, docs/cryo-mangling-spec.md), which the rendered text
    cannot: `a::b::f` is a function `f` of module `a::b` or a static of a
    type `b`, and only the declaration knows which."""
    path = symbol[2:] if symbol.startswith("C$") else ""
    if len(path) > 3 and path[2] == "$" and path[:2].isalpha():
        path = path[3:]
    if "-" not in path.split("$F", 1)[0]:
        return None
    head = text.split("(", 1)[0]
    depth = 0
    flat = []
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            flat.append(ch)
    head = "".join(flat)
    last = head.rsplit("::", 1)[-1]
    params = text.split("(", 1)[1] if "(" in text else ""
    if "." in last:
        owner, meth = head.rsplit(".", 1)
        return owner, meth, "write" if params.startswith("mut &this") else "read"
    if "::" in head:
        owner, meth = head.rsplit("::", 1)
        return owner, meth, "static"
    return None


# A type path in a rendered signature; its last segment names the type.
TYPE_PATH_RE = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*")


def mentions_key(text):
    """Whether a rendered callee's signature - its parameters or its return,
    generic arguments included - names a KEY TYPE.  Read by each type's last
    segment, so `std::collections::string::String` is not `string`."""
    sig = text[text.index("("):] if "(" in text else ""
    return any(t.rsplit("::", 1)[-1] in KEY_TYPES for t in TYPE_PATH_RE.findall(sig))


def count_facts(tree, facts):
    """Rule 2 and rule 3, from the compiler's own report: ({kind: {relpath:
    count}}, unplaced, {store: {method: read|write|static}}).

    Every `call` record in `facts` is one call sema bound, with the
    declaration it reached.  A call is a store's when the declaration is a
    method of the store's type, and name-keyed when that declaration's
    signature names a key type - the compiler's answer to both, so a
    receiver spelled any way, a cast, a turbofish or a chained call is
    placed as the call it is.  A call sema left unpinned whose written
    method is one a row counts is REFUSED: the compiler could not say
    which declaration it reaches, and one dropped reads as progress."""
    by_lower = {rel.lower(): rel for rel in tree.rels}
    paths = {store_path(name, st): name for name, st in STORES.items()}
    counted = []
    sets = {name: {} for name in STORES}
    others = []
    unpinned = []
    seen_store = set()
    require_facts(facts)
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15:
                raise SystemExit("lane-gate: %s: a record with %d fields, not 15; the facts "
                                 "format has changed and this reader has not" % (facts, len(f)))
            if f[0] != "call":
                continue
            p = f[1][4:] if f[1].startswith("src/") else None
            rel = by_lower.get(p.lower()) if p is not None else None
            if rel is None:
                continue
            site = (rel, int(f[2]), int(f[3]))
            if f[9] == "none":
                spelled = f[12]
                unpinned.append((site, re.split(r"[.:]", spelled.split("(", 1)[0])[-1], spelled))
                continue
            c = callee_of(f[12], f[8])
            if c is None:
                continue
            owner, meth, recv = c
            door = (owner, meth)
            if door == REENTRY_DOOR:
                if not STORES["Resolver"].owns(rel):
                    counted.append(("REENTRY", site))
                continue
            if door == DEFID_PATH_DOOR:
                counted.append(("DEFID_PATH", site))
                continue
            if door == HOME_WRITE_DOOR:
                counted.append(("HOME_WRITE", site))
                continue
            store = paths.get(owner)
            if store is None:
                others.append((site, meth))
                continue
            seen_store.add(store)
            if not mentions_key(f[12]):
                continue
            sets[store][meth] = recv
            st = STORES[store]
            if st.owns(rel):
                continue
            if store == INDEX_TYPE and meth in LOOKUPS:
                counted.append(("LOOKUP", site))
            else:
                counted.append((st.write if recv == "write" else st.read, site))
    # Control on the reader: every store is reached by some call, its own
    # file's included.  A store whose path the reader renders differently
    # from the compiler would otherwise count zero and read as progress.
    missing = sorted(set(STORES) - seen_store)
    if missing:
        raise SystemExit("lane-gate: no call in %s reaches %s; the reader does not place "
                         "calls on that store" % (facts, ", ".join(
                             "%s (%s)" % (n, store_path(n, STORES[n])) for n in missing)))
    names = set().union(*sets.values())
    # A same-named method on some other type: not the surface, but counted
    # so that a store the reader stopped recognising shows as a move here.
    for site, meth in others:
        if meth in names:
            counted.append(("LOOKUP_LOCAL", site))
    watched = names | {REENTRY_DOOR[1], DEFID_PATH_DOOR[1], HOME_WRITE_DOOR[1]}
    unplaced = [(site[0], site[1], spelled) for site, meth, spelled in unpinned if meth in watched]
    found = {k: {} for k in KINDS}
    for kind, (rel, _line, _col) in sorted(set(counted)):
        found[kind][rel] = found[kind].get(rel, 0) + 1
    return found, unplaced, sets


def scan(src, facts):
    """Return ({kind: {relpath: count}}, unplaced, {store: set}) over `src`,
    counting from `facts`."""
    tree = Tree(src, facts)
    place_map_owners(tree)
    place_array_owners(tree)
    scans = place_inline_scans(tree, facts)
    check_sealed_types(facts)
    found, unplaced, sets = count_facts(tree, facts)
    return found, unplaced, sets, (tree.map_owners, array_candidates(tree), scans)


HEADER = [
    "# Resolution-lane surface baseline -- docs/name-resolution.md §7.2 mechanism 5.",
    "#",
    "# ASSERTED: both totals and every per-file row.",
    "#",
    "# A store is a type that owns a map, read from the tree: every map owner",
    "# is a store with rows or an exclusion with a reason, and one in neither",
    "# refuses the run; plus TypeUtils (sema's funnel). The calls are the",
    "# compiler's own report (`make facts`, .facts/compiler.facts): a call is a",
    "# store's when the declaration it was bound to is a method of the store's",
    "# type, and name-keyed when that declaration's signature mentions",
    "# SymbolStr, string or ModulePath - not a list of names, so a reader added",
    "# under any spelling, in any file, on any store, is inside the rule as",
    "# soon as it is called. Receivers are not read: the compiler placed the",
    "# call.",
    "#",
    "# LOOKUP         answered by the DeclarationIndex, under one of the",
    "#                per-kind names mechanism 5 gives. The lane surface; falls.",
    "# LOOKUP_OTHER   answered by the DeclarationIndex under ANY OTHER name-",
    "#                crossing READ (entry accessors, visibility, reachability,",
    "#                the global and extern tables, a static key parser). A",
    "#                surface pinned at a few names is one a caller can leave by",
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
    "# LOOKUP_LOCAL   a method of a type that is not a store, sharing its name",
    "#                with a name-keyed store method some call reaches: a",
    "#                type's OWN same-named method over its own symbol map or",
    "#                scope stack. Not a lane site; no migration removes one. A",
    "#                FLOOR, and a control: a store the reader stopped",
    "#                recognising moves this row.",
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
    "# CONST_READ     a name-keyed read of the ConstantTable (ConstEval enters it by",
    "#                the stamp's canonical name, from the store's own file).",
    "# CONST_WRITE    a constant or enum registered under its qualified name.",
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
    "# DEFID_PATH    DefTable::path_of() -- where an identity becomes a name",
    "#               again. Legitimate where the output IS text (a diagnostic, a",
    "#               mangled symbol) or a store is still keyed by the path; a",
    "#               lookup keyed by it has re-derived what it was handed, and",
    "#               should take the DefId instead.",
    "#",
    "# Every row is asserted exactly, both directions. For LOOKUP and REENTRY an",
    "# increase is regrowth and a decrease is progress that must still be re-pinned",
    "# with `make lane-check ARGS=--update`, because a tolerated decrease leaves",
    "# the old number as the ceiling. A destination or a floor row has no preferred",
    "# direction and is pinned so that an unexplained move fails.",
    "#",
    "# No per-host sections: the facts are what one host compiled, and both",
    "# hosts' facts give these counts file for file (code gated to one OS",
    "# calls no store). Splitting by host would let one host's re-pin hide",
    "# another's regression; a store call added inside gated code is the",
    "# first thing that would make the two differ.",
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
    ap.add_argument("--row", metavar="KIND",
                    help="print one bucket's live total, read from the tree, and exit")
    ap.add_argument("--rows", action="store_true",
                    help="print the number of buckets this gate counts and exit")
    ap.add_argument("--src", default=DEFAULT_SRC,
                    help="the compiler source tree to measure (default: compiler/src)")
    ap.add_argument("--golden", default=DEFAULT_GOLDEN,
                    help="the golden to compare against (default: tests/lane-baseline.txt)")
    ap.add_argument("--facts",
                    help="the compiler's report of its calls (default: .facts/compiler.facts, "
                         "refused when missing or stale; `make facts`)")
    args = ap.parse_args()

    # The bucket count is a property of this script, not of a golden: a
    # ledger row that asks it here asks the tree's gate, not a recorded file.
    if args.rows:
        print(len(KINDS))
        return 0
    if args.row is not None and args.row not in KINDS:
        sys.stderr.write("lane-gate: no bucket named %s (%s)\n" % (args.row, ", ".join(KINDS)))
        return 1

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import parse_cache
    facts = args.facts
    if facts is None:
        import facts as facts_mod
        facts = facts_mod.facts_path("compiler")
    counts, unplaced, sets, (owners, array_owners, scans) = parse_cache.memo(
        "lane-scan", [os.path.abspath(__file__), os.path.abspath(facts)], args.src,
        lambda: scan(args.src, facts))
    if args.row is not None:
        # A live total, read from the tree.  Refused on an unplaceable
        # receiver below like every other read, since a count over a tree
        # the gate could not place is not a measurement.
        if not unplaced:
            print(sum(counts[args.row].values()))
            return 0
    if args.names:
        print("map owners (%d): rule 1's population, each placed" % len(owners))
        for label in sorted(owners):
            side = "STORE" if label in STORES else "excluded: " + EXCLUDED[label].reason
            print("  %-20s %s" % (label, side))
            for _rel, field, kind, key in owners[label]:
                print("      %s: %s<%s, ...>" % (field, kind, key))
        print("array owners (%d): rule 1b's population - no map, an array of names, a name-taking method - each placed"
              % len(array_owners))
        for label in sorted(array_owners):
            _rels, fields, methods = array_owners[label]
            side = "STORE" if label in STORES else "excluded (array): " + EXCLUDED_ARRAYS[label].reason
            print("  %-20s %s" % (label, side))
            print("      %s; takes a name in: %s" % (", ".join(fields), ", ".join(methods)))
        by_array = {}
        for rel, lineno, label, _elem, _key in scans:
            by_array.setdefault(label, []).append("%s:%d" % (rel, lineno))
        print("scanned arrays (%d): rule 1c's population - an element's key compared inline - each placed"
              % len(by_array))
        for label in sorted(by_array):
            sc = SCANNED_ARRAYS[label]
            print("  %-36s %s: %s" % (label, sc.kind.upper(), sc.reason))
            print("      %s[] scanned at %s" % (sc.elem, ", ".join(by_array[label])))
        for label in STORES:
            names = sets[label]
            print("%s (%d):" % (label, len(names)))
            for name in sorted(names):
                print("  %-6s %s" % (names[name], name))
        return 0
    if unplaced:
        sys.stderr.write(
            "lane-gate: %d call(s) to a method a row counts, which the compiler\n"
            "left unpinned: it could not say which declaration each reaches.\n"
            "A call this gate cannot place is a call it cannot pin, and a\n"
            "silently dropped one reads as progress.\n"
            % len(unplaced))
        for rel, lineno, spelled in unplaced:
            sys.stderr.write("  %s:%d  %s\n" % (rel, lineno, spelled))
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
    # A golden section the gate does not count is a row nobody measures: a
    # retired bucket left in the file would read OK forever.
    for kind in sorted(set(gold_totals) - set(KINDS)):
        problems.append("  %s: golden carries a section this gate does not count; re-pin" % kind)
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
