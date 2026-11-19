//! What the tests of floats in the scalar form share: the values that are the
//! edges of each format, the answers worked out apart from the evaluator, and
//! the functions of the form that the generators build.

use smith::scalar::{
    self, Bits, Builder, FloatBinary, FloatBits, FloatCompare, Function, Op, Program,
    Stream, System, Type, Value,
};

use crate::generate::Gen;

pub const F32: Type = Type::Float(FloatBits::F32);
pub const F64: Type = Type::Float(FloatBits::F64);
pub const I8: Type = Type::Int(Bits::B8);
pub const I32: Type = Type::Int(Bits::B32);
pub const I64: Type = Type::Int(Bits::B64);

pub const BINARY: [FloatBinary; 4] = [
    FloatBinary::Add,
    FloatBinary::Sub,
    FloatBinary::Mul,
    FloatBinary::Div,
];

pub fn float_bits(ty: Type) -> FloatBits {
    match ty {
        Type::Float(bits) => bits,
        _ => panic!("not a float"),
    }
}

/// The bits of the numbers at the edges of a float of the size: the zeros of
/// both signs, the infinities, numbers that are not numbers of several kinds,
/// the least and the greatest, and numbers whose fractions round.
pub fn edges(bits: FloatBits) -> Vec<u64> {
    match bits {
        FloatBits::F32 => [
            0.0f32,
            -0.0,
            1.0,
            -1.0,
            0.5,
            1.5,
            2.5,
            -0.5,
            3.0,
            127.5,
            -128.5,
            255.5,
            65535.5,
            16_777_216.0,
            16_777_218.0,
            2_147_483_648.0,
            4_294_967_296.0,
            9.3e18,
            -9.3e18,
            1.9e19,
            f32::MAX,
            f32::MIN,
            f32::MIN_POSITIVE,
            1e-45,
            f32::INFINITY,
            f32::NEG_INFINITY,
            f32::NAN,
        ]
        .iter()
        .map(|x| u64::from(x.to_bits()))
        .chain([0xffc0_0001, 0x7f80_0001, 0x7fff_ffff])
        .collect(),
        FloatBits::F64 => [
            0.0f64,
            -0.0,
            1.0,
            -1.0,
            0.5,
            1.5,
            2.5,
            -0.5,
            3.0,
            127.5,
            -128.5,
            255.5,
            65535.5,
            4_294_967_296.5,
            9_007_199_254_740_992.0,
            9_223_372_036_854_775_808.0,
            -9_223_372_036_854_775_808.0,
            9.3e18,
            -9.3e18,
            1.844_674_407_370_955_2e19,
            1e300,
            f64::MAX,
            f64::MIN,
            f64::MIN_POSITIVE,
            5e-324,
            f64::INFINITY,
            f64::NEG_INFINITY,
            f64::NAN,
        ]
        .iter()
        .map(|x| x.to_bits())
        .chain([0xfff8_0000_0000_0001, 0x7ff0_0000_0000_0001])
        .collect(),
    }
}

/// A float of the size: one of the edges, or any bits.
pub fn float(g: &mut Gen, bits: FloatBits) -> u64 {
    let edges = edges(bits);
    if g.below(2) == 0 {
        edges[g.below(edges.len())]
    } else {
        let n = (g.below(1 << 32) as u64) << 32 | g.below(1 << 32) as u64;
        match bits {
            FloatBits::F32 => n & u64::from(u32::MAX),
            FloatBits::F64 => n,
        }
    }
}

/// An integer of the size, as bits: one of the numbers at the edges of its
/// size, or any bits.
pub fn int(g: &mut Gen, bits: Bits) -> u64 {
    let size = bits.count();
    let mask = if size == 64 { u64::MAX } else { (1 << size) - 1 };
    let edge = [0, 1, mask, mask >> 1, (mask >> 1) + 1, 1 << 24 | 1, 1 << 53 | 1];
    if g.below(2) == 0 {
        edge[g.below(edge.len())] & mask
    } else {
        ((g.below(1 << 32) as u64) << 32 | g.below(1 << 32) as u64) & mask
    }
}

