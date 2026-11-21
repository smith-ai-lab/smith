//! The square root of a float is one instruction of the scalar form that gives
//! the float nearest the exact root, as IEEE 754 says and Rust's `sqrt` does,
//! and the bits of a float are an integer of its size and back, with every bit
//! kept, a float that is not a number included. Every executable is held to the
//! evaluator by the tests of the instructions of floats.

use smith::scalar::{self, Bits, Builder, FloatBits, Program, Type};

use crate::Test;
use crate::float_form as ff;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_square_root_of_a_float_is_the_one_rounded_once_as_rust_gives_it",
        the_square_root_of_a_float_is_the_one_rounded_once_as_rust_gives_it,
    ),
    (
        "the_bits_of_a_float_are_an_integer_of_its_size_and_back_with_every_bit_kept",
        the_bits_of_a_float_are_an_integer_of_its_size_and_back_with_every_bit_kept,
    ),
];

/// A program of one function that gives the square root of a float.
fn root_of(bits: FloatBits) -> Program {
    let ty = Type::Float(bits);
    let mut b = Builder::new();
    let v = b.params(&[ty]);
    let r = b.float_sqrt(v[0]);
    b.ret(vec![r]);
    Program {
        data: Vec::new(),
        functions: vec![b.finish(&[ty])],
        main: 0,
    }
}

fn numbers(bits: FloatBits, g: &mut Gen) -> Vec<u64> {
    let mut all = ff::edges(bits);
    for _ in 0..300 {
        all.push(ff::float(g, bits));
    }
    // The perfect squares and the numbers beside them, where a root is exact
    // or is half way between two floats.
    for k in [1u64, 2, 3, 4, 9, 16, 25, 100, 4_503_599_627_370_496] {
        match bits {
            FloatBits::F32 => {
                let x = (k as f32) * (k as f32);
                all.extend([x.to_bits(), x.to_bits() + 1, x.to_bits() - 1].map(u64::from));
            }
            FloatBits::F64 => {
                let x = (k as f64) * (k as f64);
                all.extend([x.to_bits(), x.to_bits() + 1, x.to_bits() - 1]);
            }
        }
    }
    all
}

fn the_square_root_of_a_float_is_the_one_rounded_once_as_rust_gives_it() {
    for (seed, bits) in [(0, FloatBits::F32), (1, FloatBits::F64)] {
        let program = root_of(bits);
        for x in generate::sample(seed, 0, |g| numbers(bits, g)) {
            let got = scalar::call(&program, 0, &[x])[0];
            let want = match bits {
                FloatBits::F32 => u64::from(f32::from_bits(x as u32).sqrt().to_bits()),
                FloatBits::F64 => f64::from_bits(x).sqrt().to_bits(),
            };
            assert!(ff::same(bits, got, want), "{x:#x} for {bits:?}: {got:#x} for {want:#x}");
        }
    }
}

fn the_bits_of_a_float_are_an_integer_of_its_size_and_back_with_every_bit_kept() {
    for (seed, bits, int) in [(2, FloatBits::F32, Bits::B32), (3, FloatBits::F64, Bits::B64)] {
        let ty = Type::Float(bits);
        let mut b = Builder::new();
        let v = b.params(&[ty]);
        let n = b.float_bits(v[0]);
        let back = b.float_from_bits(n);
        b.ret(vec![n, back]);
        let program = Program {
            data: Vec::new(),
            functions: vec![b.finish(&[Type::Int(int), ty])],
            main: 0,
        };
        for x in generate::sample(seed, 0, |g| numbers(bits, g)) {
            // Every bit is kept, as the evaluator holds a float.
            assert_eq!(scalar::call(&program, 0, &[x]), vec![x, x], "{x:#x}");
        }
    }
}
