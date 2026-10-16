# Class removal and borrowed trait objects: the plan

Classes are removed from the language before the v1.0 freeze, on branch
`class-removal`.  What remains is structs, enums and traits.  The compiler's
AST goes straight to enums and `match` (ruling 290).  Borrowed trait objects
(`virtual &T`, `virtual T*`; rulings 285, 288, 289, 360) land as part of the
same branch, built from the class vtable machinery rather than deleting it
and rebuilding it.  The compiler and the standard library use the language's
own traits where one fits (ruling 368).

This plan was written from the tree of `type-syntax-impl` at `d5ad3a9d`, read
and measured only; nothing in it was built except two throwaway probes
(section 2.6).  The decisions it needs are in section 9; nothing in a slice
that depends on one starts before it is answered.

**Status.**  The 13 decisions in section 9 are answered.  Slices 0 and 1
are built; their results are recorded under each slice in section 6.
Every temporary bridge piece is listed in section 10 until it is deleted.

**Prerequisite:** the array unification is finished (it has landed).  The
node shapes below hold their children in `[T]` (`Array<T>`); the pin of
`d5ad3a9d` refused `for (s in xs)` over a `[Stmt]` (E0306, "`[Stmt]` does
not implement `Iterator`"), which the unification removed.

---

## 1. Inventory, measured

Every number is a grep over `compiler/src` unless it says otherwise.  The
commands are in section 1.8 so each one can be re-run.

### 1.1 Classes: 109, all in the compiler

| group | classes | where |
|---|---|---|
| AST nodes | 69 | `AST/{node,expression,statement,declaration,pattern}.cryo` |
| AST helpers | 2 | `AsmOperand`, `AsmPiece` (`AST/statement.cryo`; no base) |
| visitors | 8 | `ASTVisitor`, `BaseASTVisitor`, and 6 concrete ones |
| types | 26 | `Type` + 25 subclasses (`types/{type_base,compound,generic,user_defined}.cryo`) |
| parser | 3 | `ParserBase` -> `ExprParser` -> `Parser` |
| other | 1 | `DirectiveRegistry` (`passes/directive_processing.cryo`; no base) |

(112 `type class` lines; 3 are comments.)

The AST's 69 are `ASTNode`; `ProgramNode`; the three category bases
`ExpressionNode`, `StatementNode`, `DeclarationNode`; 25 expression, 15
statement and 19 declaration kinds; `PatternNode` (concrete, with its own
`PatternKind` tag) and `EnumPatternNode` under it; `MatchArmNode`,
`StaticMatchArmNode`, `DirectiveNode`.  The deepest chain is 3
(`ASTNode` -> `ExpressionNode` -> `LiteralNode`).

`NodeKind` has 72 entries; 6 have no class and nothing outside the AST
module names them (`Expression`, `Statement`, `Declaration`,
`AsyncExpression`, `Unknown`, and `Pattern` except in `PatternNode`'s own
constructor).

Every hierarchy is closed: all subclasses are in the compiler, and none of
the 109 is extended outside its own group.  None needs open dispatch.

### 1.2 Dispatch and casts

| measure | today | earlier figure |
|---|---|---|
| `.accept(` calls | 188 | 188 |
| lines comparing against `NodeKind::` (`==`/`!=`) | 877 | |
| `match` arms on `NodeKind::` | 804 | |
| all `NodeKind::` references | 1,766 | ~1,300 "kind checks" |
| casts to an AST node pointer (`as XNode*`) | 2,266 | ~2,400 casts |
| casts to a `Type` subclass pointer | 904 | |
| all `as X*` casts | 3,198 | |
| `TypeKind::` references | 1,693 | |

The AST casts by directory: sema 882, passes 574, AST 241, mono 171,
resolver 117, codegen 85, parser 75, bindgen 65, types 21, others 20.  The
heaviest single file is `sema/async_lower.cryo` (7,157 lines, 383 `NodeKind`
references), then `passes/drop_insertion.cryo` (110), `sema/sema.cryo` (92),
`passes/type_resolution.cryo` (90), `sema/call_resolver.cryo` (81),
`mono/call_specializer.cryo` (74), `passes/dead_code.cryo` (72),
`passes/move_check.cryo` (67), `mono/ast_resolver.cryo` (54) and
`resolver/stamp_check.cryo` (52).

Outside the six visitors, at least 35 hand-written recursive tree walks
switch on `NodeKind` and cast (drop insertion 14, move check 8, async
lowering 3, call specializer 3, and others).  That count comes from a
function-name heuristic (`walk_`, `scan_`, `contains_`, `collect_` and
similar, taking a node pointer), so it is a floor.

### 1.3 `virtual` and `override`

Measured as declaration modifiers (a line starting with the keyword):

- `virtual`: 74 lines in 4 files.  `AST/visitor.cryo` 68 (the `visit`
  overloads), `types/type_base.cryo` 3 (`display_name`, `size_bytes`,
  `alignment`), `parser/parser_base.cryo` 2 (`parse_inner_block`,
  `parse_expr_from_tokens`) and `AST/node.cryo` 1 (`accept`).
- `override`: 555 lines in 17 files.  `override visit(` is 413 of them:
  dumper 68, `BaseASTVisitor` 68, cloner 65, name resolver 59, substituter
  53, sema 49, IR generator 47 (+ 4 more in codegen helpers' comments).
  The rest are `override accept` on 69 nodes, and the type trio on 25 types
  (`display_name`/`size_bytes`/`alignment`, plus one `is_resolved`), and
  the parser's 2 hooks.

So the vtables in use are four: the visitor's (overloaded `visit`), the
node's (`accept`), the type's (three methods) and the parser's (two hooks).

### 1.4 `new`, `delete`, heap identity, inheritance

- `new` of a class: **545 sites** (parser 155, sema 153, AST 71, passes 47,
  bindgen 47, types 40, codegen 28, mono 4).
- `delete` of a class: **1** (`compilation_context.cryo:913`, the directive
  registry).
- Classes made as values, not with `new`: the visitors and the parser
  (`ASTCloner()`, `Parser(...)`, `NameResolver(...)`, `SemaVisitor(...)`,
  boxed `IRGeneratorVisitor`).  `docs/cryo.md` section 9 says classes are always
  allocated with `new`; the compiler itself contradicts it.
- **Node identity is the address.**  64 fields outside the AST hold a node
  pointer (sema 26, mono 9, types 8, others), among them
  `DeclarationIndex.declarations: [ASTNode*]` and
  `overload_func_decl: [ASTNode*]`, `ConstTable`'s `init: ExpressionNode*`
  and `ImplBlockNode.trait_item_methods`.  No table hashes a node address:
  every store is keyed by `DefId` or another identity (rule 2), and nodes
  are compared by pointer where they are compared at all.
- **Nodes are rewritten in place** through pointers: `desugared_call`,
  `unwind_cleanup`, the substituter's rewriting of types and annotations,
  and the async lowering's restructuring.
- **Inheritance is relied on for:** the shared header fields (`kind`,
  `span`, `has_error`, `generated` on every node; `resolved_type`,
  `autoref`, `arg_binding` on expressions); the implicit upcast
  `XNode*` -> `ASTNode*`/`ExpressionNode*`; the arena (`Allocate` on
  `ASTNode`, inherited by all 69, ruling 237); and `accept`.  The header
  is read a lot: `.span` 1,175 times, `.resolved_type` 632, `.has_error`
  97, `.generated` 48.
- `ASTNode*` is the "any node" handle in 737 places (43 arrays of it),
  `ExpressionNode*` 849, `StatementNode*` 334, `PatternNode*` 79,
  `DeclarationNode*` 50.

### 1.5 Outside the compiler

- **Standard library:** no class, no `virtual`/`override`, no `new` of a
  class.  "Class" appears only as allocator size classes.
- **Examples:** none (one comment, `examples/07-shapes`).
- **Language server (`tools/CryoLSP`):** declares no class, but uses the
  compiler's AST heavily: 571 references to node classes, 203 node casts,
  191 `NodeKind::`, 78 `.span`, and no `accept`.  Class-specific uses are in
  `completion.cryo` (walks `base_class` for member completion),
  `identity.cryo` (`base_res`), `members.cryo`, `symbols.cryo` (LSP symbol
  kind 5 for a class), `uses.cryo`, `code_lens.cryo`, `hover.cryo`,
  `names.cryo` (`TypeKind::Class`), `semantic_tokens.cryo` and
  `keyword_docs.cryo` (hover text for `class`, `protected`, `virtual`,
  `override`).  `tools/CryoAnalyzer` lists `class` in its TextMate grammar.
- **bindgen** builds AST nodes (47 `new`, 65 casts) and skips C++ classes.
- **Tests:** 57 `type class` lines.
  - About class features: `tests/tests/lang/classes.cryo` (11 tests);
    negative files `E0200_class_literal_inherited_field_type`,
    `E0203_undefined_base_class`, `E0208_class_value_to_pointer`,
    `E0308_allocate_below_base`, `E0360_missing_visibility_block`,
    `E0364_async_constructor`, `E0364_async_destructor`,
    `E0364_async_virtual_method`, and `E0359_protected_in_struct`, whose
    premise is `protected`; projects `class_at_compiler_ast_path_is_ordinary`,
    `class_casts_by_pointer`, `class_constructed_by_name`,
    `class_constructor_arguments_refused`,
    `class_constructor_overloads_by_arity`,
    `class_field_redeclared_in_derived`,
    `class_inherited_function_field_call`,
    `generic_class_infers_from_constructor`, and `allocate_trait_routes_new`
    (a base/leaf hierarchy).
  - Using classes incidentally, so they get ported to structs:
    `lang/async_method.cryo`, `lang/swap_pointer_array.cryo`,
    `lang/visibility.cryo` (protected and subclass access);
    `E0208_cast_into_other_aggregate`, `E0312_allocate_wrong_shape`,
    `E0452_double_drop`, `E0452_use_after_move_into_constructor`;
    `compare_with_null_refused`, `generic_instance_from_use`,
    `visibility_module_private`.
  - The roster golden names 16 class unit tests and 11 class
    negative/project entries.

### 1.6 The language surface to delete

- **Lexer:** `KwClass`, `KwVirtual`, `KwOverride`, and in effect
  `KwProtected` (`lex/_module.cryo`).  `new` and `delete` stay: they serve
  structs, arrays and enum payloads too.
- **Parser:** `parse_class_declaration` (`parser.cryo:1136`), the base list,
  `protected:` in `parse_type_body`, the `virtual`/`override` modifiers
  (`:1367-1399`), the base-constructor initializer `: Base(args)`
  (`:1481-1494`), and the `class` tag on implement heads (`:2193` etc.).
  Constructors and destructors (`Name(...) {}`, `~Name()`) are parsed in
  struct bodies too; whether they stay is question 6.
- **AST:** `ClassDeclNode`; `MethodNode.is_virtual`, `is_override`,
  `base_ctor_name`, `base_ctor_args`; `NodeKind::ClassDeclaration`.
- **Types:** `ClassType` (`base_class`, `has_virtual_methods`,
  `own_field_position`), `TypeKind::Class`, `is_subclass_of`,
  `TypeArena::create_class`, `class_at_depth`, `MethodInfo.is_virtual` and
  `is_override`.
- **Resolution:** member lookup up the base chain (`member_resolver.cryo`,
  `call_resolver.cryo`), the base-class name (E0203 site), binding a class
  name as its constructor (declaration D68; `declares_constructor`,
  `SymbolKind::Constructor`), the scope of a base-constructor initializer,
  and the `member-field` door `ClassType::own_field_position` in
  `docs/resolution-rules.md`'s door table.
- **`new` and casts:** the vtable seed at the three allocation sites; the
  implicit `Derived*` -> `Base*` conversion (`types/checker.cryo:277`);
  pointer casts along the hierarchy (ruling 258); the class-value
  refusals and their help text (ruling 253; E0208 at `sema.cryo:2803-2826`).
- **`Allocate` on classes:** the nearest-ancestor rule (ruling 249),
  `refuse_allocate_below_base` (ruling 294/303, E0308), and E0312's
  "struct or class" wording (`check_allocate_owner`).
- **Codegen:** `declare_class_type`, `declare_class_methods`,
  `collect_vtable_slots`, `find_vtable_slot_impl`, `vtable_slot_index`,
  `codegen_vtable_for_class` (all `codegen/ops/declaration_emitter.cryo`),
  `TypeMapper::map_class`, `emit_class_decl`, `emit_base_ctor_call`, the
  virtual-call branch in `call_emitter.cryo`, `compute_class_layout`.
  Part of this is reused for trait objects (section 5).
- **Passes:** `validate_class`, `DirectiveTarget::Class`, the class arms in
  type resolution (7), mono's type populator and monomorphizer, and drop
  insertion.
- **Mangling:** `MangledName::for_vtable` (`C$vt$...`) and the demangler's
  `<vtable for ...>`; the spec's `ct`/`dt`/`vt` tags.
- **Errors:**
  - Class-only and emitted: E0360 (missing visibility block), E0359
    (`protected:` in a struct).
  - Class sites in shared codes: E0308, E0312, E0208, E0364 (virtual or
    override async; async constructor/destructor), E0203 (base class),
    E0201/E0231's constructor note, E0214's constructor arity, and E0353's
    `protected` handling.
  - Declared and never emitted, to be retired with their numbers reserved:
    E0351, E0352, E0354, E0357, E0610, E0618, E0639, E0909.
- **Docs:**
  - `docs/cryo.md`: section 9 entirely (1665-1831), the keyword table,
    section 2.9 on casts, section 8.2 on `protected` in structs, section
    14.4, section 15.2 on `new`/`delete` of classes and `Allocate` with a
    base, section 17.3.1 on the vtable layout, the directive table, and
    the async rows.
  - `docs/grammar.md`: the aggregate rule, the implement kind tag and the
    keyword list.
  - `docs/abi.md`: two minor lines.
  - `docs/cryo-mangling-spec.md`: the `vt`/`ct`/`dt` tags and the
    `C$vt$6Animal` example.
  - `docs/resolution-rules.md`: the `member-field` door row for classes,
    and new rulings superseding 249's inheritance clause, 253, 258, 294,
    295 and 303.

### 1.7 Ad-hoc methods that want a trait (ruling 368)

In `compiler/src` today:

- 0 `Clone` implementations against 11 hand-written `clone(&this)`
  methods: `TypeAnnotation`, `TraitRef`, `TraitBound` (AST),
  `ParamPlan` (codegen ABI), `Diagnostic`, `Suggestion`,
  `MonomorphRequest`, and others.
- 0 `Display` implementations against 9 `to_string(&this)` methods:
  `NodeKind`, `TypeKind` and its kind enums, `TokenType`, `SourceSpan`,
  `Diagnostic`, and a scope kind.
- 12 `Eq` and 10 `Hash` implementations already.  16 `equals(&this` lines,
  some of them those implementations; `decl_index.cryo` has 5.

### 1.8 How the numbers were taken

From `compiler/src` with Git Bash `grep -rn ... --include=*.cryo`:

- classes: `type class\b`
- accepts: `\.accept\(`
- `NodeKind` compares: `(==|!=) *NodeKind::`
- `NodeKind` arms: `^\s*NodeKind::[A-Za-z]+ *(\||=>)`
- node casts: `as (ASTNode|ProgramNode|[A-Za-z]+Node)\*`
- `new` of a class: `new (<the 109 names>)\b`
- modifiers: `^\s*virtual [a-z_]+\(` and `^\s*override [a-z_]+\(`

The control on the cast and `new` counts: the per-directory sums add up to
the totals, and the parser's count (155 `new`) matches a file that builds
nodes and does little else.

---

## 2. The AST design

### 2.1 Shape: a node struct that owns its kind enum

Each category is a **struct carrying the shared header, with an enum of
pointers to per-kind payload structs**:

```cryo
/// Every expression: what all kinds share, and which kind it is.
type struct Expr {
    span:          SourceSpan;
    has_error:     boolean;
    generated:     boolean;
    resolved_type: TypeRef;
    autoref:       boolean;
    arg_binding:   ArgBinding;
    kind:          ExprKind;
}

type enum ExprKind {
    Literal(LiteralExpr*);
    Identifier(IdentifierExpr*);
    Binary(BinaryExpr*);
    Unary(UnaryExpr*);
    Call(CallExpr*);
    MemberAccess(MemberAccessExpr*);
    // ... one variant per expression kind, 25 in all
}
```

The categories are `Expr`/`ExprKind`, `Stmt`/`StmtKind`, `Decl`/`DeclKind`,
`Pattern`/`PatternKind` (the existing tag enum of that name is folded in),
plus the small fixed nodes `MatchArm`, `StaticMatchArm` and `Directive`,
which are plain structs because they have one shape each.  `Program` is a
plain struct.  The "any node" handle is an enum over the categories:

```cryo
type enum Node {
    Expr(Expr*);
    Stmt(Stmt*);
    Decl(Decl*);
    Pattern(Pattern*);
    Arm(MatchArm*);
    StaticArm(StaticMatchArm*);
    Directive(Directive*);
    Program(Program*);
}
```

**Why this shape, and not a bare enum value as the handle** (question 1
offers both):

- **Identity stays the address.**  An `Expr*` is what an `ExpressionNode*`
  is today: 8 bytes, nullable, compared by pointer.  The 64 node-pointer
  fields outside the AST and the arrays of them keep their meaning.  An
  absent child stays `null`, so the codebase's 4,134 null comparisons are
  not touched by the conversion; with a bare enum handle every absent
  child becomes an `Expr?`.
- **The header stays a field.**  The 1,175 `.span` and 632
  `.resolved_type` reads stay `e.span` and `e.resolved_type`.  A bare enum
  handle makes each of them a method call that matches over 25 variants.
- **Payloads are plain-copy pointers.**  `match (e.kind)` copies a tag and a
  pointer, so no `match` ever moves an owning value out from behind a
  reference: the hole the roadmap records under "Moving a value out from
  behind a reference" is not on this path.
- **Dispatch is `match`, and exhaustive.**  A `match (e.kind)` with no `_`
  arm is checked for exhaustiveness, which the `NodeKind` compare-and-cast
  chains never were.
- **Nodes are rewritten in place** through `Expr*` as today, including
  replacing the kind: `e.kind = ExprKind::Call(c)` is how a desugaring can
  turn a node into another kind without the parent knowing.

This is not an intermediate struct stage in ruling 290's sense: the
categories are enums from the first converted kind, and no consumer
dispatches on a tag field.  `NodeKind` is deleted at the end.

The cost is two arena allocations per node (header + payload) where there
is one today.  Section 2.5 says how that is measured.

### 2.2 Before and after: `BinaryExprNode`

Before (`AST/expression.cryo:264`, comments trimmed):

```cryo
type class BinaryExprNode : ExpressionNode {
public:
    op:               Token;
    left:             ExpressionNode*;
    right:            ExpressionNode*;
    desugared_call:   ExpressionNode*;
    desugar_by_bound: boolean;
    append_entry:     OverloadId;
    unwind_cleanup:   [ASTNode*];

    BinaryExprNode(op: Token, left: ExpressionNode*, right: ExpressionNode*, span: SourceSpan)
        : ExpressionNode(NodeKind::BinaryExpression, span) {
        this.op    = op;
        this.left  = left;
        this.right = right;
        this.desugared_call = null;
        // ...
    }

    override accept(visitor: ASTVisitor*) -> void {
        visitor.visit(this);
    }
}
```

After:

```cryo
type struct BinaryExpr {
    op:               Token;
    left:             Expr*;
    right:            Expr*;
    desugared_call:   Expr*;
    desugar_by_bound: boolean;
    append_entry:     OverloadId;
    unwind_cleanup:   [Node];
}

implement trait Allocate for struct BinaryExpr {
    static allocate(size: u64, align: u64) -> void* {
        return cryo_ast_arena_alloc(size, align);
    }
}

implement struct BinaryExpr {
    /// Visit the operands in source order, then the operator's desugaring.
    static walk_children<V>(n: BinaryExpr*, v: mut &V) -> void where V: Walk {
        v.walk_expr(n.left);
        v.walk_expr(n.right);
        if (n.desugared_call != null) { v.walk_expr(n.desugared_call); }
    }
}

implement struct Expr {
    /// A binary expression at `span`, untyped.
    static binary(op: Token, left: Expr*, right: Expr*, span: SourceSpan) -> Expr* {
        const n: BinaryExpr* = new BinaryExpr {
            op: op, left: left, right: right, desugared_call: null,
            desugar_by_bound: false, append_entry: OverloadId::invalid(),
            unwind_cleanup: [],
        };
        return Expr::of(ExprKind::Binary(n), span);
    }

    /// An expression of `kind` at `span` with the header's defaults.
    static of(kind: ExprKind, span: SourceSpan) -> Expr* {
        return new Expr {
            span: span, has_error: false, generated: false,
            resolved_type: TypeRef::invalid(), autoref: false,
            arg_binding: ArgBinding::Unclassified, kind: kind,
        };
    }
}
```

A constructor call `new BinaryExprNode(op, l, r, span)` becomes
`Expr::binary(op, l, r, span)`; the 545 `new` sites are rewritten this way,
mechanically.

### 2.3 Before and after: a consumer

Before (`passes/drop_insertion.cryo:3982`):

```cryo
match (stmt.kind) {
    NodeKind::IfStatement => {
        const it: IfStmtNode* = stmt as IfStmtNode*;
        if (this.stmt_assigns_tracked_init(it.then_stmt)) { return true; }
        return it.else_stmt != null &&
               this.stmt_assigns_tracked_init(it.else_stmt);
    }
    NodeKind::WhileStatement => {
        return this.stmt_assigns_tracked_init((stmt as WhileStmtNode*).body);
    }
    // ...
}
```

After:

```cryo
match (stmt.kind) {
    StmtKind::If(it) => {
        if (this.stmt_assigns_tracked_init(it.then_stmt)) { return true; }
        return it.else_stmt != null &&
               this.stmt_assigns_tracked_init(it.else_stmt);
    }
    StmtKind::While(w) => {
        return this.stmt_assigns_tracked_init(w.body);
    }
    // ...
}
```

The arm binds the payload, so the cast, the second name for the same node
and the chance of casting to the wrong class all go.

### 2.4 Ownership, children, spans and stamps

- **Arena.**  Nodes are allocated in the AST arena and never released one
  at a time; the arena is reset whole at each target boundary, as today.
  Each node struct implements `Allocate` over `cryo_ast_arena_alloc`.
  Neither inheritance (none) nor a blanket implementation (the language has
  none) can share that implementation, and an enum may not implement
  `Allocate` (ruling 292).  So it is one 5-line implementation per node
  struct: about 70 payloads + 4 headers + the fixed nodes.  Question 2.
- **Children** are `Expr*`, `Stmt*` etc.; child lists are `[Expr*]`,
  `[Stmt*]`; heterogeneous lists (`Program.items`, `unwind_cleanup`) are
  `[Node]`.  Array storage comes from the arena when the driver turns arena
  routing on, as today.
- **No `Drop`** on node structs.  A node's `[T]` and `string` fields are
  arena memory or leaked until the arena reset, as today.  The two-pointer
  `match` copies need no drop glue.
- **Spans** live in the category header, `e.span`.  The fixed nodes carry
  their own `span`.
- **Identity stamps** stay on the payload that carries the name.  These are
  `res: ResSlot`, the `DefId`s, `sym_id`, `resolved_callee: CalleePin`, and
  `resolved_trait_slot`.  Rule 1 does not change: the name layer stamps the
  payload where the name is written, and a synthesizing pass creates its
  payload already stamped.  `Expr*`/`Decl*` addresses remain the node
  identity for the side tables that store nodes.
- **The `void*` cycle-breakers go.**  `TypeofAnnotation.operand`,
  `PatternSub.pattern` and `StaticAssertItem.cond` are `void*` today, and
  their comments say the AST root module cannot import
  `compiler::ast::expression` without a module cycle.  Slice 0 measured
  that claim false: the node structs keep the current module split, and
  these fields become `Expr*` and `Pattern*` (section 6, slice 0).
- **`TypeAnnotation`** is already an enum of pointers to annotation structs
  (`AST/_module.cryo:335`).  It is the precedent for this design, and it
  does not change except that its hand-written `clone` becomes `Clone`
  (section 4).

### 2.5 Types: the same shape

`Type` and its 25 subclasses become:

```cryo
type struct Type {
    id:   u64;
    kind: TypeKind;
    data: TypeData;
}

type enum TypeData {
    Void; Unit; Bool; Char; String; Never; VaList;
    Int(IntegerKind);
    Float(FloatKind);
    Pointer(PointerType*);
    Reference(ReferenceType*);
    Array(ArrayType*);
    Function(FunctionType*);
    Tuple(TupleType*);
    Optional(OptionalType*);
    GenericParam(GenericParamType*);
    Instantiated(InstantiatedType*);
    AssocProjection(AssocProjectionType*);
    Error(ErrorType*);
    Struct(StructType*);
    Class(ClassType*);
    Enum(EnumType*);
    Trait(TraitType*);
    TypeAlias(TypeAliasType*);
    FunctionTemplate(FunctionTemplateType*);
}
```

`TypeArena.types: [Type*]` and `TypeRef` are unchanged.  The three
virtuals become `match` methods on `Type`.  `kind` stays a stored field
during the migration, so the 1,053 `TypeKind` compares are untouched.
Whether it then becomes derived from `data` is question 9.  `Class` is a
variant until the feature is deleted (slice 9).

### 2.6 What the probes showed

Two single-file probes, built and run with the pin `bin/cryo.exe`, live in
`.scratch/class-removal/probe*/` and are deleted with the scratch.  Both
compiled and ran correctly.

- Mutually recursive enum and struct types across categories (`Expr`
  holding `[Stmt]`, `Stmt` holding `Expr`), `Stmt?`, and
  `new Struct { ... }` routed through a struct's own `Allocate` (counted:
  one allocation through it).
- A header struct with a `kind` enum, static factories, and a trait with
  default methods dispatching by `match`.  An implementation overrides
  some hooks, and an overriding hook does work before and after calling
  a shared generic static traversal (`BinaryExpr::walk_children(n, this)`,
  `V` inferred as the implementing type).

The one refusal was `for (s in n.stmts)` over a `[Stmt]` (E0306), which is
the array-unification state, not this design.

---

## 3. The visitors

Today `ASTVisitor` declares 68 `visit` overloads, and `BaseASTVisitor` gives
each an **empty** body.  So there is no shared traversal: every visitor
spells its own recursion, and a visitor that forgets a child silently skips
it.  The visitors that compute something (sema's types, codegen's values)
pass results through a state field (`state.last_value`, the cloner's
`result`), because `visit` returns `void`.

### 3.1 The walker trait

A trait whose default methods **do** the traversal, so an implementation
overrides only the kinds it cares about:

```cryo
/// A walk over the syntax tree in source order.  Every hook's default visits
/// the node's children; an implementation overrides the hooks for the kinds
/// it acts on, and calls the kind's `walk_children` to continue below it.
type trait Walk {
    walk_expr(mut &this, e: Expr*) -> void {
        match (e.kind) {
            ExprKind::Literal(n)      => { this.walk_literal(e, n); }
            ExprKind::Identifier(n)   => { this.walk_identifier(e, n); }
            ExprKind::Binary(n)       => { this.walk_binary(e, n); }
            ExprKind::Call(n)         => { this.walk_call(e, n); }
            // ... every kind, no `_` arm
        }
    }

    walk_stmt(mut &this, s: Stmt*) -> void {
        match (s.kind) {
            StmtKind::Block(n) => { this.walk_block(s, n); }
            StmtKind::If(n)    => { this.walk_if(s, n); }
            StmtKind::While(n) => { this.walk_while(s, n); }
            // ...
        }
    }

    walk_decl(mut &this, d: Decl*) -> void { /* the same, over DeclKind */ }
    walk_pattern(mut &this, p: Pattern*) -> void { /* over PatternKind */ }
    walk_annotation(mut &this, a: TypeAnnotation*) -> void { }

    walk_literal(mut &this, e: Expr*, n: LiteralExpr*) -> void { }
    walk_identifier(mut &this, e: Expr*, n: IdentifierExpr*) -> void { }
    walk_binary(mut &this, e: Expr*, n: BinaryExpr*) -> void {
        BinaryExpr::walk_children(n, this);
    }
    walk_call(mut &this, e: Expr*, n: CallExpr*) -> void {
        CallExpr::walk_children(n, this);
    }
    walk_if(mut &this, s: Stmt*, n: IfStmt*) -> void {
        IfStmt::walk_children(n, this);
    }
    // ... one hook per kind
}
```

- The traversal of a kind lives once, in its `walk_children`.  Adding a
  field to a node means editing one `walk_children`, and every walker sees
  the new child.
- A trait method cannot call the default body it overrides, so
  `walk_children` is the shared continuation.  The probe showed it working
  for pre/post work around the children (a depth counter).
- Every call is static: the default bodies are instantiated per
  implementing type, and `walk_children<V>` is monomorphized per walker.
  No vtable and no indirect call is involved, so the hot path is a `switch`
  where it is an indirect `accept` call plus an indirect `visit` call today.
- One trait serves read-only and rewriting walks.  The payloads are
  pointers, so a hook can rewrite `n.left` or `e.resolved_type` in place.

### 3.2 Each visitor

| visitor | today | becomes | why |
|---|---|---|---|
| **substituter** (`ASTTypeSubstituter`, 53 overrides) | rewrites every expression's type and every annotation, then recurses by hand into the same children | **`Walk`**, overriding `walk_expr` (substitute the header, then the default dispatch) and `walk_annotation`, plus a handful of kinds with their own stamps (`desugared_call`, names that denote the specialization) | It is a full traversal with a uniform action, which is exactly what the defaults are for.  Expected to shrink from 53 hand-written recursions to about 10 overrides. |
| **name resolver** (`NameResolver`, 59 overrides, 76 `accept`) | most overrides only recurse (`if`, `while`, `do-while`); some push and pop scopes | **`Walk`**, overriding the scope-opening kinds (block, function, method, lambda, `for`, match arm, implement block) and the name-carrying ones (identifier, scope resolution, member access, annotations) | Its recursion-only overrides are the defaults.  Scope push and pop wrap `walk_children`. |
| **dumper** (`ASTDumper`, 68 overrides) | prints a tree with a depth stack, needs the intern table | **`Walk`**, one override per kind that prints a line, then `walk_children` inside an indent | Each kind prints different fields, so every hook is overridden; the gain is that traversal and printing stop being written twice.  `Debug`/`Display` do not fit: they cannot take the intern table. |
| **cloner** (`ASTCloner`, 65 overrides, `result` field) | builds a copy, returning it through `this.result` | **plain `match`**: `clone_expr(&this, e: Expr*) -> Expr*` and so on, one arm per kind, returning the copy | A copy returns a new tree, which a `void` walk cannot express without the result channel this removes.  It is configured per use (`carries_types`, `spec_name`, `skip_generics`), so the context-free `Clone` trait does not fit the whole cloner.  `Clone` does fit the leaf data it copies (section 4). |
| **sema** (`SemaVisitor`, 49 overrides, `resolve_expr` already a `match (expr.kind)`) | types each node; the overrides are reached through `accept` and return through a side channel | **plain `match`** in `resolve_expr`/`check_stmt`, calling `check_binary(e, n)` etc. directly and returning the `TypeRef` | Typing is a fold with an expected type in and a type out, not a walk.  It already dispatches by tag; this removes the cast and the `accept` round trip. |
| **IR generator** (`IRGeneratorVisitor`, 47 overrides, `state.last_value`) | emits each node, value through `last_value` | **plain `match`** (`emit_expr(e) -> LValue`, `emit_stmt(s)`) | The same reason as sema.  The emitters (`CallEmitter`, `ExprDispatch` etc.) keep their structure. |

Returning values instead of the side channels (sema's expression return
channel, codegen's `last_value`, the cloner's `result`) is a separate slice
from the dispatch change, so neither change hides inside the other
(question 8).

**The 35+ ad-hoc walks** in drop insertion, move check, async lowering and
the others are converted to `match` mechanically with their category.
Those that are "does this subtree contain X" or "collect every Y" questions
become small `Walk` implementations (a struct with a flag or a list) where
that is shorter.  That call is made per walk, not by quota.

**The language server's walks** (`node_locator` in the compiler, the
handlers' casts) follow their category.  `node_locator.cryo` (614 lines) is
a `Walk` candidate.

### 3.3 Where traits help and where `match` is simpler

- **Traits help** where many consumers share one traversal and differ in a
  few kinds: substituter, name resolver, dumper, node locator,
  stamp checker, the contains/collect walks.  The trait makes "visit every
  child" a property of the tree rather than of each consumer.
- **`match` is simpler** where each kind produces something different and
  the result flows back up: sema, codegen, the cloner, the async lowering,
  drop insertion's statement rewriting.  A trait would only be a detour
  through a hook that returns `void`.

---

## 4. Other traits to adopt (ruling 368)

Concrete places, each a candidate to confirm as its slice reaches it:

- **`Allocate`**: every node struct (section 2.4), replacing `ASTNode`'s
  single inherited implementation.  `Type` and its payloads keep the
  default allocator.
- **`Clone`**: the 11 hand-written `clone(&this)` methods.  These are
  `TypeAnnotation` (and its `clone_ptr`), `TraitRef`, `TraitBound`,
  `ParamPlan`, `Diagnostic`, `Suggestion`, `MonomorphRequest` and the
  rest.  Also the leaf data the cloner copies field by field:
  `DirectiveArg`, `PatternElement` and its structs, `AsmOperand`,
  `AsmPiece`, `CalleePin`.  The cloner then calls `.clone()` on them.
- **`Eq`**: `Expr`/`Stmt`/`Decl` identity compare stays pointer `==`; `Node`
  gets `Eq` comparing tag and address, for the places that compare
  heterogeneous nodes.  The 16 `equals(&this` lines are surveyed for any
  that do not implement `Eq` (`decl_index.cryo` holds 5).
- **`Hash`**: only where a node or a node-derived key enters a map.  None
  does today (rule 2), so nothing is expected; the slice records zero with
  its control.
- **`Display`**: the 9 `to_string(&this)` methods on self-describing values
  (`SourceSpan`, `TokenType`, `IntegerKind`, `FloatKind`, `TypeKind`).
  `NodeKind::to_string` goes with `NodeKind`; its replacement is a
  `kind_name()` on each category for the dumper and debug output.
  Anything that needs the intern table to print stays a method.
- **`Drop`**: no AST node needs one.  `DirectiveRegistry`, the one class
  that is `delete`d, is checked for owned members that its `delete` should
  release, and gets `Drop` if it has them.
- **`Default`**: the node headers.  `Expr::of` is the factory; whether a
  `Default` implementation reads better than a named static is decided
  in the slice and stated.
- **`Iterator`**: not proposed for child traversal.  `walk_children` covers
  it without allocating a child list.

---

## 5. Trait objects (ruling 360)

### 5.1 The class machinery, mapped

| class machinery today | location | trait objects |
|---|---|---|
| slot list: built leaf to root, deduplicated by **name text + parameter type ids**; `override` adds no slot | `collect_vtable_slots`, `declaration_emitter.cryo:734-784` | **replaced.**  Slot *i* is the trait's method at position *i*, by identity: `trait_item_position(DefId)` (`types/generic_registry.cryo:1302`), the numbering E0312 checks implementations against. |
| slot filler: first method up the chain with that name and parameters | `find_vtable_slot_impl`, `:793-844` | **replaced.**  Slot *i*'s function is the implementation's method for trait item *i*: `trait_item_method(block, slot)` (`:1320`), filled by `fill_trait_item_table`.  A trait default fills it with the default's instance for the implementing type. |
| slot index at a call: rebuilt slot list, searched by name, -1 means a direct call | `vtable_slot_index`, `:861-871`; `call_emitter.cryo:585-620` | **replaced.**  The call site already carries `MemberAccess.resolved_trait_slot` (`sema/member_resolver.cryo:837`).  No search, and no -1 fallback. |
| table global: constant `[n x i8*]` of bitcast functions, external linkage | `codegen_vtable_for_class`, `:877-917` | **reused.**  One table per (implementation, trait), emitted in the implementation's module. |
| table name | `MangledName::for_vtable` -> `C$vt$<path>`; demangler `<vtable for ...>` | **extended.**  A new form naming the type and the trait, so two traits' tables for one type differ.  The spelling is question 10. |
| table pointer: hidden `i8**` at field 0 of the object, seeded at the three allocation sites | `TypeMapper::map_class` `type_map.cryo:760`; `new_delete_emitter.cryo:264`, `call_emitter.cryo:342`, `ir_generator.cryo:1239` | **not reused.**  A borrowed object is a pair, `{ data: ptr, table: ptr }`, so the referenced value carries nothing and any implementing type, a primitive among them, can be borrowed as an object. |
| dispatch: GEP field 0, load table, GEP slot, load function, indirect call with the statically resolved function type | `call_emitter.cryo:833-867` | **reused**, reading the table from the pair's second word instead of the object's field 0.  The receiver passed is the pair's first word. |
| `Allocate` through the base chain, `delete` through a base pointer | `new_delete_emitter.cryo:104`, `:480` | **not needed.**  Borrowed objects are never allocated or released through the object (ruling 289); owning boxes come after v1.0. |

### 5.2 What is new

- **Spelling and refusal.**  `virtual &T`, `virtual mut &T` and
  `virtual T*` with `T` a trait, as a prefix that composes inside `[...]`
  (ruling 285).  The parser takes `virtual` in type position.  E0313 keeps
  refusing `&Shape`, `Shape*` and bare `Shape` where they are written
  (287, 302, 304); its note already names the `virtual` spelling.
- **Object safety per method** (ruling 288).  A method is in the table when
  it has a `&this` or `mut &this` receiver, no type parameters of its own,
  and no `This` outside a reference or pointer.  The others are
  unavailable on the object, refused at the call with a code of their own
  (a new code, so question 10).  The rest of the trait still works.
- **Coercion.**  `&x` where `x: X` and `X` implements `T`, written where a
  `virtual &T` is expected, builds the pair.  Only non-generic
  implementations, since a generic one coerced to an object comes after
  v1.0.  A type that does not implement `T` is refused at the coercion.
- **Calls.**  `obj.area()` resolves through the trait's member door, stamps
  `resolved_trait_slot` as a bounded call does, and lowers to the indirect
  call.
- **Layout and ABI.**  Two pointers, passed and returned as a two-field
  aggregate.  Written into `docs/abi.md` in the same commit.

### 5.3 The class defects, and why trait objects cannot repeat them

| defect | bug / ruling | the cause | trait objects |
|---|---|---|---|
| `override` is not checked: an override of nothing, or with another signature, compiles | 0006, ruling 291 | no check exists (only E0364 for async) | Nothing is written as an override.  An implementation is checked against its trait before the merge (E0312, ruling 252): missing, extra and mismatched methods are each refused. |
| a missing slot is silent: the whole table is skipped | 0007 | the early return at `declaration_emitter.cryo:886-892` | The table is built from the trait's positions after E0312 has passed, so every position has a method or a default.  An empty position is an internal error (E0900), never a skipped table or a direct call. |
| slots are matched by name | 0008 | `collect_vtable_slots`/`find_vtable_slot_impl` compare name text | Slots are trait item positions, read by `DefId` (rule 4: identity text never becomes a key).  Two methods of one name in two traits are two tables. |
| `delete` through a base pointer runs the static type's `Drop` | 0009, ruling 295 | `bind_destructor` from `new_delete_emitter.cryo:479` | Borrowed objects are not deleted.  The owning box, when it comes, takes drop, size and alignment from the table (roadmap). |
| `Allocate` on a generic class fails to link | 0001, ruling 292 | `allocator_of` never asks for the generic instance | Moot with classes.  It still needs measuring for a generic **struct**'s `Allocate`, as the roadmap says.  That gets a project test in slice 9, and a bug-log file if it fails. |

Each row gets a negative or project test in the trait-object slices:
an implementation with a missing method refused; one with a mismatched
signature refused; two traits with a same-named method dispatching to the
right one; a default method reached through the table.

### 5.4 When: B, folded in

- **A) Before the AST conversion.**  Nothing in the AST needs open
  dispatch: every hierarchy is closed, and the walker is static.  Building
  trait objects first would also make the compiler their first and
  biggest user before any test has exercised them, and a compiler source
  that uses them needs a two-phase re-pin.  No.
