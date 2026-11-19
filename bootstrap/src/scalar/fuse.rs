//! The fusing of a product of floats with the one sum or difference that uses
//! it, as [the rules floats keep](../../../knowledge/language/float-rules.md)
//! say, into one instruction with one rounding.
//!
//! A product is fused when its one use is as an operand of an addition or a
//! subtraction of floats of the same size, or of vectors of floats, on either
//! side of it. A product that is read twice, or by anything else, such as a
//! jump, a call, a comparison, or another product, is left as it is. The
//! product stays where it is, read by nothing, for `remove` to take away, and
//! the fused instruction takes the place of the sum, where the product's
//! operands are alive, as the product's own place comes before it.
//!
//! A sum of two products fuses the first, and the second stays a product that
//! the fused instruction reads as its addend.

use std::collections::HashMap;

use super::graph::{self, jumps};
use super::{End, FloatBinary, Function, Fused, Op, Program, Value};

/// The program with every product that has one use, as the operand of a sum
/// or a difference, fused with it.
pub fn fuse(program: &Program) -> Program {
    Program {
        data: program.data.clone(),
        functions: program.functions.iter().map(fused).collect(),
        main: program.main,
    }
}

/// How many times each value is read. A value that a jump gives to a parameter
/// that nothing reads is not read by the jump, as `remove` takes the parameter
/// away, so a product that a block is passed and its sum alone reads is read
/// once.
fn reads(f: &Function) -> Vec<u32> {
    let mut uses = vec![0u32; f.types.len()];
    loop {
        let mut next = vec![0u32; f.types.len()];
        let mut values = Vec::new();
        for block in &f.blocks {
            for i in &block.instructions {
                i.op.operands_into(&mut values);
                values.iter().for_each(|v| next[v.0] += 1);
            }
            match &block.end {
                End::Jump(_) => {}
                End::Switch { on, .. } => next[on.0] += 1,
                End::Return(returned) => returned.iter().for_each(|v| next[v.0] += 1),
            }
            for j in jumps(&block.end) {
                let params = &f.blocks[j.block].params;
                for (arg, param) in j.args.iter().zip(params) {
                    if uses[param.0] > 0 {
                        next[arg.0] += 1;
                    }
                }
            }
        }
        if next == uses {
            return uses;
        }
        uses = next;
    }
}

/// A product, and whether it is of vectors.
#[derive(Clone, Copy)]
struct Product {
    a: Value,
    b: Value,
    vector: bool,
}

fn fused(f: &Function) -> Function {
    let mut f = f.clone();
    // A value that a parameter only copies is the value it copies, so a
    // product that a block is passed is read where the sum reads it.
    let copy = graph::copies(&f);
    graph::rename(&mut f, &|v| copy[v.0]);
    let uses = reads(&f);
    let mut products: HashMap<Value, Product> = HashMap::new();
    for block in &mut f.blocks {
        for i in &mut block.instructions {
            match i.op {
                Op::FloatBinary {
                    op: FloatBinary::Mul,
                    a,
                    b,
                } if uses[i.results[0].0] == 1 => {
                    products.insert(
                        i.results[0],
                        Product {
                            a,
                            b,
                            vector: false,
                        },
                    );
                }
                Op::VectorFloatBinary {
                    op: FloatBinary::Mul,
                    a,
                    b,
                } if uses[i.results[0].0] == 1 => {
                    products.insert(i.results[0], Product { a, b, vector: true });
                }
                Op::FloatBinary {
                    op: op @ (FloatBinary::Add | FloatBinary::Sub),
                    a: x,
                    b: y,
                }
                | Op::VectorFloatBinary {
                    op: op @ (FloatBinary::Add | FloatBinary::Sub),
                    a: x,
                    b: y,
                } => {
                    let vector = matches!(i.op, Op::VectorFloatBinary { .. });
                    let sub = op == FloatBinary::Sub;
                    let found = match (products.get(&x), products.get(&y)) {
                        (Some(p), _) if p.vector == vector => {
                            Some((if sub { Fused::MulSub } else { Fused::MulAdd }, *p, y))
                        }
                        (_, Some(p)) if p.vector == vector => {
                            Some((if sub { Fused::NegMulAdd } else { Fused::MulAdd }, *p, x))
                        }
                        _ => None,
                    };
                    if let Some((op, Product { a, b, .. }, c)) = found {
                        i.op = if vector {
                            Op::VectorFloatFused { op, a, b, c }
                        } else {
                            Op::FloatFused { op, a, b, c }
                        };
                    }
                }
                _ => {}
            }
        }
    }
    f
}
