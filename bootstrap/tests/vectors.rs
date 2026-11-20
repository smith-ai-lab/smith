//! Vectors in the scalar form: the evaluator gives each instruction from the
//! scalar instruction of each lane, the passes work out what is known and
//! rewrite nothing else, and a function that does not use vectors the way the
//! form says is refused.

use smith::scalar::{
    self, Binary, Bits, Builder, Compare, FloatBinary, FloatBits, FloatCompare, Lane, Op, Program,
    Relation, Type, Why,
};
use smith::target::Target;

use crate::Test;
use crate::float_form as ff;
use crate::generate::{self, Gen};
use crate::vector_form::{
    self as vf, FLOAT_OPS, INT_OPS, I8, I64, lane_at, lane_mask, made, run_one, vec,
    vector_instructions, vector_value,
};

pub const ONCE: &[Test] = &[
    (
        "each_vector_instruction_gives_what_the_scalar_instruction_of_each_lane_gives",
        each_vector_instruction_gives_what_the_scalar_instruction_of_each_lane_gives,
    ),
    (
        "a_sum_of_lanes_adds_the_lanes_that_lie_next_to_each_other_first",
        a_sum_of_lanes_adds_the_lanes_that_lie_next_to_each_other_first,
    ),
    (
        "a_choice_by_a_mask_takes_each_bit_from_the_value_the_mask_names",
        a_choice_by_a_mask_takes_each_bit_from_the_value_the_mask_names,
    ),
    (
        "a_lane_is_spread_read_written_and_found_as_it_is_said",
        a_lane_is_spread_read_written_and_found_as_it_is_said,
    ),
    (
        "a_vector_is_kept_in_memory_with_its_lowest_lane_first",
        a_vector_is_kept_in_memory_with_its_lowest_lane_first,
    ),
    (
        "folding_gives_the_bits_the_evaluator_gives_for_vectors",
        folding_gives_the_bits_the_evaluator_gives_for_vectors,
    ),
    (
        "the_passes_rewrite_no_vector_that_is_not_known",
        the_passes_rewrite_no_vector_that_is_not_known,
    ),
    (
        "every_pass_keeps_what_a_generated_function_of_vectors_gives",
        every_pass_keeps_what_a_generated_function_of_vectors_gives,
    ),
    (
        "a_vector_where_the_form_wants_another_type_is_refused",
        a_vector_where_the_form_wants_another_type_is_refused,
    ),
    (
        "a_target_that_does_not_build_vectors_stops_with_one_error_that_names_the_function",
        a_target_that_does_not_build_vectors_stops_with_one_error_that_names_the_function,
    ),
];

/// A pass over a program.
type Pass = fn(&Program) -> Program;

/// What the scalar instruction `make` makes of the arguments gives, by the
/// evaluator, as bits.
fn scalar_gives(
    params: &[Type],
    result: Type,
    args: &[u64],
    make: impl Fn(&mut Builder, &[scalar::Value]) -> scalar::Value,
) -> u64 {
    let program = ff::one(params, result, make);
    scalar::call(&program, 0, args)[0]
}

/// The type of an integer lane, which the instruction of one lane works on.
fn scalar_of(lane: Lane) -> Type {
    lane.scalar()
}

/// A vector of the lane for each of the two pairs the generator makes.
fn two_vectors(g: &mut Gen) -> (Lane, u128, u128) {
    let lane = *g.pick(&Lane::ALL);
    let a = vector_value(g, lane);
    // Often the same lanes in another order or with a few changed, so that
    // lanes are often equal in a pair.
    let b = if g.below(3) == 0 {
        let mut b = a;
        for _ in 0..g.below(3) {
            let at = g.below(lane.count() as usize) as u32;
            let n = vf::lane_value(g, lane);
            b = made(lane, |i| if i == at { n } else { lane_at(lane, b, i) });
        }
        b
    } else {
        vector_value(g, lane)
    };
    (lane, a, b)
}

