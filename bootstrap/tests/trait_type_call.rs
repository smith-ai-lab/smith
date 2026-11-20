//! A call of a function of a trait may write the types it is for in brackets
//! after the trait, `Tagged[Cat].tag("p")`, which names the impl as the types of
//! the arguments do. A function of a trait that none of its parameters and none
//! of its result names is called only so. The expectation of each program is
//! what the generator that wrote its impls chose each to give.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_call_with_the_type_written_after_the_trait_runs_the_impl_for_that_type",
        a_call_with_the_type_written_after_the_trait_runs_the_impl_for_that_type,
    ),
    (
        "a_function_that_names_no_type_of_its_trait_is_refused_without_the_type_written",
        a_function_that_names_no_type_of_its_trait_is_refused_without_the_type_written,
    ),
    (
        "a_type_written_with_no_impl_or_the_wrong_number_of_types_is_one_error",
        a_type_written_with_no_impl_or_the_wrong_number_of_types_is_one_error,
    ),
    (
        "a_tuple_or_unit_type_written_after_the_trait_is_one_type",
        a_tuple_or_unit_type_written_after_the_trait_is_one_type,
    ),
    (
        "a_type_parameter_written_after_the_trait_runs_the_impl_of_the_type_it_stands_for",
        a_type_parameter_written_after_the_trait_runs_the_impl_of_the_type_it_stands_for,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_call_with_the_type_written_prints_what_the_evaluator_says",
    every_executable_of_a_call_with_the_type_written_prints_what_the_evaluator_says,
)];

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// The text each type's impl gives, by type, and the types `main` asks.
#[derive(Clone, Debug)]
struct Program {
    says: Vec<String>,
    asked: Vec<usize>,
}

fn word(g: &mut Gen) -> String {
    (0..g.below(3) + 1)
        .map(|_| (b'a' + g.below(26) as u8) as char)
        .collect()
}

fn program(g: &mut Gen) -> Program {
    let count = g.below(3) + 1;
    Program {
        says: (0..count).map(|_| word(g)).collect(),
        asked: (0..g.below(3) + 1).map(|_| g.below(count)).collect(),
    }
}

impl Program {
    fn source(&self) -> String {
        let mut text = String::from("use std.console (console, print)\n\n");
        for k in 0..self.says.len() {
            text += &format!("struct S{k} {{\n  x: str\n}}\n\n");
        }
        text += "trait Tagged[T] {\n  fn tag(prefix: str) -> str\n}\n\n";
        for (k, said) in self.says.iter().enumerate() {
            text += &format!(
                "impl Tagged[S{k}] {{\n  fn tag(prefix: str) -> str {{ \"{said}\" }}\n}}\n\n"
            );
        }
        text += "fn main() ! console {\n";
        for k in &self.asked {
            text += &format!("  print(Tagged[S{k}].tag(\"p\"))\n");
        }
        text + "}\n"
    }

    fn prints(&self) -> Vec<u8> {
        let mut said = String::new();
        for k in &self.asked {
            said += &format!("{}\n", self.says[*k]);
        }
        said.into_bytes()
    }
}

/// The impl that the types written name is the one run, whichever types the
/// program has impls for and asks.
fn a_call_with_the_type_written_after_the_trait_runs_the_impl_for_that_type() {
    generate::check(7931, 40, program, |p| {
        let source = p.source();
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?} for\n{source}");
        assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
    });
}

/// A function of a trait that names no type of the trait is called with the
/// type written, and without it the call is one error that says so.
fn a_function_that_names_no_type_of_its_trait_is_refused_without_the_type_written() {
    let source = |call: &str| {
        format!(
            "use std.console (console, print)\n\n\
             struct Cat {{\n  x: str\n}}\n\n\
             trait Tagged[T] {{\n  fn tag(prefix: str) -> str\n}}\n\n\
             impl Tagged[Cat] {{\n  fn tag(prefix: str) -> str {{ \"cat\" }}\n}}\n\n\
             fn main() ! console {{\n  print({call})\n}}\n"
        )
    };
    assert!(errors(&source("Tagged[Cat].tag(\"p\")")).is_empty());
    let found = errors(&source("Tagged.tag(\"p\")"));
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(
        found[0].message,
        "`tag` is for a type that nothing it is given names, and the call writes it"
    );
    assert_eq!(
        found[0].fix.as_deref(),
        Some("write the type after the trait, as in `Tagged[T].tag(...)`")
    );
}

