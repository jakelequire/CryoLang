(* ================================================================ *)
(*  Cryo Language Grammar  -  W3C EBNF                              *)
(*                                                                  *)
(*  This file is the formal counterpart to `docs/cryo.md`.  When    *)
(*  the two disagree, the parser at                                 *)
(*  `compiler/src/compiler/parser/` is the source of truth.         *)
(* ================================================================ *)


(*  Program & Statements ======================================= *)

Program            ::= Directive* Namespace? TopLevelItem*
Namespace          ::= "namespace" QualName ";"

TopLevelItem       ::= Import | ModuleDecl | VarDecl | FunctionDecl
                     | ExternDecl | CHeaderImport | IntrinsicDecl
                     | AggregateDecl | UnionDecl | EnumDecl | TraitDecl
                     | TypeAlias | ImplBlock | StaticAssert

Statement          ::= VarDecl | FunctionDecl
                     | AggregateDecl | EnumDecl | TraitDecl
                     | TypeAlias | ImplBlock
                     | If | While | For | "loop" Block | DoWhile
                     | Match | Switch | StaticMatch
                     | "break" ";" | "continue" ";" | Return
                     | "unsafe" Block | AsmBlock | Block | Expr ";"

Block              ::= "{" Statement* "}"
Return             ::= "return" Expr? ";"

(*  Inline assembly.  The block body is raw target assembly (no string
    quoting); a mandatory `![arch(<arch>, <dialect>)]` directive above it
    selects the arch (gating) + dialect, and an optional `![clobber(...)]`
    lists clobbered registers.  `${ ... }` holes bind Cryo operands; bare
    `{` / `}` are literal assembly text (e.g. AVX-512 masks `{k1}`).       *)
AsmBlock           ::= Directive* "asm" "{" AsmBody "}"
AsmBody            ::= (AsmText | AsmOperand)*
AsmOperand         ::= "$" "{" ("=" | "+")? Expr (":" AsmConstraint)? "}"
AsmConstraint      ::= StringLit | "m" | "i" | "r"


(*  Modules, Imports, Directives =============================== *)

ModuleDecl         ::= "public"? "module" ModulePath ";"
Import             ::= "import" ImportForm ";"
ImportForm         ::= ModulePath "::" "*"
                     | ModulePath "::" "{" Ident ("," Ident)* "}"
                     | ModulePath "as" Ident
                     | ModulePath
ModulePath         ::= Ident ("::" Ident)*
QualName           ::= Ident ("::" Ident)*

(*  Directives use the bang-bracket form `![name(...)]`.  The leading
    `!` is part of a single `![` token produced by the lexer, so no
    whitespace is permitted between `!` and `[`.                       *)
Directive          ::= "!" "[" Ident DirectiveArgs? "]"
DirectiveArgs      ::= "(" (DirectiveArg ("," DirectiveArg)*)? ")"
DirectiveArg       ::= StringLit | Ident | NumLit | BoolLit
                     | Ident "=" DirectiveArg
                     | Ident "(" (DirectiveArg ("," DirectiveArg)*)? ")"


(*  Declarations =============================================== *)

VarDecl            ::= ("const" | "mut") Ident ":" Type ("=" Expr)? ";"
                     | ("const" | "mut") StructDestructure ":" Type "=" Expr ";"
(*  Struct-destructuring binding: moves each named field of a struct value
    into a like-named local.  Idiomatic with a by-value `this` receiver to
    move fields out of a consumed value (see cryo.md S8.3).                *)
StructDestructure  ::= "{" Ident ("," Ident)* "}"

FunctionDecl       ::= Visibility? "function" Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)? Block

ExternDecl         ::= "extern" "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"
                     | "extern" StringLit "{" ExternFn* "}"
ExternFn           ::= "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"

(*  C-header import - a named-namespace form of `extern "C"` that
    asks the compiler to invoke clang on the listed headers and pull
    the resulting declarations into the named namespace.            *)
