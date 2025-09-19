
<program>        ::= { <statement> }
<statement>      ::= <var-declaration> | <function-declaration> | <expression-statement> | <if-statement> | <while-statement> | <for-statement> | <break-statement> | <continue-statement> | <return-statement>

<var-declaration> ::= ("const" | "mut") <identifier> ":" <type> ["=" <expression>] ";"

<function-declaration>  ::=  <visibility> "function" <identifier> "(" [<param-list>] ")" ["->" <type>] <block>
<param-list>            ::=  <param> { "," <param> }
<param>                 ::=  <type> <identifier>
<block>                 ::=  "{" { <statement> } "}"
<visibility>            ::=  "public" | "private"


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


<type>           ::=  "int"| "i8" | "i16" | "i32" | "i64" | "i128" | "float" | "boolean" | "str" | "void" | <identifier> | <type> "[]"
<identifier>     ::=  <letter> { <letter> | <digit> }
<number>         ::=  { <digit> } ["." { <digit> }]
<string>         ::=  '"' { <any-character-except-quote> } '"'
<boolean>        ::=  "true" | "false"

<comments>       ::=  "//" { <any-character-except-newline> } | "/*" { <any-character> } "*/" | "/**" { <any-character> } "**/" | "///" { <any-character-except-newline> }