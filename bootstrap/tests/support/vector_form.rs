//! What the tests of vectors in the scalar form share: the lanes of a vector
//! and the values at their edges, the programs that write what a function of
//! vectors gives, and the generator of functions of vectors.

use std::collections::HashMap;

use smith::scalar::{
    self, Binary, Bits, Builder, FloatBinary, FloatBits, FloatCompare, Function, Lane, Op,
    Program, Relation, Stream, System, Type, Value,
};

use crate::float_form as ff;
use crate::generate::Gen;

pub const I8: Type = Type::Int(Bits::B8);
pub const I16: Type = Type::Int(Bits::B16);
pub const I32: Type = Type::Int(Bits::B32);
pub const I64: Type = Type::Int(Bits::B64);
pub const F32: Type = Type::Float(FloatBits::F32);
pub const F64: Type = Type::Float(FloatBits::F64);

/// The vector type of the lane.
pub fn vec(lane: Lane) -> Type {
    Type::Vector(lane)
}

/// The bits of a lane of the size.
pub fn lane_mask(lane: Lane) -> u64 {
    match lane.bits() {
        64 => u64::MAX,
        n => (1 << n) - 1,
    }
}

/// The lane at the place of the vector, as its bits.
pub fn lane_at(lane: Lane, v: u128, at: u32) -> u64 {
    ((v >> (at * lane.bits())) & u128::from(lane_mask(lane))) as u64
}

/// A vector of the lane whose lane at each place is `each` of the place.
pub fn made(lane: Lane, each: impl Fn(u32) -> u64) -> u128 {
    (0..lane.count()).fold(0u128, |v, at| {
        v | (u128::from(each(at) & lane_mask(lane)) << (at * lane.bits()))
    })
}

/// The values at the edges of a lane.
pub fn lane_edges(lane: Lane) -> Vec<u64> {
    match lane {
        Lane::Float(bits) => ff::edges(bits),
        Lane::Int(bits) => {
            let mask = lane_mask(lane);
            let top = 1u64 << (bits.count() - 1);
            vec![0, 1, 2, mask, mask - 1, top, top - 1, top + 1, 0x55 & mask, 0xaa & mask]
        }
    }
}

/// A lane, as bits: one of the edges, or any bits.
pub fn lane_value(g: &mut Gen, lane: Lane) -> u64 {
    match lane {
        Lane::Float(bits) => ff::float(g, bits),
        Lane::Int(bits) => ff::int(g, bits),
    }
}

/// A vector of the lane: every lane an edge, or any bits, or every lane the one
/// value, so that the pairs of lanes of two vectors are often equal.
pub fn vector_value(g: &mut Gen, lane: Lane) -> u128 {
    match g.below(4) {
        0 => {
            let one = lane_value(g, lane);
            made(lane, |_| one)
        }
        1 => {
            let edges = lane_edges(lane);
            let picks: Vec<u64> = (0..lane.count()).map(|_| edges[g.below(edges.len())]).collect();
            made(lane, |at| picks[at as usize])
        }
        _ => {
            let values: Vec<u64> = (0..lane.count()).map(|_| lane_value(g, lane)).collect();
            made(lane, |at| values[at as usize])
        }
    }
}

/// The bits of a value as the one number a slot of 16 bytes holds.
fn slot_value(ty: Type, n: u128) -> u128 {
    match ty {
        Type::Vector(_) => n,
        _ => n & ((1u128 << (8 * ty.bytes())) - 1),
    }
}

/// A constant of the type that holds the bits.
fn constant(b: &mut Builder, ty: Type, n: u128) -> Value {
    match ty {
        Type::Vector(lane) => b.vector(lane, n),
        Type::Int(bits) => b.int(bits, n as u64),
        Type::Float(bits) => b.float(bits, n as u64),
        Type::Address => panic!("an argument that is an address"),
    }
}

