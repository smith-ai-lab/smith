; inherits: c

; module game::replay;   import lib::text as tx;   import <stdio.h>;   export ...
["module" "import" "as"] @keyword.import
"export" @keyword.modifier

(module_declaration (module_path (identifier) @module))
(import_declaration (module_path (identifier) @module))
(import_declaration alias: (identifier) @module)

; x::name, c::printf, tx::Text, struct c::stat
(scoped_identifier scope: (identifier) @module)
(scoped_type_identifier scope: (identifier) @module)
(scoped_type_identifier name: (type_identifier) @type)
(call_expression function: (scoped_identifier name: (identifier) @function.call))
"::" @punctuation.delimiter

; T max(constexpr Type T, T a, T b)
(type_parameter "constexpr" @keyword.modifier)
(type_parameter "Type" @keyword.type)
(type_parameter name: (identifier) @type)

; SMITH_CHECK(a, ==, b)
(operator) @operator

; a macro that defines functions at file scope
(macro_invocation name: (identifier) @function.macro)

; if constexpr (T == f32 || is_integer(T)) { ... } in a generic
(if_statement "constexpr" @keyword.conditional)
((call_expression function: (identifier) @function.builtin)
  (#any-of? @function.builtin "is_integer" "is_float" "is_signed" "is_pointer" "is_bool" "is_enum" "is_record"
   "integer" "floating" "signed_integer" "unsigned_integer" "arithmetic" "boolean" "pointer" "enumeration"
   "structure"))

; ---- The syntax of #1428 (indentation blocks)

; # a comment and ## a doc comment
((comment) @comment.documentation
  (#match? @comment.documentation "^##"))

; fn, let, const, mut, pub, struct, enum, match, in, from, defer
["fn" "fn_once"] @keyword.function
["let" "const" "mut" "pub" "struct" "enum" "match" "in" "from" "defer"] @keyword
"try" @keyword.exception
(layout_import ["import" "as" "from"] @keyword.import)

; pub fn name[T](a: A) -> R:
(fn_declaration name: (identifier) @function)
(generic_parameters (identifier) @type.parameter)
(fn_parameters (identifier) @variable.parameter)
(fn_parameters "..." @punctuation.special)
"->" @punctuation.delimiter

; struct Point:   enum Color(u8):
(layout_struct name: (identifier) @type)
(layout_enum name: (identifier) @type)
(layout_field . (identifier) @variable.member)
(layout_variant . (identifier) @constant)
(variant_expression name: (identifier) @constant)

; the types: i32, ?T, *mut T, []T, Name(T), a.B
(type_identifier) @type
(pointer_type "mut" @keyword.modifier)
(array_type "mut" @keyword.modifier)

; import std.math as m   from std.math import sin
(dotted_path (identifier) @module)

; @inline   @serial.rename(to = "id")
(layout_attribute "@" @attribute)
(layout_attribute (dotted_path (identifier) @attribute))

; let x = 1   let (q, mut r) = f()   const n = 3
(layout_pattern (identifier) @variable)
(layout_const_declaration . (identifier) @constant)

; control flow
(layout_return_statement "return" @keyword.return)
["if" "else"] @keyword.conditional
["while" "for" "break" "continue"] @keyword.repeat
(layout_match_statement "match" @keyword.conditional)
