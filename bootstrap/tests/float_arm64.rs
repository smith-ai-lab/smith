//! Floats in the executables for arm64, checked on a machine of any kind, as the
//! machine `aarch64` is: each instruction of floats at the edges of each format,
//! and functions of floats and whole numbers that are generated, give what the
//! evaluator gives. The tests that run an executable on the machine that runs
//! the tests cannot see the code of a target that the machine cannot run.
//!
//! No test names a target: an executable is one for arm64 when its file says so.

use smith::scalar::{self, FloatBinary, FloatBits, Program, Type};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::float_form::{self as ff, canonical_program, pressure};
use crate::float_fused;
use crate::vector_form as vf;
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
        "an_arm64_executable_of_each_fused_form_gives_what_the_evaluator_gives_where_the_exact_result_is_a_zero",
        an_arm64_executable_of_each_fused_form_gives_what_the_evaluator_gives_where_the_exact_result_is_a_zero,
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

/// A number of the size, as bits, and the number negated when `negative`.
fn number(bits: FloatBits, x: i32, negative: bool) -> u64 {
    let x = if negative { -x } else { x };
    match bits {
        FloatBits::F32 => u64::from((x as f32).to_bits()),
        FloatBits::F64 => (f64::from(x)).to_bits(),
    }
}

/// A fused shape whose three arguments make an exact result that is zero, of
/// either sign, or is one that is not: `a * b` and `c` are the same number,
/// or `c` is a zero of either sign.
fn cancelling(g: &mut generate::Gen) -> (Type, FloatBinary, bool, [u128; 3]) {
    let all = float_fused::types();
    let ty = all[g.below(all.len())];
    let bits = float_fused::bits_of(ty);
    let op = [FloatBinary::Add, FloatBinary::Sub][g.below(2)];
    // For each lane: the factors, the sign of the product, and what the third
    // argument is: the product, its negation, or a zero of either sign.
    let lane = |g: &mut generate::Gen| -> [u64; 3] {
        let (x, y) = (g.below(4) as i32, g.below(4) as i32);
        let (nx, ny) = (g.below(2) == 0, g.below(2) == 0);
        let product = x * y;
        let negative = nx != ny;
        let c = match g.below(4) {
            0 => number(bits, product, negative),
            1 => number(bits, product, !negative),
            2 => number(bits, 0, false),
            _ => number(bits, 0, true),
        };
        [number(bits, x, nx), number(bits, y, ny), c]
    };
    let lanes: Vec<[u64; 3]> = (0..4).map(|_| lane(g)).collect();
    let args = [0, 1, 2].map(|k| match ty {
        Type::Vector(l) => vf::made(l, |i| lanes[i as usize % 4][k]),
        _ => u128::from(lanes[0][k]),
    });
    (ty, op, g.below(2) == 0, args)
}

fn an_arm64_executable_of_each_fused_form_gives_what_the_evaluator_gives_where_the_exact_result_is_a_zero()
 {
    generate::check(6172, 60, cancelling, |(ty, op, first, args)| {
        let program = float_fused::program(*ty, float_fused::shape(*ty, *op, *first), args);
        check(&program);
    });
}
