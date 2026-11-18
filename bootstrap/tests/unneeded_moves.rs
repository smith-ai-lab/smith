//! A value is moved only when it must be moved. The tests build functions in
//! the scalar form, so that the shape of what is alive and where it goes is
//! known, and check what the executables of every chip do and how many moves
//! their code holds: a loop that hands its parameters round unchanged moves
//! none, and wherever a move is needed, it does what the evaluator says.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Op, Type};
use smith::target::Target;

use crate::Test;
use crate::aarch64;
use crate::generate::{self, Gen};
use crate::machine_code::{self, Kind, Machine};
use crate::targets::{TYPES, runnable, scalar_runs_as, starting, write_all};

pub const ONCE: &[Test] = &[
    (
        "a_loop_that_hands_its_parameters_round_unchanged_moves_nothing_for_them",
        a_loop_that_hands_its_parameters_round_unchanged_moves_nothing_for_them,
    ),
    (
        "a_jump_that_swaps_two_values_moves_nothing_when_no_other_value_needs_their_registers",
        a_jump_that_swaps_two_values_moves_nothing_when_no_other_value_needs_their_registers,
    ),
    (
        "an_arm64_executable_that_moves_values_at_jumps_does_what_the_evaluator_says",
        an_arm64_executable_that_moves_values_at_jumps_does_what_the_evaluator_says,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_that_moves_values_at_jumps_does_what_the_evaluator_says",
    every_executable_that_moves_values_at_jumps_does_what_the_evaluator_says,
)];

/// A program that holds values of the types and goes round a loop, in which
/// the number of rounds is kept in memory, so that nothing but the values
/// themselves goes round. Each round hands every value on as it is.
fn handing_round(types: &[Type], starts: &[u64]) -> scalar::Program {
    let int = |b: &mut Builder, n: u64| b.int(Bits::B64, n);
    let mut b = Builder::new();
    let eight = int(&mut b, 8);
    let cell = b.allocate(eight);
    let rounds = int(&mut b, 3);
    b.store(cell, rounds);
    let first = starting(&mut b, types, starts);
    let (head, held) = b.block(types);
    let (body, _) = b.block(&[]);
    let (done, _) = b.block(&[]);
    b.jump(head, first);
    b.at(head);
    let left = b.load(Type::Int(Bits::B64), cell);
    let zero = int(&mut b, 0);
    let stop = b.compare(Compare::Equal, left, zero);
    b.switch(stop, vec![(body, vec![]), (done, vec![])]);
    b.at(body);
    let one = int(&mut b, 1);
    let fewer = b.binary(Binary::Sub, left, one);
    b.store(cell, fewer);
    b.jump(head, held.clone());
    b.at(done);
    write_all(&mut b, types, &held);
    b.ret(vec![]);
    program(vec![b.finish(&[])], 0)
}

