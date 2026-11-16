//! Runs a program in the form every target receives, in-process. What it
//! gives is what the program does, so every target's executable must do the
//! same.

use std::rc::Rc;

use crate::ir::{Program, Step, Stream, Value};

/// What a program did: what it wrote to standard output, and the status it
/// ended with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub stdout: Vec<u8>,
    pub status: i32,
}

/// What a local holds: a text, or a case of an enum by its place among the
/// enum's cases.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Word {
    Text(Rc<[u8]>),
    Case(usize),
    Unset,
}

/// Runs the program, with standard output going to memory, where no write
/// is refused, and no memory is either.
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
    f.results
        .iter()
        .map(|v| word(program, &locals, *v))
        .collect()
}

fn word(program: &Program, locals: &[Word], value: Value) -> Word {
    match value {
        Value::Text(t) => Word::Text(program.texts[t].as_slice().into()),
        Value::Case(c) => Word::Case(c),
        Value::Local(l) => locals[l].clone(),
    }
}

fn text(program: &Program, locals: &[Word], value: Value) -> Rc<[u8]> {
    match word(program, locals, value) {
        Word::Text(text) => text,
        _ => panic!("a text that is not one"),
    }
}

fn steps(program: &Program, steps_: &[Step], locals: &mut [Word], stdout: &mut Vec<u8>) {
    for step in steps_ {
        match step {
            Step::Write {
                stream: Stream::Stdout,
                value,
            } => stdout.extend_from_slice(&text(program, locals, *value)),
            Step::Call {
                function,
                args,
                results,
            } => {
                let args = args.iter().map(|a| word(program, locals, *a)).collect();
                let given = call(program, *function, args, stdout);
                for (r, w) in results.iter().zip(given) {
                    locals[*r] = w;
                }
            }
            Step::Set { local, value } => locals[*local] = word(program, locals, *value),
            Step::Join { local, parts } => {
                let parts: Vec<Rc<[u8]>> =
                    parts.iter().map(|p| text(program, locals, *p)).collect();
                locals[*local] = Word::Text(parts.concat().into());
            }
            Step::Escape { local, value } => {
                let text = text(program, locals, *value);
                locals[*local] = Word::Text(escaped(&text).into());
            }
            Step::Match { value, arms } => {
                let Word::Case(c) = word(program, locals, *value) else {
                    panic!("a match on what is not a case")
                };
                steps(program, &arms[c], locals, stdout);
            }
        }
    }
}

/// The text written as a string literal, as `Step::Escape` makes it.
fn escaped(text: &[u8]) -> Vec<u8> {
    let mut out = vec![b'"'];
    for &b in text {
        match b {
            b'"' | b'\\' | b'{' | b'}' => out.extend([b'\\', b]),
            b'\n' => out.extend(b"\\n"),
            b'\t' => out.extend(b"\\t"),
            b => out.push(b),
        }
    }
    out.push(b'"');
    out
}
