//! Floats in the scalar form: the evaluator gives the bits IEEE 754 gives, the
//! passes fold what is known to those bits and rewrite nothing else, and a
//! function that does not use floats the way the form says is refused.

use smith::scalar::{
    self, Bits, Builder, FloatBinary, FloatBits, FloatCompare, Function, Op, Program, Type, Why,
};

use crate::Test;
use crate::float_form::{
    self as ff, BINARY, F32, F64, I8, I32, I64, edges, float_bits, float_instructions, int,
    observed, one, same,
};
use crate::generate;

pub const ONCE: &[Test] = &[
    (
        "each_float_operation_gives_the_bits_ieee_754_gives",
        each_float_operation_gives_the_bits_ieee_754_gives,
    ),
    (
        "a_float_from_a_whole_number_and_a_whole_number_from_a_float_are_what_they_say",
        a_float_from_a_whole_number_and_a_whole_number_from_a_float_are_what_they_say,
    ),
    (
        "a_result_that_is_not_a_number_is_the_one_quiet_number_that_is_not_a_number",
        a_result_that_is_not_a_number_is_the_one_quiet_number_that_is_not_a_number,
    ),
    (
        "a_float_is_kept_in_memory_and_chosen_as_it_is",
        a_float_is_kept_in_memory_and_chosen_as_it_is,
    ),
    (
        "folding_gives_the_bits_the_evaluator_gives",
        folding_gives_the_bits_the_evaluator_gives,
    ),
    (
        "the_passes_rewrite_no_float_that_is_not_known",
        the_passes_rewrite_no_float_that_is_not_known,
    ),
    (
        "every_pass_keeps_what_a_generated_function_of_floats_and_whole_numbers_gives",
        every_pass_keeps_what_a_generated_function_of_floats_and_whole_numbers_gives,
    ),
    (
        "a_float_where_the_form_wants_another_type_is_refused",
        a_float_where_the_form_wants_another_type_is_refused,
    ),
];

/// A pass over a program.
type Pass = fn(&Program) -> Program;

fn call(program: &Program, args: &[u64]) -> u64 {
    scalar::call(program, 0, args)[0]
}

/// Every pair of the edges of the size, and some pairs of any bits.
fn pairs(bits: FloatBits) -> Vec<(u64, u64)> {
    let edges = edges(bits);
    let mut pairs = Vec::new();
    for x in &edges {
        for y in &edges {
            pairs.push((*x, *y));
        }
    }
    for case in 0..200 {
        let (x, y) = generate::sample(5130, case, |g| (ff::float(g, bits), ff::float(g, bits)));
        pairs.push((x, y));
    }
    pairs
}

fn each_float_operation_gives_the_bits_ieee_754_gives() {
    for ty in [F32, F64] {
        let bits = float_bits(ty);
        for op in BINARY {
            let p = one(&[ty, ty], ty, |b, v| b.float_binary(op, v[0], v[1]));
            for (x, y) in pairs(bits) {
                let got = call(&p, &[x, y]);
                let want = ff::binary(op, bits, x, y);
                assert!(
                    same(bits, got, want),
                    "{op:?} of {x:#x} and {y:#x} in {bits:?} gives {got:#x}, not {want:#x}"
                );
            }
        }
        for op in FloatCompare::ALL {
            let p = one(&[ty, ty], I8, |b, v| b.float_compare(op, v[0], v[1]));
            for (x, y) in pairs(bits) {
                let got = call(&p, &[x, y]);
                let want = ff::compare(op, bits, x, y);
                assert_eq!(
                    got, want,
                    "{op:?} of {x:#x} and {y:#x} in {bits:?} gives {got}, not {want}"
                );
            }
        }
        // Negation flips the sign bit of whatever it is, a number that is not
        // a number too, and keeps the rest.
        let negate = one(&[ty], ty, |b, v| b.float_negate(v[0]));
        for x in edges(bits) {
            let sign = 1u64 << (float_bits_count(bits) - 1);
            assert_eq!(call(&negate, &[x]), x ^ sign, "the negation of {x:#x}");
        }
    }
    // The comparisons say what the zeros and the numbers that are not numbers
    // must: the zeros are equal, and nothing is below, above, or equal to a
    // number that is not a number, which is not equal to itself.
    let p = one(&[F64, F64], I8, |b, v| {
        b.float_compare(FloatCompare::Equal, v[0], v[1])
    });
    assert_eq!(call(&p, &[0.0f64.to_bits(), (-0.0f64).to_bits()]), 1);
    assert_eq!(call(&p, &[f64::NAN.to_bits(), f64::NAN.to_bits()]), 0);
}

fn float_bits_count(bits: FloatBits) -> u32 {
    bits.count()
}

