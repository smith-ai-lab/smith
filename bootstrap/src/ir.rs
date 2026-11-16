//! The program as every target receives it. This is the target seam: what
//! comes before it is the same for every target, and each target turns it
//! into an executable.

/// A standard stream of the program.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stream {
    Stdout,
}

/// Where a piece of text is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Value {
    /// One of the program's texts, by its place in `Program::texts`.
    Text(usize),
    /// A local of the function running: one of its parameters, which come
    /// first, or a value it holds.
    Local(usize),
}

/// One step of a function.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    /// Write all of the text to the stream. A write the system refuses ends
    /// the program with a failing status.
    Write { stream: Stream, value: Value },
    /// Run a function, by its place in `Program::functions`, with these
    /// values as its parameters, in order. The texts it gives back go to
    /// the locals `results`, in order.
    Call {
        function: usize,
        args: Vec<Value>,
        results: Vec<usize>,
    },
}

/// A function: steps run in order, and then it gives back `results`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Function {
    /// How many parameters it takes.
    pub params: usize,
    /// How many locals it has, its parameters included.
    pub locals: usize,
    pub steps: Vec<Step>,
    /// The texts it gives back, in order, which may be none.
    pub results: Vec<Value>,
}

/// A program: it runs the function `main`, which takes no parameters and
/// gives back nothing, and then ends with status 0. No function calls
/// itself, directly or through others.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Program {
    pub texts: Vec<Vec<u8>>,
    pub functions: Vec<Function>,
    pub main: usize,
}
