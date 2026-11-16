//! Where a function's values are held, which every target says the same way:
//! each value at the size of its type, at a place that is a multiple of that
//! size.

use crate::scalar::{Bits, Function, Op, Program, Type, Value};

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

/// What a function's frame holds, apart from the bytes the system may use
/// and what a caller passes it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Frame {
    /// The bytes for the calls the function makes: the most any one takes,
    /// as `call_area` says.
    pub outgoing: u32,
    /// The bytes its values take: each value but its parameters, in the
    /// order of their numbers, at the next multiple of its size.
    pub held: u32,
    /// Where each value is in those bytes, or `None` for a parameter, which
    /// is passed.
    pub places: Vec<Option<u32>>,
}

/// The frame of each function of the program.
pub fn frames(program: &Program) -> Vec<Frame> {
    program
        .functions
        .iter()
        .map(|f| frame(program, f))
        .collect()
}

pub fn frame(program: &Program, f: &Function) -> Frame {
    let passed = &f.blocks[0].params;
    let held: Vec<usize> = (0..f.types.len())
        .filter(|v| !passed.contains(&Value(*v)))
        .collect();
    let (at, end) = lay_out(held.iter().map(|v| f.types[*v]));
    let mut places = vec![None; f.types.len()];
    for (v, at) in held.into_iter().zip(at) {
        places[v] = Some(at);
    }
    let mut outgoing = 0;
    for instruction in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = &instruction.op {
            outgoing = outgoing.max(call_area(&program.functions[*function]).1);
        }
    }
    Frame {
        outgoing: round_up(outgoing, 8),
        held: end,
        places,
    }
}
