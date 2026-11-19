//! The first pass over the scalar form: a call of a small function becomes
//! the function's own instructions.
//!
//! A call is replaced when the function is called from one place only, or
//! when the function is no larger than `BOUND`, or when it calls nothing and
//! is no larger than `LEAF_BOUND`, and never when the function is cold.
//! Nothing measured from a run is used. Functions are taken callees first, so each is already as small
//! as it will be when a call of it is looked at, and a copy of it is never
//! looked at again. The pass is one walk over the program it gives, and
//! gives the same program every time.

use super::{Block, End, Function, Instruction, Jump, Op, Program, Value};

/// The most a function may hold, as `size` counts, for a call of it to be
/// replaced when it is called from more than one place.
pub const BOUND: usize = 4;

/// The most a function that calls nothing may hold, as `size` counts, for a
/// call of it to be replaced. A copy of it holds no call, so what the later
/// stages make of the copy does not grow with the calls it makes.
pub const LEAF_BOUND: usize = 40;

/// How large a function is: its instructions, and one for how each block
/// ends.
pub fn size(f: &Function) -> usize {
    f.blocks.iter().map(|b| b.instructions.len() + 1).sum()
}

/// The program with every call that the rule replaces replaced, and every
/// function that nothing calls left out.
pub fn inline(program: &Program) -> Program {
    let order = callees_first(program);
    let mut sites = vec![0usize; program.functions.len()];
    for &f in &order {
        for callee in calls(&program.functions[f]) {
            sites[callee] += 1;
        }
    }
    let mut done: Vec<Option<Function>> = vec![None; program.functions.len()];
    for &f in &order {
        let function = inlined(&program.functions[f], &done, &sites);
        done[f] = Some(function);
    }
    // What is left is what `main` still calls, in the order of the program.
    let mut kept = vec![false; program.functions.len()];
    kept[program.main] = true;
    let mut stack = vec![program.main];
    while let Some(f) = stack.pop() {
        for callee in calls(done[f].as_ref().unwrap()) {
            if !kept[callee] {
                kept[callee] = true;
                stack.push(callee);
            }
        }
    }
    let mut place = vec![0usize; kept.len()];
    let mut count = 0;
    for (f, keep) in kept.iter().enumerate() {
        place[f] = count;
        count += usize::from(*keep);
    }
    let functions = (done.into_iter().enumerate())
        .filter(|(f, _)| kept[*f])
        .map(|(_, f)| {
            let mut f = f.unwrap();
            for block in &mut f.blocks {
                for i in &mut block.instructions {
                    if let Op::Call { function, .. } = &mut i.op {
                        *function = place[*function];
                    }
                }
            }
            f
        })
        .collect();
    Program {
        data: program.data.clone(),
        functions,
        main: place[program.main],
    }
}

/// The functions `main` reaches, each after the functions it calls.
fn callees_first(program: &Program) -> Vec<usize> {
    let mut seen = vec![false; program.functions.len()];
    let mut order = Vec::new();
    seen[program.main] = true;
    let mut stack = vec![(program.main, 0, calls(&program.functions[program.main]))];
    while let Some((f, next, callees)) = stack.last_mut() {
        if let Some(&callee) = callees.get(*next) {
            *next += 1;
            if !seen[callee] {
                seen[callee] = true;
                let inner = calls(&program.functions[callee]);
                stack.push((callee, 0, inner));
            }
        } else {
            order.push(*f);
            stack.pop();
        }
    }
    order
}

/// Whether a call of the function is replaced when it is called from more
/// than one place.
fn is_small(f: &Function) -> bool {
    let size = size(f);
    size <= BOUND || (size <= LEAF_BOUND && calls(f).is_empty())
}

/// The function of each call in the function, in order.
pub(super) fn calls(f: &Function) -> Vec<usize> {
    (f.blocks.iter())
        .flat_map(|b| &b.instructions)
        .filter_map(|i| match i.op {
            Op::Call { function, .. } => Some(function),
            _ => None,
        })
        .collect()
}

