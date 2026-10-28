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
(type_parameter "type" @keyword.type)
(type_parameter name: (identifier) @type)

; SMITH_CHECK(a, ==, b)
(operator) @operator

; a macro that defines functions at file scope
(macro_invocation name: (identifier) @function.macro)

; if constexpr (T == F32 || Int(T)) { ... } in a generic
(if_statement "constexpr" @keyword.conditional)
((call_expression function: (identifier) @function.builtin)
  (#any-of? @function.builtin "is_integer" "is_float" "is_signed" "is_pointer" "is_bool" "is_enum" "is_record"
   "integer" "floating" "signed_integer" "unsigned_integer" "arithmetic" "boolean" "pointer" "enumeration"
   "structure"))
