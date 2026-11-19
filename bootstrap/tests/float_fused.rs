//! A product whose only use, in its function, is one sum or difference is one
//! instruction with one rounding, for floats and for each lane of a vector of
//! floats. `scalar::fuse` decides it where a form is made, and no pass makes a
//! fused instruction or takes one apart.

use smith::scalar::{
    self, Builder, FloatBinary, FloatBits, Fused, Lane, Op, Program, Type, Value,
};

use crate::Test;
use crate::float_form as ff;
use crate::generate::{self, Gen};
use crate::targets::{runnable, scalar_runs_as};
use crate::vector_form as vf;

pub const ONCE: &[Test] = &[
    (
        "a_fused_instruction_rounds_once_where_a_product_and_a_sum_round_twice",
        a_fused_instruction_rounds_once_where_a_product_and_a_sum_round_twice,
    ),
    (
        "a_product_whose_only_use_is_one_sum_or_difference_is_one_fused_instruction_and_no_product",
        a_product_whose_only_use_is_one_sum_or_difference_is_one_fused_instruction_and_no_product,
    ),
    (
        "a_product_that_is_used_twice_or_by_anything_but_one_sum_or_difference_is_not_fused",
        a_product_that_is_used_twice_or_by_anything_but_one_sum_or_difference_is_not_fused,
    ),
    (
        "a_product_in_one_function_and_its_sum_in_another_is_not_fused_whether_or_not_the_call_is_inlined",
        a_product_in_one_function_and_its_sum_in_another_is_not_fused_whether_or_not_the_call_is_inlined,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_fused_product_gives_what_the_evaluator_gives",
    every_executable_of_a_fused_product_gives_what_the_evaluator_gives,
)];

/// The types a product is made of: floats of each size, and vectors of floats.
fn types() -> Vec<Type> {
    let mut all = Vec::new();
    for bits in [FloatBits::F32, FloatBits::F64] {
        all.push(Type::Float(bits));
        all.push(vf::vec(Lane::Float(bits)));
    }
    all
}

fn bits_of(ty: Type) -> FloatBits {
    match ty {
        Type::Float(bits) | Type::Vector(Lane::Float(bits)) => bits,
        _ => panic!("not a float"),
    }
}

/// Makes `a * b` of the type.
fn product(b: &mut Builder, ty: Type, x: Value, y: Value) -> Value {
    match ty {
        Type::Vector(_) => b.vector_float_binary(FloatBinary::Mul, x, y),
        _ => b.float_binary(FloatBinary::Mul, x, y),
    }
}

/// Makes `x op y` of the type, for a sum or a difference.
fn sum(b: &mut Builder, ty: Type, op: FloatBinary, x: Value, y: Value) -> Value {
    match ty {
        Type::Vector(_) => b.vector_float_binary(op, x, y),
        _ => b.float_binary(op, x, y),
    }
}

/// A function of three parameters that gives what a product of the first two
/// and the third make: the product first or second in the sum or the
/// difference.
fn shape(ty: Type, op: FloatBinary, product_first: bool) -> Builder {
    let mut b = Builder::new();
    let v = b.params(&[ty, ty, ty]);
    let p = product(&mut b, ty, v[0], v[1]);
    let r = if product_first {
        sum(&mut b, ty, op, p, v[2])
    } else {
        sum(&mut b, ty, op, v[2], p)
    };
    b.ret(vec![r]);
    b
}

fn program(ty: Type, b: Builder, args: &[u128]) -> Program {
    let f = b.finish(&[ty]);
    let args: Vec<(Type, u128)> = [ty, ty, ty].into_iter().zip(args.iter().copied()).collect();
    vf::observed(vec![f], 0, &args, true)
}

fn instructions(program: &Program) -> impl Iterator<Item = &Op> {
    program
        .functions
        .iter()
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .map(|i| &i.op)
}

fn products(program: &Program) -> usize {
    instructions(program)
        .filter(|op| {
            matches!(
                op,
                Op::FloatBinary { op: FloatBinary::Mul, .. }
                    | Op::VectorFloatBinary { op: FloatBinary::Mul, .. }
            )
        })
        .count()
}

fn sums(program: &Program) -> usize {
    instructions(program)
        .filter(|op| {
            matches!(
                op,
                Op::FloatBinary { op: FloatBinary::Add | FloatBinary::Sub, .. }
                    | Op::VectorFloatBinary { op: FloatBinary::Add | FloatBinary::Sub, .. }
            )
        })
        .count()
}

fn fused(program: &Program) -> Vec<Fused> {
    instructions(program)
        .filter_map(|op| match op {
            Op::FloatFused { op, .. } | Op::VectorFloatFused { op, .. } => Some(*op),
            _ => None,
        })
        .collect()
}

