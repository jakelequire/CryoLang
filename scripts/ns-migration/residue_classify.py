#!/usr/bin/env python3
"""Classify D32's population and write the site table of
scripts/ns-migration/residue.md (between its two `residue-table` markers).

`residue.py` derives the population - every call in compiler/src that hands
a spelling into a read method of a store or of a member table - from the
lane gate's own parser.  This script is the JUDGEMENT over it: one class per
method, with the reason, and a site override where one call's key comes
from somewhere the method's other callers' do not.  Both tables are here so
the classification is reproducible and reviewable as code; the prose in
residue.md is written by hand around the generated table.

Classes (one letter each; the residue Jake rules on is the J rows):

  J  JUSTIFIED - name-keyed by design, stays.  The sub-kind is the first
     word of the reason:
       member  - a member's leaf asked INSIDE a settled owner (an impl's
                 `This::Member` binding, a global or a refused signature in
                 a module named by its DefId): Rust looks a field or a
                 method up by name off the owner too;
       lang    - a declaration the LANGUAGE names by its own spelling and
                 claims at declaration (`Drop`, `Copy`, `Future`, the
                 operator traits, `std::collections::array::Array`, the
                 test runner's entry points): Rust's lang items, which are
                 keyed by the attribute's string and nothing else;
       module  - a MODULE by its namespace path: the module graph is the
                 declaration of modules, a namespace names exactly one
                 (D5), and Rust's module tree is path-keyed at resolution;
       prim    - a primitive by the spelling its `Res::PrimTy` stamp
                 carries, the stamp having no other payload;
       extern  - an extern C symbol by its link name, which is the only
                 identity a C symbol has;
       hint    - a did-you-mean that asks which trait spells a method
                 leaf; the spelling IS the question.
  N  NOT A KEY - the string parameter names no declaration: a diagnostic
     label beside a `DefId`, a file path, a literal's text, a constructor's
     source file, a write onto an AST node, or a funnel's own forwarding
     body (its callers are the sites).  Inside the population because the
     gate's parameter test cannot tell a label from a key; outside D32.
  S  STAMP-DERIVED - an index or funnel door keyed by a canonical string
     the caller derived from a stamp or a `TypeRef` (`def_id.qualified_name()`,
     `lookup_type_name(ref)`, `get_qualified_name(ref)`, `tr.identity()`).
     Convertible: the door can take the stamp.  Not by design.
  B  REGISTRY - the `GenericRegistry` keyed by the canonical string of a
     stamped identity.  Bucket B's reader side (§8.245): re-keying the
     registry by identity is Jake's unit.  Convertible, not by design.
  C  COMPOSED - a key BUILT from parts (`owner::member`, a family string,
     `ns::name`): bucket C, waits on pin-as-declaration (§8.279).
  F  VISIBILITY by name (`enforce_callee_visibility`): bucket F, waits on
     the same.
  W  WRITTEN - a spelling with no stamp behind it, a read the migration has
     not converted and no reason above covers.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import residue  # noqa: E402

BEGIN = "<!-- residue-table:begin -->"
END = "<!-- residue-table:end -->"

# {"Holder::method": (class, reason)}.  Every read method the population
# holds must be here; a method here the tree no longer declares is refused.
CLASS_OF_METHOD = {
    # -- member tables inside their owner (rule 1b's justified kind) --
    "ImplBlockNode::lookup_assoc_binding":
        ("J", "member: the impl's own `This::Member` binding by the member's leaf, asked from inside the block"),
    "ImplBlockNode::set_target_type":
        ("N", "a write: the specialized impl's target name stored on the node"),
    "TraitType::add_assoc_type":
        ("N", "a write: the trait's own associated-type name recorded on the trait type"),
    "ResolutionContext::lookup_assoc_binding":
        ("J", "member: the `This::Member` binding of the impl being resolved, by leaf"),
    "ResolutionContext::new":
        ("N", "a constructor: the string is the SOURCE FILE the context resolves in"),
    # -- the member tables of the user-defined types: an array of records
    #    (`FieldInfo[]`, `MethodInfo[]`, `EnumVariantInfo[]`) with the name
    #    in each, searched by leaf off the type already in hand --
    "StructType::field_index":
        ("J", "member: a struct's field position by its leaf off the `StructType` in hand"),
    "StructType::get_method":
        ("J", "member: a struct's method by its leaf off the `StructType` in hand; Rust's method lookup is name-keyed off the owner too"),
    "ClassType::field_index":
        ("J", "member: a class's field position by its leaf off the `ClassType` in hand"),
    "ClassType::get_method":
        ("J", "member: a class's method by its leaf off the `ClassType` in hand"),
    "EnumType::get_variant":
        ("J", "member: an enum's variant by its leaf off the `EnumType` in hand"),
    "EnumType::variant_index":
        ("J", "member: an enum's variant ordinal by its leaf off the `EnumType` in hand - sema's one lookup, pinned on the node for codegen"),
    "EnumType::get_method":
        ("J", "member: an enum's method by its leaf off the `EnumType` in hand"),
    "TraitDeclNode::lookup_assoc_type":
        ("J", "member: the trait's own associated type by its leaf, asked from the trait node in hand"),
    "TraitDeclNode::assoc_type_index":
        ("J", "member: the trait's own associated type's position by its leaf, asked from the trait node in hand - the index the positional trait arguments are read by"),
    "TraitDeclNode::lookup_method":
        ("J", "member: the trait's own method by its leaf, asked from the trait node in hand"),
    # -- the same tables scanned INLINE (rule 1c): the door's loop written at
    #    the caller, `field[]` on the owner.  The class is the door's; a
    #    scan a door cannot express (a second predicate beside the leaf)
    #    stays a scan and stays a row --
    "StructType::fields[]":
        ("J", "member: a struct's field by its leaf off the `StructType` in hand, scanned inline"),
    "StructType::methods[]":
        ("J", "member: a struct's method by its leaf off the `StructType` in hand, scanned inline with an arity predicate"),
    "ClassType::fields[]":
        ("J", "member: a class's field by its leaf off the `ClassType` in hand, scanned inline"),
    "ClassType::methods[]":
        ("J", "member: a class's method by its leaf off the `ClassType` in hand, scanned inline (the vtable's slot walk; an arity predicate)"),
    "EnumType::variants[]":
        ("J", "member: an enum's variant by its leaf off the `EnumType` in hand, scanned inline"),
    "EnumType::methods[]":
        ("J", "member: an enum's method by its leaf off the `EnumType` in hand, scanned inline with an arity predicate"),
    "TraitType::required_methods[]":
        ("J", "member: a trait's required method by its leaf off the `TraitType` in hand, scanned inline"),
    "TraitDeclNode::assoc_types[]":
        ("J", "member: the trait's own associated type by its leaf off the trait node in hand, scanned inline"),
    "TraitDeclNode::methods[]":
        ("J", "member: the trait's own method by its leaf off the trait node in hand, scanned inline (an `async` or a default-body predicate beside the leaf)"),
    "ImplBlockNode::methods[]":
        ("J", "member: the impl's own method by its leaf off the impl node in hand, scanned inline"),
    "ImplBlockNode::generic_params[]":
        ("J", "member: the impl's own generic parameter by its leaf off the impl node in hand, scanned inline"),
    "StructDeclNode::fields[]":
        ("J", "member: a struct declaration's field by its leaf off the node in hand, scanned inline"),
    "StructDeclNode::methods[]":
        ("J", "member: a struct declaration's method by its leaf off the node in hand, scanned inline"),
    "UnionDeclNode::fields[]":
        ("J", "member: a union declaration's field by its leaf off the node in hand, scanned inline"),
    "UnionDeclNode::methods[]":
        ("J", "member: a union declaration's method by its leaf off the node in hand, scanned inline"),
    "ClassDeclNode::fields[]":
        ("J", "member: a class declaration's field by its leaf off the node in hand, scanned inline"),
    "ClassDeclNode::methods[]":
        ("J", "member: a class declaration's method by its leaf off the node in hand, scanned inline"),
    "ExternBlockNode::functions[]":
        ("J", "member: an extern block's function by its leaf off the block in hand, scanned inline"),
    "GenericRegistry::entries[]":
        ("B", "the registry's template rows by name and module, scanned in the registry's own file only"),
    "ModuleGraph::modules[]":
        ("J", "module: the graph's modules by namespace, scanned inline"),
    "ModuleInfo::reexports[]":
        ("J", "module: a module's re-exported namespaces, module identities by path, scanned inline"),
    "ModuleInfo::imported_namespaces[]":
        ("J", "module: a module's imported namespaces, module identities by path, scanned in the graph's own file only"),
    "ModuleInfo::submodules[]":
        ("J", "module: a module's submodules, module identities by path, scanned in the graph's own file only"),
    "EnumDeclNode::variants[]":
        ("J", "member: an enum declaration's variant by its leaf off the node in hand, scanned inline"),
    "StructLiteralNode::field_inits[]":
        ("J", "member: a struct literal's written initializer by the field's leaf, scanned inline"),
    "DestructureDeclNode::bindings[]":
        ("J", "member: a destructure's written binding by the source field's leaf (which binding takes a field), scanned inline"),
    "ImplBlockNode::assoc_binding_names[]":
        ("J", "member: the impl's own `This::Member` binding by the member's leaf (the door's body; own file only)"),
    "LambdaExprNode::captured_names[]":
        ("J", "member: the lambda's captured names, asked whether one is captured (own file only)"),
    # -- local tables: the owner out of view, the element says what it is --
    "local::MethodNode[]":
        ("J", "member: an impl's or a declaration's methods held in a local, by leaf"),
    "local::MethodInfo[]":
        ("J", "member: an arena type's methods held in a local, by leaf"),
    "local::FieldInfo[]":
        ("J", "member: a struct's or class's fields held by reference in a local, by leaf"),
    "local::FieldDeclNode[]":
        ("J", "member: a declaration's written fields held in a local, by leaf (the duplicate-field check)"),
    # RULED (Jake, 2026-09-22): this site stays in the population, read as a
    # rule 1c scan.  The review board read it as a text boundary outside D32;
    # outside the population does not mean clean, it means UNWATCHED - a J row
    # is enumerated, carries a written reason and the gate sees it drift,
    # while a site ruled outside D32 is governed by nothing.
    "local::GenericParamNode[]":
        ("J", "member: a declaration's own written generic parameters, scanned for a repeated spelling as the parser builds the list. The question IS about spellings - two parameters written `<T, T>` are a redeclaration - and at parse time there is no identity to ask instead: the resolver stamps a `SymbolID` on each parameter only after the list exists. Rust refuses the same shape the same way, by comparing idents in the parameter list"),
    "local::VTableSlot[]":
        ("J", "member: a class's vtable slots held in a local, by the method's leaf (the override walk)"),
    # -- the declaration index --
    "DeclarationIndex::type_of_decl":
        ("N", "keyed by the `DefId`; the string is the diagnostic label of the asking site"),
    "DeclarationIndex::impl_owner":
        ("N", "keyed by the impl node; the string is the diagnostic label of the asking site"),
    "DeclarationIndex::global_entry_in_module":
        ("J", "member: a global's leaf inside the module the qualifier's stamp names (a `DefId`)"),
    "DeclarationIndex::lookup_family_entries":
        ("J", "member: an overload family's leaf asked inside the owner the caller holds by identity - a module's or a type's definition, or a type's arena id (`FamilyOwner`); the store is keyed by that owner's path and the leaf (`family_slot`), as a definition is its parent and its leaf. Overloading is kept, so a family is the SET one owner declares under one written leaf, and the leaf is the question asked, not a stand-in for an identity - Rust's resolver keys the same set by parent module, ident and namespace"),
    "DeclarationIndex::lookup_func_type_overloads":
        ("J", "member: an overload family's signatures, asked as `lookup_family_entries` is"),
    "DeclarationIndex::lookup_func_type":
        ("J", "member: the signature last registered in an overload family, asked as `lookup_family_entries` is"),
    "DeclarationIndex::lookup_type":
        ("S", "a type by a canonical name derived from a stamp"),
    "DeclarationIndex::lookup_method_return":
        ("J", "member: a method's return by its leaf off the owner `TypeRef` in hand; the store is keyed by the owner's arena id"),
    "DeclarationIndex::method_signature_refused":
        ("J", "member: whether a method's written signature was refused, by its leaf off the owner `TypeRef` in hand; keyed as `lookup_method_return` is"),
    "DeclarationIndex::is_prelude_ns":
        ("J", "module: a namespace asked whether it is the prelude's; a module's identity is its path"),
    "DeclarationIndex::ns_imports":
        ("J", "module: two namespaces asked whether one imports the other; module identities"),
    "DeclarationIndex::family_intrinsic_kind":
        ("J", "member: the intrinsic kind an overload family's entries share, asked as `lookup_family_entries` is (the `CalleePin::Family` arm; the `Decl` arm reads the kind off the entry)"),
    "DeclarationIndex::extern_symbol_conflict":
        ("J", "extern: a C symbol by its link name, the only identity a C symbol has"),
    # -- the funnel --
    "TypeUtils::lookup_type_exact":
        ("S", "a type by a canonical name derived from a stamp (`scope_owner_key`, `trait_identity`, `tr.identity()`)"),
    "TypeUtils::lookup_func_type_exact":
        ("J", "member: the funnel's door onto `DeclarationIndex::lookup_func_type`, an overload family by its owner and leaf"),
    "TypeUtils::lookup_method_return":
        ("J", "member: a method's return by its leaf off the owner `TypeRef` in hand (`method_owner_ref` for a wrapper; `lookup_type_exact` where the caller holds a stamp's name)"),
    # -- the generic registry --
    "GenericRegistry::inherent_impl_has_method":
        ("J", "member: a method's leaf inside the owner's `implement` blocks, the owner the `TypeRef` in hand"),
    "GenericRegistry::find_inherent_impl_method":
        ("J", "member: a method's leaf inside the owner's `implement` blocks, the owner the `TypeRef` in hand"),
    "GenericRegistry::find_inherent_impl_generic_method":
        ("J", "member: a generic method's leaf inside the owner's `implement` blocks, the owner the `TypeRef` in hand"),
    "GenericRegistry::template_in":
        ("J", "member: a template declared inside its container - a module's generic function, a type's generic static method - by the leaf written after the path segment; the container is the segment's resolved identity (`ResBase`), never a spelling"),
    "GenericRegistry::member_template_key":
        ("N", "a key MINTED at the member template's declaration (`Owner::method`) for its registration; the write side of `template_in`"),
    "GenericRegistry::find_trait_defining_method":
        ("J", "hint: the did-you-mean asks which trait declares a method of this leaf; the spelling is the question"),
    # -- the module graph and the resolver's module scopes --
    "ModuleGraph::find_module_index":
        ("J", "module: a module by its namespace path"),
    "ModuleGraph::module_def":
        ("J", "module: a module's definition by its namespace path - the id the graph registered when it added the module, or the name layer's for a C import's module; a declaration asks it for its parent"),
    "ModuleGraph::reexport_closure":
        ("J", "module: a module's re-export closure by its namespace path"),
    "ModuleGraph::source_file_for_owner_key":
        ("J", "module: a module's source file by its namespace path"),
    "ModuleGraph::find_module_by_path":
        ("N", "a module by its FILE PATH"),
    "ModuleGraph::ns_sym_of_file":
        ("N", "a namespace by its FILE PATH"),
    "Resolver::find_module_scope":
        ("J", "module: a module's scope by its namespace path, to stand in it while a template is instantiated"),
    # -- the arena and the constant table --
    "ConstantTable::intern_qualified":
        ("N", "a key MINTED at the constant's declaration (`ns::name`) for its registration; the write side"),
    "ConstantTable::parse_int_literal":
        ("N", "a literal's text parsed to a value"),
}

# {(file, "Holder::method", argument text): (class, reason)} - a site whose
# key comes from somewhere the method's other callers' do not.
SITE_OVERRIDES = {
    ("compiler/passes/type_resolution.cryo", "DeclarationIndex::lookup_type", "n"):
        ("J", "prim: `Res::PrimTy(n)` - the primitive's identity is the spelling the stamp carries"),
    ("compiler/sema/type_utils.cryo", "DeclarationIndex::lookup_type", "name"):
        ("N", "the funnel's own forwarding body (`lookup_type_exact`); its callers are the sites"),
    ("compiler/sema/type_utils.cryo", "DeclarationIndex::lookup_func_type", "owner, leaf"):
        ("N", "the funnel's own forwarding body (`lookup_func_type_exact`); its callers are the sites"),
    ("compiler/sema/type_utils.cryo", "DeclarationIndex::lookup_method_return", "owner, method_sym"):
        ("N", "the funnel's own forwarding body (`lookup_method_return`); its callers are the sites"),
    ("compiler/sema/sema.cryo", "TypeUtils::lookup_method_return", 'owner_ref, this.intern.intern("iter")'):
        ("J", "member: the for-in protocol's `iter` off a collection that does not implement `Iterator`"),
    # The member tables asked with a leaf the LANGUAGE fixes rather than one
    # the program wrote: the protocols' own variant and method names.
    ("compiler/sema/sema.cryo", "EnumType::get_variant", 'this.intern.intern("Ready")'):
        ("J", "lang: `Poll::Ready`, the variant the async protocol names, inside the enum a synthesized `poll` returns - the enum matched to the language's `Poll` by the identity its declaration claimed before the variant is read"),
    ("compiler/sema/lambda_synth.cryo", "StructType::get_method", 'this.intern.intern("__call__")'):
        ("J", "lang: the call protocol's `__call__`, the method leaf the language fixes for a callable struct"),
    ("compiler/types/checker.cryo", "StructType::get_method", " ..."):
        ("J", "lang: the call protocol's `__call__` (the argument continues on the next line), asked when a struct converts to a function type"),
    # The inline scans whose key is not the table's usual one.
    # RULED (Jake, 2026-09-22): these two sites stay in the population, read as
    # rule 1c scans, for the reason above `local::GenericParamNode[]`.
    ("compiler/bindgen/importer.cryo", "ExternBlockNode::functions[]", "sym"):
        ("J", "extern: whether this extern block already declares the C function of this LINK NAME, asked as the importer emits it. A C symbol's link name is the whole of its identity - C has no declaration to key on, and two C declarations sharing a link name are one entity by the linkage rule - so there is no stamp this could ask instead, and the leaf it compares is the mangling the linker will use"),
    ("compiler/resolver/name_resolution.cryo", "FunctionDeclNode::parameters[]", "param.name.id"):
        ("J", "member: the resolver's duplicate-parameter check, a parameter by leaf inside the function being bound"),
    ("compiler/AST/dumper.cryo", "DestructureDeclNode::bindings[]", "b.local_name.id"):
        ("N", "a display: the binding's own two names compared to print `x` rather than `x: x`"),
    ("compiler/resolver/name_resolution.cryo", "DestructureDeclNode::bindings[]", "b.local_name.id"):
        ("J", "member: the resolver's duplicate-binding check, a binding by its local's leaf inside the destructure being bound"),
}

CLASSES = "JNSBCFW"


def classify(rows):
    out = []
    unknown = set()
    for rel, lineno, ty, name, arg in rows:
        key = ty + "::" + name
        ov = SITE_OVERRIDES.get((rel, key, arg))
        if ov is not None:
            cls, reason = ov
        elif key in CLASS_OF_METHOD:
            cls, reason = CLASS_OF_METHOD[key]
        else:
            unknown.add(key)
            cls, reason = "?", "UNCLASSIFIED"
        out.append((rel, lineno, key, arg, cls, reason))
    return out, unknown


def render(classified):
    counts = {c: 0 for c in CLASSES}
    for row in classified:
        if row[4] in counts:
            counts[row[4]] += 1
    lines = []
    lines.append("Population **%d** - J %d · N %d · S %d · B %d · C %d · F %d · W %d"
                 % (len(classified), counts["J"], counts["N"], counts["S"], counts["B"],
                    counts["C"], counts["F"], counts["W"]))
    lines.append("")
    lines.append("| site | read | key as written | class | reason |")
    lines.append("|---|---|---|---|---|")
    for rel, lineno, key, arg, cls, reason in sorted(classified, key=lambda r: (r[4], r[2], r[0], r[1])):
        arg_md = "`" + arg.replace("|", "\\|") + "`" if arg else ""
        lines.append("| `%s:%d` | `%s` | %s | %s | %s |" % (rel, lineno, key, arg_md, cls, reason))
    return "\n".join(lines) + "\n", counts


def render_elsewhere(elsewhere):
    """The second table: every population name called through a dotted
    receiver placed on a type that holds nothing, per (type, method) with
    its count.  Not sites - the type is no holder - but pinned, because a
    holder the placement rule misreads as another type lands here and
    nowhere else; the gate's LOOKUP_LOCAL row, by name."""
    lines = ["Elsewhere **%d** - a population name on a type that holds nothing, per (type, method)"
             % sum(elsewhere.values()), ""]
    lines.append("| receiver type and read | calls |")
    lines.append("|---|---|")
    for (ty, name), n in sorted(elsewhere.items()):
        lines.append("| `%s::%s` | %d |" % (ty, name, n))
    return "\n".join(lines) + "\n"