pub fn is_nan(bits: FloatBits, n: u64) -> bool {
    match bits {
        FloatBits::F32 => f32::from_bits(n as u32).is_nan(),
        FloatBits::F64 => f64::from_bits(n).is_nan(),
    }
}

/// The one float of the size that a result which is not a number is.
pub fn quiet_nan(bits: FloatBits) -> u64 {
    match bits {
        FloatBits::F32 => 0x7fc0_0000,
        FloatBits::F64 => 0x7ff8_0000_0000_0000,
    }
}

/// Whether two floats are the same, a number that is not a number being the
/// same as any other, since its sign and its payload are the chip's.
pub fn same(bits: FloatBits, a: u64, b: u64) -> bool {
    a == b || (is_nan(bits, a) && is_nan(bits, b))
}

/// The float as a number of 64 bits, which holds one of 32 exactly.
pub fn value(bits: FloatBits, n: u64) -> f64 {
    match bits {
        FloatBits::F32 => f64::from(f32::from_bits(n as u32)),
        FloatBits::F64 => f64::from_bits(n),
    }
}

/// What IEEE 754 gives for `a op b`. For a float of 32 bits it is worked out
/// in 64, where each of the four is exact enough that rounding once more to 32
/// gives what one rounding does.
pub fn binary(op: FloatBinary, bits: FloatBits, x: u64, y: u64) -> u64 {
    let (a, b) = (value(bits, x), value(bits, y));
    let r = match op {
        FloatBinary::Add => a + b,
        FloatBinary::Sub => a - b,
        FloatBinary::Mul => a * b,
        FloatBinary::Div => a / b,
    };
    match bits {
        FloatBits::F32 => u64::from((r as f32).to_bits()),
        FloatBits::F64 => r.to_bits(),
    }
}

/// What the six comparisons say, by the order of the two numbers, which does
/// not exist when one is not a number.
pub fn compare(op: FloatCompare, bits: FloatBits, x: u64, y: u64) -> u64 {
    use std::cmp::Ordering::{Equal, Greater, Less};
    let order = value(bits, x).partial_cmp(&value(bits, y));
    u64::from(match op {
        FloatCompare::Equal => order == Some(Equal),
        FloatCompare::NotEqual => order != Some(Equal),
        FloatCompare::Less => order == Some(Less),
        FloatCompare::LessEqual => matches!(order, Some(Less | Equal)),
        FloatCompare::Greater => order == Some(Greater),
        FloatCompare::GreaterEqual => matches!(order, Some(Greater | Equal)),
    })
}

/// The integer of `bits` bits that the float is cut to, as its bits: the float
/// without its fraction when that fits, the least or the greatest integer when
/// it does not, and 0 when it is not a number.
pub fn cut(signed: bool, from: FloatBits, to: Bits, x: u64) -> u64 {
    let v = value(from, x);
    if v.is_nan() {
        return 0;
    }
    let n = v.trunc() as i128;
    let size = to.count();
    let (least, greatest) = if signed {
        (-(1i128 << (size - 1)), (1i128 << (size - 1)) - 1)
    } else {
        (0, (1i128 << size) - 1)
    };
    let n = n.clamp(least, greatest);
    (n as u64) & if size == 64 { u64::MAX } else { (1 << size) - 1 }
}