/// A program whose `main` calls the function `f` with the arguments, and writes
/// what it gives to standard output, each result in a slot of 16 bytes whose
/// bytes above the result are 0. When `opaque` the arguments go through memory
/// first, so that no pass knows them.
pub fn observed(functions: Vec<Function>, f: usize, args: &[(Type, u128)], opaque: bool) -> Program {
    observe(functions, f, args, opaque, false)
}

/// As `observed`, except that each lane of a result that is not a number is written
/// as the one number the evaluator gives, so that what an executable writes is what
/// the evaluator writes, though the sign and the payload of such a number are the
/// chip's.
pub fn observed_canonically(
    functions: Vec<Function>,
    f: usize,
    args: &[(Type, u128)],
    opaque: bool,
) -> Program {
    observe(functions, f, args, opaque, true)
}

/// The float or the vector of floats, with each lane that is not a number the
/// one number that is not a number the evaluator gives.
fn canonical(b: &mut Builder, v: Value) -> Value {
    match b.type_of(v) {
        Type::Float(bits) => {
            let not_a_number = b.float_compare(FloatCompare::NotEqual, v, v);
            let nan = b.float(bits, ff::quiet_nan(bits));
            b.choose(not_a_number, nan, v)
        }
        Type::Vector(Lane::Float(bits)) => {
            let not_a_number = b.vector_float_compare(FloatCompare::NotEqual, v, v);
            let nan = b.float(bits, ff::quiet_nan(bits));
            let nan = b.vector_splat(nan);
            b.vector_choose(not_a_number, nan, v)
        }
        _ => v,
    }
}

