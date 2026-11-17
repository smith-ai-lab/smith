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
mod eval;
mod fold;
mod graph;
mod inline;
mod remove;
pub mod runtime;
mod wellformed;

pub use build::Builder;
pub use eval::{Outcome, call, run, run_with_limit};
pub use fold::fold;
pub use inline::{BOUND, inline, size};
pub use remove::remove;
pub use wellformed::{Malformed, Why, check};

/// The passes over a program in the scalar form, in the order they run:
/// inlining, folding of constants, and the removal of what nothing uses.
pub fn passes(program: &Program) -> Program {
    remove(&fold(&inline(program)))
}

/// How many bits an integer has.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
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

/// What a value is, which also says its size.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Type {
    /// An integer. Whether it is signed belongs to the operation that
    /// reads it, not to the value.
    Int(Bits),
    /// An address in the program's memory.
    Address,
}

/// A value, by its place in `Function::types`. It is defined once: as a
/// parameter of a block, or as a result of an instruction.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Value(pub usize);

/// A standard stream of the program.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stream {
    Stdout,
}

/// A constant. Its type is the type of the value the instruction gives.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Constant {
    /// An integer, which is the number's bits, in the value's width.
    Int(u64),
    /// The address that holds nothing.
    Null,
    /// The address of the first byte of one of the program's texts, by its
    /// place in `Program::data`.
    Data(usize),
}

/// A call into the system.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
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
}

/// What an instruction does with two integers of the same size. Every one
/// gives an integer of that size, in which the bits that do not fit are
/// left off, so no overflow is ever reported. Whether a number is signed
/// is in the operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
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
}

/// What an instruction asks of two integers of the same size. It gives an
/// integer of 8 bits that is 1 if it is so and 0 if it is not.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Compare {
    /// Whether `a` and `b` are the same.
    Equal,
    /// Whether `a` is below `b`, as unsigned numbers.
    LessU,
    /// Whether `a` is below `b`, as signed numbers.
    LessS,
}

/// What an instruction does.
#[derive(Clone, Debug, PartialEq, Eq)]
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
    /// Gives the integer of the type that is held at the address, in as many
    /// bytes as its size, least significant first.
    Load {
        ty: Type,
        address: Value,
    },
    /// Holds the integer of the type at the address, in as many bytes as its
    /// size, least significant first. It gives nothing.
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
        match self {
            Op::Constant(_) => Vec::new(),
            Op::Binary { a, b, .. } | Op::Compare { a, b, .. } => vec![*a, *b],
            Op::Widen { value, .. } | Op::Narrow(value) => vec![*value],
            Op::Choose {
                on,
                then,
                otherwise,
            } => vec![*on, *then, *otherwise],
            Op::Load { address, .. } => vec![*address],
            Op::Store { address, value, .. } => vec![*address, *value],
            Op::Call { args, .. } | Op::System { args, .. } => args.clone(),
        }
    }

    /// Whether the instruction runs a function or asks the system for
    /// something, which may change any register that is not kept for it.
    pub fn is_call(&self) -> bool {
        matches!(self, Op::Call { .. } | Op::System { .. })
    }
}

/// An instruction: what it does, and the values it defines.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Instruction {
    pub results: Vec<Value>,
    pub op: Op,
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
        match self {
            End::Jump(jump) => jump.args.clone(),
            End::Switch { on, jumps } => {
                let mut values = vec![*on];
                values.extend(jumps.iter().flat_map(|j| j.args.iter().copied()));
                values
            }
            End::Return(values) => values.clone(),
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
/// that block are the function's own.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Function {
    /// The type of each value, by its place.
    pub types: Vec<Type>,
    /// The types of the values it returns, in order.
    pub results: Vec<Type>,
    pub blocks: Vec<Block>,
}

/// A program: it runs the function `main`, which takes no parameters and
/// gives back nothing, and then ends with status 0. No function calls
/// itself, directly or through others.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Program {
    /// The bytes of the program's texts, which `Constant::Data` addresses.
    pub data: Vec<Vec<u8>>,
    pub functions: Vec<Function>,
    pub main: usize,
}

impl Program {
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
