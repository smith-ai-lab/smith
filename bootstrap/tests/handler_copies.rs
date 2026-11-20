//! A function that carries the effect a handler answers is built for the
//! handlers it runs under, and takes what their functions use: whichever
//! handler is in force where it is called answers it, a function that runs
//! under two handlers is built once for each, and the executable has no table
//! of handlers. The expectation of each program is what the generator that
//! wrote it chose the handlers to give.

use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_function_that_carries_the_effect_gives_what_the_handler_in_force_where_it_is_called_gives",
        a_function_that_carries_the_effect_gives_what_the_handler_in_force_where_it_is_called_gives,
    ),
    (
        "a_function_that_runs_under_two_handlers_is_built_once_for_each_and_once_under_the_same",
        a_function_that_runs_under_two_handlers_is_built_once_for_each_and_once_under_the_same,
    ),
    (
        "a_function_runs_under_a_handler_inside_another_that_answers_by_asking_the_one_outside",
        a_function_runs_under_a_handler_inside_another_that_answers_by_asking_the_one_outside,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_function_that_runs_under_handlers_prints_what_the_evaluator_says",
    every_executable_of_a_function_that_runs_under_handlers_prints_what_the_evaluator_says,
)];

/// What one of the handlers gives when asked.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Answer {
    /// The number the call of the handler was given, whatever the operation is
    /// asked.
    Given,
    /// The number the operation is asked about.
    Asked,
}

/// A site: the handler it installs, the number it gives that handler, how
/// deep the chain of functions between the site and the operation is, and the
/// number it asks about.
#[derive(Clone, Debug)]
struct Site {
    handler: usize,
    given: i32,
    asked: i32,
}

#[derive(Clone, Debug)]
struct Program {
    handlers: Vec<Answer>,
    depth: usize,
    sites: Vec<Site>,
}

fn number(g: &mut Gen) -> i32 {
    g.below(2000) as i32 - 1000
}

fn program(g: &mut Gen) -> Program {
    let handlers: Vec<Answer> = (0..g.below(2) + 1)
        .map(|_| *g.pick(&[Answer::Given, Answer::Asked]))
        .collect();
    let sites = (0..g.below(3) + 1)
        .map(|_| Site {
            handler: g.below(handlers.len()),
            given: number(g),
            asked: number(g),
        })
        .collect();
    Program {
        handlers,
        depth: g.below(3),
        sites,
    }
}

impl Program {
    fn source(&self) -> String {
        let mut text = String::from("use std.bits (i32)\nuse std.console (console, print)\n\n");
        text += "effect fx {\n  fn ask(a: i32) -> i32\n}\n\n";
        for (k, answer) in self.handlers.iter().enumerate() {
            let gives = match answer {
                Answer::Given => "k",
                Answer::Asked => "a",
            };
            text += &format!(
                "fn handler{k}[T](k: i32, body: fn() -> T ! fx) -> T {{\n  fx(ask: fn(a) {{ {gives} }}, body: body)\n}}\n\n"
            );
        }
        text += "fn run0(a: i32) -> i32 ! fx {\n  ask(a)\n}\n\n";
        for d in 1..=self.depth {
            text += &format!("fn run{d}(a: i32) -> i32 ! fx {{\n  run{}(a)\n}}\n\n", d - 1);
        }
        for (i, site) in self.sites.iter().enumerate() {
            text += &format!(
                "fn site{i}() -> i32 {{\n  with handler{}({})\n  run{}({})\n}}\n\n",
                site.handler, site.given, self.depth, site.asked
            );
        }
        text += "fn main() ! console, alloc {\n";
        for i in 0..self.sites.len() {
            text += &format!("  print(\"{{site{i}()}}\")\n");
        }
        text + "}\n"
    }

    fn prints(&self) -> Vec<u8> {
        let mut said = String::new();
        for site in &self.sites {
            let n = match self.handlers[site.handler] {
                Answer::Given => site.given,
                Answer::Asked => site.asked,
            };
            said += &format!("{n}\n");
        }
        said.into_bytes()
    }
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// The sites may call one function under different handlers, and under the
/// same one with different numbers, and each is answered by its own.
fn a_function_that_carries_the_effect_gives_what_the_handler_in_force_where_it_is_called_gives() {
    generate::check(7401, 30, program, |p| {
        let source = p.source();
        assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
    });
}

/// How many functions of the form are copies of the function `name`.
fn copies_of(program: &scalar::Program, name: &str) -> usize {
    (program.functions.iter())
        .filter(|f| f.name.rsplit('.').next() == Some(name))
        .count()
}

/// The function that asks is built once for each handler it runs under, which
/// is each function that handles it that a site installs, and once when two
/// sites install the same one with different numbers, because what a handler
/// uses is passed to the copy and is not part of what it is built for.
fn a_function_that_runs_under_two_handlers_is_built_once_for_each_and_once_under_the_same() {
    generate::check(7402, 30, program, |p| {
        let source = p.source();
        let form = lower(source.as_bytes()).expect("a program");
        let mut used: Vec<usize> = p.sites.iter().map(|s| s.handler).collect();
        used.sort_unstable();
        used.dedup();
        for d in 0..=p.depth {
            assert_eq!(
                copies_of(&form, &format!("run{d}")),
                used.len(),
                "for\n{source}"
            );
        }
    });
}

/// A handler that answers by asking the effect again is answered by the
/// handler outside it, and a function that runs under both is built for the
/// two together.
fn a_function_runs_under_a_handler_inside_another_that_answers_by_asking_the_one_outside() {
    generate::check(7403, 30, |g: &mut Gen| (number(g), number(g)), |(outer, asked)| {
        let source = format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             effect fx {{\n  fn ask(a: i32) -> i32\n}}\n\n\
             fn fixed[T](k: i32, body: fn() -> T ! fx) -> T {{\n  fx(ask: fn(a) {{ k }}, body: body)\n}}\n\n\
             fn through[T](body: fn() -> T ! fx) -> T ! fx {{\n  fx(ask: fn(a) {{ ask(a) }}, body: body)\n}}\n\n\
             fn run(a: i32) -> i32 ! fx {{\n  ask(a)\n}}\n\n\
             fn nested() -> i32 {{\n  with fixed({outer})\n  with through()\n  run({asked})\n}}\n\n\
             fn main() ! console, alloc {{\n  print(\"{{nested()}}\")\n}}\n"
        );
        assert_eq!(outcome(&source).stdout, format!("{outer}\n").into_bytes(), "for\n{source}");
    });
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_function_that_runs_under_handlers_prints_what_the_evaluator_says() {
    generate::check(7404, 6, program, |p| {
        let source = p.source();
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
