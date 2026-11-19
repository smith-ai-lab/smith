//! Registers offered by kind, for vectors: a vector is held in a register of
//! floats or in the frame, with the rules of the allocator that hold for every
//! kind, and in no register across a call, as a register that a call leaves alone
//! may leave only part of it. A vector is passed and returned in a register of
//! the kind of floats.

use smith::scalar::{Function, Op};
use smith::target::alloc::{Home, Kinds, Registers, allocate_kinds};

use crate::Test;
use crate::float_registers::{FLOATS, check};
use crate::generate;
use crate::vector_form as vf;

pub const ONCE: &[Test] = &[
    (
        "vectors_are_held_in_registers_of_floats_and_in_none_across_a_call",
        vectors_are_held_in_registers_of_floats_and_in_none_across_a_call,
    ),
    (
        "a_vector_that_arrives_in_a_register_and_is_not_alive_across_a_call_stays_in_it",
        a_vector_that_arrives_in_a_register_and_is_not_alive_across_a_call_stays_in_it,
    ),
];

const INTS: Registers = Registers {
    changed: 5,
    preserved: 6,
    passed: 5,
};

fn vectors_are_held_in_registers_of_floats_and_in_none_across_a_call() {
    generate::check(
        8134,
        30,
        |g| {
            if g.below(2) == 0 {
                vf::program(g)
            } else {
                vf::pressure(g)
            }
        },
        |program| {
            for f in &program.functions {
                for floats in FLOATS {
                    check(f, floats);
                }
            }
        },
    );
}

/// A function that takes vectors, does one instruction on each pair, and gives the
/// results back, calling nothing.
fn pairs(count: usize, lane: smith::scalar::Lane) -> Function {
    let types = vec![vf::vec(lane); count];
    let mut b = smith::scalar::Builder::new();
    let params = b.params(&types);
    let given: Vec<_> = params
        .iter()
        .map(|p| b.vector_binary(smith::scalar::Binary::Xor, *p, *p))
        .collect();
    b.ret(given);
    b.finish(&types)
}

fn a_vector_that_arrives_in_a_register_and_is_not_alive_across_a_call_stays_in_it() {
    for floats in [FLOATS[0], FLOATS[3]] {
        let kinds = Kinds {
            ints: INTS,
            floats,
        };
        let first = INTS.count();
        let f = pairs(floats.passed, smith::scalar::Lane::Int(smith::scalar::Bits::B32));
        assert!(
            f.blocks
                .iter()
                .all(|b| b.instructions.iter().all(|i| !matches!(i.op, Op::Call { .. })))
        );
        let allocation = allocate_kinds(&f, kinds, &vec![false; f.types.len()]);
        for (k, p) in f.blocks[0].params.iter().enumerate() {
            assert_eq!(
                allocation.homes[p.0],
                Home::Register(first + k),
                "the vector {k} is where it arrives"
            );
        }
    }
}