fn each_vector_instruction_gives_what_the_scalar_instruction_of_each_lane_gives() {
    generate::check(7101, 24, two_vectors, |&(lane, a, b)| {
        let v = vec(lane);
        let s = scalar_of(lane);
        let lanes = |v: u128| (0..lane.count()).map(move |i| lane_at(lane, v, i));
        let pairs: Vec<(u64, u64)> = lanes(a).zip(lanes(b)).collect();
        match lane {
            Lane::Int(bits) => {
                for op in INT_OPS {
                    let got = run_one(&[v, v], v, &[a, b], |bu, p| bu.vector_binary(op, p[0], p[1]));
                    let want = made(lane, |i| {
                        let (x, y) = pairs[i as usize];
                        scalar_gives(&[s, s], s, &[x, y], |bu, p| bu.binary(op, p[0], p[1]))
                    });
                    assert_eq!(got, want, "{op:?} of {bits:?}");
                }
                for op in Relation::ALL {
                    for signed in [false, true] {
                        let got = run_one(&[v, v], lane.mask(), &[a, b], |bu, p| {
                            bu.vector_compare(op, signed, p[0], p[1])
                        });
                        let want = made(lane, |i| {
                            let (x, y) = pairs[i as usize];
                            let less = if signed { Compare::LessS } else { Compare::LessU };
                            let is = |c: Compare, x: u64, y: u64| {
                                scalar_gives(&[s, s], I8, &[x, y], |bu, p| {
                                    bu.compare(c, p[0], p[1])
                                }) == 1
                            };
                            let holds = match op {
                                Relation::Equal => is(Compare::Equal, x, y),
                                Relation::NotEqual => !is(Compare::Equal, x, y),
                                Relation::Less => is(less, x, y),
                                Relation::LessEqual => !is(less, y, x),
                                Relation::Greater => is(less, y, x),
                                Relation::GreaterEqual => !is(less, x, y),
                            };
                            if holds { lane_mask(lane) } else { 0 }
                        });
                        assert_eq!(got, want, "{op:?} signed {signed} of {bits:?}");
                    }
                }
            }
            Lane::Float(bits) => {
                for op in FLOAT_OPS {
                    let got = run_one(&[v, v], v, &[a, b], |bu, p| {
                        bu.vector_float_binary(op, p[0], p[1])
                    });
                    let want = made(lane, |i| {
                        let (x, y) = pairs[i as usize];
                        scalar_gives(&[s, s], s, &[x, y], |bu, p| {
                            bu.float_binary(op, p[0], p[1])
                        })
                    });
                    assert_eq!(got, want, "{op:?} of {bits:?}");
                }
                for op in FloatCompare::ALL {
                    let got = run_one(&[v, v], lane.mask(), &[a, b], |bu, p| {
                        bu.vector_float_compare(op, p[0], p[1])
                    });
                    let want = made(lane, |i| {
                        let (x, y) = pairs[i as usize];
                        let holds = scalar_gives(&[s, s], I8, &[x, y], |bu, p| {
                            bu.float_compare(op, p[0], p[1])
                        });
                        if holds == 1 { lane_mask(lane) } else { 0 }
                    });
                    assert_eq!(got, want, "{op:?} of {bits:?}");
                }
            }
        }
    });
}

/// A vector of the lane and what the sum of its lanes must be, worked out by
/// the sum of the numbers the lanes stand for.
fn a_sum_of_lanes_adds_the_lanes_that_lie_next_to_each_other_first() {
    generate::check(
        7102,
        30,
        |g| {
            let lane = *g.pick(&Lane::ALL);
            (lane, vector_value(g, lane))
        },
        |&(lane, v)| {
            let got = run_one(&[vec(lane)], lane.scalar(), &[v], |b, p| b.vector_sum(p[0]));
            match lane {
                Lane::Int(bits) => {
                    let total: u128 = (0..lane.count()).map(|i| u128::from(lane_at(lane, v, i))).sum();
                    assert_eq!(got, total & u128::from(lane_mask(Lane::Int(bits))));
                }
                Lane::Float(FloatBits::F32) => {
                    let l = |i| f32::from_bits(lane_at(lane, v, i) as u32);
                    let want = (l(0) + l(1)) + (l(2) + l(3));
                    assert!(ff::same(FloatBits::F32, got as u64, u64::from(want.to_bits())));
                }
                Lane::Float(FloatBits::F64) => {
                    let l = |i| f64::from_bits(lane_at(lane, v, i));
                    let want = l(0) + l(1);
                    assert!(ff::same(FloatBits::F64, got as u64, want.to_bits()));
                }
            }
        },
    );
    // The order is the one the form says: a sum that is exact only in that order.
    let big = 16_777_216.0f32;
    let v = made(Lane::Float(FloatBits::F32), |i| {
        u64::from([big, 1.0, -big, 1.0][i as usize].to_bits())
    });
    let lane = Lane::Float(FloatBits::F32);
    let got = run_one(&[vec(lane)], lane.scalar(), &[v], |b, p| b.vector_sum(p[0]));
    assert_eq!(got as u64, u64::from(((big + 1.0) + (-big + 1.0)).to_bits()));
}

