//! Vectors in the executables for arm64, checked on a machine of any kind, as the
//! machine `aarch64` is: each instruction of vectors at the edges of each lane,
//! and functions of vectors that are generated, give what the evaluator gives. The
//! tests that run an executable on the machine that runs the tests cannot see the
//! code of a target that the machine cannot run.
//!
//! No test names a target: an executable is one for arm64 when its file says so.

use smith::scalar::{self, Program};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::generate;
use crate::vector_form as vf;

pub const ONCE: &[Test] = &[
    (
        "an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors",
        an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors,
    ),
    (
        "an_arm64_executable_of_a_function_of_vectors_does_what_the_evaluator_says",
        an_arm64_executable_of_a_function_of_vectors_does_what_the_evaluator_says,
    ),
    (
        "an_arm64_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says",
        an_arm64_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says,
    ),
    (
        "an_arm64_executable_where_a_jump_swaps_a_vector_and_a_float_gives_each_what_it_was_given",
        an_arm64_executable_where_a_jump_swaps_a_vector_and_a_float_gives_each_what_it_was_given,
    ),
];

/// Runs the program and its passes for each target that is an executable for
/// arm64, and checks that each does what the evaluator says.
fn check(program: &Program) {
    assert_eq!(scalar::check(program), Ok(()));
    let said = scalar::run(program);
    let passed = scalar::passes(program);
    let mut ran = 0;
    for target in Target::ALL.iter().filter(|t| t.builds_vectors()) {
        for p in [program, &passed] {
            let bytes = target.emit(p);
            if aarch64::is_executable(&bytes) {
                let got = aarch64::run(&bytes);
                assert_eq!(
                    (got.stdout, got.status),
                    (said.stdout.clone(), said.status),
                    "{}",
                    target.name()
                );
                ran += 1;
            }
        }
    }
    assert!(ran > 0, "no target is an executable for arm64");
}

fn an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_vectors() {
    for program in vf::instruction_programs() {
        check(&program);
    }
}

fn an_arm64_executable_of_a_function_of_vectors_does_what_the_evaluator_says() {
    generate::check(8136, 40, vf::canonical_program, check);
}

fn an_arm64_executable_of_a_function_that_holds_more_vectors_than_registers_does_what_the_evaluator_says()
 {
    generate::check(8137, 30, vf::pressure, check);
}

/// The program of these choices of the generator is one whose jump gives a block a
/// vector of floats in the register that held a float of 64 bits, and the float in
/// the register that held another, in a circle of copies of two widths, which a
/// target breaks by holding one aside.
fn an_arm64_executable_where_a_jump_swaps_a_vector_and_a_float_gives_each_what_it_was_given() {
    let choices: Vec<usize> = CHOICES.to_vec();
    check(&vf::canonical_program(&mut generate::Gen::replay(&choices)));
}

const CHOICES: [usize; 117] = [1, 9, 7, 2, 3, 9, 5, 9, 9, 4, 6, 6, 3, 6, 5, 2, 0, 3, 1, 2695308200, 1401260015, 0, 5, 0, 1, 1, 1503573921, 862470798, 1, 1284784296, 1968849449, 1, 1757460756, 4224843110, 0, 1, 1, 9, 1, 3, 1, 2504141328, 3542624836, 0, 6, 1, 2299686648, 2265904165, 1, 1430947148, 1365558086, 0, 0, 6, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 7, 5, 0, 0, 0, 8, 5, 1, 0, 0, 0, 1, 0, 1, 1, 9, 3, 0, 1, 1, 9, 0, 1, 29, 29, 1, 1, 3, 1, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
