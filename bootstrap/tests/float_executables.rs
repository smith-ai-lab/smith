//! Floats in the executables of the chips that build them: each instruction of
//! floats, and functions of floats and whole numbers that are generated, give
//! what the evaluator gives, at the edges of each format and in functions that
//! hold more floats than any chip has registers for.

use smith::scalar::{self, Program};
use smith::target::Target;

use crate::Test;
use crate::float_form::{
    self as ff, canonical_program, pressure,
};
use crate::generate;
use crate::targets::{runnable, scalar_runs_as};

pub const MACHINE: &[Test] = &[
    (
        "every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats",
        every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats,
    ),
    (
        "every_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says",
        every_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says",
        every_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says,
    ),
];

/// The targets that build floats and that this machine runs, or that a
/// fingerprint is made for.
fn building() -> Vec<Target> {
    (runnable().into_iter())
        .filter(|t| t.builds_floats())
        .collect()
}

fn check(program: &Program) {
    assert_eq!(scalar::check(program), Ok(()));
    let said = scalar::run(program);
    let passed = scalar::passes(program);
    for target in building() {
        scalar_runs_as(program, target, &said);
        scalar_runs_as(&passed, target, &said);
    }
}

fn every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats() {
    for program in ff::instruction_programs() {
        check(&program);
    }
}

fn every_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says() {
    generate::check(6132, 60, canonical_program, check);
}

fn every_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says()
{
    generate::check(6133, 40, pressure, check);
}

