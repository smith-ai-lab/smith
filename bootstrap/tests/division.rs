//! Whole-number division and remainder in the scalar form: the quotient and
//! what is left over, as unsigned and as signed numbers, rounding toward zero.
//! An instruction is given a divisor that is not zero and, for the signed ones,
//! not the least number of the size divided by -1. What a program does that
//! gives otherwise is not defined, as the processors differ, so no test gives
//! it to a target: the evaluator refuses it, and no pass may move a division to
//! where the source would not have run it.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Function, Program, Type};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::generate::{self, Gen};
use crate::targets::{runnable, scalar_runs_as};
use crate::vector_form as vf;

pub const ONCE: &[Test] = &[
    (
        "the_evaluator_divides_as_arithmetic_does_for_every_size_and_sign",
        the_evaluator_divides_as_arithmetic_does_for_every_size_and_sign,
    ),
    (
        "the_evaluator_refuses_a_division_by_zero_and_the_least_number_by_minus_one_and_only_those",
        the_evaluator_refuses_a_division_by_zero_and_the_least_number_by_minus_one_and_only_those,
    ),
    (
        "an_arm64_executable_of_a_division_gives_what_the_evaluator_gives",
        an_arm64_executable_of_a_division_gives_what_the_evaluator_gives,
    ),
    (
        "no_pass_runs_a_division_where_the_source_would_not_have_run_it",
        no_pass_runs_a_division_where_the_source_would_not_have_run_it,
    ),
    (
        "an_arm64_executable_runs_a_division_only_where_the_source_would_have_run_it",
        an_arm64_executable_runs_a_division_only_where_the_source_would_have_run_it,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_of_a_division_gives_what_the_evaluator_gives",
        every_executable_of_a_division_gives_what_the_evaluator_gives,
    ),
    (
        "no_executable_runs_a_division_where_the_source_would_not_have_run_it",
        no_executable_runs_a_division_where_the_source_would_not_have_run_it,
    ),
];

const OPERATIONS: [Binary; 4] = [Binary::DivU, Binary::DivS, Binary::RemU, Binary::RemS];

fn is_signed(op: Binary) -> bool {
    matches!(op, Binary::DivS | Binary::RemS)
}

fn mask(bits: Bits) -> u64 {
    match bits {
        Bits::B64 => u64::MAX,
        _ => (1 << bits.count()) - 1,
    }
}

/// The number of `bits` bits that `n` stands for, read as a signed number.
fn signed(n: u64, bits: Bits) -> i128 {
    let up = 64 - bits.count();
    (((n << up) as i64) >> up) as i128
}

/// The least signed number of the size, as bits.
fn least(bits: Bits) -> u64 {
    1 << (bits.count() - 1)
}

/// Whether an instruction may not be given these: a divisor of zero, or, for the
/// signed ones, the least number of the size divided by -1.
fn forbidden(op: Binary, bits: Bits, a: u64, b: u64) -> bool {
    b == 0 || (is_signed(op) && b == mask(bits) && a == least(bits))
}

/// What arithmetic says `op` of `a` and `b` is, as bits of the size: the
/// quotient or what is left of the numbers read as unsigned or as signed,
/// rounded toward zero, which `i128` does, whatever the size.
fn arithmetic(op: Binary, bits: Bits, a: u64, b: u64) -> u64 {
    let (x, y) = if is_signed(op) {
        (signed(a, bits), signed(b, bits))
    } else {
        (i128::from(a), i128::from(b))
    };
    let r = match op {
        Binary::DivU | Binary::DivS => x / y,
        _ => x % y,
    };
    r as u64 & mask(bits)
}

/// The numbers where a mistake of the size, of the sign, or of how it rounds
/// shows, for the size: zero, one, two, three, the greatest and the least and the
/// one beside it, and -1, which is also the greatest unsigned number.
fn edges(bits: Bits) -> Vec<u64> {
    let m = mask(bits);
    let l = least(bits);
    let mut edges = vec![0, 1, 2, 3, 7, 10, l - 1, l, l + 1, m - 1, m, m / 3];
    edges.sort_unstable();
    edges.dedup();
    edges.into_iter().map(|n| n & m).collect()
}

fn number(g: &mut Gen, bits: Bits) -> u64 {
    if g.one_in(2) {
        *g.pick(&edges(bits))
    } else {
        ((g.below(1 << 30) as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15) ^ g.below(1 << 30) as u64)
            & mask(bits)
    }
}

