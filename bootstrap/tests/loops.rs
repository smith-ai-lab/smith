//! Generators and properties of `loop`: over a range of whole numbers, and
//! open-ended until a `break`.
//!
//! Every generator works out the answer with the arithmetic of Rust on 128
//! bits, or by counting, so it shares nothing with the compiler.

use smith::scalar::{self, Op, Outcome, System};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value, with_header};

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
        "two_literal_ends_of_a_range_take_the_type_the_counter_is_used_as",
        two_literal_ends_of_a_range_take_the_type_the_counter_is_used_as,
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
        "a_var_assigned_in_a_loop_is_known_to_arith_in_the_round_that_gave_it_and_only_there",
        a_var_assigned_in_a_loop_is_known_to_arith_in_the_round_that_gave_it_and_only_there,
    ),
    (
        "the_ends_of_a_range_are_values_from_when_the_loop_starts_and_nothing_ties_them_to_an_assigned_var",
        the_ends_of_a_range_are_values_from_when_the_loop_starts_and_nothing_ties_them_to_an_assigned_var,
    ),
    (
        "a_break_in_an_if_gives_arith_the_condition_that_is_false_in_the_rest_of_the_round",
        a_break_in_an_if_gives_arith_the_condition_that_is_false_in_the_rest_of_the_round,
    ),
    (
        "a_loop_that_decreases_a_measure_ends_without_diverge",
        a_loop_that_decreases_a_measure_ends_without_diverge,
    ),
    (
        "a_loop_with_decreases_compiles_exactly_when_the_measure_is_unsigned_and_gets_smaller",
        a_loop_with_decreases_compiles_exactly_when_the_measure_is_unsigned_and_gets_smaller,
    ),
    (
        "a_round_that_always_ends_in_break_has_no_place_to_go_round_again",
        a_round_that_always_ends_in_break_has_no_place_to_go_round_again,
    ),
    (
        "every_loop_with_decreases_that_compiles_ends",
        every_loop_with_decreases_that_compiles_ends,
    ),
    (
        "a_measure_is_never_computed_when_the_program_runs",
        a_measure_is_never_computed_when_the_program_runs,
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
    let sum = "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\nfn sum_below(n: u64) -> u64 {\n  var total: u64 = 0\n  loop i in 0..n { total = wrapping_add(total, i) }\n  total\n}\n\nfn main() ! console, alloc {\n  print(\"{sum_below(10)}\")\n}\n";
    prints(sum, vec!["45".into()]);
    let count = "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add, wrapping_sub)\n\nfn takes(from: u64, step: u64) -> u64 ! diverge {\n  var left: u64 = from\n  var times: u64 = 0\n  loop {\n    if left < step { break }\n    left = wrapping_sub(left, step)\n    times = wrapping_add(times, 1)\n  }\n  times\n}\n\nfn main() ! console, alloc, diverge {\n  print(\"{takes(10, 3)}\")\n}\n";
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
    with_header(format!(
        "fn show(a: {t}, b: {t}) ! console, alloc {{\n  loop i in a..b {{ print(\"{{i}}\") }}\n}}\n\nfn main() ! console, alloc {{\n  show({a}, {b})\n}}\n",
        t = ty.name
    ))
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

/// Inside `loop i in lo..hi`, `i + c` given to a binding of the type compiles
/// exactly when `hi - 1 + c` is a number of the type, and `i - c` exactly when
/// `lo - c` is, with no `if`.
fn inside_a_loop_over_a_range_arith_knows_the_counter_is_inside_the_range() {
    generate::check(43, 150, reach, |r| {
        let operation = if r.up {
            format!("i + {}", r.by)
        } else {
            format!("i - {}", r.by)
        };
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  lo: {t} = {lo}\n  hi: {t} = {hi}\n  loop i in lo..hi {{\n    x: {t} = {operation}\n    print(\"{{x}}\")\n  }}\n}}\n",
            t = r.ty.name,
            lo = r.lo,
            hi = r.hi,
        ));
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
            let source = with_header(format!(
                "fn rounds(n: u32, m: u32) -> u32 ! diverge {{\n  var total: u32 = 0\n  loop i in 0..n {{\n    var done: u32 = 0\n    loop {{\n      if done >= m {{ break }}\n      done = wrapping_add(done, 1)\n      total = wrapping_add(total, 1)\n    }}\n  }}\n  total\n}}\n\nfn main() ! console, alloc, diverge {{\n  print(\"{{rounds({n}, {m})}}\")\n}}\n"
            ));
            prints(&source, vec![(n * m).to_string()]);
        },
    );
    // A `break` inside a loop over a range ends it early, whatever else the
    // round has done.
    for stop in 0..5 {
        let source = with_header(format!(
            "fn until(stop: Int) -> Int {{\n  var seen: Int = 0\n  loop i in 0..10 {{\n    if i == stop {{ break }}\n    seen = wrapping_add(seen, 1)\n  }}\n  seen\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{until({stop})}}\")\n}}\n"
        ));
        prints(&source, vec![stop.to_string()]);
    }
}

fn a_break_with_no_loop_to_end_is_an_error() {
    let outside = "use std.console (console)\n\nfn stop() {\n  break\n}\n\nfn main() ! console, alloc {\n  stop()\n}\n";
    let in_lambda = "use std.console (console)\n\nfn twice(body: fn()) {\n  body()\n  body()\n}\n\nfn main() ! console, alloc {\n  loop i in 0..3 {\n    twice({ break })\n  }\n}\n";
    for source in [outside, in_lambda] {
        let errors = refused(source);
        assert!(
            errors.iter().any(|e| e.contains("`break`")),
            "{errors:?} does not name `break` for {source}"
        );
    }
    let fine = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  loop i in 0..3 {\n    if i == 1 { break }\n    print(\"{i}\")\n  }\n}\n";
    assert_eq!(refused(fine), Vec::<String>::new());
}

fn an_open_ended_loop_makes_its_function_and_every_caller_carry_diverge() {
    let program = |first: &str, second: &str, main: &str| {
        with_header(format!(
            "fn once() -> Byte{first} {{\n  loop {{ break }}\n  1\n}}\n\nfn twice() -> Byte{second} {{\n  once()\n}}\n\nfn main() ! console, alloc{main} {{\n  print(\"{{twice()}}\")\n}}\n"
        ))
    };
    let all = " ! diverge";
    let names = |source: &str| refused(source).iter().any(|e| e.contains("`diverge`"));
    assert!(!names(&program(all, all, ", diverge")));
    assert!(names(&program("", all, ", diverge")));
    assert!(names(&program(all, "", ", diverge")));
    assert!(names(&program(all, all, "")));
    // A loop over a range adds no effect, and stays pure.
    let pure = "use std.console (console, print)\nuse std.int (wrapping_add)\n\nfn total(n: Byte) -> Byte {\n  var sum: Byte = 0\n  loop i in 0..n { sum = wrapping_add(sum, i) }\n  sum\n}\n\nfn main() ! console, alloc {\n  print(\"{total(4)}\")\n}\n";
    prints(pure, vec!["6".into()]);
}

/// Inside one round of a loop, a `var` the loop assigns has what that round
/// last gave it, and nothing is known of what it held in an earlier round or
/// after the loop.
fn a_var_assigned_in_a_loop_is_known_to_arith_in_the_round_that_gave_it_and_only_there() {
    for ty in INTS {
        let t = ty.name;
        let goal = format!("x + 1 <= {}", ty.max());
        let inside = with_header(format!(
            "fn main() ! console, alloc {{\n  var x: {t} = 1\n  loop i in 0..2 {{\n    x = 1\n    y: {t} = x + 1\n    print(\"{{y}}\")\n  }}\n}}\n"
        ));
        assert_eq!(refused(&inside), Vec::<String>::new(), "for {inside}");
        let after = with_header(format!(
            "fn main() ! console, alloc {{\n  var x: {t} = 1\n  loop i in 0..2 {{ x = 1 }}\n  y: {t} = x + 1\n  print(\"{{y}}\")\n}}\n"
        ));
        let earlier = with_header(format!(
            "fn main() ! console, alloc {{\n  var x: {t} = 1\n  loop i in 0..2 {{\n    y: {t} = x + 1\n    print(\"{{y}}\")\n    x = 1\n  }}\n}}\n"
        ));
        for source in [after, earlier] {
            let errors = refused(&source);
            assert!(
                errors.iter().any(|e| e.contains(&goal)),
                "{errors:?} does not state `{goal}` for {source}"
            );
        }
    }
}

/// The ends of a range are values the loop holds from when it starts. Where
/// an end reads a `var` that the loop assigns, in its block, a lambda, a
/// nested loop, or one arm of an `if`, before the use or after it, no fact
/// about the end is used of the `var` in the block, and the goal that fails is
/// the one about the end.
fn the_ends_of_a_range_are_values_from_when_the_loop_starts_and_nothing_ties_them_to_an_assigned_var()
 {
    let wrap = |assign: &str| {
        [
            ("direct", assign.to_string()),
            ("lambda", format!("run({{ {assign} }})")),
            ("nested", format!("loop j in 0..2 {{ {assign} }}")),
            ("arm", format!("if i == 7 {{ {assign} }}")),
        ]
    };
    let list = |before: &str, after: &str| {
        with_header(format!(
            "fn run(f: fn()) {{ f() }}\n\nfn main() ! console, alloc {{\n  var xs: List[Byte] = [1, 2, 3]\n  loop i in 0..xs.size {{\n    {before}\n    print(\"{{xs[i]}}\")\n    {after}\n  }}\n}}\n"
        ))
    };
    let number = |before: &str, after: &str| {
        with_header(format!(
            "fn run(f: fn()) {{ f() }}\n\nfn f(n: Byte) -> Byte {{\n  var m: Byte = n\n  var s: Byte = 0\n  loop i in 0..m {{\n    {before}\n    s = m - i - 1\n    {after}\n  }}\n  s\n}}\n\nfn main() ! console, alloc {{ print(\"{{f(3)}}\") }}\n"
        ))
    };
    for ((name, list_assign), (_, number_assign)) in wrap("xs = []").into_iter().zip(wrap("m = 0"))
    {
        for (source, goal) in [
            (list("", &list_assign), "i < xs.size"),
            (list(&list_assign, ""), "i < xs.size"),
            (number("", &number_assign), "m - i"),
            (number(&number_assign, ""), "m - i"),
        ] {
            let errors = refused(&source);
            assert!(
                errors.iter().any(|e| e.contains(goal)),
                "{name}: {errors:?} does not state `{goal}` for {source}"
            );
        }
    }
    // Where the loop assigns neither, the end holds.
    assert_eq!(refused(&list("", "")), Vec::<String>::new());
}

/// After `if left < 3 { break }`, `left - 3` compiles, in an open-ended loop
/// with or without `decreases` and in a loop over a range, for every type.
fn a_break_in_an_if_gives_arith_the_condition_that_is_false_in_the_rest_of_the_round() {
    for ty in INTS {
        let t = ty.name;
        let round = "    if left < 3 { break }\n    left = left - 3\n";
        let plain = with_header(format!(
            "fn f(n: {t}) -> {t} ! diverge {{\n  var left: {t} = n\n  loop {{\n{round}  }}\n  left\n}}\n\nfn main() ! console, alloc, diverge {{\n  print(\"{{f(7)}}\")\n}}\n"
        ));
        let ranged = with_header(format!(
            "fn f(n: {t}) -> {t} {{\n  var left: {t} = n\n  loop i in 0..5 {{\n{round}  }}\n  left\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(7)}}\")\n}}\n"
        ));
        let mut sources = vec![plain, ranged];
        if !ty.signed {
            sources.push(with_header(format!(
                "fn f(n: {t}) -> {t} {{\n  var left: {t} = n\n  loop decreases left {{\n{round}  }}\n  left\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(7)}}\")\n}}\n"
            )));
        }
        for source in sources {
            assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
        }
    }
    // Without the `if`, nothing says `left` is at least 3.
    let bare = "use std.console (console, print)\n\nfn f(n: Byte) -> Byte ! diverge {\n  var left: Byte = n\n  loop {\n    left = left - 3\n  }\n  left\n}\n\nfn main() ! console, alloc, diverge {\n  print(\"{f(7)}\")\n}\n";
    assert!(refused(bare).iter().any(|e| e.contains("left - 3")));
}

fn count_program(with: &str) -> String {
    with_header(format!(
        "fn count() -> u64{effects} {{\n  var left: u64 = 10\n  var times: u64 = 0\n  loop{with} {{\n    if left < 3 {{ break }}\n    left = left - 3\n    times = wrapping_add(times, 1)\n  }}\n  times\n}}\n\nfn main() ! console, alloc{more} {{\n  print(\"{{count()}}\")\n}}\n",
        effects = if with.is_empty() { " ! diverge" } else { "" },
        more = if with.is_empty() { ", diverge" } else { "" },
    ))
}

/// The loop that counts how many threes fit in ten.
fn a_loop_that_decreases_a_measure_ends_without_diverge() {
    prints(&count_program(" decreases left"), vec!["3".into()]);
    // Without `decreases`, the function needs `diverge`.
    let without = count_program("")
        .replace(" ! diverge", "")
        .replace(", diverge", "");
    assert!(
        refused(&without).iter().any(|e| e.contains("`diverge`")),
        "for {without}"
    );
    prints(&count_program(""), vec!["3".into()]);
    // A round that takes nothing off the measure is refused, and the error
    // states what could not be proven of it.
    let stuck = count_program(" decreases left").replace("left - 3", "left - 0");
    let errors = refused(&stuck);
    assert!(
        errors
            .iter()
            .any(|e| e.contains("`left` is smaller than when the round began")),
        "{errors:?} for {stuck}"
    );
}

/// What a round of a loop with a measure does to a counter that starts at
/// `left`: `if left < g { break }`, then `left = left - k`, written in one of
/// two ways.
#[derive(Debug)]
struct Round {
    ty: Int,
    guard: i128,
    step: i128,
    /// Whether the round is an `if` with an `else` that breaks.
    with_else: bool,
}

fn round(g: &mut Gen) -> Round {
    Round {
        ty: *g.pick(INTS),
        guard: g.below(4) as i128,
        step: g.below(4) as i128,
        with_else: g.one_in(2),
    }
}

/// For every `loop decreases e`, the program compiles exactly when `e` is of
/// an unsigned type and each place the loop goes round again has `e` smaller
/// than the round began with.
fn a_loop_with_decreases_compiles_exactly_when_the_measure_is_unsigned_and_gets_smaller() {
    generate::check(53, 160, round, |r| {
        let (t, guard, step) = (r.ty.name, r.guard, r.step);
        let rest = if r.with_else {
            format!(
                "    if left >= {guard} {{\n      left = left - {step}\n    }} else {{\n      break\n    }}\n"
            )
        } else {
            format!("    if left < {guard} {{ break }}\n    left = left - {step}\n")
        };
        let source = with_header(format!(
            "fn f(n: {t}) -> {t} {{\n  var left: {t} = n\n  loop decreases left {{\n{rest}  }}\n  left\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(5)}}\")\n}}\n"
        ));
        let errors = refused(&source);
        let fits = step <= guard;
        let smaller = step >= 1;
        assert_eq!(
            errors.is_empty(),
            !r.ty.signed && fits && smaller,
            "{errors:?} for {source}"
        );
        let named = if r.ty.signed {
            "`decreases`".to_string()
        } else if !fits {
            format!("left - {step}")
        } else {
            "`left` is smaller than when the round began".to_string()
        };
        if !errors.is_empty() {
            assert!(
                errors.iter().any(|e| e.contains(&named)),
                "{errors:?} does not mention {named} for {source}"
            );
        }
    });
}

/// A round that always ends in `break` has no place where the loop goes
/// round again, so it needs no smaller measure.
fn a_round_that_always_ends_in_break_has_no_place_to_go_round_again() {
    for ty in INTS.iter().filter(|t| !t.signed) {
        let t = ty.name;
        for round in [
            "left = wrapping_add(left, 1)\n    break",
            "if left == 0 { break } else { break }",
        ] {
            let source = with_header(format!(
                "fn f(n: {t}) -> {t} {{\n  var left: {t} = n\n  loop decreases left {{\n    {round}\n  }}\n  left\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(5)}}\")\n}}\n"
            ));
            assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
        }
    }
}

/// A counter of an unsigned type that starts at `start`, and has `step`
/// taken off each round while it is at least `step`.
#[derive(Debug)]
struct Countdown {
    ty: Int,
    start: i128,
    step: i128,
}

fn countdown(g: &mut Gen) -> Countdown {
    let ty = *g.pick(
        &INTS
            .iter()
            .copied()
            .filter(|t| !t.signed)
            .collect::<Vec<_>>(),
    );
    let max = ty.max();
    let (start, step) = match g.below(3) {
        // At the limit, in a few steps.
        0 => (max, max / (1 + g.below(6) as i128)),
        // A few steps, with something left over.
        1 => {
            let step = 1 + g.below(3) as i128;
            (
                g.below(7) as i128 * step + g.below(step as usize) as i128,
                step,
            )
        }
        // Not enough for one step, or nothing.
        _ => (g.below(3) as i128, max),
    };
    Countdown { ty, start, step }
}

/// For every program with a `loop decreases` that the compiler accepts, run
/// from the limits of its type, the loop ends, after as many rounds as the
/// steps that fit.
fn every_loop_with_decreases_that_compiles_ends() {
    generate::check(59, 120, countdown, |c| {
        let (t, start, step) = (c.ty.name, c.start, c.step);
        let source = with_header(format!(
            "fn rounds(n: {t}) -> u64 {{\n  var left: {t} = n\n  var times: u64 = 0\n  loop decreases left {{\n    if left < {step} {{ break }}\n    left = left - {step}\n    times = wrapping_add(times, 1)\n  }}\n  times\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{rounds({start})}}\")\n}}\n"
        ));
        prints(&source, vec![(start / step).to_string()]);
    });
}

/// The measure is never computed: the program is that of the same loop
/// without `decreases`.
fn a_measure_is_never_computed_when_the_program_runs() {
    for ty in INTS.iter().filter(|t| !t.signed) {
        let t = ty.name;
        let program = |head: &str, effects: &str| {
            with_header(format!(
                "fn f(n: {t}) -> {t}{effects} {{\n  var left: {t} = n\n  loop{head} {{\n    if left < 3 {{ break }}\n    left = left - 3\n  }}\n  left\n}}\n\nfn main() ! console, alloc{effects2} {{\n  print(\"{{f(10)}}\")\n}}\n",
                effects2 = if effects.is_empty() { "" } else { ", diverge" },
            ))
        };
        let with = lower(program(" decreases left", "").as_bytes()).expect("compiles");
        let without = lower(program("", " ! diverge").as_bytes()).expect("compiles");
        assert_eq!(with, without, "for {t}");
    }
}

/// For every program with a loop over a range, what it runs each time round
/// is the counter and one comparison: one addition, one comparison, no call,
/// and nothing asked of the system for memory.
fn a_loop_over_a_range_is_one_counter_and_one_comparison_each_time_round() {
    for ty in INTS {
        let source = with_header(format!(
            "fn main() ! console {{\n  a: {t} = {a}\n  b: {t} = {b}\n  loop i in a..b {{ print(\"x\") }}\n}}\n",
            t = ty.name,
            a = ty.min(),
            b = ty.max()
        ));
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
        .any(|(at, block)| block.end.successors().iter().any(|&to| to <= at));
        assert!(loops, "no loop in {source}");
    }
}

/// A range of two literals is one number, whose type is decided by how the
/// counter is used, as for a literal bound to a name, and is `Int` when
/// nothing decides.
fn two_literal_ends_of_a_range_take_the_type_the_counter_is_used_as() {
    for ty in INTS {
        let (t, max) = (ty.name, ty.max());
        let source = |from: i128, by: i128| {
            with_header(format!(
                "fn f() -> {t} {{\n  var s: {t} = 0\n  loop i in {from}..{max} {{ s = i + {by} }}\n  s\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f()}}\")\n}}\n"
            ))
        };
        prints(&source(max - 3, 1), vec![max.to_string()]);
        let too_far = source(max - 3, 2);
        let errors = refused(&too_far);
        assert!(
            errors.iter().any(|e| e.contains("i + 2")),
            "{errors:?} does not state the goal about `i + 2` for {too_far}"
        );
    }
    // Nothing decides, so the numbers are `Int`.
    let nothing = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  loop i in 0..3 { print(\"{i}\") }\n}\n";
    prints(nothing, vec!["0".into(), "1".into(), "2".into()]);
}
