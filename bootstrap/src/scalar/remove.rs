//! The fourth pass over the scalar form: what nothing uses is removed, and the
//! jumps that only lead on are skipped.
//!
//! The steps, for each function, in this order: a jump to a block that holds
//! only a jump goes where that block goes; a choice between jumps that are the
//! same is that jump; a block that holds at most one instruction and jumps on
//! is copied into the blocks that jump to it; blocks that nothing reaches go;
//! a parameter that every jump into its block gives the same value is that
//! value; an instruction whose values nothing uses goes, unless it writes
//! memory, calls a function, or calls into the system, and so does a
//! parameter that nothing uses, with what each jump gave it; and a block that
//! is jumped to once, by a block that ends with a jump, is joined to that
//! block. The steps are repeated while one changes the function.

use super::graph::{self, jumps, jumps_mut};
use super::{Block, End, Function, Jump, Op, Program, Value};

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
    // Each step can leave work for the others, as an instruction that goes
    // leaves a block with only a jump, so the steps are repeated while one
    // changes the function. Each round that changes it makes it smaller.
    loop {
        let before = f.clone();
        skip_jumps(&mut f);
        join_choices(&mut f);
        copy_small_blocks(&mut f);
        let mut reached = vec![false; f.blocks.len()];
        for b in graph::reverse_postorder(&f) {
            reached[b] = true;
        }
        keep_blocks(&mut f, &reached);
        let copy = graph::copies(&f);
        graph::rename(&mut f, &|v| copy[v.0]);
        drop_unused(&mut f);
        join(&mut f);
        if f == before {
            return f;
        }
    }
}

/// Copies a block that jumps on, and holds no call, into each block that
/// jumps to it, when every jump to it is a plain jump, nothing outside it
/// reads what it defines, and the function is no larger for it: the copies
/// hold no more instructions than the block and its end do together. The
/// block is then reached by nothing and goes, and each block that jumped to
/// it goes on from where it did without a jump between, and without giving
/// a block its parameters to hand them on.
fn copy_small_blocks(f: &mut Function) {
    let n = f.blocks.len();
    let mut into: Vec<Vec<usize>> = vec![Vec::new(); n];
    let mut by_choice = vec![false; n];
    for (b, block) in f.blocks.iter().enumerate() {
        for j in jumps(&block.end) {
            into[j.block].push(b);
            by_choice[j.block] |= matches!(block.end, End::Switch { .. });
        }
    }
    let mut defined_in: Vec<Option<usize>> = vec![None; f.types.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for v in (block.params.iter()).chain(block.instructions.iter().flat_map(|i| &i.results)) {
            defined_in[v.0] = Some(b);
        }
    }
    let mut read_outside = vec![false; n];
    for (b, block) in f.blocks.iter().enumerate() {
        let reads =
            (block.instructions.iter().flat_map(|i| i.op.operands())).chain(block.end.operands());
        for v in reads {
            if let Some(d) = defined_in[v.0]
                && d != b
            {
                read_outside[d] = true;
            }
        }
    }
    for s in 1..n {
        let block = &f.blocks[s];
        let small = block.instructions.len() * (into[s].len().max(1) - 1) <= 1
            && !block.instructions.iter().any(|i| i.op.is_call())
            && matches!(&block.end, End::Jump(j) if j.block != s);
        let preds = &into[s];
        if !small
            || by_choice[s]
            || read_outside[s]
            || preds.len() < 2
            || preds.contains(&s)
            || preds.windows(2).any(|w| w[0] == w[1])
        {
            continue;
        }
        let Block {
            params,
            instructions,
            end,
        } = f.blocks[s].clone();
        for &p in preds {
            let End::Jump(entry) = f.blocks[p].end.clone() else {
                unreachable!()
            };
            let mut name: std::collections::HashMap<Value, Value> = params
                .iter()
                .copied()
                .zip(entry.args.iter().copied())
                .collect();
            let seen = |name: &std::collections::HashMap<Value, Value>, v: Value| {
                name.get(&v).copied().unwrap_or(v)
            };
            for i in &instructions {
                let op = super::inline::renamed(&i.op, &|v| seen(&name, v));
                let results = (i.results.iter())
                    .map(|r| {
                        f.types.push(f.types[r.0]);
                        let fresh = Value(f.types.len() - 1);
                        name.insert(*r, fresh);
                        fresh
                    })
                    .collect();
                f.blocks[p]
                    .instructions
                    .push(super::Instruction { results, op });
            }
            let mut end = end.clone();
            graph::rename_end(&mut end, &|v| seen(&name, v));
            f.blocks[p].end = end;
        }
        // Nothing jumps to the block now, and the next walk drops it.
    }
}

/// Replaces a choice between jumps that are the same by that jump.
fn join_choices(f: &mut Function) {
    for block in &mut f.blocks {
        if let End::Switch { jumps, .. } = &block.end
            && jumps.windows(2).all(|w| w[0] == w[1])
        {
            block.end = End::Jump(jumps[0].clone());
        }
    }
}

/// Makes every jump to a block that holds only a jump go where that block
/// goes, giving what the block would have given. The values the block gives
/// besides its own parameters are defined where every path to the block
/// passes, so they reach each jump to it. A block that jumps to itself is
/// left, and so is the first block, which the function starts in.
fn skip_jumps(f: &mut Function) {
    // Where each value is read outside the end of the block that holds it as
    // a parameter: a block's parameter may be read by any block it reaches.
    let mut read_elsewhere = vec![false; f.types.len()];
    for block in &f.blocks {
        for instruction in &block.instructions {
            for v in instruction.op.operands() {
                read_elsewhere[v.0] = true;
            }
        }
        // What a block's end reads of its own parameters is moved by the jump
        // that skips it, so only what it reads of the others counts.
        for v in block.end.operands() {
            if !block.params.contains(&v) {
                read_elsewhere[v.0] = true;
            }
        }
    }
    let only: Vec<Option<(Vec<Value>, Jump)>> = (f.blocks.iter().enumerate())
        .map(|(b, block)| match &block.end {
            End::Jump(j)
                if b != 0
                    && block.instructions.is_empty()
                    && j.block != b
                    && !block.params.iter().any(|p| read_elsewhere[p.0]) =>
            {
                Some((block.params.clone(), j.clone()))
            }
            _ => None,
        })
        .collect();
    // A cycle of such blocks is a program that never ends, and the walk
    // along it stops after as many steps as there are blocks.
    for block in &mut f.blocks {
        for j in jumps_mut(&mut block.end) {
            for _ in 0..only.len() {
                let Some((params, next)) = &only[j.block] else {
                    break;
                };
                let args = (next.args.iter())
                    .map(|v| match params.iter().position(|p| p == v) {
                        Some(k) => j.args[k],
                        None => *v,
                    })
                    .collect();
                *j = Jump {
                    block: next.block,
                    args,
                };
            }
        }
    }
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
        match def[v.0].unwrap_or_else(|| panic!("{v:?} is not defined in {f:?}")) {
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
