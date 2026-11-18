//! The register allocator, which every chip shares. It names no chip: a chip
//! says how many registers it offers, and which of them a call may change,
//! and the allocator gives each value of a function one home for its whole
//! life, a register or a place in the frame.
//!
//! How it works, in time roughly linear in the size of the function:
//!
//! 1. **Liveness.** For each value, a walk from each of its uses back
//!    through the blocks that lead there, which stops at the block that
//!    defines it, marks the blocks it is alive in. A block is marked once for
//!    a value, so the work is the size of what is alive, and nothing is
//!    iterated until it stops changing.
//! 2. **Spilling.** One pass over the blocks keeps, at every point, the
//!    values alive there that are held in registers to no more than the
//!    registers offered, and the values alive across a call to no more than
//!    the registers a call does not change. Where there are more, the one
//!    whose last use is furthest away goes to the frame instead.
//! 3. **Colouring.** The blocks are taken in reverse postorder, so a value is
//!    defined after everything its block is reached through, and each
//!    value takes a register that no value alive at its definition holds.
//!    Interference between values of a function in this form is chordal, so
//!    this finds a register whenever there are enough. A value alive across a
//!    call takes a register the call does not change. Any other takes one a
//!    call may change first, so a function that calls nothing uses no
//!    register it must save. A parameter of the function takes the register
//!    its call passes it in, when it is free and the parameter may have it.
//! 4. **The frame.** Each value that has no register has a place of its own,
//!    at its size.
//!
//! A value is alive from after the instruction that defines it until its last
//! use, so an instruction's result may take the register of an operand that
//! the instruction reads last. A call, and a call into the system, may change
//! every register the chip says a call may change.

use super::frame::lay_out;
use crate::scalar::{End, Function, Jump, Op, Value};

/// The registers a chip offers the allocator, by number. The registers from 0
/// below `changed` are those a call may change, and the `preserved` after them
/// are those it does not. The first `passed` of those a call may change carry
/// the arguments of a call, and then its results, in order, so a parameter of
/// a function that arrives in a register is best left in it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Registers {
    pub changed: usize,
    pub preserved: usize,
    pub passed: usize,
}

impl Registers {
    pub fn count(self) -> usize {
        self.changed + self.preserved
    }

    /// Whether a call leaves the register as it was.
    pub fn is_preserved(self, register: usize) -> bool {
        register >= self.changed
    }
}

/// Where a value lives, for the whole of its life.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Home {
    /// A register, by its number.
    Register(usize),
    /// A place in the frame, from the start of the bytes the function's
    /// values take, at a multiple of the value's size.
    Frame(u32),
}

/// The homes of the values of a function.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Allocation {
    /// The home of each value, by its place.
    pub homes: Vec<Home>,
    /// How many bytes the places in the frame take.
    pub frame: u32,
    /// The registers a call does not change that some value is in, in order.
    /// The function must put each back as it found it.
    pub saved: Vec<usize>,
}

const NONE: usize = usize::MAX;