- **C) After the class feature is deleted.**  That deletes
  `codegen_vtable_for_class`, the table naming and the dispatch sequence,
  then writes them again, which is what ruling 360 says not to do.
- **B) Folded in, at a fixed point: after the compiler is free of classes
  (slices 1-6), before the class feature is deleted (slice 9).**  At that
  point the vtable code has no client in the compiler, so it can be
  generalized without a class program regressing in the compiler itself.
  The class tests still run against it, as a control that the shared
  emission still works for classes until they are deleted.  Then the class
  deletion removes only what is class-specific.

**Recommendation: B.**

Dogfood: there is one genuine open-dispatch boundary in the tree, the
compiler library's yield hook.  `yield_hook: (void*) -> void` plus a
`void*` client, in `compilation_context.cryo:219` and `instance.cryo:354`,
set by the language server in `server.cryo:153` and `session.cryo:190`.
It becomes a `virtual YieldHook*` after trait objects are in the pin.
That is a two-phase re-pin: land trait objects, re-pin, then use them.
Question 11.

---

## 6. Migration order

Every slice is one or more commits, each a finished thing with its tests and
docs.  "Identical" below is

```
make verify ARGS="--baseline HEAD --require-identical"
```

The compiler's own code changes in these slices, but no program it compiles
may change: every test and example object must be byte-identical.

