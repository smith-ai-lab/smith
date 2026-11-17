//! Generators and properties of `loop`: over a range of whole numbers, and
//! open-ended until a `break`.
//!
//! Every generator works out the answer with the arithmetic of Rust on 128
//! bits, or by counting, so it shares nothing with the compiler.

use smith::scalar::{self, End, Op, Outcome, System};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};

pub const ONCE: &[Test] = &[
    (
        "a_loop_over_a_range_sums_and_an_open_ended_loop_counts_until_it_breaks",
        a_loop_over_a_range_sums_and_an_open_ended_loop_counts_until_it_breaks,
    ),
    (
        "a_loop_over_a_range_runs_once_for_each_number_from_a_up_to_b_in_order",
        a_loop_over_a_range_runs_once_for_each_number_from_a_up_to_b_in_order,
    ),
    (
        "a_loop_over_the_numbers_of_a_type_that_has_eight_bits_runs_each_once_and_ends",
        a_loop_over_the_numbers_of_a_type_that_has_eight_bits_runs_each_once_and_ends,
    ),
    (
        "inside_a_loop_over_a_range_arith_knows_the_counter_is_inside_the_range",
        inside_a_loop_over_a_range_arith_knows_the_counter_is_inside_the_range,
    ),
    (
        "a_break_ends_the_nearest_loop_around_it_and_no_other",
        a_break_ends_the_nearest_loop_around_it_and_no_other,
    ),
    (
        "a_break_with_no_loop_to_end_is_an_error",
        a_break_with_no_loop_to_end_is_an_error,
    ),
    (
        "an_open_ended_loop_makes_its_function_and_every_caller_carry_diverge",
        an_open_ended_loop_makes_its_function_and_every_caller_carry_diverge,
    ),
    (
        "a_var_assigned_in_a_loop_is_known_to_arith_only_by_the_range_of_its_type",
        a_var_assigned_in_a_loop_is_known_to_arith_only_by_the_range_of_its_type,
    ),
    (
        "a_loop_over_a_range_is_one_counter_and_one_comparison_each_time_round",
        a_loop_over_a_range_is_one_counter_and_one_comparison_each_time_round,
    ),
];

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    scalar::run(&program)
}

fn prints(source: &str, lines: Vec<String>) {
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(
        run(source),
        Outcome {
            stdout: stdout.into_bytes(),
            status: 0
        },
        "for {source}"
    );
}

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

fn a_loop_over_a_range_sums_and_an_open_ended_loop_counts_until_it_breaks() {
    let sum = "fn sum_below(n: u64) -> u64 {\n  var total: u64 = 0\n  loop i in 0..n { total = wrapping_add(total, i) }\n  total\n}\n\nfn main() ! console, alloc {\n  print(\"{sum_below(10)}\")\n}\n";
    prints(sum, vec!["45".into()]);
    let count = "fn takes(from: u64, step: u64) -> u64 ! diverge {\n  var left: u64 = from\n  var times: u64 = 0\n  loop {\n    if left < step { break }\n    left = wrapping_sub(left, step)\n    times = wrapping_add(times, 1)\n  }\n  times\n}\n\nfn main() ! console, alloc, diverge {\n  print(\"{takes(10, 3)}\")\n}\n";
    prints(count, vec!["3".into()]);
}

#[derive(Debug)]
struct Span {
    ty: Int,
    a: i128,
    b: i128,
}

/// A pair of numbers of one type, a few apart, in either order, away from
/// the limits of the type or at them, or any two, when they are not far apart.
fn span(g: &mut Gen) -> Span {
    let ty = *g.pick(INTS);
    let length = g.below(10) as i128;
    let (a, b) = match g.below(5) {
        0 => {
            let a = value(g, ty);
            (a, (a + length).min(ty.max()))
        }
        1 => {
            let a = value(g, ty);
            (a, (a - length).max(ty.min()))
        }
        2 => (ty.max() - length, ty.max()),
        3 => (ty.min(), ty.min() + length),
        _ => (value(g, ty), value(g, ty)),
    };
    // A range of more numbers than a test can run is cut short.
    let b = if b - a > 20 {
        (a + length).min(ty.max())
    } else {
        b
    };
    Span { ty, a, b }
}

fn show(ty: Int, a: i128, b: i128) -> String {
    format!(
        "fn show(a: {t}, b: {t}) ! console, alloc {{\n  loop i in a..b {{ print(\"{{i}}\") }}\n}}\n\nfn main() ! console, alloc {{\n  show({a}, {b})\n}}\n",
        t = ty.name
    )
}