/// Gives each value of the function a home.
pub fn allocate(f: &Function, registers: Registers) -> Allocation {
    let blocks = f.blocks.len();
    let values = f.types.len();
    // Where each block starts, counting a place for its parameters, one for
    // each instruction, and one for how it ends.
    let mut base = Vec::with_capacity(blocks);
    let mut total = 0;
    for block in &f.blocks {
        base.push(total);
        total += block.instructions.len() + 2;
    }
    let mut defined_in = vec![NONE; values];
    let mut uses: Vec<Vec<(usize, usize)>> = vec![Vec::new(); values];
    let mut preds: Vec<Vec<usize>> = vec![Vec::new(); blocks];
    // The values the jumps give each parameter, and the parameters each value
    // is given to.
    let mut given: Vec<Vec<Value>> = vec![Vec::new(); values];
    let mut feeds: Vec<Vec<Value>> = vec![Vec::new(); values];
    // The register a call passes a value in, or takes it from, or a function
    // gives it back in, for the first it is found in.
    let mut passed_in = vec![NONE; values];
    for (b, block) in f.blocks.iter().enumerate() {
        for p in &block.params {
            defined_in[p.0] = b;
        }
        for (i, instruction) in block.instructions.iter().enumerate() {
            for r in &instruction.results {
                defined_in[r.0] = b;
            }
            for v in instruction.op.operands() {
                uses[v.0].push((b, i + 1));
            }
        }
        let last = block.instructions.len() + 1;
        for v in block.end.operands() {
            uses[v.0].push((b, last));
        }
        for s in block.end.successors() {
            preds[s].push(b);
        }
        for instruction in &block.instructions {
            if let Op::Call { args, .. } = &instruction.op {
                for (i, v) in args.iter().chain(&instruction.results).enumerate() {
                    let i = if i < args.len() { i } else { i - args.len() };
                    if i < registers.passed && passed_in[v.0] == NONE {
                        passed_in[v.0] = i;
                    }
                }
            }
        }
        if let End::Return(values) = &block.end {
            for (i, v) in values.iter().enumerate().take(registers.passed) {
                if passed_in[v.0] == NONE {
                    passed_in[v.0] = i;
                }
            }
        }
        let jumps: &[Jump] = match &block.end {
            End::Jump(jump) => std::slice::from_ref(jump),
            End::Switch { jumps, .. } => jumps,
            End::Return(_) => &[],
        };
        for jump in jumps {
            for (arg, param) in jump.args.iter().zip(&f.blocks[jump.block].params) {
                given[param.0].push(*arg);
                feeds[arg.0].push(*param);
            }
        }
    }

    // Liveness: the blocks each value is alive at the start of, and at the
    // end of.
    let mut live_in: Vec<Vec<Value>> = vec![Vec::new(); blocks];
    let mut live_out: Vec<Vec<Value>> = vec![Vec::new(); blocks];
    let mut seen_in = vec![NONE; blocks];
    let mut seen_out = vec![NONE; blocks];
    // Where each value's last use is, counting a block that it is alive at
    // the end of as using it there.
    let mut last_use = vec![0; values];
    let mut stack = Vec::new();
    for v in 0..values {
        for &(b, i) in &uses[v] {
            last_use[v] = last_use[v].max(base[b] + i);
            if defined_in[v] != b && seen_in[b] != v {
                seen_in[b] = v;
                live_in[b].push(Value(v));
                stack.push(b);
            }
        }
        while let Some(b) = stack.pop() {
            for &p in &preds[b] {
                if seen_out[p] != v {
                    seen_out[p] = v;
                    live_out[p].push(Value(v));
                    last_use[v] = last_use[v].max(base[p] + f.blocks[p].instructions.len() + 1);
                }
                if defined_in[v] != p && seen_in[p] != v {
                    seen_in[p] = v;
                    live_in[p].push(Value(v));
                    stack.push(p);
                }
            }
        }
    }

    // Where each value is last read, and which are never read at all, at
    // each place.
    let mut dying: Vec<Vec<Value>> = vec![Vec::new(); total];
    let mut dead: Vec<Vec<Value>> = vec![Vec::new(); total];
    let mut alive = vec![false; values];
    for (b, block) in f.blocks.iter().enumerate() {
        for v in &live_out[b] {
            alive[v.0] = true;
        }
        let last = block.instructions.len() + 1;
        for v in block.end.operands() {
            if !alive[v.0] {
                alive[v.0] = true;
                dying[base[b] + last].push(v);
            }
        }
        for (i, instruction) in block.instructions.iter().enumerate().rev() {
            for r in &instruction.results {
                if alive[r.0] {
                    alive[r.0] = false;
                } else {
                    dead[base[b] + i + 1].push(*r);
                }
            }
            for v in instruction.op.operands() {
                if !alive[v.0] {
                    alive[v.0] = true;
                    dying[base[b] + i + 1].push(v);
                }
            }
        }
        for p in &block.params {
            if alive[p.0] {
                alive[p.0] = false;
            } else {
                dead[base[b]].push(*p);
            }
        }
        for v in live_in[b].iter().chain(&live_out[b]) {
            alive[v.0] = false;
        }
    }

    let count = registers.count();
    let mut spilled = vec![false; values];
    let mut crosses = vec![false; values];
    spill(
        f,
        registers,
        &Facts {
            base: &base,
            live_in: &live_in,
            dying: &dying,
            dead: &dead,
            last_use: &last_use,
        },
        &mut spilled,
        &mut crosses,
    );

    // Colouring.
    let mut register = vec![NONE; values];
    let mut holder = vec![NONE; count];
    for b in reverse_postorder(&f.blocks) {
        holder.fill(NONE);
        for v in &live_in[b] {
            if spilled[v.0] {
                continue;
            }
            // A block that nothing reaches may read a value that no path
            // from its definition leads to, so that it was never alive
            // there, or one defined in another such block, taken later.
            // Such a value may have no register yet, or the register of
            // another that the block reads. It goes to the frame.
            match register[v.0] {
                NONE => spilled[v.0] = true,
                r if holder[r] != NONE => spilled[v.0] = true,
                r => holder[r] = v.0,
            }
        }
        let block = &f.blocks[b];
        for (i, p) in block.params.iter().enumerate() {
            // Only the function's own parameters arrive in registers.
            let arrives = if b == 0 && i < registers.passed {
                Some(i)
            } else {
                None
            };
            let near = if arrives.is_some() {
                Vec::new()
            } else {
                near(&given[p.0], &register, &spilled, passed_in[p.0])
            };
            colour(
                registers,
                &mut holder,
                &mut register,
                &mut spilled,
                &crosses,
                *p,
                arrives,
                &near,
            );
        }
        release(&mut holder, &register, &dead[base[b]]);
        for (i, instruction) in block.instructions.iter().enumerate() {
            let at = base[b] + i + 1;
            release(&mut holder, &register, &dying[at]);
            for r in &instruction.results {
                let near = near(&feeds[r.0], &register, &spilled, passed_in[r.0]);
                colour(
                    registers,
                    &mut holder,
                    &mut register,
                    &mut spilled,
                    &crosses,
                    *r,
                    None,
                    &near,
                );
            }
            release(&mut holder, &register, &dead[at]);
        }
    }

    let frame_values: Vec<usize> = (0..values)
        .filter(|&v| register[v] == NONE || spilled[v])
        .collect();
    let (places, frame) = lay_out(frame_values.iter().map(|&v| f.types[v]));
    let mut homes = vec![Home::Frame(0); values];
    for (v, home) in homes.iter_mut().enumerate() {
        if register[v] != NONE && !spilled[v] {
            *home = Home::Register(register[v]);
        }
    }
    for (&v, at) in frame_values.iter().zip(places) {
        homes[v] = Home::Frame(at);
    }
    let mut saved: Vec<usize> = homes
        .iter()
        .filter_map(|h| match h {
            Home::Register(r) if registers.is_preserved(*r) => Some(*r),
            _ => None,
        })
        .collect();
    saved.sort_unstable();
    saved.dedup();
    Allocation {
        homes,
        frame,
        saved,
    }
}