fn a_float_from_a_whole_number_and_a_whole_number_from_a_float_are_what_they_say() {
    for from in Bits::ALL {
        for to in [FloatBits::F32, FloatBits::F64] {
            for signed in [false, true] {
                let p = one(&[Type::Int(from)], Type::Float(to), |b, v| {
                    b.float_from_int(signed, v[0], to)
                });
                let mut inputs: Vec<u64> = Vec::new();
                for case in 0..60 {
                    inputs.push(generate::sample(5131, case, |g| int(g, from)));
                }
                for n in inputs {
                    let want = ff::nearest(to, ff::number(signed, from, n));
                    assert_eq!(
                        call(&p, &[n]),
                        want,
                        "the float of {n:#x}, read as {} of {from:?}, in {to:?}",
                        if signed { "signed" } else { "unsigned" }
                    );
                }
            }
        }
    }
    for from in [FloatBits::F32, FloatBits::F64] {
        for to in Bits::ALL {
            for signed in [false, true] {
                let p = one(&[Type::Float(from)], Type::Int(to), |b, v| {
                    b.int_from_float(signed, v[0], to)
                });
                let mut inputs = edges(from);
                for case in 0..60 {
                    inputs.push(generate::sample(5132, case, |g| ff::float(g, from)));
                }
                for x in inputs {
                    assert_eq!(
                        call(&p, &[x]),
                        ff::cut(signed, from, to, x),
                        "the {to:?} of {x:#x} in {from:?}, {}",
                        if signed { "signed" } else { "unsigned" }
                    );
                }
            }
        }
    }
    for (from, to) in [(FloatBits::F32, FloatBits::F64), (FloatBits::F64, FloatBits::F32)] {
        let p = one(&[Type::Float(from)], Type::Float(to), |b, v| {
            b.float_resize(v[0], to)
        });
        let mut inputs = edges(from);
        for case in 0..100 {
            inputs.push(generate::sample(5133, case, |g| ff::float(g, from)));
        }
        for x in inputs {
            let want = match to {
                FloatBits::F64 => ff::value(from, x).to_bits(),
                FloatBits::F32 => u64::from((f64::from_bits(x) as f32).to_bits()),
            };
            assert!(
                same(to, call(&p, &[x]), want),
                "{from:?} to {to:?} of {x:#x}"
            );
        }
    }
}

fn a_result_that_is_not_a_number_is_the_one_quiet_number_that_is_not_a_number() {
    for ty in [F32, F64] {
        let bits = float_bits(ty);
        let nan = ff::quiet_nan(bits);
        let (zero, infinity) = (0u64, edges(bits)[edges(bits).len() - 6]);
        let _ = infinity;
        for op in [FloatBinary::Div, FloatBinary::Sub, FloatBinary::Mul] {
            let p = one(&[ty, ty], ty, |b, v| b.float_binary(op, v[0], v[1]));
            let inf = match bits {
                FloatBits::F32 => u64::from(f32::INFINITY.to_bits()),
                FloatBits::F64 => f64::INFINITY.to_bits(),
            };
            let (x, y) = match op {
                FloatBinary::Div => (zero, zero),
                FloatBinary::Sub => (inf, inf),
                _ => (zero, inf),
            };
            assert_eq!(call(&p, &[x, y]), nan, "{op:?} of {x:#x} and {y:#x}");
        }
    }
}

fn a_float_is_kept_in_memory_and_chosen_as_it_is() {
    for ty in [F32, F64] {
        let bits = float_bits(ty);
        // Written and read back through an address, at the size of the float.
        let program = one(&[ty], ty, |b, v| {
            let size = b.int(Bits::B64, 16);
            let at = b.allocate(size);
            b.store(at, v[0]);
            b.load(ty, at)
        });
        // Chosen by a flag, either way.
        let choice = one(&[I8, ty, ty], ty, |b, v| b.choose(v[0], v[1], v[2]));
        for x in edges(bits) {
            assert_eq!(call(&program, &[x]), x, "the float {x:#x} kept in memory");
            assert_eq!(call(&choice, &[1, x, 0]), x);
            assert_eq!(call(&choice, &[0, 0, x]), x);
        }
    }
    // Two floats of different sizes lie at their own places in an element.
    assert_eq!(scalar::element(&[F32, F64, F32]), (vec![0, 8, 16], 24));
}

/// The constants a function of the form is made of and the instruction that
/// works on them, as a function with no parameter.
fn folded_case(make: impl Fn(&mut Builder) -> (scalar::Value, Type)) -> Program {
    let mut b = Builder::new();
    let (r, ty) = make(&mut b);
    b.ret(vec![r]);
    let f = b.finish(&[ty]);
    observed(vec![f], 0, &[], false)
}

