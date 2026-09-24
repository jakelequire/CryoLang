#!/usr/bin/env python3
"""The intrinsic kind rides on the declaration (section 8.325).

Before: codegen decided a call pinned to an `intrinsic function` entry was an
intrinsic, then took the pinned declaration's LEAF as a string and looked it
up in `IntrinsicKind::from_name` - twice (`is_name`, then `try_emit`) - and
compared it with "try_catch" and "panic".  After: type resolution reads the
spelling once, where it registers the declaration, and stores the kind on
the index entry; codegen reads the kind off the pin and never sees a string.

Also deleted: the 44 conversion names (41 kinds) that duplicate `as` - no
declaration names any of them, so no call can be pinned to one.

    python scripts/ns-migration/8.325/intrinsic_kind_on_declaration.py          # apply
    python scripts/ns-migration/8.325/intrinsic_kind_on_declaration.py --check  # 0 = applied
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
C = "compiler/src/compiler/"
OLD_TABLE = C + "codegen/ops/intrinsics_codegen.cryo"
NEW_TABLE = C + "intrinsic_kind.cryo"

# The conversion kinds `x as T` already lowers: every one is deleted.
CAST_KIND = re.compile(
    r"^(SExt|Trunc|ZExt|UTrunc|SignBitcast|FpExt|FpTrunc|SiToF|UiToF|FpToSi|FpToUi)")


def new_table(old: str) -> str:
    """The enum and its name table, moved out of codegen, less the casts."""
    enum_body = old[old.index("type enum IntrinsicKind {"):old.index("implement enum IntrinsicKind {")]
    table = old[old.index("    static from_name(name: string)"):old.index("    /// Fast prefilter")]

    out_enum = []
    skip_comment = []
    for line in enum_body.split("\n"):
        s = line.strip()
        if s.startswith("//"):
            skip_comment.append(line)
            continue
        names = re.findall(r"(\w+);", s)
        if names and all(CAST_KIND.match(n) for n in names):
            skip_comment = []           # the comment described a deleted group
            continue
        out_enum.extend(skip_comment)
        skip_comment = []
        out_enum.append(line)
    out_enum.extend(skip_comment)
    enum_text = "\n".join(out_enum)
    enum_text = enum_text.replace(
        "    NotIntrinsic;            // sentinel: unrecognized name\n",
        "    // Not lowered inline: a call pinned to the entry is an ordinary call\n"
        "    // to its symbol.  Every entry that is no intrinsic, and the intrinsics\n"
        "    // that ARE their C symbol (`malloc`, `realloc`, `free`).\n"
        "    Call;\n\n"
        "    // `try_catch(body, data)`, the catch_unwind primitive: lowered by the\n"
        "    // call emitter itself (an `invoke`, a catch-all landing pad, a phi),\n"
        "    // never by the inline dispatcher.\n"
        "    TryCatch;\n")
    enum_text = enum_text.replace(
        "    // itself.  Names carry a width suffix (like `bswap64`) so common leaf\n"
        "    // names (`round`, `sqrt`, ...) are not reserved by the leaf-name\n"
        "    // intrinsic dispatch.  `FTrunc64` is float truncation (`llvm.trunc.f64`,\n"
        "    // round toward zero), distinct from the integer `Trunc*` narrowing casts.\n",
        "    // itself.  Names carry a width suffix (like `fabs64`) so the common\n"
        "    // leaf names (`round`, `sqrt`, ...) stay free for user code.\n"
        "    // `FTrunc64` is float truncation (`llvm.trunc.f64`, round toward\n"
        "    // zero); an integer narrowing is `x as T`.\n")
    enum_text = re.sub(r"\n{3,}", "\n\n", enum_text)

    out_rows = []
    skip_comment = []
    for line in table.split("\n"):
        s = line.strip()
        if s.startswith("//"):
            skip_comment.append(line)
            continue
        m = re.search(r"=>\s*\{\s*IntrinsicKind::(\w+)", s)
        if m and CAST_KIND.match(m.group(1)):
            skip_comment = []
            continue
        out_rows.extend(skip_comment)
        skip_comment = []
        out_rows.append(line)
    out_rows.extend(skip_comment)
    table_text = "\n".join(out_rows)
    table_text = table_text.replace(
        '            _ => { IntrinsicKind::NotIntrinsic }',
        '            "try_catch" => { IntrinsicKind::TryCatch }\n\n'
        '            _ => { IntrinsicKind::Call }')
    table_text = table_text.replace(
        "            // printf-family va_list forwarders\n\n", "")
    table_text = re.sub(r"\n{3,}", "\n\n", table_text)
    table_text = table_text.rstrip() + "\n"

    header = '''/// The intrinsic kinds codegen lowers inline, and the one table that maps
/// an `intrinsic function` declaration's name to its kind.
///
/// Type resolution asks the table once, where it registers the declaration,
/// and stores the kind on the declaration's index entry; codegen reads the
/// kind off the entry a call is pinned to and is never handed a name.  A
/// name the table does not know registers `Call`: the declaration is an
/// ordinary call to its symbol.
///
/// Adding an intrinsic: an enum variant, one row in `from_name`, one arm in
/// `IntrinsicEmitter::try_emit`, and the declaration in
/// `stdlib/core/intrinsics.cryo`.

namespace compiler::intrinsic_kind;

'''
    return (header + enum_text.rstrip() + "\n\n\n"
            + "implement enum IntrinsicKind {\n"
            + "    /// The kind an `intrinsic function` declaration named `name` lowers\n"
            + "    /// as.  Asked only where the declaration is registered.\n"
            + table_text.replace("    static from_name(name: string)", "    static from_name(name: string)", 1)
            + "}\n")


def src_type_method(old: str) -> str:
    """`IntrinsicKind::src_type`, as the emitter's own static, less the casts."""
    body = old[old.index("    src_type(&this) -> LType {"):]
    body = body[:body.index("\n    }\n") + len("\n    }\n")]
    out = []
    skip_comment = []
    for line in body.split("\n"):
        s = line.strip()
        if s.startswith("//"):
            skip_comment.append(line)
            continue
        m = re.match(r"IntrinsicKind::(\w+)\s*=>", s)
        if m and CAST_KIND.match(m.group(1)):
            skip_comment = []
            continue
        out.extend(skip_comment)
        skip_comment = []
        out.append(line)
    text = "\n".join(out)
    text = text.replace("    src_type(&this) -> LType {\n        return match (this) {",
                        "    static src_type_of(kind: IntrinsicKind) -> LType {\n        return match (kind) {")
    doc = ("    /// The value type a unary intrinsic's first argument is loaded as when\n"
           "    /// the caller passes a pointer to it (`intrinsic(&this)`), or a null\n"
           "    /// type when that argument is not auto-dereferenced.\n")
    return doc + text


