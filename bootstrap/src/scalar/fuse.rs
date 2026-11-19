//! The fusing of a product of floats with the one sum or difference that uses
//! it, as [the rules floats keep](../../../knowledge/language/float-rules.md)
//! say, into one instruction with one rounding.
//!
//! This is part of what a program means, so it is decided where the form is
//! made, from each function as it was written, and no pass makes a fused
//! instruction or takes one apart. A product is fused when its one read, in
//! its own function, is as an operand of an addition or a subtraction of floats
//! of the same size, or of vectors of floats, on either side of it. A product
//! that is read twice, or by anything else, such as a jump, a call, a
//! comparison, or another product, is left as it is. A product that a call
//! gives back is read by the call's function and not by the sum, so inlining
//! never makes it fused. The product stays where it is, read by nothing, for
//! `remove` to take away, and the fused instruction takes the place of the sum,
//! where the product's operands are alive, as the product's own place comes
//! before it.
//!
//! A sum of two products fuses the first, and the second stays a product that
//! the fused instruction reads as its addend.

use std::collections::HashMap;

use super::{At, FloatBinary, Function, Fused, Op, Program, Value};

/// The program with every product that has one use, in its function, as the
/// operand of a sum or a difference, fused with it. It is applied to a form
/// when it is made, before any pass.
pub fn fuse(program: &Program) -> Program {
    Program {
        data: program.data.clone(),
        functions: program.functions.iter().map(fused).collect(),
        main: program.main,
    }
}

/// A product, and whether it is of vectors.
#[derive(Clone, Copy)]
struct Product {
    a: Value,
    b: Value,
    vector: bool,
    at: At,
}

fn fused(f: &Function) -> Function {
    let mut f = f.clone();
    let mut uses = vec![0u32; f.types.len()];
    let mut values = Vec::new();
    for block in &f.blocks {
        for i in &block.instructions {
            i.op.operands_into(&mut values);
            values.iter().for_each(|v| uses[v.0] += 1);
        }
        block.end.operands_into(&mut values);
        values.iter().for_each(|v| uses[v.0] += 1);
    }
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
                            at: i.at,
                        },
                    );
                }
                Op::VectorFloatBinary {
                    op: FloatBinary::Mul,
                    a,
                    b,
                } if uses[i.results[0].0] == 1 => {
                    products.insert(
                        i.results[0],
                        Product {
                            a,
                            b,
                            vector: true,
                            at: i.at,
                        },
                    );
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
                    if let Some((op, Product { a, b, at, .. }, c)) = found {
                        // It takes the place of the first of the two it makes one of.
                        i.at = at;
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
