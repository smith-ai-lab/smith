//! The scalar form: the one small typed form below the kernel form that
//! every target receives. A function is a list of blocks. A block takes
//! parameters, runs its instructions in order, and ends by jumping to a
//! block with arguments, by choosing between blocks, or by returning
//! values. Every value is named once, has a type, and has a size.
//!
//! Only what the programs lowered so far need is built: whole numbers, at
//! the size of their type, memory read and written one value at a time, and
//! a call into the system that asks for memory. The rest of the form
//! `knowledge/compiler.md` describes is added with the features that need
//! it. A program is lowered to it from the checked program, by
//! `check::to_scalar`, and what it needs at run time is in `runtime`.

mod build;
mod common;
mod escape;
mod eval;
mod fold;
mod fuse;
mod graph;
pub mod handler;
mod inline;
mod remat;
mod remove;
pub mod runtime;
mod sink;
mod thread;
mod trim;
mod vector;
mod wellformed;
mod words;

pub use build::Builder;
pub use common::common;
pub use eval::{
    Outcome, call, run, run_copying, run_counting, run_measured, run_taking, run_with_command_line,
    run_with_command_line_and_errors, run_with_limit, run_with_words, run_with_words_and_errors,
};
pub use fold::fold;
pub use fuse::fuse;
pub use inline::{BOUND, LEAF_BOUND, inline, size};
pub use remat::rematerialized;
pub use remove::{WALKS, remove, remove_counting};
pub use thread::thread;
pub use trim::trim;
pub use wellformed::{Malformed, Why, check};

/// The passes over a program in the scalar form, in the order they run:
/// inlining, the taking away of what a function is passed and gives back and
/// does not need, the sending of a jump past a block
/// that only chooses, folding of constants, the replacing of what is worked out
/// twice, and the removal of what nothing uses. None of them makes a fused
/// instruction or takes one apart: `fuse` does that, once, to a form as it is
/// made.
pub fn passes(program: &Program) -> Program {
    remove(&common(&fold(&thread(&trim(&inline(program))))))
}

/// How many bits an integer has.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Bits {
    B8,
    B16,
    B32,
    B64,
}

impl Bits {
    pub const ALL: [Bits; 4] = [Bits::B8, Bits::B16, Bits::B32, Bits::B64];

    pub fn count(self) -> u32 {
        match self {
            Bits::B8 => 8,
            Bits::B16 => 16,
            Bits::B32 => 32,
            Bits::B64 => 64,
        }
    }
}

/// How many bits a float has.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FloatBits {
    F32,
    F64,
}

impl FloatBits {
    pub fn count(self) -> u32 {
        match self {
            FloatBits::F32 => 32,
            FloatBits::F64 => 64,
        }
    }
}

/// What each lane of a vector is: an integer or a float. A vector is 128
/// bits whole, so the lane says how many lanes it has.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Lane {
    Int(Bits),
    Float(FloatBits),
}

impl Lane {
    /// Every lane a vector has.
    pub const ALL: [Lane; 6] = [
        Lane::Int(Bits::B8),
        Lane::Int(Bits::B16),
        Lane::Int(Bits::B32),
        Lane::Int(Bits::B64),
        Lane::Float(FloatBits::F32),
        Lane::Float(FloatBits::F64),
    ];

    /// How many bits a lane has.
    pub fn bits(self) -> u32 {
        match self {
            Lane::Int(bits) => bits.count(),
            Lane::Float(bits) => bits.count(),
        }
    }

    /// How many lanes a vector of the lane has: 2, 4, 8, or 16.
    pub fn count(self) -> u32 {
        128 / self.bits()
    }

    /// The type of a lane read alone.
    pub fn scalar(self) -> Type {
        match self {
            Lane::Int(bits) => Type::Int(bits),
            Lane::Float(bits) => Type::Float(bits),
        }
    }

    /// The type of a mask over vectors of the lane: a vector of integers as
    /// wide as the lane.
    pub fn mask(self) -> Type {
        Type::Vector(Lane::Int(match self.bits() {
            8 => Bits::B8,
            16 => Bits::B16,
            32 => Bits::B32,
            _ => Bits::B64,
        }))
    }
}