CHeaderImport      ::= "extern" "module" Ident ":=" StringLit
                       "{" CIncludeLine+ "}"
CIncludeLine       ::= "#include" ( "<" /* path */ ">"
                                  | StringLit )

IntrinsicDecl      ::= "intrinsic" "function" Ident
                       "(" ParamList? ")" ("->" Type)? (Block | ";")
                     | "intrinsic" "const" Ident ":" Type ("=" Expr)? ";"

(*  Compile-time assertion: `cond` is folded after layout; a false or
    non-constant condition is a compile error (E0237).  `cond` may use
    integer/boolean literals, `sizeof`/`alignof`, and arithmetic /
    comparison / logical / bitwise operators.                          *)
StaticAssert       ::= "static_assert" "(" Expr ("," StringLit)? ")" ";"

ParamList          ::= Param ("," Param)* ("," VariadicParam)?
                     | VariadicParam
Param              ::= Ident ":" Type
                     | "&this" | "mut" "&this"    (* borrowing receiver *)
                     | "this"  | "mut" "this"     (* by-value (consuming) receiver *)
VariadicParam      ::= Ident ":" Type "..."

WhereClause        ::= Ident ":" TraitBound ("+" TraitBound)*
                       ("," Ident ":" TraitBound ("+" TraitBound)*)*
                       (* TraitBound is a possibly-qualified trait name with
                          optional generic arguments - same shape as
                          super-trait references on a `type trait` decl. *)
Visibility         ::= "public" | "private" | "protected"


(*  Aggregates (struct/class), Enums, Traits, Type Aliases ===== *)

(* Structs and classes share a single rule; the keyword and a few
   class-only members (constructors, destructors, virtual/override)
   are the only differences.  The leading `type` keyword is the
   canonical form (`type struct Foo { ... }`); the bare `struct Foo`
   form is also accepted.                                            *)

AggregateDecl      ::= "type"? AggregateKind Ident Generics?
                       (":" Ident)?                  (* base class  *)
                       "{" Member* "}"
AggregateKind      ::= "struct" | "class"

Member             ::= Visibility? "static"?
                       (Field | Method | Constructor | Destructor)
                     | VisibilityBlock          (* `public:` / `private:` / `protected:`
                                                   labels a run of subsequent members. *)
VisibilityBlock    ::= Visibility ":" (Field | Method | Constructor | Destructor)*

Field              ::= Ident ":" Type ("=" Expr)? ";"
Method             ::= ("virtual" | "override")? Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)?
                       (Block | ";")
Constructor        ::= Ident "(" ParamList? ")"
                       (":" Ident "(" ArgList? ")")? Block
Destructor         ::= "~" Ident "(" ")" Block

EnumDecl           ::= "type"? "enum" Ident Generics? (":" Type)?
                       "{" (EnumVariant ("," | ";"))* "}"
                       (* the optional ":" Type is the explicit discriminant
                          base type, e.g. `type enum Foo : i32 { ... }` *)
EnumVariant        ::= Ident
                     | Ident "=" NumLit
                     | Ident "(" Type ("," Type)* ")"

UnionDecl          ::= "type" "union" Ident Generics?
                       "{" Member* "}"
                       (* An untagged C-style union: every field overlaps at
                          offset 0, so the union's size is its largest member
                          and its alignment its most-aligned member.  Shares
                          the aggregate Member grammar (fields + methods,
                          including static methods).  Honours `![repr(c)]` /
                          `![align(N)]`.  A union literal initialises EXACTLY
                          one field (`Value { i: 1 }`); members are accessed as
                          `u.field`.  There is no discriminant and it is not
                          matched variant-wise - use `type enum` for a tagged
                          (discriminated) union.  Reading a member other than
                          the one last written is the programmer's
                          responsibility.  Unlike `struct`/`class` there is no
                          bare `union Foo` form; the leading `type` is
                          required. *)

TraitDecl          ::= "type" "trait" Ident Generics?
                       (":" TraitBound ("," TraitBound)*)?  (* super-traits *)
                       "{" TraitMember* "}"
