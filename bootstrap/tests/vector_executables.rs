//! Vectors in the executables of the chips that build them: each instruction of
//! vectors, and functions of vectors that are generated, give what the evaluator
//! gives, at the edges of each lane and in functions that hold more vectors than
//! any chip has registers for.

use smith::scalar::{self, Program};
use smith::target::Target;

use crate::Test;
use crate::generate;
use crate::targets::{runnable, scalar_runs_as};
use crate::vector_form as vf;

pub const MACHINE: &[Test] = &[
    (
        "a_jump_that_swaps_a_vector_and_a_float_between_two_registers_gives_each_what_it_was_given",
        a_jump_that_swaps_a_vector_and_a_float_between_two_registers_gives_each_what_it_was_given,
    ),
    (
        "every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors",
        every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors,
    ),
    (
        "every_executable_of_a_function_of_vectors_does_what_the_evaluator_says",
        every_executable_of_a_function_of_vectors_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says",
        every_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says,
    ),
];

/// The targets that build vectors and that this machine runs, or that a
/// fingerprint is made for.
fn building() -> Vec<Target> {
    (runnable().into_iter())
        .filter(|t| t.builds_vectors())
        .collect()
}

fn check(program: &Program) {
    assert_eq!(scalar::check(program), Ok(()));
    let said = scalar::run(program);
    let passed = scalar::passes(program);
    for target in building() {
        scalar_runs_as(program, target, &said);
        // A product that a sum uses is fused in the passed form, so its answer is
        // the evaluator's of that form.
        scalar_runs_as(&passed, target, &scalar::run(&passed));
    }
}

fn every_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors() {
    for program in vf::instruction_programs() {
        check(&program);
    }
}

fn every_executable_of_a_function_of_vectors_does_what_the_evaluator_says() {
    generate::check(8132, 60, vf::canonical_program, check);
}

fn every_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says()
 {
    generate::check(8133, 40, vf::pressure, check);
}

/// The program of these choices of the generator is one whose jump gives a block a
/// vector of floats in the register that held a float of 64 bits, and the float in
/// the register that held another, in a circle of copies of two widths, which a
/// target breaks by holding one aside.
fn a_jump_that_swaps_a_vector_and_a_float_between_two_registers_gives_each_what_it_was_given() {
    let choices: Vec<usize> = [1, 9, 7, 2, 3, 9, 5, 9, 9, 4, 6, 6, 3, 6, 5, 2, 0, 3, 1, 2695308200, 1401260015, 0, 5, 0, 1, 1, 1503573921, 862470798, 1, 1284784296, 1968849449, 1, 1757460756, 4224843110, 0, 1, 1, 9, 1, 3, 1, 2504141328, 3542624836, 0, 6, 1, 2299686648, 2265904165, 1, 1430947148, 1365558086, 0, 0, 6, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 7, 5, 0, 0, 0, 8, 5, 1, 0, 0, 0, 1, 0, 1, 1, 9, 3, 0, 1, 1, 9, 0, 1, 29, 29, 1, 1, 3, 1, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0].to_vec();
    check(&vf::canonical_program(&mut generate::Gen::replay(&choices)));
}
