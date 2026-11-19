//! How a function's frame is laid out, which every target says the same way:
//! each value that is in the frame at the size of its type, at a place that
//! is a multiple of that size, and where a call's arguments and results go.

use super::alloc;
use crate::scalar::{Bits, FloatBits, Function, Op, Program, Type};

/// How many bytes a value of the type takes, and so where it is a multiple
/// of.
pub fn size(ty: Type) -> u32 {
    match ty {
        Type::Int(Bits::B8) => 1,
        Type::Int(Bits::B16) => 2,
        Type::Int(Bits::B32) => 4,
        Type::Float(FloatBits::F32) => 4,
        Type::Int(Bits::B64) | Type::Float(FloatBits::F64) | Type::Address => 8,
        Type::Vector(_) => 16,
    }
}

/// The bytes of a page of memory, which a system that gives a program its
/// stack one page at a time grows it by.
pub const PAGE: u32 = 4096;

pub fn round_up(n: u32, to: u32) -> u32 {
    n.div_ceil(to) * to
}

/// Where values of the types go one after another, each at a multiple of its
/// size, and where they end.
pub fn lay_out(types: impl Iterator<Item = Type>) -> (Vec<u32>, u32) {
    let mut end = 0;
    let places = types
        .map(|ty| {
            let at = round_up(end, size(ty));
            end = at + size(ty);
            at
        })
        .collect();
    (places, end)
}

/// Where a parameter or a result of a function is passed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Passed {
    /// In a register: the k-th register of the kind of the value, for the k-th
    /// value of its kind that is passed.
    Register(usize),
    /// In the area of the caller's frame, at this place from its start.
    Frame(u32),
}

/// How the parameters and the results of a function are passed. The first
/// `passed[kind]` parameters of each kind of value, and the first
/// `passed[kind]` results of each, go in registers, as the chip says, which
/// kinds are those of `alloc::kind`. The area holds the parameters after them,
/// and then the results after those, each at its size, so that writing a result
/// never overwrites a parameter still to be read. It is in the caller's frame,
/// just above the bytes the system may use.
pub struct CallArea {
    /// How each parameter is passed.
    pub params: Vec<Passed>,
    /// How each result is passed.
    pub results: Vec<Passed>,
}

pub fn call_area(f: &Function, passed: [usize; 2]) -> CallArea {
    let mut params = Vec::with_capacity(f.blocks[0].params.len());
    let mut results = Vec::with_capacity(f.results.len());
    lay_out_call(f, passed, |is_result, place| {
        if is_result {
            results.push(place);
        } else {
            params.push(place);
        }
    });
    CallArea { params, results }
}

/// How each parameter and each result of the function is passed, in order, which
/// `place` is told of, and how many bytes the area takes.
fn lay_out_call(f: &Function, passed: [usize; 2], mut place: impl FnMut(bool, Passed)) -> u32 {
    let mut end = 0;
    let mut pass = |seen: &mut [usize; 2], ty: Type| -> Passed {
        let kind = alloc::kind(ty);
        let k = seen[kind];
        seen[kind] += 1;
        if k < passed[kind] {
            Passed::Register(k)
        } else {
            let at = round_up(end, size(ty));
            end = at + size(ty);
            Passed::Frame(at)
        }
    };
    let mut seen = [0; 2];
    for v in &f.blocks[0].params {
        place(false, pass(&mut seen, f.types[v.0]));
    }
    let mut seen = [0; 2];
    for ty in &f.results {
        place(true, pass(&mut seen, *ty));
    }
    end
}

/// The bytes a function keeps at the bottom of its frame for the calls it
/// makes: the most any one takes, as `call_area` says, rounded up to 8.
pub fn outgoing(program: &Program, f: &Function, passed: [usize; 2]) -> u32 {
    let mut outgoing = 0;
    for instruction in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = &instruction.op {
            let size = lay_out_call(&program.functions[*function], passed, |_, _| {});
            outgoing = outgoing.max(size);
        }
    }
    round_up(outgoing, 8)
}
