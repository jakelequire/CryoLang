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

     One thing a name-keyed READ can be written without is a method: the
     door's loop, written at the caller (`for (i) if
     (st.methods[i].name.equals(n))`), is the same read and mentions no
     signature.  Rule 1c (SCANNED_ARRAYS, `inline_scans`) reads that shape
     from the tree - an element of a typed array or of a local array of
     records, directly or through a local bound to it, whose key-typed
     field (or the element itself, for an array of names; or its `.id`) is
     compared with `.equals(` / `.eq(` / `==` / `!=` - and places every
     scanned (owner, array) as a TABLE or as DATA, refusing one in neither.
     The tree held 156 such scans, 145 of them outside the owner's file,
     under a residue of 294 method calls that read OK.

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
    python3 scripts/lane-gate.py [--update] [--names] [--row KIND | --rows] [--src DIR --golden FILE]

`--row KIND` prints one bucket's LIVE total and `--rows` the number of
buckets, both read from the tree and neither from the golden: a ledger row
that cites a bucket asks the gate, not a recorded file, so a stale golden
cannot satisfy it.
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
    "ClassDeclNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "ClassType":          Excluded("compiler/types/user_defined.cryo",
                                   "the class's own fields and methods (`FieldInfo[]`, `MethodInfo[]`), asked by the member's leaf off the type in hand: a member table inside its owner"),
    "Command":            Excluded("CLI/_module.cryo",
                                   "one CLI command's declared arguments: FLAGS by the spelling typed"),
    "CompilationContext": Excluded("compiler/compilation_context.cryo",
                                   "include paths, FILE PATHS; the stores the context carries by pointer are placed on their own"),
    "DeclarationNode":    Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's own attached directives, asked by the directive's kind - a spelling the language fixes (`inline`, `cfg`, `test`), TEXTS, not a declaration looked up"),
    "DiagSink":           Excluded("compiler/codegen/state/diag_sink.cryo",
                                   "codegen's record of the functions whose bodies it stripped, by the LINKER SYMBOL it minted (utils/diag_sink.cryo declares an unrelated type of the same name with no array)"),
    "Diagnostic":         Excluded("compiler/diag/diagnostic.cryo",
                                   "a diagnostic's labels, children and suggestions: TEXTS"),
    "DirectiveRegistry":  Excluded("compiler/passes/directive_processing.cryo",
                                   "the directives observed in a module, asked whether any is of a kind - a spelling the language fixes, TEXTS"),
    "DropInserter":       Excluded("compiler/passes/drop_insertion.cryo",
                                   "the pass's OWN RIB: the bindings it tracks and the drop flags it minted, with their spans (FILE PATHS); its `lookup_type` is over its own locals"),
    "EnumDeclNode":       Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "EnumType":           Excluded("compiler/types/user_defined.cryo",
                                   "the enum's own variants and methods (`EnumVariantInfo[]`, `MethodInfo[]`), asked by the member's leaf off the type in hand: a member table inside its owner"),
    "ErrorType":          Excluded("compiler/types/generic.cryo",
                                   "a diagnostic's notes, TEXTS"),
    "ExternBlockNode":    Excluded("compiler/AST/declaration.cryo",
                                   "the extern block's own written include paths, FILE PATHS"),
    "FunctionDeclNode":   Excluded("compiler/AST/declaration.cryo",
                                   "the declaration's OWN WRITTEN members - its parameters, fields, methods, variants and `where` bounds as spelled; resolution stamps them, and the name-taking methods are writes onto the node"),
    "ImplBlockNode":      Excluded("compiler/AST/declaration.cryo",
                                   "the impl's own `This::Member` bindings and derived parameter DISPLAYS, asked by the member's leaf from inside the block that declared them: a member table inside its owner"),
    "ImportDeclNode":     Excluded("compiler/AST/declaration.cryo",
                                   "the import's own WRITTEN SEGMENTS and item names; resolution stamps them"),
    "ImportReport":       Excluded("compiler/bindgen/importer.cryo",
                                   "the C importer's report TEXTS: what it skipped, approximated or ignored"),
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
    "ParserBase":         Excluded("compiler/parser/parser_base.cryo",
                                   "pending doc-comment TEXTS; the parser's name tables that decided `ident <` by module-local spelling are deleted (the turbofish)"),
    "PhaseArtifacts":     Excluded("compiler/artifacts.cryo",
                                   "object and source FILE PATHS kept for the link"),
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
    "Suggestion":         Excluded("compiler/diag/suggestion.cryo",
                                   "a suggestion's replacement parts, TEXTS"),
    "TemplateEntry":      Excluded("compiler/types/generic_registry.cryo",
                                   "the registry's row: its parameters' DISPLAYS beside `param_syms`, the key; the spelling compares call_resolver still makes over them are the residue the arena keyed by symbol retires"),
    "TokenStream":        Excluded("compiler/artifacts.cryo",
                                   "a file's tokens, TEXTS handed on to the parser"),
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
# method call for rule 2 to see.  The rule reads the tree: an element of a
# typed array (`<recv>.<field>[i]`, the receiver placed by declared type, the
# field one of the owner's `Elem[]` / `Elem*[]` arrays) - or a local bound
# to one, for the block it is bound in - whose key-typed field (walked through
# the element's declared fields, `m.func.name`) is compared with `.equals(`
# or `==` / `!=`, in either operand position.  Every (owner, array) so
# scanned must be here, and an entry the tree no longer scans is stale.
# The count of such scans was 0 by construction under rules 1-1b: a grep for
# `methods[i].name.equals(` found 11 of them in the compiler while the
# residue read OK, and this table is what the shape reaches in full.
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
    "ImplBlockNode.where_bounds": Scanned("compiler/AST/declaration.cryo", "TraitBound", TABLE,
                                          "an impl's `where` bounds by the bounded parameter's leaf"),
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
    "FunctionDeclNode.trait_bounds": Scanned("compiler/AST/declaration.cryo", "TraitBound", TABLE,
                                             "a function's `where` bounds by the bounded parameter's leaf"),
    "ExternBlockNode.functions":  Scanned("compiler/AST/declaration.cryo", "FunctionDeclNode", TABLE,
                                          "an extern block's functions by leaf off the block in hand"),
    # -- a pass's rib of generic-parameter NODES, asked by spelling --
    "SemaState.symbolic_owner_param_nodes": Scanned("compiler/sema/state.cryo", "GenericParamNode", TABLE,
                                                    "the owner's generic parameters under symbolic check, asked whether a spelling is one of them"),
    "SemaState.symbolic_method_param_nodes": Scanned("compiler/sema/state.cryo", "GenericParamNode", TABLE,
                                                     "the method's generic parameters under symbolic check, asked whether a spelling is one of them"),
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
    "TemplateEntry.param_names":  Scanned("compiler/types/generic_registry.cryo", "SymbolStr", TABLE,
                                          "the template's parameter DISPLAYS, compared by spelling - the compares the arena keyed by symbol retires"),
    "ModuleInfo.imported_namespaces": Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                              "a module's imported namespaces: module identities by path"),
    "ModuleInfo.reexports":       Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                          "a module's re-exported namespaces: module identities by path"),
    "ModuleInfo.submodules":      Scanned("compiler/module_graph.cryo", "SymbolStr", TABLE,
                                          "a module's submodules: module identities by path"),
    # -- LOCAL tables: a local or parameter annotated as an array of records
    #    and scanned; the owner is out of view, the element says what it is --
    "local.MethodNode":           Scanned(None, "MethodNode", TABLE,
                                          "an impl's or a declaration's methods, held in a local, by leaf"),
    "local.MethodInfo":           Scanned(None, "MethodInfo", TABLE,
                                          "an arena type's methods, held in a local, by leaf"),
    "local.FieldInfo":            Scanned(None, "FieldInfo", TABLE,
                                          "a struct's or class's fields, held by reference in a local, by leaf"),
    "local.FieldDeclNode":        Scanned(None, "FieldDeclNode", TABLE,
                                          "a declaration's written fields, held in a local, by leaf"),
    "local.GenericParamNode":     Scanned(None, "GenericParamNode", TABLE,
                                          "a declaration's written generic parameters, held in a local, by leaf"),
    "local.TraitBound":           Scanned(None, "TraitBound", TABLE,
                                          "`where` bounds, held in a local, by the bounded parameter's leaf"),
    "local.VTableSlot":           Scanned(None, "VTableSlot", TABLE,
                                          "a class's vtable slots, held in a local, by the method's leaf"),
    "local.NegDiag":              Scanned(None, "NegDiag", DATA,
                                          "the negative test runner's expected diagnostics matched by code and severity: TEXTS"),
    "local.EmitJob":              Scanned(None, "EmitJob", DATA,
                                          "an emit job's error TEXT, asked whether it is set"),
    "local.SymbolStr":            Scanned(None, "SymbolStr", DATA,
                                          "a function's own scratch list of names (`seen`, `results`, `tie_traits`), asked whether it already holds one: a dedupe set"),
    "local.DirectiveNode":        Scanned(None, "DirectiveNode", DATA,
                                          "directives held in a local, by kind: spellings the language fixes, TEXTS"),
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
    "DeclarationNode.attached_directives": Scanned("compiler/AST/declaration.cryo", "DirectiveNode", DATA,
                                                   "a declaration's attached directives by kind: spellings the language fixes, TEXTS"),
    "DirectiveRegistry.records":  Scanned("compiler/passes/directive_processing.cryo", "DirectiveRecord", DATA,
                                          "the directives observed in a module by kind: spellings the language fixes, TEXTS"),
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
    "ParserBase.pending_doc_comments": Scanned("compiler/parser/parser_base.cryo", "string", DATA,
                                               "pending doc-comment TEXTS"),
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
# module, with no error and no change at any caller, and the mint rows below
# would go on counting a door that is no longer the only one.  Each entry:
# (declaring file, why nothing outside that module may build one).
SEALED_TYPES = {
    "DefId": ("compiler/resolver/res.cryo",
              "a definition's identity: built anywhere, it could be made to name a definition "
              "nothing registered, or one its holder never resolved to"),
    "OverloadId": ("compiler/resolver/res.cryo",
                   "a signature's position in the index's registry: built anywhere, it names "
                   "whichever entry sits at the position it was given"),
}

SEALED_HEAD_RE = re.compile(r"^\s*(?:public\s+|private\s+)?type\s+(?:struct|class)\s+(\w+)\b")
SEALED_LABEL_RE = re.compile(r"^\s*(public|private|protected)\s*:\s*")
SEALED_FIELD_RE = re.compile(r"^\s*(?:(public|private|protected)\s+)?(?:mut\s+)?[A-Za-z_]\w*\s*:\s*[^;()]+;")


def private_fields(lines, head):
    """The fields declared private in the type block opening at `lines[head]`:
    under a `private:` label or with a leading `private`.  A struct's
    members are public until a label says otherwise."""
    out = []
    depth = 0
    opened = False
    section = "public"
    for raw in lines[head:]:
        code = strip_comment(raw)
        if depth == 1:
            rest = code
            m = SEALED_LABEL_RE.match(rest)
            if m is not None:
                section = m.group(1)
                rest = rest[m.end():]
            f = SEALED_FIELD_RE.match(rest)
            if f is not None and (f.group(1) or section) == "private":
                out.append(rest.strip())
        depth += code.count("{") - code.count("}")
        opened = opened or depth > 0
        if opened and depth <= 0:
            break
    return out


def check_sealed_types(tree):
    """Every SEALED_TYPES entry is declared in its file and keeps at least
    one private field.  Refuses with every problem listed."""
    problems = []
    for name, (defn, reason) in sorted(SEALED_TYPES.items()):
        lines = tree.files.get(defn)
        head = None
        for i, raw in enumerate(lines or []):
            m = SEALED_HEAD_RE.match(strip_comment(raw))
            if m is not None and m.group(1) == name:
                head = i
                break
        if head is None:
            problems.append("  `%s` is listed in SEALED_TYPES but %s declares no such type: a stale entry"
                            % (name, defn))
            continue
        if not private_fields(lines, head):
            problems.append("  `%s` (%s) has no private field, so any module can write its literal and\n"
                            "      its construction is no longer sealed; it must stay sealed because it is\n"
                            "      %s" % (name, defn, reason))
    if problems:
        raise SystemExit("lane-gate: sealed types - an identity type is constructible outside its module:\n"
                         + "\n".join(problems))


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
         "CONST_READ", "CONST_WRITE",
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
# A field that is an ARRAY OF NAMES: `param_names: SymbolStr[];`, `parts:
# string[];`, or an array of pairs headed by a name (`Pair<SymbolStr, TypeRef>[]`,
# the shape a linear name-keyed table takes when it stores an answer beside
# each name).  Rule 1b's candidate test: a name-keyed table needs no map - a
# linear search over an array of names answers the same question - and a
# type that owns one AND declares a method taking a name is where such a
# table is asked.  Every such type is placed, as rule 1's map owners are.
NAME_ARRAY_RE = re.compile(
    r"^    (?:public\s+|private\s+)?([a-z_][a-z_0-9]*)\s*:\s*"
    r"((?:[a-z_][a-z_0-9]*::)*(?:SymbolStr|string)|(?:[a-z_][a-z_0-9]*::)*Pair\s*<\s*(?:SymbolStr|string)\s*,[^;]*>)"
    r"\s*\[\]\s*;")
# A field that is an ARRAY OF RECORDS, `fields: FieldInfo[];`, `assoc_types:
# AssocTypeDeclNode*[];`: a candidate when the record is a type declared in
# the tree with a key-typed field of its own, because `if (rows[i].name.equals(n))`
# over such an array is the same table as an array of names with the answer
# stored beside each - the shape `StructType`, `ClassType` and `EnumType`
# keep their members in, invisible to the name-array test above.  The
# element is resolved against the tree after every file is read, so the
# record may be declared anywhere.
RECORD_ARRAY_RE = re.compile(
    r"^    (?:public\s+|private\s+)?([a-z_][a-z_0-9]*)\s*:\s*"
    r"(?:[a-z_][a-z_0-9]*::)*([A-Z][A-Za-z_0-9]*)\s*\*?\s*\[\]\s*;")
# A key type in PARAMETER position: `name: SymbolStr`, `s: string`, `&SymbolStr`,
# never `SymbolStr[]` (an array handed in is a table, not a key).  Rule 1b's
# signature test, the half of rule 2's that says "the caller hands a name in".
KEY_PARAM_RE = re.compile(r":\s*&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*(?:%s)\b(?!\s*\[)"
                          % "|".join(KEY_TYPES))
RETURN_RE = re.compile(r"\)\s*->\s*&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*([A-Za-z_][A-Za-z_0-9]*)")
# A receiver: segments joined by `.`, each an identifier optionally followed
# by `()` (a zero-argument accessor, placed by its declared return type) or
# `[...]` (an index, placed by the element type).  Anything else in receiver
# position is refused.
SEGMENT = r"[A-Za-z_][A-Za-z_0-9]*(?:\(\)|\[[^\[\]]*\])?"
RECEIVER = r"(%s(?:\.%s)*)" % (SEGMENT, SEGMENT)
# A CAST receiver, `(t as StructType*).get_method(`: placed by the type the
# cast names, which is the receiver's static type whatever `t` was declared
# as.  The cast's operand carries no parentheses of its own; one that does is
# refused with the rest of the unplaceable forms.
CAST_RECEIVER = r"\(\s*[^()]*?\bas\s+([A-Za-z_][A-Za-z_0-9]*)\s*\*?\s*\)"
# A static call's owner, `Owner::name(` or the turbofish `Owner::<T, U>::name(`:
# the owner is spelled, so it is placed by that spelling and cannot be
# misplaced.  `Pair::<LValue, TypeRef>::new(` is the shape a constructor call
# on a generic type takes, and a static pattern that stops at the first `::`
# reads it as a call it cannot place.
STATIC_OWNER = r"([A-Za-z_][A-Za-z_0-9]*)(?:::<[^;]*?>)?"


def call_patterns(names):
    """The four patterns every call to one of `names` is matched by, given the
    names longest first: `seen` (any `.name(` / `::name(`, the count the other
    three must account for), `dotted` (a placeable receiver), `cast` (a cast
    receiver, placed by the cast's type) and `static` (an owner, plain or
    turbofish).  One definition, so the residue enumerator that derives its
    population from this gate places exactly what the gate places."""
    alt = "|".join(re.escape(n) for n in names)
    return (re.compile(r"(?:\.|::)\s*(?:%s)\s*\(" % alt),
            re.compile(r"%s\.(%s)\s*\(" % (RECEIVER, alt)),
            re.compile(r"%s\.(%s)\s*\(" % (CAST_RECEIVER, alt)),
            re.compile(r"%s::(%s)\s*\(" % (STATIC_OWNER, alt)))


# Rule 1c's element read: `<recv>.<field>[<index>]`, the receiver a placeable
# one, not preceded by a segment of its own (so `a.b[i].c[j]` is read once at
# `c`, with `a.b[i]` its receiver, and once at `b`).
ELEM_RE = re.compile(r"(?<![A-Za-z_0-9.])%s\.([a-z_][a-z_0-9]*)\[([^\[\]]*)\]" % RECEIVER)
# A local table's element read: a bare local or parameter indexed,
# `fields[i]`, `slots[ki]` - placed by the local's annotation.
LOCAL_ELEM_RE = re.compile(r"(?<![A-Za-z_0-9.])([a-z_][a-z_0-9]*)\[([^\[\]]*)\]")
# The owner a local table is placed under.
LOCAL = "local"
# A local bound to a whole expression: `const m: MethodNode* = <expr>;`,
# `mut f: &FieldInfo = &<expr>;`.  The alias holds for the block it is bound
# in; a binding to a PART of an element (`.value`) is not an alias.
BIND_RE = re.compile(r"^\s*(?:const|mut|let)\s+([a-z_][a-z_0-9]*)\s*:\s*[^=]+=\s*&?\s*(.+?)\s*;\s*$")
IDENT = r"[a-z_][a-z_0-9]*"
# What ends an operand of `==` / `!=` read outward from the operator.
OPERAND_END_RE = re.compile(r"\)|&&|\|\||;|\{|\?|:")


def argument_text(line, start):
    """The text between the `(` at `start` and its matching `)`, one line."""
    depth = 0
    for i in range(start, len(line)):
        ch = line[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return line[start + 1:i].strip()
    return line[start + 1:].strip() + " ..."


def operand_before(line, end):
    """The operand ending at `end`, read backwards to the nearest `(`,
    `&&`, `||`, `=`, `;` or `{` (one line)."""
    i = end
    depth = 0
    while i > 0:
        ch = line[i - 1]
        two = line[i - 2:i]
        if ch == ")":
            depth += 1
        elif ch == "(":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and (ch in "=;{!" or two in ("&&", "||")):
            break
        i -= 1
    return line[i:end].strip()


def operand_after(line, start):
    """The operand starting at `start`, read forward to the nearest
    operand end (one line)."""
    depth = 0
    i = start
    while i < len(line):
        ch = line[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and OPERAND_END_RE.match(line, i):
            break
        i += 1
    return line[start:i].strip()


def key_chain(tree, elem, chain, key_types=KEY_TYPES):
    """Whether `.a.b.c` walked from `elem` through the tree's declared fields
    ends on a KEY-typed field.  A hop the tree does not declare (a field
    inherited from a base class, an accessor) is unknown, and unknown is not
    a key.  An empty chain is the element itself: an array of NAMES
    (`captured_names[i].equals(n)`) is a table whose record is its key."""
    ty = elem
    segs = [s for s in chain.split(".") if s]
    if not segs:
        return elem in key_types
    for n, s in enumerate(segs):
        # `.name.id`: the interned id IS the symbol, compared as a number.
        if s == "id" and ty == "SymbolStr" and "SymbolStr" in key_types and n == len(segs) - 1:
            return True
        f = tree.fields.get(ty, {}).get(s)
        if f is None:
            return False
        if n == len(segs) - 1:
            return f in key_types
        ty = f
    return False


def inline_scans(tree, key_types=KEY_TYPES):
    """Rule 1c's reads: [(rel, lineno, owner, field, elem, chain, key text)]
    for every comparison of a typed array element's key-typed field, read
    directly (`st.methods[i].name.equals(n)`) or through a local bound to
    the element in the enclosing block (`const m: MethodNode* =
    d.methods[i]; .. m.func.name.equals(n)`), against `.equals(`'s
    argument or receiver, or the other operand of `==` / `!=`.  One row per
    comparison: a compare with elements on both sides (`a[i].name.equals(b[j].name)`)
    is the left element's read, keyed by the right.

    A LOCAL TABLE - a local or parameter annotated as an array of records
    (`mut fields: &FieldInfo[] = &st.fields;`, `slots: MethodInfo[]`) and
    indexed - is the same scan with the owner out of view; it is placed
    by its element under the owner `local` (`local.FieldInfo`).

    `key_types` is what a compared field must be to count: the name types
    for rule 1c's placement, IDENTITY_TYPES for an IDENTITY entry's check."""
    elem_of = {}
    for owner, arrays in tree.typed_arrays.items():
        for _rel, field, elem in arrays:
            elem_of[(owner, field)] = elem
    # Rule 1b's arrays of bare names (`string[]` is lowercase and not a
    # typed array above): the element is the key itself.
    for owner, arrays in tree.array_owners.items():
        for _rel, field, elem in arrays:
            if elem in key_types:
                elem_of[(owner, field)] = elem
    # A record is any declared type with a key-typed field; a key type is
    # its own record.
    records = {ty for ty, fs in tree.fields.items() if any(t in key_types for t in fs.values())}
    records |= set(key_types)
    rows = []
    for rel in tree.rels:
        depth = 0
        aliases = {}
        for lineno, raw in enumerate(tree.files[rel], 1):
            code = STRING_RE.sub('""', strip_comment(raw))
            for k in [k for k, v in aliases.items() if depth < v[3]]:
                del aliases[k]
            exprs = []
            for m in ELEM_RE.finditer(code):
                owner = tree.receiver_type(rel, lineno, m.group(1))
                if owner is None:
                    continue
                elem = elem_of.get((owner, m.group(2)))
                if elem is None:
                    continue
                exprs.append((m.group(0), owner, m.group(2), elem))
            for m in LOCAL_ELEM_RE.finditer(code):
                if m.group(1) == "this":
                    continue
                elem = tree.local_array_elem(rel, lineno, m.group(1))
                if elem is None or elem not in records:
                    continue
                exprs.append((m.group(0), LOCAL, elem, elem))
            b = BIND_RE.match(code)
            if b is not None:
                for text, owner, field, elem in exprs:
                    if b.group(2) == text:
                        aliases[b.group(1)] = (owner, field, elem, depth)
            cands = [(re.escape(t), o, f, e) for t, o, f, e in exprs]
            cands += [(r"\b" + re.escape(k), v[0], v[1], v[2]) for k, v in aliases.items()]
            seen_at = set()
            found = []
            chain = r"((?:\.%s)*)" % IDENT
            for pat, owner, field, elem in cands:
                # `elem.key.equals(X)` / `.eq(X)`: keyed by X.
                for cm in re.finditer(pat + chain + r"\.(?:equals|eq)\s*\(", code):
                    if key_chain(tree, elem, cm.group(1), key_types):
                        found.append((cm.end() - 1, 0, owner, field, elem, cm.group(1),
                                      argument_text(code, cm.end() - 1)))
                # `X.equals(elem.key)`: keyed by X, the receiver.
                for cm in re.finditer(r"\.(?:equals|eq)\s*\(\s*" + pat + chain + r"\s*\)", code):
                    if key_chain(tree, elem, cm.group(1), key_types):
                        found.append((code.index("(", cm.start()), 1, owner, field, elem, cm.group(1),
                                      operand_before(code, cm.start())))
                # `elem.key == X` / `!= X`: keyed by X.
                for cm in re.finditer(pat + chain + r"\s*(==|!=)\s*", code):
                    if key_chain(tree, elem, cm.group(1), key_types):
                        found.append((cm.start(2), 0, owner, field, elem, cm.group(1),
                                      operand_after(code, cm.end())))
                # `X == elem.key`: keyed by X.
                for cm in re.finditer(r"(?<![=!<>])(==|!=)\s*" + pat + chain, code):
                    if key_chain(tree, elem, cm.group(2), key_types):
                        found.append((cm.start(1), 1, owner, field, elem, cm.group(2),
                                      operand_before(code, cm.start(1))))
            # One row per comparison, keyed at the operator's position: the
            # element on the LEFT is the read where one stands on both sides.
            for pos, _side, owner, field, elem, ch, key in sorted(found, key=lambda f: (f[0], f[1])):
                if pos in seen_at:
                    continue
                seen_at.add(pos)
                rows.append((rel, lineno, owner, field, elem, ch, key))
            for ch in code:
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
    return rows


def place_inline_scans(tree):
    """Rule 1c: every (owner, array) the tree scans inline is in
    SCANNED_ARRAYS, as a TABLE or as DATA with its reason, declared in the
    file and with the element the entry names; every entry is still scanned
    somewhere.  Refuses with every problem listed.  Returns the scans."""
    scans = inline_scans(tree)
    problems = []
    seen = {}
    for rel, lineno, owner, field, elem, chain, key in scans:
        seen.setdefault((owner, field), []).append((rel, lineno, elem, chain))
    for (owner, field), sites in sorted(seen.items()):
        label = "%s.%s" % (owner, field)
        entry = SCANNED_ARRAYS.get(label)
        rel, lineno, elem, chain = sites[0]
        if entry is None:
            problems.append(
                "  `%s` (%s: %s[]) is scanned inline by its element's key (%s:%d `%s`, %d site%s) and is not in\n"
                "      SCANNED_ARRAYS: a loop comparing a record's name is the same read as the owner's\n"
                "      door; say which - a TABLE of declarations by leaf, or DATA with the reason the\n"
                "      field compared names no declaration"
                % (label, owner, elem, rel, lineno, chain, len(sites), "" if len(sites) == 1 else "s"))
            continue
        if entry.elem != elem:
            problems.append("  `%s` is listed in SCANNED_ARRAYS with element `%s` but the tree declares `%s[]`"
                            % (label, entry.elem, elem))
        if owner == LOCAL:
            continue
        declared = sorted({r for r, f, _e in tree.typed_arrays.get(owner, []) + tree.array_owners.get(owner, [])
                           if f == field})
        if entry.defn not in declared:
            problems.append("  `%s` is listed in SCANNED_ARRAYS at %s but declared in %s"
                            % (label, entry.defn, ", ".join(declared) or "no file"))
    # An IDENTITY entry: its array is scanned by an identity-typed field, and
    # never by a name.
    by_identity = {}
    for rel, lineno, owner, field, elem, chain, key in inline_scans(tree, IDENTITY_TYPES):
        by_identity.setdefault((owner, field), []).append((rel, lineno, elem, chain))
    for label, entry in sorted(SCANNED_ARRAYS.items()):
        owner, field = label.split(".", 1)
        if entry.kind == IDENTITY:
            if (owner, field) in seen:
                rel, lineno, _elem, chain = seen[(owner, field)][0]
                problems.append(
                    "  `%s` is listed in SCANNED_ARRAYS as keyed by identity (%s) but is scanned by a name\n"
                    "      (%s:%d `%s`, %d site%s): compare the row's identity, or change the entry's kind\n"
                    "      and say why its key went back to a spelling"
                    % (label, " / ".join(IDENTITY_TYPES), rel, lineno, chain, len(seen[(owner, field)]),
                       "" if len(seen[(owner, field)]) == 1 else "s"))
            if (owner, field) not in by_identity:
                problems.append("  `%s` is listed in SCANNED_ARRAYS as keyed by identity but nothing in the tree scans it"
                                " by an identity (%s): a stale entry" % (label, " / ".join(IDENTITY_TYPES)))
            continue
        if (owner, field) not in seen:
            problems.append("  `%s` is listed in SCANNED_ARRAYS but nothing in the tree scans it inline: a stale entry"
                            % label)
    if problems:
        raise SystemExit("lane-gate: rule 1c - the inline scans and the table disagree:\n"
                         + "\n".join(problems))
    return scans


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
        # {type_name: [(relpath, field, element)]}: every type block owning
        # an array of names, rule 1b's candidates before the signature test.
        self.array_owners = {}
        # {type_name: [(relpath, field, element)]}: every type block owning
        # an array of some declared type; `record_owners` keeps the ones
        # whose element carries a key-typed field, once the tree is read.
        self.typed_arrays = {}
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
                    na = NAME_ARRAY_RE.match(code)
                    if na is not None:
                        self.array_owners.setdefault(current, []).append(
                            (rel, na.group(1), re.sub(r"\s+", "", na.group(2))))
                    ra = RECORD_ARRAY_RE.match(code)
                    if ra is not None:
                        self.typed_arrays.setdefault(current, []).append(
                            (rel, ra.group(1), ra.group(2)))
            for ch in code:
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
        return heads

    def record_owners(self):
        """{type_name: [(relpath, field, element)]}: the typed arrays whose
        element is a type this tree declares with a field of a key type -
        a record with a spelling in it, so an array of them is a table the
        owner can search by that spelling."""
        out = {}
        for owner, arrays in self.typed_arrays.items():
            for rel, field, elem in arrays:
                if any(ty in KEY_TYPES for ty in self.fields.get(elem, {}).values()):
                    out.setdefault(owner, []).append((rel, field, elem))
        return out

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

    def local_array_elem(self, rel, lineno, name):
        """The element type `name` was last annotated as an ARRAY of before
        `lineno` - `slots: MethodInfo[]`, `fields: &FieldInfo[]`,
        `params: GenericParamNode*[]` - or None when its nearest annotation
        is not an array (`local_type` answers that one)."""
        pat = re.compile(r"\b%s\s*:\s*(&?\s*(?:mut\s+)?(?:[a-z_][a-z_0-9]*::)*([A-Za-z_][A-Za-z_0-9]*)\s*\*?\s*(\[\])?)"
                         % re.escape(name))
        lines = self.files[rel]
        i = lineno - 1
        while i >= 0:
            code = STRING_RE.sub('""', strip_comment(lines[i]))
            for m in pat.finditer(code):
                ty = m.group(2)
                if ty[0].isupper() or ty in self.fields:
                    return ty if m.group(3) else None
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


def name_taking_methods(tree, type_name):
    """Rule 1b's signature test for one type: the names of the methods
    declared at depth 1 of its `type` block(s) and of every inherent
    `implement` block naming it, whose head takes a KEY TYPE as a parameter.
    A trait impl's methods are the trait's signature, not the type's own
    surface, and are not read (as `store_methods` has it)."""
    found = set()
    type_re = re.compile(r"^type\s+(?:struct|class|union|enum)\s+%s\b" % re.escape(type_name))
    for rel in tree.rels:
        lines = tree.files[rel]
        for i, line in enumerate(lines):
            code = strip_comment(line)
            m = INHERENT_IMPL_RE.match(code)
            if type_re.match(code) is None and (m is None or m.group(1) != type_name):
                continue
            depth = 0
            j = i
            while j < len(lines):
                c = STRING_RE.sub('""', strip_comment(lines[j]))
                if depth == 1:
                    mh = METHOD_HEAD_RE.match(c)
                    if mh is not None:
                        head = c
                        k = j
                        while ")" not in head and k + 1 < len(lines):
                            k += 1
                            head += " " + STRING_RE.sub('""', strip_comment(lines[k])).strip()
                        params = head[head.index("(") + 1:head.index(")")] if ")" in head else head
                        if KEY_PARAM_RE.search(params):
                            found.add(mh.group(2))
                for ch in c:
                    if ch == "{":
                        depth += 1
                    elif ch == "}":
                        depth -= 1
                if depth <= 0 and j > i:
                    break
                j += 1
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


def scan(src):
    """Return ({kind: {relpath: count}}, unplaced, {store: set}) over `src`."""
    tree = Tree(src)
    place_map_owners(tree)
    place_array_owners(tree)
    scans = place_inline_scans(tree)
    check_sealed_types(tree)
    sets = {name: store_methods(tree, name, st.defn) for name, st in STORES.items()}
    # Control on the parser: the LOOKUP row is the per-kind names, and they
    # are declared on the index.  A parser that cannot see them cannot see
    # the row it is asked to pin.
    missing = [n for n in LOOKUPS if n not in sets[INDEX_TYPE]]
    if missing:
        raise SystemExit("lane-gate: %s does not declare %s as name-crossing; "
                         "the parser has not measured the tree"
                         % (STORES[INDEX_TYPE].defn, ", ".join(missing)))
    names = sorted(set().union(*sets.values()), key=len, reverse=True)
    # Any call to a set name: a dotted or a cast receiver, a `Type::` static,
    # or none of those (which is a call this gate cannot place and refuses).
    # A definition line has no `.` or `::` before the name, so it is not a
    # call.
    seen_re, dotted_re, cast_re, static_re = call_patterns(names)

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
            placed = [(m.group(1), m.group(2), tree.receiver_type(rel, lineno, m.group(1)))
                      for m in dotted_re.finditer(line)]
            # A cast receiver is placed by the cast's type, not by a lookup.
            placed += [(m.group(0), m.group(2), m.group(1)) for m in cast_re.finditer(line)]
            for recv, name, ty in placed:
                accounted += 1
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
    return found, unplaced, sets, (tree.map_owners, array_candidates(tree), scans)


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
    "# field's declaration, the type a cast names), never by the receiver's",
    "# spelling; a static's owner, plain or turbofish, is that spelling.",
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
    ap.add_argument("--row", metavar="KIND",
                    help="print one bucket's live total, read from the tree, and exit")
    ap.add_argument("--rows", action="store_true",
                    help="print the number of buckets this gate counts and exit")
    ap.add_argument("--src", default=DEFAULT_SRC,
                    help="the compiler source tree to measure (default: compiler/src)")
    ap.add_argument("--golden", default=DEFAULT_GOLDEN,
                    help="the golden to compare against (default: tests/lane-baseline.txt)")
    args = ap.parse_args()

    # The bucket count is a property of this script, not of a golden: a
    # ledger row that asks it here asks the tree's gate, not a recorded file.
    if args.rows:
        print(len(KINDS))
        return 0
    if args.row is not None and args.row not in KINDS:
        sys.stderr.write("lane-gate: no bucket named %s (%s)\n" % (args.row, ", ".join(KINDS)))
        return 1

    counts, unplaced, sets, (owners, array_owners, scans) = scan(args.src)
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
        for rel, lineno, owner, field, _elem, _chain, _key in scans:
            by_array.setdefault("%s.%s" % (owner, field), []).append("%s:%d" % (rel, lineno))
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
