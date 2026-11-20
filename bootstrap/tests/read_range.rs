//! A number read from JSON into a type that is a range is read only when it is
//! in the range, whatever size the range is kept at: at the least value, at the
//! greatest, and one past each, and through the arguments of a function that
//! `Function.call` runs. The expectation of each program is the range and the
//! numbers the generator wrote.

use smith::scalar::{self, Outcome};
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_number_read_for_an_argument_of_a_function_called_by_call_is_held_to_the_range_of_its_parameter",
        a_number_read_for_an_argument_of_a_function_called_by_call_is_held_to_the_range_of_its_parameter,
    ),
    (
        "a_number_read_into_a_range_is_read_exactly_when_it_is_in_the_range_at_both_ends_and_one_past",
        a_number_read_into_a_range_is_read_exactly_when_it_is_in_the_range_at_both_ends_and_one_past,
    ),
];

/// The executables of the program of the issue are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_refuses_a_number_outside_the_range_it_is_read_into",
    every_executable_refuses_a_number_outside_the_range_it_is_read_into,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// A range, from `least` up to but not including `end`.
#[derive(Clone, Debug)]
struct Range {
    least: i64,
    end: i64,
}

fn range(g: &mut Gen) -> Range {
    // Sizes of the holder that the range is kept at: 8, 16, 32, and 64 bits.
    let width = [3, 200, 2_000, 70_000, 5_000_000_000i64][g.below(5)];
    let least = match g.below(3) {
        0 => 0,
        1 => 1,
        _ => -(g.below(100) as i64) - 1,
    };
    Range {
        least,
        end: least + width,
    }
}

impl Range {
    /// The numbers to read: the ends, and one past each.
    fn numbers(&self) -> [i64; 4] {
        [self.least - 1, self.least, self.end - 1, self.end]
    }

    fn holds(&self, n: i64) -> bool {
        self.least <= n && n < self.end
    }
}

/// `use`s for what a program that reads JSON needs.
const USES: &str = "use std.console (console, print)\nuse std.deserialize (ReadError)\nuse std.json (FromJson, from_json)\n\n";

/// Each of the four numbers is read into `struct P { a: least..end }`, and is
/// the number when it is in the range and `err` when it is not.
fn a_number_read_into_a_range_is_read_exactly_when_it_is_in_the_range_at_both_ends_and_one_past() {
    generate::check(7941, 5, range, |r| {
        let mut source = format!(
            "{USES}struct P {{ a: {}..{} }}\n\n\
             fn show(r: Result[P, ReadError]) -> str ! alloc {{\n  match r {{\n    \
             .ok(p) {{ \"{{p.a}}\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
             fn main() ! console, alloc {{\n",
            r.least, r.end
        );
        let mut said = String::new();
        for n in r.numbers() {
            source += &format!(
                "  r{k}: Result[P, ReadError] = from_json(\"\\{{\\\"a\\\": {n}\\}}\")\n  print(show(r{k}))\n",
                k = said.lines().count()
            );
            said += &if r.holds(n) {
                format!("{n}\n")
            } else {
                "err\n".to_string()
            };
        }
        source += "}\n";
        assert_eq!(outcome(&source).stdout, said.into_bytes(), "for\n{source}");
    });
}

/// `fn pick(a: least..end, b: least..end = least) -> least..end`, read through
/// `Function.call`: a number outside the range of `a` is no call, and one inside
/// gives `a`.
fn a_number_read_for_an_argument_of_a_function_called_by_call_is_held_to_the_range_of_its_parameter()
 {
    generate::check(7942, 4, range, |r| {
        let mut source = format!(
            "{USES}fn pick(a: {l}..{e}, b: {l}..{e} = {l}) -> {l}..{e} {{ a }}\n\n\
             fn run[F, A, R](f: F, text: str) -> Option[R] ! alloc\n  \
             requires Function[F, A, R]\n  requires FromJson[A]\n  requires Discard[A]\n{{\n  \
             r: Result[A, ReadError] = from_json(text)\n  match r {{\n    \
             .ok(args) {{ Option.some(Function.call(f, args)) }}\n    .err(e) {{ Option.none }}\n  }}\n}}\n\n\
             fn show(o: Option[{l}..{e}]) -> str ! alloc {{\n  match o {{\n    \
             .some(v) {{ \"{{v}}\" }}\n    .none {{ \"none\" }}\n  }}\n}}\n\n\
             fn main() ! console, alloc {{\n",
            l = r.least,
            e = r.end
        );
        let mut said = String::new();
        for (k, n) in r.numbers().into_iter().enumerate() {
            source += &format!(
                "  t{k} = \"\\{{\\\"a\\\": {n}\\}}\"\n  print(show(run(pick, t{k})))\n"
            );
            said += &if r.holds(n) {
                format!("{n}\n")
            } else {
                "none\n".to_string()
            };
        }
        source += "}\n";
        assert_eq!(outcome(&source).stdout, said.into_bytes(), "for\n{source}");
    });
}

/// `{"a": 5000}` read into `a: 0..1000` is `err` in the evaluator and in every
/// executable that can run here.
fn every_executable_refuses_a_number_outside_the_range_it_is_read_into() {
    let source = format!(
        "{USES}struct P {{ a: 0..1000 }}\n\n\
         fn show(r: Result[P, ReadError]) -> str ! alloc {{\n  match r {{\n    \
         .ok(p) {{ \"{{p.a}}\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
         fn main() ! console, alloc {{\n  \
         r: Result[P, ReadError] = from_json(\"\\{{\\\"a\\\": 5000\\}}\")\n  \
         print(show(r))\n}}\n"
    );
    let said = outcome(&source);
    assert_eq!(said.stdout, b"err\n");
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
