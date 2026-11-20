//! Registers offered by kind: a float is held in a register of floats or in the
//! frame, and every other value in a register of integers or in the frame, and
//! the rules of the allocator hold for each kind, whatever each offers.

use std::collections::HashSet;

use smith::scalar::{End, Function, Op, Value};
use smith::target::alloc::{Home, Kinds, Registers, allocate_kinds, kind};

use crate::Test;
use crate::float_form::{self as ff};
use crate::generate;

pub const ONCE: &[Test] = &[(
    "values_are_held_in_registers_of_their_kind_and_no_two_that_are_alive_together_share_one",
    values_are_held_in_registers_of_their_kind_and_no_two_that_are_alive_together_share_one,
)];

const INTS: Registers = Registers {
    changed: 5,
    preserved: 6,
    passed: 5,
};

/// What the chip offers of floats, from none to more than a function needs.
pub const FLOATS: [Registers; 6] = [
    Registers {
        changed: 14,
        preserved: 0,
        passed: 8,
    },
    Registers {
        changed: 2,
        preserved: 0,
        passed: 2,
    },
    Registers {
        changed: 1,
        preserved: 1,
        passed: 1,
    },
    Registers {
        changed: 3,
        preserved: 2,
        passed: 2,
    },
    Registers {
        changed: 0,
        preserved: 0,
        passed: 0,
    },
    Registers {
        changed: 4,
        preserved: 4,
        passed: 3,
    },
];

fn jumps(end: &End) -> Vec<&smith::scalar::Jump> {
    match end {
        End::Jump(j) => vec![j],
        End::Switch { jumps, .. } => jumps.iter().collect(),
        End::Return(_) => Vec::new(),
    }
}

/// The values alive at the start of each block, found by going round until
/// nothing changes, which is slow and plain.
fn live_in(f: &Function) -> Vec<HashSet<usize>> {
    let mut live: Vec<HashSet<usize>> = vec![HashSet::new(); f.blocks.len()];
    loop {
        let mut changed = false;
        for b in (0..f.blocks.len()).rev() {
            let mut now: HashSet<usize> = HashSet::new();
            for j in jumps(&f.blocks[b].end) {
                let target = &f.blocks[j.block];
                now.extend(
                    live[j.block]
                        .iter()
                        .filter(|v| !target.params.contains(&Value(**v))),
                );
            }
            let block = &f.blocks[b];
            now.extend(block.end.operands().map(|v| v.0));
            for i in block.instructions.iter().rev() {
                for r in &i.results {
                    now.remove(&r.0);
                }
                now.extend(i.op.operands().map(|v| v.0));
            }
            for p in &block.params {
                now.remove(&p.0);
            }
            if now != live[b] {
                live[b] = now;
                changed = true;
            }
        }
        if !changed {
            return live;
        }
    }
}

pub fn check(f: &Function, floats: Registers) {
    let kinds = Kinds {
        ints: INTS,
        floats,
    };
    let homeless = vec![false; f.types.len()];
    let allocation = allocate_kinds(f, kinds, &homeless);
    let live = live_in(f);
    let first_float = INTS.count();
    let is_changed = |r: usize| {
        if r < first_float {
            !INTS.is_preserved(r)
        } else {
            !floats.is_preserved(r - first_float)
        }
    };
    for (v, home) in allocation.homes.iter().enumerate() {
        if let Home::Register(r) = home {
            let is_float = kind(f.types[v]) == 1;
            assert_eq!(*r >= first_float, is_float, "value {v} is in register {r}");
            assert!(*r < first_float + floats.count(), "register {r} is not offered");
        }
    }
    let home = |v: usize| allocation.homes[v];
    let together = |values: &HashSet<usize>| {
        let mut held: Vec<(usize, usize)> = values
            .iter()
            .filter_map(|v| match home(*v) {
                Home::Register(r) => Some((r, *v)),
                _ => None,
            })
            .collect();
        held.sort_unstable();
        for pair in held.windows(2) {
            assert_ne!(pair[0].0, pair[1].0, "values {} and {} share a register", pair[0].1, pair[1].1);
        }
    };
    for block in &f.blocks {
        // Every value alive after each instruction, walking back.
        let mut alive: HashSet<usize> = HashSet::new();
        for j in jumps(&block.end) {
            let target = &f.blocks[j.block];
            alive.extend(
                live[j.block]
                    .iter()
                    .filter(|v| !target.params.contains(&Value(**v))),
            );
        }
        alive.extend(block.end.operands().map(|v| v.0));
        together(&alive);
        for i in block.instructions.iter().rev() {
            let mut at: HashSet<usize> = alive.clone();
            at.extend(i.results.iter().map(|r| r.0));
            together(&at);
            if matches!(i.op, Op::Call { .. } | Op::System { .. }) {
                // What is alive across the call is alive after it and before it.
                let results: HashSet<usize> = i.results.iter().map(|r| r.0).collect();
                for v in alive.iter().filter(|v| !results.contains(v)) {
                    if let Home::Register(r) = home(*v) {
                        assert!(!is_changed(r), "value {v} is in register {r} across a call");
                        // A register that a call leaves alone may leave only part of
                        // a vector, so a vector is never in one across a call.
                        assert!(
                            !matches!(f.types[*v], smith::scalar::Type::Vector(_)),
                            "the vector {v} is in register {r} across a call"
                        );
                    }
                }
            }
            for r in &i.results {
                alive.remove(&r.0);
            }
            alive.extend(i.op.operands().map(|v| v.0));
        }
        let mut start = alive;
        start.extend(block.params.iter().map(|p| p.0));
        together(&start);
    }
    // A function that has no float alive across a call saves no register of
    // floats.
    let saved_floats = allocation.saved.iter().filter(|r| **r >= first_float).count();
    assert!(saved_floats <= floats.preserved, "{saved_floats} saved");
}

fn values_are_held_in_registers_of_their_kind_and_no_two_that_are_alive_together_share_one() {
    generate::check(
        6134,
        30,
        |g| {
            if g.below(2) == 0 {
                ff::program(g)
            } else {
                ff::pressure(g)
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