TraitBound         ::= Ident GenericArgs?
TraitMember        ::= Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)?
                       (Block | ";")     (* body = default impl    *)

TypeAlias          ::= "type" Ident Generics? "=" Type ";"


(*  Implementation Blocks & Generics =========================== *)

(*  Two shapes: inherent impl (no `trait ... for`) and trait impl
    (with `trait ... for`).  Both may carry leading `<T, ...>`
    generic parameters on the impl head, an optional kind tag
    (`struct`/`enum`/`class`) on the target, and generic arguments
    on the target type.                                              *)

ImplBlock          ::= "implement" Generics?
                       ( "trait" Type "for" )?
                       ("enum" | "struct" | "union" | "class")?
                       TargetType
                       "{" MethodImpl* "}"
TargetType         ::= QualName GenericArgs?
                     | Primitive
                     | "()"                              (* unit type *)

MethodImpl         ::= ("virtual" | "override")? "static"?
                       Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)? Block

Generics           ::= "<" GenericParam ("," GenericParam)* ">"
GenericParam       ::= Ident (":" Ident ("+" Ident)*)? ("=" Type)?
                       (* the optional "=" Type is a default type argument,
                          e.g. `<A = GlobalAlloc>` *)
GenericArgs        ::= "<" Type ("," Type)* ">"


(*  Expressions ================================================ *)

(* Operators and their precedence/associativity are summarised below. *)

Expr               ::= Assign
Assign             ::= Coalesce (AssignOp Assign)?
AssignOp           ::= "=" | "+=" | "-=" | "*=" | "/=" | "%="
                     | "&=" | "|=" | "^=" | "<<=" | ">>="
Coalesce           ::= Pipe ("??" Coalesce)?            (* null-coalescing, right-assoc *)
Pipe               ::= Conditional (("|>" | "<|") Conditional)*   (* pipeline, left-assoc *)

Conditional        ::= BinaryExpr ("?" Expr ":" Conditional)?
BinaryExpr         ::= UnaryExpr (BinOp UnaryExpr)*
BinOp              ::= "||" | "&&" | "|" | "^" | "&"
                     | "==" | "!=" | "<" | ">" | "<=" | ">=" | "<=>"
                     | "<<" | ">>" | "+" | "-" | "*" | "/" | "%"
                     | ".." | "..="     (* range constructors: a..b, a..=b *)

UnaryExpr          ::= UnaryOp UnaryExpr
                     | PostfixExpr ("as" Type)*
UnaryOp            ::= "-" | "!" | "&" | "*" | "~" | "++" | "--"

PostfixExpr        ::= Primary PostfixOp*
PostfixOp          ::= "(" ArgList? ")"
                     | "[" Expr "]"
                     | "." Ident GenericArgs?    (* `.` auto-derefs pointers; there is no `->` operator *)
                     | "::" Ident GenericArgs?
                     | "?"                              (* error propagation / try *)
                     | "++" | "--"

ArgList            ::= Expr ("," Expr)*

Primary            ::= Literal
                     | "null" | "this"
                     | Ident
                     | QualName
                     | Ident GenericArgs ("(" ArgList? ")")?
                     | QualName GenericArgs ("(" ArgList? ")")?
                     | StructLit
                     | ArrayLit
                     | NewExpr
                     | "sizeof"  "(" Type ")"
                     | "alignof" "(" Type ")"
                     | "typeof"  "(" Expr ")"
                     | IfExpr
                     | Match
                     | StaticMatch
                     | Lambda
                     | "delete" Expr           (* parsed; see cryo.md section 21 *)
                     | "await"  Expr           (* parsed; no async semantics yet *)
                     | "yield"  Expr?          (* parsed; no generator semantics yet *)
                     | "(" Expr ")"

Lambda             ::= "move"? "(" (LambdaParam ("," LambdaParam)*)? ")" "->" Type
                       Block                     (* function literal; captures
                                                    its environment by value-copy
                                                    for Copy types and by move
                                                    otherwise.  `move` makes the
                                                    move-capture explicit. *)
