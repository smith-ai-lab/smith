//! The third pass over the scalar form: an instruction whose inputs are all
//! known is replaced by its answer.
//!
//! One walk over each function, each block after the blocks that jump to it
//! except by a jump back. A value is known when it is a constant, or a
//! parameter that every jump into its block gives the same constant, or a
//! choice made on a known value. The answers are worked out by the functions
//! the evaluator uses, so the two cannot disagree. A block that ends by
//! choosing between blocks on a known value ends with a jump, and the blocks
//! only it reached are left for `remove`. A block that ends by choosing on a
//! comparison with its bit flipped chooses on the comparison, the other way
//! round, so `a <= b` is the one comparison `b < a` that its branch reads.

use std::collections::{HashMap, HashSet};

use super::eval::{
    binary, bits_of, compare, float_binary, float_bits_of, float_compare, float_from_int,
    float_fused, float_mask, float_negate, float_resize, int_from_float, mask, narrow, widen,
};
use super::graph::{self, jumps};
use super::vector::{
    lane_of, lane_type, vector_binary, vector_choose, vector_compare, vector_first_set,
    vector_float_binary, vector_float_compare, vector_float_fused, vector_splat, vector_sum,
    with_lane,
};
use super::{Binary, Constant, End, Function, Instruction, Lane, Op, Place, Program, Type, Value};

/// The program with every instruction whose inputs are all constants
/// replaced by a constant, and every choice on a constant made.
pub fn fold(program: Program) -> Program {
    Program {
        data: program.data,
        functions: program.functions.into_iter().map(folded).collect(),
        main: program.main,
    }
}

