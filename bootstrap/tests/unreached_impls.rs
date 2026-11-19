//! What a program does not reach is not built. A program is built beside the
//! same program with an `impl` of a trait it reaches for a type it never
//! makes, and beside the same program with a generic function it never calls,
//! and every stage after the front does what it does for the first, as does
//! the checker's count of comparisons. The front reads the added text, so it
//! is held to the same program with that text in a function nothing reaches,
//! which it reads and does nothing else with.

use smith::scalar::{self, Program};
use smith::target::Target;
use smith::work::{Compared, compared};

use crate::Test;
use crate::counting::{Allocated, allocated};
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "an_impl_for_a_type_never_made_and_a_function_never_called_are_not_built",
        an_impl_for_a_type_never_made_and_a_function_never_called_are_not_built,
    ),
    (
        "an_impl_of_the_program_that_is_not_reached_is_dead_code_and_one_that_is_reached_is_checked",
        an_impl_of_the_program_that_is_not_reached_is_dead_code_and_one_that_is_reached_is_checked,
    ),
];

/// How many lines the larger body has.
const LINES: usize = 60;

/// A program that reaches `Area`, with `extra` after it.
fn program(extra: &str) -> String {
    format!(
        "use std.console (console, print)

struct Square {{
  side: i64
}}

struct Circle {{
  radius: i64
}}

trait Area[T] {{
  fn area(x: T) -> i64
}}

impl Area[Square] {{
  fn area(x: Square) -> i64 {{
    wrapping_mul(x.side, x.side)
  }}
}}

fn main() ! console, alloc {{
  print(\"{{area(Square(side: 3))}}\")
}}
{extra}"
    )
}

/// The lines of a body that reads `x.radius`, which neither a reader nor the
/// checker has anything to say about.
fn body(lines: usize) -> String {
    let mut text = String::from("    a0 = wrapping_add(x.radius, 1)\n");
    for n in 1..lines {
        text += &format!("    a{n} = wrapping_add(a{}, x.radius)\n", n - 1);
    }
    text + &format!("    a{}\n", lines - 1)
}

/// What is added to the program, with a body of `lines` lines: an `impl` of
/// `Area` for a `Circle` nothing makes, a generic function nothing calls, and,
/// for what reading costs, a function nothing reaches.
fn added(lines: usize) -> [String; 3] {
    let body = body(lines);
    [
        format!("\nimpl Area[Circle] {{\n  fn area(x: Circle) -> i64 {{\n{body}  }}\n}}\n"),
        format!("\nfn nothing[T](x: Circle, y: T) -> i64 {{\n{body}}}\n"),
        format!("\nfn nothing(x: Circle) -> i64 {{\n{body}}}\n"),
    ]
}

/// What each stage allocates over a program, by the name of the stage, and the
/// comparisons the checker makes.
fn stages(source: &str) -> (Vec<(&'static str, Allocated)>, Compared) {
    let before = compared();
    let (program, front) = allocated(|| smith::lower(source.as_bytes()));
    let after = compared();
    let program: Program = program.expect("the source builds");
    let (program, inline) = allocated(|| scalar::inline(&program));
    let (program, trim) = allocated(|| scalar::trim(&program));
    let (program, fold) = allocated(|| scalar::fold(&program));
    let (program, common) = allocated(|| scalar::common(&program));
    let (program, remove) = allocated(|| scalar::remove(&program));
    let (_, emit) = allocated(|| Target::ALL[0].emit(&program));
    let comparisons = Compared {
        items: after.items - before.items,
        instances: after.instances - before.instances,
    };
    let stages = vec![
        ("lex, parse, check, and lower", front),
        ("inline", inline),
        ("trim", trim),
        ("fold", fold),
        ("common", common),
        ("remove", remove),
        ("emit", emit),
    ];
    (stages, comparisons)
}

/// A body of sixty lines in an `impl` of `Area` for a `Circle` nothing makes,
/// or in a generic function nothing calls, is built as a body of one line is:
/// each stage after the front allocates what it did, within what a hash seed moves, and checking compares what
/// it did. The front reads the lines, as it does in a function nothing reaches,
/// and what it allocates for them is no more than there, and a hundredth.
fn an_impl_for_a_type_never_made_and_a_function_never_called_are_not_built() {
    // The first build of a thread reads what every build shares.
    stages(&program(""));
    let [small, large] = [1, LINES].map(|lines| added(lines).map(|extra| stages(&program(&extra))));
    let (reading, _) = (&large[2], &small[2]);
    for form in 0..2 {
        let (counted, comparisons) = (small[form].1, large[form].1);
        assert_eq!(counted, comparisons, "checking compares as the body grows, in form {form}");
        for (i, (stage, one)) in small[form].0.iter().enumerate() {
            let sixty = large[form].0[i].1;
            if i == 0 {
                let read = |a: Allocated, b: Allocated| a.calls - b.calls;
                let (grown, read_calls) = (read(sixty, *one), read(reading.0[0].1, small[2].0[0].1));
                assert!(
                    100 * grown <= 101 * read_calls,
                    "{stage} allocates {grown} more times for the body, and reading it takes {read_calls}, in form {form}"
                );
            } else {
                // A stage that keeps a table by hash allocates a little more or
                // less by the seed the run gives the hash.
                let near = |a: u64, b: u64, per_hundred: u64| 100 * a.abs_diff(b) <= per_hundred * a;
                assert!(
                    near(one.calls, sixty.calls, 2) && near(one.bytes, sixty.bytes, 10),
                    "{stage} allocates {one:?} and then {sixty:?} as the body grows, in form {form}"
                );
            }
        }
    }
}

/// A program with an `impl` of `Show` that the generator chose the type of,
/// whether its body has a mistake, and whether `main` makes the type.
#[derive(Debug)]
struct Case {
    source: String,
    /// The line of the `impl`'s function, where the mistake is.
    line: usize,
    mistake: bool,
    made: bool,
}

/// A type of the program, or one of the library's, for which `Show` is
/// implemented, whose function is one line that is sound or has a mistake.
/// A type of the library is not made by `main`, as it has no way to here.
fn case(g: &mut Gen) -> Case {
    let own = g.one_in(2);
    let mistake = g.one_in(2);
    let made = own && g.one_in(2);
    let ty = if own { "Unused" } else { "Utf8Error" };
    let body = if mistake { "x.missing + 1" } else { "\"s\"" };
    let uses = if made { "\n  print(show(Unused(n: 2)))" } else { "" };
    let source = format!(
        "use std.console (console, print)
use std.utf8 (Utf8Error)

trait Show[T] {{
  fn show(x: T) -> str
}}

struct A {{
  n: i64
}}

struct Unused {{
  n: i64
}}

impl Show[A] {{
  fn show(x: A) -> str {{ \"a\" }}
}}

impl Show[{ty}] {{
  fn show(x: {ty}) -> str {{ {body} }}
}}

fn main() ! console {{
  print(show(A(n: 1))){uses}
}}
"
    );
    let line = 1 + source
        .lines()
        .position(|l| l.starts_with(&format!("  fn show(x: {ty})")))
        .expect("the function is written");
    Case { source, line, mistake, made }
}

/// An `impl` of the program's own that `main` does not reach, for a type of the
/// program or of the library, builds whatever its body says, and `smith check`
/// says once that it is dead code, at the `impl`, which is the line before its
/// function. The same `impl` for a type `main` makes is checked: its mistake
/// is an error at the line it is on, and what is sound is no warning.
fn an_impl_of_the_program_that_is_not_reached_is_dead_code_and_one_that_is_reached_is_checked() {
    generate::check(29, 40, case, |case| {
        let at = |errors: &[smith::diag::Error]| {
            errors.iter().filter(|e| e.pos.line == case.line - 1).count()
        };
        let source = case.source.as_bytes();
        if case.made {
            match smith::check(source) {
                Ok(warnings) => {
                    assert!(!case.mistake, "a mistake in an impl that is made builds");
                    assert_eq!(at(&warnings), 0, "an impl that is reached is not dead code");
                }
                Err(errors) => {
                    assert!(case.mistake, "a sound impl that is made builds");
                    assert!(errors.iter().any(|e| e.pos.line == case.line));
                }
            }
        } else {
            let warnings = smith::check(source).expect("an impl that is not reached builds");
            assert_eq!(at(&warnings), 1, "one warning at the impl");
            assert!(smith::lower(source).is_ok());
        }
    });
}