/// What a function of three floats gives, written by its program run by the
/// evaluator, as the bits of the result.
fn gives(program: &Program) -> u128 {
    assert_eq!(scalar::check(program), Ok(()));
    vf::written(&scalar::run(program).stdout)[0]
}

/// A float or a vector of the type with every lane the bits `n`.
fn spread(ty: Type, n: u64) -> u128 {
    match ty {
        Type::Vector(lane) => vf::made(lane, |_| n),
        _ => u128::from(n),
    }
}

/// `1 + 2^-k` of the size, and its negation, as bits.
fn one_plus(bits: FloatBits, k: i32) -> u64 {
    match bits {
        FloatBits::F32 => u64::from((1.0f32 + 2f32.powi(-k)).to_bits()),
        FloatBits::F64 => (1.0f64 + 2f64.powi(-k)).to_bits(),
    }
}

fn a_fused_instruction_rounds_once_where_a_product_and_a_sum_round_twice() {
    for ty in types() {
        let bits = bits_of(ty);
        // `x = 1 + 2^-k` is such that `x * x` is `1 + 2^-(k-1) + 2^-2k`, whose last
        // bit does not fit, and rounds to `1 + 2^-(k-1)`.
        let k = match bits {
            FloatBits::F32 => 12,
            FloatBits::F64 => 27,
        };
        let x = one_plus(bits, k);
        let near = one_plus(bits, k - 1);
        let negative = near | (1 << (bits.count() - 1));
        let exact = match bits {
            FloatBits::F32 => u64::from(2f32.powi(-2 * k).to_bits()),
            FloatBits::F64 => 2f64.powi(-2 * k).to_bits(),
        };
        let args = [spread(ty, x), spread(ty, x), spread(ty, negative)];
        let mut b = Builder::new();
        let v = b.params(&[ty, ty, ty]);
        let r = match ty {
            Type::Vector(_) => b.vector_float_fused(Fused::MulAdd, v[0], v[1], v[2]),
            _ => b.float_fused(Fused::MulAdd, v[0], v[1], v[2]),
        };
        b.ret(vec![r]);
        assert_eq!(gives(&program(ty, b, &args)), spread(ty, exact));
        // Two roundings of the same numbers give zero.
        let twice = shape(ty, FloatBinary::Add, true);
        assert_eq!(gives(&program(ty, twice, &args)), 0);
    }
}

fn a_product_whose_only_use_is_one_sum_or_difference_is_one_fused_instruction_and_no_product() {
    for ty in types() {
        for op in [FloatBinary::Add, FloatBinary::Sub] {
            for product_first in [true, false] {
                let bits = bits_of(ty);
                let args = [
                    spread(ty, ff::edges(bits)[2]),
                    spread(ty, ff::edges(bits)[5]),
                    spread(ty, ff::edges(bits)[3]),
                ];
                let unfused = program(ty, shape(ty, op, product_first), &args);
                let made = scalar::fuse(&unfused);
                let what = format!("{ty:?} {op:?} {product_first}");
                assert_eq!(scalar::check(&made), Ok(()), "{what}");
                let kind = match (op, product_first) {
                    (FloatBinary::Add, _) => Fused::MulAdd,
                    (_, true) => Fused::MulSub,
                    (_, false) => Fused::NegMulAdd,
                };
                // After the passes the form holds the one fused instruction and no
                // product or sum, and gives what it gave.
                let passed = scalar::passes(&made);
                assert_eq!(fused(&passed), vec![kind], "{what}");
                assert_eq!(products(&passed), 0, "{what}");
                assert_eq!(sums(&passed), 0, "{what}");
                assert_eq!(gives(&passed), gives(&made), "{what}");
                // No pass makes one: the form that was not fused stays as it is.
                let kept = scalar::passes(&unfused);
                assert!(fused(&kept).is_empty(), "{what}");
                assert_eq!(products(&kept), 1, "{what}");
                assert_eq!(gives(&kept), gives(&unfused), "{what}");
            }
        }
    }
}