fn folded(mut f: Function) -> Function {
    // A value that a parameter only copies is the value it copies.
    let copy = graph::copies(&f);
    graph::rename(&mut f, &|v| copy[v.0]);
    let order = graph::reverse_postorder(&f);
    let n = f.blocks.len();
    let mut at = vec![usize::MAX; n];
    for (place, &b) in order.iter().enumerate() {
        at[b] = place;
    }
    // Whether a block is jumped to from a block that is not before it.
    let mut back = vec![false; n];
    for &b in &order {
        for j in jumps(&f.blocks[b].end) {
            back[j.block] |= at[j.block] <= at[b];
        }
    }
    let mut known: Vec<Option<u64>> = vec![None; f.types.len()];
    // The bits of each value that is a vector of constants, which a function with
    // no vector never fills.
    let mut vector_known: Known = Known(HashMap::new());
    // What each value is read as from here on, once it is known to be
    // another value.
    let mut sub: Vec<Value> = (0..f.types.len()).map(Value).collect();
    // The arguments of each jump into a block from a block already walked,
    // which a switch on a known value makes only for the arm it takes.
    let mut into: Vec<Vec<Vec<Value>>> = vec![Vec::new(); n];
    let mut walked = vec![false; n];
    // The comparisons, and the values that are one of them with its bit
    // flipped, by what they flip.
    let mut compares: HashSet<Value> = HashSet::new();
    let mut flips: HashMap<Value, Value> = HashMap::new();
    for &b in &order {
        if b != 0 && into[b].is_empty() {
            continue;
        }
        walked[b] = true;
        let mut block = std::mem::replace(&mut f.blocks[b], empty());
        let mut instructions = Vec::with_capacity(block.instructions.len());
        if b != 0 && !back[b] {
            for (k, &p) in block.params.iter().enumerate() {
                let Type::Int(_) = f.types[p.0] else { continue };
                let first = into[b][0][k];
                let Some(c) = known[first.0] else { continue };
                if into[b].iter().all(|args| known[args[k].0] == Some(c)) {
                    let q = Value(f.types.len());
                    f.types.push(f.types[p.0]);
                    known.push(Some(c));
                    sub.push(q);
                    sub[p.0] = q;
                    instructions.push(Instruction {
                        results: vec![q],
                        op: Op::Constant(Constant::Int(c)),
                        at: Place::NONE,
                    });
                }
            }
        }
        for instruction in std::mem::take(&mut block.instructions) {
            let op = graph_renamed(&instruction.op, &sub);
            let [result] = instruction.results[..] else {
                instructions.push(Instruction {
                    results: instruction.results,
                    op,
                    at: instruction.at,
                });
                continue;
            };
            let ty = f.types[result.0];
            // A vector is worked out as a float is: only an instruction whose
            // inputs are all constants is replaced, by the bits that the code
            // of the evaluator gives.
            let vector_answer: Option<u128> = match &op {
                Op::Constant(c @ Constant::Vector(..)) => {
                    vector_known.0.extend(c.vector_bits().map(|n| (result, n)));
                    None
                }
                Op::VectorBinary { op, a, b } => match (vector_known.of(a), vector_known.of(b)) {
                    (Some(x), Some(y)) => {
                        let Lane::Int(bits) = lane_type(ty) else {
                            unreachable!("lanes that are not integers")
                        };
                        Some(vector_binary(*op, bits, x, y))
                    }
                    _ => None,
                },
                Op::VectorFloatFused { op, a, b, c } => {
                    match (vector_known.of(a), vector_known.of(b), vector_known.of(c)) {
                        (Some(x), Some(y), Some(z)) => {
                            let Lane::Float(bits) = lane_type(ty) else {
                                unreachable!("lanes that are not floats")
                            };
                            Some(vector_float_fused(*op, bits, x, y, z))
                        }
                        _ => None,
                    }
                }
                Op::VectorFloatBinary { op, a, b } => {
                    match (vector_known.of(a), vector_known.of(b)) {
                        (Some(x), Some(y)) => {
                            let Lane::Float(bits) = lane_type(ty) else {
                                unreachable!("lanes that are not floats")
                            };
                            Some(vector_float_binary(*op, bits, x, y))
                        }
                        _ => None,
                    }
                }
                Op::VectorCompare { op, signed, a, b } => {
                    match (vector_known.of(a), vector_known.of(b)) {
                        (Some(x), Some(y)) => {
                            let Lane::Int(bits) = lane_type(f.types[a.0]) else {
                                unreachable!("lanes that are not integers")
                            };
                            Some(vector_compare(*op, *signed, bits, x, y))
                        }
                        _ => None,
                    }
                }
                Op::VectorFloatCompare { op, a, b } => {
                    match (vector_known.of(a), vector_known.of(b)) {
                        (Some(x), Some(y)) => {
                            let Lane::Float(bits) = lane_type(f.types[a.0]) else {
                                unreachable!("lanes that are not floats")
                            };
                            Some(vector_float_compare(*op, bits, x, y))
                        }
                        _ => None,
                    }
                }
                Op::VectorChoose {
                    mask,
                    then,
                    otherwise,
                } => match (
                    vector_known.of(mask),
                    vector_known.of(then),
                    vector_known.of(otherwise),
                ) {
                    (Some(m), Some(t), Some(o)) => Some(vector_choose(m, t, o)),
                    _ => None,
                },
                Op::VectorSplat(value) => known[value.0].map(|n| vector_splat(lane_type(ty), n)),
                Op::VectorInsert {
                    vector,
                    lane,
                    value,
                } => match (vector_known.of(vector), known[value.0]) {
                    (Some(v), Some(n)) => Some(with_lane(lane_type(ty), v, u32::from(*lane), n)),
                    _ => None,
                },
                _ => None,
            };
            if let Some(n) = vector_answer {
                vector_known.0.insert(result, n);
                instructions.push(Instruction {
                    results: vec![result],
                    op: Op::Constant(Constant::vector(n)),
                    at: instruction.at,
                });
                continue;
            }
            let answer = match &op {
                Op::VectorExtract { vector, lane } => vector_known
                    .of(vector)
                    .map(|v| lane_of(lane_type(f.types[vector.0]), v, u32::from(*lane))),
                Op::VectorSum(vector) => vector_known
                    .of(vector)
                    .map(|v| vector_sum(lane_type(f.types[vector.0]), v)),
                Op::VectorFirstSet(mask) => vector_known
                    .of(mask)
                    .map(|v| vector_first_set(lane_type(f.types[mask.0]), v)),
                Op::Constant(Constant::Int(n)) => {
                    known[result.0] = Some(n & mask(bits_of(ty)));
                    None
                }
                Op::Constant(Constant::Float(n)) => {
                    known[result.0] = Some(n & float_mask(float_bits_of(ty)));
                    None
                }
                // A float is worked out by the code the evaluator uses, with
                // nothing rewritten: only an instruction whose inputs are all
                // constants is replaced, by the bits the evaluator gives.
                Op::FloatBinary { op, a, b } => match (known[a.0], known[b.0]) {
                    (Some(x), Some(y)) => Some(float_binary(*op, float_bits_of(ty), x, y)),
                    _ => None,
                },
                Op::FloatFused { op, a, b, c } => match (known[a.0], known[b.0], known[c.0]) {
                    (Some(x), Some(y), Some(z)) => {
                        Some(float_fused(*op, float_bits_of(ty), x, y, z))
                    }
                    _ => None,
                },
                Op::FloatNegate(value) => {
                    known[value.0].map(|x| float_negate(float_bits_of(ty), x))
                }
                Op::FloatCompare { op, a, b } => match (known[a.0], known[b.0]) {
                    (Some(x), Some(y)) => {
                        Some(float_compare(*op, float_bits_of(f.types[a.0]), x, y))
                    }
                    _ => None,
                },
                Op::FloatFromInt { signed, value } => known[value.0].map(|n| {
                    float_from_int(*signed, bits_of(f.types[value.0]), float_bits_of(ty), n)
                }),
                Op::IntFromFloat { signed, value } => known[value.0].map(|x| {
                    int_from_float(*signed, float_bits_of(f.types[value.0]), bits_of(ty), x)
                }),
                Op::FloatResize(value) => known[value.0]
                    .map(|x| float_resize(float_bits_of(f.types[value.0]), float_bits_of(ty), x)),
                Op::Binary { op, a, b } => {
                    // An operation that leaves its first value as it was.
                    let leaves = matches!(
                        (op, known[b.0]),
                        (
                            Binary::Add
                                | Binary::Sub
                                | Binary::Or
                                | Binary::Xor
                                | Binary::ShiftLeft
                                | Binary::ShiftRightU
                                | Binary::ShiftRightS,
                            Some(0)
                        ) | (Binary::Mul, Some(1))
                    );
                    if leaves {
                        sub[result.0] = *a;
                        known[result.0] = known[a.0];
                        continue;
                    }
                    match (known[a.0], known[b.0]) {
                        (Some(x), Some(y)) if matches!(ty, Type::Int(_)) => {
                            Some(binary(*op, bits_of(ty), x, y))
                        }
                        _ => None,
                    }
                }
                Op::Compare { op, a, b } => match (known[a.0], known[b.0]) {
                    (Some(x), Some(y)) => Some(compare(*op, bits_of(f.types[a.0]), x, y)),
                    _ => None,
                },
                Op::Widen { signed, value } => known[value.0]
                    .map(|n| widen(*signed, bits_of(f.types[value.0]), bits_of(ty), n)),
                Op::Narrow(value) => known[value.0].map(|n| narrow(bits_of(ty), n)),
                Op::Choose {
                    on,
                    then,
                    otherwise,
                } => {
                    if let Some(chosen) = known[on.0] {
                        let value = if chosen != 0 { *then } else { *otherwise };
                        sub[result.0] = value;
                        known[result.0] = known[value.0];
                        continue;
                    }
                    None
                }
                _ => None,
            };
            match answer {
                Some(n) => {
                    known[result.0] = Some(n);
                    instructions.push(Instruction {
                        results: vec![result],
                        op: Op::Constant(match ty {
                            Type::Float(_) => Constant::Float(n),
                            _ => Constant::Int(n),
                        }),
                        at: instruction.at,
                    });
                }
                None => {
                    match &op {
                        Op::Compare { .. } => {
                            compares.insert(result);
                        }
                        Op::Binary {
                            op: Binary::Xor,
                            a,
                            b,
                        } if known[b.0] == Some(1) && compares.contains(a) => {
                            flips.insert(result, *a);
                        }
                        _ => {}
                    }
                    instructions.push(Instruction {
                        results: vec![result],
                        op,
                        at: instruction.at,
                    });
                }
            }
        }
        graph::rename_end(&mut block.end, &|v| sub[v.0]);
        if let End::Switch { on, jumps } = &block.end
            && let Some(n) = known[on.0]
        {
            block.end = End::Jump(jumps[n as usize].clone());
        } else if let End::Switch { on, jumps } = &mut block.end
            && let Some(&compare) = flips.get(on)
            && jumps.len() == 2
        {
            // A choice on a comparison with its bit flipped is a choice on
            // the comparison, with the blocks the other way round.
            *on = compare;
            jumps.swap(0, 1);
        }
        for j in jumps(&block.end) {
            if at[j.block] > at[b] {
                into[j.block].push(j.args.clone());
            }
        }
        block.instructions = instructions;
        f.blocks[b] = block;
    }
    // What nothing reaches is not walked, but it reads the same values.
    for (b, block) in f.blocks.iter_mut().enumerate() {
        if !walked[b] {
            for i in &mut block.instructions {
                i.op = graph_renamed(&i.op, &sub);
            }
            graph::rename_end(&mut block.end, &|v| sub[v.0]);
        }
    }
    f
}

/// The bits of the values that are vectors of constants.
struct Known(HashMap<Value, u128>);

impl Known {
    fn of(&self, v: &Value) -> Option<u128> {
        self.0.get(v).copied()
    }
}

fn graph_renamed(op: &Op, sub: &[Value]) -> Op {
    super::inline::renamed(op, &|v| sub[v.0])
}

fn empty() -> super::Block {
    super::Block {
        params: Vec::new(),
        instructions: Vec::new(),
        end: End::Return(Vec::new()),
    }
}