/// The float of the size nearest the integer `n`, rounded once, ties to even,
/// worked out by hand on the bits of the number.
pub fn nearest(to: FloatBits, n: i128) -> u64 {
    let mantissa = match to {
        FloatBits::F32 => 24,
        FloatBits::F64 => 53,
    };
    let magnitude = n.unsigned_abs();
    let width = 128 - magnitude.leading_zeros();
    let v = if magnitude == 0 {
        0.0
    } else if width <= mantissa {
        magnitude as f64
    } else {
        let shift = width - mantissa;
        let mut kept = magnitude >> shift;
        let lost = magnitude & ((1u128 << shift) - 1);
        let half = 1u128 << (shift - 1);
        if lost > half || (lost == half && kept & 1 == 1) {
            kept += 1;
        }
        kept as f64 * 2f64.powi(shift as i32)
    };
    let v = if n < 0 { -v } else { v };
    match to {
        FloatBits::F32 => u64::from((v as f32).to_bits()),
        FloatBits::F64 => v.to_bits(),
    }
}

/// The integer of `from` bits as the number it stands for.
pub fn number(signed: bool, from: Bits, n: u64) -> i128 {
    let size = from.count();
    if signed {
        i128::from(((n << (64 - size)) as i64) >> (64 - size))
    } else {
        i128::from(n)
    }
}

/// A program whose `main` calls the function `f` with the arguments, and
/// writes what it gives to standard output, each result in 8 bytes. When
/// `opaque` the arguments go through memory first, so that no pass knows them.
pub fn observed(functions: Vec<Function>, f: usize, args: &[(Type, u64)], opaque: bool) -> Program {
    observe(functions, f, args, opaque, false)
}

/// As `observed`, except that a float that is not a number is written as the one
/// float that the evaluator gives, so that what an executable writes is what the
/// evaluator writes, though the sign and the payload of such a float are the
/// chip's.
pub fn observed_canonically(
    functions: Vec<Function>,
    f: usize,
    args: &[(Type, u64)],
    opaque: bool,
) -> Program {
    observe(functions, f, args, opaque, true)
}

/// The float, or the one float that is not a number when it is not a number.
fn canonical(b: &mut Builder, v: Value) -> Value {
    let Type::Float(bits) = b.type_of(v) else {
        return v;
    };
    let not_a_number = b.float_compare(FloatCompare::NotEqual, v, v);
    let nan = b.float(bits, quiet_nan(bits));
    b.choose(not_a_number, nan, v)
}

fn observe(
    mut functions: Vec<Function>,
    f: usize,
    args: &[(Type, u64)],
    opaque: bool,
    canonically: bool,
) -> Program {
    let results = functions[f].results.clone();
    let mut b = Builder::new();
    let at = |b: &mut Builder, buffer: Value, k: usize| {
        let by = b.int(Bits::B64, 8 * k as u64);
        b.binary(scalar::Binary::Add, buffer, by)
    };
    let size = b.int(Bits::B64, 8 * (args.len() + results.len() + 1) as u64);
    let buffer = b.allocate(size);
    let mut values = Vec::new();
    for (k, (ty, n)) in args.iter().enumerate() {
        let v = match ty {
            Type::Float(bits) => b.float(*bits, *n),
            Type::Int(bits) => b.int(*bits, *n),
            Type::Address => panic!("an argument that is an address"),
        };
        if opaque {
            let place = at(&mut b, buffer, k);
            b.store(place, v);
            values.push(b.load(*ty, place));
        } else {
            values.push(v);
        }
    }
    let given = b.call(f, values, &results);
    let out = at(&mut b, buffer, args.len());
    for (k, v) in given.iter().enumerate() {
        let place = at(&mut b, out, k);
        let v = if canonically { canonical(&mut b, *v) } else { *v };
        b.store(place, v);
    }
    let count = b.int(Bits::B64, 8 * results.len() as u64);
    b.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![out, count],
        },
        &[],
    );
    b.ret(vec![]);
    functions.push(b.finish(&[]));
    Program {
        data: Vec::new(),
        main: functions.len() - 1,
        functions,
    }
}

/// A function of one instruction that gives what `make` makes of its
/// parameters.
pub fn one(params: &[Type], result: Type, make: impl Fn(&mut Builder, &[Value]) -> Value) -> Program {
    let mut b = Builder::new();
    let values = b.params(params);
    let r = make(&mut b, &values);
    b.ret(vec![r]);
    let f = b.finish(&[result]);
    let mut empty = Builder::new();
    empty.ret(vec![]);
    Program {
        data: Vec::new(),
        functions: vec![f, empty.finish(&[])],
        main: 1,
    }
}

