(* ================================================================ *)
(*  Cryo Language Grammar  -  W3C EBNF                              *)
(* ================================================================ *)


(*  Program & Statements ======================================= *)

Program            ::= Directive* Namespace? TopLevelItem*
Namespace          ::= "namespace" QualName ";"

TopLevelItem       ::= Import | ModuleDecl | VarDecl | FunctionDecl
                     | ExternDecl | IntrinsicDecl
                     | AggregateDecl | EnumDecl | TypeAlias | ImplBlock

Statement          ::= VarDecl | FunctionDecl | AggregateDecl | EnumDecl
                     | TypeAlias | ImplBlock
                     | If | While | For | "loop" Block | Match | Switch
                     | "break" ";" | "continue" ";" | Return
                     | "unsafe" Block | Block | Expr ";"

Block              ::= "{" Statement* "}"
Return             ::= "return" Expr? ";"


(*  Modules, Imports, Directives =============================== *)

ModuleDecl         ::= "public"? "module" ModulePath ";"
Import             ::= "import" ImportForm ";"
ImportForm         ::= "*" "from" ModulePath
                     | Ident ("," Ident)* "from" ModulePath
                     | ModulePath "as" Ident
                     | ModulePath "::" "{" Ident ("," Ident)* "}"
                     | ModulePath
ModulePath         ::= Ident ("::" Ident)*
QualName           ::= Ident ("::" Ident)*

Directive          ::= "#" "[" Ident DirectiveArgs? "]"
DirectiveArgs      ::= "(" (DirectiveArg ("," DirectiveArg)*)? ")"
DirectiveArg       ::= StringLit | Ident | NumLit


(*  Declarations =============================================== *)

VarDecl            ::= ("const" | "mut") Ident ":" Type ("=" Expr)? ";"

FunctionDecl       ::= Visibility? "function" Ident Generics?
                       "(" ParamList? ")" ("->" Type)?
                       ("where" WhereClause)? Block

ExternDecl         ::= "extern" "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"
                     | "extern" StringLit "{" ExternFn* "}"
ExternFn           ::= "extern" "function" Ident
                       "(" ParamList? ")" ("->" Type)? ";"

IntrinsicDecl      ::= "intrinsic" "function" Ident
                       "(" ParamList? ")" ("->" Type)? (Block | ";")
                     | "intrinsic" "const" Ident ":" Type ("=" Expr)? ";"

ParamList          ::= Param ("," Param)* ("," VariadicParam)?
                     | VariadicParam
Param              ::= Ident ":" Type | "&this" | "mut" "&this"
VariadicParam      ::= Ident ":" Type "..."

WhereClause        ::= Ident ":" Ident ("," Ident ":" Ident)*
Visibility         ::= "public" | "private" | "protected"


(*  Aggregates (struct/class), Enums, Type Aliases ============= *)

(* Structs and classes share a single rule; the keyword and a few
   class-only members (constructors, destructors, virtual/override)
   are the only differences.                                        *)

AggregateDecl      ::= "type"? AggregateKind Ident Generics?
                       (":" Ident)?                  (* base class  *)
                       "{" Member* "}"
AggregateKind      ::= "struct" | "class"

Member             ::= Visibility? "static"?
                       (Field | Method | Constructor | Destructor)

Field              ::= Ident ":" Type ("=" Expr)? ";"
Method             ::= ("virtual" | "override")? Ident Generics?
                       "(" ParamList? ")" ("->" Type)? (Block | ";")
Constructor        ::= Ident "(" ParamList? ")"
                       (":" Ident "(" ArgList? ")")? Block
Destructor         ::= "~" Ident "(" ")" ("->" Type)? Block

EnumDecl           ::= "type"? "enum" Ident Generics?
                       "{" (EnumVariant ("," | ";"))* "}"
EnumVariant        ::= Ident
                     | Ident "=" NumLit
                     | Ident "(" Type ("," Type)* ")"

TypeAlias          ::= "type" Ident Generics? "=" Type ";"


(*  Implementation Blocks & Generics =========================== *)

ImplBlock          ::= "implement" ("enum" | "struct" | "class")?
                       QualName GenericArgs?
                       "{" MethodImpl* "}"
MethodImpl         ::= "static"? Ident Generics?
                       "(" ParamList? ")" ("->" Type)? Block

Generics           ::= "<" GenericParam ("," GenericParam)* ">"
GenericParam       ::= Ident (":" Ident ("+" Ident)*)?
GenericArgs        ::= "<" Type ("," Type)* ">"


(*  Expressions ================================================ *)

(* Operators and their precedence/associativity are defined in §10. *)

Expr               ::= Assign
Assign             ::= Conditional (AssignOp Assign)?
AssignOp           ::= "=" | "+=" | "-=" | "*=" | "/=" | "&=" | "|="

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
                     | ("." | "->" | "?.") Ident
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
                     | IfExpr
                     | Match
                     | Expr "|>" Expr
                     | Expr "??" Expr
                     | "(" Expr ")"

StructLit          ::= Ident GenericArgs?
                       "{" Ident ":" Expr ("," Ident ":" Expr)* "}"
ArrayLit           ::= "[" (Expr ("," Expr)*)? "]"
                     | "[" Expr ";" Expr "]"
NewExpr            ::= "new" Type ("(" ArgList? ")")?
                     | "new" Type "[" Expr "]"
IfExpr             ::= "if" "(" Expr ")" "{" Expr "}" "else" "{" Expr "}"


(*  Control Flow =============================================== *)

If                 ::= "if" "(" Expr ")" Block
                       ("else" "if" "(" Expr ")" Block)*
                       ("else" Block)?
While              ::= "while" "(" Expr ")" Block
For                ::= "for" "(" ForInit Expr ";" Expr ")" Block
ForInit            ::= VarDecl | Ident ":" Type ("=" Expr)? ";"

Match              ::= "match" "("? Expr ")"? "{" MatchArm* "}"
MatchArm           ::= Pattern ("|" Pattern)* "=>" (Block | Expr)

Switch             ::= "switch" "(" Expr ")" "{" CaseClause* "}"
CaseClause         ::= ("case" Expr | "default") ":" Statement*


(*  Patterns =================================================== *)

Pattern            ::= "_"
                     | Literal
                     | Ident
                     | Ident "::" Ident ("(" PatElem ("," PatElem)* ")")?
                     | Literal ".." Literal
PatElem            ::= Ident | "_" | Literal


(*  Types & Literals =========================================== *)

Type               ::= BaseType "*"+                       (* pointer *)
                     | "&" "mut"? Type                     (* reference *)
                     | BaseType ("[" NumLit? "]")+         (* array *)
                     | "(" Type ("," Type)* ")"            (* tuple *)
                     | "(" (Type ("," Type)*)? ")" "->" Type  (* fn *)
                     | "()"                                (* unit *)
                     | BaseType
BaseType           ::= Primitive | Ident GenericArgs?
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
(*   15   () [] . -> ?. ++ -- (postfix)      left                   *)


(*   Reserved Keywords =========================================    *)
(*                                                                  *)
(*  alignof  as       break    case      class    const             *)
(*  continue default  else     enum      extern   false             *)
(*  for      from     function if        implement import           *)
(*  intrinsic loop    match    module    mut      namespace         *)
(*  new      null     override private   protected public           *)
(*  return   sizeof   static   struct    switch   this              *)
(*  true     type     unsafe   virtual   void     where  while      *)