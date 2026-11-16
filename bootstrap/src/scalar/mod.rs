//! The scalar form: the one small typed form below the kernel form that
//! every target will receive. A function is a list of blocks. A block takes
//! parameters, runs its instructions in order, and ends by jumping to a
//! block with arguments, by choosing between blocks, or by returning
//! values. Every value is named once, has a type, and has a size.
//!
//! Only what the programs lowered so far need is built: no number is held,
//! no memory is read or written at run time, and no text is built. The
//! rest of the form `knowledge/compiler.md` describes is added with the
//! features that need it. A program is lowered to it from the checked
//! program, by `check::to_scalar`.

mod eval;
mod wellformed;

pub use eval::run;
pub use wellformed::{Malformed, Why, check};

/// The feature that stopped a program from being lowered to the scalar form.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Stopped(pub &'static str);

/// How many bits an integer has.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Bits {
    B8,
    B16,
    B32,
    B64,
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
}

/// What an instruction does.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Op {
    Constant(Constant),
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