/// An operation, a size, and two numbers of it that the operation may be
/// given: where the generator draws a pair that it may not, the divisor is 1.
fn allowed(g: &mut Gen) -> (Binary, Bits, u64, u64) {
    let op = *g.pick(&OPERATIONS);
    let bits = *g.pick(&Bits::ALL);
    let a = number(g, bits);
    let mut b = number(g, bits);
    if forbidden(op, bits, a, b) {
        b = 1;
    }
    (op, bits, a, b)
}

/// An operation, a size, and two numbers of it that are any, including the ones
/// the operation may not be given.
fn any(g: &mut Gen) -> (Binary, Bits, u64, u64) {
    let op = *g.pick(&OPERATIONS);
    let bits = *g.pick(&Bits::ALL);
    (op, bits, number(g, bits), number(g, bits))
}

fn function(op: Binary, bits: Bits) -> Function {
    let ty = Type::Int(bits);
    let mut b = Builder::new();
    let p = b.params(&[ty, ty]);
    let r = b.binary(op, p[0], p[1]);
    b.ret(vec![r]);
    b.finish(&[ty])
}

/// A program that gives `a` and `b` to a function that is `op` of them, and
/// writes what it gives, with the numbers through memory when `opaque`.
fn dividing(op: Binary, bits: Bits, a: u64, b: u64, opaque: bool) -> Program {
    observed(function(op, bits), bits, a, b, opaque)
}