/// What a value is, which also says its size.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Type {
    /// An integer. Whether it is signed belongs to the operation that
    /// reads it, not to the value.
    Int(Bits),
    /// A float of 32 or 64 bits, as IEEE 754 says.
    Float(FloatBits),
    /// A vector of a fixed number of one of those, 128 bits in all: lane 0 is
    /// the lowest bits, and a lane is as an integer or a float of its size is.
    Vector(Lane),
    /// An address in the program's memory.
    Address,
}

impl Type {
    /// How many bytes a value of the type takes in memory.
    pub fn bytes(self) -> u64 {
        match self {
            Type::Int(bits) => u64::from(bits.count() / 8),
            Type::Float(bits) => u64::from(bits.count() / 8),
            Type::Vector(_) => 16,
            Type::Address => 8,
        }
    }
}

/// Where each scalar of an element of a list is held, and how many bytes the
/// element takes: the scalars lie one after another, each at a multiple of
/// its size, and the element takes a whole number of its largest scalar.
pub fn element(scalars: &[Type]) -> (Vec<u64>, u64) {
    let mut end: u64 = 0;
    let places = scalars
        .iter()
        .map(|t| {
            let at = end.next_multiple_of(t.bytes());
            end = at + t.bytes();
            at
        })
        .collect();
    let largest = scalars.iter().map(|t| t.bytes()).max().unwrap_or(1);
    (places, end.next_multiple_of(largest))
}

/// A value, by its place in `Function::types`. It is defined once: as a
/// parameter of a block, or as a result of an instruction.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Value(pub usize);

/// A standard stream of the program.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Stream {
    Stdout,
    Stderr,
}

/// A constant. Its type is the type of the value the instruction gives.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Constant {
    /// An integer, which is the number's bits, in the value's width.
    Int(u64),
    /// A float: the bits of the number, in the value's width.
    Float(u64),
    /// The address that holds nothing.
    Null,
    /// The address of the first byte of one of the program's texts, by its
    /// place in `Program::data`.
    Data(usize),
    /// A vector: the bits of the whole, with lane 0 in the lowest, as the low
    /// 64 bits and then the high 64, which keeps a constant as small as it was.
    Vector(u64, u64),
}

impl Constant {
    /// The constant that is a vector of the bits.
    pub fn vector(bits: u128) -> Constant {
        Constant::Vector(bits as u64, (bits >> 64) as u64)
    }

    /// The bits of a constant that is a vector.
    pub fn vector_bits(self) -> Option<u128> {
        match self {
            Constant::Vector(low, high) => Some(u128::from(low) | (u128::from(high) << 64)),
            _ => None,
        }
    }
}

/// A call into the system.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum System {
    /// Writes the bytes from an address, as many as a 64-bit integer says,
    /// to the stream. It takes the address and then the count, and gives
    /// nothing. A write the system refuses ends the program with a failing
    /// status.
    Write(Stream),
    /// Asks for as many bytes as a 64-bit integer says, and gives the
    /// address of the first. The bytes are the program's until it ends. A
    /// request the system refuses ends the program with a failing status.
    Allocate,
    /// Ends the program with the status, a byte, that it takes. It gives
    /// nothing.
    Exit,
}

/// What an instruction does with two integers of the same size, except a
/// shift, whose second is an integer of any size. Every one gives an integer
/// of the size of its first, in which the bits that do not fit are left off,
/// so no overflow is ever reported. Whether a number is signed is in the
/// operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Binary {
    /// `a + b`. It is also what moves an address: `a` is an address and `b`
    /// is a 64-bit integer, the number of bytes, and it gives an address.
    Add,
    /// `a - b`.
    Sub,
    /// The low half of `a * b`.
    Mul,
    /// The high half of `a * b`, with `a` and `b` as unsigned numbers.
    MulHighU,
    /// The high half of `a * b`, with `a` and `b` as signed numbers.
    MulHighS,
    /// The bits `a` and `b` both have.
    And,
    /// The bits that only one of `a` and `b` has.
    Xor,
    /// The bits that `a` or `b` has.
    Or,
    /// `a` with its bits moved up by `b`, the bits that leave the top
    /// dropped. `b` is taken modulo 64, so a program that means a shift is
    /// by fewer bits than the size of `a`.
    ShiftLeft,
    /// `a` with its bits moved down by `b`, and zeros filled in at the top.
    /// `b` is taken modulo 64.
    ShiftRightU,
    /// `a` with its bits moved down by `b`, and the sign of `a` filled in at
    /// the top. `b` is taken modulo 64.
    ShiftRightS,
}