/// What liveness found, by place.
struct Facts<'a> {
    base: &'a [usize],
    live_in: &'a [Vec<Value>],
    dying: &'a [Vec<Value>],
    dead: &'a [Vec<Value>],
    last_use: &'a [usize],
}

/// The values held in registers that are alive at one point.
struct Held {
    values: Vec<Value>,
    at: Vec<usize>,
}

impl Held {
    fn add(&mut self, v: Value) {
        if self.at[v.0] == NONE {
            self.at[v.0] = self.values.len();
            self.values.push(v);
        }
    }

    fn remove(&mut self, v: Value) {
        let at = self.at[v.0];
        if at != NONE {
            self.values.swap_remove(at);
            if let Some(moved) = self.values.get(at) {
                self.at[moved.0] = at;
            }
            self.at[v.0] = NONE;
        }
    }

    fn clear(&mut self) {
        for v in self.values.drain(..) {
            self.at[v.0] = NONE;
        }
    }
}

/// Sends values to the frame until, at every point, no more are held in
/// registers than the chip offers, and no more are held across a call in
/// registers than a call leaves alone.
fn spill(
    f: &Function,
    registers: Registers,
    facts: &Facts,
    spilled: &mut [bool],
    crosses: &mut [bool],
) {
    let values = f.types.len();
    let mut held = Held {
        values: Vec::new(),
        at: vec![NONE; values],
    };
    // Whether a value is read last at the place being looked at.
    let mut last_here = vec![NONE; values];
    // The one whose last use is furthest away, of the values.
    let furthest = |values: &[Value]| -> usize {
        let mut best = 0;
        for (i, v) in values.iter().enumerate() {
            if facts.last_use[v.0] > facts.last_use[values[best].0] {
                best = i;
            }
        }
        best
    };
    for (b, block) in f.blocks.iter().enumerate() {
        held.clear();
        for v in &facts.live_in[b] {
            if !spilled[v.0] {
                held.add(*v);
            }
        }
        let squeeze = |held: &mut Held, spilled: &mut [bool]| {
            while held.values.len() > registers.count() {
                let v = held.values[furthest(&held.values)];
                spilled[v.0] = true;
                held.remove(v);
            }
        };
        for p in &block.params {
            if !spilled[p.0] {
                held.add(*p);
            }
        }
        squeeze(&mut held, spilled);
        for v in &facts.dead[facts.base[b]] {
            held.remove(*v);
        }
        for (i, instruction) in block.instructions.iter().enumerate() {
            let at = facts.base[b] + i + 1;
            if instruction.op.is_call() {
                for v in &facts.dying[at] {
                    last_here[v.0] = at;
                }
                let mut across: Vec<Value> = (held.values.iter().copied())
                    .filter(|v| last_here[v.0] != at)
                    .collect();
                for v in &across {
                    crosses[v.0] = true;
                }
                while across.len() > registers.preserved {
                    let v = across.swap_remove(furthest(&across));
                    spilled[v.0] = true;
                    held.remove(v);
                }
            }
            for v in &facts.dying[at] {
                held.remove(*v);
            }
            for r in &instruction.results {
                if !spilled[r.0] {
                    held.add(*r);
                }
            }
            squeeze(&mut held, spilled);
            for v in &facts.dead[at] {
                held.remove(*v);
            }
        }
    }
}