fn observe(
    mut functions: Vec<Function>,
    f: usize,
    args: &[(Type, u128)],
    opaque: bool,
    canonically: bool,
) -> Program {
    let results = functions[f].results.clone();
    let mut b = Builder::new();
    let at = |b: &mut Builder, buffer: Value, k: usize| {
        let by = b.int(Bits::B64, 16 * k as u64);
        b.binary(Binary::Add, buffer, by)
    };
    let size = b.int(Bits::B64, 16 * (args.len() + results.len() + 1) as u64);
    let buffer = b.allocate(size);
    let mut values = Vec::new();
    for (k, (ty, n)) in args.iter().enumerate() {
        let v = constant(&mut b, *ty, *n);
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
    let count = b.int(Bits::B64, 16 * results.len() as u64);
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

/// What a program made by `observed` wrote: the slots, each as the bits of the
/// result it holds.
pub fn written(stdout: &[u8]) -> Vec<u128> {
    stdout
        .chunks(16)
        .map(|c| u128::from_le_bytes(c.try_into().expect("a whole slot")))
        .collect()
}

/// A function of the parameters whose one result is what `make` makes of
/// them, as a program that `observed` calls with the arguments.
pub fn one(
    params: &[Type],
    result: Type,
    args: &[u128],
    make: impl Fn(&mut Builder, &[Value]) -> Value,
) -> Program {
    let mut b = Builder::new();
    let values = b.params(params);
    let r = make(&mut b, &values);
    b.ret(vec![r]);
    let f = b.finish(&[result]);
    let args: Vec<(Type, u128)> = params.iter().copied().zip(args.iter().copied()).collect();
    observed(vec![f], 0, &args, false)
}

/// What `make` makes of the arguments, run by the evaluator, as the bits of the
/// result.
pub fn run_one(
    params: &[Type],
    result: Type,
    args: &[u128],
    make: impl Fn(&mut Builder, &[Value]) -> Value,
) -> u128 {
    let program = one(params, result, args, make);
    assert_eq!(scalar::check(&program), Ok(()));
    let outcome = scalar::run(&program);
    slot_value(result, written(&outcome.stdout)[0])
}

/// How many instructions of vectors the program has, of every kind but a
/// constant.
pub fn vector_instructions(program: &Program) -> usize {
    program
        .functions
        .iter()
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .filter(|i| {
            matches!(
                i.op,
                Op::VectorBinary { .. }
                    | Op::VectorFloatBinary { .. }
                    | Op::VectorFloatFused { .. }
                    | Op::VectorCompare { .. }
                    | Op::VectorFloatCompare { .. }
                    | Op::VectorChoose { .. }
                    | Op::VectorSplat(_)
                    | Op::VectorExtract { .. }
                    | Op::VectorInsert { .. }
                    | Op::VectorSum(_)
                    | Op::VectorFirstSet(_)
            )
        })
        .count()
}

/// The integer operations a vector has, and the float ones.
pub const INT_OPS: [Binary; 6] = [
    Binary::Add,
    Binary::Sub,
    Binary::Mul,
    Binary::And,
    Binary::Or,
    Binary::Xor,
];
pub const FLOAT_OPS: [FloatBinary; 3] = [FloatBinary::Add, FloatBinary::Sub, FloatBinary::Mul];

/// The values of each type that a function being generated can read.
#[derive(Clone, Default)]
struct Pools {
    values: HashMap<Type, Vec<Value>>,
    /// The masks that a comparison gave, whose lanes are all ones or all zeros,
    /// by the type of the mask.
    masks: HashMap<Type, Vec<Value>>,
}

impl Pools {
    fn add(&mut self, ty: Type, v: Value) {
        self.values.entry(ty).or_default().push(v);
    }

    /// Adds a mask that a comparison gave, which is a vector too.
    fn add_mask(&mut self, ty: Type, v: Value) {
        self.masks.entry(ty).or_default().push(v);
        self.add(ty, v);
    }

    /// One of the last three masks that a comparison gave, of the type, if there
    /// is one.
    fn last_mask(&self, ty: Type, g: &mut Gen) -> Option<Value> {
        let values = self.masks.get(&ty)?;
        Some(values[values.len() - 1 - g.below(values.len().min(3))])
    }

    /// One of the last three values of the type.
    fn last(&self, ty: Type, g: &mut Gen) -> Value {
        let values = &self.values[&ty];
        values[values.len() - 1 - g.below(values.len().min(3))]
    }

    fn newest(&self, ty: Type) -> Value {
        *self.values[&ty].last().unwrap()
    }
}

/// The parameters of a generated function: a vector of each lane, and a
/// scalar of the type of each lane.
const PARAMS: [Type; 12] = [
    Type::Vector(Lane::Int(Bits::B8)),
    Type::Vector(Lane::Int(Bits::B16)),
    Type::Vector(Lane::Int(Bits::B32)),
    Type::Vector(Lane::Int(Bits::B64)),
    Type::Vector(Lane::Float(FloatBits::F32)),
    Type::Vector(Lane::Float(FloatBits::F64)),
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
];

/// What a generated function gives back: a vector of each lane, a whole number,
/// and a float.
const RESULTS: [Type; 8] = [
    Type::Vector(Lane::Int(Bits::B8)),
    Type::Vector(Lane::Int(Bits::B16)),
    Type::Vector(Lane::Int(Bits::B32)),
    Type::Vector(Lane::Int(Bits::B64)),
    Type::Vector(Lane::Float(FloatBits::F32)),
    Type::Vector(Lane::Float(FloatBits::F64)),
    I64,
    F64,
];

/// The parameters of the function the generated functions call.
const HELPER: [Type; 3] = [
    Type::Vector(Lane::Float(FloatBits::F64)),
    Type::Vector(Lane::Int(Bits::B32)),
    I64,
];

fn any_lane(g: &mut Gen) -> Lane {
    *g.pick(&Lane::ALL)
}

fn int_lane(g: &mut Gen) -> Lane {
    *g.pick(&Lane::ALL[..4])
}

fn float_lane(g: &mut Gen) -> Lane {
    *g.pick(&Lane::ALL[4..])
}

/// Adds one instruction, chosen by the generator, to the block being filled.
fn step(b: &mut Builder, g: &mut Gen, p: &mut Pools, helper: Option<usize>) {
    match g.below(15) {
        0 => {
            let lane = int_lane(g);
            let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
            let r = b.vector_binary(*g.pick(&INT_OPS), x, y);
            p.add(vec(lane), r);
        }
        1 => {
            let lane = float_lane(g);
            let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
            let r = b.vector_float_binary(*g.pick(&FLOAT_OPS), x, y);
            p.add(vec(lane), r);
        }
        2 => {
            let lane = int_lane(g);
            let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
            let signed = g.below(2) == 0;
            let r = b.vector_compare(*g.pick(&Relation::ALL), signed, x, y);
            p.add_mask(lane.mask(), r);
        }
        3 => {
            let lane = float_lane(g);
            let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
            let r = b.vector_float_compare(*g.pick(&FloatCompare::ALL), x, y);
            p.add_mask(lane.mask(), r);
        }
        4 => {
            let lane = any_lane(g);
            // Any mask chooses bit by bit between two vectors of integers. Between
            // vectors of floats it takes whole lanes, from a mask of a comparison:
            // a bit from a float that is not a number is the chip's.
            let mask = if matches!(lane, Lane::Float(_)) {
                p.last_mask(lane.mask(), g)
            } else {
                Some(p.last(lane.mask(), g))
            };
            if let Some(mask) = mask {
                let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
                let r = b.vector_choose(mask, x, y);
                p.add(vec(lane), r);
            }
        }
        5 => {
            let lane = any_lane(g);
            let r = b.vector_splat(p.last(lane.scalar(), g));
            p.add(vec(lane), r);
        }
        6 => {
            let lane = any_lane(g);
            let at = g.below(lane.count() as usize) as u8;
            let r = b.vector_extract(p.last(vec(lane), g), at);
            p.add(lane.scalar(), r);
        }
        7 => {
            let lane = any_lane(g);
            let at = g.below(lane.count() as usize) as u8;
            let (v, x) = (p.last(vec(lane), g), p.last(lane.scalar(), g));
            let r = b.vector_insert(v, at, x);
            p.add(vec(lane), r);
        }
        8 => {
            let lane = any_lane(g);
            let r = b.vector_sum(p.last(vec(lane), g));
            p.add(lane.scalar(), r);
        }
        9 => {
            let lane = int_lane(g);
            let r = b.vector_first_set(p.last(vec(lane), g));
            p.add(I64, r);
        }
        10 => {
            let lane = any_lane(g);
            let n = vector_value(g, lane);
            let r = b.vector(lane, n);
            p.add(vec(lane), r);
        }
        11 => {
            // Through memory, at a place that is not a multiple of 16.
            let lane = any_lane(g);
            let size = b.int(Bits::B64, 32);
            let base = b.allocate(size);
            let by = b.int(Bits::B64, 3 * g.below(2) as u64);
            let at = b.binary(Binary::Add, base, by);
            b.store(at, p.last(vec(lane), g));
            let r = b.load(vec(lane), at);
            p.add(vec(lane), r);
        }
        12 => {
            // A choice of whole vectors by a number, which is not a mask.
            let lane = any_lane(g);
            let on = b.narrow(p.last(I64, g), Bits::B8);
            let (x, y) = (p.last(vec(lane), g), p.last(vec(lane), g));
            let r = b.choose(on, x, y);
            p.add(vec(lane), r);
        }
        13 => {
            // Through memory at an index times a scale, which a target may write
            // as one instruction: the address is made for the write, and again
            // for the read, so that nothing else reads the product and the sum
            // that the write has.
            let lane = any_lane(g);
            let size = b.int(Bits::B64, 64);
            let base = b.allocate(size);
            let three = b.int(Bits::B64, 3);
            let index = b.binary(Binary::And, p.last(I64, g), three);
            let scale = b.int(Bits::B64, 8);
            let offset = b.binary(Binary::Mul, index, scale);
            let at = b.binary(Binary::Add, base, offset);
            b.store(at, p.last(vec(lane), g));
            let offset = b.binary(Binary::Mul, index, scale);
            let again = b.binary(Binary::Add, base, offset);
            let r = b.load(vec(lane), again);
            p.add(vec(lane), r);
        }
        _ => {
            if let Some(callee) = helper {
                let args = vec![
                    p.last(HELPER[0], g),
                    p.last(HELPER[1], g),
                    p.last(I64, g),
                ];
                let got = b.call(callee, args, &[HELPER[0], HELPER[1]]);
                p.add(HELPER[0], got[0]);
                p.add(HELPER[1], got[1]);
            }
        }
    }
}

fn pools_of(params: &[Value], types: &[Type]) -> Pools {
    let mut p = Pools::default();
    for (v, ty) in params.iter().zip(types) {
        p.add(*ty, *v);
    }
    p
}

fn finish(p: &Pools) -> Vec<Value> {
    RESULTS.iter().map(|ty| p.newest(*ty)).collect()
}

/// A function that calls nothing: it takes two vectors and a whole number, and
/// gives two vectors.
fn helper(g: &mut Gen) -> Function {
    let mut b = Builder::new();
    let params = b.params(&HELPER);
    let mut p = Pools::default();
    // Every type the steps read has a value to start from.
    for lane in Lane::ALL {
        let n = vector_value(g, lane);
        let v = b.vector(lane, n);
        p.add(vec(lane), v);
        let s = b.vector_extract(v, 0);
        p.add(lane.scalar(), s);
    }
    for (v, ty) in params.iter().zip(&HELPER) {
        p.add(*ty, *v);
    }
    for _ in 0..g.below(6) {
        step(&mut b, g, &mut p, None);
    }
    b.ret(vec![p.newest(HELPER[0]), p.newest(HELPER[1])]);
    b.finish(&[HELPER[0], HELPER[1]])
}

/// A generated function of the parameters `PARAMS` whose results are `RESULTS`:
/// straight, or with a choice between two blocks that each work on vectors, or
/// with a loop that runs up to 7 rounds with vectors carried from one to the
/// next, any of them calling the helper.
fn body(g: &mut Gen, called: Option<usize>) -> Function {
    let mut b = Builder::new();
    let params = b.params(&PARAMS);
    let mut p = pools_of(&params, &PARAMS);
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
            let on = {
                let (x, y) = (p.last(I64, g), p.last(I64, g));
                b.compare(scalar::Compare::LessU, x, y)
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
            let bound = b.binary(Binary::And, p.last(I64, g), seven);
            let zero = b.int(Bits::B64, 0);
            let carried = [
                vec(Lane::Int(Bits::B32)),
                vec(Lane::Float(FloatBits::F64)),
                vec(Lane::Int(Bits::B8)),
            ];
            let mut head_types = vec![I64];
            head_types.extend(carried);
            let (head, head_params) = b.block(&head_types);
            let (round, _) = b.block(&[]);
            let (exit, exit_params) = b.block(&carried);
            let mut first = vec![zero];
            first.extend(carried.iter().map(|ty| p.last(*ty, g)));
            b.jump(head, first);
            b.at(head);
            let go = b.compare(scalar::Compare::LessU, head_params[0], bound);
            b.switch(
                go,
                vec![(exit, head_params[1..].to_vec()), (round, vec![])],
            );
            b.at(round);
            let mut q = p.clone();
            q.add(I64, head_params[0]);
            for (ty, v) in carried.iter().zip(&head_params[1..]) {
                q.add(*ty, *v);
            }
            for _ in 0..g.below(6) {
                step(&mut b, g, &mut q, called);
            }
            let one = b.int(Bits::B64, 1);
            let next = b.binary(Binary::Add, head_params[0], one);
            let mut back = vec![next];
            back.extend(carried.iter().map(|ty| q.newest(*ty)));
            b.jump(head, back);
            b.at(exit);
            let mut q = p.clone();
            for (ty, v) in carried.iter().zip(&exit_params) {
                q.add(*ty, *v);
            }
            let r = finish(&q);
            b.ret(r);
        }
    }
    b.finish(&RESULTS)
}

/// The arguments of a generated function, drawn by the generator.
pub fn arguments(g: &mut Gen) -> Vec<(Type, u128)> {
    PARAMS
        .iter()
        .map(|ty| match ty {
            Type::Vector(lane) => (*ty, vector_value(g, *lane)),
            Type::Float(bits) => (*ty, u128::from(ff::float(g, *bits))),
            Type::Int(bits) => (*ty, u128::from(ff::int(g, *bits))),
            Type::Address => unreachable!(),
        })
        .collect()
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
    let args = arguments(g);
    let opaque = g.below(2) == 0;
    observe(vec![called, top], 1, &args, opaque, canonically)
}


/// A function that holds more vectors than any chip has registers for, which is
/// called round after round of a loop with every one alive across the call, and
/// then gives all of them back: a function takes each vector's place from a sum, a
/// difference, a product, or a bit operation of two of its parameters of the same
/// lanes, or from a parameter itself, in an order that the generator draws.
pub fn pressure(g: &mut Gen) -> Program {
    let count = 9 + g.below(14);
    let types: Vec<Type> = (0..count).map(|_| vec(*g.pick(&Lane::ALL))).collect();
    // The function the loop calls.
    let mut b = Builder::new();
    let params = b.params(&types);
    let mut given = Vec::new();
    for ty in &types {
        let same: Vec<Value> = (0..count)
            .filter(|k| types[*k] == *ty)
            .map(|k| params[k])
            .collect();
        let (x, y) = (same[g.below(same.len())], same[g.below(same.len())]);
        given.push(match (ty, g.below(4)) {
            (_, 0) => x,
            (Type::Vector(Lane::Float(_)), 1) => b.vector_float_binary(FloatBinary::Add, x, y),
            (Type::Vector(Lane::Float(_)), 2) => b.vector_float_binary(FloatBinary::Sub, x, y),
            (Type::Vector(Lane::Float(_)), _) => b.vector_float_binary(FloatBinary::Mul, x, y),
            (_, 1) => b.vector_binary(Binary::Add, x, y),
            (_, 2) => b.vector_binary(Binary::Sub, x, y),
            _ => b.vector_binary(*g.pick(&[Binary::Mul, Binary::Xor]), x, y),
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
    let next = b.binary(Binary::Add, state[0], one);
    let mut again = vec![next];
    again.extend(&called);
    b.jump(head, again);
    b.at(exit);
    b.ret(state[1..].to_vec());
    let top = b.finish(&types);
    let args: Vec<(Type, u128)> = types
        .iter()
        .map(|ty| (*ty, vector_value(g, lane_of_type(*ty))))
        .collect();
    observe(vec![helper, top], 1, &args, g.below(2) == 0, true)
}

fn lane_of_type(ty: Type) -> Lane {
    match ty {
        Type::Vector(lane) => lane,
        _ => panic!("not a vector"),
    }
}

/// What every instruction of vectors of one lane gives of two vectors and a lane:
/// as many results as there are instructions, each a vector or a number.
fn on_vectors(lane: Lane) -> Function {
    let (v, s) = (vec(lane), lane.scalar());
    let mut b = Builder::new();
    let p = b.params(&[v, v, s]);
    let (x, y, n) = (p[0], p[1], p[2]);
    let mut results = Vec::new();
    let mut types = Vec::new();
    let mut mask = None;
    match lane {
        Lane::Int(_) => {
            for op in INT_OPS {
                results.push(b.vector_binary(op, x, y));
                types.push(v);
            }
            for op in Relation::ALL {
                for signed in [false, true] {
                    let m = b.vector_compare(op, signed, x, y);
                    mask = Some(m);
                    results.push(m);
                    types.push(lane.mask());
                }
            }
            results.push(b.vector_first_set(x));
            types.push(I64);
        }
        Lane::Float(_) => {
            for op in FLOAT_OPS {
                results.push(b.vector_float_binary(op, x, y));
                types.push(v);
            }
            for op in FloatCompare::ALL {
                let m = b.vector_float_compare(op, x, y);
                mask = Some(m);
                results.push(m);
                types.push(lane.mask());
            }
        }
    }
    let mask = mask.expect("a comparison");
    results.push(b.vector_choose(mask, x, y));
    types.push(v);
    let odd = x_as_mask(&mut b, lane, x);
    results.push(b.vector_choose(odd, x, y));
    types.push(v);
    let first = b.vector_first_set(mask);
    results.push(first);
    types.push(I64);
    // A choice of whole vectors by a number.
    let flag = b.narrow(first, Bits::B8);
    results.push(b.choose(flag, x, y));
    types.push(v);
    results.push(b.vector_splat(n));
    types.push(v);
    for at in [0, lane.count() as u8 / 2, lane.count() as u8 - 1] {
        results.push(b.vector_extract(x, at));
        types.push(s);
        results.push(b.vector_insert(x, at, n));
        types.push(v);
    }
    results.push(b.vector_sum(x));
    types.push(s);
    b.ret(results);
    b.finish(&types)
}

/// A vector that is a mask of the lane: the first vector when its lanes are
/// integers, and a constant that has some bits of each lane set when they are
/// floats, so that a choice by it takes bits from both.
fn x_as_mask(b: &mut Builder, lane: Lane, x: Value) -> Value {
    match lane {
        Lane::Int(_) => x,
        Lane::Float(_) => b.vector(
            lane_of_type(lane.mask()),
            0x5555_5555_5555_5555_5555_5555_5555_5555,
        ),
    }
}

/// The programs that call each instruction of vectors on vectors at the edges of
/// each lane, and on any vectors.
pub fn instruction_programs() -> Vec<Program> {
    Lane::ALL
        .iter()
        .map(|lane| {
            let lane = *lane;
            let mut rows: Vec<Vec<u128>> = Vec::new();
            let edges = lane_edges(lane);
            for x in &edges {
                for y in edges.iter().step_by(2) {
                    rows.push(vec![
                        made(lane, |_| *x),
                        made(lane, |at| if at % 2 == 0 { *x } else { *y }),
                        u128::from(*y),
                    ]);
                }
            }
            for case in 0..60 {
                rows.push(crate::generate::sample(8140, case, |g| {
                    let (x, y) = (vector_value(g, lane), vector_value(g, lane));
                    // The lanes of the second are often those of the first.
                    let y = if g.below(3) == 0 { x } else { y };
                    vec![x, y, u128::from(lane_value(g, lane))]
                }));
            }
            table(on_vectors(lane), &[vec(lane), vec(lane), lane.scalar()], &rows)
        })
        .collect()
}

/// A program that calls the function `f`, which takes values of the types, once
/// for each row, with the bits of the row as its arguments, which it reads from
/// the program's data, and writes what each call gives, floats that are not
/// numbers as one, each result in a slot of 16 bytes. A pass cannot know what a row
/// holds, so each call is made as a function of the form is, with its arguments
/// in registers.
pub fn table(f: Function, types: &[Type], rows: &[Vec<u128>]) -> Program {
    let results = f.results.clone();
    let mut data = Vec::new();
    for row in rows {
        for n in row {
            data.extend_from_slice(&n.to_le_bytes());
        }
    }
    let mut b = Builder::new();
    let table = b.emit(Op::Constant(scalar::Constant::Data(0)), &[Type::Address])[0];
    let total = b.int(Bits::B64, (16 * results.len() * rows.len()) as u64);
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
    let stride = b.int(Bits::B64, (16 * types.len()) as u64);
    let offset = b.binary(Binary::Mul, i, stride);
    let row = b.binary(Binary::Add, table, offset);
    let mut args = Vec::new();
    for (k, ty) in types.iter().enumerate() {
        let by = b.int(Bits::B64, 16 * k as u64);
        let at = b.binary(Binary::Add, row, by);
        args.push(b.load(*ty, at));
    }
    let given = b.call(0, args, &results);
    let width = b.int(Bits::B64, (16 * results.len()) as u64);
    let offset = b.binary(Binary::Mul, i, width);
    let mine = b.binary(Binary::Add, out, offset);
    for (k, v) in given.iter().enumerate() {
        let by = b.int(Bits::B64, 16 * k as u64);
        let at = b.binary(Binary::Add, mine, by);
        let v = canonical(&mut b, *v);
        b.store(at, v);
    }
    let one = b.int(Bits::B64, 1);
    let next = b.binary(Binary::Add, i, one);
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
