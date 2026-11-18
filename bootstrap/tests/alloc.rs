//! The register allocator, tested on its own, with no target: on every
//! function of every generated program, and on functions built to have
//! loops that go back, blocks that nothing reaches, calls, and more values
//! alive at once than any chip has registers.

use std::sync::OnceLock;

use smith::scalar::{self, Bits, Builder, Function, Type, Value};
use smith::target::alloc::{Allocation, Home, Registers, allocate};
use smith::target::moves::{Move, Step, sequence};

use crate::Test;
use crate::generate::{self, Gen};
use crate::scalar::every_function;

pub const ONCE: &[Test] = &[
    (
        "no_two_values_alive_together_share_a_home",
        no_two_values_alive_together_share_a_home,
    ),
    (
        "no_value_alive_across_a_call_is_in_a_register_the_call_may_change",
        no_value_alive_across_a_call_is_in_a_register_the_call_may_change,
    ),
    (
        "a_function_that_calls_nothing_and_fits_the_registers_keeps_everything_in_them",
        a_function_that_calls_nothing_and_fits_the_registers_keeps_everything_in_them,
    ),
    (
        "a_function_saves_the_registers_a_call_does_not_change_that_it_uses_and_no_others",
        a_function_saves_the_registers_a_call_does_not_change_that_it_uses_and_no_others,
    ),
    (
        "a_parameter_that_arrives_in_a_register_stays_there_when_it_may",
        a_parameter_that_arrives_in_a_register_stays_there_when_it_may,
    ),
    (
        "allocating_twice_gives_the_same_homes",
        allocating_twice_gives_the_same_homes,
    ),
    (
        "copying_as_if_all_at_once_leaves_every_place_with_what_it_was_to_get",
        copying_as_if_all_at_once_leaves_every_place_with_what_it_was_to_get,
    ),
];

fn size(ty: Type) -> u32 {
    match ty {
        Type::Int(bits) => bits.count() / 8,
        Type::Address => 8,
    }
}

/// What is alive where, worked out by iterating to a fixed point, which is
/// slow and plain, so that it does not share a mistake with the allocator.
#[derive(Clone)]
struct Facts {
    /// The sets of values that must have homes of their own: for each
    /// block, those alive at its start with its parameters, and for each
    /// instruction, those alive after it with its results.
    together: Vec<Vec<Value>>,
    /// For each call, the values alive before and after it.
    across: Vec<Vec<Value>>,
    /// Whether the function has a call.
    calls: bool,
}

fn facts(f: &Function) -> Facts {
    let n = f.blocks.len();
    let values = f.types.len();
    // What each block reads before it defines it, and what it defines.
    let mut reads = vec![vec![false; values]; n];
    let mut defines = vec![vec![false; values]; n];
    for (b, block) in f.blocks.iter().enumerate() {
        for p in &block.params {
            defines[b][p.0] = true;
        }
        for instruction in &block.instructions {
            for v in instruction.op.operands() {
                if !defines[b][v.0] {
                    reads[b][v.0] = true;
                }
            }
            for r in &instruction.results {
                defines[b][r.0] = true;
            }
        }
        for v in block.end.operands() {
            if !defines[b][v.0] {
                reads[b][v.0] = true;
            }
        }
    }
    let mut live_in = vec![vec![false; values]; n];
    let mut live_out = vec![vec![false; values]; n];
    loop {
        let mut changed = false;
        for b in (0..n).rev() {
            for s in f.blocks[b].end.successors() {
                for v in 0..values {
                    if live_in[s][v] && !live_out[b][v] {
                        live_out[b][v] = true;
                        changed = true;
                    }
                }
            }
            for v in 0..values {
                let alive = reads[b][v] || (live_out[b][v] && !defines[b][v]);
                if alive && !live_in[b][v] {
                    live_in[b][v] = true;
                    changed = true;
                }
            }
        }
        if !changed {
            break;
        }
    }
    let mut together = Vec::new();
    let mut across = Vec::new();
    let mut calls = false;
    for (b, block) in f.blocks.iter().enumerate() {
        let mut start: Vec<Value> = (0..values).filter(|v| live_in[b][*v]).map(Value).collect();
        start.extend(&block.params);
        together.push(start);
        // Walk the block backwards, with what is alive after each place.
        let mut alive = live_out[b].clone();
        for v in block.end.operands() {
            alive[v.0] = true;
        }
        for instruction in block.instructions.iter().rev() {
            let after: Vec<Value> = (0..values).filter(|v| alive[*v]).map(Value).collect();
            let mut set = after.clone();
            for r in &instruction.results {
                if !set.contains(r) {
                    set.push(*r);
                }
            }
            together.push(set);
            if instruction.op.is_call() {
                calls = true;
                across.push(
                    after
                        .into_iter()
                        .filter(|v| !instruction.results.contains(v))
                        .collect(),
                );
            }
            for r in &instruction.results {
                alive[r.0] = false;
            }
            for v in instruction.op.operands() {
                alive[v.0] = true;
            }
        }
    }
    Facts {
        together,
        across,
        calls,
    }
}