fn program(functions: Vec<scalar::Function>, main: usize) -> scalar::Program {
    let program = scalar::Program {
        data: vec![b"swap".to_vec(), b"x".to_vec()],
        functions,
        main,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

/// How many instructions that copy a register to another there are in the
/// first loop of the executable, where a loop is the code from a jump that
/// goes back to the place it goes to. The loops of the programs here come
/// before the code that writes what they hold. `None` when the file is not
/// one the reader of machine code knows.
fn moves_in_the_first_loop(file: &[u8]) -> Option<usize> {
    let (machine, found) = machine_code::instructions(file)?;
    let (from, to) = found.iter().find_map(|i| match i.kind {
        Kind::Jump(to) | Kind::Branch(to) if to < i.at => Some((to, i.at)),
        _ => None,
    })?;
    let moves = found.iter().filter(|i| {
        from <= i.at && i.at <= to && is_a_move(machine, &file[i.at..i.at + i.len])
    });
    Some(moves.count())
}

/// How many instructions that copy a register to another there are in the
/// executable, or `None` when the file is not one the reader knows.
fn moves_in_the_code(file: &[u8]) -> Option<usize> {
    let (machine, found) = machine_code::instructions(file)?;
    let moves = found
        .iter()
        .filter(|i| is_a_move(machine, &file[i.at..i.at + i.len]));
    Some(moves.count())
}

/// A program that writes two numbers, which a jump gives to the block that
/// writes them, in the order they were made, or the other way round when
/// `swapped`. Nothing is called between the jump and the writes that either
/// value is alive across, so each of the two may be in any register.
fn handing_two_on(swapped: bool) -> scalar::Program {
    let int = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let sixteen = b.int(Bits::B64, 16);
    let buffer = b.allocate(sixteen);
    let (x, y) = (b.int(Bits::B64, 1), b.int(Bits::B64, 2));
    let (next, held) = b.block(&[int, int]);
    b.jump(next, if swapped { vec![y, x] } else { vec![x, y] });
    b.at(next);
    b.store(buffer, held[0]);
    let eight = b.int(Bits::B64, 8);
    let second = b.binary(Binary::Add, buffer, eight);
    b.store(second, held[1]);
    let call = scalar::System::Write(scalar::Stream::Stdout);
    b.emit(Op::System { call, args: vec![buffer, sixteen] }, &[]);
    b.ret(vec![]);
    program(vec![b.finish(&[])], 0)
}

/// Whether the instruction copies a register to another.
fn is_a_move(machine: Machine, code: &[u8]) -> bool {
    match machine {
        // `mov r64, r64` in either direction, which has no memory operand.
        Machine::X86_64 => {
            let at = usize::from((0x40..0x50).contains(&code[0]));
            matches!(code[at], 0x89 | 0x8b) && code[at + 1] >> 6 == 3
        }
        // `orr xd, xzr, xm`.
        Machine::Arm64 => {
            let word = u32::from_le_bytes(code[..4].try_into().unwrap());
            word & 0xffe0_ffe0 == 0xaa00_03e0
        }
    }
}

fn a_loop_that_hands_its_parameters_round_unchanged_moves_nothing_for_them() {
    let generate = |g: &mut Gen| {
        let count = 1 + g.below(20);
        let types: Vec<Type> = (0..count).map(|_| *g.pick(&TYPES)).collect();
        let starts: Vec<u64> = (0..count)
            .map(|_| (g.below(1 << 20) as u64 + 1).wrapping_mul(0x9e37_79b9_7f4a_7c15))
            .collect();
        (types, starts)
    };
    generate::check(100, 12, generate, |(types, starts)| {
        let without = handing_round(&[], &[]);
        let with = handing_round(types, starts);
        let mut read = 0;
        for target in Target::ALL {
            let (a, b) = (target.emit(&without), target.emit(&with));
            if let (Some(a), Some(b)) = (moves_in_the_first_loop(&a), moves_in_the_first_loop(&b)) {
                read += 1;
                assert_eq!(b, a, "{} values handed round, {}", types.len(), target.name());
            }
        }
        assert!(read > 0, "no target is an ELF file the reader knows");
    });
}

/// Two values that a jump gives to a block in the other order than they are
/// held in are not moved, as the block's parameters take the registers they
/// are in: a swap costs what no swap costs.
fn a_jump_that_swaps_two_values_moves_nothing_when_no_other_value_needs_their_registers() {
    let (plain, swapped) = (handing_two_on(false), handing_two_on(true));
    let mut read = 0;
    for target in Target::ALL {
        let (a, b) = (target.emit(&plain), target.emit(&swapped));
        if let (Some(a), Some(b)) = (moves_in_the_code(&a), moves_in_the_code(&b)) {
            read += 1;
            assert_eq!(b, a, "{}", target.name());
        }
    }
    assert!(read > 0, "no target is an ELF file the reader knows");
}

/// A function of `count` values that go round a loop, in which each value is
/// given the one at the place `sources` says in the round before, all at
/// once, or a constant when it says none, and in which a call is made, with
/// every value alive across it, when `calling`. The values are written at the
/// end.
fn going_round(
    types: &[Type],
    starts: &[u64],
    sources: &[Option<usize>],
    rounds: u64,
    calling: bool,
) -> scalar::Program {
    let int = |b: &mut Builder, n: u64| b.int(Bits::B64, n);
    // The function the loop calls: one more than its argument.
    let mut step = Builder::new();
    let own = step.params(&[Type::Int(Bits::B64)]);
    let one = int(&mut step, 1);
    let next = step.binary(Binary::Add, own[0], one);
    step.ret(vec![next]);
    let step = step.finish(&[Type::Int(Bits::B64)]);

    let mut b = Builder::new();
    let mut first = vec![int(&mut b, 0)];
    first.extend(starting(&mut b, types, starts));
    let mut held = vec![Type::Int(Bits::B64)];
    held.extend(types);
    let (head, in_head) = b.block(&held);
    let (again, _) = b.block(&[]);
    let (done, in_done) = b.block(types);
    b.jump(head, first);
    b.at(head);
    let limit = int(&mut b, rounds);
    let finished = b.compare(Compare::Equal, in_head[0], limit);
    b.switch(
        finished,
        vec![(again, vec![]), (done, in_head[1..].to_vec())],
    );
    b.at(again);
    let next = if calling {
        b.call(0, vec![in_head[0]], &[Type::Int(Bits::B64)])[0]
    } else {
        let one = int(&mut b, 1);
        b.binary(Binary::Add, in_head[0], one)
    };
    let mut carried = vec![next];
    for (i, source) in sources.iter().enumerate() {
        carried.push(match source {
            Some(s) => in_head[1 + s],
            // A constant on this jump, where the jump into the loop gave a
            // constant too, and the jump that leaves it gives the value.
            None => starting(&mut b, &types[i..=i], &[starts[i] ^ 0x55])[0],
        });
    }
    b.jump(head, carried);
    b.at(done);
    write_all(&mut b, types, &in_done);
    b.ret(vec![]);
    program(vec![step, b.finish(&[])], 1)
}

/// What a generated loop is made of: the types of what goes round, where
/// each value comes from, how many rounds, and whether a call is made.
type Shape = (Vec<Type>, Vec<u64>, Vec<Option<usize>>, u64, bool);

fn shape(g: &mut Gen) -> Shape {
    // Often more values than there are registers, and sometimes fewer.
    let count = if g.one_in(2) {
        1 + g.below(5)
    } else {
        10 + g.below(18)
    };
    let types: Vec<Type> = (0..count).map(|_| *g.pick(&TYPES)).collect();
    let starts: Vec<u64> = (0..count)
        .map(|_| (g.below(1 << 20) as u64 + 1).wrapping_mul(0x9e37_79b9_7f4a_7c15))
        .collect();
    let sources: Vec<Option<usize>> = (0..count)
        .map(|i| {
            if g.one_in(6) {
                return None;
            }
            let same: Vec<usize> = (0..count).filter(|j| types[*j] == types[i]).collect();
            // Most are kept as they are, and some are handed to another.
            Some(if g.one_in(3) { *g.pick(&same) } else { i })
        })
        .collect();
    (types, starts, sources, g.below(4) as u64, g.one_in(2))
}

/// A swap of two values, three that go in a ring, and more than that, each
/// with a call and without.
fn shapes_that_must_work() -> Vec<Shape> {
    let int = Type::Int(Bits::B64);
    let mut shapes = Vec::new();
    for calling in [false, true] {
        shapes.push((vec![int, int], vec![1, 2], vec![Some(1), Some(0)], 3, calling));
        shapes.push((
            vec![int, int, int],
            vec![1, 2, 3],
            vec![Some(1), Some(2), Some(0)],
            4,
            calling,
        ));
        // One is a constant on the jump that goes back, and a value on the
        // jump that goes in.
        shapes.push((
            vec![int, int, int],
            vec![1, 2, 3],
            vec![None, Some(0), Some(2)],
            3,
            calling,
        ));
        // More alive across the loop and the call than any chip has
        // registers, handed round in a ring.
        let count = 30;
        shapes.push((
            vec![int; count],
            (1..=count as u64).collect(),
            (0..count).map(|i| Some((i + 1) % count)).collect(),
            3,
            calling,
        ));
    }
    shapes
}

fn an_arm64_executable_that_moves_values_at_jumps_does_what_the_evaluator_says() {
    let check = |(types, starts, sources, rounds, calling): &Shape| {
        let program = going_round(types, starts, sources, *rounds, *calling);
        let said = scalar::run(&program);
        let mut ran = 0;
        for target in Target::ALL {
            let bytes = target.emit(&program);
            if aarch64::is_executable(&bytes) {
                let result = aarch64::run(&bytes);
                assert_eq!((result.stdout, result.status), (said.stdout.clone(), said.status));
                ran += 1;
            }
        }
        assert!(ran > 0, "no target is built for arm64");
    };
    shapes_that_must_work().iter().for_each(check);
    generate::check(101, 12, shape, check);
}

fn every_executable_that_moves_values_at_jumps_does_what_the_evaluator_says() {
    let targets = runnable();
    let check = |(types, starts, sources, rounds, calling): &Shape| {
        let program = going_round(types, starts, sources, *rounds, *calling);
        let said = scalar::run(&program);
        for &target in &targets {
            scalar_runs_as(&program, target, &said);
        }
    };
    shapes_that_must_work().iter().for_each(check);
    generate::check(102, 20, shape, check);
}
