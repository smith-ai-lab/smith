//! Every function of the file that is built, with a name and no type
//! parameters, and none that takes a function, is given `Function`: `name()`
//! is its name, `about()` is the comment above it, and `call(f, args)` calls it
//! with the fields of a struct that holds its arguments, which is read from
//! JSON as any struct is. The expectation of each program is what the generator
//! that wrote it chose each function to be.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_function_is_given_its_name_and_its_comment_by_function",
        a_function_is_given_its_name_and_its_comment_by_function,
    ),
    (
        "call_runs_the_function_with_the_arguments_read_from_json_and_a_default_left_out_is_the_default_part_1_of_3",
        call_part_1,
    ),
    (
        "call_runs_the_function_with_the_arguments_read_from_json_and_a_default_left_out_is_the_default_part_2_of_3",
        call_part_2,
    ),
    (
        "call_runs_the_function_with_the_arguments_read_from_json_and_a_default_left_out_is_the_default_part_3_of_3",
        call_part_3,
    ),
    (
        "a_lambda_a_generic_function_and_one_that_takes_a_function_have_no_function_and_are_each_one_error",
        a_lambda_a_generic_function_and_one_that_takes_a_function_have_no_function_and_are_each_one_error,
    ),
    (
        "a_program_that_does_not_write_function_builds_no_struct_of_arguments",
        a_program_that_does_not_write_function_builds_no_struct_of_arguments,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_function_told_by_function_prints_what_the_evaluator_says",
    every_executable_of_a_function_told_by_function_prints_what_the_evaluator_says,
)];

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// A function with some comment lines above it.
#[derive(Clone, Debug)]
struct Named {
    comment: Vec<String>,
}

fn word(g: &mut Gen) -> String {
    let words = ["Makes a number", "Twice, by default", "x", "the first"];
    g.pick(&words).to_string()
}

fn named(g: &mut Gen) -> Vec<Named> {
    (0..g.below(3) + 1)
        .map(|_| Named {
            comment: (0..g.below(3)).map(|_| word(g)).collect(),
        })
        .collect()
}

/// The program that tells the name and the comment of each function.
fn names_source(fs: &[Named]) -> String {
    let mut source = String::from("use std.bits (i32)\nuse std.console (console, print)\n\n");
    for (k, f) in fs.iter().enumerate() {
        for line in &f.comment {
            source += &format!("// {line}\n");
        }
        source += &format!("fn f{k}(x: i32) -> i32 {{ x }}\n\n");
    }
    source += "fn told[F: Function](f: F) -> str ! alloc {\n  \"{Function.name(f)}|{Function.about(f)}\"\n}\n\n";
    source += "fn main() ! console, alloc {\n";
    for k in 0..fs.len() {
        source += &format!("  print(told(f{k}))\n");
    }
    source + "}\n"
}

/// `name()` and `about()` of each function: its name, and its comment lines
/// joined by a line break, or the empty text when it has none.
fn a_function_is_given_its_name_and_its_comment_by_function() {
    generate::check(7921, 40, named, |fs| {
        let source = names_source(fs);
        let mut said = String::new();
        for (k, f) in fs.iter().enumerate() {
            said += &format!("f{k}|{}\n", f.comment.join("\n"));
        }
        assert_eq!(outcome(&source).stdout, said.into_bytes(), "for\n{source}");
    });
}

/// A function of some parameters, which of them have a default, and which of
/// them it gives.
#[derive(Clone, Debug)]
struct Callee {
    /// For each parameter, its default if it has one.
    defaults: Vec<Option<i32>>,
    /// The parameter the function gives.
    gives: usize,
    /// The value of each parameter in the JSON, if the JSON says it.
    said: Vec<Option<i32>>,
    /// Whether the JSON is broken.
    broken: bool,
}

fn callee(g: &mut Gen) -> Callee {
    let count = g.below(3) + 1;
    // The parameters with a default come last.
    let with = g.below(count + 1);
    let defaults: Vec<Option<i32>> = (0..count)
        .map(|i| (i >= count - with).then(|| g.below(100) as i32))
        .collect();
    let said = (0..count)
        .map(|i| (defaults[i].is_none() || g.below(2) == 0).then(|| g.below(100) as i32))
        .collect();
    Callee {
        defaults,
        gives: g.below(count),
        said,
        broken: g.below(4) == 0,
    }
}

