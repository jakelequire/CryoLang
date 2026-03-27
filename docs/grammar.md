(*
 * Cryo Programming Language — Formal Grammar Specification
 * Notation: Extended Backus-Naur Form (EBNF)
 *
 *   { X }     — zero or more repetitions of X
 *   [ X ]     — optional (zero or one occurrence of X)
 *   X | Y     — alternative (X or Y)
 *   ( X )     — grouping
 *   "x"       — terminal symbol / keyword / literal token
 *   X Y       — concatenation (X followed by Y)
 *)


(* ================================================================ *)
(* Program Structure                                                *)
(* ================================================================ *)

program                 = { directive } [ namespace_decl ] { top_level_item } ;

top_level_item          = import_decl
                        | module_decl
                        | var_declaration
                        | function_declaration
                        | extern_function_decl
                        | extern_block
                        | intrinsic_decl
                        | struct_declaration
                        | class_declaration
                        | enum_declaration
                        | type_alias_declaration
                        | implementation_block ;

statement               = var_declaration
                        | function_declaration
                        | struct_declaration
                        | class_declaration
                        | enum_declaration
                        | type_alias_declaration
                        | implementation_block
                        | if_statement
                        | while_statement
                        | for_statement
                        | loop_statement
                        | match_statement
                        | switch_statement
                        | break_statement
                        | continue_statement
                        | return_statement
                        | unsafe_block
                        | block
                        | expression_statement ;

block                   = "{" { statement } "}" ;


(* ================================================================ *)
(* Namespace, Imports, and Modules                                  *)
(* ================================================================ *)

namespace_decl          = "namespace" qualified_name ";" ;

import_decl             = "import" import_form ";" ;

import_form             = "*" "from" module_path
                        | identifier { "," identifier } "from" module_path
                        | module_path "as" identifier
                        | module_path "::" "{" identifier { "," identifier } "}"
                        | module_path ;

module_decl             = [ "public" ] "module" module_path ";" ;

module_path             = identifier { "::" identifier } ;
qualified_name          = identifier { "::" identifier } ;


(* ================================================================ *)
(* Directives                                                       *)
(* ================================================================ *)

directive               = "#" "[" identifier [ directive_args ] "]" ;
directive_args          = "(" [ directive_arg { "," directive_arg } ] ")" ;
directive_arg           = string_literal | identifier | numeric_literal ;


(* ================================================================ *)
(* Variable Declarations                                            *)
(* ================================================================ *)

var_declaration         = ( "const" | "mut" ) identifier ":" type "=" expression ";"
                        | ( "const" | "mut" ) identifier ":" type ";" ;


(* ================================================================ *)
(* Function Declarations                                            *)
(* ================================================================ *)

function_declaration    = [ visibility ] "function" identifier
                          [ generic_params ] "(" [ param_list ] ")"
                          [ "->" type ]
                          [ "where" where_clause ]
                          block ;

extern_function_decl    = "extern" "function" identifier
                          "(" [ param_list ] ")" [ "->" type ] ";" ;

extern_block            = "extern" string_literal "{" { extern_function_decl } "}" ;

intrinsic_decl          = "intrinsic" "function" identifier
                          "(" [ param_list ] ")" [ "->" type ] ( block | ";" )
                        | "intrinsic" "const" identifier ":" type [ "=" expression ] ";" ;

param_list              = param { "," param } [ "," variadic_param ]
                        | variadic_param ;

param                   = identifier ":" type
                        | "&this"
                        | "mut" "&this" ;

variadic_param          = identifier ":" type "..." ;

where_clause            = identifier ":" identifier { "," identifier ":" identifier } ;

visibility              = "public" | "private" | "protected" ;


(* ================================================================ *)
(* Struct Declarations                                              *)
(* ================================================================ *)

struct_declaration      = "type" "struct" identifier [ generic_params ]
                          "{" { struct_member } "}" ;

struct_member           = struct_field | struct_method ;

struct_field            = [ visibility ] identifier ":" type [ "=" expression ] ";" ;

