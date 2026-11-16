//! Runs a program in the form every target receives, in-process. What it
//! gives is what the program does, so every target's executable must do the
//! same.

use std::rc::Rc;

use crate::ir::{Program, Step, Stream, Value, Word as Op};

/// What a program did: what it wrote to standard output, and the status it
/// ended with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub stdout: Vec<u8>,
    pub status: i32,
}

/// What a local holds: a text, a case of an enum by its place among the
/// enum's cases, or a number as its word of 64 bits.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Word {
    Text(Rc<[u8]>),
    Case(usize),
    Number(u64),
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
        Value::Number(n) => Word::Number(n),
        Value::Local(l) => locals[l].clone(),
    }
}

fn number(program: &Program, locals: &[Word], value: Value) -> u64 {
    match word(program, locals, value) {
        Word::Number(n) => n,
        _ => panic!("a number that is not one"),
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
            Step::Equal { local, a, b } => {
                let same = text(program, locals, *a) == text(program, locals, *b);
                locals[*local] = Word::Case(usize::from(same));
            }
            Step::Word { local, op, a, b } => {
                let (a, b) = (number(program, locals, *a), number(program, locals, *b));
                locals[*local] = match op {
                    Op::Add => Word::Number(a.wrapping_add(b)),
                    Op::Sub => Word::Number(a.wrapping_sub(b)),
                    Op::Mul => Word::Number(a.wrapping_mul(b)),
                    Op::MulHighU => Word::Number(((a as u128 * b as u128) >> 64) as u64),
                    Op::MulHighS => {
                        Word::Number(((a as i64 as i128 * b as i64 as i128) >> 64) as u64)
                    }
                    Op::And => Word::Number(a & b),
                    Op::Xor => Word::Number(a ^ b),
                    Op::ShiftLeft => Word::Number(a << b),
                    Op::ShiftRightS => Word::Number(((a as i64) >> b) as u64),
                    Op::Equal => Word::Case(usize::from(a == b)),
                    Op::LessU => Word::Case(usize::from(a < b)),
                    Op::LessS => Word::Case(usize::from((a as i64) < (b as i64))),
                };
            }
            Step::Decimal { local, value } => {
                let n = number(program, locals, *value);
                locals[*local] = Word::Text(n.to_string().into_bytes().into());
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