/// For every pair of whole numbers of one type, the loop runs for each
/// number from `a` up to but not including `b`, in order, and not at all
/// when `a >= b`.
fn a_loop_over_a_range_runs_once_for_each_number_from_a_up_to_b_in_order() {
    generate::check(41, 120, span, |s| {
        let lines = (s.a..s.b).map(|n| n.to_string()).collect();
        prints(&show(s.ty, s.a, s.b), lines);
    });
}

fn a_loop_over_the_numbers_of_a_type_that_has_eight_bits_runs_each_once_and_ends() {
    for ty in INTS.iter().filter(|t| t.bits == 8) {
        let (a, b) = (ty.min(), ty.max());
        let lines: Vec<String> = (a..b).map(|n| n.to_string()).collect();
        assert_eq!(lines.len(), 255);
        prints(&show(*ty, a, b), lines);
    }
}

#[derive(Debug)]
struct Reach {
    ty: Int,
    lo: i128,
    hi: i128,
    by: i128,
    up: bool,
}

/// A range of a few numbers of a type, at its limits or between, and a small
/// number to add to the counter, or to take from it.
fn reach(g: &mut Gen) -> Reach {
    let ty = *g.pick(INTS);
    let mut lo = value(g, ty).min(ty.max() - 1);
    if g.one_in(2) {
        lo = ty.max() - g.below(4) as i128 - 1;
    }
    let hi = (lo + 1 + g.below(4) as i128).min(ty.max());
    Reach {
        ty,
        lo,
        hi,
        by: g.below(4) as i128,
        up: g.one_in(2),
    }
}

/// Inside `loop i in lo..hi`, `i + c` compiles exactly when `hi - 1 + c` is a
/// number of the type, and `i - c` exactly when `lo - c` is, with no `if`.
fn inside_a_loop_over_a_range_arith_knows_the_counter_is_inside_the_range() {
    generate::check(43, 150, reach, |r| {
        let operation = if r.up {
            format!("i + {}", r.by)
        } else {
            format!("i - {}", r.by)
        };
        let source = format!(
            "fn main() ! console, alloc {{\n  lo: {t} = {lo}\n  hi: {t} = {hi}\n  loop i in lo..hi {{\n    x = {operation}\n    print(\"{{x}}\")\n  }}\n}}\n",
            t = r.ty.name,
            lo = r.lo,
            hi = r.hi,
        );
        let fits = if r.up {
            r.hi - 1 + r.by <= r.ty.max()
        } else {
            r.lo - r.by >= r.ty.min()
        };
        let errors = refused(&source);
        assert_eq!(errors.is_empty(), fits, "{errors:?} for {source}");
        if fits {
            let counted = (r.lo..r.hi).map(|i| if r.up { i + r.by } else { i - r.by });
            prints(&source, counted.map(|n| n.to_string()).collect());
        } else {
            assert!(
                errors.iter().any(|e| e.contains(&operation)),
                "{errors:?} does not state the goal about `{operation}`"
            );
        }
    });
}

/// Rounds of an outer loop over a range, and rounds of an inner open-ended
/// loop that breaks after a number of its own.
fn a_break_ends_the_nearest_loop_around_it_and_no_other() {
    generate::check(
        47,
        40,
        |g| (g.below(6) as u32, g.below(6) as u32),
        |&(n, m)| {
            let source = format!(
                "fn rounds(n: u32, m: u32) -> u32 ! diverge {{\n  var total: u32 = 0\n  loop i in 0..n {{\n    var done: u32 = 0\n    loop {{\n      if done >= m {{ break }}\n      done = wrapping_add(done, 1)\n      total = wrapping_add(total, 1)\n    }}\n  }}\n  total\n}}\n\nfn main() ! console, alloc, diverge {{\n  print(\"{{rounds({n}, {m})}}\")\n}}\n"
            );
            prints(&source, vec![(n * m).to_string()]);
        },
    );
    // A `break` inside a loop over a range ends it early, whatever else the
    // round has done.
    for stop in 0..5 {
        let source = format!(
            "fn until(stop: i64) -> i64 {{\n  var seen: i64 = 0\n  loop i in 0..10 {{\n    if i == stop {{ break }}\n    seen = wrapping_add(seen, 1)\n  }}\n  seen\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{until({stop})}}\")\n}}\n"
        );
        prints(&source, vec![stop.to_string()]);
    }
}