struct_method           = [ visibility ] [ "static" ] identifier
                          [ generic_params ] "(" [ param_list ] ")"
                          [ "->" type ] ( block | ";" ) ;


(* ================================================================ *)
(* Class Declarations                                               *)
(* ================================================================ *)

class_declaration       = [ "type" ] "class" identifier [ generic_params ]
                          [ ":" identifier ]
                          "{" { class_member } "}" ;

class_member            = [ visibility ] ( class_field
                                         | class_method
                                         | constructor
                                         | destructor ) ;

class_field             = identifier ":" type [ "=" expression ] ";" ;

class_method            = [ "static" ] [ "virtual" | "override" ] identifier
                          [ generic_params ] "(" [ param_list ] ")"
                          [ "->" type ] ( block | ";" ) ;

constructor             = identifier "(" [ param_list ] ")"
                          [ ":" identifier "(" [ arg_list ] ")" ]
                          block ;

destructor              = "~" identifier "(" ")" [ "->" type ] block ;


(* ================================================================ *)
(* Enum Declarations                                                *)
(* ================================================================ *)

enum_declaration        = [ "type" ] "enum" identifier [ generic_params ]
                          "{" { enum_variant ( "," | ";" ) } "}" ;

enum_variant            = identifier
                        | identifier "=" numeric_literal
                        | identifier "(" type_list ")" ;

type_list               = type { "," type } ;


(* ================================================================ *)
(* Type Alias Declarations                                          *)
(* ================================================================ *)

type_alias_declaration  = "type" identifier [ generic_params ] "=" type ";" ;


(* ================================================================ *)
(* Implementation Blocks                                            *)
(* ================================================================ *)

implementation_block    = "implement" [ "enum" | "struct" | "class" ]
                          qualified_name [ generic_args ]
                          "{" { method_implementation } "}" ;

method_implementation   = [ "static" ] identifier
                          [ generic_params ] "(" [ param_list ] ")"
                          [ "->" type ] block ;


(* ================================================================ *)
(* Generics                                                         *)
(* ================================================================ *)

generic_params          = "<" generic_param { "," generic_param } ">" ;
generic_param           = identifier [ ":" type_constraint ] ;
type_constraint         = identifier { "+" identifier } ;

generic_args            = "<" type { "," type } ">" ;


(* ================================================================ *)
(* Expressions — Precedence (lowest to highest)                     *)
(* ================================================================ *)

expression              = assignment_expr ;

assignment_expr         = conditional_expr [ assignment_op assignment_expr ] ;
assignment_op           = "=" | "+=" | "-=" | "*=" | "/=" | "&=" | "|=" ;

conditional_expr        = logical_or_expr [ "?" expression ":" conditional_expr ] ;

logical_or_expr         = logical_and_expr { "||" logical_and_expr } ;
logical_and_expr        = bitwise_or_expr { "&&" bitwise_or_expr } ;

bitwise_or_expr         = bitwise_xor_expr { "|" bitwise_xor_expr } ;
bitwise_xor_expr        = bitwise_and_expr { "^" bitwise_and_expr } ;
bitwise_and_expr        = equality_expr { "&" equality_expr } ;

equality_expr           = relational_expr { ( "==" | "!=" ) relational_expr } ;
relational_expr         = shift_expr { ( "<" | ">" | "<=" | ">=" | "<=>" ) shift_expr } ;

shift_expr              = additive_expr { ( "<<" | ">>" ) additive_expr } ;
additive_expr           = multiplicative_expr { ( "+" | "-" ) multiplicative_expr } ;
multiplicative_expr     = cast_expr { ( "*" | "/" | "%" ) cast_expr } ;

cast_expr               = unary_expr { "as" type } ;

unary_expr              = unary_op unary_expr
                        | postfix_expr ;
unary_op                = "-" | "!" | "&" | "*" | "~" | "++" | "--" ;