/// How many instructions of floats the program has, of every kind but a
/// constant.
pub fn float_instructions(program: &Program) -> usize {
    program
        .functions
        .iter()
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .filter(|i| {
            matches!(
                i.op,
                Op::FloatBinary { .. }
                    | Op::FloatNegate(_)
                    | Op::FloatCompare { .. }
                    | Op::FloatFromInt { .. }
                    | Op::IntFromFloat { .. }
                    | Op::FloatResize(_)
            )
        })
        .count()
}

/// The values of each type that a function being generated can read.
#[derive(Clone, Default)]
struct Pools {
    f32: Vec<Value>,
    f64: Vec<Value>,
    i32: Vec<Value>,
    i64: Vec<Value>,
    flags: Vec<Value>,
}

impl Pools {
    fn of(params: &[Value], types: &[Type]) -> Pools {
        let mut p = Pools::default();
        for (v, ty) in params.iter().zip(types) {
            p.add(*ty, *v);
        }
        p
    }

    fn add(&mut self, ty: Type, v: Value) {
        match ty {
            Type::Float(FloatBits::F32) => self.f32.push(v),
            Type::Float(FloatBits::F64) => self.f64.push(v),
            Type::Int(Bits::B32) => self.i32.push(v),
            Type::Int(Bits::B64) => self.i64.push(v),
            Type::Int(Bits::B8) => self.flags.push(v),
            _ => {}
        }
    }
}

fn last(values: &[Value], g: &mut Gen) -> Value {
    values[values.len() - 1 - g.below(values.len().min(3))]
}

/// The types of the parameters of the function the generated functions call.
pub const HELPER: [Type; 3] = [F64, F32, I64];

