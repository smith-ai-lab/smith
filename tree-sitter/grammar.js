/**
 * @file tree-sitter grammar for .smith: tree-sitter-c's grammar plus what .smith adds (knowledge/smith-language.md, issue #11),
 * plus the C23 and clang spellings smith's own C uses that tree-sitter-c does not know (#embed, _Atomic(T), _BitInt,
 * `enum : unsigned char`, types and operators as macro arguments).
 * Plain C stays C: module, import, export and type are keywords only where the added rules want them, and an ordinary
 * name everywhere else.
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// scripts/tree-sitter.sh puts the pinned tree-sitter-c here before it generates.
const C = require('./.vendor/tree-sitter-c/grammar');

const sized = ['u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'usize', 'isize', 'f16', 'f32', 'f64'];
// Words .smith gives a meaning only where its rules say so; an ordinary name elsewhere (and where the grammar could
// take either, it takes the .smith meaning: prec.dynamic below).
function commaSep(rule) { return optional(commaSep1(rule)); }
function commaSep1(rule) { return seq(rule, repeat(seq(',', rule))); }

const soft = ['module', 'import', 'export', 'type'];

module.exports = grammar(C, {
  name: 'smith',

  conflicts: ($, original) => original.concat([
    [$.scoped_identifier, $.scoped_type_identifier],
    [$.macro_type_specifier, $._argument],
    [$.expression, $.macro_type_specifier],
    [$.type_specifier, $.expression, $.macro_type_specifier, $.macro_invocation],
    [$.sized_type_specifier, $.enum_specifier],
    [$.expression, $.primitive_type],
    [$.module_declaration, $.type_specifier],
    [$.import_declaration, $.type_specifier],
    [$.type_parameter, $.type_specifier],
    [$.type_parameter, $.type_qualifier],
    [$.storage_class_specifier, $.list_declaration, $.auto_declaration],
    [$.type_qualifier, $.auto_declaration],
    [$._declaration_modifiers, $.attributed_statement, $.export_declaration],
    [$.pointer_field_declarator, $.tuple_field],
    [$.type_specifier, $.payload_variant],
    [$.field_designator, $.variant_expression],
  ]),

  rules: {
    _top_level_item: ($, original) => choice(
      original,
      $.module_declaration,
      $.import_declaration,
      $.export_declaration,
      $.macro_invocation,
    ),

    // `#if` around exports (the conditional's items are block items)
    _block_item: ($, original) => choice(original, $.export_declaration, $.import_declaration),

    // SMITH_TEST_DEFINE_VALUE(bool, bool, ...) at file scope, whose expansion is a definition: no semicolon
    macro_invocation: $ => prec.dynamic(-2, seq(field('name', $.identifier), $.argument_list)),

    // typeof(*x) f(auto x): the operand of a type-like macro or of typeof may be an expression; slice_of(T, countof(rest))
    // f(...): a generic's result type computed from its parameters has a list of them
    macro_type_specifier: ($, original) => choice(
      original,
      prec.dynamic(-2, seq(field('name', choice($.identifier, $.scoped_identifier)), '(', commaSep1(field('argument', $.expression)), ')')),
    ),

    // x.[f] and x->[f]: the member named by a field f of fields(T)
    field_expression: $ => seq(
      prec(C.PREC.FIELD, seq(field('argument', $.expression), field('operator', choice('.', '->')))),
      field('field', choice($._field_identifier, seq('[', $.expression, ']'))),
    ),

    // return {a, b}; in a function that returns a tuple
    return_statement: ($, original) => choice(original, seq('return', $.initializer_list, ';')),

    // Apple blocks: ^(int x) { ... }
    block_expression: $ => prec(1, seq('^', optional(field('type', $.type_specifier)), optional($.parameter_list), $.compound_statement)),

    // module game::replay;
    module_declaration: $ => prec.dynamic(1, seq('module', $.module_path, ';')),

    // import lib::text as tx;   import <stdio.h>;   import "local.h";
    import_declaration: $ => prec.dynamic(1, seq(
      'import',
      choice(
        seq($.module_path, optional(seq('as', field('alias', $.identifier)))),
        $.system_lib_string,
        $.string_literal,
      ),
      ';',
    )),

    module_path: $ => seq($.identifier, repeat(seq('::', $.identifier))),

    // x::name, c::printf, c::EOF
    scoped_identifier: $ => prec(1, seq(
      field('scope', $.identifier),
      repeat1(seq('::', field('name', $.identifier))),
    )),

    // tx::Text, struct c::stat
    scoped_type_identifier: $ => prec(1, seq(
      field('scope', $.identifier),
      repeat1(seq('::', field('name', alias($.identifier, $.type_identifier)))),
    )),

    // The sized types are names to an expression (`u8 + 1` where a file declares its own u8) and types elsewhere.
    expression: ($, original) => choice(
      original,
      $.scoped_identifier,
      $.block_expression,
      $.variant_expression,
      alias(choice(...sized), $.identifier),
      alias(choice(...soft), $.identifier),
    ),

    // `draw({.circle = {.radius = 1}})`: braces that begin with a designator are no block: an enum's payload, as an argument
    designated_list: $ => seq('{', $.field_designator, '=', choice($.expression, $.initializer_list),
      repeat(seq(',', $._initializer_item)), optional(','), '}'),

    // A sized type's name is a keyword where a type could begin (the first word in `enum { ... }` may be a payload's
    // type), and still an enumerator's name there (`enum { i64 = 7, u8 }` in plain C).
    enumerator: $ => seq(
      field('name', choice($.identifier, alias(choice(...sized), $.identifier))),
      optional(seq('=', field('value', $.expression))),
    ),

    // `.empty`: a variant of an enum with payloads, in the type the context gives (`Shape s = .empty;`, `case .empty:`)
    variant_expression: $ => prec.dynamic(-1, seq('.', field('name', $.identifier))),

    // module = 0; (an assignment's left side is a short list of its own)
    _assignment_left_expression: ($, original) => choice(
      original,
      $.scoped_identifier,
      alias(choice(...sized), $.identifier),
      alias(choice(...soft), $.identifier),
    ),

    type_specifier: ($, original) => choice(
      original,
      $.atomic_type_specifier,
      $.bit_int_type,
      alias(choice('module', 'import', 'type'), $.type_identifier),
    ),

    _type_identifier: ($, original) => choice(original, $.scoped_type_identifier),

    // Apple blocks: __block int x;
    storage_class_specifier: ($, original) => choice(original, '__block'),

    // export on a declaration, a function definition, a typedef or a tag: only where a module declares (file scope),
    // so a field or a variable called export stays one.
    // [[smith::c_name("Name")]] export ...: attributes before `export` give the export its C name.
    export_declaration: $ => seq(
      repeat($.attribute_declaration),
      'export',
      choice(
        $.function_definition,
        $.declaration,
        $.type_definition,
        seq($.type_specifier, ';'),
      ),
    ),

    primitive_type: ($, original) => choice(original, ...sized),

    // T max(constexpr type T, T a, T b)
    // and, for argument lists, auto... rest
    parameter_declaration: ($, original) => choice(original, prec.dynamic(1, $.type_parameter), prec.dynamic(1, $.pack_parameter)),
    type_parameter: $ => seq('constexpr', 'type', field('name', $.identifier)),
    pack_parameter: $ => seq('auto', '...', field('name', $.identifier)),

    // if constexpr (T == f32) { ... }
    if_statement: ($, original) => choice(
      original,
      prec.right(seq(
        'if',
        'constexpr',
        field('condition', choice($.parenthesized_expression, prec.dynamic(-2, $.constexpr_condition))),
        field('consequence', $.statement),
        optional(field('alternative', $.else_clause)),
      )),
    ),

    // for constexpr (usize i = 0; i < countof(rest); i++) { ... }: C's for, run at compile time
    for_statement: ($, original) => choice(
      original,
      seq('for', 'constexpr', '(', $._for_statement_body, ')', field('body', $.statement)),
    ),

    // defer c::close(fd); and defer { ... } in a module (C2y's defer TS). The keyword is a token of its own at the start
    // of a statement only: `int defer = 1;` in a file with no module line is still a declaration.
    defer_statement: $ => seq(alias(token(prec(1, /defer/)), 'defer'), $.statement),
    _non_case_statement: ($, original) => choice(original, prec.dynamic(2, $.defer_statement)),

    // auto args = {a, b}; makes a list (C23 leaves `auto x = {...}` invalid, so a declaration cannot read it)
    list_declaration: $ => seq('auto', field('name', $.identifier), '=', field('value', $.initializer_list), ';'),
    // auto x = expr; (C23's type inference; there is no type for a declaration to read), also constexpr auto f = fields(T)[i];
    auto_declaration: $ => seq(
      optional('constexpr'),
      'auto',
      field('name', $.identifier),
      '=',
      field('value', $.expression),
      ';',
    ),
    declaration: ($, original) => choice(original, prec.dynamic(3, $.list_declaration), prec.dynamic(2, $.auto_declaration)),

    // args = {args..., x}; assigns a list (auto args = {...} is a declaration, which C already reads)
    list_assignment: $ => seq(field('left', $.identifier), '=', field('right', $.initializer_list)),
    expression_statement: ($, original) => choice(original, prec.dynamic(2, seq($.list_assignment, ';'))),
    // if constexpr (T == const c::V2 *): a condition that compares a type parameter with a type that is no expression.
    constexpr_condition: $ => seq(
      '(',
      $._constexpr_term,
      repeat1(seq(choice('==', '!=', '&&', '||'), $._constexpr_term)),
      ')',
    ),
    _constexpr_term: $ => choice(prec.dynamic(1, $.expression), $.type_descriptor),

    // arena.push(&a, f32) and SMITH_CHECK(a, ==, b): a type or an operator where an argument goes (a macro's).
    argument_list: $ => seq(
      '(',
      optional(seq(
        $._argument,
        repeat(seq(',', $._argument)),
      )),
      ')',
    ),
    // rest...: the arguments of a list, expanded
    pack_expansion: $ => seq($.expression, '...'),
    // (a call such as `c::strlen(path)` is an expression, not a type with a function declarator)
    _argument: $ => choice(
      prec.dynamic(3, $.expression),
      prec.dynamic(3, $.pack_expansion),
      $.designated_list,
      $.compound_statement,
      prec.dynamic(-1, $.type_descriptor),
      alias(choice('==', '!=', '<', '>', '<=', '>=', '&&', '||'), $.operator),
    ),

    // `enum Mode : unsigned char { ... }`
    // C23 attributes after the keyword: typedef struct [[clang::annotate("smith::capability")]] SmithOp { ... } SmithOp;
    struct_specifier: $ => prec.right(seq(
      'struct',
      optional($.attribute_specifier),
      repeat($.attribute_declaration),
      optional($.ms_declspec_modifier),
      choice(
        seq(
          field('name', $._type_identifier),
          field('body', optional($.field_declaration_list)),
        ),
        field('body', $.field_declaration_list),
        field('body', $.tuple_field_list),
      ),
      optional($.attribute_specifier),
    )),

    // struct { i32, f32 }: a struct whose fields have no names, a list of types (issue #96)
    // (a type name, stars and qualifiers: the parts of a field's type before its name, so that the parser reads `u8 *a;`
    // and `u8 *,` with the same states up to the name or the comma)
    tuple_field_list: $ => seq('{', commaSep1($.tuple_field), '}'),
    tuple_field: $ => seq(
      $._declaration_specifiers,
      repeat(seq('*', repeat($.type_qualifier))),
    ),

    // A run-time array (issue #82): `i32 xs[:]` declares a slice, `xs[a:b]`, `xs[:b]`, `xs[a:]` and `xs[:]` slice one
    // (`xs[a:b:2]`, a step, is an error of the lowering and parses). Issue #126: the brackets take C's qualifiers,
    // `xs[const :]` and `xs[const restrict :]` (`static` and `volatile` parse and are errors of the lowering).
    array_declarator: ($, original) => choice(original, prec(1, seq(
      field('declarator', $._declarator), '[', repeat(choice($.type_qualifier, 'static')), field('size', ':'), ']'))),
    array_field_declarator: ($, original) => choice(original, prec(1, seq(
      field('declarator', $._field_declarator), '[', repeat(choice($.type_qualifier, 'static')), field('size', ':'), ']'))),
    array_type_declarator: ($, original) => choice(original, prec(1, seq(
      field('declarator', $._type_declarator), '[', repeat(choice($.type_qualifier, 'static')), field('size', ':'), ']'))),
    abstract_array_declarator: ($, original) => choice(original, prec(1, seq(
      field('declarator', optional($._abstract_declarator)), '[', repeat(choice($.type_qualifier, 'static')), field('size', ':'), ']'))),
    subscript_expression: ($, original) => choice(original, prec(C.PREC.SUBSCRIPT, seq(
      field('argument', $.expression), '[', field('index', $.slice_range), ']'))),
    slice_range: $ => choice(
      seq($.expression, ':', optional($.expression), optional(seq(':', $.expression))),
      seq(':', optional($.expression)),
    ),

    union_specifier: $ => prec.right(seq(
      'union',
      repeat($.attribute_declaration),
      optional($.ms_declspec_modifier),
      choice(
        seq(
          field('name', $._type_identifier),
          field('body', optional($.field_declaration_list)),
        ),
        field('body', $.field_declaration_list),
      ),
      optional($.attribute_specifier),
    )),

    enum_specifier: $ => seq(
      'enum',
      repeat($.attribute_declaration),
      choice(
        seq(
          field('name', $._type_identifier),
          optional(seq(':', field('underlying_type', choice($.primitive_type, $.sized_type_specifier)))),
          field('body', optional(choice($.enumerator_list, $.payload_variant_list))),
        ),
        seq(
          optional(seq(':', field('underlying_type', choice($.primitive_type, $.sized_type_specifier)))),
          field('body', choice($.enumerator_list, $.payload_variant_list)),
        ),
      ),
      optional($.attribute_specifier),
    ),

    // `enum { Circle circle; Rect rect; empty; }`: the variants of an enum with payloads are written as union members, or
    // a name alone
    payload_variant_list: $ => prec.dynamic(-3, seq('{', repeat1(choice($.field_declaration, $.payload_variant)), '}')),
    payload_variant: $ => seq(field('name', $.identifier), ';'),

    // _Atomic(SmithOp *)
    atomic_type_specifier: $ => seq('_Atomic', '(', $.type_descriptor, ')'),

    // unsigned _BitInt(128)
    bit_int_type: $ => seq(optional(choice('signed', 'unsigned')), '_BitInt', '(', $.expression, ')'),

    // { #embed "file" }
    // Directives can sit between the items (`#embed`, or `#if` around the last of them).
    initializer_list: $ => seq(
      '{',
      optional($._directives),
      optional(seq(
        $._initializer_item,
        repeat(seq(optional($._directives), ',', optional($._directives), $._initializer_item)),
        optional($._directives),
        optional(seq(',', optional($._directives))),
      )),
      '}',
    ),
    // {args..., x}: the elements of a list expanded in a list
    _initializer_item: $ => choice($.initializer_pair, $.expression, prec.dynamic(3, $.pack_expansion), $.initializer_list),
    _directives: $ => repeat1($.preproc_call),

    // #if __has_include("lib/io.c")
    _preproc_expression: ($, original) => choice(original, $.string_literal, $.system_lib_string),
  },
});
