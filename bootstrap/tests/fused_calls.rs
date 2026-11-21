//! A fused product given as a call, for a target that has no instruction for
//! it: the function that `scalar::unfuse` puts in its place gives the bits the
//! fused instruction gives, whatever the three floats are, for each of the
//! three fused instructions and both sizes.

use smith::scalar::{self, Builder, FloatBits, Fused, Program, Type};

use crate::Test;
use crate::float_form as ff;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "an_unfused_multiply_add_gives_the_bits_the_fused_instruction_gives_for_f32",
        || unfused_is_fused(FloatBits::F32, Fused::MulAdd, 0),
    ),
    (
        "an_unfused_multiply_subtract_gives_the_bits_the_fused_instruction_gives_for_f32",
        || unfused_is_fused(FloatBits::F32, Fused::MulSub, 1),
    ),
    (
        "an_unfused_negated_multiply_add_gives_the_bits_the_fused_instruction_gives_for_f32",
        || unfused_is_fused(FloatBits::F32, Fused::NegMulAdd, 2),
    ),
    (
        "an_unfused_multiply_add_gives_the_bits_the_fused_instruction_gives_for_f64",
        || unfused_is_fused(FloatBits::F64, Fused::MulAdd, 3),
    ),
    (
        "an_unfused_multiply_subtract_gives_the_bits_the_fused_instruction_gives_for_f64",
        || unfused_is_fused(FloatBits::F64, Fused::MulSub, 4),
    ),
    (
        "an_unfused_negated_multiply_add_gives_the_bits_the_fused_instruction_gives_for_f64",
        || unfused_is_fused(FloatBits::F64, Fused::NegMulAdd, 5),
    ),
];

/// A program of one function of three floats that gives the fused instruction
/// of them.
fn fused_program(bits: FloatBits, op: Fused) -> Program {
    let ty = Type::Float(bits);
    let mut b = Builder::new();
    let v = b.params(&[ty, ty, ty]);
    let r = b.float_fused(op, v[0], v[1], v[2]);
    b.ret(vec![r]);
    Program {
        data: Vec::new(),
        functions: vec![b.finish(&[ty])],
        main: 0,
    }
}

/// The floats of a size that a factor or an addend is drawn from: the edges of
/// the format, powers of 2 and the numbers beside them, and numbers drawn at
/// random of any exponent.
fn floats(bits: FloatBits, g: &mut Gen) -> Vec<u64> {
    let (mantissa, top, width) = match bits {
        FloatBits::F32 => (23u32, 254u64, 32u32),
        FloatBits::F64 => (52, 2046, 64),
    };
    let mut all = ff::edges(bits);
    for e in (1..top).step_by(if bits == FloatBits::F32 { 31 } else { 211 }) {
        let power = e << mantissa;
        all.extend([power, power + 1, power - 1]);
    }
    for _ in 0..30 {
        let x = (g.below(1 << 30) as u64) << 34 | (g.below(1 << 30) as u64) << 4 | g.below(16) as u64;
        let exponent = (g.below(top as usize + 1) as u64) << mantissa;
        let fraction = if g.below(4) == 0 { 0 } else { x & ((1 << mantissa) - 1) };
        all.push(exponent | fraction);
    }
    let sign = 1u64 << (width - 1);
    let negated: Vec<u64> = all.iter().map(|x| x ^ sign).collect();
    all.extend(negated);
    all
}

/// What `a * b` is, rounded, as the bits.
fn product(bits: FloatBits, a: u64, b: u64) -> u64 {
    match bits {
        FloatBits::F32 => (f32::from_bits(a as u32) * f32::from_bits(b as u32)).to_bits() as u64,
        FloatBits::F64 => (f64::from_bits(a) * f64::from_bits(b)).to_bits(),
    }
}

/// The triples tried: any three of the floats, and an addend that nearly
/// cancels the product, one unit from it and equal and opposite to it, where a
/// sum loses most of its bits.
fn triples(bits: FloatBits, g: &mut Gen) -> Vec<[u64; 3]> {
    let values = floats(bits, g);
    let width = if bits == FloatBits::F32 { 32 } else { 64 };
    let sign = 1u64 << (width - 1);
    let mut all = Vec::new();
    for _ in 0..900 {
        let (a, b, c) = (
            *g.pick(&values),
            *g.pick(&values),
            *g.pick(&values),
        );
        all.push([a, b, c]);
    }
    for _ in 0..500 {
        let (a, b) = (*g.pick(&values), *g.pick(&values));
        let p = product(bits, a, b);
        let nudge = [0u64, 1, 2, u64::MAX][g.below(4)];
        all.push([a, b, (p ^ sign).wrapping_add(nudge)]);
    }
    all
}

/// What the program that gives the fused instruction of three floats writes,
/// run by the evaluator, as the bits of the result.
fn written(program: &Program, ty: Type, args: &[u64]) -> Vec<u8> {
    let observed = ff::observed(
        program.functions.clone(),
        0,
        &[(ty, args[0]), (ty, args[1]), (ty, args[2])],
        true,
    );
    let observed = Program {
        data: program.data.clone(),
        ..observed
    };
    scalar::run(&observed).stdout
}

fn unfused_is_fused(bits: FloatBits, op: Fused, seed: u64) {
    let all = generate::sample(seed, 0, |g| triples(bits, g));
    let ty = Type::Float(bits);
    let program = fused_program(bits, op);
    let unfused = scalar::unfuse(&program).expect("a fused instruction");
    for [a, b, c] in all {
        let args = [a, b, c];
        let want = written(&program, ty, &args);
        let got = written(&unfused, ty, &args);
        assert!(
            got == want || same_nan(bits, &got, &want),
            "{op:?} of {a:#x}, {b:#x}, {c:#x} for {bits:?}: {got:?} for {want:?}"
        );
    }
}

/// Whether both are a float that is not a number, in any form, as the bytes
/// of the result are written.
fn same_nan(bits: FloatBits, a: &[u8], b: &[u8]) -> bool {
    let word = |x: &[u8]| {
        let mut w = [0u8; 8];
        w[..x.len().min(8)].copy_from_slice(&x[..x.len().min(8)]);
        u64::from_le_bytes(w)
    };
    ff::same(bits, word(a), word(b))
}
