//! What every target shares about a branch on a comparison.

use crate::scalar::{End, Function, Op};

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
                jumps.len() == 2
                    && reads[on.0] == 1
                    && last.results == [*on]
                    && matches!(last.op, Op::Compare { .. })
            }
            _ => false,
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