fn a_choice_by_a_mask_takes_each_bit_from_the_value_the_mask_names() {
    generate::check(
        7103,
        30,
        |g| {
            let lane = *g.pick(&Lane::ALL);
            let mask = made(lane, |_| 0) | u128::from(g.below(1 << 30) as u32) | (u128::from(g.below(1 << 30) as u32) << 64);
            (lane, mask, vector_value(g, lane), vector_value(g, lane))
        },
        |&(lane, mask, then, otherwise)| {
            let (v, m) = (vec(lane), lane.mask());
            // Any mask picks bit by bit.
            let got = run_one(&[m, v, v], v, &[mask, then, otherwise], |b, p| {
                b.vector_choose(p[0], p[1], p[2])
            });
            assert_eq!(got, (then & mask) | (otherwise & !mask));
            // A mask that a comparison gives picks lane by lane, as the choice of
            // the scalar form does on the lane of the comparison.
            let flags = made(lane, |i| if lane_at(lane, mask, i) & 1 == 1 { lane_mask(lane) } else { 0 });
            let got = run_one(&[m, v, v], v, &[flags, then, otherwise], |b, p| {
                b.vector_choose(p[0], p[1], p[2])
            });
            let s = lane.scalar();
            let want = made(lane, |i| {
                let on = u64::from(lane_at(lane, flags, i) != 0);
                let (x, y) = (lane_at(lane, then, i), lane_at(lane, otherwise, i));
                scalar_gives(&[I8, s, s], s, &[on, x, y], |b, p| b.choose(p[0], p[1], p[2]))
            });
            assert_eq!(got, want);
        },
    );
}

fn a_lane_is_spread_read_written_and_found_as_it_is_said() {
    generate::check(
        7104,
        30,
        |g| {
            let lane = *g.pick(&Lane::ALL);
            let at = g.below(lane.count() as usize) as u8;
            (lane, vector_value(g, lane), at, vf::lane_value(g, lane))
        },
        |&(lane, v, at, n)| {
            let (vt, s) = (vec(lane), lane.scalar());
            let spread = run_one(&[s], vt, &[u128::from(n)], |b, p| b.vector_splat(p[0]));
            assert_eq!(spread, made(lane, |_| n));
            let read = run_one(&[vt], s, &[v], |b, p| b.vector_extract(p[0], at));
            assert_eq!(read, u128::from(lane_at(lane, v, u32::from(at))));
            let written = run_one(&[vt, s], vt, &[v, u128::from(n)], |b, p| {
                b.vector_insert(p[0], at, p[1])
            });
            let want = made(lane, |i| if i == u32::from(at) { n } else { lane_at(lane, v, i) });
            assert_eq!(written, want);
            // The first lane a mask has set, or the number of lanes.
            if let Lane::Int(_) = lane {
                let found = run_one(&[vt], I64, &[v], |b, p| b.vector_first_set(p[0]));
                let want = (0..lane.count())
                    .find(|&i| lane_at(lane, v, i) >> (lane.bits() - 1) == 1)
                    .unwrap_or(lane.count());
                assert_eq!(found, u128::from(want));
                let none = run_one(&[vt], I64, &[0], |b, p| b.vector_first_set(p[0]));
                assert_eq!(none, u128::from(lane.count()));
            }
        },
    );
}

fn a_vector_is_kept_in_memory_with_its_lowest_lane_first() {
    // A vector takes 16 bytes, and lies at a multiple of 16 in an element.
    for lane in Lane::ALL {
        assert_eq!(vec(lane).bytes(), 16);
        assert_eq!(scalar::element(&[I8, vec(lane)]), (vec![0, 16], 32));
        assert_eq!(lane.count() * lane.bits(), 128);
    }
    generate::check(
        7105,
        20,
        |g| {
            let lane = *g.pick(&Lane::ALL);
            (lane, vector_value(g, lane))
        },
        |&(lane, v)| {
            // The bytes the evaluator writes for a vector, read back one by one.
            for at in 0..16u64 {
                let got = run_one(&[vec(lane)], I8, &[v], |b, p| {
                    let size = b.int(Bits::B64, 16);
                    let place = b.allocate(size);
                    b.store(place, p[0]);
                    let by = b.int(Bits::B64, at);
                    let byte = b.binary(Binary::Add, place, by);
                    b.load(I8, byte)
                });
                assert_eq!(got, (v >> (8 * at)) & 0xff, "byte {at}");
            }
        },
    );
}

