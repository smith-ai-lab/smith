//! The program as every target receives it. This is the target seam: what
//! comes before it is the same for every target, and each target turns it
//! into an executable.

/// A standard stream of the program.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stream {
    Stdout,
}

/// Where a value is. A value is a piece of text, or which case of an enum
/// a value of the enum holds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Value {
    /// One of the program's texts, by its place in `Program::texts`.
    Text(usize),
    /// A local of the function running: one of its parameters, which come
    /// first, or a value it holds.
    Local(usize),
    /// A case of an enum, by its place among the enum's cases. It also
    /// fills the places a value of an enum keeps for a larger case than
    /// the one it holds, which nothing reads.
    Case(usize),
}

/// One step of a function.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    /// Write all of the text to the stream. A write the system refuses ends
    /// the program with a failing status.
    Write { stream: Stream, value: Value },
    /// Run a function, by its place in `Program::functions`, with these
    /// values as its parameters, in order. The values it gives back go to
    /// the locals `results`, in order.
    Call {
        function: usize,
        args: Vec<Value>,
        results: Vec<usize>,
    },
    /// Copy a value to a local.
    Set { local: usize, value: Value },
    /// Make a new text that is the texts `parts` one after another, in
    /// memory asked of the system, and put it in the local. Memory the
    /// system refuses ends the program with a failing status. Memory is
    /// never given back while the program runs.
    Join { local: usize, parts: Vec<Value> },
    /// Make a new text that is the text `value` written as a string
    /// literal: in double quotes, with a backslash before each `"`, `\`,
    /// `{`, and `}`, and each line break and tab written `\n` and `\t`.
    /// It is put in the local, in memory asked of the system as for `Join`.
    Escape { local: usize, value: Value },
    /// Run the steps of the arm at the place of the case `value` is, which
    /// is one of the arms.
    Match { value: Value, arms: Vec<Vec<Step>> },
}

/// A function: steps run in order, and then it gives back `results`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Function {
    /// How many parameters it takes.
    pub params: usize,
    /// How many locals it has, its parameters included.
    pub locals: usize,
    pub steps: Vec<Step>,
    /// The values it gives back, in order, which may be none.
    pub results: Vec<Value>,
}

impl Function {
    /// The most values one call among its steps, in any arm, passes and
    /// receives.
    pub fn outgoing(&self) -> usize {
        fn most(steps: &[Step]) -> usize {
            (steps.iter())
                .map(|s| match s {
                    Step::Call { args, results, .. } => args.len() + results.len(),
                    Step::Match { arms, .. } => arms.iter().map(|a| most(a)).max().unwrap_or(0),
                    Step::Write { .. }
                    | Step::Set { .. }
                    | Step::Join { .. }
                    | Step::Escape { .. } => 0,
                })
                .max()
                .unwrap_or(0)
        }
        most(&self.steps)
    }
}

/// Whether any of the steps, in any arm, asks the system for memory.
fn allocates(steps: &[Step]) -> bool {
    steps.iter().any(|s| match s {
        Step::Join { .. } | Step::Escape { .. } => true,
        Step::Match { arms, .. } => arms.iter().any(|a| allocates(a)),
        Step::Write { .. } | Step::Call { .. } | Step::Set { .. } => false,
    })
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

impl Program {
    /// Whether it ever asks the system for memory. A target reaches for the
    /// system's allocator only in a program that does.
    pub fn allocates(&self) -> bool {
        self.functions.iter().any(|f| allocates(&f.steps))
    }
}
