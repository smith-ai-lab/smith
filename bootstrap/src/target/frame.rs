//! How a function's frame is laid out, which every target says the same way:
//! each value that is in the frame at the size of its type, at a place that
//! is a multiple of that size, and where a call's arguments and results go.

use crate::scalar::{Bits, Function, Op, Program, Type};

/// How many bytes a value of the type takes, and so where it is a multiple
/// of.
pub fn size(ty: Type) -> u32 {
    match ty {
        Type::Int(Bits::B8) => 1,
        Type::Int(Bits::B16) => 2,
        Type::Int(Bits::B32) => 4,
        Type::Int(Bits::B64) | Type::Address => 8,
    }
}

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

/// The places of what a call to the function passes: its parameters, and
/// after them what it gives back, so that writing a result never overwrites
/// a parameter still to be read. It is in the caller's frame, just above the
/// bytes the system may use.
pub fn call_area(f: &Function) -> (Vec<u32>, u32) {
    let params = f.blocks[0].params.iter().map(|v| f.types[v.0]);
    lay_out(params.chain(f.results.iter().copied()))
}

/// The bytes a function keeps at the bottom of its frame for the calls it
/// makes: the most any one takes, as `call_area` says, rounded up to 8.
pub fn outgoing(program: &Program, f: &Function) -> u32 {
    let mut outgoing = 0;
    for instruction in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = &instruction.op {
            outgoing = outgoing.max(call_area(&program.functions[*function]).1);
        }
    }
    round_up(outgoing, 8)
}
