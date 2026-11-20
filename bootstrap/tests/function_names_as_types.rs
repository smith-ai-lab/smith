//! A function's name, written where a type is expected, is the type whose one
//! value is that function. A trait is implemented for it as for any type, the
//! impl is found for that function and for no other, even one of the same
//! parameters and result, and a function given where a type parameter that a
//! trait bounds is needed settles it to that type. The expectation of each
//! program is what the generator that wrote it chose to implement.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "an_impl_for_a_function_is_found_for_that_function_and_for_no_other_of_the_same_signature",
        an_impl_for_a_function_is_found_for_that_function_and_for_no_other_of_the_same_signature,
    ),
    (
        "a_function_given_for_a_type_parameter_that_a_trait_bounds_settles_it_and_a_lambda_does_not",
        a_function_given_for_a_type_parameter_that_a_trait_bounds_settles_it_and_a_lambda_does_not,
    ),
    (
        "a_function_given_for_a_type_parameter_nothing_bounds_is_refused_as_it_was_and_a_function_type_still_takes_it",
        a_function_given_for_a_type_parameter_nothing_bounds_is_refused_as_it_was_and_a_function_type_still_takes_it,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_program_that_asks_a_function_what_it_is_prints_what_the_evaluator_says",
    every_executable_of_a_program_that_asks_a_function_what_it_is_prints_what_the_evaluator_says,
)];

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// A program of functions that all take and give an `i32`: which of them have
/// an impl of `Described`, each with a text of its own, and which are asked.
#[derive(Clone, Debug)]
struct Program {
    /// For each function, the text its impl gives, if it has an impl.
    impls: Vec<Option<String>>,
    /// The functions `main` asks, by place.
    asked: Vec<usize>,
}

fn word(g: &mut Gen) -> String {
    (0..g.below(3) + 1)
        .map(|_| (b'a' + g.below(26) as u8) as char)
        .collect()
}

fn program(g: &mut Gen) -> Program {
    let count = g.below(4) + 1;
    let impls: Vec<Option<String>> = (0..count)
        .map(|_| (g.below(3) != 0).then(|| word(g)))
        .collect();
    let asked = (0..g.below(3) + 1).map(|_| g.below(count)).collect();
    Program { impls, asked }
}

impl Program {
    fn source(&self) -> String {
        let mut text = String::from("use std.bits (i32)\nuse std.console (console, print)\n\n");
        for k in 0..self.impls.len() {
            text += &format!("fn f{k}(x: i32) -> i32 {{ x }}\n");
        }
        text += "\ntrait Described[F] {\n  fn describe(f: F) -> str\n}\n\n";
        for (k, said) in self.impls.iter().enumerate() {
            if let Some(said) = said {
                text += &format!(
                    "impl Described[f{k}] {{\n  fn describe(f: f{k}) -> str {{ \"{said}\" }}\n}}\n\n"
                );
            }
        }
        text += "fn told[F: Described](f: F) -> str {\n  Described.describe(f)\n}\n\n";
        text += "fn main() ! console {\n";
        for k in &self.asked {
            text += &format!("  print(told(f{k}))\n");
        }
        text + "}\n"
    }

    /// The functions asked that have no impl.
    fn missing(&self) -> Vec<usize> {
        let mut missing: Vec<usize> = (self.asked.iter())
            .copied()
            .filter(|k| self.impls[*k].is_none())
            .collect();
        missing.dedup();
        missing
    }

    fn prints(&self) -> Vec<u8> {
        let mut said = String::new();
        for k in &self.asked {
            said += &format!("{}\n", self.impls[*k].as_ref().expect("an impl"));
        }
        said.into_bytes()
    }
}

/// Functions of one signature each have the impl written for them, and a call
/// that asks one with no impl is refused at the argument, naming the function.
fn an_impl_for_a_function_is_found_for_that_function_and_for_no_other_of_the_same_signature() {
    generate::check(7911, 40, program, |p| {
        let source = p.source();
        let found = errors(&source);
        let missing = p.missing();
        if (p.asked.iter()).all(|k| p.impls[*k].is_some()) {
            assert!(found.is_empty(), "{found:?} for\n{source}");
            assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
        } else {
            for error in &found {
                assert!(
                    missing.iter().any(|k| error.message
                        == format!(
                            "there is no implementation of `Described` for `f{k}`, which `told` needs"
                        )),
                    "{found:?} for\n{source}"
                );
            }
            let asked_missing = p
                .asked
                .iter()
                .filter(|k| p.impls[**k].is_none())
                .count();
            assert_eq!(found.len(), asked_missing, "{found:?} for\n{source}");
        }
    });
}

/// A function given where `F: Described` is needed is that function's type,
/// and a lambda has no name to be one.
fn a_function_given_for_a_type_parameter_that_a_trait_bounds_settles_it_and_a_lambda_does_not() {
    let program = |arg: &str| {
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             fn one(x: i32) -> i32 {{ x }}\n\n\
             trait Described[F] {{\n  fn describe(f: F) -> str\n}}\n\n\
             impl Described[one] {{\n  fn describe(f: one) -> str {{ \"one\" }}\n}}\n\n\
             fn told[F: Described](f: F) -> str {{\n  Described.describe(f)\n}}\n\n\
             fn main() ! console {{\n  print(told({arg}))\n}}\n"
        )
    };
    let accepted = program("one");
    assert!(errors(&accepted).is_empty(), "{:?}", errors(&accepted));
    assert_eq!(outcome(&accepted).stdout, b"one\n");
    let lambda = errors(&program("fn(x: i32) { x }"));
    assert_eq!(lambda.len(), 1, "{lambda:?}");
    assert_eq!(lambda[0].message, "this is a lambda, where a `F` is needed");
}

/// A type parameter that no trait bounds is given a function as it was: it is
/// not supported yet, and a parameter of a function type takes the same name
/// by the type of the function.
fn a_function_given_for_a_type_parameter_nothing_bounds_is_refused_as_it_was_and_a_function_type_still_takes_it()
 {
    let program = |params: &str, arg: &str| {
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             fn one(x: i32) -> i32 {{ x }}\n\n\
             fn keep{params} -> i32 {{ 1 }}\n\n\
             fn main() ! console, alloc {{\n  print(\"{{keep({arg})}}\")\n}}\n"
        )
    };
    let unbounded = errors(&program("[T](f: T)", "one"));
    assert_eq!(unbounded.len(), 1, "{unbounded:?}");
    assert_eq!(
        unbounded[0].message,
        "a function given for a type parameter is not supported yet"
    );
    let typed = program("(f: fn(i32) -> i32)", "one");
    assert!(errors(&typed).is_empty(), "{:?}", errors(&typed));
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_program_that_asks_a_function_what_it_is_prints_what_the_evaluator_says() {
    generate::check(7912, 6, program, |p| {
        if !(p.asked.iter()).all(|k| p.impls[*k].is_some()) {
            return;
        }
        let source = p.source();
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
