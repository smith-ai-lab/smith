//! What a target does before it gives values their homes: every constant is
//! made in each block that reads it.

use std::collections::HashMap;

use super::graph;
use super::{Block, End, Function, Instruction, Op, Place, Type, Value};

/// The function with each constant made in every block that reads it, in
/// place of one made where it is defined and kept in a register or in the
/// frame across the blocks between. A constant costs one instruction to make
/// and a register or a place in the frame to keep, and a target can often
/// take it in the instruction that reads it, so it is made where it is read.
/// What a constant stands for does not change, so neither does what the
/// program does.
pub fn rematerialized(f: &Function) -> Function {
    let mut f = f.clone();
    let mut constant: Vec<Option<(usize, Op)>> = vec![None; f.types.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for i in &block.instructions {
            if let (Op::Constant(_), [r]) = (&i.op, &i.results[..]) {
                constant[r.0] = Some((b, i.op.clone()));
            }
        }
    }
    for b in 0..f.blocks.len() {
        // The constants this block reads that another block makes, each made
        // once at the start of the block.
        let mut made: HashMap<(Op, Type), Value> = HashMap::new();
        let mut first: Vec<Instruction> = Vec::new();
        let mut local: HashMap<Value, Value> = HashMap::new();
        let mut block = std::mem::replace(
            &mut f.blocks[b],
            Block {
                params: Vec::new(),
                instructions: Vec::new(),
                end: End::Return(Vec::new()),
            },
        );
        // A constant that a choice only gives to the block it goes to is made
        // on the way there, in a block of its own, and not for the paths that
        // go the other way.
        let mut on_the_way: Vec<Block> = Vec::new();
        let read_by_instructions: std::collections::HashSet<Value> =
            (block.instructions.iter().flat_map(|i| i.op.operands())).collect();
        if let End::Switch { on, jumps } = &mut block.end {
            for j in jumps.iter_mut() {
                let args = (j.args.iter())
                    .filter(|v| {
                        constant[v.0].as_ref().is_some_and(|(d, _)| *d != b)
                            && **v != *on
                            && !read_by_instructions.contains(v)
                    })
                    .copied()
                    .collect::<Vec<_>>();
                if args.is_empty() {
                    continue;
                }
                let mut here: HashMap<Value, Value> = HashMap::new();
                let mut made_here = Vec::new();
                for v in args {
                    let Some((_, op)) = &constant[v.0] else {
                        continue;
                    };
                    let ty = f.types[v.0];
                    f.types.push(ty);
                    let fresh = Value(f.types.len() - 1);
                    made_here.push(Instruction {
                        results: vec![fresh],
                        op: op.clone(),
                        at: Place::NONE,
                    });
                    here.insert(v, fresh);
                }
                let args = j.args.iter().map(|v| here.get(v).copied().unwrap_or(*v));
                on_the_way.push(Block {
                    params: Vec::new(),
                    instructions: made_here,
                    end: End::Jump(super::Jump {
                        block: j.block,
                        args: args.collect(),
                    }),
                });
                // The new blocks are added after the last one there is.
                j.block = f.blocks.len() + on_the_way.len() - 1;
                j.args = Vec::new();
            }
        }
        let reads = (block.instructions.iter().flat_map(|i| i.op.operands()))
            .chain(block.end.operands())
            .collect::<Vec<_>>();
        for v in reads {
            let Some((d, op)) = &constant[v.0] else {
                continue;
            };
            if *d == b || local.contains_key(&v) {
                continue;
            }
            let ty = f.types[v.0];
            let fresh = *made.entry((op.clone(), ty)).or_insert_with(|| {
                f.types.push(ty);
                let fresh = Value(f.types.len() - 1);
                first.push(Instruction {
                    results: vec![fresh],
                    op: op.clone(),
                    at: Place::NONE,
                });
                fresh
            });
            local.insert(v, fresh);
        }
        let by = |v: Value| local.get(&v).copied().unwrap_or(v);
        for i in &mut block.instructions {
            i.op.rename(&by);
        }
        graph::rename_end(&mut block.end, &by);
        first.append(&mut block.instructions);
        block.instructions = first;
        f.blocks[b] = block;
        f.blocks.extend(on_the_way);
    }
    // A constant that nothing reads now is not made.
    let mut read = vec![false; f.types.len()];
    for block in &f.blocks {
        let reads =
            (block.instructions.iter().flat_map(|i| i.op.operands())).chain(block.end.operands());
        for v in reads {
            read[v.0] = true;
        }
    }
    for block in &mut f.blocks {
        block
            .instructions
            .retain(|i| !matches!(i.op, Op::Constant(_)) || i.results.iter().any(|r| read[r.0]));
    }
    f
}