/// What an instruction asks of two integers of the same size. It gives an
/// integer of 8 bits that is 1 if it is so and 0 if it is not.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Compare {
    /// Whether `a` and `b` are the same.
    Equal,
    /// Whether `a` is below `b`, as unsigned numbers.
    LessU,
    /// Whether `a` is below `b`, as signed numbers.
    LessS,
}

/// What an instruction does with two floats of the same size. Every one gives
/// a float of that size, rounded to the nearest, ties to even.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FloatBinary {
    Add,
    Sub,
    Mul,
    Div,
}

/// What a fused instruction works out of three floats of one size, `a`, `b`,
/// and `c`, exactly and then rounded once, to the nearest, ties to even.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Fused {
    /// `a * b + c`.
    MulAdd,
    /// `a * b - c`.
    MulSub,
    /// `c - a * b`.
    NegMulAdd,
}

impl Fused {
    pub const ALL: [Fused; 3] = [Fused::MulAdd, Fused::MulSub, Fused::NegMulAdd];
}

/// What an instruction asks of two floats of the same size. It gives an
/// integer of 8 bits that is 1 if it is so and 0 if it is not. A float that is
/// not a number is neither below, above, nor equal to any, so every one of
/// these but `NotEqual` is 0 when either is not a number, and `NotEqual` is 1.
/// `0.0` and `-0.0` are equal.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FloatCompare {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
}

impl FloatCompare {
    pub const ALL: [FloatCompare; 6] = [
        FloatCompare::Equal,
        FloatCompare::NotEqual,
        FloatCompare::Less,
        FloatCompare::LessEqual,
        FloatCompare::Greater,
        FloatCompare::GreaterEqual,
    ];
}

/// What an instruction asks of each pair of lanes of two vectors of integers.
/// For `Equal` and `NotEqual` whether the lanes are signed makes no difference.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Relation {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
}

impl Relation {
    pub const ALL: [Relation; 6] = [
        Relation::Equal,
        Relation::NotEqual,
        Relation::Less,
        Relation::LessEqual,
        Relation::Greater,
        Relation::GreaterEqual,
    ];
}