Every compiler slice also runs:

- **`make test-census`**
- **`make lsp-check`**: the language server converts in the same slice as
  the category it reads.
- **`make cross-check`** for the other OS's objects.
- **`make check-fast`** before each commit.
- A **clean-build warning set**, taken after `rm -rf compiler/build` and
  compared as a set.
- **`cryo check src/main.cryo`** on the compiler: wall time, interleaved x3
  against the baseline compiler on an idle machine, and peak memory.
- **`make selfhost-check`** at the end of each slice that converts a
  category, both OS, two `FIXED POINT OK`.

### Slice 0: instruments and baseline

- A measuring script (committed, under `scripts/`) that runs
  `cryo check src/main.cryo` and reports wall time and peak working set.
  Baseline figures go into the plan doc.
- Measure the AST arena's bytes per target.  The arena can report its
  chunk count; if a counter is needed, it rides on an existing debug flag,
  not new output.  This is the number section 2.1's second allocation
  moves.
- Settle the module layout for the node structs, so the `void*`
  cycle-breakers can become typed: one `compiler::ast` module with the
  node files, or the current split if the cycle rule allows it.  A probe
  project decides.
- **Proof:** nothing in the compiler changes; check-fast.
- **Prediction:** none, since this is the baseline.

**Built.**

- `scripts/check-cost.py` runs `cryo check src/main.cryo` in `compiler/`
  with one or more compilers, interleaved run by run, all given this
  tree's `--stdlib`.  It reports wall time; peak memory (on Windows the
  peak working set read from the process handle after exit, on POSIX the
  child's `ru_maxrss` through `wait4`, so no `/usr/bin/time`); and the
  arena figures below.  It refuses a run that does not end in a clean
  check.  Control: `PeakWorkingSet64` polled by PowerShell every 100 ms
  during one run read 1515.9 MiB; the script read 1520.3 MiB for the same
  input, and the poll can miss the last 100 ms.
