//! The fifth pass over the scalar form: what nothing uses is removed, and the
//! jumps that only lead on are skipped.
//!
//! A walk takes these steps for a function, in this order: a jump to a block
//! that holds only a jump goes where that block goes, and a choice between
//! jumps that are the same is that jump, with the comparison that only it read,
//! and the block that leaves with only a jump, going in the same pass over the
//! blocks; a block that holds at most one instruction
//! and jumps on is copied into the blocks that jump to it; blocks that nothing
//! reach go; a parameter that every jump into its block gives the same value
//! is that value; an instruction whose values nothing uses goes, unless it
//! writes memory, calls a function, or calls into the system, and so does a
//! parameter that nothing uses, with what each jump gave it; the first three
//! steps are taken again, as what went can leave them work, with the blocks
//! and instructions they leave behind taken away; and last a block that is
//! jumped to once, by a block that ends with a jump, is joined to that block.
//! At most `WALKS` walks are made, and the next only when the steps that skip,
//! join, and copy changed the function in the one before. Then a choice on the
//! value that the end of its block chooses on, which only the blocks the end
//! jumps to read, is made in them, as `sink` says, and goes. The last step of
//! all moves a comparison that only the choice ending its block reads to the
//! end of the block.

use super::graph::{self, jumps, jumps_mut};
use super::sink;

use super::{Block, End, Function, Jump, Op, Program, Value};

/// The most walks removal makes over a function. Each walk takes every step
/// once, in order, and the next is made only when the steps that skip jumps,
/// join equal choices, and copy small blocks changed the function in this one,
/// as only they leave work that the steps before them could not see.
pub const WALKS: usize = 4;

/// The program with what nothing uses removed from every function.
pub fn remove(program: &Program) -> Program {
    remove_counting(program).0
}

/// `remove`, and how many walks it made over the function it made most over.
pub fn remove_counting(program: &Program) -> (Program, usize) {
    let mut most = 0;
    let functions = (program.functions.iter())
        .map(|f| {
            let (f, walks) = removed(f);
            most = most.max(walks);
            f
        })
        .collect();
    (
        Program {
            data: program.data.clone(),
            functions,
            main: program.main,
        },
        most,
    )
}

fn removed(f: &Function) -> (Function, usize) {
    let mut f = f.clone();
    let mut walks = 0;
    while walks < WALKS {
        walks += 1;
        if !walk(&mut f, walks == 1) {
            break;
        }
    }
    if sink::choices(&mut f) {
        drop_unused(&mut f, true);
    }
    sink_compares(&mut f);
    (f, walks)
}

/// Every step, once, though the steps that take away what nothing uses are
/// left out of a walk after the first when the steps before them found nothing
/// to change. It says whether the steps that skip jumps, join equal choices,
/// and copy small blocks changed the function.
fn walk(f: &mut Function, first: bool) -> bool {
    let changed = collapse(f) | copy_small_blocks(f);
    if !changed && !first {
        return false;
    }
    drop_unreached(f);
    let copy = graph::copies(f);
    graph::rename(f, &|v| copy[v.0]);
    let removed = drop_unused(f, true);
    // What goes can leave a block that holds only a jump, and a choice
    // between jumps that are the same, and that can leave a comparison that
    // nothing reads.
    let mut changed = changed;
    if removed && (collapse(f) | copy_small_blocks(f)) {
        changed = true;
        drop_unreached(f);
        drop_unused(f, true);
    }
    join(f);
    changed
}

/// Takes away the blocks that nothing reaches.
pub(super) fn drop_unreached(f: &mut Function) {
    let mut reached = vec![false; f.blocks.len()];
    for b in graph::reverse_postorder(f) {
        reached[b] = true;
    }
    keep_blocks(f, &reached);
}