/// How many values are alive together at the most.
fn most_together(facts: &Facts) -> usize {
    facts.together.iter().map(Vec::len).max().unwrap_or(0)
}

/// Whether two homes hold the same bytes or the same register.
fn overlap(f: &Function, a: (Value, Home), b: (Value, Home)) -> bool {
    match (a.1, b.1) {
        (Home::Register(x), Home::Register(y)) => x == y,
        (Home::Frame(x), Home::Frame(y)) => {
            let (sx, sy) = (size(f.types[a.0.0]), size(f.types[b.0.0]));
            x < y + sy && y < x + sx
        }
        _ => false,
    }
}

/// The registers to offer: none, few, and more than the function needs.
fn offers(needs: usize) -> Vec<Registers> {
    let mut offers = vec![
        (0, 0),
        (0, 1),
        (1, 0),
        (1, 1),
        (2, 3),
        (3, 2),
        (5, 6),
        (15, 10),
    ];
    offers.extend([(needs, 0), (0, needs), (needs, needs), (needs + 2, 3)]);
    let mut offers: Vec<Registers> = offers
        .into_iter()
        .map(|(changed, preserved)| Registers {
            changed,
            preserved,
            passed: changed,
        })
        .collect();
    // A chip that passes fewer in registers than a call may change.
    offers.push(Registers {
        changed: 5,
        preserved: 6,
        passed: 2,
    });
    offers
}

/// A function, what is alive in it, the registers offered, and what the
/// allocator gave.
struct Case {
    f: Function,
    facts: Facts,
    registers: Registers,
    allocation: Allocation,
}

/// Every function of every generated program and every function built,
/// with every set of registers offered, allocated once for all the tests.
fn cases() -> &'static [Case] {
    static CASES: OnceLock<Vec<Case>> = OnceLock::new();
    CASES.get_or_init(|| {
        let mut functions: Vec<Function> = every_function().into_iter().cloned().collect();
        functions.extend(generated_functions());
        let mut cases = Vec::new();
        for f in functions {
            let facts = facts(&f);
            for registers in offers(most_together(&facts)) {
                let allocation = allocate(&f, registers, &vec![false; f.types.len()]);
                cases.push(Case {
                    f: f.clone(),
                    facts: facts.clone(),
                    registers,
                    allocation,
                });
            }
        }
        cases
    })
}

/// A property of an allocation, for every function and every set of
/// registers offered.
fn for_every_allocation(check: impl Fn(&Function, &Facts, Registers, &Allocation)) {
    for case in cases() {
        check(&case.f, &case.facts, case.registers, &case.allocation);
    }
}

fn no_two_values_alive_together_share_a_home() {
    for_every_allocation(|f, facts, registers, a| {
        assert_eq!(a.homes.len(), f.types.len());
        for (v, home) in a.homes.iter().enumerate() {
            match home {
                Home::Register(r) => assert!(*r < registers.count()),
                Home::Frame(at) => {
                    let size = size(f.types[v]);
                    assert_eq!(at % size, 0, "a place is a multiple of its size");
                    assert!(at + size <= a.frame, "a place is in the frame");
                }
                Home::Held => panic!("{v} has no home, and none was said to need none"),
            }
        }
        for set in &facts.together {
            for (i, x) in set.iter().enumerate() {
                for y in &set[i + 1..] {
                    assert!(
                        !overlap(f, (*x, a.homes[x.0]), (*y, a.homes[y.0])),
                        "{x:?} and {y:?} are alive together and share a home, with {registers:?}\n{:?}\n{:?}",
                        f.blocks,
                        a.homes
                    );
                }
            }
        }
    });
}

fn no_value_alive_across_a_call_is_in_a_register_the_call_may_change() {
    for_every_allocation(|_, facts, registers, a| {
        for set in &facts.across {
            for v in set {
                if let Home::Register(r) = a.homes[v.0] {
                    assert!(
                        registers.is_preserved(r),
                        "{v:?} is alive across a call, in register {r} of {registers:?}"
                    );
                }
            }
        }
    });
}