/// The registers that the values given to a parameter, or the parameters a
/// value is given to, are in so far, the one most of them are in first. A
/// value that moves in or out of the register of the one it is given to or
/// takes is not moved at the jump.
fn near(others: &[Value], register: &[usize], spilled: &[bool], passed: usize) -> Vec<usize> {
    let mut found: Vec<(usize, usize)> = Vec::new();
    for v in others {
        let r = register[v.0];
        if r == NONE || spilled[v.0] {
            continue;
        }
        match found.iter_mut().find(|(seen, _)| *seen == r) {
            Some((_, n)) => *n += 1,
            None => found.push((r, 1)),
        }
    }
    // The sort keeps the order they were found in among those that tie.
    found.sort_by_key(|&(_, n)| std::cmp::Reverse(n));
    let mut near: Vec<usize> = found.into_iter().map(|(r, _)| r).collect();
    if passed != NONE {
        near.push(passed);
    }
    near
}

/// Gives the value the register it arrives in, if it has one and may have it
/// and no value alive holds it, and otherwise the first of the registers
/// `near` that it may have and no value alive holds, and otherwise the first
/// register that no value alive holds, from those it may have, or sends it to
/// the frame when there is none.
#[allow(clippy::too_many_arguments)]
fn colour(
    registers: Registers,
    holder: &mut [usize],
    register: &mut [usize],
    spilled: &mut [bool],
    crosses: &[bool],
    v: Value,
    arrives: Option<usize>,
    near: &[usize],
) {
    if spilled[v.0] {
        return;
    }
    let from = if crosses[v.0] { registers.changed } else { 0 };
    let free = |r: usize| r >= from && r < registers.count() && holder[r] == NONE;
    let wanted = arrives
        .filter(|&r| free(r))
        .or_else(|| near.iter().copied().find(|&r| free(r)));
    match wanted.or_else(|| (from..registers.count()).find(|&r| holder[r] == NONE)) {
        Some(r) => {
            holder[r] = v.0;
            register[v.0] = r;
        }
        None => spilled[v.0] = true,
    }
}

fn release(holder: &mut [usize], register: &[usize], values: &[Value]) {
    for v in values {
        let r = register[v.0];
        if r != NONE && holder[r] == v.0 {
            holder[r] = NONE;
        }
    }
}

/// The blocks in reverse postorder of a walk from the first, so each comes
/// after every block that it can only be reached through, then the blocks
/// that nothing reaches, in order.
fn reverse_postorder(blocks: &[crate::scalar::Block]) -> Vec<usize> {
    let mut seen = vec![false; blocks.len()];
    let mut post = Vec::with_capacity(blocks.len());
    let mut roots = vec![0];
    roots.extend(1..blocks.len());
    let mut order = Vec::new();
    for root in roots {
        if seen[root] {
            continue;
        }
        let from = post.len();
        seen[root] = true;
        let mut stack = vec![(root, blocks[root].end.successors(), 0)];
        while let Some((b, next, i)) = stack.last_mut() {
            if let Some(&s) = next.get(*i) {
                *i += 1;
                if !seen[s] {
                    seen[s] = true;
                    let successors = blocks[s].end.successors();
                    stack.push((s, successors, 0));
                }
            } else {
                post.push(*b);
                stack.pop();
            }
        }
        order.extend(post[from..].iter().rev());
    }
    order
}
