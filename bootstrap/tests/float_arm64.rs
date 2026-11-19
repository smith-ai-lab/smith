//! Floats in the executables for arm64, checked on a machine of any kind, as the
//! machine `aarch64` is: each instruction of floats at the edges of each format,
//! and functions of floats and whole numbers that are generated, give what the
//! evaluator gives. The tests that run an executable on the machine that runs
//! the tests cannot see the code of a target that the machine cannot run.
//!
//! No test names a target: an executable is one for arm64 when its file says so.

use smith::scalar::{self, Program};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::float_form::{self as ff, canonical_program, pressure};
use crate::generate;

pub const ONCE: &[Test] = &[
    (
        "an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats",
        an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats,
    ),
    (
        "an_arm64_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says",
        an_arm64_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says,
    ),
    (
        "an_arm64_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says",
        an_arm64_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says,
    ),
];

/// Runs the program and its passes for each target that is an executable for
/// arm64, and checks that each does what the evaluator says.
fn check(program: &Program) {
    // The form as it is made: a product that one sum uses is fused.
    let program = &scalar::fuse(program);
    assert_eq!(scalar::check(program), Ok(()));
    let said = scalar::run(program);
    let passed = scalar::passes(program);
    let mut ran = 0;
    for target in Target::ALL.iter().filter(|t| t.builds_floats()) {
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

fn an_arm64_executable_gives_what_the_evaluator_gives_for_each_instruction_of_floats() {
    for program in ff::instruction_programs() {
        check(&program);
    }
}

fn an_arm64_executable_of_a_function_of_floats_and_whole_numbers_does_what_the_evaluator_says() {
    generate::check(6135, 40, canonical_program, check);
}

fn an_arm64_executable_of_a_function_that_holds_more_floats_than_registers_does_what_the_evaluator_says()
 {
    generate::check(6136, 30, pressure, check);
}
