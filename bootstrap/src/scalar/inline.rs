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
/// function that nothing calls left out. A function with no call that is
/// replaced is the one it was, not a copy.
pub fn inline(program: Program) -> Program {
    let order = callees_first(&program);
    let mut sites = vec![0usize; program.functions.len()];
    for &f in &order {
        for callee in calls(&program.functions[f]) {
            sites[callee] += 1;
        }
    }
    let (data, main) = (program.data, program.main);
    let mut pending: Vec<Option<Function>> = program.functions.into_iter().map(Some).collect();
    let mut done: Vec<Option<Function>> = pending.iter().map(|_| None).collect();
    for &f in &order {
        let function = inlined(pending[f].take().unwrap(), &mut done, &sites);
        done[f] = Some(function);
    }
    // What is left is what `main` still calls, in the order of the program.
    let mut kept = vec![false; done.len()];
    kept[main] = true;
    let mut stack = vec![main];
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
        data,
        functions,
        main: place[main],
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
fn inlined(f: Function, done: &mut [Option<Function>], sites: &[usize]) -> Function {
    // Whether a call of the function is replaced.
    let replaced = |done: &[Option<Function>], function: usize| {
        let callee = done[function].as_ref().unwrap();
        !(callee.cold || (sites[function] != 1 && !is_small(callee)))
    };
    if !(f.blocks.iter().flat_map(|b| &b.instructions))
        .any(|i| matches!(&i.op, Op::Call { function, .. } if replaced(done, *function)))
    {
        return f;
    }
    let Function {
        mut types,
        results,
        blocks: old,
        cold,
        name,
    } = f;
    let mut blocks: Vec<Block> = Vec::new();
    // Where each block of `f` starts in `blocks`, and which blocks end as a
    // block of `f` does, with jumps to blocks of `f`.
    let mut first = Vec::with_capacity(old.len());
    let mut original_ends = Vec::with_capacity(old.len());
    for block in old {
        let Block {
            params,
            instructions,
            end,
        } = block;
        let mut at = blocks.len();
        first.push(at);
        blocks.push(Block {
            params,
            instructions: Vec::new(),
            end: End::Return(Vec::new()),
        });
        for instruction in instructions {
            let Instruction {
                results: call_results,
                op,
                at: place,
            } = instruction;
            let (function, args) = match op {
                Op::Call { function, args } if replaced(done, function) => (function, args),
                op => {
                    blocks[at].instructions.push(Instruction {
                        results: call_results,
                        op,
                        at: place,
                    });
                    continue;
                }
            };
            // The blocks of the callee for this call: its own when this is the only
            // call of it, and a copy when it is not.
            let base = types.len();
            let copied = if sites[function] == 1 {
                let callee = done[function].take().unwrap();
                types.extend(callee.types);
                callee.blocks
            } else {
                let callee = done[function].as_ref().unwrap();
                types.extend_from_slice(&callee.types);
                callee.blocks.clone()
            };
            let rename = |v: Value| Value(v.0 + base);
            let entry = blocks.len();
            let after = entry + copied.len();
            blocks[at].end = End::Jump(Jump { block: entry, args });
            for mut block in copied {
                block.params.iter_mut().for_each(|v| *v = rename(*v));
                for i in &mut block.instructions {
                    i.results.iter_mut().for_each(|v| *v = rename(*v));
                    i.op.rename(&rename);
                }
                block.end = match block.end {
                    End::Return(mut values) => {
                        values.iter_mut().for_each(|v| *v = rename(*v));
                        End::Jump(Jump {
                            block: after,
                            args: values,
                        })
                    }
                    mut end => {
                        if let End::Switch { on, .. } = &mut end {
                            *on = rename(*on);
                        }
                        for j in super::graph::jumps_mut(&mut end) {
                            j.block += entry;
                            j.args.iter_mut().for_each(|v| *v = rename(*v));
                        }
                        end
                    }
                };
                blocks.push(block);
            }
            at = blocks.len();
            blocks.push(Block {
                params: call_results,
                instructions: Vec::new(),
                end: End::Return(Vec::new()),
            });
        }
        blocks[at].end = end;
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
        results,
        blocks,
        cold,
        name,
    }
}

/// The instruction with each value it reads given by `rename`.
pub(super) fn renamed(op: &Op, rename: &impl Fn(Value) -> Value) -> Op {
    let mut op = op.clone();
    op.rename(rename);
    op
}
