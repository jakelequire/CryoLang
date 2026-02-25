<program>        ::= { <statement> }
<statement>      ::= <var-declaration> | <function-declaration> | <struct-declaration> | <class-declaration> | <enum-declaration> | <type-alias-declaration> | <implementation-block> | <expression-statement> | <if-statement> | <while-statement> | <for-statement> | <break-statement> | <continue-statement> | <return-statement>

<var-declaration> ::= ("const" | "mut") <identifier> ":" <type> ["=" <expression>] ";"

<function-declaration>  ::=  "function" <identifier> "(" [<param-list>] ")" ["->" <type>] <block>
<param-list>            ::=  <param> { "," <param> }
<param>                 ::=  <identifier> ":" <type>
<block>                 ::=  "{" { <statement> } "}"
<visibility>            ::=  "public" | "private"

<struct-declaration>    ::=  "type" "struct" <identifier> [<generic-params>] "{" { <struct-member> } "}"
<class-declaration>     ::=  "class" <identifier> [<generic-params>] [":" <identifier>] "{" { <class-member> } "}"
<enum-declaration>      ::=  ["type"] "enum" <identifier> [<generic-params>] "{" { <enum-variant> } "}"
<type-alias-declaration>::=  "type" <identifier> "=" <type> ";"
<implementation-block>  ::=  "implement" ("enum" | "struct" | "class") <identifier> "{" { <implementation-member> } "}"

<generic-params>        ::=  "<" <generic-param> { "," <generic-param> } ">"
<generic-param>         ::=  <identifier> [":" <type-constraint>]
<type-constraint>       ::=  <type> { "+" <type> }

<struct-member>         ::=  <struct-field> | <struct-method>
<class-member>          ::=  [<visibility>] ( <struct-field> | <struct-method> )
<implementation-member> ::=  <field-implementation> | <method-implementation>

<struct-field>          ::=  [<visibility>] <identifier> ":" <type> ["=" <expression>] ";"
<struct-method>         ::=  [<visibility>] <identifier> "(" [<param-list>] ")" ["->" <type>] [ <block> ] ";"

<field-implementation>  ::=  <identifier> "=" <expression> ";"
<method-implementation> ::=  <identifier> "(" [<param-list>] ")" ["->" <type>] <block>

<enum-variant>          ::=  <identifier> ["(" <type-list> ")"] [","]
<type-list>             ::=  <type> { "," <type> }


<expression-statement>    ::=  <expression> ";"
<expression>              ::=  <assignment-expression>
<assignment-expression>   ::=  <conditional-expression> | <identifier> "=" <assignment-expression>
<conditional-expression>  ::=  <logical-or-expression> ["?" <expression> ":" <conditional-expression>]
<logical-or-expression>   ::=  <logical-and-expression> { "||" <logical-and-expression> }
<logical-and-expression>  ::=  <equality-expression> { "&&" <equality-expression> }
<equality-expression>     ::=  <relational-expression> { ("==" | "!=") <relational-expression> }
<relational-expression>   ::=  <additive-expression> { ("<" | ">" | "<=" | ">=") <additive-expression> }
<additive-expression>     ::=  <term> { ("+" | "-") <term> }
<term>                    ::=  <factor> { ("*" | "/" | "%") <factor> }
<factor>                  ::=  <unary-operator> <factor> | <primary>
<unary-operator>          ::=  "-" | "!"
<primary>                 ::=  <number> | <string> | <boolean> | <identifier> | "(" <expression> ")"


<if-statement>        ::=  "if" "(" <expression> ")" <block> ["else" <block>]
<while-statement>     ::=  "while" "(" <expression> ")" <block>
<for-statement>       ::=  "for" "(" <var-declaration> <expression> ";" <expression> ")" <block>
<break-statement>     ::=  "break" ";"
<continue-statement>  ::=  "continue" ";"
<return-statement>    ::=  "return" [<expression>] ";"


<type>           ::=  "int"| "i8" | "i16" | "i32" | "i64" | "i128" | "float" | "boolean" | "string" | "char" | "void" | <identifier> | <type> "[]"
<identifier>     ::=  <letter> { <letter> | <digit> }
<number>         ::=  { <digit> } ["." { <digit> }]
<string>         ::=  '"' { <any-character-except-quote> } '"'
<boolean>        ::=  "true" | "false"

<comments>       ::=  "//" { <any-character-except-newline> } | "/*" { <any-character> } "*/" | "/**" { <any-character> } "**/" | "///" { <any-character-except-newline> }