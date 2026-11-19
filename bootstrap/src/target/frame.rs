//! How a function's frame is laid out, which every target says the same way:
//! each value that is in the frame at the size of its type, at a place that
//! is a multiple of that size, and where a call's arguments and results go.

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

/// Where what a call to the function passes in the frame is, from the start
/// of the area, and how many bytes the area takes. The first `passed`
/// parameters, and the first `passed` results, go in registers, as the chip
/// says, so the area holds the parameters after them, and then the results
/// after those, each at its size, so that writing a result never overwrites a
/// parameter still to be read. It is in the caller's frame, just above the
/// bytes the system may use.
pub struct CallArea {
    /// The place of each parameter after the first `passed`.
    pub params: Vec<u32>,
    /// The place of each result after the first `passed`.
    pub results: Vec<u32>,
    pub size: u32,
}

pub fn call_area(f: &Function, passed: usize) -> CallArea {
    let params = f.blocks[0].params.iter().skip(passed).map(|v| f.types[v.0]);
    let results = f.results.iter().skip(passed).copied();
    let count = f.blocks[0].params.len().saturating_sub(passed);
    let (places, size) = lay_out(params.chain(results));
    CallArea {
        params: places[..count].to_vec(),
        results: places[count..].to_vec(),
        size,
    }
}

/// The bytes a function keeps at the bottom of its frame for the calls it
/// makes: the most any one takes, as `call_area` says, rounded up to 8.
pub fn outgoing(program: &Program, f: &Function, passed: usize) -> u32 {
    let mut outgoing = 0;
    for instruction in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = &instruction.op {
            outgoing = outgoing.max(call_area(&program.functions[*function], passed).size);
        }
    }
    round_up(outgoing, 8)
}