- A counter was needed, because the arena's own `used()` also counts the
  container heap routed into it.  `cryo_ast_arena_alloc` counts node
  allocations and their requested bytes, reset with the arena, and the
  `CRYO_TIMINGS` report prints them with the arena's `used()` and
  `capacity()`.  That is a compiler change, so the proof is verify
  identical rather than "nothing changes".
- **Module layout: the current split stays.**  The `void*` comments are
  wrong: the AST root module already imports `compiler::ast::expression`
  and holds a typed `ExpressionNode*` (`size_expr`, `AST/_module.cryo`),
  and the sibling node modules already import each other (`expression`
  and `statement` both ways).  A probe project with a parent module and a
  child whose struct types point at each other, built and run with the
  pin, compiled and printed the right value.  No `compiler::ast` merge is
  needed for the fields to become typed.

**Baseline** (Windows, the compiler built from `class-removal` with the
counter alone, by the pin; three runs on an otherwise idle machine):

| measure | value |
|---|---|
| `cryo check src/main.cryo` wall time | 8.47 / 8.27 / 8.28 s, median 8.28 s |
| peak working set | 1526.4 / 1522.1 / 1518.6 MiB, median 1522.1 MiB |
| AST node allocations | 1,935,887 |
| AST node bytes | 333,217,680 (172 bytes per node on average) |
| AST arena used (nodes + routed containers) | 1,592,521,804 bytes |

