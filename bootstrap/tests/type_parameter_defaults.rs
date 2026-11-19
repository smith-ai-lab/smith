//! A type parameter may have a default, which a use that leaves the
//! parameter out gets.

use smith::diag::Error;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::{lines_added, pos_after, shifted, with_imports};

pub const ONCE: &[Test] = &[
    (
        "a_use_that_leaves_a_parameter_out_is_the_program_that_writes_its_default",
        a_use_that_leaves_a_parameter_out_is_the_program_that_writes_its_default,
    ),
    (
        "a_default_may_name_a_parameter_before_it",
        a_default_may_name_a_parameter_before_it,
    ),
    (
        "a_lambda_whose_parameter_nothing_decides_takes_the_default_of_a_function",
        a_lambda_whose_parameter_nothing_decides_takes_the_default_of_a_function,
    ),
    (
        "a_call_finds_the_impl_by_its_arguments_whether_the_trait_has_defaults_or_not",
        a_call_finds_the_impl_by_its_arguments_whether_the_trait_has_defaults_or_not,
    ),
    (
        "a_parameter_with_no_default_after_one_with_a_default_is_one_error_at_the_parameter",
        a_parameter_with_no_default_after_one_with_a_default_is_one_error_at_the_parameter,
    ),
    (
        "a_default_that_names_a_parameter_after_it_is_one_error_at_the_name",
        a_default_that_names_a_parameter_after_it_is_one_error_at_the_name,
    ),
    (
        "a_use_with_more_arguments_than_parameters_or_fewer_than_those_without_a_default_is_one_error",
        a_use_with_more_arguments_than_parameters_or_fewer_than_those_without_a_default_is_one_error,
    ),
];

/// The whole number types.
const INTS: [&str; 8] = ["i8", "i16", "i32", "Int", "Byte", "u16", "u32", "u64"];

fn one_error(source: &str) -> Error {
    let found = lower(source.as_bytes()).expect_err("an error");
    assert_eq!(found.len(), 1, "{found:?} in {source}");
    found.into_iter().next().expect("one")
}

fn printed(source: &str) -> Vec<u8> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {source}"));
    scalar::run(&program).stdout
}