fn a_product_that_is_used_twice_or_by_anything_but_one_sum_or_difference_is_not_fused() {
    for ty in types() {
        let bits = bits_of(ty);
        let args = [spread(ty, ff::edges(bits)[2]); 3];
        let make: [(&str, Reader); 5] = [
            ("a sum and a difference", |b, ty, v| {
                let p = product(b, ty, v[0], v[1]);
                let s = sum(b, ty, FloatBinary::Add, p, v[2]);
                sum(b, ty, FloatBinary::Sub, p, s)
            }),
            ("a sum of it with itself", |b, ty, v| {
                let p = product(b, ty, v[0], v[1]);
                let s = sum(b, ty, FloatBinary::Add, p, p);
                sum(b, ty, FloatBinary::Add, s, v[2])
            }),
            ("another product", |b, ty, v| {
                let p = product(b, ty, v[0], v[1]);
                product(b, ty, p, v[2])
            }),
            ("a difference of it with itself", |b, ty, v| {
                let p = product(b, ty, v[0], v[1]);
                let s = sum(b, ty, FloatBinary::Sub, p, p);
                sum(b, ty, FloatBinary::Add, s, v[2])
            }),
            ("what the function gives, and a sum", |b, ty, v| {
                let p = product(b, ty, v[0], v[1]);
                let s = sum(b, ty, FloatBinary::Add, p, v[2]);
                let (next, params) = b.block(&[ty, ty]);
                b.jump(next, vec![p, s]);
                b.at(next);
                sum(b, ty, FloatBinary::Sub, params[0], params[1])
            }),
        ];
        for (what, make) in make {
            let mut b = Builder::new();
            let v = b.params(&[ty, ty, ty]);
            let r = make(&mut b, ty, &v);
            b.ret(vec![r]);
            let made = scalar::fuse(&program(ty, b, &args));
            let passed = scalar::passes(&made);
            assert_eq!(scalar::check(&passed), Ok(()), "{what}");
            assert_eq!(fused(&made).len(), 0, "{what} of {ty:?}");
            assert_eq!(fused(&passed).len(), 0, "{what} of {ty:?}");
            assert!(products(&passed) >= 1, "{what} of {ty:?}");
        }
    }
}

fn a_product_in_one_function_and_its_sum_in_another_is_not_fused_whether_or_not_the_call_is_inlined() {
    for ty in types() {
        let bits = bits_of(ty);
        // The numbers of the first test, where one rounding and two differ.
        let k = if bits == FloatBits::F32 { 12 } else { 27 };
        let x = one_plus(bits, k);
        let negative = one_plus(bits, k - 1) | (1 << (bits.count() - 1));
        let args = [spread(ty, x), spread(ty, x), spread(ty, negative)];
        for cold in [false, true] {
            let mut b = Builder::new();
            let v = b.params(&[ty, ty]);
            let p = product(&mut b, ty, v[0], v[1]);
            b.ret(vec![p]);
            let mut multiply = b.finish(&[ty]);
            // A function that is cold is never replaced by its instructions.
            multiply.cold = cold;
            let mut b = Builder::new();
            let v = b.params(&[ty, ty, ty]);
            let p = b.call(0, vec![v[0], v[1]], &[ty])[0];
            let r = sum(&mut b, ty, FloatBinary::Add, p, v[2]);
            b.ret(vec![r]);
            let add = b.finish(&[ty]);
            let params = [(ty, args[0]), (ty, args[1]), (ty, args[2])];
            let made = scalar::fuse(&vf::observed(vec![multiply, add], 1, &params, true));
            let passed = scalar::passes(&made);
            let what = format!("{ty:?}, cold {cold}");
            assert_eq!(scalar::check(&passed), Ok(()), "{what}");
            assert!(fused(&made).is_empty(), "{what}");
            assert!(fused(&passed).is_empty(), "{what}");
            // A function that is not cold is inlined, and one that is stays a call.
            let calls = instructions(&passed)
                .filter(|op| matches!(op, Op::Call { function: 0, .. }))
                .count();
            assert_eq!(calls == 0, !cold, "{what}: {calls} calls of the product");
            // Two roundings of these numbers give zero.
            assert_eq!(gives(&made), 0, "{what}");
            assert_eq!(gives(&passed), 0, "{what}");
        }
    }
}

/// A function of three parameters, made in a builder.
type Reader = fn(&mut Builder, Type, &[Value]) -> Value;

/// A fused shape and its three arguments, for any type and any edge.
fn generated(g: &mut Gen) -> (Type, FloatBinary, bool, [u128; 3]) {
    let all = types();
    let ty = all[g.below(all.len())];
    let op = [FloatBinary::Add, FloatBinary::Sub][g.below(2)];
    let args = [(); 3].map(|_| match ty {
        Type::Vector(lane) => vf::vector_value(g, lane),
        _ => u128::from(ff::float(g, bits_of(ty))),
    });
    (ty, op, g.below(2) == 0, args)
}

fn every_executable_of_a_fused_product_gives_what_the_evaluator_gives() {
    generate::check(6171, 60, generated, |(ty, op, first, args)| {
        let f = shape(*ty, *op, *first).finish(&[*ty]);
        let args: Vec<(Type, u128)> = [*ty, *ty, *ty].into_iter().zip(args.iter().copied()).collect();
        // A result that is not a number is written as the one the evaluator gives.
        let made = scalar::fuse(&vf::observed_canonically(vec![f], 0, &args, true));
        assert!(!fused(&made).is_empty());
        assert_eq!(scalar::check(&made), Ok(()));
        let said = scalar::run(&made);
        let passed = scalar::passes(&made);
        for target in runnable() {
            scalar_runs_as(&made, target, &said);
            scalar_runs_as(&passed, target, &said);
        }
    });
}