/// Whether every block can be reached from the first.
fn all_reachable(f: &Function) -> bool {
    let mut seen = vec![false; f.blocks.len()];
    let mut stack = vec![0];
    seen[0] = true;
    while let Some(b) = stack.pop() {
        for s in f.blocks[b].end.successors() {
            if !seen[s] {
                seen[s] = true;
                stack.push(s);
            }
        }
    }
    seen.into_iter().all(|s| s)
}

fn a_function_that_calls_nothing_and_fits_the_registers_keeps_everything_in_them() {
    for_every_allocation(|f, facts, registers, a| {
        // A block that nothing reaches may read what no path leads to, so
        // what is alive there is not what any run holds.
        if !facts.calls && most_together(facts) <= registers.count() && all_reachable(f) {
            assert_eq!(a.frame, 0, "{registers:?}");
            for home in &a.homes {
                assert!(matches!(home, Home::Register(_)), "{registers:?}");
            }
        }
    });
}

fn a_function_saves_the_registers_a_call_does_not_change_that_it_uses_and_no_others() {
    for_every_allocation(|_, facts, registers, a| {
        let mut used: Vec<usize> = a
            .homes
            .iter()
            .filter_map(|h| match h {
                Home::Register(r) if registers.is_preserved(*r) => Some(*r),
                _ => None,
            })
            .collect();
        used.sort_unstable();
        used.dedup();
        assert_eq!(a.saved, used);
        // With nothing to cross and room for everything in the registers a
        // call may change, nothing needs saving.
        if !facts.calls && most_together(facts) <= registers.changed {
            assert!(a.saved.is_empty(), "{registers:?}");
        }
    });
}

/// A parameter that is not alive across a call, and that the allocator
/// keeps in a register, is in the one it arrives in, so that the function
/// copies nothing. A function that calls nothing and fits the registers
/// keeps every parameter that arrives in one where it arrives.
fn a_parameter_that_arrives_in_a_register_stays_there_when_it_may() {
    for_every_allocation(|f, facts, registers, a| {
        for (i, p) in f.blocks[0].params.iter().enumerate().take(registers.passed) {
            match a.homes[p.0] {
                Home::Register(r) if !registers.is_preserved(r) => {
                    assert_eq!(r, i, "{registers:?}\n{:?}", f.blocks[0].params)
                }
                _ => {}
            }
            if !facts.calls && most_together(facts) <= registers.count() && all_reachable(f) {
                assert_eq!(a.homes[p.0], Home::Register(i), "{registers:?}");
            }
        }
    });
}

fn allocating_twice_gives_the_same_homes() {
    for_every_allocation(|f, _, registers, a| {
        assert_eq!(*a, allocate(f, registers, &vec![false; f.types.len()]));
    });
}

/// Does the steps on places that hold a number each, with one more place
/// that holds what was set aside.
fn run(places: &mut [u32], steps: &[Step<usize>]) {
    let mut aside = None;
    for step in steps {
        match step {
            Step::Copy { to, from, .. } => places[*to] = places[*from],
            Step::Save { from, .. } => {
                assert!(aside.is_none(), "one is set aside at a time");
                aside = Some(places[*from]);
            }
            Step::Restore { to, .. } => places[*to] = aside.take().expect("it was set aside"),
        }
    }
    assert!(aside.is_none(), "what was set aside is put back");
}

fn copying_as_if_all_at_once_leaves_every_place_with_what_it_was_to_get() {
    let generate = |g: &mut Gen| {
        let places = 1 + g.below(10);
        // A place is written by at most one move, and read by any number.
        let mut to: Vec<usize> = (0..places).collect();
        for i in (1..places).rev() {
            to.swap(i, g.below(i + 1));
        }
        let moves: Vec<(usize, usize)> = to
            .into_iter()
            .take(1 + g.below(places))
            .map(|to| (to, g.below(places)))
            .collect();
        (places, moves)
    };
    generate::check(90, 2000, generate, |(places, moves)| {
        let before: Vec<u32> = (0..*places as u32).map(|n| 100 + n).collect();
        let mut after = before.clone();
        for (to, from) in moves {
            after[*to] = before[*from];
        }
        let moves: Vec<Move<usize>> = moves
            .iter()
            .map(|(to, from)| Move {
                to: *to,
                from: *from,
                width: 8,
            })
            .collect();
        let mut done = before.clone();
        run(&mut done, &sequence(&moves));
        assert_eq!(done, after);
    });
}

