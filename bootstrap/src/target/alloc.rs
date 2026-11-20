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
//!    call may change first, and a function with no value alive across a call
//!    is offered no other, so it uses no register it must save, unless it
//!    calls nothing and has a loop, when it is offered every register. A parameter
//!    of a block, a result, and a value passed or given back take the register
//!    of the value they are given to or from, or that a call passes them in,
//!    when it is free and the value may have it.
//! 4. **The frame.** Each value that has no register has a place of its own,
//!    at its size.
//!
//! A value is alive from after the instruction that defines it until its last
//! use, so an instruction's result may take the register of an operand that
//! the instruction reads last. A call, and a call into the system, may change
//! every register the chip says a call may change.

use super::frame::lay_out;
use crate::scalar::{End, Function, Jump, Op, Type, Value};

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

/// The registers a chip offers, of each kind: the registers of integers and
/// addresses, and the registers of floats. A register is named by one number
/// for the whole chip: the registers of integers are the numbers from 0, and
/// those of floats follow them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Kinds {
    pub ints: Registers,
    pub floats: Registers,
}

/// The registers the allocator may still give out of those a chip offers, by
/// kind, which are fewer for a kind when no value of it is alive across a call:
/// the numbers of the registers stay what the chip says.
#[derive(Clone, Copy)]
struct Offer {
    ints: Registers,
    floats: Registers,
    /// The number of the first register of floats.
    float_base: usize,
}

impl Offer {
    fn new(kinds: Kinds) -> Offer {
        Offer {
            ints: kinds.ints,
            floats: kinds.floats,
            float_base: kinds.ints.count(),
        }
    }

    fn of(self, kind: usize) -> Registers {
        [self.ints, self.floats][kind]
    }

    fn first(self, kind: usize) -> usize {
        if kind == 0 { 0 } else { self.float_base }
    }

    fn count(self) -> usize {
        self.float_base + self.floats.count()
    }

    fn is_preserved(self, register: usize) -> bool {
        if register < self.float_base {
            self.ints.is_preserved(register)
        } else {
            self.floats.is_preserved(register - self.float_base)
        }
    }
}

/// The kind of register a value of the type is held in: 0 for an integer or an
/// address, 1 for a float or a vector.
pub fn kind(ty: Type) -> usize {
    usize::from(matches!(ty, Type::Float(_) | Type::Vector(_)))
}

/// Whether a value of the type may not be kept in a register across a call. A
/// register that a call leaves alone may leave only part of a vector, as the
/// registers `v8` to `v15` of arm64 do, so a vector is never kept in one.
fn is_lost_by_calls(ty: Type) -> bool {
    matches!(ty, Type::Vector(_))
}

/// Where a value lives, for the whole of its life.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Home {
    /// A register, by its number.
    Register(usize),
    /// A place in the frame, from the start of the bytes the function's
    /// values take, at a multiple of the value's size.
    Frame(u32),
    /// No place at all: the value was said to need none, as the chip holds
    /// it in each instruction that reads it.
    Held,
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

/// Gives each value of the function a home, except the values `homeless` says
/// the chip holds in the instruction that reads each, which are given none
/// and take no register or place in the frame.
pub fn allocate(f: &Function, registers: Registers, homeless: &[bool]) -> Allocation {
    let none = Registers {
        changed: 0,
        preserved: 0,
        passed: 0,
    };
    let kinds = Kinds {
        ints: registers,
        floats: none,
    };
    allocate_kinds(f, kinds, homeless)
}

/// Gives each value of the function a home as `allocate` does, in a register of
/// its kind: a float in one of the chip's registers of floats, and anything
/// else in one of the registers of integers. The register a value is passed in,
/// or arrives in, is the one that is the number of its kind among the values
/// passed, so the k-th float passed is in the k-th register of floats that
/// carries one.
pub fn allocate_kinds(f: &Function, kinds: Kinds, homeless: &[bool]) -> Allocation {
    let a = allocate_offering(f, kinds, homeless, false);
    // Only a function that calls nothing and spilled can gain from every
    // register, so only it is looked at for a loop.
    let spilled = a.homes.iter().any(|h| matches!(h, Home::Frame(_)));
    let calls = (f.blocks.iter()).any(|b| b.instructions.iter().any(|i| i.op.is_call()));
    if spilled && !calls && has_a_loop(&f.blocks) {
        return allocate_offering(f, kinds, homeless, true);
    }
    a
}

