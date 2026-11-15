//! The program as every target receives it. This is the target seam: what
//! comes before it is the same for every target, and each target turns it
//! into an executable.

/// A standard stream of the program.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stream {
    Stdout,
}

/// One step of a program.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Step {
    /// Write all of the bytes to the stream. A write the system refuses ends
    /// the program with a failing status.
    Write { stream: Stream, bytes: Vec<u8> },
}

/// A program: steps run in order, and then the program ends with status 0.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Program {
    pub steps: Vec<Step>,
}