EMITTER = C + "codegen/ops/intrinsic_emitter.cryo"


def emitter_edits(src_type_text: str):
    cast_arm = re.compile(r"^\s*IntrinsicKind::(\w+)\s*=>.*$")
    return [
        (EMITTER,
         "/// Methods are kind-driven: `try_emit(name, args)` runs\n"
         "/// `IntrinsicKind::from_name` from `IntrinsicsCodegen`, validates arity,\n"
         "/// and dispatches to a per-kind LLVM emission path.",
         "/// Methods are kind-driven: `try_emit(kind, args)` takes the kind the\n"
         "/// pinned declaration was registered with, validates arity, and\n"
         "/// dispatches to a per-kind LLVM emission path."),
        (EMITTER,
         "import compiler::codegen::ops::intrinsics_codegen;\n"
         "import compiler::codegen::ops::intrinsics_codegen::{ IntrinsicKind };\n",
         "import compiler::intrinsic_kind;\n"
         "import compiler::intrinsic_kind::{ IntrinsicKind };\n"),
        (EMITTER,
         "    /// Inline-lower a bare-name function call to LLVM IR.  Returns\n"
         "    /// `Option::Some(value)` if `name` matches a known intrinsic, or\n"
         "    /// `Option::None` if the call should fall through to the normal\n"
         "    /// function-resolution path.\n"
         "    try_emit(mut &this, name: string, args: LValue[]) -> Option<LValue> {\n"
         "        const kind: IntrinsicKind = IntrinsicKind::from_name(name);\n",
         src_type_text + "\n"
         "    /// Inline-lower a call to the intrinsic `kind`.  Returns\n"
         "    /// `Option::Some(value)`, or `Option::None` when `kind` is not lowered\n"
         "    /// here or `args` is not its arity, and the call is an ordinary call.\n"
         "    try_emit(mut &this, kind: IntrinsicKind, args: LValue[]) -> Option<LValue> {\n"),
        (EMITTER,
         "            IntrinsicKind::NotIntrinsic => { -1 }\n",
         "            IntrinsicKind::Call         => { -1 }\n"
         "            IntrinsicKind::TryCatch     => { -1 }\n"),
        (EMITTER,
         "        const src_ty: LType = kind.src_type();\n",
         "        const src_ty: LType = IntrinsicEmitter::src_type_of(kind);\n"),
        (EMITTER,
         "            IntrinsicKind::NotIntrinsic => { LValue::null() }   // unreachable\n",
         "            IntrinsicKind::Call     => { LValue::null() }   // declined above\n"
         "            IntrinsicKind::TryCatch => { LValue::null() }   // declined above\n"),
    ], cast_arm


