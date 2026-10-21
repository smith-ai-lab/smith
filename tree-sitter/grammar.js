/**
 * @file tree-sitter grammar for .smith: tree-sitter-c's grammar plus what .smith adds (docs/smith-lang.md, issue #11),
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
    [$.type_specifier, $.expression, $.macro_type_specifier, $.macro_invocation],
    [$.sized_type_specifier, $.enum_specifier],
    [$.expression, $.primitive_type],
    [$.module_declaration, $.type_specifier],
    [$.import_declaration, $.type_specifier],
    [$.type_parameter, $.type_specifier],
    [$.type_parameter, $.type_qualifier],
    [$._declaration_modifiers, $.attributed_statement, $.export_declaration],
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
      alias(choice(...sized), $.identifier),
      alias(choice(...soft), $.identifier),
    ),

    // module = 0; (an assignment's left side is a short list of its own)
    _assignment_left_expression: ($, original) => choice(
      original,
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
    parameter_declaration: ($, original) => choice(original, prec.dynamic(1, $.type_parameter)),
    type_parameter: $ => seq('constexpr', 'type', field('name', $.identifier)),

    // if constexpr (T == f32) { ... }
    if_statement: ($, original) => choice(
      original,
      prec.right(seq(
        'if',
        'constexpr',
        field('condition', $.parenthesized_expression),
        field('consequence', $.statement),
        optional(field('alternative', $.else_clause)),
      )),
    ),

    // arena.push(&a, f32) and SMITH_CHECK(a, ==, b): a type or an operator where an argument goes (a macro's).
    argument_list: $ => seq(
      '(',
      optional(seq(
        $._argument,
        repeat(seq(',', $._argument)),
      )),
      ')',
    ),
    // (a call such as `c::strlen(path)` is an expression, not a type with a function declarator)
    _argument: $ => choice(
      prec.dynamic(3, $.expression),
      $.compound_statement,
      prec.dynamic(-1, $.type_descriptor),
      alias(choice('==', '!=', '<', '>', '<=', '>=', '&&', '||'), $.operator),
    ),

    // `enum Mode : unsigned char { ... }`
    enum_specifier: $ => seq(
      'enum',
      choice(
        seq(
          field('name', $._type_identifier),
          optional(seq(':', field('underlying_type', choice($.primitive_type, $.sized_type_specifier)))),
          field('body', optional($.enumerator_list)),
        ),
        seq(
          optional(seq(':', field('underlying_type', choice($.primitive_type, $.sized_type_specifier)))),
          field('body', $.enumerator_list),
        ),
      ),
      optional($.attribute_specifier),
    ),

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
    _initializer_item: $ => choice($.initializer_pair, $.expression, $.initializer_list),
    _directives: $ => repeat1($.preproc_call),

    // #if __has_include("lib/io.c")
    _preproc_expression: ($, original) => choice(original, $.string_literal, $.system_lib_string),
  },
});