fn observed(f: Function, bits: Bits, a: u64, b: u64, opaque: bool) -> Program {
    let ty = Type::Int(bits);
    let program = vf::observed(vec![f], 0, &[(ty, u128::from(a)), (ty, u128::from(b))], opaque);
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

/// What the program writes, as the number in its one slot.
fn written(program: &Program) -> u64 {
    vf::written(&scalar::run(program).stdout)[0] as u64
}

fn the_evaluator_divides_as_arithmetic_does_for_every_size_and_sign() {
    generate::check(7381, 240, allowed, |(op, bits, a, b)| {
        let program = dividing(*op, *bits, *a, *b, true);
        assert_eq!(written(&program), arithmetic(*op, *bits, *a, *b));
        // What is left over has the sign of the first, and the two make it.
        if is_signed(*op) {
            let q = signed(arithmetic(Binary::DivS, *bits, *a, *b), *bits);
            let r = signed(arithmetic(Binary::RemS, *bits, *a, *b), *bits);
            assert_eq!(q * signed(*b, *bits) + r, signed(*a, *bits));
            assert!(r == 0 || (r < 0) == (signed(*a, *bits) < 0));
        }
    });
}

fn the_evaluator_refuses_a_division_by_zero_and_the_least_number_by_minus_one_and_only_those() {
    generate::check(7382, 240, any, |(op, bits, a, b)| {
        let program = dividing(*op, *bits, *a, *b, true);
        let ran = std::panic::catch_unwind(|| scalar::run(&program));
        assert_eq!(ran.is_err(), forbidden(*op, *bits, *a, *b), "{op:?} {bits:?} {a} {b}");
    });
}

/// The executables for arm64 of the program, which the machine in `aarch64` runs.
fn arm64(program: &Program) -> Vec<Vec<u8>> {
    Target::ALL
        .iter()
        .map(|t| t.emit(program))
        .filter(|bytes| aarch64::is_executable(bytes))
        .collect()
}

fn an_arm64_executable_of_a_division_gives_what_the_evaluator_gives() {
    generate::check(7383, 120, allowed, |(op, bits, a, b)| {
        for opaque in [true, false] {
            let program = dividing(*op, *bits, *a, *b, opaque);
            let said = scalar::run(&program);
            let executables = arm64(&program);
            assert!(!executables.is_empty(), "no target is built for arm64");
            for bytes in executables {
                let ran = aarch64::run(&bytes);
                assert_eq!((ran.stdout, ran.status), (said.stdout.clone(), said.status));
            }
        }
    });
}

fn every_executable_of_a_division_gives_what_the_evaluator_gives() {
    generate::check(7384, 60, allowed, |(op, bits, a, b)| {
        let program = dividing(*op, *bits, *a, *b, true);
        let said = scalar::run(&program);
        let passed = scalar::passes(&program);
        for target in runnable() {
            scalar_runs_as(&program, target, &said);
            scalar_runs_as(&passed, target, &said);
        }
    });
}

/// A function of two numbers that is `op` of them where the operation may be
/// given them and 0 where it may not, in one of two forms: the test is made
/// where the choice is, or in a block before it that only passes it on, which
/// threading sends past.
fn guarded(op: Binary, bits: Bits, passed_on: bool) -> Function {
    let ty = Type::Int(bits);
    let mut b = Builder::new();
    let p = b.params(&[ty, ty]);
    let zero = b.int(bits, 0);
    let mut bad = b.compare(Compare::Equal, p[1], zero);
    if is_signed(op) {
        let smallest = b.int(bits, least(bits));
        let minus_one = b.int(bits, mask(bits));
        let is_least = b.compare(Compare::Equal, p[0], smallest);
        let is_minus_one = b.compare(Compare::Equal, p[1], minus_one);
        let both = b.binary(Binary::And, is_least, is_minus_one);
        bad = b.binary(Binary::Or, bad, both);
    }
    let (join, joined) = b.block(&[ty]);
    let (work, _) = b.block(&[]);
    let (skip, _) = b.block(&[]);
    if passed_on {
        let (choose, chosen) = b.block(&[Type::Int(Bits::B8)]);
        b.jump(choose, vec![bad]);
        b.at(choose);
        b.switch(chosen[0], vec![(work, vec![]), (skip, vec![])]);
    } else {
        b.switch(bad, vec![(work, vec![]), (skip, vec![])]);
    }
    b.at(work);
    let q = b.binary(op, p[0], p[1]);
    b.jump(join, vec![q]);
    b.at(skip);
    let nothing = b.int(bits, 0);
    b.jump(join, vec![nothing]);
    b.at(join);
    b.ret(joined);
    b.finish(&[ty])
}

/// A guarded division, and numbers for it that are any, including the ones the
/// division may not be given, which the guard is there for.
fn guarded_case(g: &mut Gen) -> (Binary, Bits, bool, bool, u64, u64) {
    let (op, bits, a, b) = any(g);
    (op, bits, g.one_in(2), g.one_in(2), a, b)
}

fn guarded_program(case: &(Binary, Bits, bool, bool, u64, u64)) -> Program {
    let (op, bits, passed_on, opaque, a, b) = *case;
    observed(guarded(op, bits, passed_on), bits, a, b, opaque)
}

/// The program with every pass in turn and all of them, which the evaluator runs
/// as it ran the first, and so never reaches a division that is given what it may
/// not be given if the first did not.
fn no_pass_runs_a_division_where_the_source_would_not_have_run_it() {
    generate::check(7385, 240, guarded_case, |case| {
        let program = guarded_program(case);
        let said = scalar::run(&program);
        let (op, bits, _, _, a, b) = *case;
        let expected = if forbidden(op, bits, a, b) { 0 } else { arithmetic(op, bits, a, b) };
        assert_eq!(vf::written(&said.stdout)[0] as u64, expected);
        let inlined = scalar::inline(program.clone());
        let trimmed = scalar::trim(inlined.clone());
        let threaded = scalar::thread(trimmed.clone());
        let folded = scalar::fold(threaded.clone());
        let shared = scalar::common(folded.clone());
        let removed = scalar::remove(shared.clone());
        // Each pass on its own, on the program as inlining makes it, where the
        // numbers a call gave are known inside the function and the block of a
        // division that the guard excludes is still there; and in a row.
        let alone = [
            ("trim", scalar::trim(inlined.clone())),
            ("thread", scalar::thread(inlined.clone())),
            ("fold", scalar::fold(inlined.clone())),
            ("common", scalar::common(inlined.clone())),
            ("remove", scalar::remove(inlined.clone())),
        ];
        let row = [
            ("inline", inlined.clone()),
            ("trim after inline", trimmed),
            ("thread after trim", threaded),
            ("fold after thread", folded),
            ("common after fold", shared),
            ("remove after common", removed),
            ("passes", scalar::passes(&program)),
        ];
        for (name, after) in alone.iter().chain(&row) {
            assert_eq!(scalar::check(after), Ok(()), "{name}");
            // `run` panics on a division that it may not be given.
            assert_eq!(&scalar::run(after), &said, "{name} for {case:?}");
        }
    });
}

fn an_arm64_executable_runs_a_division_only_where_the_source_would_have_run_it() {
    generate::check(7386, 120, guarded_case, |case| {
        let program = guarded_program(case);
        let said = scalar::run(&program);
        let passed = scalar::passes(&program);
        for p in [&program, &passed] {
            let executables = arm64(p);
            assert!(!executables.is_empty(), "no target is built for arm64");
            for bytes in executables {
                let ran = aarch64::run(&bytes);
                assert_eq!((ran.stdout, ran.status), (said.stdout.clone(), said.status));
            }
        }
    });
}

fn no_executable_runs_a_division_where_the_source_would_not_have_run_it() {
    generate::check(7387, 80, guarded_case, |case| {
        let program = guarded_program(case);
        let said = scalar::run(&program);
        let passed = scalar::passes(&program);
        for target in runnable() {
            scalar_runs_as(&program, target, &said);
            scalar_runs_as(&passed, target, &said);
        }
    });
}