/// The function with each call that the rule replaces replaced by the
/// callee's blocks. A block that holds such a call is cut where the call
/// was: what came before jumps to a copy of the callee's blocks, which
/// return by jumping to a block that takes the call's results and holds
/// what came after.
fn inlined(f: &Function, done: &[Option<Function>], sites: &[usize]) -> Function {
    let mut types = f.types.clone();
    let mut blocks: Vec<Block> = Vec::new();
    // Where each block of `f` starts in `blocks`, and which blocks end as a
    // block of `f` does, with jumps to blocks of `f`.
    let mut first = Vec::with_capacity(f.blocks.len());
    let mut original_ends = Vec::with_capacity(f.blocks.len());
    for block in &f.blocks {
        let mut at = blocks.len();
        first.push(at);
        blocks.push(Block {
            params: block.params.clone(),
            instructions: Vec::new(),
            end: End::Return(Vec::new()),
        });
        for instruction in &block.instructions {
            let Op::Call { function, args } = &instruction.op else {
                blocks[at].instructions.push(instruction.clone());
                continue;
            };
            let callee = done[*function].as_ref().unwrap();
            if callee.cold || (sites[*function] != 1 && !is_small(callee)) {
                blocks[at].instructions.push(instruction.clone());
                continue;
            }
            let base = types.len();
            types.extend_from_slice(&callee.types);
            let rename = |v: Value| Value(v.0 + base);
            let entry = blocks.len();
            let after = entry + callee.blocks.len();
            blocks[at].end = End::Jump(Jump {
                block: entry,
                args: args.clone(),
            });
            for block in &callee.blocks {
                let to = |j: &Jump| Jump {
                    block: j.block + entry,
                    args: j.args.iter().map(|v| rename(*v)).collect(),
                };
                let end = match &block.end {
                    End::Jump(j) => End::Jump(to(j)),
                    End::Switch { on, jumps } => End::Switch {
                        on: rename(*on),
                        jumps: jumps.iter().map(to).collect(),
                    },
                    End::Return(values) => End::Jump(Jump {
                        block: after,
                        args: values.iter().map(|v| rename(*v)).collect(),
                    }),
                };
                blocks.push(Block {
                    params: block.params.iter().map(|v| rename(*v)).collect(),
                    instructions: (block.instructions.iter())
                        .map(|i| Instruction {
                            results: i.results.iter().map(|v| rename(*v)).collect(),
                            op: renamed(&i.op, &rename),
                        })
                        .collect(),
                    end,
                });
            }
            at = blocks.len();
            blocks.push(Block {
                params: instruction.results.clone(),
                instructions: Vec::new(),
                end: End::Return(Vec::new()),
            });
        }
        blocks[at].end = block.end.clone();
        original_ends.push(at);
    }
    for at in original_ends {
        let to = |j: &mut Jump| j.block = first[j.block];
        match &mut blocks[at].end {
            End::Jump(j) => to(j),
            End::Switch { jumps, .. } => jumps.iter_mut().for_each(to),
            End::Return(_) => {}
        }
    }
    Function {
        types,
        results: f.results.clone(),
        blocks,
        cold: f.cold,
    }
}

/// The instruction with each value it reads given by `rename`.
pub(super) fn renamed(op: &Op, rename: &impl Fn(Value) -> Value) -> Op {
    let all = |values: &[Value]| values.iter().map(|v| rename(*v)).collect();
    match op {
        Op::Constant(c) => Op::Constant(*c),
        Op::Binary { op, a, b } => Op::Binary {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::Overflows { op, signed, a, b } => Op::Overflows {
            op: *op,
            signed: *signed,
            a: rename(*a),
            b: rename(*b),
        },
        Op::Compare { op, a, b } => Op::Compare {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::FloatBinary { op, a, b } => Op::FloatBinary {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::FloatNegate(value) => Op::FloatNegate(rename(*value)),
        Op::FloatCompare { op, a, b } => Op::FloatCompare {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::FloatFromInt { signed, value } => Op::FloatFromInt {
            signed: *signed,
            value: rename(*value),
        },
        Op::IntFromFloat { signed, value } => Op::IntFromFloat {
            signed: *signed,
            value: rename(*value),
        },
        Op::FloatResize(value) => Op::FloatResize(rename(*value)),
        Op::VectorBinary { op, a, b } => Op::VectorBinary {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::VectorFloatBinary { op, a, b } => Op::VectorFloatBinary {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::VectorCompare { op, signed, a, b } => Op::VectorCompare {
            op: *op,
            signed: *signed,
            a: rename(*a),
            b: rename(*b),
        },
        Op::VectorFloatCompare { op, a, b } => Op::VectorFloatCompare {
            op: *op,
            a: rename(*a),
            b: rename(*b),
        },
        Op::VectorChoose {
            mask,
            then,
            otherwise,
        } => Op::VectorChoose {
            mask: rename(*mask),
            then: rename(*then),
            otherwise: rename(*otherwise),
        },
        Op::VectorSplat(value) => Op::VectorSplat(rename(*value)),
        Op::VectorExtract { vector, lane } => Op::VectorExtract {
            vector: rename(*vector),
            lane: *lane,
        },
        Op::VectorInsert {
            vector,
            lane,
            value,
        } => Op::VectorInsert {
            vector: rename(*vector),
            lane: *lane,
            value: rename(*value),
        },
        Op::VectorSum(vector) => Op::VectorSum(rename(*vector)),
        Op::VectorFirstSet(mask) => Op::VectorFirstSet(rename(*mask)),
        Op::Widen { signed, value } => Op::Widen {
            signed: *signed,
            value: rename(*value),
        },
        Op::Narrow(value) => Op::Narrow(rename(*value)),
        Op::Choose {
            on,
            then,
            otherwise,
        } => Op::Choose {
            on: rename(*on),
            then: rename(*then),
            otherwise: rename(*otherwise),
        },
        Op::Load { ty, address } => Op::Load {
            ty: *ty,
            address: rename(*address),
        },
        Op::Store { ty, address, value } => Op::Store {
            ty: *ty,
            address: rename(*address),
            value: rename(*value),
        },
        Op::Call { function, args } => Op::Call {
            function: *function,
            args: all(args),
        },
        Op::System { call, args } => Op::System {
            call: *call,
            args: all(args),
        },
    }
}
