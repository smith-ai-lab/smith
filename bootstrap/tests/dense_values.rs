//! How the passes number the values they leave: from 0, with none left out, in
//! the order they are defined. A target gives a place in a frame to every
//! number below the count of a function's values that has no register, so a
//! number that nothing defines any more would be a place for nothing.

use smith::scalar::{self, Function, Program};

use crate::Test;
use crate::generate;
use crate::programs;

pub const ONCE: &[Test] = &[
    (
        "the_values_a_function_is_left_with_are_numbered_from_0_with_none_left_out_in_the_order_they_are_defined",
        the_values_a_function_is_left_with_are_numbered_from_0_with_none_left_out_in_the_order_they_are_defined,
    ),
    (
        "a_program_whose_passes_take_values_away_is_left_with_none_left_out",
        a_program_whose_passes_take_values_away_is_left_with_none_left_out,
    ),
];

/// The numbers a function defines, in the order its blocks define them: the
/// parameters of a block, then what each of its instructions makes.
fn defined(f: &Function) -> Vec<usize> {
    let mut numbers = Vec::new();
    for block in &f.blocks {
        numbers.extend(block.params.iter().map(|p| p.0));
        for i in &block.instructions {
            numbers.extend(i.results.iter().map(|r| r.0));
        }
    }
    numbers
}

fn assert_dense(program: &Program, what: &str) {
    for (k, f) in program.functions.iter().enumerate() {
        let numbers = defined(f);
        let wanted: Vec<usize> = (0..numbers.len()).collect();
        assert_eq!(numbers, wanted, "function {k} of {what}");
        assert_eq!(f.types.len(), numbers.len(), "the types of function {k} of {what}");
    }
}

fn the_values_a_function_is_left_with_are_numbered_from_0_with_none_left_out_in_the_order_they_are_defined() {
    generate::check(9301, 20, programs::program, |case| {
        if case.mistake.is_some() {
            return;
        }
        let program = scalar::passes(&smith::lower(&case.source).expect("a program that builds"));
        assert_dense(&program, &format!("{case:?}"));
    });
}

/// Programs whose threading and folding make values that nothing reads after,
/// which the passes leave out of the instructions and not out of the numbers.
fn a_program_whose_passes_take_values_away_is_left_with_none_left_out() {
    for (name, source) in [
        ("bits_narrow", include_str!("cases/bits_narrow.smith")),
        ("compare", include_str!("cases/compare.smith")),
    ] {
        let lowered = smith::lower(source.as_bytes()).expect("a program that builds");
        let before: usize = lowered.functions.iter().map(|f| f.types.len()).sum();
        let program = scalar::passes(&lowered);
        let after: usize = program.functions.iter().map(|f| f.types.len()).sum();
        assert!(after < before, "{name}: the passes took no value away, so the case shows nothing");
        assert_dense(&program, name);
    }
}