fn folding_gives_the_bits_the_evaluator_gives() {
    let mut count = 0;
    let mut check = |program: Program| {
        let want = scalar::run(&program);
        let passed = scalar::passes(&program);
        assert_eq!(scalar::check(&passed), Ok(()));
        assert_eq!(scalar::run(&passed), want, "{program:?}");
        assert_eq!(
            float_instructions(&passed),
            0,
            "an instruction on constants is left in {passed:?}"
        );
        count += 1;
    };
    for bits in [FloatBits::F32, FloatBits::F64] {
        let some: Vec<(u64, u64)> = pairs(bits).into_iter().step_by(37).collect();
        for (x, y) in some {
            for op in BINARY {
                check(folded_case(|b| {
                    let (x, y) = (b.float(bits, x), b.float(bits, y));
                    (b.float_binary(op, x, y), Type::Float(bits))
                }));
            }
            for op in FloatCompare::ALL {
                check(folded_case(|b| {
                    let (x, y) = (b.float(bits, x), b.float(bits, y));
                    (b.float_compare(op, x, y), I8)
                }));
            }
            check(folded_case(|b| {
                let x = b.float(bits, x);
                (b.float_negate(x), Type::Float(bits))
            }));
            check(folded_case(|b| {
                let x = b.float(bits, x);
                let r = b.int_from_float(true, x, Bits::B32);
                (r, I32)
            }));
            check(folded_case(|b| {
                let x = b.float(bits, x);
                let other = match bits {
                    FloatBits::F32 => FloatBits::F64,
                    FloatBits::F64 => FloatBits::F32,
                };
                (b.float_resize(x, other), Type::Float(other))
            }));
            check(folded_case(|b| {
                let n = b.int(Bits::B64, x);
                (b.float_from_int(x & 1 == 1, n, bits), Type::Float(bits))
            }));
        }
    }
    assert!(count > 100);
}

/// A function of three floats with the instructions `make` adds.
fn of_three(make: impl Fn(&mut Builder, &[scalar::Value]) -> scalar::Value) -> Function {
    let mut b = Builder::new();
    let v = b.params(&[F64, F64, F64]);
    let r = make(&mut b, &v);
    b.ret(vec![r]);
    b.finish(&[F64])
}

fn the_passes_rewrite_no_float_that_is_not_known() {
    // Each of these is written so that a rewrite that looks harmless changes
    // the bits for some input: `x + 0.0` is not `x` when `x` is `-0.0`,
    // `x * 0.0` is not `0.0` when `x` is infinite or not a number, `x / x` is
    // not `1.0`, `x - x` is not `0.0`, and `(x * y) * z` is not `x * (y * z)`.
    let zero = 0.0f64.to_bits();
    let one_bits = 1.0f64.to_bits();
    let functions: Vec<Function> = vec![
        of_three(|b, v| {
            let z = b.float(FloatBits::F64, zero);
            b.float_binary(FloatBinary::Add, v[0], z)
        }),
        of_three(|b, v| {
            let z = b.float(FloatBits::F64, zero);
            b.float_binary(FloatBinary::Add, z, v[0])
        }),
        of_three(|b, v| {
            let z = b.float(FloatBits::F64, zero);
            b.float_binary(FloatBinary::Sub, v[0], z)
        }),
        of_three(|b, v| {
            let z = b.float(FloatBits::F64, zero);
            b.float_binary(FloatBinary::Sub, z, v[0])
        }),
        of_three(|b, v| {
            let o = b.float(FloatBits::F64, one_bits);
            b.float_binary(FloatBinary::Mul, v[0], o)
        }),
        of_three(|b, v| {
            let o = b.float(FloatBits::F64, one_bits);
            b.float_binary(FloatBinary::Div, v[0], o)
        }),
        of_three(|b, v| {
            let z = b.float(FloatBits::F64, zero);
            b.float_binary(FloatBinary::Mul, v[0], z)
        }),
        of_three(|b, v| b.float_binary(FloatBinary::Div, v[0], v[0])),
        of_three(|b, v| b.float_binary(FloatBinary::Sub, v[0], v[0])),
        of_three(|b, v| {
            let p = b.float_binary(FloatBinary::Mul, v[0], v[1]);
            b.float_binary(FloatBinary::Mul, p, v[2])
        }),
        of_three(|b, v| {
            let p = b.float_binary(FloatBinary::Mul, v[0], v[1]);
            b.float_binary(FloatBinary::Add, p, v[2])
        }),
        of_three(|b, v| {
            let n = b.float_negate(v[0]);
            b.float_negate(n)
        }),
        of_three(|b, v| {
            let p = b.float_binary(FloatBinary::Add, v[0], v[1]);
            b.float_binary(FloatBinary::Add, p, v[2])
        }),
    ];
    let inputs = edges(FloatBits::F64);
    for f in functions {
        let before = f.blocks[0].instructions.len();
        let mut worst = 0;
        for x in &inputs {
            for y in [inputs[0], inputs[1], inputs[2], inputs[4], inputs[7], inputs[25]] {
                let args = [(F64, *x), (F64, y), (F64, inputs[1])];
                let p = observed(vec![f.clone()], 0, &args, true);
                let want = scalar::run(&p);
                let passed = scalar::passes(&p);
                assert_eq!(scalar::check(&passed), Ok(()));
                assert_eq!(scalar::run(&passed), want, "{f:?} on {args:#x?}");
                worst = float_instructions(&passed);
            }
        }
        // Nothing is taken away: every instruction of floats that the function
        // has is still there once it is called from a function that does not
        // know the values.
        let float_ops = f.blocks[0]
            .instructions
            .iter()
            .filter(|i| !matches!(i.op, Op::Constant(_)))
            .count();
        assert_eq!(worst, float_ops, "an instruction of {f:?} is gone ({before})");
    }
}