/// A function of no parameter whose result is what `make` makes of constants.
fn folded_case(make: impl Fn(&mut Builder) -> (scalar::Value, Type)) -> Program {
    let mut b = Builder::new();
    let (r, ty) = make(&mut b);
    b.ret(vec![r]);
    let f = b.finish(&[ty]);
    vf::observed(vec![f], 0, &[], false)
}

fn folding_gives_the_bits_the_evaluator_gives_for_vectors() {
    generate::check(
        7106,
        24,
        two_vectors,
        |&(lane, x, y)| {
            let check = |program: Program| {
                let want = scalar::run(&program);
                let passed = scalar::passes(&program);
                assert_eq!(scalar::check(&passed), Ok(()));
                assert_eq!(scalar::run(&passed), want, "{program:?}");
                assert_eq!(
                    vector_instructions(&passed),
                    0,
                    "an instruction on constants is left in {passed:?}"
                );
            };
            let v = vec(lane);
            match lane {
                Lane::Int(_) => {
                    for op in INT_OPS {
                        check(folded_case(|b| {
                            let (p, q) = (b.vector(lane, x), b.vector(lane, y));
                            (b.vector_binary(op, p, q), v)
                        }));
                    }
                    check(folded_case(|b| {
                        let (p, q) = (b.vector(lane, x), b.vector(lane, y));
                        (b.vector_compare(Relation::Less, true, p, q), lane.mask())
                    }));
                    check(folded_case(|b| {
                        let p = b.vector(lane, x);
                        (b.vector_first_set(p), I64)
                    }));
                }
                Lane::Float(_) => {
                    for op in FLOAT_OPS {
                        check(folded_case(|b| {
                            let (p, q) = (b.vector(lane, x), b.vector(lane, y));
                            (b.vector_float_binary(op, p, q), v)
                        }));
                    }
                    check(folded_case(|b| {
                        let (p, q) = (b.vector(lane, x), b.vector(lane, y));
                        (b.vector_float_compare(FloatCompare::LessEqual, p, q), lane.mask())
                    }));
                }
            }
            check(folded_case(|b| {
                let (m, p, q) = (b.vector(mask_lane(lane), y), b.vector(lane, x), b.vector(lane, y));
                (b.vector_choose(m, p, q), v)
            }));
            check(folded_case(|b| {
                let p = b.vector(lane, x);
                (b.vector_sum(p), lane.scalar())
            }));
            check(folded_case(|b| {
                let p = b.vector(lane, x);
                (b.vector_extract(p, 1), lane.scalar())
            }));
            check(folded_case(|b| {
                let p = b.vector(lane, x);
                let n = b.vector_extract(p, 0);
                (b.vector_splat(n), v)
            }));
            check(folded_case(|b| {
                let (p, q) = (b.vector(lane, x), b.vector(lane, y));
                let n = b.vector_extract(q, 0);
                (b.vector_insert(p, 0, n), v)
            }));
        },
    );
}

/// The lane of the mask of the lane's vectors.
fn mask_lane(lane: Lane) -> Lane {
    match lane.mask() {
        Type::Vector(mask) => mask,
        _ => unreachable!("a mask is a vector"),
    }
}

/// A function of two vectors and a lane with the instructions `make` adds.
fn of_vectors(
    lane: Lane,
    make: impl Fn(&mut Builder, &[scalar::Value]) -> scalar::Value,
) -> scalar::Function {
    let mut b = Builder::new();
    let v = b.params(&[vec(lane), vec(lane), lane.scalar()]);
    let r = make(&mut b, &v);
    let result = b.type_of(r);
    b.ret(vec![r]);
    b.finish(&[result])
}