postfix_expr            = primary_expr { postfix_op } ;
postfix_op              = "(" [ arg_list ] ")"
                        | "[" expression "]"
                        | "." identifier
                        | "->" identifier
                        | "?." identifier
                        | "++"
                        | "--" ;

arg_list                = expression { "," expression } ;


(* ================================================================ *)
(* Primary Expressions                                              *)
(* ================================================================ *)

primary_expr            = numeric_literal
                        | string_literal
                        | char_literal
                        | boolean_literal
                        | "null"
                        | "this"
                        | identifier
                        | scope_resolution_expr
                        | generic_call_expr
                        | struct_literal
                        | array_literal
                        | new_expr
                        | sizeof_expr
                        | alignof_expr
                        | if_expr
                        | match_expr
                        | pipe_expr
                        | null_coalescing_expr
                        | "(" expression ")" ;

scope_resolution_expr   = identifier "::" identifier { "::" identifier } ;

generic_call_expr       = identifier generic_args [ "(" [ arg_list ] ")" ]
                        | identifier "::" identifier generic_args [ "(" [ arg_list ] ")" ] ;

struct_literal          = identifier [ generic_args ]
                          "{" field_init { "," field_init } "}" ;
field_init              = identifier ":" expression ;

array_literal           = "[" [ expression { "," expression } ] "]"
                        | "[" expression ";" expression "]" ;

new_expr                = "new" type [ "(" [ arg_list ] ")" ]
                        | "new" type "[" expression "]" ;

sizeof_expr             = "sizeof" "(" type ")" ;
alignof_expr            = "alignof" "(" type ")" ;

if_expr                 = "if" "(" expression ")" "{" expression "}"
                          "else" "{" expression "}" ;

match_expr              = "match" [ "(" ] expression [ ")" ]
                          "{" { match_arm } "}" ;

pipe_expr               = expression "|>" expression ;

null_coalescing_expr    = expression "??" expression ;


(* ================================================================ *)
(* Control Flow Statements                                          *)
(* ================================================================ *)

if_statement            = "if" "(" expression ")" block
                          { "else" "if" "(" expression ")" block }
                          [ "else" block ] ;

while_statement         = "while" "(" expression ")" block ;

for_statement           = "for" "(" for_init expression ";" expression ")" block ;
for_init                = var_declaration
                        | identifier ":" type [ "=" expression ] ";" ;

loop_statement          = "loop" block ;

match_statement         = "match" [ "(" ] expression [ ")" ]
                          "{" { match_arm } "}" ;
match_arm               = pattern { "|" pattern } "=>" ( block | expression ) ;

switch_statement        = "switch" "(" expression ")" "{" { case_clause } "}" ;
case_clause             = ( "case" expression | "default" ) ":" { statement } ;

break_statement         = "break" ";" ;
continue_statement      = "continue" ";" ;
return_statement        = "return" [ expression ] ";" ;

unsafe_block            = "unsafe" block ;

expression_statement    = expression ";" ;


(* ================================================================ *)
(* Pattern Matching                                                 *)
(* ================================================================ *)

pattern                 = wildcard_pattern
                        | literal_pattern
                        | identifier_pattern
                        | enum_pattern
                        | range_pattern ;

wildcard_pattern        = "_" ;
literal_pattern         = numeric_literal | string_literal | char_literal | boolean_literal ;
identifier_pattern      = identifier ;

enum_pattern            = identifier "::" identifier
                          [ "(" pattern_element { "," pattern_element } ")" ] ;
pattern_element         = identifier | "_" | numeric_literal | boolean_literal | string_literal ;

range_pattern           = literal_pattern ".." literal_pattern ;


(* ================================================================ *)
(* Type Annotations                                                 *)
(* ================================================================ *)

type                    = base_type
                        | base_type "*" { "*" }
                        | "&" [ "mut" ] type
                        | base_type "[" [ numeric_literal ] "]" { "[" [ numeric_literal ] "]" }
                        | "(" type { "," type } ")"
                        | "(" [ type_list ] ")" "->" type
                        | "()" ;