DECL = C + "decl_index.cryo"
TR = C + "passes/type_resolution.cryo"
CALL = C + "codegen/visit/call_emitter.cryo"

EDITS = [
    (DECL,
     "import std::collections::pair::{ Pair };\n",
     "import std::collections::pair::{ Pair };\n"
     "import compiler::intrinsic_kind;\n"
     "import compiler::intrinsic_kind::{ IntrinsicKind };\n"),
    (DECL,
     "    overload_func_intrinsic: boolean[]; // the entry is an `intrinsic function`\n"
     "                                      // declaration, one codegen lowers inline\n"
     "                                      // (memcpy, ptr_diff, panic, atomic_*, ...).\n"
     "                                      // The mark is on the ENTRY, so a pin to\n",
     "    overload_func_intrinsic: IntrinsicKind[]; // how a call pinned to the entry\n"
     "                                      // lowers: an `intrinsic function` codegen\n"
     "                                      // lowers inline (ptr_diff, panic, atomic_*,\n"
     "                                      // ...), else `Call`.  The kind is on the\n"
     "                                      // ENTRY, read from the declaration where it\n"
     "                                      // was registered, so a pin to\n"),
    (DECL,
     "        this.overload_func_intrinsic.push(false);\n",
     "        this.overload_func_intrinsic.push(IntrinsicKind::Call);\n"),
    (DECL,
     "    /// Mark entry `id` as an `intrinsic function` declaration.  Its leaf is\n"
     "    /// a key in no function table: an intrinsic is reached through its\n"
     "    /// module (`intrinsics::free`) like every declaration, and its bare\n"
     "    /// spelling is an error where it is declared in no scope.  Codegen\n"
     "    /// reads the mark off the pin to tell the intrinsic from a function\n"
     "    /// sharing its leaf (`callee_is_intrinsic`).\n"
     "    mark_intrinsic(mut &this, id: OverloadId) -> void {\n"
     "        if (!id.is_valid()) { return; }\n"
     "        this.overload_func_intrinsic[id.position() as i64] = true;\n"
     "    }\n"
     "\n"
     "    /// Whether entry `id` is an `intrinsic function` declaration; false for\n"
     "    /// an invalid id.\n"
     "    entry_is_intrinsic(&this, id: OverloadId) -> boolean {\n"
     "        if (!id.is_valid()) { return false; }\n"
     "        return this.overload_func_intrinsic[id.position() as i64];\n"
     "    }\n"
     "\n"
     "    /// Whether every entry under key `name` is an intrinsic: a call pinned\n"
     "    /// to a FAMILY rather than an entry names the intrinsic only when the\n"
     "    /// family holds nothing else.  False for a key with no entries.\n"
     "    family_is_intrinsic(&this, name: SymbolStr) -> boolean {\n"
     "        const entries: OverloadId[] = this.lookup_family_entries(name);\n"
     "        if (entries.length == 0) { return false; }\n"
     "        for (mut k: i64 = 0; k < entries.length; k++) {\n"
     "            if (!this.entry_is_intrinsic(entries[k])) { return false; }\n"
     "        }\n"
     "        return true;\n"
     "    }\n",
     "    /// Record the kind an `intrinsic function` declaration's entry `id`\n"
     "    /// lowers as.  Its leaf is a key in no function table: an intrinsic is\n"
     "    /// reached through its module (`intrinsics::free`) like every\n"
     "    /// declaration, and its bare spelling is an error where it is declared\n"
     "    /// in no scope.  Codegen reads the kind off the pin, so a function\n"
     "    /// sharing an intrinsic's leaf is never taken for it.\n"
     "    mark_intrinsic(mut &this, id: OverloadId, kind: IntrinsicKind) -> void {\n"
     "        if (!id.is_valid()) { return; }\n"
     "        this.overload_func_intrinsic[id.position() as i64] = kind;\n"
     "    }\n"
     "\n"
     "    /// How a call pinned to entry `id` lowers; `Call` for an invalid id.\n"
     "    entry_intrinsic_kind(&this, id: OverloadId) -> IntrinsicKind {\n"
     "        if (!id.is_valid()) { return IntrinsicKind::Call; }\n"
     "        return this.overload_func_intrinsic[id.position() as i64];\n"
     "    }\n"
     "\n"
     "    /// How a call pinned to the FAMILY `name` lowers: the kind its entries\n"
     "    /// share, when every entry has the same one; `Call` otherwise, and for a\n"
     "    /// key with no entries.\n"
     "    family_intrinsic_kind(&this, name: SymbolStr) -> IntrinsicKind {\n"
     "        const entries: OverloadId[] = this.lookup_family_entries(name);\n"
     "        if (entries.length == 0) { return IntrinsicKind::Call; }\n"
     "        const kind: IntrinsicKind = this.entry_intrinsic_kind(entries[0]);\n"
     "        for (mut k: i64 = 1; k < entries.length; k++) {\n"
     "            if (this.entry_intrinsic_kind(entries[k]) != kind) { return IntrinsicKind::Call; }\n"
     "        }\n"
     "        return kind;\n"
     "    }\n"),

    (TR,
     "import compiler::types::user_defined::{\n",
     "import compiler::intrinsic_kind;\n"
     "import compiler::intrinsic_kind::{ IntrinsicKind };\n"
     "import compiler::types::user_defined::{\n"),
    (TR,
     "                        // The ENTRY is marked as the intrinsic - codegen's\n"
     "                        // inline decision reads the mark off the pin - and\n"
     "                        // its leaf is a key nowhere: a bare `malloc` with no\n"
     "                        // declaration in scope is an error, and the\n"
     "                        // intrinsic is reached as `intrinsics::malloc` like\n"
     "                        // every declaration.\n"
     "                        ctx.decl_index.mark_intrinsic(i_entry);\n",
     "                        // The ENTRY carries the intrinsic's kind, read from\n"
     "                        // the declaration's own name here, the one place a\n"
     "                        // spelling decides it: codegen's inline decision reads\n"
     "                        // the kind off the pin.  Its leaf is a key nowhere: a\n"
     "                        // bare `malloc` with no declaration in scope is an\n"
     "                        // error, and the intrinsic is reached as\n"
     "                        // `intrinsics::malloc` like every declaration.\n"
     "                        ctx.decl_index.mark_intrinsic(i_entry,\n"
     "                            IntrinsicKind::from_name(ctx.intern_table.resolve(node.name)));\n"),

    (CALL,
     "import compiler::codegen::ops::intrinsics_codegen;\n"
     "import compiler::codegen::ops::intrinsics_codegen::{ IntrinsicKind };\n",
     "import compiler::intrinsic_kind;\n"
     "import compiler::intrinsic_kind::{ IntrinsicKind };\n"),
    (CALL,
     "    /// The leaf of the INTRINSIC a call is pinned to, or \"\" when the call\n"
     "    /// is a function call.  The pin decides, by the mark the intrinsic's\n"
     "    /// registration put on its entry: any other pin - the exported\n"
     "    /// `std::core::panic` wrapper, a user's `rotl32`, an `extern \"C\"`\n"
     "    /// declaration whose C symbol IS the leaf - is a real function with a\n"
     "    /// body, whatever its leaf or its symbol; and a call with NO pin is a\n"
     "    /// call through a VALUE - a parameter, a local or a field holding a\n"
     "    /// function pointer - which is never an intrinsic, whatever the value\n"
     "    /// is named (a parameter `bswap32: (u32) -> u32` called through the\n"
     "    /// pointer was inlined as the byte swap).  The kind is then selected by\n"
     "    /// the pinned declaration's own leaf, never by the written one.  A leaf\n"
     "    /// test in front of the pin inlined every `core::panic(...)` in the\n"
     "    /// stdlib against a spelling that named the wrapper, and a symbol test\n"
     "    /// after it would take `libc::malloc` for the intrinsic.\n"
     "    pinned_intrinsic_leaf(&this, node: CallExprNode*) -> string {\n"
     "        const di: DeclarationIndex* = this.cg.get_decl_index();\n"
     "        mut key: SymbolStr = SymbolStr::empty();\n"
     "        match (&node.resolved_callee) {\n"
     "            CalleePin::None      => { }\n"
     "            CalleePin::Family(f) => { if (di.family_is_intrinsic(f)) { key = f; } }\n"
     "            CalleePin::Decl(e)   => { if (di.entry_is_intrinsic(e)) { key = di.entry_key(e); } }\n"
     "        }\n"
     "        if (!key.is_valid()) { return \"\"; }\n"
     "        return QualifiedName::leaf_of(this.cg.get_intern().resolve(key));\n"
     "    }\n",
     "    /// The kind of the INTRINSIC a call is pinned to, or `Call` when the\n"
     "    /// call is a function call.  The pin decides, by the kind the\n"
     "    /// intrinsic's registration put on its entry: any other pin - the\n"
     "    /// exported `std::core::panic` wrapper, a user's `rotl32`, an `extern\n"
     "    /// \"C\"` declaration whose C symbol IS an intrinsic's leaf - is a real\n"
     "    /// function with a body, whatever its leaf or its symbol; and a call\n"
     "    /// with NO pin is a call through a VALUE - a parameter, a local or a\n"
     "    /// field holding a function pointer - which is never an intrinsic,\n"
     "    /// whatever the value is named.  No spelling is read here.\n"
     "    pinned_intrinsic_kind(&this, node: CallExprNode*) -> IntrinsicKind {\n"
     "        const di: DeclarationIndex* = this.cg.get_decl_index();\n"
     "        return match (&node.resolved_callee) {\n"
     "            CalleePin::None      => { IntrinsicKind::Call }\n"
     "            CalleePin::Family(f) => { di.family_intrinsic_kind(f) }\n"
     "            CalleePin::Decl(e)   => { di.entry_intrinsic_kind(e) }\n"
     "        };\n"
     "    }\n"),
    (CALL,
     "        const leaf: string = this.pinned_intrinsic_leaf(node);\n"
     "        if (leaf.length() == 0) { return false; }\n",
     "        const kind: IntrinsicKind = this.pinned_intrinsic_kind(node);\n"
     "        if (kind == IntrinsicKind::Call) { return false; }\n"),
    (CALL,
     "        // lowered here rather than through `IntrinsicKind`.\n"
     "        if (leaf.eq(\"try_catch\")) {\n",
     "        // lowered here rather than by the inline dispatcher.\n"
     "        if (kind == IntrinsicKind::TryCatch) {\n"),
    (CALL,
     "        if (!IntrinsicKind::is_name(leaf)) { return false; }\n", ""),
    (CALL,
     "        if (leaf.eq(\"panic\")\n",
     "        if (kind == IntrinsicKind::Panic\n"),
    (CALL,
     "        return match (this.cg.intrinsics.try_emit(leaf, args)) {\n",
     "        return match (this.cg.intrinsics.try_emit(kind, args)) {\n"),

    (C + "codegen/passes.cryo",
     "import compiler::codegen::ops::intrinsics_codegen;\n", ""),
    (C + "codegen/passes.cryo",
     "        // Cryo runtime support is fully inline-lowered: every bare-name\n"
     "        // intrinsic (i32_to_i64, ptr_diff, format, bswap*, clz*, fpclass,\n"
     "        // atomic_fence, dirent_*) is emitted directly into LLVM IR by\n"
     "        // IntrinsicsCodegen.",
     "        // Cryo runtime support is fully inline-lowered: every intrinsic\n"
     "        // with a kind (ptr_diff, fpclass, atomic_*, dirent_*, ...) is\n"
     "        // emitted directly into LLVM IR by IntrinsicEmitter."),
    (C + "codegen/visit/ir_generator.cryo",
     "import compiler::codegen::ops::{ ir_flow_emitter, intrinsics_codegen };\n",
     "import compiler::codegen::ops::{ ir_flow_emitter };\n"),
]


