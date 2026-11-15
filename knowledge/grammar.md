---
type: specification
title: Smith grammar
description: Lexical rules, operator precedence, the EBNF grammar, and the properties the parser relies on.
---

# Smith grammar

This grammar is the single source of truth for syntax. It is a first draft, and the examples in [language/](language/index.md) are its first test cases.

## Lexical rules

- A newline ends a statement, except inside `( )` or `[ ]`, after an operator or comma, or before `requires`, `ensures`, and the `{` that opens a function's body. A block inside `( )` or `[ ]` separates its own statements by newlines as usual.
- Commas appear only inside `( )` and `[ ]`, in a list of effects, and between the indices of a `for`.
- An identifier, `IDENT`, is a letter or `_` followed by letters, digits, and `_`, and is not a keyword. The letters are `a` to `z` and `A` to `Z`.
- The keywords are `and`, `as`, `break`, `derive`, `effect`, `else`, `ensures`, `enum`, `fn`, `for`, `foreign`, `if`, `impl`, `in`, `loop`, `match`, `not`, `or`, `out`, `pub`, `requires`, `some`, `struct`, `take`, `trait`, `try`, `use`, `var`, `with`, and `without`.
- A comment starts with `//`, stands on its own line, and attaches to the definition, field, case, or statement that follows it.
- Literals keep their source spelling, so `0x10` and `16` stay distinct in the syntax tree.
- A string may hold expressions in braces. The lexer returns the text before a `{`, then ordinary tokens up to the matching `}`, then resumes the string.
- Inside a string, `TEXT` is a run of characters other than `"`, `\`, `{`, `}`, and a line break. `ESCAPE` is a backslash followed by one of `"`, `\`, `{`, `}`, `n`, or `t`. A backslash before anything else, a `}` that closes no expression, and a line break before the closing quote are errors.

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

`not` covers the whole comparison that follows it, so `not a < b` means `not (a < b)`. The EBNF below gives the forms an expression can take, and this table alone says how its operators group.

## EBNF

Items inside braces are separated by newlines under the lexical rules above.

```
file       = { item } ;
item       = use | [ "pub" ] ( fn_def | struct_def | enum_def | trait_def | effect_def | define )
           | impl_def
           | derive | without | foreign ;

fn_def     = fn_sig block ;
fn_sig     = "fn" IDENT [ generics ] "(" [ params ] ")" [ "->" result ]
             [ effects ] { contract } ;
result     = [ IDENT ":" ] type ;
generics   = "[" generic { "," generic } "]" ;
generic    = [ "out" ] IDENT [ ":" type ] ;
params     = param { "," param } [ "," ] ;
param      = IDENT ":" [ "take" ] type [ "=" expr ] ;
effects    = "!" IDENT { "," IDENT } ;
contract   = ( "requires" | "ensures" ) expr ;

struct_def = "struct" IDENT [ generics ] "{" { [ "pub" ] IDENT ":" type [ "=" expr ] } "}" ;
enum_def   = "enum" IDENT [ generics ] "{" { IDENT [ ":" type ] } "}" ;
trait_def  = "trait" IDENT generics "{" { fn_sig } "}" ;
effect_def = "effect" IDENT "{" { fn_sig } "}" ;
impl_def   = "impl" [ generics ] IDENT "[" type { "," type } "]" "{" { fn_def } "}" ;
derive     = "derive" IDENT "for" IDENT ;
without    = "without" IDENT "for" IDENT ;
use        = "use" IDENT { "." IDENT } [ "as" IDENT ]
             [ "(" IDENT { "," IDENT } [ "," ] ")" ] ;
foreign    = "foreign" STRING fn_sig ;

block      = "{" { stmt } "}" ;
stmt       = define | var_def | assign | with | "break" | expr ;
define     = pattern [ ":" type ] "=" expr ;
var_def    = "var" IDENT [ ":" type ] "=" expr ;
assign     = IDENT [ "[" index { "," index } "]" ] ( ":=" | "+=" | "-=" | "*=" | "/=" ) expr ;
with       = "with" [ pattern "=" ] expr ;
pattern    = IDENT | "var" IDENT | "." IDENT [ "(" pattern ")" ]
           | "(" pattern { "," pattern } ")" ;

expr       = operand { BINOP operand } ;
operand    = { "not" } unary ;
unary      = [ "-" | "try" ] postfix ;
postfix    = primary { "(" [ args ] ")" | "[" index { "," index } "]" | "." IDENT } ;
args       = arg { "," arg } [ "," ] ;
arg        = [ IDENT ":" ] expr ;
index      = expr | [ expr ] ".." [ expr ] ;
primary    = IDENT | LITERAL | string | "(" [ expr { "," expr } ] ")" | array | block
           | case | if | match | for | loop | lambda ;
array      = "[" [ expr { "," expr } [ "," ] ] "]" ;

string     = '"' { TEXT | ESCAPE | "{" expr "}" } '"' ;
case       = "." IDENT [ "(" [ args ] ")" ] ;
if         = "if" expr block [ "else" ( block | if ) ] ;
match      = "match" expr "{" { pattern block } "}" ;
for        = "for" IDENT { "," IDENT } block ;
loop       = "loop" [ IDENT "in" expr ] block ;
lambda     = "fn" "(" [ IDENT { "," IDENT } ] ")" block ;

type       = IDENT [ "[" type_arg { "," type_arg } "]" ]
           | "[" size { "," size } "]" type
           | "(" [ type { "," type } ] ")" [ "->" type [ effects ] ] ;
size       = expr | "some" IDENT ;
type_arg   = type | expr ;
```

## Parsing properties

- **No name lookup.** Parsing never needs to know what a name refers to, so definitions parse independently and in parallel.
- **One token of lookahead,** with one exception: a statement that starts like a tuple is read as an expression and converted to a pattern when `=` follows. This is still linear time.
- **`f[n](x)` and `a[i]` are the same postfix form.** The type checker tells a type argument from an index.
- **`{` never continues an expression.** After the condition of an `if`, a `match`, a `loop`, a `for`, or a contract it opens that construct's body, and anywhere else it starts a block. Construction is a call, so `{` never starts a literal.
- **Files split cheaply.** Top-level items start at column zero, with a keyword or with the name being defined, and only edited definitions are re-parsed.
