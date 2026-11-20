//! A function of another module, listed by the file that is built, is given
//! `Function` as a function of the file is: its name, the comment above it, and
//! a call with its arguments read from JSON. The expectation of each program
//! is what the generator that wrote the module chose each function to be.

use smith::lower_project;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_function_of_another_module_is_given_its_name_and_its_comment_by_function",
        a_function_of_another_module_is_given_its_name_and_its_comment_by_function,
    ),
    (
        "call_runs_a_function_of_another_module_with_the_arguments_read_from_json",
        call_runs_a_function_of_another_module_with_the_arguments_read_from_json,
    ),
    (
        "a_function_of_the_standard_library_is_given_its_name_by_function",
        a_function_of_the_standard_library_is_given_its_name_by_function,
    ),
    (
        "a_module_that_the_file_does_not_write_function_for_holds_no_struct_of_arguments",
        a_module_that_the_file_does_not_write_function_for_holds_no_struct_of_arguments,
    ),
    (
        "two_modules_with_a_function_of_one_name_are_each_given_their_own_comment",
        two_modules_with_a_function_of_one_name_are_each_given_their_own_comment,
    ),
];

fn said(source: &str, modules: &[(&str, &str)]) -> String {
    let program = lower_project(source.as_bytes(), modules)
        .unwrap_or_else(|e| panic!("{e:?} for\n{source}\n{modules:?}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// A function of the module with some comment lines above it.
#[derive(Clone, Debug)]
struct Named {
    comment: Vec<String>,
}

fn named(g: &mut Gen) -> Vec<Named> {
    let words = ["Makes a number", "Twice, by default", "x", "the first"];
    (0..g.below(3) + 1)
        .map(|_| Named {
            comment: (0..g.below(3))
                .map(|_| g.pick(&words).to_string())
                .collect(),
        })
        .collect()
}

fn module_source(fs: &[Named]) -> String {
    let mut source = String::from("use std.bits (i32)\n\n");
    for (k, f) in fs.iter().enumerate() {
        for line in &f.comment {
            source += &format!("// {line}\n");
        }
        source += &format!("pub fn f{k}(x: i32) -> i32 {{ x }}\n\n");
    }
    source
}

fn a_function_of_another_module_is_given_its_name_and_its_comment_by_function() {
    generate::check(7951, 20, named, |fs| {
        let names: Vec<String> = (0..fs.len()).map(|k| format!("f{k}")).collect();
        let mut source = format!(
            "use app.fns ({})\nuse std.console (console, print)\n\n",
            names.join(", ")
        );
        source += "fn told[F: Function](f: F) -> str ! alloc {\n  \"{Function.name(f)}|{Function.about(f)}\"\n}\n\n";
        source += "fn main() ! console, alloc {\n";
        for name in &names {
            source += &format!("  print(told({name}))\n");
        }
        source += "}\n";
        let mut expected = String::new();
        for (k, f) in fs.iter().enumerate() {
            expected += &format!("f{k}|{}\n", f.comment.join("\n"));
        }
        assert_eq!(said(&source, &[("app.fns", &module_source(fs))]), expected, "{source}");
    });
}

/// The arguments are read from JSON, a default left out is the default.
fn call_runs_a_function_of_another_module_with_the_arguments_read_from_json() {
    let module = "use std.bits (i32)\n\npub fn pick(a: i32, b: i32 = 7) -> i32 { b }\n";
    let source = "use app.m (pick)\nuse std.console (console, print)\nuse std.json (from_json)\nuse std.deserialize (Deserialize, ReadError)\n\n\
fn run[F, A, R](f: F, text: str) -> str ! alloc\n  requires Function[F, A, R]\n  requires Deserialize[A]\n  requires Discard[A]\n  requires Fmt[R]\n  requires Discard[R]\n{\n  r: Result[A, ReadError] = from_json(text)\n  match r {\n    .ok(args) { \"{Function.call(f, args)}\" }\n    .err(e) { \"err\" }\n  }\n}\n\n\
fn main() ! console, alloc {\n  print(run(pick, \"\\{\\\"a\\\": 1\\}\"))\n  print(run(pick, \"\\{\\\"a\\\": 1, \\\"b\\\": 3\\}\"))\n  print(run(pick, \"\\{\\}\"))\n}\n";
    assert_eq!(said(source, &[("app.m", module)]), "7\n3\nerr\n");
}

/// A function of one name in two modules.
fn two_modules_with_a_function_of_one_name_are_each_given_their_own_comment() {
    let a = "// In a.\npub fn go(x: Int) -> Int { x }\n";
    let b = "// In b.\npub fn go(x: Int) -> Int { x }\n";
    let source = "use app.a\nuse app.b\nuse std.console (console, print)\n\n\
fn told[F: Function](f: F) -> str ! alloc {\n  Function.about(f)\n}\n\n\
fn main() ! console, alloc {\n  print(told(a.go))\n  print(told(b.go))\n}\n";
    assert_eq!(said(source, &[("app.a", a), ("app.b", b)]), "In a.\nIn b.\n");
}

/// A function of a module of the standard library that is read when it is
/// reached.
fn a_function_of_the_standard_library_is_given_its_name_by_function() {
    let source = "use std.console (console, print)\nuse std.ascii (is_digit)\n\n\
fn told[F: Function](f: F) -> str ! alloc {\n  Function.name(f)\n}\n\n\
fn main() ! console, alloc {\n  print(told(is_digit))\n}\n";
    assert_eq!(said(source, &[]), "is_digit\n");
}

/// A file that does not write `Function` is built as it was, with no struct
/// for a function of a module.
fn a_module_that_the_file_does_not_write_function_for_holds_no_struct_of_arguments() {
    let module = "pub fn go(x: Int) -> Int { x }\n";
    let source = "use app.m (go)\nuse std.console (console, print)\n\nfn main() ! console, alloc {\n  print(\"{go(1)}\")\n}\n";
    let program = lower_project(source.as_bytes(), &[("app.m", module)]).expect("builds");
    assert!(!format!("{program:?}").contains("Args"));
}
