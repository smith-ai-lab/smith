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

// `smith build` puts the pinned tree-sitter-c here before it generates the parser.
const C = require('./.vendor/tree-sitter-c/grammar');

const sized = ['U8', 'U16', 'U32', 'U64', 'I8', 'I16', 'I32', 'I64', 'Usize', 'Isize', 'F16', 'F32', 'F64'];
// Words .smith gives a meaning only where its rules say so; an ordinary name elsewhere (and where the grammar could
// take either, it takes the .smith meaning: prec.dynamic below).
function commaSep(rule) { return optional(commaSep1(rule)); }
function commaSep1(rule) { return seq(rule, repeat(seq(',', rule))); }

const soft = ['module', 'import', 'export', 'Type', 'fn'];

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
    [$.storage_class_specifier, $.range_variable, $.list_declaration, $.auto_declaration],
    [$._declarator, $._declaration_declarator],
    [$.storage_class_specifier, $.range_variable],
    [$.type_qualifier, $.auto_declaration],
    [$.storage_class_specifier, $.element_parameter],
    [$.type_qualifier, $.element_parameter],
    [$.type_definition, $._declaration_modifiers, $.attributed_statement, $.export_declaration],
    [$.type_definition, $._declaration_modifiers],
    [$.pointer_field_declarator, $.tuple_field],
    [$.type_specifier, $.payload_variant],
    [$.field_designator, $.variant_expression],
    [$.call_expression, $.trailing_call_expression],
    [$.expression, $.lambda_capture_list],
    [$.expression, $.fn_type, $.fn_lambda],
    [$.expression, $.fn_lambda],
    [$.macro_type_specifier, $.type_call],
    [$.type_specifier, $.expression, $._new_type],
    [$.array_type, $.array_literal, $.lambda_capture_list],
    [$.array_type, $.array_literal],
    [$.type_specifier, $.expression, $.macro_type_specifier, $.type_call],
    [$.expression, $.dotted_type],
    [$.expression, $.type_call],
    [$.expression, $._new_type],
    [$.binary_expression, $.fn_lambda],
    [$.subscript_designator, $.array_literal],
    [$.enum_specifier, $.layout_enum],
    [$.dotted_path, $.module_path],
    [$.array_literal, $.lambda_capture_list],
    [$.type_specifier, $.layout_import, $.import_declaration],
    [$.type_qualifier, $.layout_const_declaration],
  ]),

  // The layout tokens of the syntax of #1428 come from scanner.c: they have no width and are returned only where a
  // rule below asks for them.
  externals: ($, original) => original.concat([$._indent, $._dedent, $._newline]),

  rules: {
    // `#` and a space (or `##`) starts a comment in the syntax of #1428; `#word` stays a directive of C.
    comment: ($, original) => choice(original, token(/#[ \t#][^\n]*/)),

    // ---- The syntax of #1428 (layout blocks). Every rule here is new-syntax only; the rules of C stay while .c and
    // .h files parse with this grammar.

    // [pub] fn name[T, const n: usize](a: A, mut b: B = 1; kw: K) -> R: and its block
    fn_declaration: $ => seq(
      optional('pub'),
      'fn', field('name', $.identifier),
      optional($.generic_parameters),
      field('parameters', $.fn_parameters),
      optional(seq('->', field('type', $._new_type))),
      choice(field('body', $.layout_block), $._newline)),
    generic_parameters: $ => seq('[', commaSep1(seq(optional('const'), $.identifier, optional(seq(':', $._new_type)))), ']'),
    fn_parameters: $ => seq('(', commaSep($._any_parameter), optional(seq(';', commaSep($._any_parameter))), ')'),
    _any_parameter: $ => choice($._fn_parameter, seq($.identifier, ':', $._new_type, '...'), seq($.identifier, '...')),
    _fn_parameter: $ => seq(optional('mut'), $.identifier, ':', $._new_type, optional(seq('=', $.expression))),

    // The types: a name, ?T, *T, *mut T, [N]T, []T, []mut T, [,]T, [3, 3]T, (A, B), (n: A), fn(A) -> R, Name(T)
    _new_type: $ => choice(
      alias($.identifier, $.type_identifier),
      alias('void', $.type_identifier),
      $.dotted_type,
      $.optional_type,
      $.pointer_type,
      $.array_type,
      $.tuple_type,
      $.fn_type,
      $.type_call),
    dotted_type: $ => prec.left(seq(alias($.identifier, $.type_identifier), repeat1(seq('.', alias($.identifier, $.type_identifier))))),
    optional_type: $ => prec.right(seq('?', $._new_type)),
    pointer_type: $ => prec.right(seq('*', optional('mut'), $._new_type)),
    array_type: $ => prec.right(seq('[', optional($.expression), repeat(seq(',', optional($.expression))), ']', optional('mut'), $._new_type)),
    tuple_type: $ => seq('(', $.tuple_type_item, repeat1(seq(',', $.tuple_type_item)), optional(','), ')'),
    tuple_type_item: $ => seq(optional(seq($.identifier, ':')), $._new_type),
    fn_type: $ => prec.right(seq(choice('fn', 'fn_once'), '(', commaSep($._new_type), ')', optional(seq('->', $._new_type)))),
    type_call: $ => seq(alias($.identifier, $.type_identifier), '(', commaSep1(choice($._new_type, $.expression)), ')'),

    // The body after a `:`: indented lines, or one statement on the same line
    layout_block: $ => seq(':', choice(
      seq($._indent, repeat1($._layout_statement), $._dedent),
      $._layout_simple_statement)),
    _layout_statement: $ => choice($._layout_simple_statement, $._layout_compound_statement),
    _layout_simple_statement: $ => seq(choice(
      $.layout_return_statement, $.layout_let_declaration, $.layout_const_declaration, $.layout_defer_statement,
      $.layout_break_statement, $.layout_continue_statement, $.layout_expression_statement), $._newline),
    _layout_compound_statement: $ => choice(
      $.layout_if_statement, $.layout_while_statement, $.layout_for_statement, $.layout_match_statement,
      $.layout_block_call_statement, $.layout_let_block),
    layout_return_statement: $ => seq('return', optional($.expression)),
    layout_break_statement: $ => 'break',
    layout_continue_statement: $ => 'continue',
    layout_expression_statement: $ => choice($.expression, $.try_expression),
    layout_defer_statement: $ => seq(alias(token(prec(1, /defer|errdefer/)), 'defer'), choice($.expression, $.try_expression)),
    // let [mut] x: T = v   and   let (q, mut r) = v   and   const n: T = v
    layout_let_declaration: $ => seq('let', $.layout_pattern, optional(seq(':', $._new_type)), '=', choice($.expression, $.try_expression)),
    layout_const_declaration: $ => seq('const', $.identifier, optional(seq(':', $._new_type)), '=', $.expression),
    // let x: T = if c: ... else: ...  (a value that ends with a block, so no line end after it)
    layout_let_block: $ => seq('let', $.layout_pattern, optional(seq(':', $._new_type)), '=', $.layout_if_statement),
    layout_block_call_statement: $ => seq(choice($.call_expression, $.identifier), $.layout_block),
    layout_if_statement: $ => seq(
      'if', choice(field('condition', $.expression), seq('let', $.layout_pattern, '=', field('value', $.expression))),
      field('consequence', $.layout_block),
      optional(seq('else', field('alternative', choice($.layout_block, $.layout_if_statement))))),
    layout_while_statement: $ => seq(
      'while', choice(field('condition', $.expression), seq('let', $.layout_pattern, '=', field('value', $.expression))),
      field('body', $.layout_block)),
    layout_for_statement: $ => seq(
      'for', commaSep1($.layout_pattern), 'in', field('range', $.expression), field('body', $.layout_block)),
    layout_match_statement: $ => seq('match', field('value', $.expression), ':', $._indent, repeat1($.layout_match_arm), $._dedent),
    layout_match_arm: $ => seq(commaSep1($.layout_pattern), field('body', $.layout_block)),

    // Patterns: a name, `_`, `mut x`, a literal, `.variant` with payload names, a tuple of patterns
    layout_pattern: $ => choice(
      seq(optional('mut'), $.identifier),
      $.number_literal, $.string_literal, $.char_literal, $.true, $.false,
      seq($.variant_expression, optional(seq('(', commaSep($.layout_pattern), ')'))),
      seq('(', commaSep1($.layout_pattern), ')')),

    // struct Point: / enum Color(u8): and their members, one per line
    layout_struct: $ => seq(optional('pub'), 'struct', field('name', $.identifier), optional($.generic_parameters), ':',
      $._indent, repeat1($.layout_field), $._dedent),
    layout_field: $ => seq(optional('pub'), $.identifier, ':', $._new_type, optional(seq('=', $.expression)), $._newline),
    layout_enum: $ => seq(optional('pub'), 'enum', field('name', $.identifier), optional(seq('(', $._new_type, ')')), ':',
      $._indent, repeat1($.layout_variant), $._dedent),
    layout_variant: $ => seq($.identifier, optional(seq('(', commaSep1($._new_type), ')')), optional(seq('=', $.expression)), $._newline),

    // import std.math   import std.math as m   from std.math import sin, cos   import c <stdio.h>   pub import a.b
    layout_import: $ => seq(optional('pub'), choice(
      seq('import', $.dotted_path, optional(seq('as', $.identifier))),
      seq('import', $.identifier, $.system_lib_string, optional(seq('as', $.identifier))),
      seq('from', $.dotted_path, 'import', commaSep1(seq($.identifier, optional(seq('as', $.identifier)))))), $._newline),
    dotted_path: $ => seq($.identifier, repeat(seq('.', $.identifier))),

    // @inline   @c_name("x")   @when(target.os == .macos)   @serial.rename(to = "id"): one line before a declaration
    layout_attribute: $ => seq('@', $.dotted_path, optional($.argument_list), $._newline),

    // let / const at the top level (a global)
    layout_global: $ => seq(optional('pub'), choice($.layout_let_declaration, $.layout_const_declaration), $._newline),

    // x.*  and  t.0.1
    tuple_index: $ => token(prec(2, /\.[0-9]+/)),

    // [1, 2, 3] and [] and [[1, 2], [3, 4]]
    array_literal: $ => prec.dynamic(-10, seq('[', commaSep($.expression), optional(','), ']')),

    // fn(x: i32) -> i32: x + 1
    fn_lambda: $ => prec.dynamic(1, seq('fn', '(', commaSep($._fn_parameter), ')', optional(seq('->', $._new_type)), ':', $.expression)),

    _top_level_item: ($, original) => choice(
      original,
      $.fn_declaration,
      $.layout_struct,
      $.layout_enum,
      $.layout_import,
      $.layout_attribute,
      $.layout_global,
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
      $.lambda_type,
      prec.dynamic(-2, seq(field('name', choice($.identifier, $.scoped_identifier)), '(', commaSep1(field('argument', $.expression)), ')')),
    ),

    // x.[f] and x->[f]: the member named by a field f of fields(T)
    field_expression: $ => seq(
      prec(C.PREC.FIELD, seq(field('argument', $.expression), field('operator', choice('.', '->')))),
      field('field', choice($._field_identifier, seq('[', $.expression, ']'), '*')),
    ),
    tuple_field_expression: $ => prec(C.PREC.FIELD, seq(field('argument', $.expression), $.tuple_index)),

    // return {a, b}; in a function that returns a tuple
    return_statement: ($, original) =>
      choice(original, seq('return', $.initializer_list, ';'), seq('return', $.enum_specifier, ';'),
        seq('return', $.try_expression, ';')),

    // return {.ok = try f()}; (a variant's payload)
    initializer_pair: ($, original) => choice(original, seq(
      field('designator', repeat1(choice($.subscript_designator, $.field_designator, $.subscript_range_designator))),
      '=', field('value', $.try_expression))),

    // int x = try f(); and x = try f();
    init_declarator: ($, original) =>
      choice(original,
        seq(field('declarator', $._declarator), '=', field('value', $.try_expression)),
        // constexpr type Handler = Response (*)(Request *);  and  constexpr type Block = u8[16];  (a type value
        // written as a full C type, in the abstract-declarator form of a cast)
        seq(field('declarator', $._declarator), '=', field('value', prec.dynamic(-3, $.type_descriptor))),
        // register long a0 asm("x0") = fd; (a GNU asm label before the initializer)
        seq(field('declarator', $._declarator), $.gnu_asm_expression, '=',
          field('value', choice($.initializer_list, $.expression, $.try_expression)))),
    assignment_expression: ($, original) => choice(original, prec.right(C.PREC.ASSIGNMENT, seq(
      field('left', $._assignment_left_expression), field('operator', '='), field('right', $.try_expression)))),

    // asm("x0"), asm(REGISTER) and asm volatile("bl " SYMBOL(puts) : ...): the text may be a macro's name or a macro call
    // between string pieces (the grammar reads a file before the preprocessor).
    gnu_asm_expression: $ => prec(15, seq(
      choice('asm', '__asm__', '__asm'),
      repeat($.gnu_asm_qualifier),
      '(',
      field('assembly_code', $._asm_text),
      optional(seq(
        field('output_operands', $.gnu_asm_output_operand_list),
        optional(seq(
          field('input_operands', $.gnu_asm_input_operand_list),
          optional(seq(
            field('clobbers', $.gnu_asm_clobber_list),
            optional(field('goto_labels', $.gnu_asm_goto_list)))))))),
      ')')),
    _asm_text: $ => repeat1(choice($.string_literal, $.identifier, seq($.identifier, $.argument_list))),

    // Apple blocks: ^(int x) { ... }
    block_expression: $ => prec(1, seq('^', optional(field('type', $.type_specifier)), optional($.parameter_list), $.compound_statement)),

    // [x, y](i32 a) -> i32 { ... }, [](i32 a) { ... } and [] { ... } (issue #316): `[` starting an expression is
    // invalid C, so a lambda never reads as C.
    lambda_expression: $ => prec(1, seq(
      $.lambda_capture_list,
      optional($.parameter_list),
      optional(seq('->', field('type', $.type_specifier))),
      field('body', $.compound_statement))),
    lambda_capture_list: $ => seq('[', commaSep($.identifier), ']'),

    // f(args) { ... } and f(args) [](Person *a, Person *b) { ... }: a block or a lambda after a call's `)` is its
    // last argument.
    trailing_call_expression: $ => prec.dynamic(-1, prec(C.PREC.CALL, seq(
      field('function', $.expression), field('arguments', $.argument_list),
      choice($.compound_statement, $.lambda_expression)))),

    // Lambda(bool (T *, T *)): the type of a parameter that takes a lambda, with the lifetime attributes before it
    lambda_type: $ => prec.dynamic(2, seq(
      alias(token(prec(1, /Lambda/)), 'Lambda'), '(', $.type_descriptor, ')')),

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
      $.lambda_expression,
      $.trailing_call_expression,
      $.variant_expression,
      $.array_literal,
      $.index_list,
      $.fn_lambda,
      $.tuple_field_expression,
      alias(choice(...sized), $.identifier),
      alias(choice(...soft), $.identifier),
    ),

    // `draw({.circle = {.radius = 1}})`: braces that begin with a designator are no block: an enum's payload, as an argument
    designated_list: $ => seq('{', $.field_designator, '=', choice($.expression, $.initializer_list, $.try_expression),
      repeat(seq(',', $._initializer_item)), optional(','), '}'),

    // A sized type's name is a keyword where a type could begin (the first word in `enum { ... }` may be a payload's
    // type), and still an enumerator's name there (`enum { i64 = 7, u8 }` in plain C).
    enumerator: $ => seq(
      field('name', choice($.identifier, alias(choice(...sized), $.identifier))),
      optional(seq('=', field('value', $.expression))),
    ),

    // `try f()` in a module: the failure of a result or an option returns from the function (issue #211). A keyword
    // where an expression begins; `int try = 1;` in a file with no module line is still a declaration.
    try_expression: $ => prec.dynamic(2, prec.right(20, seq(alias(token(prec(1, /try/)), 'try'), $.call_expression))),

    // `.empty`: a variant of an enum with payloads, in the type the context gives (`Shape s = .empty;`, `case .empty:`)
    variant_expression: $ => prec.dynamic(-1, seq('.', field('name', $.identifier))),

    // module = 0; (an assignment's left side is a short list of its own)
    _assignment_left_expression: ($, original) => choice(
      original,
      $.tuple_field_expression,
      $.index_list,
      $.scoped_identifier,
      alias(choice(...sized), $.identifier),
      alias(choice(...soft), $.identifier),
    ),

    type_specifier: ($, original) => choice(
      original,
      $.atomic_type_specifier,
      $.bit_int_type,
      alias(choice('module', 'import', 'Type'), $.type_identifier),
    ),

    _type_identifier: ($, original) => choice(original, $.scoped_type_identifier),

    // Apple blocks: __block int x;
    storage_class_specifier: ($, original) => choice(original, '__block'),

    // export on a declaration, a function definition, a typedef or a tag: only where a module declares (file scope),
    // so a field or a variable called export stays one.
    // [[std::c_name("Name")]] export ...: attributes before `export` give the export its C name.
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

    // C23 attributes before a typedef: [[std::returns_internal]] typedef char *(*Describe)(int);
    type_definition: $ => seq(
      choice(optional('__extension__'), repeat1($.attribute_declaration)),
      'typedef',
      $._type_definition_type,
      $._type_definition_declarators,
      repeat($.attribute_specifier),
      ';',
    ),

    // T max(constexpr type T, T a, T b)
    // and, for argument lists, auto... rest
    // and `auto` as the element of a declarator (issue #193): const auto xs[:], auto *p, auto xs[4]
    parameter_declaration: ($, original) => choice(
      original,
      prec.dynamic(1, $.type_parameter),
      prec.dynamic(1, $.pack_parameter),
      prec.dynamic(2, $.element_parameter),
    ),
    element_parameter: $ => seq(optional('const'), 'auto', field('declarator', choice($.pointer_declarator, $.array_declarator))),
    type_parameter: $ => seq('constexpr', 'Type', field('name', $.identifier)),
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
    // and the range form (issue #243): for (i32 x : xs) { ... }, for (usize i, auto x : xs) { ... }, also with constexpr
    for_statement: ($, original) => choice(
      original,
      seq('for', 'constexpr', '(', $._for_statement_body, ')', field('body', $.statement)),
      seq('for', optional('constexpr'), '(', $.range_head, ')', field('body', $.statement)),
    ),
    range_head: $ => seq(
      repeat(seq($.range_variable, ',')), // an index; more than one is smith's error, not a syntax error
      $.range_variable,
      ':',
      field('range', $.expression),
    ),
    range_variable: $ => seq(choice($._declaration_specifiers, 'auto'), field('declarator', $._declarator)),

    // defer c::close(fd); and defer { ... } in a module (C2y's defer TS). The keyword is a token of its own at the start
    // of a statement only: `int defer = 1;` in a file with no module line is still a declaration.
    defer_statement: $ => seq(alias(token(prec(1, /defer/)), 'defer'), $.statement),
    _non_case_statement: ($, original) => choice(original, prec.dynamic(2, $.defer_statement), prec.dynamic(2, $.errdefer_statement)),

    // errdefer mem::free(b); runs like defer, but only when the function fails (issue #211)
    errdefer_statement: $ => seq(alias(token(prec(1, /errdefer/)), 'errdefer'), $.statement),

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
    expression_statement: ($, original) => choice(original, prec.dynamic(2, seq($.list_assignment, ';')),
      prec.dynamic(2, seq($.try_expression, ';'))),
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
      prec.dynamic(-1, alias(choice($.optional_type, $.pointer_type, $.array_type, $.fn_type), $.type_form)),
      prec.dynamic(-1, $.type_descriptor),
      alias(choice('==', '!=', '<', '>', '<=', '>=', '&&', '||'), $.operator),
    ),

    // `enum Mode : unsigned char { ... }`
    // C23 attributes after the keyword: typedef struct [[clang::annotate("std::capability")]] SmithOp { ... } SmithOp;
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
    // A slice written bare as a function's result, `i32[:] f(...) {...}` and `T[:] window(constexpr type T, ...)`
    function_definition: ($, original) => choice(original, seq(
      $._declaration_specifiers, '[', ':', ']',
      field('declarator', $._declarator),
      field('body', $.compound_statement),
    )),
    subscript_expression: ($, original) => choice(original, prec(C.PREC.SUBSCRIPT, seq(
      field('argument', $.expression), '[', field('index', $.slice_range), ']'))),
    // g[i, j]: a block's index, one index for each dimension
    index_list: $ => prec(C.PREC.SUBSCRIPT, seq(
      field('argument', $.expression), '[', $.expression, repeat1(seq(',', $.expression)), ']')),
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
