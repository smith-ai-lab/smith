---
type: specification
title: Smith grammar
description: Lexical rules, operator precedence, the EBNF grammar, and the properties the parser relies on.
---

# Smith grammar

This grammar is the single source of truth for syntax. It is a first draft, and the examples in [language.md](language.md) are its first test cases.

## Lexical rules

- A newline ends a statement, except inside `( )` or `[ ]`, or after an operator or comma.
- Commas appear only inside `( )` and `[ ]`.
- A comment starts with `//`, stands on its own line, and attaches to the definition, field, case, or statement that follows it.
- Literals keep their source spelling, so `0x10` and `16` stay distinct in the syntax tree.

## Precedence

Operators bind more tightly further down the table.

| Operators | Associativity |
| --- | --- |
| `or` | left |
| `and` | left |
| `not` | prefix |
| `==` `!=` `<` `<=` `>` `>=` | none |
| `..` | none |
| `+` `-` | left |
| `*` `/` `@` | left |
| unary `-`, `try` | prefix |
| `^` | right |
| call, index, `.` | left |

## EBNF

Items inside braces are separated by newlines under the lexical rules above.

```
file       = { item } ;
item       = use | [ "pub" ] ( fn_def | struct_def | enum_def | trait_def | define )
           | impl_def
           | derive | without | foreign ;

fn_def     = fn_sig block ;
fn_sig     = "fn" IDENT [ generics ] "(" [ params ] ")" [ "->" result ]
             [ effects ] { contract } ;
result     = [ IDENT ":" ] type ;
generics   = "[" generic { "," generic } "]" ;
generic    = [ "out" ] IDENT [ ":" type ] ;
params     = param { "," param } [ "," ] ;
param      = IDENT ":" [ "take" ] type ;
effects    = "!" IDENT { "," IDENT } ;
contract   = ( "requires" | "ensures" ) expr ;

struct_def = "struct" IDENT [ generics ] "{" { [ "pub" ] IDENT ":" type } "}" ;
enum_def   = "enum" IDENT [ generics ] "{" { IDENT [ ":" type ] } "}" ;
trait_def  = "trait" IDENT generics "{" { fn_sig } "}" ;
impl_def   = "impl" [ generics ] IDENT "[" type { "," type } "]" "{" { fn_def } "}" ;
derive     = "derive" IDENT "for" IDENT ;
without    = "without" IDENT "for" IDENT ;
use        = "use" IDENT { "." IDENT } [ "as" IDENT ]
             [ "(" IDENT { "," IDENT } [ "," ] ")" ] ;
foreign    = "foreign" STRING fn_sig ;

block      = "{" { stmt } "}" ;
stmt       = define | var_def | assign | with | "break" | expr ;
define     = pattern "=" expr ;
var_def    = "var" IDENT [ ":" type ] "=" expr ;
assign     = IDENT [ "[" index { "," index } "]" ] ( ":=" | "+=" | "-=" | "*=" | "/=" ) expr ;
with       = "with" [ pattern "=" ] expr ;
pattern    = IDENT | "var" IDENT | "." IDENT [ "(" pattern ")" ]
           | "(" pattern { "," pattern } ")" ;

expr       = unary { BINOP unary } ;
unary      = [ "-" | "not" | "try" ] postfix ;
postfix    = primary { "(" [ args ] ")" [ block ] | block | "[" index { "," index } "]"
                     | "." IDENT } ;
args       = arg { "," arg } [ "," ] ;
arg        = [ IDENT ":" ] expr ;
index      = expr | [ expr ] ".." [ expr ] ;
primary    = IDENT | LITERAL | "(" expr { "," expr } ")" | block
           | case | if | match | for | loop | lambda ;

case       = "." IDENT [ "(" [ args ] ")" ] ;
if         = "if" expr block [ "else" ( block | if ) ] ;
match      = "match" expr "{" { pattern block } "}" ;
for        = "for" IDENT { "," IDENT } block ;
loop       = "loop" [ IDENT "in" expr ] block ;
lambda     = "fn" "(" [ params ] ")" block ;

type       = IDENT [ "[" args "]" ]
           | "[" size { "," size } "]" type
           | "(" [ type { "," type } ] ")" [ "->" type [ effects ] ] ;
size       = expr | "some" IDENT ;
```

## Parsing properties

- **No name lookup.** Parsing never needs to know what a name refers to, so definitions parse independently and in parallel.
- **One token of lookahead,** with one exception: a statement that starts like a tuple is read as an expression and converted to a pattern when `=` follows. This is still linear time.
- **`f[n](x)` and `a[i]` are the same postfix form.** The type checker tells a type argument from an index.
- **`{` on the same line after a call opens a trailing block,** except in the header of `if`, `match`, `loop`, or `for`, where it opens that construct's body. On a new line it starts a new statement. Construction is a call, so `{` never starts a literal.
- **Files split cheaply.** Top-level items start with a keyword at column zero, and only edited definitions are re-parsed.