The node count and bytes are the same on every run, as they must be for a
deterministic compiler; a slice that moves them says by how much.

### Slice 1: the parser and the small classes

- `ParserBase` -> `ExprParser` -> `Parser` becomes one struct `Parser`, with
  `implement struct Parser` blocks in the three files (an implement block
  may be written in another module; the fields it touches must then be
  public, which they are today).  The two virtual hooks become direct
  calls.
- `DirectiveRegistry`, `AsmOperand` and `AsmPiece` become structs, with
  `Clone` for the two asm types.
- **Proof:** identical; selfhost.
- **Prediction:** 4 vtables gone from the parser's objects, 5 fewer
  classes, and no measurable change in check time.
- **Wrong if:** any test or example object moves.

**Built.**

- `Parser` is one struct, declared in `parser/cursor.cryo` (the file
  `parser_base.cryo` was, renamed for what it holds) with the state, the
  cursor, spans, errors and recovery.  `expr_parser.cryo` and `parser.cryo`
  each hold an `implement struct Parser` block.  The struct is declared in
  the lowest of the three modules because a module's methods are found
  only once the module is compiled, and module discovery follows imports:
  a probe with the struct in the top module and the blocks below it,
  imported by nothing, refused a call into a lower block (E0358).
  `parser.cryo` re-exports `Parser` (`export`), so an importer of
  `compiler::parser::parser` names the struct and compiles every block.