/// Adds one instruction, chosen by the generator, to the block being filled.
fn step(b: &mut Builder, g: &mut Gen, p: &mut Pools, helper: Option<usize>) {
    match g.below(12) {
        0 => {
            let (x, y) = (last(&p.f64, g), last(&p.f64, g));
            let r = b.float_binary(*g.pick(&BINARY), x, y);
            p.f64.push(r);
        }
        1 => {
            let (x, y) = (last(&p.f32, g), last(&p.f32, g));
            let r = b.float_binary(*g.pick(&BINARY), x, y);
            p.f32.push(r);
        }
        2 => {
            if g.below(2) == 0 {
                let r = b.float_negate(last(&p.f64, g));
                p.f64.push(r);
            } else {
                let r = b.float_negate(last(&p.f32, g));
                p.f32.push(r);
            }
        }
        3 => {
            let op = *g.pick(&FloatCompare::ALL);
            let r = if g.below(2) == 0 {
                let (x, y) = (last(&p.f64, g), last(&p.f64, g));
                b.float_compare(op, x, y)
            } else {
                let (x, y) = (last(&p.f32, g), last(&p.f32, g));
                b.float_compare(op, x, y)
            };
            p.flags.push(r);
        }
        4 if !p.flags.is_empty() => {
            let on = last(&p.flags, g);
            if g.below(2) == 0 {
                let (x, y) = (last(&p.f64, g), last(&p.f64, g));
                let r = b.choose(on, x, y);
                p.f64.push(r);
            } else {
                let (x, y) = (last(&p.f32, g), last(&p.f32, g));
                let r = b.choose(on, x, y);
                p.f32.push(r);
            }
        }
        5 => {
            let signed = g.below(2) == 0;
            let to = if g.below(2) == 0 {
                FloatBits::F32
            } else {
                FloatBits::F64
            };
            let from = if g.below(2) == 0 {
                last(&p.i64, g)
            } else {
                last(&p.i32, g)
            };
            let r = b.float_from_int(signed, from, to);
            match to {
                FloatBits::F32 => p.f32.push(r),
                FloatBits::F64 => p.f64.push(r),
            }
        }
        6 => {
            let signed = g.below(2) == 0;
            let from = if g.below(2) == 0 {
                last(&p.f64, g)
            } else {
                last(&p.f32, g)
            };
            match g.below(3) {
                0 => {
                    let r = b.int_from_float(signed, from, Bits::B32);
                    p.i32.push(r);
                }
                1 => {
                    let r = b.int_from_float(signed, from, Bits::B64);
                    p.i64.push(r);
                }
                _ => {
                    let small = b.int_from_float(signed, from, Bits::B8);
                    let r = b.widen(signed, small, Bits::B64);
                    p.i64.push(r);
                }
            }
        }
        7 => {
            if g.below(2) == 0 {
                let r = b.float_resize(last(&p.f32, g), FloatBits::F64);
                p.f64.push(r);
            } else {
                let r = b.float_resize(last(&p.f64, g), FloatBits::F32);
                p.f32.push(r);
            }
        }
        8 => {
            if g.below(2) == 0 {
                let n = float(g, FloatBits::F64);
                let r = b.float(FloatBits::F64, n);
                p.f64.push(r);
            } else {
                let n = float(g, FloatBits::F32);
                let r = b.float(FloatBits::F32, n);
                p.f32.push(r);
            }
        }
        9 => {
            if g.below(2) == 0 {
                let (x, y) = (last(&p.i64, g), last(&p.i64, g));
                let r = b.binary(*g.pick(&[scalar::Binary::Add, scalar::Binary::Mul]), x, y);
                p.i64.push(r);
            } else {
                let (x, y) = (last(&p.i32, g), last(&p.i32, g));
                let r = b.binary(*g.pick(&[scalar::Binary::Add, scalar::Binary::Mul]), x, y);
                p.i32.push(r);
            }
        }
        10 => {
            if let Some(callee) = helper {
                let args = vec![last(&p.f64, g), last(&p.f32, g), last(&p.i64, g)];
                let got = b.call(callee, args, &[F64, F32]);
                p.f64.push(got[0]);
                p.f32.push(got[1]);
            }
        }
        _ => {
            let r = b.narrow(last(&p.i64, g), Bits::B32);
            p.i32.push(r);
        }
    }
}

const PARAMS: [Type; 6] = [F64, F64, F32, F32, I64, I32];
const RESULTS: [Type; 4] = [F64, F32, I64, I32];

fn finish(p: &Pools) -> Vec<Value> {
    vec![
        *p.f64.last().unwrap(),
        *p.f32.last().unwrap(),
        *p.i64.last().unwrap(),
        *p.i32.last().unwrap(),
    ]
}

/// A function of floats and integers that calls nothing: it takes a float of
/// 64 bits, one of 32, and a whole number, and gives two floats.
fn helper(g: &mut Gen) -> Function {
    let mut b = Builder::new();
    let params = b.params(&HELPER);
    let mut p = Pools::of(&params, &HELPER);
    p.i32.push(b.narrow(params[2], Bits::B32));
    for _ in 0..g.below(6) {
        step(&mut b, g, &mut p, None);
    }
    b.ret(vec![*p.f64.last().unwrap(), *p.f32.last().unwrap()]);
    b.finish(&[F64, F32])
}