fn every_pass_keeps_what_a_generated_function_of_floats_and_whole_numbers_gives() {
    generate::check(5134, 60, ff::program, |program| {
        // The form as it is made: a product that one sum uses is fused, and no
        // pass makes or takes apart a fused instruction.
        let program = &scalar::fuse(program);
        assert_eq!(scalar::check(program), Ok(()));
        let want = scalar::run(program);
        let mut p = program.clone();
        let stages: [(&str, Pass); 6] = [
            ("inline", scalar::inline),
            ("trim", scalar::trim),
            ("thread", scalar::thread),
            ("fold", scalar::fold),
            ("common", scalar::common),
            ("remove", |p| scalar::remove(p.clone())),
        ];
        for (name, stage) in stages {
            p = stage(&p);
            assert_eq!(scalar::check(&p), Ok(()), "not well formed after {name}");
            assert_eq!(scalar::run(&p), want, "the pass {name} changed what it gives");
        }
        assert_eq!(scalar::run(&scalar::passes(program)), want);
    });
}

/// A function made of one instruction, which the check says is refused for
/// the reason.
fn refused(ops: impl Fn(&mut Builder) -> scalar::Value, result: Type, why: impl Fn(&Why) -> bool) {
    let mut b = Builder::new();
    let r = ops(&mut b);
    b.ret(vec![r]);
    let f = b.finish(&[result]);
    let mut main = Builder::new();
    main.ret(vec![]);
    let program = Program {
        data: Vec::new(),
        functions: vec![f, main.finish(&[])],
        main: 1,
    };
    match scalar::check(&program) {
        Err(m) if why(&m.why) => {}
        other => panic!("expected a refusal, got {other:?}"),
    }
}

fn a_float_where_the_form_wants_another_type_is_refused() {
    let wrong = |w: &Why| matches!(w, Why::WrongType(_));
    // Two floats of different sizes.
    refused(
        |b| {
            let x = b.float(FloatBits::F32, 0);
            let y = b.float(FloatBits::F64, 0);
            b.emit(
                Op::FloatBinary {
                    op: FloatBinary::Add,
                    a: x,
                    b: y,
                },
                &[F32],
            )[0]
        },
        F32,
        wrong,
    );
    // Arithmetic of floats by an instruction of integers.
    refused(
        |b| {
            let x = b.float(FloatBits::F32, 0);
            b.binary(scalar::Binary::Add, x, x)
        },
        F32,
        wrong,
    );
    // A float of a whole number's instruction, and the other way.
    refused(
        |b| {
            let x = b.int(Bits::B32, 0);
            b.emit(Op::FloatNegate(x), &[I32])[0]
        },
        I32,
        wrong,
    );
    refused(
        |b| {
            let x = b.float(FloatBits::F32, 0);
            b.emit(
                Op::Compare {
                    op: scalar::Compare::Equal,
                    a: x,
                    b: x,
                },
                &[I8],
            )[0]
        },
        I8,
        wrong,
    );
    // A comparison of floats gives an integer of 8 bits.
    refused(
        |b| {
            let x = b.float(FloatBits::F64, 0);
            b.emit(
                Op::FloatCompare {
                    op: FloatCompare::Less,
                    a: x,
                    b: x,
                },
                &[I64],
            )[0]
        },
        I64,
        wrong,
    );
    // A resizing to the size it has.
    refused(
        |b| {
            let x = b.float(FloatBits::F64, 0);
            b.float_resize(x, FloatBits::F64)
        },
        F64,
        wrong,
    );
    // A constant that is a float, held by an integer.
    refused(
        |b| b.emit(Op::Constant(scalar::Constant::Float(0)), &[I64])[0],
        I64,
        wrong,
    );
}