- The two virtual hooks are plain methods in `parser.cryo`; the base
  versions returning `null` are gone.  The constructor chain is one
  `static new(...)` returning a struct literal.
- `DirectiveRegistry` is a struct held by pointer as before.  It owns
  `records`, which its `delete` does not release; neither did the class's.
  `delete` runs no field drop glue for a struct or a class (a probe: an
  `[E]` field's elements are dropped at scope exit and not by `delete`).
  That is a defect in `delete`, logged; giving the registry a `Drop` would
  work around it in one type.
- `AsmOperand` and `AsmPiece` are value structs held in `[AsmOperand]` and
  `[AsmPiece]`, each implementing `Clone`; the cloner calls `.clone()` and
  then replaces the operand's expression with its own copy.  Every null
  check on an element went with the pointers (the parser never stored a
  null).
- **Measured.**  verify `--require-identical`: 0 test and 0 example
  objects moved.  Classes 109 -> 103: the prediction said 5, but its own
  list is 6.  Vtables in the compiler binary 287 -> 284 symbol lines: the
  three parser tables; the prediction said 4, but the registry and the asm
  types had no virtual methods and so no table.  Check time, interleaved
  x3 over the same source: 8.34 s base, 8.35 s new (medians); peak
  1528.9 / 1525.5 MiB; node count identical (1,936,462); the arena
  1,270 bytes larger, the asm operands and pieces now stored inline in
  arena-routed arrays rather than as separately allocated objects.  The
  binary is 30,892 bytes larger: `Array<AsmOperand>` and `Array<AsmPiece>`
  each emit 94 functions where the pointer arrays emitted 90, `clone`
  among them now that the element meets `Clone`, and each method copies a
  32- or 24-byte element where it copied a pointer.

### Slice 2: the walker trait over today's classes, and the visitors that use it

`Walk` is introduced with hooks typed over today's node classes
(`walk_binary(mut &this, n: BinaryExprNode*)`).  Its default `walk_expr`
dispatches by `match (e.kind)` plus a cast, the last place that does.

The substituter, dumper and name resolver move onto it one per commit, each
identical.  That retires their `accept` calls (66 + 2 + 76 of the 188) and
their `override visit`s.

This decouples the visitors from the node representation, so slices 4-7
change the trait's dispatch body and each node's `walk_children`, not
every visitor.

- **Prediction:** the substituter shrinks the most.  The name resolver's
  scope behaviour is unchanged, which the resolution corpus and the LSP
  harness check.
- **Wrong if:** a walk now visits a child it skipped before.  That shows
  up as a moved object or a new diagnostic in census, and it is a finding
  either way: the old visitor was missing a child, or the new default
  visits one it must not.

**Built.**

- `compiler::ast::walk` holds the trait: `walk_node` dispatches on
  `NodeKind` with a cast (`walk_expr`, `walk_stmt` and `walk_decl` forward
  to it), and there is one hook per node class, 65.  A hook's default is
  its kind's `walk_children`, a generic static in an `implement class`
  block in the same file, so the traversal of each kind is written once.
  The children are the tree as written, in source order; what a later pass
  hangs beside a node (`desugared_call`, `desugared`, `unwind_cleanup`,
  `synth_drops`) is not a child, and a walker that needs it visits it
  itself.  The six kinds no node is built with have empty arms, among them
  `NodeKind::EnumPattern`: an enum pattern is a `Pattern` whose
  `pattern_kind` is `Enum`, so the five comparisons against
  `NodeKind::EnumPattern` in the tree never hold (logged as a bug).
- The three visitors are structs implementing `Walk`, in one commit.  The
  conversion is mechanical so that the traversal cannot move: each
  `override visit(X)` became the hook for `X`; each `child.accept(this)`
  became a dispatching `this.walk_node(child)`; each direct
  `this.visit(child)` became a direct hook call; and then an override whose
  body was exactly its kind's default recursion was deleted.  The renames
  were made by a one-off codemod, `scripts/walk-codemod.py`, which also
  moved a type's hooks out of its body into its `Walk` implementation; it
  is in the commit that ran it (`1d9945cf`) and deleted in the next.
  - The substituter: 53 overrides -> 41 hooks, 22 of them continuing
    through `walk_children`.  It keeps its own recursion where it skips a child
    (a match arm's guard, a self-returning default's body) or visits one
    that is not a child (`desugared_call`).
  - The dumper: 68 -> 65 hooks.  Every hook prints its line and then its
    children with the "last child" flag the tree drawing needs, which a
    default walk does not carry, so none uses the default.  The three
    category fallbacks went: `walk_node` never reaches a category.
  - The name resolver: 59 -> 34 hooks; 25 were exactly the default.
- **Traversal agreement.**  The probe: every dispatch that reaches a node
  (an `accept` in the old compiler; an `accept` or a `walk_node` in the
  new) writes the node's kind, line and column to a trace, and the two
  compilers' traces are compared line by line over the corpus.
  - Corpus: the compiler, the 14 examples, the 265 test projects, and
    every unit-test and negative file built on its own: 826 inputs.
  - Result: 826 identical, 0 differing, 62,496,968 events in the old
    traces.  41 inputs traced nothing on either compiler; each stops
    before any visitor runs (a lexer or parser refusal, a missing module).
  - Controls: the old compiler against itself agrees (determinism); a
    compiler with `DoWhileStmtNode::walk_children` visiting the condition
    before the body differs from the old one at event 27,432 of
    `lang/control_flow.cryo`, where the new compiler agrees.
  - The dumper's output is the stronger check for it: `--ast` over the
    same 468 files is byte-identical old against new (198 MB of dumps).
- **Measured.**  verify `--require-identical`: 0 test and 0 example
  objects moved.  Check time interleaved x3 against the slice-1 compiler,
  over the same source: 7.95 s and 7.95 s (medians); peak 1552.2 and
  1551.8 MiB; node count identical (1,941,481); arena use 187,338 bytes
  lower, which the prediction did not include and which is not yet
  attributed.  Clean build: 0 warnings.  Classes 103 -> 100.

### Slice 3: sema, codegen and the cloner onto direct `match`

- Their `visit` overrides become methods called from the existing
  `match (x.kind)` dispatch.  The cloner gets `clone_expr`/`clone_stmt`/
  `clone_decl` returning the copy.
- `ASTVisitor`, `BaseASTVisitor` and every node's `accept` are deleted:
  69 `virtual` declarations (68 `visit`, 1 `accept`) and 482 `override`s
  (413 `visit`, 69 `accept`).  The AST classes now
  have no virtual methods, so no node carries a table pointer.
- **3b**, a separate commit: the side channels (`last_value`, the cloner's
  `result`, sema's expression return channel) become return values.
- **Proof:** identical; selfhost; time and memory.
- **Prediction:** each node shrinks by 8 bytes (the table pointer goes),
  visible in the arena bytes from slice 0.  Check time flat or down, since
  two indirect calls per node become a `switch`.
- **Wrong if:** arena bytes do not drop by about 8 x node count.

### Slice 4: the types

`Type` + 25 classes become `Type` + `TypeData` (section 2.5), one
`TypeData` variant group per commit where that keeps commits small.  The
three virtuals become `match` methods, and `Clone`/`Display` go in where
section 4 names them.

The language server's `TypeKind`/`ClassType` reads follow.  `ClassType`
stays as a variant.

- **Proof:** identical; selfhost; lsp-check.
- **Prediction:** the 904 type casts go to near zero; the type arena's size
  is unchanged within the header growth.

### Slice 5: patterns (the pilot for node conversion)

`PatternNode`/`EnumPatternNode` become `Pattern`/`PatternKind`.  It is
small (79 `PatternNode*` references, 6 arrays), so it converts as one
commit and proves the node shape, `Allocate` per struct, the factory
statics, `walk_children` and the language server's adaptation, before the
large categories.

`PatternSub.pattern: void*` becomes `Pattern*`.

- **Proof:** identical; selfhost; arena bytes and time against slice 3.
- **Prediction:** arena bytes up by the second allocation per pattern node,
  small in absolute terms.  The figure per node is extrapolated to the
  other categories here, before they start.
- **Wrong if:** the extrapolated cost for expressions exceeds what question
  1 accepted.  Then stop and ask.

### Slice 6: statements, declarations, expressions

Each category is a sequence of commits.  A category converts under a
**bridge**: the header class (`ExpressionNode`) stays a class for the
duration, gains the `kind: ExprKind` field, and `ExprKind` has a
`Legacy` variant for kinds not yet converted.

Converting a kind deletes its class, adds its payload struct and factory,
and rewrites its consumers from compare-and-cast to `match`, including the
language server's.  When the last kind converts, `Legacy` goes and the
header class becomes the `Expr` struct.  That last step needs every
`ExpressionNode*` renamed to `Expr*` at once, mechanically: a codemod
committed with the change and deleted in the next commit.

- **Statements** (15 kinds, 334 refs): probably two or three groups.
- **Declarations** (19 kinds): the riskiest for identity.  `DeclarationIndex`
  stores `ASTNode*`, mono stores `FunctionDeclNode*`/`MethodNode*`, and
  `ImplBlockNode.trait_item_methods` is read by the trait-slot code.  Groups:
  the type declarations (struct, union, class, enum, variant, trait,
  alias, field, generic param); functions and methods (the identity-heavy
  ones); then imports, modules, extern, intrinsics, variables and
  destructuring.
- **Expressions** (25 kinds, 849 refs, 2,266 casts mostly here): four to six
  groups, smallest first.  `async_lower.cryo` is converted in the group
  that contains `Await`, `Yield` and `Lambda` plus the statement kinds it
  restructures, and gets its own commit.
- **`Program` and `Node`** last: `[ASTNode*]` -> `[Node]`, and `NodeKind`
  deleted with its 6 dead entries.
- **Proof per commit:** identical, test-census, lsp-check.  **Per
  category:** selfhost, time, memory and the warning set.
- **Prediction per commit:** the kind's casts and `NodeKind` references go
  to zero (grep is the control: run it on a kind already converted).
  Objects identical.
- **Wrong if:** a `match` refused for non-exhaustiveness where the old
  chain had a silent default.  That is a finding to record (which kinds
  fell through), not something to silence with `_`.

### Slice 7: the remaining walks and traits

- The ad-hoc walks that are better as `Walk` implementations.
- `node_locator` onto `Walk`.
- The `Clone`/`Display`/`Eq` adoptions not already done.
- The cloner checked for fields it never copied.  A field a clone drops is
  a defect today; give each one a test.
- **Proof:** identical; selfhost.

### Slice 8: borrowed trait objects

- **8a. Front end.**  `virtual` in type position, object safety, coercion,
  calls, with the new codes (question 10).  Tests: unit, negative files
  with `//~` annotations, a project.  Each test is shown to fail on the
  slice-7 compiler.
- **8b. Back end.**  `codegen_vtable_for_class` generalized to emit a
  table from a list of functions; the trait table built from item
  positions; the dispatch sequence shared; the pair layout in
  `docs/abi.md`; the table symbol in `docs/cryo-mangling-spec.md` and the
  demangler, with its test (ruling 359).  The class path keeps working
  through the generalized emitter until slice 9.
- **8c. Dogfood**, after a re-pin: the yield hook (question 11).
- **Proof:** census with the new tests; verify declaring in
  `tests/started-compiling` the programs E0313 refused that now compile;
  every other object identical; selfhost; the gate pairs below.
- **Gate pairs**, each shown on a broken tree, each refusal shown with the
  trait-object path and absent with the class path's old behaviour:
  - Delete a method from an implementation: the class path compiled the
    equivalent silently; E0312 refuses.
  - Swap two trait methods' positions in the table builder: the
    same-name/two-traits test fails.

### Slice 9: delete the class feature

- **Refused syntax.**  `type class`, `virtual` and `override` as member
  modifiers, `protected:`, base lists, base-constructor initializers, the
  `class` tag on implement heads, and constructors/destructors (if question
  6 says so).  Each is refused where it is written with a targeted message
  (question 7), each with a negative test.
- **Code removed.**  Section 1.6 in full, the language server's class
  handling included.  Retired codes keep their numbers reserved.
- **Tests.**  The class-feature tests are deleted and the incidental ones
  ported to structs (question 5).  The roster golden is re-pinned in the
  same commit, with the count named in the message.
- **Proof:** verify with declared moves: each deleted test is gone from
  both sides, and each ported test's objects are listed as moved by the
  port, not by the compiler.  Selfhost; lsp-check; cross-check.
- **Gate pair:** the old compiler accepts `type class A {}`; the new one
  refuses it with the new code.

### Slice 10: docs (in each slice's own commit, listed here so nothing is missed)

- `docs/cryo.md`: section 9 removed; casts, `new`/`delete`, `Allocate`,
  layout, keywords, the directive table and async updated; trait objects
  written into section 11.
- `docs/grammar.md`, `docs/abi.md`, `docs/cryo-mangling-spec.md`.
- `docs/resolution-rules.md`: the door table's class field door, and
  rulings superseding 249 (inheritance clause), 253, 258, 291, 294, 295
  and 303.
- `docs/roadmap.md`: the class-removal and trait-object items marked
  built, with what is left (owning boxes, upcasting, generic
  implementations as objects, supertrait reach).
- The language server's keyword hover text and `tools/CryoAnalyzer`'s
  grammar.
- This plan is deleted when slice 9 lands.

### The riskiest slices

1. **Slice 6, declarations**: node identity in `DeclarationIndex`, mono and
   the trait-slot tables, and the language server's identity handlers.
2. **Slice 6, the async lowering's group**: 7,157 lines, 383 `NodeKind`
   references, restructuring trees in place.
3. **Slice 8b**: the generalized emitter is shared with the class path
   until slice 9, and a miscompile of dispatch that reproduces itself is
   invisible to selfhost.  The trait-object tests must run programs, not
   only compile them.
4. **Slice 2**: a default traversal that visits a child the old visitor
   skipped (or the reverse) changes resolution or substitution silently
   where no test looks.
5. **Memory**: the second allocation per node, measured in slice 5 before
   it is paid across the tree.

---

## 7. Risks and open questions

- **Bridge cost.**  The `Legacy` bridge in slice 6 means the header class
  outlives most of the conversion, and every consumer handles two shapes
  of one category for a while.  The alternative is a flag day per
  category, which is the expression category's ~2,000 sites in one commit.
  Question 4.
- **A `match` without `_` is exhaustive, and the old chains were not.**
  Conversions will find kinds that fell through to a default.  Each is
  recorded and decided (a real arm or an explicit refusal), not papered
  over with `_`.
- **Incremental builds** change their cache keys with the node layouts;
  the incremental gates run in verify each slice.  A struct layout change
  read from cache is a known segfault shape: `rm -rf compiler/build` before
  every measurement.
- **Enum renumbering**: deleting `NodeKind` and `TypeKind::Class` shifts
  enums.  Clean rebuilds.
- **Trait default instances for the table**: a default method reached only
  through an object must be instantiated for the implementing type in the
  implementation's module, which is a monomorphization root that does not
  exist today.  The roadmap has this open for generic implementations; for
  non-generic ones it is in slice 8b's scope, and it needs an incremental
  test (`incremental-instance-check` shape).
- **Supertrait methods on an object** are open in the roadmap (direct or
  through an upcast).  Not decided here; question 12.
- **Generic struct `Allocate`** (bug 0001's shape for structs) is
  unmeasured.  Every node struct in this plan is non-generic, so it does
  not block; it is measured in slice 9.
- **`cryo check` time** is the editor's latency.  A regression there is
  felt directly, so it is measured every slice, not only at the end.

---

## 8. Size

In slices, with honest ranges:

| slice | commits | confidence |
|---|---|---|
| 0 instruments | 1 | high |
| 1 parser + small classes | 1-2 | high |
| 2 walker over classes + 3 visitors | 3-4 | medium (the name resolver) |
| 3 sema, codegen, cloner; side channels | 3-5 | medium |
| 4 types | 2-4 | medium |
| 5 patterns | 1 | high |
| 6 statements | 2-3 | medium |
| 6 declarations | 3-5 | low (identity) |
| 6 expressions incl. async lowering | 5-8 | low (size) |
| 6 `Program`/`Node`/`NodeKind` | 1-2 | medium |
| 7 walks and traits | 2-3 | medium |
| 8 trait objects | 3-5 | low (new codegen, defaults as roots) |
| 9 deletion | 2-3 | medium |

That is about 30-45 commits.  In sessions of the size the recent ones have
been, it is roughly 12-25 sessions.  The spread is mostly slice 6 and
slice 8.  The low end assumes the bridge works as drawn and the async
lowering converts mechanically; the high end assumes each declaration
group finds an identity defect, as the naming migration's groups did.

---

## 9. Decisions

**Answered by Jake** (rulings 382-394 in `docs/resolution-rules.md`):

| # | answer |
|---|---|
| 1 | A: a header struct with a `kind` enum of per-kind struct pointers (382) |
| 2 | A: one small `Allocate` implementation per node struct (383) |
| 3 | B: folded in, after the compiler is class-free and before the class feature is deleted, reusing the class vtable code (384) |
| 4 | A for declarations and expressions, B for patterns and statements; the bridge is deleted at the end of each category and tracked in section 10 until it is (385) |
| 5 | A: class-feature tests deleted, incidental users ported to structs (386) |
| 6 | A: removed with classes, after measuring their use (387) |
| 7 | A: one new code, a help line per form; the code and wording are proposed in a report first (388) |
| 8 | A: a separate commit after the dispatch change (389) |
| 9 | B, as a follow-up after slice 4 (390) |
| 10 | the implementing agent's call, reported (391) |
| 11 | A: the yield hook becomes `virtual YieldHook*` (392) |
| 12 | A: supertrait methods callable directly on the object (393) |
| 13 | A: `protected` removed (394) |

The two bugs the plan found on the way are fixed first, since the arena and
the conversion lean on `delete` and drop: `![caller_location]` on a variadic
function is refused (395), and `delete` releases the fields the pointee
owns (396).

The options as they were put:

1. **Node shape.**
   - A) A header struct owning a `kind` enum of payload pointers
     (`Expr { span, ..., kind: ExprKind }`).  This is section 2.1.
   - B) A bare enum value as the handle (`Expr` = `ExprKind`), with the
     header inside each payload, `e.span()` as a method, and `Expr?` for
     absent children.

   Lean A.  It keeps identity, nullability and the header reads, and costs
   one more allocation per node, measured in slice 5.
2. **`Allocate` for ~75 node structs.**
   - A) One implementation per struct (explicit, and it exercises the
     trait 75 times).
   - B) The factories allocate from the arena directly and the node
     structs implement nothing (fewer lines, but against ruling 368).

   Lean A.
