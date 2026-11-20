//! The pass before the first: a function whose call of itself is the last
//! thing it does is a loop.
//!
//! A call of the function from itself is a tail call when what it gives back
//! is what the function gives back, with nothing done to it between: its
//! block ends by returning the call's results, in order, or by jumping with
//! them, in order, to a block that holds no instruction and returns what it
//! was given, as the block that ends an `if` or a `match` does. Such a call
//! is a jump to the start of the function, giving the call's arguments.
//!
//! The first block of a function holds its parameters and is entered only by
//! a call, so a jump back to it would give a loop whose head is the entry. The
//! pass puts a new first block before it, that takes the function's parameters
//! and jumps to the old first block, which is then the head of the loop. A
//! function with no such call is not changed, and a call that something is
//! done after, or that is not of the function itself, stays a call.
//!
//! The pass walks each function once, and changes only the blocks that make
//! such a call, the new first block, and the number of every block, which
//! each jump holds.

use super::{Block, End, Function, Instruction, Jump, Op, Program, Value};

/// The program with each tail call of a function from itself made a jump.
pub fn tail(program: Program) -> Program {
    let main = program.main;
    Program {
        data: program.data,
        functions: (program.functions.into_iter().enumerate())
            .map(|(i, f)| if i == main { f } else { looped(i, f) })
            .collect(),
        main,
    }
}

/// The function with the calls of itself, at `index`, that are the last thing
/// it does made jumps.
fn looped(index: usize, mut f: Function) -> Function {
    let sites = tail_calls(index, &f);
    if sites.is_empty() {
        return f;
    }
    // The calls become jumps to the old first block, which is the second
    // block once the new first block is in, so every block number is one more.
    for block in &mut f.blocks {
        for j in block.end.jumps_mut() {
            j.block += 1;
        }
    }
    let entry: Vec<Value> = f.blocks[0]
        .params
        .clone()
        .into_iter()
        .map(|p| {
            f.types.push(f.types[p.0]);
            Value(f.types.len() - 1)
        })
        .collect();
    for (b, call) in sites {
        let block = &mut f.blocks[b];
        let Some(Instruction {
            op: Op::Call { args, .. },
            ..
        }) = block.instructions.pop()
        else {
            unreachable!("a tail call is the last instruction of its block")
        };
        debug_assert_eq!(call, args.len());
        block.end = End::Jump(Jump { block: 1, args });
    }
    f.blocks.insert(
        0,
        Block {
            params: entry.clone(),
            instructions: Vec::new(),
            end: End::Jump(Jump {
                block: 1,
                args: entry,
            }),
        },
    );
    f
}

/// The blocks of the function, `index`, that end with a call of it that is
/// the last thing it does, and how many arguments the call has.
fn tail_calls(index: usize, f: &Function) -> Vec<(usize, usize)> {
    // A tail call is the last instruction of its block, so a function with no
    // block that ends with a call of itself is known without reading an
    // instruction of it or making a table, which is what nearly every function
    // is.
    let ends_with_call = |block: &Block| {
        matches!(
            block.instructions.last(),
            Some(Instruction { op: Op::Call { function, .. }, .. }) if *function == index
        )
    };
    if !f.blocks.iter().any(ends_with_call) {
        return Vec::new();
    }
    let mut reads = vec![0u32; f.types.len()];
    for block in &f.blocks {
        for i in &block.instructions {
            for v in i.op.operands() {
                reads[v.0] += 1;
            }
        }
        for v in block.end.operands() {
            reads[v.0] += 1;
        }
    }
    let mut sites = Vec::new();
    for (b, block) in f.blocks.iter().enumerate() {
        if !ends_with_call(block) {
            continue;
        }
        let Some(Instruction {
            results,
            op: Op::Call { args, .. },
            ..
        }) = block.instructions.last()
        else {
            continue;
        };
        if gives_back(f, &block.end, results, &reads) {
            sites.push((b, args.len()));
        }
    }
    sites
}

/// Whether `end` returns `values`, as they are, directly or through blocks that
/// hold no instruction and hand them on in order, when nothing else reads them.
fn gives_back(f: &Function, end: &End, values: &[Value], reads: &[u32]) -> bool {
    if values.iter().any(|v| reads[v.0] != 1) {
        return false;
    }
    let mut values = values;
    let mut end = end;
    for _ in 0..=f.blocks.len() {
        match end {
            End::Return(given) => return given == values,
            End::Jump(j) => {
                let Some(next) = f.blocks.get(j.block) else {
                    return false;
                };
                if j.args != values
                    || !next.instructions.is_empty()
                    || next.params.iter().any(|p| reads[p.0] != 1)
                {
                    return false;
                }
                values = &next.params;
                end = &next.end;
            }
            End::Switch { .. } => return false,
        }
    }
    false
}
