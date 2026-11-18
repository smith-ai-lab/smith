---
type: specification
title: Smith grammar
description: Lexical rules, operator precedence, the EBNF grammar, and the properties the parser relies on.
---

# Smith grammar

This grammar is the single source of truth for syntax. It is a first draft, and the examples in [language/](language/index.md) are its first test cases.

## Lexical rules

- Source text is UTF-8. Spaces and tabs separate tokens, and a line break is a line feed. A carriage return is an error anywhere in a file, inside a string or a comment too. Any other character that belongs to no token, string, or comment is an error.
- A newline ends a statement, except inside `( )` or `[ ]`, after an operator or comma, or before `requires`, `ensures`, `decreases`, and the `{` that opens a function's body, or that opens an `impl`'s after a `requires`. A block inside `( )` or `[ ]` separates its own statements by newlines as usual.
- Commas appear only inside `( )` and `[ ]`, in a list of effects, and between the indices of a `for`.
- An identifier, `IDENT`, is a letter or `_` followed by letters, digits, and `_`, and is not a keyword. The letters are `a` to `z` and `A` to `Z`.
- The keywords are `and`, `as`, `break`, `decreases`, `derive`, `effect`, `else`, `ensures`, `enum`, `fn`, `for`, `foreign`, `if`, `impl`, `in`, `loop`, `match`, `not`, `or`, `out`, `pub`, `requires`, `return`, `struct`, `take`, `trait`, `try`, `use`, `var`, `with`, and `without`.
- A comment starts with `//`, stands on its own line, and attaches to the definition, field, case, or statement that follows it.
- A whole number is written in decimal, as `1000`, in hexadecimal after `0x`, as `0xff`, or in binary after `0b`, as `0b1010`. A float has a digit on both sides of its point, as `2.5`, and may end with an exponent, as `2.5e3`, and a decimal number with an exponent alone is a float too, as `1e-5`. An `_` may stand between two digits, as in `1_000_000`. There is no octal form and no suffix that names a type.
- A whole number is also written as one character between single quotes, as `'a'`, and it is that character's code. The character is any but `'`, `\`, and a line break, or one of the escapes `\'`, `\\`, `\n`, and `\t`.
- A `-` written where a value may begin, directly before a number, is part of the literal, so `-128` is one literal. After a value, `-` is subtraction.
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
| `*` `/` `%` `@` | left |
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
generic    = [ "out" ] IDENT [ ":" type ] [ "=" type ] ;
params     = param { "," param } [ "," ] ;
param      = IDENT ":" [ "take" ] type [ "=" expr ] ;
effects    = "!" IDENT { "," IDENT } ;
contract   = ( "requires" | "ensures" ) expr | "decreases" expr { "," expr } ;

struct_def = "struct" IDENT [ generics ] "{" { [ "pub" ] IDENT ":" type [ "=" expr ] } "}" ;
enum_def   = "enum" IDENT [ generics ] "{" { IDENT [ ":" type ] } "}" ;
trait_def  = "trait" IDENT generics "{" { fn_sig } "}" ;
effect_def = "effect" IDENT "{" { fn_sig } "}" ;
impl_def   = "impl" [ generics ] IDENT "[" type { "," type } "]" { "requires" expr }
             "{" { fn_def } "}" ;
derive     = "derive" IDENT "for" IDENT ;
without    = "without" IDENT "for" IDENT ;
use        = "use" IDENT { "." IDENT } [ "as" IDENT ]
             [ "(" use_name { "," use_name } [ "," ] ")" ] ;
use_name   = IDENT [ "as" IDENT ] ;
foreign    = "foreign" STRING fn_sig ;

block      = "{" { stmt } "}" ;
stmt       = define | var_def | assign | with | "break" | "return" [ expr ] | expr ;
define     = pattern [ ":" type ] "=" expr ;
var_def    = "var" IDENT [ ":" type ] "=" expr ;
assign     = place ( "=" | "+=" | "-=" | "*=" | "/=" ) expr ;
place      = IDENT { "." IDENT | "[" index { "," index } "]" } ;
with       = "with" [ pattern "=" ] expr ;
pattern    = IDENT | "var" IDENT | "." IDENT [ "(" pattern ")" ]
           | "(" pattern { "," pattern } ")"
           | LITERAL | string | "[" [ pattern { "," pattern } ] "]" ;

expr       = operand { BINOP operand } ;
operand    = { "not" } unary ;
unary      = [ "-" | "try" ] postfix ;
postfix    = primary { call | "[" index { "," index } "]" | "." IDENT } ;
call       = "(" [ args ] ")" [ lambda ] | lambda ;
args       = arg { "," arg } [ "," ] ;
arg        = [ IDENT ":" ] expr ;
index      = expr | [ expr ] ".." [ expr ] ;
primary    = IDENT | LITERAL | string | "(" [ expr { "," expr } ] ")" | array
           | case | if | match | for | loop | lambda ;
array      = "[" [ expr { "," expr } [ "," ] ] "]" ;

string     = '"' { TEXT | ESCAPE | "{" expr "}" } '"' ;
case       = "." IDENT [ "(" [ args ] ")" ] ;
if         = "if" expr block [ "else" ( block | if ) ] ;
match      = "match" expr "{" { pattern block } "}" ;
for        = "for" IDENT { "," IDENT } block ;
loop       = "loop" [ IDENT "in" expr | "decreases" expr { "," expr } ] block ;
lambda     = [ "fn" "(" IDENT { "," IDENT } ")" ] block ;

type       = statement
           | IDENT [ "[" type_arg { "," type_arg } "]" ]
           | "[" size { "," size } "]" type
           | "(" [ type { "," type } ] ")" [ "->" type [ effects ] ] ;
size       = expr ;
statement  = operand CMP operand ;
type_arg   = type | expr ;
```

## Parsing properties

- **A string that is a pattern holds no expression.** A literal is a pattern only in a `match`.
- **No name lookup.** Parsing never needs to know what a name refers to, so definitions parse independently and in parallel. `x = e` with a bare name is one form to the parser, and the checker decides whether it defines `x` or assigns to a `var`.
- **One token of lookahead,** with one exception: a statement that starts like a tuple is read as an expression and converted to a pattern when `=` follows. This is still linear time.
- **A statement in a type position is a comparison,** such as `value < n`, where `CMP` is one of the comparison operators. It is the type of its proofs.
- **`f[n](x)` and `a[i]` are the same postfix form,** and the brackets may hold several expressions separated by commas. The type checker tells a type argument from an index. A `requires` that names a trait, as `requires Convert[A, B]`, is that form too, and the checker tells it from a statement about values.
- **`{` continues an expression in one way.** On the same line, directly after a call's `)`, or after what is called when a lambda is its only argument, it opens a lambda that is the call's last argument. After the condition of an `if`, the value of a `match`, the head of a `loop` or a `for`, or a contract it opens that construct's body instead, so a call there takes a lambda after it only inside parentheses. Anywhere else `{` starts a lambda. Construction is a call, so `{` never starts a literal.
- **Files split cheaply.** Top-level items start at column zero, with a keyword or with the name being defined, and only edited definitions are re-parsed.