3. **When trait objects land.**
   - A) Before the AST conversion.
   - B) Folded in, after the compiler is class-free and before the class
     feature is deleted.
   - C) After the deletion.

   Lean B.
4. **How a category converts.**
   - A) The `Legacy` bridge, kind by kind (small commits, two shapes for a
     while).
   - B) A flag day per category.

   Lean A for declarations and expressions, B for patterns and
   statements.
5. **Class tests.**
   - A) Delete the class-feature tests (section 1.5) and port the
     incidental ones to structs.
   - B) Port everything that can be ported.

   Lean A.  Deleting tests is yours to approve.
6. **Constructors and destructors in struct bodies** (`Name(...) {}`,
   `~Name()`, accepted today).
   - A) Removed with classes; structs use `static new` and `Drop`.
   - B) Kept.

   I lean A, but it changes what existing programs compile, so first I
   would measure how many tests and examples use them.
7. **Refusing the removed syntax.**
   - A) One new error code for "classes were removed", with a help line per
     form (`type class` -> `type struct` plus a trait; `virtual`/`override`
     -> a trait implementation).
   - B) A code per form.

   Lean A.  The number and the wording are yours.
8. **Side channels to return values** (codegen's `last_value`, the cloner's
   `result`, sema's return channel).
   - A) A separate slice right after the dispatch change.
   - B) Folded into the dispatch change.

   Lean A.
