//! Which of a function's values can be, or hold, memory that the function
//! gives back as its result.
//!
//! The handler of `alloc` builds a function's result in its caller's memory
//! and its other lists and texts in memory it gives up when it returns. Which
//! is which is decided here, for each value, from the function alone.
//!
//! Two values are put in one group when memory that one is, or holds, can be
//! reached through the other: an address and the address moved from it, an
//! address and the addresses held at it, a value and what a block it jumps to
//! calls it, and every address a call takes or gives. A group is *given back*
//! when it has an address the function returns. The test errs towards giving
//! back: it never says a value stays when it may not, and a word of 8 bytes
//! that is no address is taken for one only where it moves through memory,
//! which is how a copy of a list's elements moves an address. A value that
//! holds no address is never in a group, and neither is a constant, which
//! names memory but is none the function made.

use super::common::dominated;
use super::graph::jumps;
use super::{Bits, Constant, End, Function, Instruction, Op, Type, Value};

/// For each value of the function, whether it is in a group that the function
/// gives back through an address it returns.
pub fn given_back(f: &Function) -> Vec<bool> {
    let (mut groups, constant) = grouped(f);
    let mut back = vec![false; f.types.len()];
    for block in &f.blocks {
        if let End::Return(values) = &block.end {
            for v in values.iter().filter(|v| f.types[v.0] == Type::Address) {
                back[groups.find(v.0)] = true;
            }
        }
    }
    (0..f.types.len())
        .map(|v| !constant[v] && back[groups.find(v)])
        .collect()
}

/// The groups of the function's values, and which values are constants.
fn grouped(f: &Function) -> (Groups, Vec<bool>) {
    let mut groups = Groups::new(f.types.len());
    let holds = |v: &Value| f.types[v.0] == Type::Address || f.types[v.0] == Type::Int(Bits::B64);
    let mut constant = vec![false; f.types.len()];
    for i in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Constant(Constant::Int(_) | Constant::Null | Constant::Data(_)) = i.op {
            constant[i.results[0].0] = true;
        }
    }
    let mut join = |a: Value, b: Value| {
        if !constant[a.0] && !constant[b.0] {
            groups.join(a.0, b.0);
        }
    };
    for block in &f.blocks {
        for i in &block.instructions {
            match &i.op {
                Op::Binary { a, .. } if f.types[i.results[0].0] == Type::Address => {
                    join(i.results[0], *a)
                }
                Op::Choose {
                    then, otherwise, ..
                } if holds(&i.results[0]) => {
                    join(i.results[0], *then);
                    join(i.results[0], *otherwise);
                }
                Op::Load { address, .. } if holds(&i.results[0]) => join(i.results[0], *address),
                Op::Store { address, value, .. } if holds(value) => join(*value, *address),
                Op::Call { args, .. } => {
                    let held: Vec<Value> = (args.iter().chain(&i.results))
                        .filter(|v| f.types[v.0] == Type::Address)
                        .copied()
                        .collect();
                    for v in held.iter().skip(1) {
                        join(held[0], *v);
                    }
                }
                _ => {}
            }
        }
        for jump in jumps(&block.end) {
            for (arg, param) in jump.args.iter().zip(&f.blocks[jump.block].params) {
                if holds(param) {
                    join(*arg, *param);
                }
            }
        }
    }
    (groups, constant)
}

/// The rounds of the function's loops that end by releasing what they took.
pub struct Rounds {
    /// For each block, for each jump it ends with, whether the jump goes back
    /// to the start of a round that hands nothing on, so that what the round
    /// took from the stack for temporaries is released by it.
    pub release: Vec<Vec<bool>>,
    /// For each block, whether a jump of it enters a loop of those.
    pub enter: Vec<bool>,
}

