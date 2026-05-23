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
                     | AggregateDecl | EnumDecl | TraitDecl
                     | TypeAlias | ImplBlock

Statement          ::= VarDecl | FunctionDecl
                     | AggregateDecl | EnumDecl | TraitDecl
                     | TypeAlias | ImplBlock
                     | If | While | For | "loop" Block | DoWhile
                     | Match | Switch
                     | "break" ";" | "continue" ";" | Return
                     | "unsafe" Block | Block | Expr ";"

Block              ::= "{" Statement* "}"
Return             ::= "return" Expr? ";"


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

FunctionDecl       ::= Visibility? "function" Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)? Block

ExternDecl         ::= "extern" "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"
                     | "extern" StringLit "{" ExternFn* "}"
ExternFn           ::= "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"

(*  C-header import — a named-namespace form of `extern "C"` that
    asks the compiler to invoke clang on the listed headers and pull
    the resulting declarations into the named namespace.            *)
CHeaderImport      ::= Ident ":=" "extern" StringLit
                       "{" CIncludeLine+ "}"
CIncludeLine       ::= "#include" ( "<" /* path */ ">"
                                  | StringLit )

IntrinsicDecl      ::= "intrinsic" "function" Ident
                       "(" ParamList? ")" ("->" Type)? (Block | ";")
                     | "intrinsic" "const" Ident ":" Type ("=" Expr)? ";"

ParamList          ::= Param ("," Param)* ("," VariadicParam)?
                     | VariadicParam
Param              ::= Ident ":" Type | "&this" | "mut" "&this"
VariadicParam      ::= Ident ":" Type "..."

WhereClause        ::= Ident ":" Ident ("+" Ident)*
                       ("," Ident ":" Ident ("+" Ident)*)*
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
                       ("enum" | "struct" | "class")?
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

UnaryExpr          ::= UnaryOp UnaryExpr
                     | PostfixExpr ("as" Type)*
UnaryOp            ::= "-" | "!" | "&" | "*" | "~" | "++" | "--"

PostfixExpr        ::= Primary PostfixOp*
PostfixOp          ::= "(" ArgList? ")"
                     | "[" Expr "]"
                     | ("." | "->") Ident GenericArgs?
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
                     | Lambda
                     | "(" Expr ")"

Lambda             ::= "(" (LambdaParam ("," LambdaParam)*)? ")" "->" Type
                       (Block | Expr)            (* non-capturing function literal *)
LambdaParam        ::= Ident (":" Type)?

StructLit          ::= Ident GenericArgs?
                       "{" Ident ":" Expr ("," Ident ":" Expr)* ","? "}"
ArrayLit           ::= "[" (Expr ("," Expr)*)? "]"
                     | "[" Expr ";" Expr "]"
NewExpr            ::= "new" Type ("(" ArgList? ")")?
                     | "new" Type "[" Expr "]"
                     | "new" Type "{" Ident ":" Expr
                                  ("," Ident ":" Expr)* ","? "}"
IfExpr             ::= "if" "(" Expr ")" "{" Expr "}" "else" "{" Expr "}"


(*  Control Flow =============================================== *)

If                 ::= "if" "(" Expr ")" Block
                       ("else" "if" "(" Expr ")" Block)*
                       ("else" Block)?
While              ::= "while" "(" Expr ")" Block
DoWhile            ::= "do" Block "while" "(" Expr ")" ";"
For                ::= "for" "(" ForInit Expr ";" Expr ")" Block
ForInit            ::= VarDecl | Ident ":" Type ("=" Expr)? ";"

Match              ::= "match" "(" Expr ")" "{" MatchArm* "}"
                       (* the parentheses around the subject are required *)
MatchArm           ::= Pattern ("|" Pattern)* "=>" (Block | Expr ","?)

Switch             ::= "switch" "(" Expr ")" "{" CaseClause* "}"
CaseClause         ::= ("case" Expr | "default") ":" Statement*


(*  Patterns =================================================== *)

(* Or-patterns are written at the arm level (see MatchArm) — a
   single Pattern node never contains `|` itself.                  *)

Pattern            ::= "_"
                     | Literal
                     | Ident                              (* binding *)
                     | QualName ("(" PatElem ("," PatElem)* ")")?  (* enum *)
                     | Literal ".." Literal               (* range  *)
PatElem            ::= "_" | Ident | Literal | "mut" Ident


(*  Types & Literals =========================================== *)

Type               ::= BaseType "*"+                       (* pointer *)
                     | "&" "mut"? Type                     (* reference *)
                     | "mut" "&" Type                      (* mut-ref alt form *)
                     | BaseType ("[" NumLit? "]")+         (* array *)
                     | "[" Type ("," Type)* "]"            (* tuple *)
                     | "(" (Type ("," Type)*)? ")" "->" Type  (* fn *)
                     | "()"                                (* unit *)
                     | Type "?"                            (* optional: T? desugars to Option<T> *)
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
                       TypeSuffix?
TypeSuffix         ::= "u8" | "u16" | "u32" | "u64"
                     | "i8" | "i16" | "i32" | "i64"
                     | "f32" | "f64" | "usize" | "isize"
StringLit          ::= /* "..."  with standard escapes           */
CharLit            ::= /* '.'    with standard escapes           */
BoolLit            ::= "true" | "false"
Ident              ::= /* [a-zA-Z_][a-zA-Z0-9_]*                */


(*   Operator Precedence  (lowest to highest) ==================    *)
(*                                                                  *)
(*    1   = += -= *= /= &= |=               right                   *)
(*    2   ? :                                right                  *)
(*    3   ||                                 left                   *)
(*    4   &&                                 left                   *)
(*    5   |                                  left                   *)
(*    6   ^                                  left                   *)
(*    7   &                                  left                   *)
(*    8   == !=                              left                   *)
(*    9   < > <= >= <=>                      left                   *)
(*   10   << >>                              left                   *)
(*   11   + -                                left                   *)
(*   12   * / %                              left                   *)
(*   13   as                                 left                   *)
(*   14   - ! & * ~ ++ -- (prefix)           right                  *)
(*   15   () [] . -> :: ++ -- (postfix)      left                   *)


(*   Reserved Keywords =========================================    *)
(*                                                                  *)
(*  These names are reserved by the lexer and may not be used as    *)
(*  identifiers.  Some (e.g. `from`, `async`, `await`, `yield`)     *)
(*  are lexed but not yet wired into the parser — see § 21 of       *)
(*  `cryo.md` for the reserved-syntax table.                        *)
(*                                                                  *)
(*  alignof    any        as        async     auto       await     *)
(*  boolean    break      case      char      class      const     *)
(*  continue   default    delete    do        double     else      *)
(*  enum       export     extern    f32       f64        false     *)
(*  float      for        from      function  generic    i8        *)
(*  i16        i32        i64       i128      if         implement *)
(*  import     in         inline    int       intrinsic  loop      *)
(*  match      module     mut       mutable   namespace  new       *)
(*  null       optional   override  private   protected  public    *)
(*  return     sizeof     static    string    struct     switch    *)
(*  this       This       trait     true      tuple      type      *)
(*  typeof     u8         u16       u32       u64        u128      *)
(*  uint       unsafe     unsigned  virtual   void       where     *)
(*  while      with       yield                                    *)