9. **`TypeKind` after the type conversion.**
   - A) Keep it as a stored tag beside `TypeData` (no churn in 1,053
     compares).
   - B) Derive it from `TypeData` with a method.

   Lean B as a follow-up, after slice 4 is identical with A.
10. **New names for trait objects.**  These are the code for "method not
    available on a `virtual` object", the code for a failed coercion, and
    the table's mangled symbol (the class one is `C$vt$<path>`; it needs
    the trait).

    No lean: these are yours.
11. **Dogfood the yield hook as `virtual YieldHook*`.**
    - A) Yes, after a re-pin.
    - B) No; the compiler uses no trait object at v1.0.

    Lean A.  It is the one genuine open boundary in the tree.
12. **Supertrait methods on a subtrait object** (open in the roadmap).
    - A) Callable directly: the subtrait's table holds the supertrait's
      slots too.
    - B) Only after an upcast, which comes after v1.0.

    Lean A, but the roadmap leaves it open, so it is yours.
13. **`protected`.**
    - A) Removed with classes, since it has no meaning without inheritance.
    - B) Kept as a reserved word.

    Lean A.

---

## 10. Bridge pieces still standing

Every piece of code that exists only to carry the tree from one shape to the
next is listed here when it is written and struck when it is deleted.  The
final audit greps for each name listed and expects none.

| piece | where | written in | deleted when |
|---|---|---|---|
| (none yet) | | | |
