//! The third pass over the scalar form: what nothing uses is removed.
//!
//! One walk for each step, in this order, for each function: blocks that
//! nothing reaches go; a parameter that every jump into its block gives the
//! same value is that value; an instruction whose values nothing uses goes,
//! unless it writes memory, calls a function, or calls into the system, and
//! so does a parameter that nothing uses, with what each jump gave it; and
//! a block that is jumped to once, by a block that ends with a jump, is
//! joined to that block.

use super::graph::{self, jumps, jumps_mut};
use super::{Block, End, Function, Op, Program, Value};

/// The program with what nothing uses removed from every function.
pub fn remove(program: &Program) -> Program {
    Program {
        data: program.data.clone(),
        functions: program.functions.iter().map(removed).collect(),
        main: program.main,
    }
}

fn removed(f: &Function) -> Function {
    let mut f = f.clone();
    let mut reached = vec![false; f.blocks.len()];
    for b in graph::reverse_postorder(&f) {
        reached[b] = true;
    }
    keep_blocks(&mut f, &reached);
    let copy = graph::copies(&f);
    graph::rename(&mut f, &|v| copy[v.0]);
    drop_unused(&mut f);
    join(&mut f);
    f
}

/// The function with only the blocks that are kept, in their order. A kept
/// block jumps only to kept blocks.
fn keep_blocks(f: &mut Function, keep: &[bool]) {
    let mut place = vec![0; keep.len()];
    let mut count = 0;
    for (b, kept) in keep.iter().enumerate() {
        place[b] = count;
        count += usize::from(*kept);
    }
    let blocks = std::mem::take(&mut f.blocks);
    f.blocks = (blocks.into_iter().enumerate())
        .filter(|(b, _)| keep[*b])
        .map(|(_, mut block)| {
            for j in jumps_mut(&mut block.end) {
                j.block = place[j.block];
            }
            block
        })
        .collect();
}

/// Whether an instruction does something besides give its values.
fn essential(op: &Op) -> bool {
    matches!(op, Op::Store { .. } | Op::Call { .. } | Op::System { .. })
}

/// Removes every instruction whose values nothing uses, and every
/// parameter that nothing uses. A value is used by what a live instruction
/// reads, by a return, by a switch, and by a jump that gives it to a
/// parameter that is used.
fn drop_unused(f: &mut Function) {
    #[derive(Clone, Copy)]
    enum Def {
        Param(usize, usize),
        Result(usize, usize),
    }
    let mut def: Vec<Option<Def>> = vec![None; f.types.len()];
    let mut into: Vec<Vec<(usize, usize)>> = vec![Vec::new(); f.blocks.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for (k, p) in block.params.iter().enumerate() {
            def[p.0] = Some(Def::Param(b, k));
        }
        for (i, instruction) in block.instructions.iter().enumerate() {
            for r in &instruction.results {
                def[r.0] = Some(Def::Result(b, i));
            }
        }
        for (ji, j) in jumps(&block.end).into_iter().enumerate() {
            into[j.block].push((b, ji));
        }
    }
    let mut live = vec![false; f.types.len()];
    let mut work: Vec<Value> = Vec::new();
    let mut mark = |v: Value, work: &mut Vec<Value>| {
        if !live[v.0] {
            live[v.0] = true;
            work.push(v);
        }
    };
    for p in &f.blocks[0].params {
        mark(*p, &mut work);
    }
    for block in &f.blocks {
        for instruction in block.instructions.iter().filter(|i| essential(&i.op)) {
            for v in instruction.op.operands() {
                mark(v, &mut work);
            }
        }
        match &block.end {
            End::Return(values) => values.iter().for_each(|v| mark(*v, &mut work)),
            End::Switch { on, .. } => mark(*on, &mut work),
            End::Jump(_) => {}
        }
    }
    while let Some(v) = work.pop() {
        match def[v.0].expect("a value that is defined") {
            Def::Result(b, i) => {
                for o in f.blocks[b].instructions[i].op.operands() {
                    mark(o, &mut work);
                }
            }
            Def::Param(b, k) => {
                for &(src, ji) in &into[b] {
                    let a = jumps(&f.blocks[src].end)[ji].args[k];
                    mark(a, &mut work);
                }
            }
        }
    }
    let kept: Vec<Vec<bool>> = (f.blocks.iter().enumerate())
        .map(|(b, block)| (block.params.iter()).map(|p| b == 0 || live[p.0]).collect())
        .collect();
    for block in &mut f.blocks {
        block
            .instructions
            .retain(|i| essential(&i.op) || i.results.iter().any(|r| live[r.0]));
        for j in jumps_mut(&mut block.end) {
            let mut place = 0;
            j.args.retain(|_| {
                place += 1;
                kept[j.block][place - 1]
            });
        }
    }
    for (b, block) in f.blocks.iter_mut().enumerate() {
        let mut place = 0;
        block.params.retain(|_| {
            place += 1;
            kept[b][place - 1]
        });
    }
}

/// Joins each block that is jumped to once, by a block that ends with a
/// jump, to the block that jumps to it.
fn join(f: &mut Function) {
    let n = f.blocks.len();
    let mut count = vec![0usize; n];
    for block in &f.blocks {
        for j in jumps(&block.end) {
            count[j.block] += 1;
        }
    }
    let mut joined = vec![false; n];
    for (a, block) in f.blocks.iter().enumerate() {
        if let End::Jump(j) = &block.end
            && j.block != 0
            && j.block != a
            && count[j.block] == 1
            && f.blocks[j.block].params.is_empty()
        {
            joined[j.block] = true;
        }
    }
    for a in 0..n {
        if joined[a] {
            continue;
        }
        while let End::Jump(j) = &f.blocks[a].end {
            let to = j.block;
            if !joined[to] {
                break;
            }
            let Block {
                instructions, end, ..
            } = std::mem::replace(
                &mut f.blocks[to],
                Block {
                    params: Vec::new(),
                    instructions: Vec::new(),
                    end: End::Return(Vec::new()),
                },
            );
            f.blocks[a].instructions.extend(instructions);
            f.blocks[a].end = end;
        }
    }
    let keep: Vec<bool> = joined.iter().map(|j| !j).collect();
    keep_blocks(f, &keep);
}
