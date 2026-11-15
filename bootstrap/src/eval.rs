//! Runs a program in the form every target receives, in-process. What it
//! gives is what the program does, so every target's executable must do the
//! same.

use crate::ir::{Program, Step, Stream};

/// What a program did: what it wrote to standard output, and the status it
/// ended with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub stdout: Vec<u8>,
    pub status: i32,
}

/// Runs the program, with standard output going to memory, where no write
/// is refused.
pub fn run(program: &Program) -> Outcome {
    let mut stdout = Vec::new();
    for step in &program.steps {
        match step {
            Step::Write {
                stream: Stream::Stdout,
                bytes,
            } => stdout.extend_from_slice(bytes),
        }
    }
    Outcome { stdout, status: 0 }
}