/// A generated function of the parameters `PARAMS` whose results are `RESULTS`:
/// straight, or with a choice between two blocks that each work on floats and
/// whole numbers, or with a loop that runs up to 7 rounds with floats carried
/// from one to the next, any of them calling the helper.
fn body(g: &mut Gen, called: Option<usize>) -> Function {
    let mut b = Builder::new();
    let params = b.params(&PARAMS);
    let mut p = Pools::of(&params, &PARAMS);
    for _ in 0..g.below(6) {
        step(&mut b, g, &mut p, called);
    }
    match g.below(3) {
        0 => {
            for _ in 0..g.below(6) {
                step(&mut b, g, &mut p, called);
            }
            let r = finish(&p);
            b.ret(r);
        }
        1 => {
            let on = if g.below(2) == 0 {
                let op = *g.pick(&FloatCompare::ALL);
                let (x, y) = (last(&p.f64, g), last(&p.f64, g));
                b.float_compare(op, x, y)
            } else {
                let op = *g.pick(&FloatCompare::ALL);
                let (x, y) = (last(&p.f32, g), last(&p.f32, g));
                b.float_compare(op, x, y)
            };
            let (join, joined) = b.block(&RESULTS);
            let (first, _) = b.block(&[]);
            let (second, _) = b.block(&[]);
            b.switch(on, vec![(second, vec![]), (first, vec![])]);
            for arm in [first, second] {
                b.at(arm);
                let mut q = p.clone();
                for _ in 0..g.below(6) {
                    step(&mut b, g, &mut q, called);
                }
                let r = finish(&q);
                b.jump(join, r);
            }
            b.at(join);
            b.ret(joined);
        }
        _ => {
            let seven = b.int(Bits::B64, 7);
            let bound = b.binary(scalar::Binary::And, last(&p.i64, g), seven);
            let zero = b.int(Bits::B64, 0);
            let (head, head_params) = b.block(&[I64, F64, F32]);
            let (round, _) = b.block(&[]);
            let (exit, exit_params) = b.block(&[F64, F32]);
            let (a64, a32) = (last(&p.f64, g), last(&p.f32, g));
            b.jump(head, vec![zero, a64, a32]);
            b.at(head);
            let go = b.compare(scalar::Compare::LessU, head_params[0], bound);
            b.switch(
                go,
                vec![(exit, vec![head_params[1], head_params[2]]), (round, vec![])],
            );
            b.at(round);
            let mut q = p.clone();
            q.i64.push(head_params[0]);
            q.f64.push(head_params[1]);
            q.f32.push(head_params[2]);
            for _ in 0..g.below(6) {
                step(&mut b, g, &mut q, called);
            }
            let one = b.int(Bits::B64, 1);
            let next = b.binary(scalar::Binary::Add, head_params[0], one);
            let (n64, n32) = (*q.f64.last().unwrap(), *q.f32.last().unwrap());
            b.jump(head, vec![next, n64, n32]);
            b.at(exit);
            let mut q = p.clone();
            q.f64.push(exit_params[0]);
            q.f32.push(exit_params[1]);
            let r = finish(&q);
            b.ret(r);
        }
    }
    b.finish(&RESULTS)
}

/// A program of generated functions: `main` calls one of them, which calls
/// another, with arguments the generator draws, and writes what it gives.
pub fn program(g: &mut Gen) -> Program {
    generated(g, false)
}

/// A program as `program` makes, which writes a float that is not a number as the
/// one the evaluator gives.
pub fn canonical_program(g: &mut Gen) -> Program {
    generated(g, true)
}

fn generated(g: &mut Gen, canonically: bool) -> Program {
    let called = helper(g);
    let top = body(g, Some(0));
    let args: Vec<(Type, u64)> = PARAMS
        .iter()
        .map(|ty| match ty {
            Type::Float(bits) => (*ty, float(g, *bits)),
            Type::Int(bits) => (*ty, int(g, *bits)),
            Type::Address => unreachable!(),
        })
        .collect();
    let opaque = g.below(2) == 0;
    observe(vec![called, top], 1, &args, opaque, canonically)
}