/// Moves each comparison that only the choice ending its block reads to the
/// end of the block, so that a target finds it where it is used and may make
/// the two one compare and one branch. A comparison reads values and writes
/// none, and the values it reads are defined before it, so it may be last.
fn sink_compares(f: &mut Function) {
    let mut reads = vec![0usize; f.types.len()];
    for block in &f.blocks {
        for v in
            (block.instructions.iter().flat_map(|i| i.op.operands())).chain(block.end.operands())
        {
            reads[v.0] += 1;
        }
    }
    for block in &mut f.blocks {
        let End::Switch { on, jumps } = &block.end else {
            continue;
        };
        if jumps.len() != 2 || reads[on.0] != 1 {
            continue;
        }
        let at = block
            .instructions
            .iter()
            .position(|i| i.results == [*on] && matches!(i.op, Op::Compare { .. }));
        if let Some(at) = at {
            let compare = block.instructions.remove(at);
            block.instructions.push(compare);
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
fn copy_small_blocks(f: &mut Function) -> bool {
    let mut changed = false;
    // Most functions have no block that jumps on with at most one
    // instruction, so the work of finding where each is read is not done.
    if !f.blocks.iter().skip(1).any(|block| {
        block.instructions.len() <= 1
            && matches!(block.end, End::Jump(_))
            && !block.instructions.iter().any(|i| i.op.is_call())
    }) {
        return false;
    }
    let n = f.blocks.len();
    let mut count = vec![0usize; n];
    let mut by_choice = vec![false; n];
    for block in &f.blocks {
        for j in jumps(&block.end) {
            count[j.block] += 1;
            by_choice[j.block] |= matches!(block.end, End::Switch { .. });
        }
    }
    // The blocks that may be copied: those that jump on, hold no call, and are
    // jumped to by plain jumps, where the copies are no more than the block
    // and its end.
    let candidates: Vec<usize> = (1..n)
        .filter(|&s| {
            let block = &f.blocks[s];
            count[s] >= 2
                && !by_choice[s]
                && block.instructions.len() * (count[s] - 1) <= 1
                && !block.instructions.iter().any(|i| i.op.is_call())
                && matches!(&block.end, End::Jump(j) if j.block != s)
        })
        .collect();
    if candidates.is_empty() {
        return false;
    }
    // Where the values the candidates define are read, and which blocks jump
    // to each.
    let mut candidate_of = vec![usize::MAX; f.types.len()];
    for &s in &candidates {
        let block = &f.blocks[s];
        for v in (block.params.iter()).chain(block.instructions.iter().flat_map(|i| &i.results)) {
            candidate_of[v.0] = s;
        }
    }
    let mut read_outside = vec![false; n];
    let mut into: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (b, block) in f.blocks.iter().enumerate() {
        let reads =
            (block.instructions.iter().flat_map(|i| i.op.operands())).chain(block.end.operands());
        for v in reads {
            let d = candidate_of[v.0];
            if d != usize::MAX && d != b {
                read_outside[d] = true;
            }
        }
        for j in jumps(&block.end) {
            if candidates.binary_search(&j.block).is_ok() {
                into[j.block].push(b);
            }
        }
    }
    for s in candidates {
        let preds = &into[s];
        if read_outside[s] || preds.contains(&s) || preds.windows(2).any(|w| w[0] == w[1]) {
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
                f.blocks[p].instructions.push(super::Instruction {
                    results,
                    op,
                    at: i.at,
                });
            }
            let mut end = end.clone();
            graph::rename_end(&mut end, &|v| seen(&name, v));
            f.blocks[p].end = end;
            changed = true;
        }
        // Nothing jumps to the block now, and the next walk drops it.
    }
    changed
}

/// What the walk of `collapse` knows of the reads of values.
struct Reads {
    /// How many times each value is read.
    count: Vec<u32>,
    /// Where each result of an instruction is made.
    made: Vec<Option<(usize, usize)>>,
    /// Which instructions nothing that stays reads.
    dead: Vec<Vec<bool>>,
}

impl Reads {
    /// Marks dead the instruction that makes `v`, and those that make what it
    /// reads, as far as nothing else reads them, when nothing reads `v`.
    fn release(&mut self, f: &Function, v: Value) {
        let mut work = vec![v];
        while let Some(v) = work.pop() {
            if self.count[v.0] != 0 {
                continue;
            }
            let Some((b, i)) = self.made[v.0] else {
                continue;
            };
            let instruction = &f.blocks[b].instructions[i];
            if self.dead[b][i]
                || essential(&instruction.op)
                || instruction.results.iter().any(|r| self.count[r.0] != 0)
            {
                continue;
            }
            self.dead[b][i] = true;
            for o in instruction.op.operands() {
                self.count[o.0] -= 1;
                work.push(o);
            }
        }
    }
}

/// Skips jumps through blocks that hold only a jump, and makes a choice
/// between jumps that are the same that jump, in one walk that takes each block
/// after the blocks it jumps to, except by a jump back. The block a choice
/// leaves with only a jump, once the comparison nothing else reads goes, is
/// skipped by the blocks before it in the same walk, so a chain of them is one
/// walk and not a walk for each. A jump to a block that holds only a jump goes
/// where that block goes, giving what it would have given. The values the block
/// gives besides its own parameters are defined where every path to the block
/// passes, so they reach each jump to it. The first block is left, as the
/// function starts in it.
fn collapse(f: &mut Function) -> bool {
    let n = f.blocks.len();
    let mut order = graph::reverse_postorder(f);
    order.reverse();
    let mut reads = Reads {
        count: vec![0; f.types.len()],
        made: vec![None; f.types.len()],
        dead: (f.blocks.iter())
            .map(|b| vec![false; b.instructions.len()])
            .collect(),
    };
    for (b, block) in f.blocks.iter().enumerate() {
        for (i, instruction) in block.instructions.iter().enumerate() {
            for r in &instruction.results {
                reads.made[r.0] = Some((b, i));
            }
            for v in instruction.op.operands() {
                reads.count[v.0] += 1;
            }
        }
        for v in block.end.operands() {
            reads.count[v.0] += 1;
        }
    }
    for b in 0..n {
        for i in 0..f.blocks[b].instructions.len() {
            if let Some(&r) = f.blocks[b].instructions[i].results.first() {
                reads.release(f, r);
            }
        }
    }
    let mut skipped: Vec<Option<(Vec<Value>, Jump)>> = vec![None; n];
    let mut changed = false;
    for b in order {
        let mut end = std::mem::replace(&mut f.blocks[b].end, End::Return(Vec::new()));
        for j in jumps_mut(&mut end) {
            while let Some((params, next)) = &skipped[j.block] {
                let args: Vec<Value> = (next.args.iter())
                    .map(|v| match params.iter().position(|p| p == v) {
                        Some(k) => j.args[k],
                        None => *v,
                    })
                    .collect();
                for v in &args {
                    reads.count[v.0] += 1;
                }
                for v in std::mem::take(&mut j.args) {
                    reads.count[v.0] -= 1;
                    reads.release(f, v);
                }
                *j = Jump {
                    block: next.block,
                    args,
                };
                changed = true;
            }
        }
        if let End::Switch { on, jumps } = &end
            && jumps.windows(2).all(|w| w[0] == w[1])
        {
            let (on, first) = (*on, jumps[0].clone());
            for extra in jumps.iter().skip(1) {
                for v in &extra.args {
                    reads.count[v.0] -= 1;
                    reads.release(f, *v);
                }
            }
            reads.count[on.0] -= 1;
            reads.release(f, on);
            end = End::Jump(first);
            changed = true;
        }
        f.blocks[b].end = end;
        if b != 0
            && let End::Jump(j) = &f.blocks[b].end
            && j.block != b
            && reads.dead[b].iter().all(|d| *d)
        {
            // What the block reads of its own parameters only goes on.
            let mut given = vec![0u32; 0];
            let params = &f.blocks[b].params;
            given.resize(params.len(), 0);
            for v in &j.args {
                if let Some(k) = params.iter().position(|p| p == v) {
                    given[k] += 1;
                }
            }
            if params
                .iter()
                .zip(&given)
                .all(|(p, g)| reads.count[p.0] == *g)
            {
                skipped[b] = Some((params.clone(), j.clone()));
            }
        }
    }
    for (block, dead) in f.blocks.iter_mut().zip(&reads.dead) {
        let mut at = 0;
        block.instructions.retain(|_| {
            at += 1;
            !dead[at - 1]
        });
    }
    changed
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
pub(super) fn drop_unused(f: &mut Function, entry_live: bool) -> bool {
    let before = (f.blocks.iter())
        .map(|b| b.instructions.len() + b.params.len())
        .sum::<usize>();
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
    if entry_live {
        for p in &f.blocks[0].params {
            mark(*p, &mut work);
        }
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
        .map(|(b, block)| {
            (block.params.iter())
                .map(|p| (b == 0 && entry_live) || live[p.0])
                .collect()
        })
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
    let after = (f.blocks.iter())
        .map(|b| b.instructions.len() + b.params.len())
        .sum::<usize>();
    after != before
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
