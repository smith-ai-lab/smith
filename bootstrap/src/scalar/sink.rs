//! A step of `remove`: a choice on the value a block ends with a choice on is
//! made in the blocks after that choice, where the value is known.
//!
//! A block that ends by choosing between two jumps on a value, and holds a
//! choice on that same value, makes that choice for the blocks that each jump
//! leads to alone, which are the blocks the block it leads to dominates, when
//! no other jump from outside them leads to it. In
//! the first the value is 0, and in the second it is not, so each block that
//! reads the choice reads the one value it would have given. A jump that
//! gives the choice gives that value too. When nothing else reads the choice,
//! it is read by nothing, and `remove` takes it away, and the comparison that
//! made the value is read by the end of its block alone, so a target makes it
//! one compare and one jump. A `match` on what `checked_add` gives is that: an
//! addition, a comparison, and a jump.

use std::collections::HashMap;

use super::common::dominated;
use super::graph::{jumps, jumps_mut, rename_end};
use super::{End, Function, Op, Value};

/// Makes each choice that only the blocks after a choice on the same value
/// read in those blocks, and says whether it made one.
pub(super) fn choices(f: &mut Function) -> bool {
    // The choices, each with the block that ends by choosing on its value.
    let mut found = Vec::new();
    for (b, block) in f.blocks.iter().enumerate() {
        let End::Switch { on, jumps } = &block.end else {
            continue;
        };
        if jumps.len() != 2 || jumps[0].block == jumps[1].block {
            continue;
        }
        for i in &block.instructions {
            if let (
                Op::Choose {
                    on: by,
                    then,
                    otherwise,
                },
                [result],
            ) = (&i.op, &i.results[..])
                && by == on
            {
                found.push((b, *result, *then, *otherwise));
            }
        }
    }
    if found.is_empty() {
        return false;
    }
    // The blocks that jump to each block, once for each jump.
    let mut into: Vec<Vec<usize>> = vec![Vec::new(); f.blocks.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for j in jumps(&block.end) {
            into[j.block].push(b);
        }
    }
    let children = dominated(f);
    // The blocks a block dominates, itself first.
    let region = |from: usize| {
        let mut all = vec![from];
        let mut at = 0;
        while at < all.len() {
            all.extend(&children[all[at]]);
            at += 1;
        }
        all
    };
    // Where each choice is read: in a block's instructions, or in its end.
    let mut reads: HashMap<Value, Vec<(usize, bool)>> = HashMap::new();
    let results: Vec<Value> = found.iter().map(|c| c.1).collect();
    for (b, block) in f.blocks.iter().enumerate() {
        for (is_end, values) in block
            .instructions
            .iter()
            .map(|i| (false, i.op.operands()))
            .chain([(true, block.end.operands())])
        {
            for v in values {
                if results.contains(&v) {
                    reads.entry(v).or_default().push((b, is_end));
                }
            }
        }
    }
    let mut made = false;
    for (b, result, then, otherwise) in found {
        let End::Switch { jumps: ends, .. } = &f.blocks[b].end else {
            unreachable!()
        };
        // The blocks that a jump leads to when no jump from outside them
        // leads to the first of them but this one, so that the value the
        // block chose on is the same wherever they are, as a jump back to
        // the first of them from inside them keeps it.
        let places = [ends[0].block, ends[1].block].map(|t| {
            let inside = region(t);
            let from_outside = into[t].iter().filter(|p| !inside.contains(p)).count();
            if from_outside == 1 && !inside.contains(&b) {
                inside
            } else {
                Vec::new()
            }
        });
        let covered = |at: &(usize, bool)| {
            // A read by the end of its own block is covered when it is a
            // jump's argument, which is the only way an end reads a choice
            // that is not the value it chooses on.
            (at.0 == b && at.1) || places.iter().any(|p| p.contains(&at.0))
        };
        let Some(readers) = reads.get(&result) else {
            continue;
        };
        if readers.is_empty() || !readers.iter().all(covered) {
            continue;
        }
        let values = [otherwise, then];
        let by = |value: Value| move |v: Value| if v == result { value } else { v };
        for (k, place) in places.iter().enumerate() {
            for &r in place {
                let block = &mut f.blocks[r];
                for i in &mut block.instructions {
                    i.op.rename(&by(values[k]));
                }
                rename_end(&mut block.end, &by(values[k]));
            }
        }
        // What the block's own jumps give: each is made for one value.
        let own = jumps_mut(&mut f.blocks[b].end);
        for (k, j) in own.into_iter().enumerate() {
            j.args.iter_mut().for_each(|v| *v = by(values[k])(*v));
        }
        made = true;
    }
    made
}
