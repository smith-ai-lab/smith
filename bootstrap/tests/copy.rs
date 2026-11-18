//! How the runtime copies the bytes of a list: whatever the length and
//! wherever the two places lie, the bytes it leaves are the bytes it was
//! given, on the evaluator and in every executable the machine can run, and
//! its loop moves 8 bytes in a few instructions.

use std::collections::HashSet;

use smith::scalar::runtime::{self, Runtime};
use smith::scalar::{self, Bits, Builder, Compare, Op, Program, Type, Value, Binary};

use crate::Test;
use crate::scalar::with_runtime;
use crate::targets::{runnable, scalar_runs_as};

pub const ONCE: &[Test] = &[
    (
        "the_copy_leaves_the_bytes_it_was_given_on_the_evaluator",
        the_copy_leaves_the_bytes_it_was_given_on_the_evaluator,
    ),
    (
        "the_loop_of_the_copy_moves_8_bytes_in_5_instructions_or_fewer",
        the_loop_of_the_copy_moves_8_bytes_in_5_instructions_or_fewer,
    ),
];

pub const MACHINE: &[Test] = &[(
    "the_copy_leaves_the_bytes_it_was_given_in_every_executable",
    the_copy_leaves_the_bytes_it_was_given_in_every_executable,
)];

/// The most bytes any copy here moves.
const MOST: u64 = 4096;
/// The byte that the memory a copy must not touch holds.
const UNTOUCHED: u64 = 0xaa;

const LENGTH: Type = Type::Int(Bits::B64);
const BYTE: Type = Type::Int(Bits::B8);

/// A loop over the places `0..count`, which runs `body` for each with the
/// place and the lengths it carries, and gives the lengths after the last.
fn counted(
    b: &mut Builder,
    count: Value,
    carried: Vec<Value>,
    body: impl FnOnce(&mut Builder, Value, Vec<Value>) -> Vec<Value>,
) -> Vec<Value> {
    let zero = b.int(Bits::B64, 0);
    let types = vec![LENGTH; carried.len() + 1];
    let (head, params) = b.block(&types);
    let (inside, _) = b.block(&[]);
    let (done, results) = b.block(&types[1..]);
    let mut start = vec![zero];
    start.extend(carried);
    b.jump(head, start);
    b.at(head);
    let finished = b.compare(Compare::Equal, params[0], count);
    b.switch(
        finished,
        vec![(inside, vec![]), (done, params[1..].to_vec())],
    );
    b.at(inside);
    let next = body(b, params[0], params[1..].to_vec());
    let one = b.int(Bits::B64, 1);
    let place = b.binary(Binary::Add, params[0], one);
    let mut args = vec![place];
    args.extend(next);
    b.jump(head, args);
    b.at(done);
    results
}

fn at(b: &mut Builder, address: Value, by: Value) -> Value {
    b.binary(Binary::Add, address, by)
}

/// Adds 1 to `count` when the bytes at the two addresses differ.
fn count_if_different(b: &mut Builder, count: Value, one: Value, other: Value) -> Value {
    let (x, y) = (b.load(BYTE, one), b.load(BYTE, other));
    let same = b.compare(Compare::Equal, x, y);
    let yes = b.int(Bits::B8, 1);
    let different = b.binary(Binary::Xor, same, yes);
    let widened = b.widen(false, different, Bits::B64);
    b.binary(Binary::Add, count, widened)
}