/// A type that has no impl, and two types where the trait takes one.
fn a_type_written_with_no_impl_or_the_wrong_number_of_types_is_one_error() {
    let source = |call: &str| {
        format!(
            "use std.console (console, print)\n\n\
             struct Cat {{\n  x: str\n}}\n\nstruct Dog {{\n  x: str\n}}\n\n\
             trait Tagged[T] {{\n  fn tag(prefix: str) -> str\n}}\n\n\
             impl Tagged[Cat] {{\n  fn tag(prefix: str) -> str {{ \"cat\" }}\n}}\n\n\
             fn main() ! console {{\n  print({call})\n}}\n"
        )
    };
    let none = errors(&source("Tagged[Dog].tag(\"p\")"));
    assert_eq!(none.len(), 1, "{none:?}");
    assert!(
        none[0]
            .message
            .starts_with("there is no implementation of `Tagged` for `Dog`"),
        "{none:?}"
    );
    let two = errors(&source("Tagged[Cat, Dog].tag(\"p\")"));
    assert_eq!(two.len(), 1, "{two:?}");
    assert_eq!(two[0].message, "`Tagged` takes 1 type, and this gives 2");
}

/// Inside a function with a type parameter, the parameter written after the
/// trait stands for the type the function is called with.
fn a_type_parameter_written_after_the_trait_runs_the_impl_of_the_type_it_stands_for() {
    generate::check(7932, 30, program, |p| {
        let mut source = p.source();
        source = source.replace(
            "fn main() ! console {\n",
            "fn tagged[T: Tagged](x: T) -> str {\n  Tagged[T].tag(\"p\")\n}\n\nfn main() ! console {\n",
        );
        // Ask by a value of each type instead of by the written type.
        let mut said = String::new();
        let main = source.find("fn main()").expect("main");
        source.truncate(main);
        source += "fn main() ! console {\n";
        for k in &p.asked {
            source += &format!("  print(tagged(S{k}(x: \"a\")))\n");
            said += &format!("{}\n", p.says[*k]);
        }
        source += "}\n";
        assert_eq!(outcome(&source).stdout, said.into_bytes(), "for\n{source}");
    });
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_call_with_the_type_written_prints_what_the_evaluator_says() {
    generate::check(7933, 6, program, |p| {
        let source = p.source();
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}

/// A tuple in parentheses, `()`, and a tuple inside a type written, are each
/// one type, and not the list of types that commas in the brackets make.
fn a_tuple_or_unit_type_written_after_the_trait_is_one_type() {
    let source = "use std.console (console, print)\n\n\
         trait Tagged[T] {\n  fn tag(prefix: str) -> str\n}\n\n\
         impl Tagged[()] {\n  fn tag(prefix: str) -> str { \"unit\" }\n}\n\n\
         impl Tagged[(Int, str)] {\n  fn tag(prefix: str) -> str { \"pair\" }\n}\n\n\
         impl Tagged[Option[(Int, str)]] {\n  fn tag(prefix: str) -> str { \"maybe\" }\n}\n\n\
         fn main() ! console {\n  print(Tagged[()].tag(\"p\"))\n  print(Tagged[(Int, str)].tag(\"p\"))\n  print(Tagged[Option[(Int, str)]].tag(\"p\"))\n}\n";
    let found = errors(source);
    assert!(found.is_empty(), "{found:?}");
    assert_eq!(outcome(source).stdout, b"unit\npair\nmaybe\n");
}
