//! The third pass over the scalar form: an instruction whose inputs are all
//! known is replaced by its answer.
//!
//! One walk over each function, each block after the blocks that jump to it
//! except by a jump back. A value is known when it is a constant, or a
//! parameter that every jump into its block gives the same constant, or a
//! choice made on a known value. The answers are worked out by the functions
//! the evaluator uses, so the two cannot disagree. A block that ends by
//! choosing between blocks on a known value ends with a jump, and the blocks
//! only it reached are left for `remove`.

use super::eval::{binary, bits_of, compare, mask, narrow, widen};
use super::graph::{self, jumps};
use super::{Binary, Constant, End, Function, Instruction, Op, Program, Type, Value};

/// The program with every instruction whose inputs are all constants
/// replaced by a constant, and every choice on a constant made.
pub fn fold(program: &Program) -> Program {
    Program {
        data: program.data.clone(),
        functions: program.functions.iter().map(folded).collect(),
        main: program.main,
    }
}

fn folded(f: &Function) -> Function {
    let mut f = f.clone();
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
    // What each value is read as from here on, once it is known to be
    // another value.
    let mut sub: Vec<Value> = (0..f.types.len()).map(Value).collect();
    // The arguments of each jump into a block from a block already walked,
    // which a switch on a known value makes only for the arm it takes.
    let mut into: Vec<Vec<Vec<Value>>> = vec![Vec::new(); n];
    let mut walked = vec![false; n];
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
                });
                continue;
            };
            let ty = f.types[result.0];
            let answer = match &op {
                Op::Constant(Constant::Int(n)) => {
                    known[result.0] = Some(n & mask(bits_of(ty)));
                    None
                }
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
                        op: Op::Constant(Constant::Int(n)),
                    });
                }
                None => instructions.push(Instruction {
                    results: vec![result],
                    op,
                }),
            }
        }
        graph::rename_end(&mut block.end, &|v| sub[v.0]);
        if let End::Switch { on, jumps } = &block.end
            && let Some(n) = known[on.0]
        {
            block.end = End::Jump(jumps[n as usize].clone());
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
