//! A value is moved only when it must be moved. The tests build functions in
//! the scalar form, so that the shape of what is alive and where it goes is
//! known, and check what the executables of every chip do and how many moves
//! their code holds: a loop that hands its parameters round unchanged moves
//! none, and wherever a move is needed, it does what the evaluator says.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Op, Type, Value};
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
        "an_instruction_that_can_hold_a_constant_does_and_the_constant_costs_no_instruction",
        an_instruction_that_can_hold_a_constant_does_and_the_constant_costs_no_instruction,
    ),
    (
        "a_function_saves_a_register_a_call_does_not_change_only_for_a_value_alive_across_a_call",
        a_function_saves_a_register_a_call_does_not_change_only_for_a_value_alive_across_a_call,
    ),
    (
        "a_value_read_from_the_frame_is_not_read_again_in_a_block_while_it_is_in_a_register",
        a_value_read_from_the_frame_is_not_read_again_in_a_block_while_it_is_in_a_register,
    ),
    (
        "an_arm64_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says",
        an_arm64_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says,
    ),
    (
        "an_arm64_executable_that_moves_values_at_jumps_does_what_the_evaluator_says",
        an_arm64_executable_that_moves_values_at_jumps_does_what_the_evaluator_says,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says",
        every_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says,
    ),
    (
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

/// What an instruction holds in itself in place of a register.
#[derive(Clone, Copy, Debug)]
enum Holds {
    /// The number added to a value.
    Added,
    /// The number written to memory.
    Stored,
}

/// A program that does `holds` with each of `constants`, each a number of its
/// own, to a value it reads from memory, or at a place of its own in memory.
/// The numbers are small, so that an instruction of any chip may hold them.
fn holding(holds: Holds, constants: &[u64], bits: Bits) -> scalar::Program {
    let mut b = Builder::new();
    let size = b.int(Bits::B64, 8 * 64);
    let cell = b.allocate(size);
    let mut last = b.load(Type::Int(bits), cell);
    for (i, c) in constants.iter().enumerate() {
        let n = b.int(bits, *c);
        match holds {
            Holds::Added => last = b.binary(Binary::Add, last, n),
            Holds::Stored => {
                let at = b.int(Bits::B64, 8 * i as u64);
                let place = b.binary(Binary::Add, cell, at);
                b.store(place, n);
            }
        }
    }
    b.store(cell, last);
    b.ret(vec![]);
    program(vec![b.finish(&[])], 0)
}

/// How many instructions the executable of an ELF file holds, as the reader
/// knows them, and the machine they are for.
fn instruction_count(file: &[u8]) -> Option<(Machine, usize)> {
    let (machine, found) = machine_code::instructions(file)?;
    Some((machine, found.len()))
}

/// A constant that an instruction can hold is held by it, so no instruction
/// makes it and no register is given to it: adding one, and writing one, cost
/// the instruction that does it and no more. With one instruction for the
/// address, which holds the number it is moved by, writing takes two on
/// x86-64, which holds the number written in the store, and three on arm64,
/// which makes the number and then stores it.
fn an_instruction_that_can_hold_a_constant_does_and_the_constant_costs_no_instruction() {
    let generate = |g: &mut Gen| {
        let holds = *g.pick(&[Holds::Added, Holds::Stored]);
        // A sum of fewer than 64 bits is cleared above its width, which is an
        // instruction of its own.
        let bits = match holds {
            Holds::Added => Bits::B64,
            Holds::Stored => *g.pick(&[Bits::B32, Bits::B64]),
        };
        let constants: Vec<u64> = (0..1 + g.below(30)).map(|_| 1 + g.below(1000) as u64).collect();
        (holds, bits, constants)
    };
    generate::check(103, 16, generate, |(holds, bits, constants)| {
        let mut read = 0;
        for target in Target::ALL {
            let (none, some) = (
                instruction_count(&target.emit(&holding(*holds, &[], *bits))),
                instruction_count(&target.emit(&holding(*holds, constants, *bits))),
            );
            let (Some((machine, none)), Some((_, some))) = (none, some) else {
                continue;
            };
            read += 1;
            let each = match (holds, machine) {
                (Holds::Added, _) => 1,
                (Holds::Stored, Machine::X86_64) => 2,
                (Holds::Stored, Machine::Arm64) => 3,
            };
            assert!(
                some - none <= each * constants.len(),
                "{holds:?} {} of {bits:?} on {}: {} instructions",
                constants.len(),
                target.name(),
                some - none
            );
        }
        assert!(read > 0, "no target is an ELF file the reader knows");
    });
}

/// Numbers at the edges of every width, which an instruction holds or not as
/// the chip says, and any.
const EDGES: [u64; 14] = [
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
    0xffff_ffff,
    0x7fff_ffff_ffff_ffff,
    u64::MAX,
];

fn number(g: &mut Gen, bits: Bits) -> u64 {
    let n = if g.one_in(3) {
        (g.below(1 << 30) as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15)
    } else {
        *g.pick(&EDGES)
    };
    match bits {
        Bits::B64 => n,
        _ => n & ((1 << bits.count()) - 1),
    }
}

/// A program that uses constants where an instruction holds them: stores
/// them at every width, chooses between them and between one and a value it
/// reads, gives them to a function that gives them back in the other order,
/// and adds them. It writes what it made, each widened to 8 bytes.
fn using_numbers(g: &mut Gen) -> scalar::Program {
    let int = Type::Int(Bits::B64);
    let mut leaf = Builder::new();
    let own = leaf.params(&[int, int]);
    leaf.ret(vec![own[1], own[0]]);
    let leaf = leaf.finish(&[int, int]);

    let mut b = Builder::new();
    let size = b.int(Bits::B64, 16);
    let cell = b.allocate(size);
    let seed = number(g, Bits::B64);
    let seed = b.int(Bits::B64, seed);
    b.store(cell, seed);
    let read = b.load(int, cell);
    let limit = number(g, Bits::B64);
    let limit = b.int(Bits::B64, limit);
    let on = b.compare(if g.one_in(2) { Compare::LessU } else { Compare::LessS }, read, limit);
    // Often more values alive until the end than there are registers, so
    // that the address the numbers are stored to is in the frame, and no
    // register is free to hold a number that an instruction does not.
    let mut made: Vec<Value> = Vec::new();
    for k in 0..if g.one_in(2) { 14 } else { 0 } {
        let k = b.int(Bits::B64, k);
        made.push(b.binary(Binary::Add, read, k));
    }
    for _ in 0..1 + g.below(12) {
        let case = g.below(5);
        // What a call gives is of 64 bits.
        let bits = if case == 4 { Bits::B64 } else { *g.pick(&Bits::ALL) };
        let ty = Type::Int(bits);
        let (n, m) = (number(g, bits), number(g, bits));
        let value = match case {
            0 => {
                let (n, m) = (b.int(bits, n), b.int(bits, m));
                b.choose(on, n, m)
            }
            1 => {
                let n = b.int(bits, n);
                let own = if bits == Bits::B64 { read } else { b.narrow(read, bits) };
                if g.one_in(2) { b.choose(on, n, own) } else { b.choose(on, own, n) }
            }
            2 => {
                let n = b.int(bits, n);
                b.store(cell, n);
                b.load(ty, cell)
            }
            3 => {
                let n = b.int(bits, n);
                let own = if bits == Bits::B64 { read } else { b.narrow(read, bits) };
                b.binary(if g.one_in(2) { Binary::Add } else { Binary::Sub }, own, n)
            }
            _ => {
                let (n, m) = (number(g, Bits::B64), number(g, Bits::B64));
                let (n, m) = (b.int(Bits::B64, n), b.int(Bits::B64, m));
                let given = b.call(0, vec![n, m], &[int, int]);
                made.push(given[1]);
                given[0]
            }
        };
        made.push(if bits == Bits::B64 {
            value
        } else {
            b.widen(false, value, Bits::B64)
        });
    }
    let types = vec![int; made.len()];
    write_all(&mut b, &types, &made);
    // The last use of the address, so that it is the one that goes to the frame.
    b.store(cell, read);
    b.ret(vec![]);
    program(vec![leaf, b.finish(&[])], 1)
}

fn an_arm64_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says() {
    generate::check(104, 40, using_numbers, |program| {
        let said = scalar::run(program);
        let mut ran = 0;
        for target in Target::ALL {
            let bytes = target.emit(program);
            if aarch64::is_executable(&bytes) {
                let result = aarch64::run(&bytes);
                assert_eq!((result.stdout, result.status), (said.stdout.clone(), said.status));
                ran += 1;
            }
        }
        assert!(ran > 0, "no target is built for arm64");
    });
}

fn every_executable_that_holds_numbers_in_instructions_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(105, 40, using_numbers, |program| {
        let said = scalar::run(program);
        for &target in &targets {
            scalar_runs_as(program, target, &said);
        }
    });
}