/// The program is the same program when the default is written where it is
/// left out, and prints the same.
fn same(left_out: &str, written: &str) {
    let a = lower(left_out.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {left_out}"));
    let b = lower(written.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {written}"));
    assert!(a == b, "{left_out}\ndiffers from\n{written}");
    assert_eq!(printed(left_out), printed(written), "{left_out}");
}

/// A struct, an enum, and a trait leave the last parameter out, for each
/// type the default is.
fn a_use_that_leaves_a_parameter_out_is_the_program_that_writes_its_default() {
    generate::check(301, 8, |g| *g.pick(&INTS), |&ty| {
        // `last` is what the uses write for the last parameter, or nothing.
        let program = |last: &str, maybe: &str| {
            with_imports(&format!(
                "struct Box[A, B = {ty}] {{\n  first: A\n  second: B\n}}\n\n\
                 enum Maybe[T = {ty}] {{\n  some: T\n  none\n}}\n\n\
                 trait Make[A, B = {ty}] {{\n  fn make(a: A) -> B\n}}\n\n\
                 impl Make[str{last}] {{\n  fn make(a: str) -> {ty} {{ 7 }}\n}}\n\n\
                 fn count(m: Maybe{maybe}) -> {ty} {{\n  match m {{\n    .some(x) {{ x }}\n    \
                 .none {{ 0 }}\n  }}\n}}\n\n\
                 fn main() ! console, alloc {{\n  b: Box[str{last}] = Box(first: \"a\", second: 3)\n  \
                 n = count(.some(4))\n  m: {ty} = Make.make(\"x\")\n  print(\"{{b.second}} {{n}} {{m}}\")\n}}\n"
            ))
        };
        let (left_out, written) = (program("", ""), program(&format!(", {ty}"), &format!("[{ty}]")));
        same(&left_out, &written);
        assert_eq!(printed(&left_out), b"3 4 7\n");
    });
}

/// A default that names a parameter before it is that parameter's type.
fn a_default_may_name_a_parameter_before_it() {
    generate::check(302, 8, |g| *g.pick(&INTS), |&ty| {
        let program = |args: &str| {
            with_imports(&format!(
                "struct Pair[A, B = A] {{\n  first: A\n  second: B\n}}\n\n\
                 fn sum(p: Pair[{ty}{args}]) -> {ty} {{ wrapping_add(p.first, p.second) }}\n\n\
                 fn main() ! console, alloc {{\n  p: Pair[{ty}{args}] = Pair(first: 1, second: 2)\n  \
                 print(\"{{sum(p)}}\")\n}}\n"
            ))
        };
        same(&program(""), &program(&format!(", {ty}")));
        assert_eq!(printed(&program("")), b"3\n");
    });
}

/// A function's parameter that no argument settles is its default, as the
/// types of a lambda's parameters show.
fn a_lambda_whose_parameter_nothing_decides_takes_the_default_of_a_function() {
    generate::check(303, 8, |g| *g.pick(&INTS), |&ty| {
        let program = |lambda: &str| {
            with_imports(&format!(
                "fn pick[T = {ty}](f: fn(T) -> u64) -> u64 {{ 1 }}\n\n\
                 fn main() ! console, alloc {{\n  r = pick {lambda}\n  print(\"{{r}}\")\n}}\n"
            ))
        };
        same(&program("fn(v) { 2 }"), &program(&format!("fn(v: {ty}) {{ 2 }}")));
        assert_eq!(printed(&program("fn(v) { 2 }")), b"1\n");
    });
}

/// A default stands in where a type is written with the parameter left out,
/// and nowhere else: a call finds the `impl` by the types of its arguments,
/// so a trait with defaults answers every call that the same trait written
/// in full answers, with `impl`s written in full or with parameters left out.
fn a_call_finds_the_impl_by_its_arguments_whether_the_trait_has_defaults_or_not() {
    generate::check(307, 8, |g| (*g.pick(&INTS), *g.pick(&INTS)), |&(ty, other)| {
        let program = |trait_head: &str, first: &str, second: &str| {
            with_imports(&format!(
                "trait Mix{trait_head} {{\n  fn mix(a: A, b: B) -> C\n}}\n\n\
                 impl Mix{first} {{\n  fn mix(a: {ty}, b: {ty}) -> {ty} {{ b }}\n}}\n\n\
                 impl Mix{second} {{\n  fn mix(a: str, b: {other}) -> {other} {{ b }}\n}}\n\n\
                 fn main() ! console, alloc {{\n  x: {ty} = 1\n  y: {other} = 2\n  \
                 print(\"{{mix(x, x)}} {{mix(\"t\", y)}}\")\n}}\n"
            ))
        };
        let left_out = program("[A, B = A, out C = A]", &format!("[{ty}]"), &format!("[str, {other}, {other}]"));
        let written = program("[A, B, out C]", &format!("[{ty}, {ty}, {ty}]"), &format!("[str, {other}, {other}]"));
        same(&left_out, &written);
        assert_eq!(printed(&left_out), b"1 2\n");
    });
}

/// A parameter after one with a default has to have one too: the error is at
/// the parameter, and names the one before it.
fn a_parameter_with_no_default_after_one_with_a_default_is_one_error_at_the_parameter() {
    let places = |g: &mut Gen| (*g.pick(&INTS), *g.pick(&["struct", "enum"]));
    generate::check(304, 12, places, |&(ty, kind)| {
        let before = format!("{kind} S[A = {ty}, ");
        let made = if kind == "struct" { "S(a: 1, b: 2)" } else { "S.a(1)" };
        let written = format!(
            "{before}B] {{\n  a: A\n  b: B\n}}\n\n\
             fn main() {{\n  s: S[{ty}, {ty}] = {made}\n}}\n"
        );
        let source = with_imports(&written);
        let error = one_error(&source);
        assert_eq!(error.pos, shifted(pos_after(&before), lines_added(&written, &source)), "{error}");
        assert!(error.message.contains("`A`"), "{error}");
    });
}

/// A default may name only the parameters before it.
fn a_default_that_names_a_parameter_after_it_is_one_error_at_the_name() {
    generate::check(305, 8, |g| *g.pick(&INTS), |&ty| {
        let before = "struct S[A = ";
        let written = format!(
            "{before}B, B = {ty}] {{\n  a: A\n  b: B\n}}\n\n\
             fn main() {{\n  s: S[{ty}, {ty}] = S(a: 1, b: 2)\n}}\n"
        );
        let source = with_imports(&written);
        let error = one_error(&source);
        assert_eq!(error.pos, shifted(pos_after(before), lines_added(&written, &source)), "{error}");
        assert!(error.message.contains("`B`"), "{error}");
    });
}

/// A type written with more arguments than it has parameters, or with fewer
/// than those that have no default, is one error at the type.
fn a_use_with_more_arguments_than_parameters_or_fewer_than_those_without_a_default_is_one_error() {
    let counts = |g: &mut Gen| (*g.pick(&INTS), g.below(2) == 0);
    generate::check(306, 12, counts, |&(ty, fewer)| {
        let before = "struct S[A, B = Byte] {\n  a: A\n  b: B\n}\n\nfn main() {\n  s: ";
        let written = if fewer { "S".to_string() } else { format!("S[{ty}, {ty}, {ty}]") };
        let raw = format!("{before}{written} = S(a: 1, b: 2)\n}}\n");
        let source = with_imports(&raw);
        let error = one_error(&source);
        assert_eq!(error.pos, shifted(pos_after(before), lines_added(&raw, &source)), "{error}");
        assert!(error.message.contains("`S`"), "{error}");
    });
}
