//! What every target shares about a branch on a comparison.

use crate::scalar::{End, Function, Instruction, Op};

/// How many jumps a choice by a number must have to be made through a table
/// of where each goes, and not by comparing the number with each place in turn.
pub const TABLE: usize = 5;

/// The value an instruction makes that says what the flags found: a
/// comparison's, or whether the answer of an operation did not fit.
fn flag(i: &Instruction) -> Option<crate::scalar::Value> {
    match i.op {
        Op::Compare { .. } => i.results.first().copied(),
        Op::Overflows { .. } => i.results.get(1).copied(),
        _ => None,
    }
}

/// For each block, whether it ends with a choice between two jumps on the
/// result of a comparison that is the last instruction of the block and that
/// nothing else reads. A target does not make that comparison into a value:
/// it compares, and branches on what the comparison found.
pub fn on_compare(f: &Function) -> Vec<bool> {
    let mut reads = vec![0usize; f.types.len()];
    for block in &f.blocks {
        let operands = block.instructions.iter().flat_map(|i| i.op.operands());
        for v in operands.chain(block.end.operands()) {
            reads[v.0] += 1;
        }
    }
    f.blocks
        .iter()
        .map(|block| match (&block.end, block.instructions.last()) {
            (End::Switch { on, jumps }, Some(last)) => {
                jumps.len() == 2 && reads[on.0] == 1 && flag(last) == Some(*on)
            }
            _ => false,
        })
        .collect()
}

/// For each instruction of each block, whether it is a comparison that the
/// next instruction, a choice, reads, and nothing else reads. A target does
/// not make that comparison into a value: it compares, and chooses on what the
/// comparison found.
pub fn before_choice(f: &Function) -> Vec<Vec<bool>> {
    let mut reads = vec![0usize; f.types.len()];
    for block in &f.blocks {
        let operands = block.instructions.iter().flat_map(|i| i.op.operands());
        for v in operands.chain(block.end.operands()) {
            reads[v.0] += 1;
        }
    }
    f.blocks
        .iter()
        .map(|block| {
            let next = block.instructions.iter().skip(1).map(Some).chain([None]);
            block
                .instructions
                .iter()
                .zip(next)
                .map(|(i, next)| match next.map(|n| &n.op) {
                    Some(Op::Choose { on, .. }) => flag(i) == Some(*on) && reads[on.0] == 1,
                    _ => false,
                })
                .collect()
        })
        .collect()
}

/// The number each value is, for a value that a constant integer gives.
pub fn constants(f: &Function) -> Vec<Option<u64>> {
    let mut constants = vec![None; f.types.len()];
    for block in &f.blocks {
        for i in &block.instructions {
            if let (Op::Constant(crate::scalar::Constant::Int(n)), [r]) = (&i.op, &i.results[..]) {
                let bytes = f.types[r.0].bytes();
                constants[r.0] = Some(if bytes == 8 {
                    *n
                } else {
                    n & ((1 << (8 * bytes)) - 1)
                });
            }
        }
    }
    constants
}