/// What an instruction does.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Op {
    Constant(Constant),
    Binary {
        op: Binary,
        a: Value,
        b: Value,
    },
    Compare {
        op: Compare,
        a: Value,
        b: Value,
    },
    /// The sum, difference, or product of two integers of one size, `Add`,
    /// `Sub`, or `Mul`, read as signed or as unsigned numbers, and whether it
    /// did not fit the size: two results, the number with the bits that do not
    /// fit left off, which is what the `Binary` instruction gives, and an
    /// integer of 8 bits that is 1 when the number was not what the operation
    /// gives of the two numbers, and 0 when it was.
    Overflows {
        op: Binary,
        signed: bool,
        a: Value,
        b: Value,
    },
    /// `a + b`, `a - b`, `a * b`, or `a / b` of two floats of one size. A
    /// product is not fused with a sum here: `FloatFused` is the instruction
    /// that does it.
    FloatBinary {
        op: FloatBinary,
        a: Value,
        b: Value,
    },
    /// What `op` says of three floats of one size, worked out exactly and
    /// rounded once. `fuse` makes it, where the form is made, of a product whose
    /// only use is one sum or difference, and no pass makes or takes one apart.
    FloatFused {
        op: Fused,
        a: Value,
        b: Value,
        c: Value,
    },
    /// The float with its sign bit flipped, whatever it is, so that the
    /// negation of zero is zero of the other sign.
    FloatNegate(Value),
    FloatCompare {
        op: FloatCompare,
        a: Value,
        b: Value,
    },
    /// The float nearest the integer, which is read as signed or as unsigned.
    FloatFromInt {
        signed: bool,
        value: Value,
    },
    /// The integer the float is cut to, which is read as signed or as
    /// unsigned: the float without its fraction when that fits, the least or
    /// the greatest integer when it does not, and 0 when it is not a number.
    IntFromFloat {
        signed: bool,
        value: Value,
    },
    /// The float of the size of the result that is nearest the float.
    FloatResize(Value),
    /// Each lane of two vectors of integers, `a` and `b`, by `Add`, `Sub`,
    /// `Mul`, `And`, `Or`, or `Xor`: the vector of what the instruction of that
    /// operation gives for each pair of lanes.
    VectorBinary {
        op: Binary,
        a: Value,
        b: Value,
    },
    /// Each lane of two vectors of floats, by `Add`, `Sub`, or `Mul`: what the
    /// instruction of that operation gives for each pair of lanes.
    VectorFloatBinary {
        op: FloatBinary,
        a: Value,
        b: Value,
    },
    /// Each lane of three vectors of floats, `a`, `b`, and `c`: what
    /// `FloatFused` gives for each triple of lanes.
    VectorFloatFused {
        op: Fused,
        a: Value,
        b: Value,
        c: Value,
    },
    /// A mask: for each pair of lanes of two vectors of integers, a lane of
    /// the same size that is all ones when the pair is as `op` says, read as
    /// signed or as unsigned numbers, and all zeros when it is not.
    VectorCompare {
        op: Relation,
        signed: bool,
        a: Value,
        b: Value,
    },
    /// A mask: for each pair of lanes of two vectors of floats, an integer lane
    /// as wide as the float that is all ones when the comparison holds, as
    /// `FloatCompare` says it, and all zeros when it does not.
    VectorFloatCompare {
        op: FloatCompare,
        a: Value,
        b: Value,
    },
    /// Takes each bit from `then` where the bit of the mask is 1, and from
    /// `otherwise` where it is 0. For a mask that a comparison gave, each lane
    /// is `then` or `otherwise` as the scalar choice on the lane of the mask.
    VectorChoose {
        mask: Value,
        then: Value,
        otherwise: Value,
    },
    /// The vector whose every lane is the integer or the float.
    VectorSplat(Value),
    /// The lane of the vector at the place, which is below the number of lanes.
    VectorExtract {
        vector: Value,
        lane: u8,
    },
    /// The vector with the lane at the place, which is below the number of
    /// lanes, replaced by the value.
    VectorInsert {
        vector: Value,
        lane: u8,
        value: Value,
    },
    /// The sum of the lanes. Of integers it is the sum with the bits that do not
    /// fit left off. Of floats it is added in pairs of lanes that lie
    /// next to each other, and then in pairs of those sums, so that the sum of
    /// lanes 0 to 3 is `(l0 + l1) + (l2 + l3)`, and each addition rounds as
    /// that of `FloatBinary` does.
    VectorSum(Value),
    /// The place of the first lane of the mask whose top bit is 1, as an integer
    /// of 64 bits, and the number of lanes when none has. A lane of a mask that
    /// a comparison gave is all ones or all zeros, so this is its first lane that
    /// is set.
    VectorFirstSet(Value),
    /// Gives an integer with more bits that stands for the same number,
    /// read as signed or as unsigned.
    Widen {
        signed: bool,
        value: Value,
    },
    /// Gives an integer with fewer bits: the low bits of the value.
    Narrow(Value),
    /// Gives `then` if the integer of 8 bits `on` is not 0, and `otherwise`
    /// if it is. Both are values of the type the instruction gives.
    Choose {
        on: Value,
        then: Value,
        otherwise: Value,
    },
    /// Gives the integer or address of the type that is held at the address,
    /// in as many bytes as its size, least significant first.
    Load {
        ty: Type,
        address: Value,
    },
    /// Holds the integer or address of the type at the address, in as many
    /// bytes as its size, least significant first. It gives nothing.
    Store {
        ty: Type,
        address: Value,
        value: Value,
    },
    /// Runs a function, by its place in `Program::functions`, with these
    /// values as the parameters of its first block, in order. It gives the
    /// values the function returns, in order.
    Call {
        function: usize,
        args: Vec<Value>,
    },
    System {
        call: System,
        args: Vec<Value>,
    },
}