def write_list(path, classified, elsewhere):
    """Rewrite the two generated tables of the list at `path`, between their
    markers, leaving the prose around them as it is."""
    table, counts = render(classified)
    text = io.open(path, encoding="utf-8").read()
    b, e = text.index(BEGIN), text.index(END)
    text = text[:b + len(BEGIN)] + "\n" + table + text[e:]
    b, e = text.index(residue.ELSEWHERE_BEGIN), text.index(residue.ELSEWHERE_END)
    text = text[:b + len(residue.ELSEWHERE_BEGIN)] + "\n" + render_elsewhere(elsewhere) + text[e:]
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    return counts


def main():
    gate = residue.load_gate()
    rows, sets, elsewhere = residue.population(gate, os.path.join(residue.ROOT, "compiler", "src"))
    declared = {h + "::" + m for h, ms in sets.items() for m in ms}
    called = {ty + "::" + name for _r, _l, ty, name, _a in rows}
    stale = sorted(k for k in CLASS_OF_METHOD if k not in declared)
    if stale:
        raise SystemExit("residue_classify: classified but no longer a read method in the tree: "
                         + ", ".join(stale))
    classified, unknown = classify(rows)
    if unknown:
        raise SystemExit("residue_classify: read methods called in the tree with no class: "
                         + ", ".join(sorted(unknown)))
    stale_sites = [k for k in SITE_OVERRIDES if not any(
        r[0] == k[0] and r[2] + "::" + r[3] == k[1] and r[4] == k[2] for r in rows)]
    if stale_sites:
        raise SystemExit("residue_classify: site overrides that match no call in the tree: "
                         + "; ".join("%s %s (%s)" % k for k in stale_sites))
    path = residue.DEFAULT_RESIDUE
    counts = write_list(path, classified, elsewhere)
    unread = sorted(declared - called)
    print("residue_classify: %d sites written to %s (J %d; elsewhere %d); %d read methods declared and called from nowhere: %s"
          % (len(classified), os.path.relpath(path, residue.ROOT), counts["J"], sum(elsewhere.values()),
             len(unread), ", ".join(unread) if unread else "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