/// A program that copies, with the runtime's copy, every length of each of
/// the ranges, from every place of a list of bytes that is one of `offsets`
/// past an address to every place that is one of them past another, and writes
/// as 8 bytes how many bytes were not what they should be: those the copy
/// was given that did not arrive, and those around them that it must leave.
fn copies(ranges: &[(u64, u64)], offsets: u64) -> Program {
    with_runtime(Runtime::Copy, Vec::new(), |b| {
        let room = b.int(Bits::B64, MOST + 32);
        let source = b.allocate(room);
        let target = b.allocate(room);
        counted(b, room, vec![], |b, i, _| {
            let k = b.int(Bits::B64, 37);
            let c = b.int(Bits::B64, 11);
            let scaled = b.binary(Binary::Mul, i, k);
            let shifted = b.binary(Binary::Add, scaled, c);
            let byte = b.narrow(shifted, Bits::B8);
            let place = at(b, source, i);
            b.store(place, byte);
            vec![]
        });
        let zero = b.int(Bits::B64, 0);
        let places = b.int(Bits::B64, offsets);
        let mut wrong = vec![zero];
        for &(from, to) in ranges {
        let lengths = b.int(Bits::B64, to - from + 1);
        wrong = counted(b, lengths, wrong, |b, l, wrong| {
            let first = b.int(Bits::B64, from);
            let length = b.binary(Binary::Add, first, l);
            counted(b, places, wrong, |b, read, wrong| {
                counted(b, places, wrong, |b, write, wrong| {
                    let (from_here, to_here) = (at(b, source, read), at(b, target, write));
                    let eight = b.int(Bits::B64, 8);
                    let end = b.binary(Binary::Add, write, length);
                    let end = b.binary(Binary::Add, end, eight);
                    let sentinel = b.int(Bits::B8, UNTOUCHED);
                    counted(b, end, vec![], |b, i, _| {
                        let place = at(b, target, i);
                        b.store(place, sentinel);
                        vec![]
                    });
                    b.call(1, vec![to_here, from_here, length], &[]);
                    let wrong = counted(b, length, wrong, |b, i, wrong| {
                        let (x, y) = (at(b, from_here, i), at(b, to_here, i));
                        vec![count_if_different(b, wrong[0], x, y)]
                    });
                    // What lies before and after the bytes must be as it was.
                    let guard = b.allocate(eight);
                    b.store(guard, sentinel);
                    let wrong = counted(b, write, wrong, |b, i, wrong| {
                        let place = at(b, target, i);
                        vec![count_if_different(b, wrong[0], place, guard)]
                    });
                    counted(b, eight, wrong, |b, i, wrong| {
                        let place = at(b, to_here, length);
                        let place = at(b, place, i);
                        vec![count_if_different(b, wrong[0], place, guard)]
                    })
                })
            })
        });
        }
        b.store(target, wrong[0]);
        let size = b.int(Bits::B64, 8);
        let call = scalar::System::Write(scalar::Stream::Stdout);
        b.emit(Op::System { call, args: vec![target, size] }, &[]);
    })
}

/// The bytes a program from `copies` writes when every byte arrived.
fn nothing_wrong() -> Vec<u8> {
    0u64.to_le_bytes().to_vec()
}

/// Every length to 40 from and to every offset of 8, and a long one from and
/// to a few, which is as much as the evaluator can do in a test's time.
fn the_copy_leaves_the_bytes_it_was_given_on_the_evaluator() {
    for program in [copies(&[(0, 40)], 8), copies(&[(MOST, MOST)], 3)] {
        let ran = scalar::run(&program);
        assert_eq!((ran.status, ran.stdout), (0, nothing_wrong()));
    }
}

/// Every length from 0 to 64, and every length within 8 of each power of two
/// from 128 to 4,096, from and to every offset of 8, in each executable this
/// machine can run. The lengths that are not covered are the same words and
/// bytes of the same loop, and every length to 4,096 took too long for the
/// slowest machine.
fn the_copy_leaves_the_bytes_it_was_given_in_every_executable() {
    let mut ranges = vec![(0, 64)];
    ranges.extend((7..=12).map(|k| ((1u64 << k) - 8, ((1u64 << k) + 8).min(MOST))));
    let program = copies(&ranges, 8);
    let said = scalar::Outcome {
        status: 0,
        stdout: nothing_wrong(),
    };
    for target in runnable() {
        scalar_runs_as(&program, target, &said);
    }
}

/// The blocks that the block can reach and that can reach it: its loop.
fn loop_of(blocks: &[scalar::Block], start: usize) -> HashSet<usize> {
    let reach = |from: usize| {
        let mut seen = HashSet::new();
        let mut open = vec![from];
        while let Some(at) = open.pop() {
            for next in blocks[at].end.successors() {
                if seen.insert(next) {
                    open.push(next);
                }
            }
        }
        seen
    };
    (0..blocks.len())
        .filter(|b| reach(start).contains(b) && reach(*b).contains(&start))
        .collect()
}

fn the_loop_of_the_copy_moves_8_bytes_in_5_instructions_or_fewer() {
    let copy = runtime::build(Runtime::Copy, &mut |_| 0);
    let word = Type::Int(Bits::B64);
    let mover = copy
        .blocks
        .iter()
        .position(|b| {
            b.instructions
                .iter()
                .any(|i| matches!(i.op, Op::Store { ty, .. } if ty == word))
        })
        .expect("no store of 8 bytes in the copy");
    let in_loop = loop_of(&copy.blocks, mover);
    assert!(in_loop.contains(&mover), "the store of 8 bytes is not in a loop");
    let of_loop = || in_loop.iter().flat_map(|b| &copy.blocks[*b].instructions);
    let instructions = of_loop().count();
    let words = of_loop()
        .filter(|i| matches!(i.op, Op::Store { ty, .. } if ty == word))
        .count();
    assert!(
        instructions <= 5 * words,
        "the loop moves {words} words in {instructions} instructions"
    );
}