LambdaParam        ::= Ident (":" Type)?

StructLit          ::= Ident GenericArgs?
                       "{" Ident ":" Expr ("," Ident ":" Expr)* ","? "}"
ArrayLit           ::= "[" (Expr ("," Expr)*)? "]"
                     | "[" Expr ";" Expr "]"
NewExpr            ::= "new" Type ("(" ArgList? ")")?
                     | "new" Type "[" Expr "]"
                     | "new" Type "{" Ident ":" Expr
                                  ("," Ident ":" Expr)* ","? "}"
IfExpr             ::= "if" Cond "{" Expr "}" "else" "{" Expr "}"


(*  Control Flow =============================================== *)

(* Cond: the `()` around a condition are OPTIONAL.  With parens, any
   expression is allowed.  Without parens, a *bare* struct literal is
   suppressed so the following `{` reads as the body - wrap a struct-valued
   condition in `()` (or nest it inside a call) to use one. *)
Cond               ::= "(" Expr ")" | Expr

If                 ::= "if" Cond Block
                       ("else" "if" Cond Block)*
                       ("else" Block)?
While              ::= "while" Cond Block
DoWhile            ::= "do" Block "while" Cond ";"
For                ::= "for" "("? ForInit Expr ";" Expr ")"? Block   (* C-style *)
                     | "for" "("? Ident "in" Expr ")"? Block         (* for-in over an iterable *)
                       (* the `(` and `)` are optional but must be balanced *)
ForInit            ::= VarDecl | Ident ":" Type ("=" Expr)? ";"

Match              ::= "match" Cond "{" MatchArm* "}"
MatchArm           ::= Pattern ("|" Pattern)* Guard? "=>" (Block | Expr ","?)
Guard              ::= "if" Cond   (* checked after the pattern matches *)

Switch             ::= "switch" Cond "{" CaseClause* "}"
CaseClause         ::= ("case" Expr | "default") ":" Statement*

StaticMatch        ::= "static" "match" "(" Type ")" "{" StaticMatchArm* "}"
                       (* compile-time type dispatch; usable in statement or
                          expression position inside a generic body *)
StaticMatchArm     ::= (Type ("|" Type)* | "_") "=>" (Block | Expr)
                       (* one or more `|`-separated types share the arm's body;
                          `_` is the wildcard default *)


(*  Patterns =================================================== *)

(* Or-patterns are written at the arm level (see MatchArm) - a
   single Pattern node never contains `|` itself.                  *)

Pattern            ::= "_"
                     | Literal
                     | Ident                              (* binding *)
                     | QualName ("(" PatElem ("," PatElem)* ")")?  (* enum *)
                     | RangeBound ".." RangeBound         (* range, both bounds
                                                             same kind: char or
                                                             integer literal *)
RangeBound         ::= CharLit | "-"? IntLit
(* A PatElem may itself be a (qualified) enum pattern, giving nested
   destructuring: `Some(Some(n))`, `Branch(Leaf(x), r)`.                 *)
PatElem            ::= "_" | "mut"? Ident | Literal
                     | QualName ("(" PatElem ("," PatElem)* ")")?  (* nested *)


(*  Types & Literals =========================================== *)

Type               ::= BaseType "*"+                       (* pointer *)
                     | "&" "mut"? Type                     (* reference *)
                     | "mut" "&" Type                      (* mut-ref alt form *)
                     | BaseType ("[" NumLit? "]")+         (* array *)
                     | "(" Type ("," Type)* ","? ")"       (* tuple - needs >=2 elements
                                                              OR a trailing comma; `(T)`
                                                              is grouping, not a 1-tuple *)
                     | "(" (Type ("," Type)*)? ")" "->" Type  (* fn (the `->` disambiguates) *)
                     | "()"                                (* unit (also the empty tuple) *)
                     | Type "?"                            (* optional: T? desugars to Option<T> *)
                     | "implement" QualName GenericArgs?   (* opaque: implement Trait (see cryo.md section 2.11) *)
                     | BaseType
