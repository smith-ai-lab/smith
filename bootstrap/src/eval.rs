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

/// What a local holds: a text by its place, or a case of an enum by its
/// place among the enum's cases.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Word {
    Text(usize),
    Case(usize),
    Unset,
}

/// Runs the program, with standard output going to memory, where no write
/// is refused.
pub fn run(program: &Program) -> Outcome {
    let mut stdout = Vec::new();
    call(program, program.main, Vec::new(), &mut stdout);
    Outcome { stdout, status: 0 }
}

/// Runs a function with its parameters, and gives what it gives back.
fn call(program: &Program, function: usize, args: Vec<Word>, stdout: &mut Vec<u8>) -> Vec<Word> {
    let f = &program.functions[function];
    let mut locals = args;
    locals.resize(f.locals, Word::Unset);
    steps(program, &f.steps, &mut locals, stdout);
    f.results.iter().map(|v| word(&locals, *v)).collect()
}

fn word(locals: &[Word], value: Value) -> Word {
    match value {
        Value::Text(t) => Word::Text(t),
        Value::Case(c) => Word::Case(c),
        Value::Local(l) => locals[l],
    }
}

fn steps(program: &Program, steps_: &[Step], locals: &mut [Word], stdout: &mut Vec<u8>) {
    for step in steps_ {
        match step {
            Step::Write {
                stream: Stream::Stdout,
                value,
            } => {
                let Word::Text(t) = word(locals, *value) else {
                    panic!("a write of what is not a text")
                };
                stdout.extend_from_slice(&program.texts[t]);
            }
            Step::Call {
                function,
                args,
                results,
            } => {
                let args = args.iter().map(|a| word(locals, *a)).collect();
                let given = call(program, *function, args, stdout);
                for (r, w) in results.iter().zip(given) {
                    locals[*r] = w;
                }
            }
            Step::Set { local, value } => locals[*local] = word(locals, *value),
            Step::Match { value, arms } => {
                let Word::Case(c) = word(locals, *value) else {
                    panic!("a match on what is not a case")
                };
                steps(program, &arms[c], locals, stdout);
            }
        }
    }
}