/// A program that calls the function `f`, which takes values of the types, once
/// for each row, with the bits of the row as its arguments, which it reads from
/// the program's data, and writes what each call gives, floats that are not
/// numbers as one, each result in 8 bytes. A pass cannot know what a row holds,
/// so each call is made as a function of the form is, with its arguments in
/// registers.
pub fn table(f: Function, types: &[Type], rows: &[Vec<u64>]) -> Program {
    let results = f.results.clone();
    let mut data = Vec::new();
    for row in rows {
        for n in row {
            data.extend_from_slice(&n.to_le_bytes());
        }
    }
    let mut b = Builder::new();
    let table = b.emit(
        Op::Constant(scalar::Constant::Data(0)),
        &[Type::Address],
    )[0];
    let total = b.int(Bits::B64, (8 * results.len() * rows.len()) as u64);
    let out = b.allocate(total);
    let zero = b.int(Bits::B64, 0);
    let (head, head_params) = b.block(&[I64]);
    let (round, _) = b.block(&[]);
    let (exit, _) = b.block(&[]);
    b.jump(head, vec![zero]);
    b.at(head);
    let count = b.int(Bits::B64, rows.len() as u64);
    let go = b.compare(scalar::Compare::LessU, head_params[0], count);
    b.switch(go, vec![(exit, vec![]), (round, vec![])]);
    b.at(round);
    let i = head_params[0];
    let stride = b.int(Bits::B64, (8 * types.len()) as u64);
    let offset = b.binary(scalar::Binary::Mul, i, stride);
    let row = b.binary(scalar::Binary::Add, table, offset);
    let mut args = Vec::new();
    for (k, ty) in types.iter().enumerate() {
        let by = b.int(Bits::B64, 8 * k as u64);
        let at = b.binary(scalar::Binary::Add, row, by);
        args.push(b.load(*ty, at));
    }
    let given = b.call(0, args, &results);
    let width = b.int(Bits::B64, (8 * results.len()) as u64);
    let offset = b.binary(scalar::Binary::Mul, i, width);
    let mine = b.binary(scalar::Binary::Add, out, offset);
    for (k, v) in given.iter().enumerate() {
        let by = b.int(Bits::B64, 8 * k as u64);
        let at = b.binary(scalar::Binary::Add, mine, by);
        let v = canonical(&mut b, *v);
        b.store(at, v);
    }
    let one = b.int(Bits::B64, 1);
    let next = b.binary(scalar::Binary::Add, i, one);
    b.jump(head, vec![next]);
    b.at(exit);
    b.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![out, total],
        },
        &[],
    );
    b.ret(vec![]);
    Program {
        data: vec![data],
        functions: vec![f, b.finish(&[])],
        main: 1,
    }
}

/// A function of floats of the sizes, more of them than any chip has registers
/// for, which is called round after round of a loop with every one alive across
/// the call, and then gives all of them back: a function takes each float's
/// place from a sum, a difference, or a product of two of its parameters, or
/// from a parameter itself, in an order that the generator draws.
pub fn pressure(g: &mut Gen) -> Program {
    let count = 9 + g.below(14);
    let types: Vec<Type> = (0..count)
        .map(|_| if g.below(2) == 0 { F32 } else { F64 })
        .collect();
    // The function the loop calls.
    let mut b = Builder::new();
    let params = b.params(&types);
    let mut given = Vec::new();
    for ty in &types {
        let same: Vec<Value> = (0..count).filter(|k| types[*k] == *ty).map(|k| params[k]).collect();
        let (x, y) = (same[g.below(same.len())], same[g.below(same.len())]);
        given.push(match g.below(4) {
            0 => x,
            1 => b.float_binary(FloatBinary::Add, x, y),
            2 => b.float_binary(FloatBinary::Sub, x, y),
            _ => b.float_binary(FloatBinary::Mul, x, y),
        });
    }
    b.ret(given);
    let helper = b.finish(&types);
    // The function with the loop.
    let mut b = Builder::new();
    let params = b.params(&types);
    let mut head_types = vec![I64];
    head_types.extend(&types);
    let (head, state) = b.block(&head_types);
    let (round, _) = b.block(&[]);
    let (exit, _) = b.block(&[]);
    let zero = b.int(Bits::B64, 0);
    let mut first = vec![zero];
    first.extend(&params);
    b.jump(head, first);
    b.at(head);
    let rounds = b.int(Bits::B64, 1 + g.below(4) as u64);
    let go = b.compare(scalar::Compare::LessU, state[0], rounds);
    b.switch(go, vec![(exit, vec![]), (round, vec![])]);
    b.at(round);
    let called = b.call(0, state[1..].to_vec(), &types);
    let one = b.int(Bits::B64, 1);
    let next = b.binary(scalar::Binary::Add, state[0], one);
    let mut again = vec![next];
    again.extend(&called);
    b.jump(head, again);
    b.at(exit);
    b.ret(state[1..].to_vec());
    let top = b.finish(&types);
    let args: Vec<(Type, u64)> = types
        .iter()
        .map(|ty| (*ty, float(g, float_bits(*ty))))
        .collect();
    observed_canonically(vec![helper, top], 1, &args, g.below(2) == 0)
}