fn the_passes_rewrite_no_vector_that_is_not_known() {
    // Each of these is another vector for some input, so each stays: no sum with
    // 0 or product with 1 is dropped, no vector is taken from itself or put
    // together with itself, and no product and sum are made one instruction.
    type Make = Box<dyn Fn(&mut Builder, &[scalar::Value]) -> scalar::Value>;
    let cases: Vec<(&str, Lane, Make)> = vec![
        (
            "x + 0",
            Lane::Int(Bits::B32),
            Box::new(|b, p| {
                let zero = b.vector(Lane::Int(Bits::B32), 0);
                b.vector_binary(Binary::Add, p[0], zero)
            }),
        ),
        (
            "x * 1",
            Lane::Int(Bits::B32),
            Box::new(|b, p| {
                let one = b.int(Bits::B32, 1);
                let one = b.vector_splat(one);
                b.vector_binary(Binary::Mul, p[0], one)
            }),
        ),
        (
            "x - x",
            Lane::Int(Bits::B8),
            Box::new(|b, p| b.vector_binary(Binary::Sub, p[0], p[0])),
        ),
        (
            "x ^ x",
            Lane::Int(Bits::B16),
            Box::new(|b, p| b.vector_binary(Binary::Xor, p[0], p[0])),
        ),
        (
            "x + 0.0",
            Lane::Float(FloatBits::F32),
            Box::new(|b, p| {
                let zero = b.vector(Lane::Float(FloatBits::F32), 0);
                b.vector_float_binary(FloatBinary::Add, p[0], zero)
            }),
        ),
        (
            "x * 1.0",
            Lane::Float(FloatBits::F64),
            Box::new(|b, p| {
                let one = b.float(FloatBits::F64, 1.0f64.to_bits());
                let one = b.vector_splat(one);
                b.vector_float_binary(FloatBinary::Mul, p[0], one)
            }),
        ),
        (
            "x * y + x",
            Lane::Float(FloatBits::F32),
            Box::new(|b, p| {
                let product = b.vector_float_binary(FloatBinary::Mul, p[0], p[1]);
                b.vector_float_binary(FloatBinary::Add, product, p[0])
            }),
        ),
        (
            "x == x",
            Lane::Float(FloatBits::F64),
            Box::new(|b, p| b.vector_float_compare(FloatCompare::Equal, p[0], p[0])),
        ),
        (
            "a choice of x and y by x",
            Lane::Int(Bits::B8),
            Box::new(|b, p| b.vector_choose(p[0], p[0], p[1])),
        ),
    ];
    for (name, lane, make) in &cases {
        let f = of_vectors(*lane, make);
        let wanted = f.blocks[0]
            .instructions
            .iter()
            .filter(|i| !matches!(i.op, Op::Constant(_) | Op::VectorSplat(_)))
            .count();
        for case in 0..6 {
            let args = vec![
                (vec(*lane), generate::sample(7108, case, |g| vector_value(g, *lane))),
                (vec(*lane), generate::sample(7109, case, |g| vector_value(g, *lane))),
                (lane.scalar(), generate::sample(7110, case, |g| u128::from(vf::lane_value(g, *lane)))),
            ];
            let p = vf::observed(vec![f.clone()], 0, &args, true);
            let want = scalar::run(&p);
            let passed = scalar::passes(&p);
            assert_eq!(scalar::check(&passed), Ok(()), "{name}");
            assert_eq!(scalar::run(&passed), want, "{name}");
            let left = vector_instructions(&passed)
                - passed
                    .functions
                    .iter()
                    .flat_map(|f| &f.blocks)
                    .flat_map(|b| &b.instructions)
                    .filter(|i| matches!(i.op, Op::VectorSplat(_)))
                    .count();
            assert_eq!(left, wanted, "{name}: an instruction is gone");
        }
    }
}