impl Callee {
    fn source(&self) -> String {
        let params: Vec<String> = (self.defaults.iter().enumerate())
            .map(|(i, d)| match d {
                Some(d) => format!("a{i}: i32 = {d}"),
                None => format!("a{i}: i32"),
            })
            .collect();
        let fields: Vec<String> = (self.said.iter().enumerate())
            .filter_map(|(i, v)| v.map(|v| format!("\\\"a{i}\\\": {v}")))
            .collect();
        let text = if self.broken {
            "\\{\\\"a0\\\": ".to_string()
        } else {
            format!("\\{{{}\\}}", fields.join(", "))
        };
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\
             use std.deserialize (ReadError)\nuse std.json (FromJson, from_json)\n\n\
             fn pick({}) -> i32 {{ a{} }}\n\n\
             fn run[F, A, R](f: F, text: str) -> Option[R] ! alloc\n  \
             requires Function[F, A, R]\n  requires FromJson[A]\n  requires Discard[A]\n{{\n  \
             r: Result[A, ReadError] = from_json(text)\n  match r {{\n    \
             .ok(args) {{ Option.some(Function.call(f, args)) }}\n    .err(e) {{ Option.none }}\n  }}\n}}\n\n\
             fn show(o: Option[i32]) -> str ! alloc {{\n  match o {{\n    .some(v) {{ \"{{v}}\" }}\n    .none {{ \"none\" }}\n  }}\n}}\n\n\
             fn main() ! console, alloc {{\n  text = \"{text}\"\n  print(show(run(pick, text)))\n}}\n",
            params.join(", "),
            self.gives
        )
    }

    fn prints(&self) -> Vec<u8> {
        if self.broken {
            return b"none\n".to_vec();
        }
        let value = self.said[self.gives]
            .or(self.defaults[self.gives])
            .expect("a parameter has a value or a default");
        format!("{value}\n").into_bytes()
    }
}

/// The arguments are read from JSON as a struct of the parameters is, with a
/// parameter that has a default left out where the JSON leaves it out, `call`
/// runs the function with them, and JSON that is not read is no call.
fn call_part(part: u64) {
    generate::check_part(7922, 12, part, 3, callee, |c| {
        let source = c.source();
        assert_eq!(outcome(&source).stdout, c.prints(), "for\n{source}");
    });
}

fn call_part_1() {
    call_part(0);
}

fn call_part_2() {
    call_part(1);
}

fn call_part_3() {
    call_part(2);
}

/// A lambda has no name, a function with type parameters or one that takes a
/// function has no struct of arguments to hold, and each is one error.
fn a_lambda_a_generic_function_and_one_that_takes_a_function_have_no_function_and_are_each_one_error()
 {
    let program = |arg: &str| {
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             fn first[T](xs: List[T]) -> i32 {{ 1 }}\n\
             fn keep(f: fn(i32) -> i32) -> i32 {{ f(1) }}\n\
             fn plain(x: i32) -> i32 {{ x }}\n\n\
             fn told[F: Function](f: F) -> str {{ Function.name(f) }}\n\n\
             fn main() ! console {{\n  print(told({arg}))\n}}\n"
        )
    };
    assert!(errors(&program("plain")).is_empty());
    for (arg, message) in [
        (
            "fn(x: i32) { x }",
            "this is a lambda, and `Function` is given to a function with a name",
        ),
        (
            "first",
            "passing a function with type parameters as a value is not supported yet",
        ),
        (
            "keep",
            "passing a function that takes a function as a value is not supported yet",
        ),
    ] {
        let found = errors(&program(arg));
        assert_eq!(found.len(), 1, "{found:?} for {arg}");
        assert_eq!(found[0].message, message);
    }
}

/// The struct of the arguments is made only for a program that writes
/// `Function`, so one that does not has the structs and functions it had.
fn a_program_that_does_not_write_function_builds_no_struct_of_arguments() {
    let plain = "use std.bits (i32)\nuse std.console (console, print)\n\n\
        fn pick(x: i32, y: i32) -> i32 { y }\n\n\
        fn main() ! console, alloc {\n  print(\"{pick(1, 2)}\")\n}\n";
    let asking = format!(
        "{plain}\nfn told[F: Function](f: F) -> str {{ Function.name(f) }}\n\
         fn also() ! console {{\n  print(told(pick))\n}}\n"
    );
    let count = |source: &str| lower(source.as_bytes()).expect("a program").functions.len();
    // The program that asks is the one that grows, by the functions it asks for.
    assert!(count(&asking) >= count(plain));
    assert_eq!(outcome(plain).stdout, b"2\n");
}

/// Every executable that can run here prints what the evaluator says for the
/// names and the comments of functions.
fn every_executable_of_a_function_told_by_function_prints_what_the_evaluator_says() {
    generate::check(7923, 6, named, |fs| {
        let source = names_source(fs);
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