impl Op {
    /// The values the instruction reads, in order.
    pub fn operands(&self) -> Vec<Value> {
        let mut values = Vec::new();
        self.operands_into(&mut values);
        values
    }

    /// Puts the values the instruction reads, in order, in `values`, which it
    /// empties first.
    pub fn operands_into(&self, values: &mut Vec<Value>) {
        values.clear();
        match self {
            Op::Constant(_) => {}
            Op::Binary { a, b, .. }
            | Op::Compare { a, b, .. }
            | Op::Overflows { a, b, .. }
            | Op::FloatBinary { a, b, .. }
            | Op::FloatCompare { a, b, .. }
            | Op::VectorBinary { a, b, .. }
            | Op::VectorFloatBinary { a, b, .. }
            | Op::VectorCompare { a, b, .. }
            | Op::VectorFloatCompare { a, b, .. } => values.extend([*a, *b]),
            Op::FloatFused { a, b, c, .. } | Op::VectorFloatFused { a, b, c, .. } => {
                values.extend([*a, *b, *c])
            }
            Op::Widen { value, .. }
            | Op::Narrow(value)
            | Op::VectorSplat(value)
            | Op::VectorSum(value)
            | Op::VectorFirstSet(value)
            | Op::VectorExtract { vector: value, .. }
            | Op::FloatNegate(value)
            | Op::FloatFromInt { value, .. }
            | Op::IntFromFloat { value, .. }
            | Op::FloatResize(value) => values.push(*value),
            Op::Choose {
                on,
                then,
                otherwise,
            }
            | Op::VectorChoose {
                mask: on,
                then,
                otherwise,
            } => values.extend([*on, *then, *otherwise]),
            Op::VectorInsert { vector, value, .. } => values.extend([*vector, *value]),
            Op::Load { address, .. } => values.push(*address),
            Op::Store { address, value, .. } => values.extend([*address, *value]),
            Op::Call { args, .. } | Op::System { args, .. } => values.extend(args),
        }
    }

    /// Whether the instruction runs a function or asks the system for
    /// something, which may change any register that is not kept for it.
    pub fn is_call(&self) -> bool {
        matches!(self, Op::Call { .. } | Op::System { .. })
    }
}

/// An instruction: what it does, the values it defines, and the place in the
/// source it was built from, which the map of an executable holds. Two
/// instructions are equal when they do the same, wherever they were written.
#[derive(Clone, Debug)]
pub struct Instruction {
    pub results: Vec<Value>,
    pub op: Op,
    pub at: Place,
}

impl PartialEq for Instruction {
    fn eq(&self, other: &Instruction) -> bool {
        self.results == other.results && self.op == other.op
    }
}

impl Eq for Instruction {}

/// A place in the source: the file and the function it is written in, each as the
/// number `place::intern` gives its name, and a line and a column of the file
/// that count from 1. A line of 0 says there is no place, as the code of the
/// runtime has none.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Place {
    pub file: u32,
    pub function: u32,
    pub line: u32,
    pub col: u32,
}

impl Place {
    /// No place.
    pub const NONE: Place = Place {
        file: 0,
        function: 0,
        line: 0,
        col: 0,
    };

    pub fn is_none(self) -> bool {
        self.line == 0
    }
}

/// The names a place holds, which are kept once for the process: a name is the
/// number it is given, and the same name is always the same number.
pub mod names {
    use std::sync::Mutex;

    static NAMES: Mutex<Vec<&'static str>> = Mutex::new(Vec::new());

    /// The number of the name. The names are few, a file and a function each.
    pub fn intern(name: &str) -> u32 {
        let mut names = NAMES.lock().expect("the names are not left held");
        if let Some(at) = names.iter().position(|n| *n == name) {
            return at as u32;
        }
        names.push(Box::leak(name.to_string().into_boxed_str()));
        names.len() as u32 - 1
    }

    /// The name with the number.
    pub fn name(number: u32) -> &'static str {
        NAMES.lock().expect("the names are not left held")[number as usize]
    }
}