base_type               = primitive_type
                        | identifier
                        | identifier generic_args ;

primitive_type          = "void" | "boolean" | "char" | "string"
                        | "int"  | "i8"  | "i16"  | "i32"  | "i64"  | "i128"
                        | "uint" | "u8"  | "u16"  | "u32"  | "u64"  | "u128"
                        | "float" | "f32" | "f64" | "double" | "usize" | "isize" ;


(* ================================================================ *)
(* Literals                                                         *)
(* ================================================================ *)

numeric_literal         = integer_literal | float_literal ;

integer_literal         = decimal_literal
                        | hex_literal
                        | binary_literal
                        | octal_literal ;

decimal_literal         = digit { digit | "_" } [ type_suffix ] ;
hex_literal             = ( "0x" | "0X" ) hex_digit { hex_digit | "_" } [ type_suffix ] ;
binary_literal          = ( "0b" | "0B" ) bin_digit { bin_digit | "_" } [ type_suffix ] ;
octal_literal           = ( "0o" | "0O" ) oct_digit { oct_digit | "_" } [ type_suffix ] ;

float_literal           = digit { digit | "_" } "." digit { digit | "_" }
                          [ ( "e" | "E" ) [ "+" | "-" ] digit { digit } ]
                          [ type_suffix ] ;

type_suffix             = "u8" | "u16" | "u32" | "u64"
                        | "i8" | "i16" | "i32" | "i64"
                        | "f32" | "f64" | "usize" | "isize" ;

string_literal          = '"' { string_char } '"' ;
string_char             = ? any character except '"' or '\' ?
                        | escape_sequence ;

char_literal            = "'" ( ? any character except "'" or '\' ? | escape_sequence ) "'" ;

escape_sequence         = "\n" | "\t" | "\r" | "\\" | "\'" | '\"' | "\0"
                        | "\a" | "\b" | "\f" | "\v"
                        | "\x" hex_digit hex_digit
                        | "\" oct_digit [ oct_digit [ oct_digit ] ] ;

boolean_literal         = "true" | "false" ;


(* ================================================================ *)
(* Lexical Elements                                                 *)
(* ================================================================ *)

identifier              = letter { letter | digit | "_" } ;

letter                  = "a" | "b" | (* ... *) "z"
                        | "A" | "B" | (* ... *) "Z"
                        | "_" ;

digit                   = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
hex_digit               = digit | "a" | "b" | "c" | "d" | "e" | "f"
                                | "A" | "B" | "C" | "D" | "E" | "F" ;
bin_digit               = "0" | "1" ;
oct_digit               = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" ;


(* ================================================================ *)
(* Comments                                                         *)
(* ================================================================ *)

line_comment            = "//" { ? any character except newline ? } ;
block_comment           = "/*" { ? any character ? } "*/" ;
doc_comment_line        = "///" { ? any character except newline ? } ;
doc_comment_block       = "/**" { ? any character ? } "*/" ;


(* ================================================================ *)
(* Operator Precedence (lowest to highest)                          *)
(*                                                                  *)
(*  1.  =  +=  -=  *=  /=  &=  |=           (right-associative)    *)
(*  2.  ? :                                  (right-associative)    *)
(*  3.  ||                                   (left-associative)     *)
(*  4.  &&                                   (left-associative)     *)
(*  5.  |                                    (left-associative)     *)
(*  6.  ^                                    (left-associative)     *)
(*  7.  &                                    (left-associative)     *)
(*  8.  ==  !=                               (left-associative)     *)
(*  9.  <  >  <=  >=  <=>                    (left-associative)     *)
(* 10.  <<  >>                               (left-associative)     *)
(* 11.  +  -                                 (left-associative)     *)
(* 12.  *  /  %                              (left-associative)     *)
(* 13.  as                                   (left-associative)     *)
(* 14.  -  !  &  *  ~  ++  --               (right-associative)    *)
(* 15.  ()  []  .  ->  ?.  ++  --           (left-associative)     *)
(*                                                                  *)
(* ================================================================ *)
