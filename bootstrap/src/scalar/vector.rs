//! What each instruction of vectors gives, worked out from the scalar
//! instruction of each lane. The evaluator and the pass that folds constants
//! both use it, so the two cannot disagree. A vector is held as the bits of
//! the whole, lane 0 in the lowest.

use super::eval::{binary, compare, float_binary, float_compare, mask};
use super::{Binary, Bits, Compare, FloatBinary, FloatBits, FloatCompare, Lane, Relation, Type};

/// The bits of a lane of the lane's size.
pub(super) fn lane_mask(lane: Lane) -> u128 {
    match lane.bits() {
        128 => u128::MAX,
        n => (1u128 << n) - 1,
    }
}

/// The lane at the place, as bits of its size.
pub(super) fn lane_of(lane: Lane, vector: u128, at: u32) -> u64 {
    ((vector >> (at * lane.bits())) & lane_mask(lane)) as u64
}

/// The vector with the lane at the place replaced by the bits.
pub(super) fn with_lane(lane: Lane, vector: u128, at: u32, bits: u64) -> u128 {
    let shift = at * lane.bits();
    let cleared = vector & !(lane_mask(lane) << shift);
    cleared | ((u128::from(bits) & lane_mask(lane)) << shift)
}

/// The vector whose lane at each place is what `each` gives for the place.
fn made(lane: Lane, each: impl Fn(u32) -> u64) -> u128 {
    (0..lane.count()).fold(0, |v, at| with_lane(lane, v, at, each(at)))
}

/// The vector of what `op` gives for each pair of lanes of two vectors of
/// integers of the lane's size.
pub(super) fn vector_binary(op: Binary, bits: Bits, a: u128, b: u128) -> u128 {
    let lane = Lane::Int(bits);
    made(lane, |at| {
        binary(op, bits, lane_of(lane, a, at), lane_of(lane, b, at))
    })
}

/// The vector of what `op` gives for each pair of lanes of two vectors of
/// floats of the lane's size.
pub(super) fn vector_float_binary(op: FloatBinary, bits: FloatBits, a: u128, b: u128) -> u128 {
    let lane = Lane::Float(bits);
    made(lane, |at| {
        float_binary(op, bits, lane_of(lane, a, at), lane_of(lane, b, at))
    })
}

/// Whether two integers of the size are as the relation says.
fn related(op: Relation, signed: bool, bits: Bits, x: u64, y: u64) -> bool {
    let less = if signed {
        Compare::LessS
    } else {
        Compare::LessU
    };
    let below = |p: u64, q: u64| compare(less, bits, p, q) == 1;
    match op {
        Relation::Equal => compare(Compare::Equal, bits, x, y) == 1,
        Relation::NotEqual => compare(Compare::Equal, bits, x, y) == 0,
        Relation::Less => below(x, y),
        Relation::LessEqual => !below(y, x),
        Relation::Greater => below(y, x),
        Relation::GreaterEqual => !below(x, y),
    }
}

/// The mask of the relation over each pair of lanes of two vectors of integers.
pub(super) fn vector_compare(op: Relation, signed: bool, bits: Bits, a: u128, b: u128) -> u128 {
    let lane = Lane::Int(bits);
    made(lane, |at| {
        let holds = related(op, signed, bits, lane_of(lane, a, at), lane_of(lane, b, at));
        if holds { mask(bits) } else { 0 }
    })
}

/// The mask of the comparison over each pair of lanes of two vectors of floats,
/// whose lanes are as wide as the floats.
pub(super) fn vector_float_compare(op: FloatCompare, bits: FloatBits, a: u128, b: u128) -> u128 {
    let lane = Lane::Float(bits);
    made(lane, |at| {
        let holds = float_compare(op, bits, lane_of(lane, a, at), lane_of(lane, b, at)) == 1;
        if holds { (lane_mask(lane)) as u64 } else { 0 }
    })
}

/// Each bit from `then` where the mask has a 1, and from `otherwise` where it
/// has a 0.
pub(super) fn vector_choose(mask: u128, then: u128, otherwise: u128) -> u128 {
    (then & mask) | (otherwise & !mask)
}

/// The vector whose lanes are all the bits.
pub(super) fn vector_splat(lane: Lane, bits: u64) -> u128 {
    made(lane, |_| bits)
}

/// The sum of the lanes: integers wrap, and floats are added in pairs of
/// lanes that lie next to each other, and then in pairs of the sums.
pub(super) fn vector_sum(lane: Lane, vector: u128) -> u64 {
    let mut sums: Vec<u64> = (0..lane.count())
        .map(|at| lane_of(lane, vector, at))
        .collect();
    while sums.len() > 1 {
        sums = sums
            .chunks(2)
            .map(|pair| match lane {
                Lane::Int(bits) => binary(Binary::Add, bits, pair[0], pair[1]),
                Lane::Float(bits) => float_binary(FloatBinary::Add, bits, pair[0], pair[1]),
            })
            .collect();
    }
    sums[0]
}

/// The place of the first lane of the mask whose top bit is 1, and the number
/// of lanes when none has.
pub(super) fn vector_first_set(lane: Lane, mask: u128) -> u64 {
    (0..lane.count())
        .find(|&at| lane_of(lane, mask, at) >> (lane.bits() - 1) == 1)
        .unwrap_or(lane.count()) as u64
}

/// The lane of a vector type, which panics for any other.
pub(super) fn lane_type(ty: Type) -> Lane {
    match ty {
        Type::Vector(lane) => lane,
        _ => panic!("what is not a vector where a vector is needed"),
    }
}