/// A jump to a block, giving it a value for each of its parameters.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Jump {
    pub block: usize,
    pub args: Vec<Value>,
}

/// How a block ends.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum End {
    Jump(Jump),
    /// Makes the jump that is at the place of the integer `on`, which is
    /// below the number of jumps.
    Switch {
        on: Value,
        jumps: Vec<Jump>,
    },
    Return(Vec<Value>),
}

impl End {
    /// The values the end reads, in order.
    pub fn operands(&self) -> Vec<Value> {
        let mut values = Vec::new();
        self.operands_into(&mut values);
        values
    }

    /// Puts the values the end reads, in order, in `values`, which it empties
    /// first.
    pub fn operands_into(&self, values: &mut Vec<Value>) {
        values.clear();
        match self {
            End::Jump(jump) => values.extend(&jump.args),
            End::Switch { on, jumps } => {
                values.push(*on);
                values.extend(jumps.iter().flat_map(|j| j.args.iter().copied()));
            }
            End::Return(returned) => values.extend(returned),
        }
    }

    /// The blocks the end may go to, in order.
    pub fn successors(&self) -> Vec<usize> {
        match self {
            End::Jump(jump) => vec![jump.block],
            End::Switch { jumps, .. } => jumps.iter().map(|j| j.block).collect(),
            End::Return(_) => Vec::new(),
        }
    }
}

/// A block: it is entered with its parameters, runs its instructions in
/// order, and ends.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Block {
    pub params: Vec<Value>,
    pub instructions: Vec<Instruction>,
    pub end: End,
}

/// A function. Its first block is where it starts, and the parameters of
/// that block are the function's own. Two functions are equal when they do the
/// same: the name is for the map of an executable and is not compared.
#[derive(Clone, Debug)]
pub struct Function {
    /// The type of each value, by its place.
    pub types: Vec<Type>,
    /// The types of the values it returns, in order.
    pub results: Vec<Type>,
    pub blocks: Vec<Block>,
    /// Whether the function is run rarely, so that a call of it is never
    /// replaced by its instructions, however many places call it. Its
    /// instructions then stay out of the code of a loop that calls it, and
    /// so do the values it needs.
    pub cold: bool,
    /// What the map of an executable calls the function: its module and its
    /// name as written, with the types it is made for in brackets after them,
    /// or a name in angle brackets for what no function was written for. It is
    /// empty for a function that has none, which the map calls `<function N>`.
    pub name: String,
}

impl PartialEq for Function {
    fn eq(&self, other: &Function) -> bool {
        self.types == other.types
            && self.results == other.results
            && self.blocks == other.blocks
            && self.cold == other.cold
    }
}

impl Eq for Function {}

impl Function {
    /// The function, called `name` in the map of an executable.
    pub fn named(mut self, name: impl Into<String>) -> Function {
        self.name = name.into();
        self
    }
}

/// A program: it runs the function `main`, which gives back nothing, and then
/// ends with status 0. `main` takes no parameters, or it takes the three that
/// `ARGUMENTS` lists, which the system gives it as `runtime::Runtime::Arguments`
/// says. No function calls itself, directly or through others.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Program {
    /// The bytes of the program's texts, which `Constant::Data` addresses.
    pub data: Vec<Vec<u8>>,
    pub functions: Vec<Function>,
    pub main: usize,
}

/// The types of the parameters of a `main` that takes the words the program was
/// started with: a number of words, the address of their addresses, and the
/// address of a command line.
pub const ARGUMENTS: [Type; 3] = [Type::Int(Bits::B64), Type::Address, Type::Address];

impl Program {
    /// Whether `main` takes what the system gives a program that is started
    /// with words, which a target must then give it.
    pub fn takes_arguments(&self) -> bool {
        self.functions[self.main].blocks[0].params.len() == ARGUMENTS.len()
    }

    /// Whether the program asks the system for memory.
    pub fn allocates(&self) -> bool {
        let instructions = self.functions.iter().flat_map(|f| &f.blocks);
        instructions.flat_map(|b| &b.instructions).any(|i| {
            matches!(
                i.op,
                Op::System {
                    call: System::Allocate,
                    ..
                }
            )
        })
    }
}
