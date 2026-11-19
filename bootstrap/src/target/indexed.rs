//! What every target shares about a write at an address that is a base and an
//! index times the size of what is written.

use crate::scalar::{Binary, Function, Op, Value};

/// What a target does with an instruction of a block.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Folded {
    /// Makes the instruction as it is.
    No,
    /// Makes nothing, as the store that reads what it makes takes its place.
    Skipped,
    /// A store whose address is `base + index * scale`, written as one
    /// instruction that reaches it.
    Store {
        base: Value,
        index: Value,
        scale: u8,
    },
}

/// For each instruction of each block, whether it is a store at the sum of a
/// base and an index times 2, 4, or 8, which the target writes as one store, and
/// the multiplication and the addition it makes no instruction of. They are
/// the two instructions that come before the store, apart from constants that
/// the target holds, and nothing else reads what either makes, so that the
/// base and the index are in the places they were in when the store is made.
/// `allowed` says whether the chip reaches an address of that scale for a write
/// of that many bytes.
pub fn stores(
    f: &Function,
    constants: &[Option<u64>],
    held: &[bool],
    allowed: impl Fn(u8, u32) -> bool,
) -> Vec<Vec<Folded>> {
    let mut reads = vec![0usize; f.types.len()];
    for block in &f.blocks {
        let operands = block.instructions.iter().flat_map(|i| i.op.operands());
        for v in operands.chain(block.end.operands()) {
            reads[v.0] += 1;
        }
    }
    let result_of = |at: usize, block: &crate::scalar::Block, v: Value| -> bool {
        block.instructions[at].results == [v] && reads[v.0] == 1
    };
    f.blocks
        .iter()
        .map(|block| {
            let mut folded = vec![Folded::No; block.instructions.len()];
            // The instruction before `at` that is not a constant held.
            let before = |at: usize| -> Option<usize> {
                (0..at).rev().find(|&j| {
                    !matches!(&block.instructions[j].op, Op::Constant(_))
                        || !held[block.instructions[j].results[0].0]
                })
            };
            for (k, i) in block.instructions.iter().enumerate() {
                let Op::Store { ty, address, value } = &i.op else {
                    continue;
                };
                let width = ty.bytes();
                let Some(add) = before(k) else { continue };
                let Op::Binary {
                    op: Binary::Add,
                    a,
                    b,
                } = &block.instructions[add].op
                else {
                    continue;
                };
                if !result_of(add, block, *address) || value == address {
                    continue;
                }
                let Some(mul) = before(add) else { continue };
                let Op::Binary {
                    op: Binary::Mul,
                    a: x,
                    b: y,
                } = &block.instructions[mul].op
                else {
                    continue;
                };
                // Which operand of the sum is the product.
                let (base, product) = match (*a, *b) {
                    (a, b) if result_of(mul, block, b) => (a, b),
                    (a, b) if result_of(mul, block, a) => (b, a),
                    _ => continue,
                };
                let (index, scale) = match (constants[x.0], constants[y.0]) {
                    (None, Some(n)) => (*x, n),
                    (Some(n), None) => (*y, n),
                    _ => continue,
                };
                if ![2, 4, 8].contains(&scale)
                    || f.types[index.0].bytes() != 8
                    || f.types[base.0].bytes() != 8
                    || base == product
                    || !allowed(scale as u8, width as u32)
                {
                    continue;
                }
                folded[mul] = Folded::Skipped;
                folded[add] = Folded::Skipped;
                folded[k] = Folded::Store {
                    base,
                    index,
                    scale: scale as u8,
                };
            }
            folded
        })
        .collect()
}