/// The kinds of value a built function holds.
const TYPES: [Type; 4] = [
    Type::Int(Bits::B8),
    Type::Int(Bits::B32),
    Type::Int(Bits::B64),
    Type::Address,
];

/// A function with several blocks that jump to any of them, forward and
/// back, some that nothing reaches, values made in the first block that
/// later blocks read, calls, and, now and then, a great many values alive
/// together.
fn function(g: &mut Gen) -> Function {
    let mut b = Builder::new();
    let blocks = 2 + g.below(6);
    let mut params: Vec<Vec<Value>> = vec![Vec::new()];
    let mut shapes: Vec<Vec<Type>> = vec![Vec::new()];
    for _ in 1..blocks {
        let types: Vec<Type> = (0..g.below(5)).map(|_| *g.pick(&TYPES)).collect();
        let (_, values) = b.block(&types);
        params.push(values);
        shapes.push(types);
    }
    // What the first block is given and makes is there for every block to
    // read.
    let own: Vec<Type> = (0..g.below(20)).map(|_| *g.pick(&TYPES)).collect();
    let given = b.params(&own);
    let wide = if g.one_in(3) {
        12 + g.below(20)
    } else {
        1 + g.below(4)
    };
    let mut entry: Vec<(Value, Type)> = given.into_iter().zip(own).collect();
    for _ in 0..wide {
        let ty = *g.pick(&TYPES);
        entry.push((value_of(&mut b, ty, g.below(1000) as u64), ty));
    }
    for block in 0..blocks {
        if block > 0 {
            b.at(block);
        }
        let mut pool = entry.clone();
        pool.extend(
            params[block]
                .iter()
                .copied()
                .zip(shapes[block].iter().copied()),
        );
        for _ in 0..g.below(8) {
            let (x, ty) = *g.pick(&pool);
            match g.below(4) {
                0 => {
                    let made = value_of(&mut b, ty, g.below(100) as u64);
                    pool.push((made, ty));
                }
                1 => {
                    let same: Vec<Value> = pool.iter().filter(|p| p.1 == ty).map(|p| p.0).collect();
                    if ty != Type::Address {
                        let y = *g.pick(&same);
                        let sum = b.binary(scalar::Binary::Add, x, y);
                        pool.push((sum, ty));
                    }
                }
                2 => {
                    let args: Vec<Value> = (0..g.below(4)).map(|_| g.pick(&pool).0).collect();
                    let results: Vec<Type> = (0..g.below(3)).map(|_| *g.pick(&TYPES)).collect();
                    let made = b.call(0, args, &results);
                    pool.extend(made.into_iter().zip(results));
                }
                _ => {}
            }
        }
        // How it ends: by returning, or by jumping to a block that is not
        // the first, giving it what it takes.
        let jump = |g: &mut Gen, b: &mut Builder, pool: &mut Vec<(Value, Type)>| {
            let to = 1 + g.below(blocks - 1);
            let args: Vec<Value> = shapes[to]
                .iter()
                .map(|ty| {
                    let of: Vec<Value> = pool.iter().filter(|p| p.1 == *ty).map(|p| p.0).collect();
                    if of.is_empty() {
                        let made = value_of(b, *ty, 0);
                        pool.push((made, *ty));
                        made
                    } else {
                        *g.pick(&of)
                    }
                })
                .collect();
            (to, args)
        };
        match g.below(4) {
            0 => b.ret(vec![]),
            1 => {
                let (to, args) = jump(g, &mut b, &mut pool);
                b.jump(to, args);
            }
            _ => {
                let on = pool
                    .iter()
                    .find(|p| p.1 == Type::Int(Bits::B8))
                    .map(|p| p.0);
                let on = on.unwrap_or_else(|| b.int(Bits::B8, 1));
                let jumps = (0..2 + g.below(2))
                    .map(|_| jump(g, &mut b, &mut pool))
                    .collect();
                b.switch(on, jumps);
            }
        }
    }
    b.finish(&[])
}

/// A value of the type, made in the block being built.
fn value_of(b: &mut Builder, ty: Type, n: u64) -> Value {
    match ty {
        Type::Int(bits) => b.int(bits, n),
        Type::Address => b.null(),
    }
}

fn generated_functions() -> Vec<Function> {
    (0..300)
        .map(|case| generate::sample(91, case, function))
        .collect()
}