def apply_edits(texts, edits, check):
    pending = 0
    for rel, old, new in edits:
        if rel not in texts:
            texts[rel] = (ROOT / rel).read_bytes().decode("utf-8")
        text = texts[rel]
        if check:
            # applied: the new text is there, or (a deletion) the old is gone
            if (new and new in text) or (not new and old not in text):
                continue
            pending += 1
            continue
        count = text.count(old)
        if count == 0:
            raise SystemExit(f"NO MATCH: {rel}: {old[:80]!r}")
        if count > 1:
            raise SystemExit(f"AMBIGUOUS ({count}): {rel}: {old[:80]!r}")
        pending += 1
        texts[rel] = text.replace(old, new, 1)
    return pending


def main() -> int:
    check = "--check" in sys.argv[1:]
    old_path = ROOT / OLD_TABLE
    if check:
        missing = old_path.exists() or not (ROOT / NEW_TABLE).exists()
        texts = {}
        pending = apply_edits(texts, EDITS, True)
        print(f"{pending} edit(s) not applied; table {'NOT moved' if missing else 'moved'}")
        return 1 if (pending or missing) else 0
    if not old_path.exists():
        print("already applied (the old table is gone)")
        return 1
    old = old_path.read_bytes().decode("utf-8")
    texts = {}
    texts[NEW_TABLE] = new_table(old)
    em_edits, cast_arm = emitter_edits(src_type_method(old))
    apply_edits(texts, em_edits, False)
    # the dispatcher's conversion arms, and the comment above each group
    lines = texts[EMITTER].split("\n")
    out = []
    for line in lines:
        m = cast_arm.match(line)
        if m and CAST_KIND.match(m.group(1)):
            while out and out[-1].strip().startswith("//"):
                out.pop()
            if out and out[-1].strip() == "":
                out.pop()
            continue
        out.append(line)
    texts[EMITTER] = "\n".join(out)
    n = apply_edits(texts, EDITS, False)
    for rel, text in texts.items():
        (ROOT / rel).write_bytes(text.encode("utf-8"))
    old_path.unlink()
    print(f"table moved to {NEW_TABLE}; {n + len(em_edits)} edit(s); {OLD_TABLE} deleted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
