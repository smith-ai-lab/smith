//! What the code the compiler writes for arm64 does, checked on a machine of
//! any kind, as the machine `aarch64` is. The tests that run an executable on
//! the machine that runs the tests cannot see the code of a target that the
//! machine cannot run, and the sweep runs on one kind of machine.
//!
//! No test names a target: an executable is one for arm64 when its file says
//! so.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Op, Type, Value};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "an_arm64_executable_does_what_the_evaluator_says_of_every_operation_on_every_width",
        an_arm64_executable_does_what_the_evaluator_says_of_every_operation_on_every_width,
    ),
    (
        "an_operation_that_leaves_no_bits_above_costs_one_instruction_at_every_width",
        an_operation_that_leaves_no_bits_above_costs_one_instruction_at_every_width,
    ),
    (
        "no_instruction_of_a_function_with_a_large_frame_changes_nothing",
        no_instruction_of_a_function_with_a_large_frame_changes_nothing,
    ),
];

/// The executables for arm64 of the program, one for each target the
/// compiler builds for arm64 that the machine in `aarch64` runs.
fn executables(program: &scalar::Program) -> Vec<Vec<u8>> {
    Target::ALL
        .iter()
        .map(|t| t.emit(program))
        .filter(|bytes| aarch64::is_executable(bytes))
        .collect()
}

