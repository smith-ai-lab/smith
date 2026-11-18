//! A call of a function no larger than the bound is replaced by the function,
//! wherever it is called from and however often, and a call of a larger one
//! is not, and the program prints what it printed without the pass.
//!
//! The functions are made larger one statement at a time, so the tests say
//! nothing of what the bound is: they find where it lies from the sizes the
//! lowering gives.

use smith::scalar::{self, Constant, Function, Op, Program};
use smith::lower;

use crate::Test;
use crate::generate;

pub const ONCE: &[Test] = &[
    (
        "a_function_called_from_three_places_is_inlined_when_no_larger_than_the_bound_and_not_when_larger",
        a_function_called_from_three_places_is_inlined_when_no_larger_than_the_bound_and_not_when_larger,
    ),
    (
        "a_program_with_a_function_of_any_size_prints_what_it_printed_before_inlining",
        a_program_with_a_function_of_any_size_prints_what_it_printed_before_inlining,
    ),
];

/// The number that the function multiplies by, to find it again.
const MARK: u64 = 7_654_321;

/// A program that calls `grow` from three places in a loop, `lines` of
/// which are statements that read what it is given.
fn source(lines: usize) -> String {
    let mut s = String::from("use std.int (wrapping_add, wrapping_mul)\n\nfn grow(a: i64) -> i64 {\n");
    s.push_str(&format!("  var r = wrapping_mul(a, {MARK})\n"));
    for _ in 0..lines {
        s.push_str("  r = wrapping_add(r, a)\n");
    }
    s.push_str("  r\n}\n\nfn main() ! console, alloc {\n  var t: i64 = 0\n");
    s.push_str("  loop i in 0..3 {\n    t = wrapping_add(t, grow(i))\n    t = wrapping_add(t, grow(t))\n  }\n");
    s.push_str("  print(\"{t}\")\n  print(\"{grow(5)}\")\n}\n");
    s
}

fn built(lines: usize) -> Program {
    let source = source(lines);
    lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// Whether a function that multiplies by the mark, but `main`, is called.
fn is_called(program: &Program) -> bool {
    let holds = |f: &Function| {
        (f.blocks.iter().flat_map(|b| &b.instructions))
            .any(|i| i.op == Op::Constant(Constant::Int(MARK)))
    };
    (program.functions.iter().flat_map(|f| &f.blocks))
        .flat_map(|b| &b.instructions)
        .any(|i| match i.op {
            Op::Call { function, .. } => function != program.main && holds(&program.functions[function]),
            _ => false,
        })
}

/// How many statements make the function as large as it can be and still be
/// inlined: one more is called. What the function calls is inlined into it
/// first, so its size is not that of the lowering.
fn most_that_fit() -> usize {
    let mut lines = 0;
    while !is_called(&scalar::inline(&built(lines + 1))) {
        lines += 1;
        assert!(lines < 1000, "no function is too large to be inlined");
    }
    lines
}

fn a_function_called_from_three_places_is_inlined_when_no_larger_than_the_bound_and_not_when_larger() {
    let fits = most_that_fit();
    assert!(fits > 0, "a function with nothing in it is called");
    assert!(!is_called(&scalar::inline(&built(fits))));
    // The function that is called is larger than the bound, by the size
    // that the rule counts.
    let after = scalar::inline(&built(fits + 1));
    let grow = after.functions.iter().enumerate().find(|(n, f)| {
        *n != after.main
            && (f.blocks.iter().flat_map(|b| &b.instructions))
                .any(|i| i.op == Op::Constant(Constant::Int(MARK)))
    });
    assert!(scalar::size(grow.unwrap().1) > scalar::BOUND);
}

fn a_program_with_a_function_of_any_size_prints_what_it_printed_before_inlining() {
    let fits = most_that_fit();
    generate::check(
        397,
        6,
        |g| g.below(fits * 2 + 2),
        |lines| {
            let program = built(*lines);
            let said = scalar::run(&program);
            let inlined = scalar::inline(&program);
            scalar::check(&inlined).unwrap();
            assert_eq!(scalar::run(&inlined), said, "for {}", source(*lines));
            assert_eq!(scalar::inline(&program), inlined, "twice, for {}", source(*lines));
            assert_eq!(scalar::run(&scalar::passes(&program)), said, "for {}", source(*lines));
        },
    );
}