fn every_pass_keeps_what_a_generated_function_of_vectors_gives() {
    generate::check(7107, 40, vf::program, |program| {
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

/// A function made of one instruction, which the check says is refused for the
/// reason.
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

fn a_vector_where_the_form_wants_another_type_is_refused() {
    let wrong = |w: &Why| matches!(w, Why::WrongType(_));
    let (i32x4, f32x4) = (Lane::Int(Bits::B32), Lane::Float(FloatBits::F32));
    // Two vectors of lanes of different sizes.
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            let y = b.vector(Lane::Int(Bits::B16), 0);
            b.emit(Op::VectorBinary { op: Binary::Add, a: x, b: y }, &[vec(i32x4)])[0]
        },
        vec(i32x4),
        wrong,
    );
    // Arithmetic of integers on lanes of floats, and the other way.
    refused(
        |b| {
            let x = b.vector(f32x4, 0);
            b.emit(Op::VectorBinary { op: Binary::Add, a: x, b: x }, &[vec(f32x4)])[0]
        },
        vec(f32x4),
        wrong,
    );
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            b.emit(
                Op::VectorFloatBinary { op: FloatBinary::Add, a: x, b: x },
                &[vec(i32x4)],
            )[0]
        },
        vec(i32x4),
        wrong,
    );
    // An operation the form does not have for lanes.
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            b.emit(Op::VectorBinary { op: Binary::ShiftLeft, a: x, b: x }, &[vec(i32x4)])[0]
        },
        vec(i32x4),
        wrong,
    );
    refused(
        |b| {
            let x = b.vector(f32x4, 0);
            b.emit(
                Op::VectorFloatBinary { op: FloatBinary::Div, a: x, b: x },
                &[vec(f32x4)],
            )[0]
        },
        vec(f32x4),
        wrong,
    );
    // A mask that is not as wide as the lanes it chooses between.
    refused(
        |b| {
            let m = b.vector(Lane::Int(Bits::B16), 0);
            let x = b.vector(i32x4, 0);
            b.emit(
                Op::VectorChoose { mask: m, then: x, otherwise: x },
                &[vec(i32x4)],
            )[0]
        },
        vec(i32x4),
        wrong,
    );
    // A comparison that does not give a mask of the width of its lanes.
    refused(
        |b| {
            let x = b.vector(f32x4, 0);
            b.emit(
                Op::VectorFloatCompare { op: FloatCompare::Equal, a: x, b: x },
                &[vec(Lane::Int(Bits::B64))],
            )[0]
        },
        vec(Lane::Int(Bits::B64)),
        wrong,
    );
    // A lane that a vector has not.
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            b.emit(Op::VectorExtract { vector: x, lane: 4 }, &[Type::Int(Bits::B32)])[0]
        },
        Type::Int(Bits::B32),
        wrong,
    );
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            let n = b.int(Bits::B32, 0);
            b.emit(
                Op::VectorInsert { vector: x, lane: 9, value: n },
                &[vec(i32x4)],
            )[0]
        },
        vec(i32x4),
        wrong,
    );
    // A lane of another type than the vector's, spread or written.
    refused(
        |b| {
            let n = b.int(Bits::B8, 0);
            b.emit(Op::VectorSplat(n), &[vec(i32x4)])[0]
        },
        vec(i32x4),
        wrong,
    );
    // The first lane set of a vector of floats.
    refused(
        |b| {
            let x = b.vector(f32x4, 0);
            b.emit(Op::VectorFirstSet(x), &[I64])[0]
        },
        I64,
        wrong,
    );
    // A vector where an integer is read.
    refused(
        |b| {
            let x = b.vector(i32x4, 0);
            b.binary(Binary::Add, x, x)
        },
        vec(i32x4),
        wrong,
    );
}

fn a_target_that_does_not_build_vectors_stops_with_one_error_that_names_the_function() {
    let mut b = Builder::new();
    b.ret(vec![]);
    let plain = b.finish(&[]);
    let mut b = Builder::new();
    let v = b.vector(Lane::Int(Bits::B8), 0);
    b.ret(vec![v]);
    let with_vector = b.finish(&[vec(Lane::Int(Bits::B8))]);
    let mut b = Builder::new();
    b.ret(vec![]);
    let main = b.finish(&[]);
    // The function with the vector is the second.
    let program = Program {
        data: Vec::new(),
        functions: vec![plain, with_vector, main],
        main: 2,
    };
    for target in Target::ALL.iter().filter(|t| !t.builds_vectors()) {
        let said = std::panic::catch_unwind(|| target.emit(&program))
            .expect_err("a program with a vector in it");
        let said = said
            .downcast_ref::<String>()
            .cloned()
            .or_else(|| said.downcast_ref::<&str>().map(|s| (*s).to_string()))
            .expect("a message");
        assert!(said.contains("vector"), "{said}");
        assert!(said.contains("function 1"), "{said}");
        assert!(said.contains(target.name()), "{said}");
    }
}