fn program(function: scalar::Function) -> scalar::Program {
    let program = scalar::Program {
        data: Vec::new(),
        functions: vec![function],
        main: 0,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

/// Numbers that are at the edges of every width, and then any.
const EDGES: [u64; 12] = [
    0,
    1,
    2,
    0x7f,
    0x80,
    0xff,
    0x7fff,
    0x8000,
    0xffff,
    0x7fff_ffff,
    0x8000_0000,
    u64::MAX,
];

/// A number of `bits`, whose top bit is set about as often as not.
fn number(g: &mut Gen, bits: Bits) -> u64 {
    let n = if g.one_in(2) {
        *g.pick(&EDGES)
    } else {
        (g.below(1 << 30) as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15) ^ g.below(1 << 30) as u64
    };
    match bits {
        Bits::B64 => n,
        _ => n & ((1 << bits.count()) - 1),
    }
}

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

/// A program in the scalar form that holds more integers than there are
/// registers, of every width, whose top bits are set about as often as not,
/// and applies operations to pairs of them that are of one width. It writes
/// every result, widened to 8 bytes, to the standard output.
fn operating(g: &mut Gen) -> scalar::Program {
    // More values than registers, so that some are in the frame.
    let count = 30 + g.below(20);
    let mut b = Builder::new();
    let held: Vec<(Bits, Value)> = (0..count)
        .map(|_| {
            let bits = *g.pick(&Bits::ALL);
            let n = number(g, bits);
            (bits, b.int(bits, n))
        })
        .collect();
    let mut results = Vec::new();
    for _ in 0..1 + g.below(30) {
        let (bits, a) = *g.pick(&held);
        let same: Vec<Value> = held.iter().filter(|(w, _)| *w == bits).map(|(_, v)| *v).collect();
        let c = *g.pick(&same);
        let (result, width) = if g.one_in(3) {
            (b.compare(*g.pick(&COMPARE), a, c), Bits::B8)
        } else {
            (b.binary(*g.pick(&BINARY), a, c), bits)
        };
        results.push(if width == Bits::B64 {
            result
        } else {
            b.widen(false, result, Bits::B64)
        });
    }
    let total = b.int(Bits::B64, 8 * results.len() as u64);
    let buffer = b.allocate(total);
    for (i, result) in results.iter().enumerate() {
        let offset = b.int(Bits::B64, 8 * i as u64);
        let at = b.binary(Binary::Add, buffer, offset);
        b.store(at, *result);
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
    program(b.finish(&[]))
}

fn an_arm64_executable_does_what_the_evaluator_says_of_every_operation_on_every_width() {
    generate::check(90, 60, operating, |program| {
        let said = scalar::run(program);
        let executables = executables(program);
        assert!(!executables.is_empty(), "no target is built for arm64");
        for bytes in executables {
            let ran = aarch64::run(&bytes);
            assert_eq!((ran.stdout, ran.status), (said.stdout.clone(), said.status));
        }
    });
}

/// A program in the scalar form that gives two integers of `bits`, takes `op`
/// of the first and the second `times` over, and holds the last in memory.
fn repeating(op: Binary, bits: Bits, times: usize) -> scalar::Program {
    let mut b = Builder::new();
    let first = b.int(bits, 1);
    let second = b.int(bits, 1);
    let mut last = first;
    for _ in 0..times {
        last = b.binary(op, last, second);
    }
    let eight = b.int(Bits::B64, 8);
    let place = b.allocate(eight);
    b.store(place, last);
    b.ret(vec![]);
    program(b.finish(&[]))
}

/// What `and`, `or`, `xor`, and a shift right of an unsigned number give of
/// two integers that have no bits above their width, has none either, so the
/// result is in its register with one instruction at any width. An
/// instruction to clear the bits above is one more than it takes, and the
/// executable of a program that repeats the operation is longer by it each
/// time.
fn an_operation_that_leaves_no_bits_above_costs_one_instruction_at_every_width() {
    const WORD: usize = 4;
    let operations = [
        Binary::And,
        Binary::Xor,
        Binary::Or,
        Binary::ShiftRightU,
    ];
    let generate = |g: &mut Gen| (*g.pick(&operations), *g.pick(&Bits::ALL), 1 + g.below(6));
    generate::check(91, 24, generate, |(op, bits, times)| {
        let without = executables(&repeating(*op, *bits, 0));
        let with = executables(&repeating(*op, *bits, *times));
        assert!(!with.is_empty(), "no target is built for arm64");
        for (with, without) in with.iter().zip(&without) {
            assert_eq!(with.len() - without.len(), WORD * times);
        }
    });
}

/// A program in the scalar form that holds `count` integers of 8 bits at
/// once, and gives them to a block in another order, so that those that are
/// in the frame are copied from one place of it to another.
fn holding(count: usize) -> scalar::Program {
    let int = Type::Int(Bits::B8);
    let mut b = Builder::new();
    let values: Vec<Value> = (0..count).map(|i| b.int(Bits::B8, i as u64)).collect();
    let (next, params) = b.block(&vec![int; count]);
    assert_eq!(params.len(), count);
    let mut turned = values.clone();
    turned.rotate_left(1);
    b.jump(next, turned);
    b.at(next);
    b.ret(vec![]);
    program(b.finish(&[]))
}

/// The words of the code of an executable.
fn words(executable: &[u8]) -> Vec<u32> {
    // The code is after the headers of the file, which are 64 bytes of the
    // file and 56 of the segment.
    executable[120..]
        .chunks_exact(4)
        .map(|w| u32::from_le_bytes(w.try_into().unwrap()))
        .collect()
}

/// `add` or `sub` of an immediate, as 64 bits, and the register, the other,
/// the number, and whether it is shifted by 12 bits.
fn immediate(word: u32) -> Option<(u32, u32, u32, bool)> {
    let is = word & 0x9f00_0000 == 0x9100_0000 && word >> 29 & 1 == 0;
    is.then_some((word & 31, word >> 5 & 31, word >> 10 & 0xfff, word >> 22 & 1 == 1))
}

/// Whether the instruction leaves everything as it was: `add` or `sub` of 0
/// to a register from itself, or a move of a register to itself.
fn changes_nothing(word: u32) -> bool {
    match immediate(word) {
        Some((to, from, number, _)) => to == from && number == 0,
        None => word & 0xffe0_ffe0 == 0xaa00_03e0 && word >> 16 & 31 == word & 31,
    }
}

/// A function with a frame of more than a page reaches a place of it that
/// is far from the stack pointer by forming its address first. A place that
/// is a whole number of pages away takes one instruction to form, and the
/// instruction for the rest of the number would add 0.
fn no_instruction_of_a_function_with_a_large_frame_changes_nothing() {
    // Each integer of 8 bits that has no register has a byte of the frame,
    // so the place a page from the stack pointer is the 4081st after the
    // registers. A few more are held, so that it is among them wherever the
    // frame starts. The allocator takes time that grows faster than the
    // number of values, so one function is all this builds.
    let executables = executables(&holding(25 + 4081 + 16));
    assert!(!executables.is_empty(), "no target is built for arm64");
    for bytes in executables {
        let code = words(&bytes);
        let reaches = code
            .iter()
            .filter_map(|w| immediate(*w))
            .any(|(to, from, _, shifted)| shifted && to != from);
        assert!(reaches, "no place of the frame is reached through its address");
        for word in code {
            assert!(!changes_nothing(word), "the instruction {word:#010x} changes nothing");
        }
    }
}
