//! Integers narrower than a register, held in more places than a function
//! has registers: what a chip must do to keep each one the number it is,
//! whichever register or place of the frame it lives in.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Op, Value};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets::{runnable, scalar_runs_as};

pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_function_that_holds_many_integers_does_what_the_evaluator_says",
    every_executable_of_a_function_that_holds_many_integers_does_what_the_evaluator_says,
)];

const BINARY: [Binary; 11] = [
    Binary::Add,
    Binary::Sub,
    Binary::Mul,
    Binary::MulHighU,
    Binary::MulHighS,
    Binary::And,
    Binary::Xor,
    Binary::Or,
    Binary::ShiftLeft,
    Binary::ShiftRightU,
    Binary::ShiftRightS,
];

const COMPARE: [Compare; 3] = [Compare::Equal, Compare::LessU, Compare::LessS];

/// What a function does, written so that a failing one can be shrunk: the
/// integers it starts with, then each step, which says its operation by
/// number and its operands as places in the list of integers so far, taken
/// modulo how many have the operand's size.
#[derive(Debug)]
struct Plan {
    starts: Vec<(Bits, u64)>,
    steps: Vec<(usize, usize, usize)>,
}

fn plan(g: &mut Gen) -> Plan {
    // More integers than any chip has registers, so that some are in the
    // frame, and few enough that a failure is read at a glance.
    let count = 1 + g.below(26);
    let steps = g.below(30);
    let starts = (0..count)
        .map(|_| {
            let bits = *g.pick(&Bits::ALL);
            // The numbers where a mistake of sign, of the size, or of the
            // bits above the size shows.
            let n = match g.below(6) {
                0 => g.below(3) as u64,
                1 => 1 << (bits.count() - 1),
                2 => u64::MAX,
                3 => (1 << (bits.count() - 1)) - 1,
                _ => (g.below(1 << 30) as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15),
            };
            let n = if bits == Bits::B64 {
                n
            } else {
                n & ((1 << bits.count()) - 1)
            };
            (bits, n)
        })
        .collect();
    let steps = (0..steps)
        .map(|_| (g.below(BINARY.len() + COMPARE.len()), g.below(1000), g.below(1000)))
        .collect();
    Plan { starts, steps }
}

/// The function the plan describes, which writes every integer it holds, each
/// as 8 bytes, widened as an unsigned number.
fn function(plan: &Plan) -> scalar::Program {
    let mut b = Builder::new();
    let mut held: Vec<(Value, Bits)> = plan
        .starts
        .iter()
        .map(|(bits, n)| (b.int(*bits, *n), *bits))
        .collect();
    for &(what, x, y) in &plan.steps {
        let (a, bits) = held[x % held.len()];
        let same: Vec<Value> = held.iter().filter(|h| h.1 == bits).map(|h| h.0).collect();
        if what < BINARY.len() {
            let op = BINARY[what];
            // The amount of a shift is an integer of any size.
            let by = if matches!(
                op,
                Binary::ShiftLeft | Binary::ShiftRightU | Binary::ShiftRightS
            ) {
                held[y % held.len()].0
            } else {
                same[y % same.len()]
            };
            held.push((b.binary(op, a, by), bits));
        } else {
            let other = same[y % same.len()];
            let said = b.compare(COMPARE[what - BINARY.len()], a, other);
            held.push((said, Bits::B8));
        }
    }
    let total = b.int(Bits::B64, 8 * held.len() as u64);
    let buffer = b.allocate(total);
    for (i, (v, bits)) in held.iter().enumerate() {
        let wide = if *bits == Bits::B64 {
            *v
        } else {
            b.widen(false, *v, Bits::B64)
        };
        let offset = b.int(Bits::B64, 8 * i as u64);
        let at = b.binary(Binary::Add, buffer, offset);
        b.store(at, wide);
    }
    let call = scalar::System::Write(scalar::Stream::Stdout);
    b.emit(
        Op::System {
            call,
            args: vec![buffer, total],
        },
        &[],
    );
    b.ret(vec![]);
    let program = scalar::Program {
        data: vec![],
        functions: vec![b.finish(&[])],
        main: 0,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

fn every_executable_of_a_function_that_holds_many_integers_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(660, 200, plan, |plan| {
        let program = function(plan);
        let said = scalar::run(&program);
        for &target in &targets {
            scalar_runs_as(&program, target, &said);
        }
    });
}