/// Which loops of the function hand nothing they take from the stack for
/// temporaries on to their next round, and where those rounds end.
///
/// A loop is the blocks from which a jump back to its first block can be
/// reached without passing through that block. A round hands on what it makes
/// when an address it makes is, or is held with, a value that is made outside
/// the loop or that the first block takes as a parameter, which is what the
/// next round or the code after it can read. Whether an address is held with
/// another is what the groups above say, which err towards holding: a loop
/// that is in doubt releases nothing. `takes` says of an instruction whether
/// the memory it gives is taken from the stack for temporaries.
pub fn rounds(f: &Function, takes: &dyn Fn(&Instruction) -> bool) -> Rounds {
    let n = f.blocks.len();
    let mut rounds = Rounds {
        release: f
            .blocks
            .iter()
            .map(|b| vec![false; jumps(&b.end).len()])
            .collect(),
        enter: vec![false; n],
    };
    let tree = dominated(f);
    let mut parent = vec![usize::MAX; n];
    for (b, children) in tree.iter().enumerate() {
        for c in children {
            parent[*c] = b;
        }
    }
    // The blocks the tree holds are the ones that can be reached.
    let reached = {
        let mut done = vec![false; n];
        let mut stack = vec![0];
        while let Some(b) = stack.pop() {
            done[b] = true;
            stack.extend(tree[b].iter().copied());
        }
        done
    };
    let dominates = |h: usize, mut b: usize| {
        loop {
            if b == h {
                return true;
            }
            if b == 0 || parent[b] == usize::MAX {
                return false;
            }
            b = parent[b];
        }
    };
    let mut headers: Vec<usize> = Vec::new();
    let mut back: Vec<(usize, usize, usize)> = Vec::new();
    for (b, block) in f.blocks.iter().enumerate() {
        if !reached[b] {
            continue;
        }
        for (j, jump) in jumps(&block.end).into_iter().enumerate() {
            if reached[jump.block] && dominates(jump.block, b) {
                back.push((b, j, jump.block));
                if !headers.contains(&jump.block) {
                    headers.push(jump.block);
                }
            }
        }
    }
    if headers.is_empty() {
        return rounds;
    }
    let mut before: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (b, block) in f.blocks.iter().enumerate() {
        if reached[b] {
            for jump in jumps(&block.end) {
                before[jump.block].push(b);
            }
        }
    }
    let (mut groups, _) = grouped(f);
    for header in headers {
        let mut inside = vec![false; n];
        inside[header] = true;
        let mut stack: Vec<usize> = Vec::new();
        for (b, _, h) in &back {
            if *h == header && !inside[*b] {
                inside[*b] = true;
                stack.push(*b);
            }
        }
        while let Some(b) = stack.pop() {
            for p in &before[b] {
                if !inside[*p] {
                    inside[*p] = true;
                    stack.push(*p);
                }
            }
        }
        // The values that are made in the loop, but not the first block's
        // parameters, which the next round is given.
        let mut made = vec![false; f.types.len()];
        for (b, block) in f.blocks.iter().enumerate() {
            if !inside[b] {
                continue;
            }
            if b != header {
                for p in &block.params {
                    made[p.0] = true;
                }
            }
            for i in &block.instructions {
                for r in &i.results {
                    made[r.0] = true;
                }
            }
        }
        let mut handed_on = vec![false; f.types.len()];
        for v in 0..f.types.len() {
            if !made[v] {
                handed_on[groups.find(v)] = true;
            }
        }
        let mut any = false;
        let mut takes_one = true;
        for (b, block) in f.blocks.iter().enumerate() {
            if !inside[b] {
                continue;
            }
            for i in &block.instructions {
                if takes(i) {
                    any = true;
                    for r in i.results.iter().filter(|r| f.types[r.0] == Type::Address) {
                        if handed_on[groups.find(r.0)] {
                            takes_one = false;
                        }
                    }
                }
            }
        }
        if !any || !takes_one {
            continue;
        }
        for (b, j, h) in &back {
            if *h == header {
                rounds.release[*b][*j] = true;
            }
        }
        for (b, block) in f.blocks.iter().enumerate() {
            if !inside[b] && reached[b] && jumps(&block.end).iter().any(|j| j.block == header) {
                rounds.enter[b] = true;
            }
        }
    }
    rounds
}

/// Groups of values, each by the value that stands for it.
struct Groups(Vec<usize>);

impl Groups {
    fn new(n: usize) -> Groups {
        Groups((0..n).collect())
    }

    fn find(&mut self, mut v: usize) -> usize {
        while self.0[v] != v {
            self.0[v] = self.0[self.0[v]];
            v = self.0[v];
        }
        v
    }

    fn join(&mut self, a: usize, b: usize) {
        let (a, b) = (self.find(a), self.find(b));
        self.0[a] = b;
    }
}
