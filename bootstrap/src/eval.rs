//! Runs a program in the form every target receives, in-process. What it
//! gives is what the program does, so every target's executable must do the
//! same.

use crate::ir::{Program, Step, Stream, Value};

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
    call(program, program.main, Vec::new(), &mut stdout);
    Outcome { stdout, status: 0 }
}

/// Runs a function with its parameters, each a text by its place, and
/// gives the place of the text it gives back, if any.
fn call(
    program: &Program,
    function: usize,
    args: Vec<usize>,
    stdout: &mut Vec<u8>,
) -> Option<usize> {
    let f = &program.functions[function];
    let mut locals = args;
    locals.resize(f.locals, usize::MAX);
    let text = |locals: &[usize], value: Value| match value {
        Value::Text(t) => t,
        Value::Local(l) => locals[l],
    };
    for step in &f.steps {
        match step {
            Step::Write {
                stream: Stream::Stdout,
                value,
            } => stdout.extend_from_slice(&program.texts[text(&locals, *value)]),
            Step::Call {
                function,
                args,
                result,
            } => {
                let args = args.iter().map(|a| text(&locals, *a)).collect();
                let given = call(program, *function, args, stdout);
                if let (Some(r), Some(t)) = (result, given) {
                    locals[*r] = t;
                }
            }
        }
    }
    f.result.map(|v| text(&locals, v))
}