/// What the instructions of floats of one size give of two floats: the four
/// operations, the negation of the first, the six comparisons, the first as a
/// float of the other size, and the first cut to each size of whole number, as
/// signed and as unsigned.
fn on_floats(ty: Type) -> Function {
    let mut b = Builder::new();
    let v = b.params(&[ty, ty]);
    let mut results = Vec::new();
    let mut types = Vec::new();
    for op in BINARY {
        results.push(b.float_binary(op, v[0], v[1]));
        types.push(ty);
    }
    results.push(b.float_negate(v[0]));
    types.push(ty);
    for op in FloatCompare::ALL {
        results.push(b.float_compare(op, v[0], v[1]));
        types.push(I8);
    }
    let other = match float_bits(ty) {
        FloatBits::F32 => FloatBits::F64,
        FloatBits::F64 => FloatBits::F32,
    };
    results.push(b.float_resize(v[0], other));
    types.push(Type::Float(other));
    for signed in [false, true] {
        for bits in Bits::ALL {
            results.push(b.int_from_float(signed, v[1], bits));
            types.push(Type::Int(bits));
            results.push(b.int_from_float(signed, v[0], bits));
            types.push(Type::Int(bits));
        }
    }
    b.ret(results);
    b.finish(&types)
}

/// What the instructions of floats give of whole numbers of each size: the
/// nearest float of each size, read as signed and as unsigned.
fn on_ints() -> Function {
    let types: Vec<Type> = Bits::ALL.iter().map(|bits| Type::Int(*bits)).collect();
    let mut b = Builder::new();
    let v = b.params(&types);
    let mut results = Vec::new();
    let mut made = Vec::new();
    for to in [FloatBits::F32, FloatBits::F64] {
        for signed in [false, true] {
            for n in &v {
                results.push(b.float_from_int(signed, *n, to));
                made.push(Type::Float(to));
            }
        }
    }
    b.ret(results);
    b.finish(&made)
}

/// The programs that call each instruction of floats on the numbers at the
/// edges of each format, and on any numbers.
pub fn instruction_programs() -> Vec<Program> {
    let mut programs = Vec::new();
    for ty in [F32, F64] {
        let bits = float_bits(ty);
        let edges = edges(bits);
        let mut rows: Vec<Vec<u64>> = Vec::new();
        for x in &edges {
            for y in &edges {
                rows.push(vec![*x, *y]);
            }
        }
        for case in 0..100 {
            rows.push(crate::generate::sample(6130, case, |g| {
                vec![float(g, bits), float(g, bits)]
            }));
        }
        programs.push(table(on_floats(ty), &[ty, ty], &rows));
    }
    let rows: Vec<Vec<u64>> = (0..150)
        .map(|case| {
            crate::generate::sample(6131, case, |g| {
                Bits::ALL.iter().map(|b| int(g, *b)).collect()
            })
        })
        .collect();
    let types: Vec<Type> = Bits::ALL.iter().map(|bits| Type::Int(*bits)).collect();
    programs.push(table(on_ints(), &types, &rows));
    programs
}
