//! A handler keeps its state in a `var` that its function assigns, and the
//! state goes where the code that asks goes: to the code under `with`, to a
//! function that carries the effect and is built for the handler, to the
//! functions that one calls, and through a `loop`, and what each gave is
//! there when it returns. Nothing is global: two handlers installed one after
//! the other each begin where they were given to. The expectation of each
//! program is what a counter that the generator runs says.

use smith::scalar::{self, Outcome};
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "the_state_of_a_handler_goes_to_every_function_that_asks_and_every_call_and_loop_and_comes_back",
        the_state_of_a_handler_goes_to_every_function_that_asks_and_every_call_and_loop_and_comes_back,
    ),
    (
        "a_handler_installed_again_begins_where_it_is_given_to_and_the_other_goes_on",
        a_handler_installed_again_begins_where_it_is_given_to_and_the_other_goes_on,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_program_whose_handler_keeps_state_prints_what_the_evaluator_says",
    every_executable_of_a_program_whose_handler_keeps_state_prints_what_the_evaluator_says,
)];

/// A line of a function: ask once, call a function written before it, or ask
/// in each round of a `loop`.
#[derive(Clone, Debug)]
enum Item {
    Ask,
    Call(usize),
    Rounds(usize),
}

#[derive(Clone, Debug)]
struct Program {
    /// The functions that carry the effect, each calling only those before it.
    functions: Vec<Vec<Item>>,
    /// What each site does under its handler, and where the handler begins and
    /// by how much it steps.
    sites: Vec<(Vec<Item>, i32, i32)>,
}

fn number(g: &mut Gen) -> i32 {
    g.below(200) as i32 - 100
}

fn item(g: &mut Gen, before: usize) -> Item {
    match g.below(3) {
        1 if before > 0 => Item::Call(g.below(before)),
        2 => Item::Rounds(g.below(3) + 1),
        _ => Item::Ask,
    }
}

fn program(g: &mut Gen) -> Program {
    let functions: Vec<Vec<Item>> = (0..g.below(3) + 1)
        .map(|k| (0..g.below(3) + 1).map(|_| item(g, k)).collect())
        .collect();
    let count = functions.len();
    let sites = (0..g.below(2) + 1)
        .map(|_| {
            let items = (0..g.below(3) + 1).map(|_| item(g, count)).collect();
            (items, number(g), number(g))
        })
        .collect();
    Program { functions, sites }
}

/// The lines of a function body that gives the sum of what its items give.
fn body(items: &[Item]) -> String {
    let mut text = String::new();
    let mut names: Vec<String> = Vec::new();
    for (i, item) in items.iter().enumerate() {
        match item {
            Item::Ask => text += &format!("  a{i} = tick()\n"),
            Item::Call(k) => text += &format!("  a{i} = run{k}()\n"),
            Item::Rounds(n) => {
                text += &format!(
                    "  var a{i}: i32 = 0\n  loop j{i} in 0..{n} {{\n    a{i} = wrapping_add(a{i}, tick())\n  }}\n"
                );
            }
        }
        names.push(format!("a{i}"));
    }
    let mut sum = names[0].clone();
    for name in &names[1..] {
        sum = format!("wrapping_add({sum}, {name})");
    }
    text + &format!("  {sum}\n")
}

impl Program {
    fn source(&self) -> String {
        let mut text = String::from(
            "use std.bits (i32)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\n\
             effect fx {\n  fn tick() -> i32\n}\n\n\
             fn counter[T](start: i32, step: i32, body: fn() -> T ! fx) -> T {\n  var n: i32 = start\n  \
             fx(\n    tick: {\n      n = wrapping_add(n, step)\n      n\n    },\n    body: body,\n  )\n}\n\n",
        );
        for (k, items) in self.functions.iter().enumerate() {
            text += &format!("fn run{k}() -> i32 ! fx {{\n{}}}\n\n", body(items));
        }
        for (s, (items, start, step)) in self.sites.iter().enumerate() {
            text += &format!(
                "fn site{s}() -> i32 {{\n  with counter({start}, {step})\n{}}}\n\n",
                body(items)
            );
        }
        text += "fn main() ! console, alloc {\n";
        for s in 0..self.sites.len() {
            text += &format!("  print(\"{{site{s}()}}\")\n");
        }
        text + "}\n"
    }

    /// What the counter says each site gives, which runs the items with the
    /// state a handler begins with.
    fn prints(&self) -> Vec<u8> {
        fn run(p: &Program, items: &[Item], n: &mut i32, step: i32) -> i32 {
            let mut sum = 0i32;
            for (i, item) in items.iter().enumerate() {
                let value = match item {
                    Item::Ask => {
                        *n = n.wrapping_add(step);
                        *n
                    }
                    Item::Call(k) => run(p, &p.functions[*k], n, step),
                    Item::Rounds(r) => {
                        let mut total = 0i32;
                        for _ in 0..*r {
                            *n = n.wrapping_add(step);
                            total = total.wrapping_add(*n);
                        }
                        total
                    }
                };
                sum = if i == 0 { value } else { sum.wrapping_add(value) };
            }
            sum
        }
        let mut said = String::new();
        for (items, start, step) in &self.sites {
            let mut n = *start;
            said += &format!("{}\n", run(self, items, &mut n, *step));
        }
        said.into_bytes()
    }
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// Each site is answered by a counter that the code under it, the functions it
/// calls, and the loops it runs all count on.
fn the_state_of_a_handler_goes_to_every_function_that_asks_and_every_call_and_loop_and_comes_back() {
    generate::check(7501, 40, program, |p| {
        let source = p.source();
        assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
    });
}

/// Two sites install the handler one after the other, and each counts from the
/// number it was given, whatever the other did.
fn a_handler_installed_again_begins_where_it_is_given_to_and_the_other_goes_on() {
    generate::check(7502, 30, |g: &mut Gen| (program(g), number(g)), |(p, start)| {
        let mut both = p.clone();
        let (items, _, step) = p.sites[0].clone();
        both.sites.push((items, *start, step));
        let source = both.source();
        let said = outcome(&source).stdout;
        assert_eq!(said, both.prints(), "for\n{source}");
        // The same items under the same beginning give the same number
        // however many sites came before.
        let alone = Program {
            functions: p.functions.clone(),
            sites: vec![both.sites.last().expect("a site").clone()],
        };
        let lines: Vec<&[u8]> = said.split(|b| *b == b'\n').collect();
        let last = lines[lines.len() - 2];
        assert_eq!(last, alone.prints().strip_suffix(b"\n").unwrap(), "for\n{source}");
    });
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_program_whose_handler_keeps_state_prints_what_the_evaluator_says() {
    generate::check(7503, 6, program, |p| {
        let source = p.source();
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