/// Gives each value a home, offering every register when `every` says so, and
/// otherwise only those a function with no value alive across a call is
/// offered.
fn allocate_offering(f: &Function, kinds: Kinds, homeless: &[bool], every: bool) -> Allocation {
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
    // The first parameter each value is given to by a jump.
    let mut fed_to = vec![NONE; values];
    let mut operands: Vec<Value> = Vec::new();
    // The number of the register a call passes a value in, or takes it from, or
    // a function gives it back in, for the first it is found in.
    let mut passed_in = vec![NONE; values];
    for (b, block) in f.blocks.iter().enumerate() {
        for p in &block.params {
            defined_in[p.0] = b;
        }
        for (i, instruction) in block.instructions.iter().enumerate() {
            for r in &instruction.results {
                defined_in[r.0] = b;
            }
            instruction.op.operands_into(&mut operands);
            for v in &operands {
                if !homeless[v.0] {
                    uses[v.0].push((b, i + 1));
                }
            }
        }
        let last = block.instructions.len() + 1;
        block.end.operands_into(&mut operands);
        for v in &operands {
            if !homeless[v.0] {
                uses[v.0].push((b, last));
            }
        }
        for s in block.end.successors() {
            preds[s].push(b);
        }
        for instruction in &block.instructions {
            if let Op::Call { args, .. } = &instruction.op {
                note_passed(f, Offer::new(kinds), args, &mut passed_in);
                note_passed(f, Offer::new(kinds), &instruction.results, &mut passed_in);
            }
        }
        if let End::Return(values) = &block.end {
            note_passed(f, Offer::new(kinds), values, &mut passed_in);
        }
        let jumps: &[Jump] = match &block.end {
            End::Jump(jump) => std::slice::from_ref(jump),
            End::Switch { jumps, .. } => jumps,
            End::Return(_) => &[],
        };
        for jump in jumps {
            for (arg, param) in jump.args.iter().zip(&f.blocks[jump.block].params) {
                if !homeless[arg.0] && fed_to[arg.0] == NONE {
                    fed_to[arg.0] = param.0;
                }
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
        block.end.operands_into(&mut operands);
        for &v in &operands {
            if !alive[v.0] && !homeless[v.0] {
                alive[v.0] = true;
                dying[base[b] + last].push(v);
            }
        }
        for (i, instruction) in block.instructions.iter().enumerate().rev() {
            for r in &instruction.results {
                if homeless[r.0] {
                    continue;
                }
                if alive[r.0] {
                    alive[r.0] = false;
                } else {
                    dead[base[b] + i + 1].push(*r);
                }
            }
            instruction.op.operands_into(&mut operands);
            for &v in &operands {
                if !alive[v.0] && !homeless[v.0] {
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

    let kinds_of: Vec<u8> = f.types.iter().map(|ty| kind(*ty) as u8).collect();
    let facts = Facts {
        base: &base,
        live_in: &live_in,
        dying: &dying,
        dead: &dead,
        last_use: &last_use,
    };
    // A function with no value alive across a call has no use for a register
    // a call does not change, which it would have to save, so it is offered
    // none, unless it calls nothing and has a loop: saving a register costs
    // the same once however many times the loop goes round, and a value in
    // the frame costs a store and a load every round.
    let has_a_call = (f.blocks.iter()).any(|b| b.instructions.iter().any(|i| i.op.is_call()));
    let across = if has_a_call {
        across_a_call(f, &kinds_of, &facts, homeless)
    } else if every {
        [true; 2]
    } else {
        [false; 2]
    };
    let offer = Offer::new(kinds);
    let offered = |kind: usize| {
        if across[kind] {
            offer.of(kind)
        } else {
            Registers {
                preserved: 0,
                ..offer.of(kind)
            }
        }
    };
    let kinds = Offer {
        ints: offered(0),
        floats: offered(1),
        ..Offer::new(kinds)
    };
    let count = kinds.count();
    let mut spilled = vec![false; values];
    let mut crosses = vec![false; values];
    spill(
        f,
        kinds,
        &kinds_of,
        &facts,
        &mut spilled,
        &mut crosses,
        homeless,
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
        let arrival = if b == 0 {
            arrivals(f, kinds, block)
        } else {
            Vec::new()
        };
        for (i, p) in block.params.iter().enumerate() {
            // Only the function's own parameters arrive in registers.
            let arrives = if b == 0 { arrival[i] } else { None };
            let near = if arrives.is_some() {
                Near::none()
            } else {
                // The values the jumps into the block give this parameter.
                let given = preds[b].iter().flat_map(|&pred| {
                    let jumps: &[Jump] = match &f.blocks[pred].end {
                        End::Jump(jump) => std::slice::from_ref(jump),
                        End::Switch { jumps, .. } => jumps,
                        End::Return(_) => &[],
                    };
                    jumps
                        .iter()
                        .filter(|j| j.block == b)
                        .map(move |j| j.args[i])
                });
                Near::of(given, &register, &spilled, passed_in[p.0])
            };
            colour(
                kinds,
                &mut holder,
                &mut register,
                &mut spilled,
                &crosses,
                (*p, kind(f.types[p.0])),
                arrives,
                near.registers(),
            );
        }
        release(&mut holder, &register, &dead[base[b]]);
        for (i, instruction) in block.instructions.iter().enumerate() {
            let at = base[b] + i + 1;
            release(&mut holder, &register, &dying[at]);
            for r in instruction.results.iter().filter(|r| !homeless[r.0]) {
                // The parameter a jump gives the result to.
                let near = if fed_to[r.0] == NONE && passed_in[r.0] == NONE {
                    Near::none()
                } else {
                    let fed = (fed_to[r.0] != NONE).then(|| Value(fed_to[r.0]));
                    Near::of(fed.into_iter(), &register, &spilled, passed_in[r.0])
                };
                colour(
                    kinds,
                    &mut holder,
                    &mut register,
                    &mut spilled,
                    &crosses,
                    (*r, kind(f.types[r.0])),
                    None,
                    near.registers(),
                );
            }
            release(&mut holder, &register, &dead[at]);
        }
    }

    let frame_values: Vec<usize> = (0..values)
        .filter(|&v| !homeless[v] && (register[v] == NONE || spilled[v]))
        .collect();
    let (places, frame) = lay_out(frame_values.iter().map(|&v| f.types[v]));
    let mut homes = vec![Home::Frame(0); values];
    for (v, home) in homes.iter_mut().enumerate() {
        if homeless[v] {
            *home = Home::Held;
        } else if register[v] != NONE && !spilled[v] {
            *home = Home::Register(register[v]);
        }
    }
    for (&v, at) in frame_values.iter().zip(places) {
        homes[v] = Home::Frame(at);
    }
    let mut saved: Vec<usize> = homes
        .iter()
        .filter_map(|h| match h {
            Home::Register(r) if kinds.is_preserved(*r) => Some(*r),
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
struct Held<'a> {
    values: Vec<Value>,
    at: Vec<usize>,
    /// The kind of register each value is held in.
    kind: &'a [u8],
    /// How many of the values are of each kind.
    counts: [usize; 2],
}

impl<'a> Held<'a> {
    fn new(kind: &'a [u8], values: usize) -> Held<'a> {
        Held {
            values: Vec::new(),
            at: vec![NONE; values],
            kind,
            counts: [0; 2],
        }
    }

    fn add(&mut self, v: Value) {
        if self.at[v.0] == NONE {
            self.at[v.0] = self.values.len();
            self.values.push(v);
            self.counts[usize::from(self.kind[v.0])] += 1;
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
            self.counts[usize::from(self.kind[v.0])] -= 1;
        }
    }

    fn clear(&mut self) {
        for v in self.values.drain(..) {
            self.at[v.0] = NONE;
        }
        self.counts = [0; 2];
    }
}

/// For each kind of register, whether any value held in one is alive across a
/// call: alive before it and after it, and not read last by it.
fn across_a_call(f: &Function, kinds_of: &[u8], facts: &Facts, homeless: &[bool]) -> [bool; 2] {
    let values = f.types.len();
    let mut crosses = [false; 2];
    let mut alive = Held::new(kinds_of, values);
    // Whether a value is read last at the place being looked at.
    let mut last_here = vec![NONE; values];
    for (b, block) in f.blocks.iter().enumerate() {
        alive.clear();
        for v in facts.live_in[b].iter().chain(&block.params) {
            alive.add(*v);
        }
        for v in &facts.dead[facts.base[b]] {
            alive.remove(*v);
        }
        for (i, instruction) in block.instructions.iter().enumerate() {
            let at = facts.base[b] + i + 1;
            if instruction.op.is_call() {
                for v in &facts.dying[at] {
                    last_here[v.0] = at;
                }
                for v in alive.values.iter().filter(|v| last_here[v.0] != at) {
                    if !is_lost_by_calls(f.types[v.0]) {
                        crosses[usize::from(kinds_of[v.0])] = true;
                    }
                }
            }
            for v in &facts.dying[at] {
                alive.remove(*v);
            }
            for r in instruction.results.iter().filter(|r| !homeless[r.0]) {
                alive.add(*r);
            }
            for v in &facts.dead[at] {
                alive.remove(*v);
            }
        }
    }
    crosses
}

/// Notes, for the values a call or a return passes in order, the number of the
/// register each is passed in, for those that go in one: the k-th of a kind in
/// the k-th register of the kind, while the chip has one.
fn note_passed(f: &Function, kinds: Offer, values: &[Value], passed_in: &mut [usize]) {
    let mut seen = [0usize; 2];
    for v in values {
        let kind = kind(f.types[v.0]);
        let k = seen[kind];
        seen[kind] += 1;
        if k < kinds.of(kind).passed && passed_in[v.0] == NONE {
            passed_in[v.0] = kinds.first(kind) + k;
        }
    }
}

/// For each parameter of the function's first block, the number of the
/// register it arrives in, if it arrives in one.
fn arrivals(f: &Function, kinds: Offer, block: &crate::scalar::Block) -> Vec<Option<usize>> {
    let mut seen = [0usize; 2];
    block
        .params
        .iter()
        .map(|p| {
            let kind = kind(f.types[p.0]);
            let k = seen[kind];
            seen[kind] += 1;
            (k < kinds.of(kind).passed).then(|| kinds.first(kind) + k)
        })
        .collect()
}

/// Sends values to the frame until, at every point, no more are held in
/// registers than the chip offers, and no more are held across a call in
/// registers than a call leaves alone.
fn spill(
    f: &Function,
    kinds: Offer,
    kinds_of: &[u8],
    facts: &Facts,
    spilled: &mut [bool],
    crosses: &mut [bool],
    homeless: &[bool],
) {
    let values = f.types.len();
    let mut held = Held::new(kinds_of, values);
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
            for kind_of in 0..2 {
                if held.counts[kind_of] <= kinds.of(kind_of).count() {
                    continue;
                }
                let of_kind = |held: &Held| -> Vec<Value> {
                    (held.values.iter().copied())
                        .filter(|v| kind(f.types[v.0]) == kind_of)
                        .collect()
                };
                loop {
                    let values = of_kind(held);
                    if values.len() <= kinds.of(kind_of).count() {
                        break;
                    }
                    let v = values[furthest(&values)];
                    spilled[v.0] = true;
                    held.remove(v);
                }
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
                // A vector alive across a call is in the frame.
                for v in across.iter().filter(|v| is_lost_by_calls(f.types[v.0])) {
                    spilled[v.0] = true;
                    held.remove(*v);
                }
                across.retain(|v| !spilled[v.0]);
                for kind_of in 0..2 {
                    let mut of_kind: Vec<Value> = (across.iter().copied())
                        .filter(|v| usize::from(kinds_of[v.0]) == kind_of)
                        .collect();
                    while of_kind.len() > kinds.of(kind_of).preserved {
                        let v = of_kind.swap_remove(furthest(&of_kind));
                        spilled[v.0] = true;
                        held.remove(v);
                    }
                }
            }
            for v in &facts.dying[at] {
                held.remove(*v);
            }
            for r in &instruction.results {
                if !spilled[r.0] && !homeless[r.0] {
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
/// value is given to, are in so far, the one most of them are in first, and
/// then the one a call passes the value in or takes it from, if it has one.
/// A value that moves in or out of the register of the one it is given to or
/// takes is not moved at the jump.
struct Near {
    registers: [usize; Near::MOST],
    count: usize,
}

impl Near {
    const MOST: usize = 5;

    fn of(
        others: impl Iterator<Item = Value>,
        register: &[usize],
        spilled: &[bool],
        passed: usize,
    ) -> Near {
        let mut found = [(NONE, 0usize); Near::MOST];
        let mut kinds = 0;
        for v in others {
            let r = register[v.0];
            if r == NONE || spilled[v.0] {
                continue;
            }
            match found[..kinds].iter_mut().find(|(seen, _)| *seen == r) {
                Some((_, n)) => *n += 1,
                None if kinds < Near::MOST - 1 => {
                    found[kinds] = (r, 1);
                    kinds += 1;
                }
                None => {}
            }
        }
        // The sort keeps the order they were found in among those that tie.
        found[..kinds].sort_by_key(|&(_, n)| std::cmp::Reverse(n));
        let mut near = Near {
            registers: [NONE; Near::MOST],
            count: kinds,
        };
        for (slot, (r, _)) in near.registers.iter_mut().zip(&found[..kinds]) {
            *slot = *r;
        }
        if passed != NONE {
            near.registers[near.count] = passed;
            near.count += 1;
        }
        near
    }

    fn none() -> Near {
        Near {
            registers: [NONE; Near::MOST],
            count: 0,
        }
    }

    fn registers(&self) -> &[usize] {
        &self.registers[..self.count]
    }
}

/// Gives the value the register it arrives in, if it has one and may have it
/// and no value alive holds it, and otherwise the first of the registers
/// `near` that it may have and no value alive holds, and otherwise the first
/// register that no value alive holds, from those it may have, or sends it to
/// the frame when there is none.
#[allow(clippy::too_many_arguments)]
fn colour(
    kinds: Offer,
    holder: &mut [usize],
    register: &mut [usize],
    spilled: &mut [bool],
    crosses: &[bool],
    (v, of_kind): (Value, usize),
    arrives: Option<usize>,
    near: &[usize],
) {
    if spilled[v.0] {
        return;
    }
    let offered = kinds.of(of_kind);
    let first = kinds.first(of_kind);
    let from = first + if crosses[v.0] { offered.changed } else { 0 };
    let to = first + offered.count();
    let free = |r: usize| r >= from && r < to && holder[r] == NONE;
    let wanted = arrives
        .filter(|&r| free(r))
        .or_else(|| near.iter().copied().find(|&r| free(r)));
    match wanted.or_else(|| (from..to).find(|&r| holder[r] == NONE)) {
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

/// Whether a block that the first reaches can be reached again from itself:
/// a walk from the first block comes to a jump to a block it is still inside.
fn has_a_loop(blocks: &[crate::scalar::Block]) -> bool {
    // 0 not yet reached, 1 inside the walk, 2 left.
    let mut state = vec![0u8; blocks.len()];
    state[0] = 1;
    let mut stack = vec![(0, 0)];
    while let Some((b, i)) = stack.last_mut() {
        if let Some(s) = blocks[*b].end.jumps().get(*i).map(|j| j.block) {
            *i += 1;
            match state[s] {
                1 => return true,
                0 => {
                    state[s] = 1;
                    stack.push((s, 0));
                }
                _ => {}
            }
        } else {
            state[*b] = 2;
            stack.pop();
        }
    }
    false
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
        let mut stack = vec![(root, 0)];
        while let Some((b, i)) = stack.last_mut() {
            if let Some(s) = blocks[*b].end.jumps().get(*i).map(|j| j.block) {
                *i += 1;
                if !seen[s] {
                    seen[s] = true;
                    stack.push((s, 0));
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