BaseType           ::= Primitive | "This" | QualName GenericArgs?
Primitive          ::= "void" | "boolean" | "char" | "string"
                     | "int"  | "i8" | "i16" | "i32" | "i64" | "i128"
                     | "uint" | "u8" | "u16" | "u32" | "u64" | "u128"
                     | "float" | "f32" | "f64" | "double"
                     | "usize" | "isize"

Literal            ::= NumLit | StringLit | CharLit | BoolLit
NumLit             ::= IntLit | FloatLit
IntLit             ::= (DecLit | HexLit | BinLit | OctLit) TypeSuffix?
DecLit             ::= /* [0-9][0-9_]*                          */
HexLit             ::= /* 0[xX][0-9a-fA-F][0-9a-fA-F_]*         */
BinLit             ::= /* 0[bB][01][01_]*                       */
OctLit             ::= /* 0[oO][0-7][0-7_]*                     */
FloatLit           ::= /* [0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9]+)? */
                       FloatSuffix?
TypeSuffix         ::= "u8" | "u16" | "u32" | "u64"
                     | "i8" | "i16" | "i32" | "i64"
                     | "usize" | "isize" | FloatSuffix
FloatSuffix        ::= "f32" | "f64"
StringLit          ::= /* "..."  with implemented escapes; an f-prefix  */
                       /* (f"...{expr}...") is an interpolated string    */
                       /* literal - see cryo.md section 1.4.                    */
CharLit            ::= /* '.'    with implemented escapes           */
BoolLit            ::= "true" | "false"
Ident              ::= /* [a-zA-Z_][a-zA-Z0-9_]*                */


(*   Operator Precedence  (lowest to highest) ==================    *)
(*   Mirrors cryo.md section 5.7; the parser is the source of truth.      *)
(*                                                                  *)
(*    1   =  +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=     right      *)
(*    2   ??   (null-coalescing)                         right      *)
(*    3   |>  <|   (pipeline)                            left       *)
(*    4   ?:   (ternary)                                 right      *)
(*    5   ..  ..=   (range)                              left       *)
(*    6   ||                                             left       *)
(*    7   &&                                             left       *)
(*    8   |                                              left       *)
(*    9   ^                                              left       *)
(*   10   &                                              left       *)
(*   11   ==  !=                                         left       *)
(*   12   <  >  <=  >=  <=>                              left       *)
(*   13   <<  >>                                         left       *)
(*   14   +  -                                           left       *)
(*   15   *  /  %                                        left       *)
(*   16   as                                             left       *)
(*   17   -  !  &  *  ~  ++  --  (prefix)  new  delete   right      *)
(*   18   ()  []  .  ->  ::  ?  ++  --  (postfix)        left       *)


(*   Reserved Keywords =========================================    *)
(*                                                                  *)
(*  These names are reserved by the lexer and may not be used as    *)
(*  identifiers.  Some (e.g. `from`, `async`, `await`, `yield`)     *)
(*  are lexed but not yet wired into the parser - see section 21 of       *)
(*  `cryo.md` for the reserved-syntax table.                        *)
(*                                                                  *)
(*  alignof    as         asm        async     auto      await     *)
(*  boolean    break      case      char      class     const     *)
(*  continue   default    delete    do        double    else      *)
(*  enum       export     extern    f32       f64       false     *)
(*  float      for        from      function  i8        i16       *)
(*  i32        i64        i128      if        implement import    *)
(*  in         inline     int       intrinsic loop      match     *)
(*  module     move       mut       namespace new       null      *)
(*  optional   override   private   protected public    return    *)
(*  sizeof     static     string    struct    switch    this      *)
(*  This       trait      true      tuple     type      typeof    *)
(*  u8         u16        u32       u64       u128      uint      *)
(*  union      unsafe     unsigned  virtual   void      where     *)
(*  while      with       yield                                   *)