/// A program of three functions. `leaf` reads `held` numbers through its
/// parameter and gives their sum, so that all are alive together, and calls
/// nothing. `middle` reads `across` numbers, calls `leaf`, and adds them to
/// what it gave, so that all are alive across the call. `main` calls `middle`.
fn saving(held: usize, across: usize) -> scalar::Program {
    let int = Type::Int(Bits::B64);
    let read = |b: &mut Builder, from: Value, count: usize| -> Vec<Value> {
        (0..count)
            .map(|i| {
                let at = b.int(Bits::B64, 8 * i as u64);
                let place = b.binary(Binary::Add, from, at);
                b.load(int, place)
            })
            .collect()
    };
    let sum = |b: &mut Builder, values: &[Value], first: Value| {
        values.iter().fold(first, |s, v| b.binary(Binary::Add, s, *v))
    };
    let mut leaf = Builder::new();
    let from = leaf.params(&[Type::Address])[0];
    let values = read(&mut leaf, from, held);
    let zero = leaf.int(Bits::B64, 0);
    let total = sum(&mut leaf, &values, zero);
    leaf.ret(vec![total]);
    let leaf = leaf.finish(&[int]);

    let mut middle = Builder::new();
    let from = middle.params(&[Type::Address])[0];
    let values = read(&mut middle, from, across);
    let given = middle.call(0, vec![from], &[int])[0];
    let total = sum(&mut middle, &values, given);
    middle.ret(vec![total]);
    let middle = middle.finish(&[int]);

    let mut main = Builder::new();
    let size = main.int(Bits::B64, 8 * 64);
    let buffer = main.allocate(size);
    main.call(1, vec![buffer], &[int]);
    main.ret(vec![]);
    program(vec![leaf, middle, main.finish(&[])], 2)
}