fn a_break_with_no_loop_to_end_is_an_error() {
    let outside = "fn stop() {\n  break\n}\n\nfn main() ! console, alloc {\n  stop()\n}\n";
    let in_lambda = "fn twice(body: () -> ()) {\n  body()\n  body()\n}\n\nfn main() ! console, alloc {\n  loop i in 0..3 {\n    twice({ break })\n  }\n}\n";
    for source in [outside, in_lambda] {
        let errors = refused(source);
        assert!(
            errors.iter().any(|e| e.contains("`break`")),
            "{errors:?} does not name `break` for {source}"
        );
    }
    let fine = "fn main() ! console, alloc {\n  loop i in 0..3 {\n    if i == 1 { break }\n    print(\"{i}\")\n  }\n}\n";
    assert_eq!(refused(fine), Vec::<String>::new());
}

fn an_open_ended_loop_makes_its_function_and_every_caller_carry_diverge() {
    let program = |first: &str, second: &str, main: &str| {
        format!(
            "fn once() -> u8{first} {{\n  loop {{ break }}\n  1\n}}\n\nfn twice() -> u8{second} {{\n  once()\n}}\n\nfn main() ! console, alloc{main} {{\n  print(\"{{twice()}}\")\n}}\n"
        )
    };
    let all = " ! diverge";
    let names = |source: &str| refused(source).iter().any(|e| e.contains("`diverge`"));
    assert!(!names(&program(all, all, ", diverge")));
    assert!(names(&program("", all, ", diverge")));
    assert!(names(&program(all, "", ", diverge")));
    assert!(names(&program(all, all, "")));
    // A loop over a range adds no effect, and stays pure.
    let pure = "fn total(n: u8) -> u8 {\n  var sum: u8 = 0\n  loop i in 0..n { sum = wrapping_add(sum, i) }\n  sum\n}\n\nfn main() ! console, alloc {\n  print(\"{total(4)}\")\n}\n";
    prints(pure, vec!["6".into()]);
}

fn a_var_assigned_in_a_loop_is_known_to_arith_only_by_the_range_of_its_type() {
    for ty in INTS {
        let t = ty.name;
        let inside = format!(
            "fn main() ! console, alloc {{\n  var x: {t} = 1\n  loop i in 0..2 {{\n    x = 1\n    y = x + 1\n    print(\"{{y}}\")\n  }}\n}}\n"
        );
        let after = format!(
            "fn main() ! console, alloc {{\n  var x: {t} = 1\n  loop i in 0..2 {{ x = 1 }}\n  y = x + 1\n  print(\"{{y}}\")\n}}\n"
        );
        for source in [inside, after] {
            let errors = refused(&source);
            let goal = format!("x + 1 <= {}", ty.max());
            assert!(
                errors.iter().any(|e| e.contains(&goal)),
                "{errors:?} does not state `{goal}` for {source}"
            );
        }
    }
}

/// For every program with a loop over a range, what it runs each time round
/// is the counter and one comparison: one addition, one comparison, no call,
/// and nothing asked of the system for memory.
fn a_loop_over_a_range_is_one_counter_and_one_comparison_each_time_round() {
    for ty in INTS {
        let source = format!(
            "fn main() ! console {{\n  a: {t} = {a}\n  b: {t} = {b}\n  loop i in a..b {{ print(\"x\") }}\n}}\n",
            t = ty.name,
            a = ty.min(),
            b = ty.max()
        );
        let program = scalar::passes(&lower(source.as_bytes()).expect("compiles"));
        let (mut compares, mut adds, mut calls, mut allocations) = (0, 0, 0, 0);
        for f in &program.functions {
            for block in &f.blocks {
                for instruction in &block.instructions {
                    match &instruction.op {
                        Op::Compare { .. } => compares += 1,
                        Op::Binary { .. } => adds += 1,
                        Op::Call { .. } => calls += 1,
                        Op::System {
                            call: System::Allocate,
                            ..
                        } => allocations += 1,
                        _ => {}
                    }
                }
            }
        }
        assert_eq!(
            (compares, adds, calls, allocations),
            (1, 1, 0, 0),
            "for {source}"
        );
        // The loop is a cycle of blocks: some block jumps back to a block
        // that comes before it.
        let loops = (program
            .functions
            .iter()
            .flat_map(|f| f.blocks.iter().enumerate()))
        .any(|(at, block)| match &block.end {
            End::Jump(j) => j.block <= at,
            _ => false,
        });
        assert!(loops, "no loop in {source}");
    }
}