/// How many instructions of the executable write a register that a call does
/// not change to the frame, which is how a function saves it.
fn saves(file: &[u8]) -> Option<usize> {
    let (machine, found) = machine_code::instructions(file)?;
    let saving = found.iter().filter(|i| {
        let code = &file[i.at..i.at + i.len];
        match machine {
            // `mov [rsp + disp], r` of rbx, rbp, or r12 to r15.
            Machine::X86_64 => {
                let rex = if (0x40..0x50).contains(&code[0]) { code[0] } else { 0 };
                let at = usize::from(rex != 0);
                if code.len() < at + 3 {
                    return false;
                }
                let reg = ((rex >> 2) & 1) << 3 | (code[at + 1] >> 3) & 7;
                code[at] == 0x89
                    && code[at + 1] >> 6 != 3
                    && code[at + 1] & 7 == 4
                    && code[at + 2] == 0x24
                    && matches!(reg, 3 | 5 | 12..=15)
            }
            // `str xt, [sp, #imm]` of x19 to x28.
            Machine::Arm64 => {
                let word = u32::from_le_bytes(code[..4].try_into().unwrap());
                word & 0xffc0_03e0 == 0xf900_03e0 && matches!(word & 31, 19..=28)
            }
        }
    });
    Some(saving.count())
}

/// A function that has no value alive across a call saves no register, however
/// many values are alive together in it, and one that has some saves
/// registers for them and for no more.
fn a_function_saves_a_register_a_call_does_not_change_only_for_a_value_alive_across_a_call() {
    let generate = |g: &mut Gen| (1 + g.below(30), g.below(5));
    generate::check(106, 20, generate, |(held, across)| {
        let mut read = 0;
        for target in Target::ALL {
            let Some(saved) = saves(&target.emit(&saving(*held, *across))) else {
                continue;
            };
            read += 1;
            if *across == 0 {
                assert_eq!(saved, 0, "{held} alive together, on {}", target.name());
            } else {
                assert!(
                    (1..=*across).contains(&saved),
                    "{across} alive across a call and {saved} registers saved, on {}",
                    target.name()
                );
            }
        }
        assert!(read > 0, "no target is an ELF file the reader knows");
    });
}

/// A function that reads `held` numbers from memory, so that more are alive
/// than there are registers and those whose last use is furthest off are in
/// the frame, writes the last two of them `times` over to the places the
/// first of them says, and then adds them all up and writes the sum.
fn rereading(held: usize, times: usize) -> scalar::Program {
    let int = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let size = b.int(Bits::B64, 8 * 64);
    let buffer = b.allocate(size);
    let values: Vec<Value> = (0..held)
        .map(|i| {
            let at = b.int(Bits::B64, 8 * i as u64);
            let place = b.binary(Binary::Add, buffer, at);
            // The place the stores go to is an address, which is a number
            // read from memory as well.
            let ty = if i == held - 2 { Type::Address } else { int };
            b.load(ty, place)
        })
        .collect();
    let (place, value) = (values[held - 2], values[held - 1]);
    for _ in 0..times {
        b.store(place, value);
    }
    let zero = b.int(Bits::B64, 0);
    let ints = values.iter().filter(|v| **v != place);
    let total = ints.fold(zero, |s, v| b.binary(Binary::Add, s, *v));
    b.store(place, total);
    b.store(buffer, total);
    b.ret(vec![]);
    program(vec![b.finish(&[])], 0)
}

/// A value that was read from the frame is in the register it was read into
/// until something changes the register, so reading it again in the block
/// takes no instruction. A store of two numbers that are in the frame, which
/// reads each into a register, takes one instruction for each store after the
/// first. Counted in the code of x86-64.
fn a_value_read_from_the_frame_is_not_read_again_in_a_block_while_it_is_in_a_register() {
    let generate = |g: &mut Gen| (24 + g.below(8), 2 + g.below(10));
    generate::check(107, 12, generate, |(held, times)| {
        let mut read = 0;
        for target in Target::ALL {
            let once = target.emit(&rereading(*held, 1));
            let many = target.emit(&rereading(*held, *times));
            let (Some((Machine::X86_64, once)), Some((_, many))) =
                (instruction_count(&once), instruction_count(&many))
            else {
                continue;
            };
            read += 1;
            assert!(
                many - once < *times,
                "{} stores of two values in the frame take {} instructions more than one, on {}",
                times,
                many - once,
                target.name()
            );
        }
        assert!(read > 0, "no target is an ELF file for x86-64");
    });
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
