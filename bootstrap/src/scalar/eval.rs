//! Runs a program in the scalar form, in-process. What it gives is what the
//! program does.

use super::{Binary, Bits, Compare, Constant, End, Jump, Op, Program, System, Type, Value};
use crate::eval::Outcome;

/// Where the program's texts start, so that no text is at the address that
/// holds nothing.
const BASE: usize = 0x1000;

/// What a value holds.
#[derive(Clone, Copy, Debug)]
enum Word {
    Int(u64),
    Address(usize),
}

struct Machine<'a> {
    program: &'a Program,
    /// The program's texts one after another, which is memory from `BASE`.
    memory: Vec<u8>,
    /// Where each text starts, as an address.
    starts: Vec<usize>,
    stdout: Vec<u8>,
}

/// Runs the program, with standard output going to memory, where no write
/// is refused.
pub fn run(program: &Program) -> Outcome {
    let mut memory = Vec::new();
    let mut starts = Vec::new();
    for text in &program.data {
        starts.push(BASE + memory.len());
        memory.extend_from_slice(text);
    }
    let mut machine = Machine {
        program,
        memory,
        starts,
        stdout: Vec::new(),
    };
    machine.call(program.main, Vec::new());
    Outcome {
        stdout: machine.stdout,
        status: 0,
    }
}

/// Runs one function of the program on integers, and gives the integers it
/// returns, each as its bits in the size of its type. A function that takes
/// or gives an address cannot be run so.
pub fn call(program: &Program, function: usize, args: &[u64]) -> Vec<u64> {
    let mut machine = Machine {
        program,
        memory: Vec::new(),
        starts: Vec::new(),
        stdout: Vec::new(),
    };
    let words = args.iter().map(|n| Word::Int(*n)).collect();
    (machine.call(function, words).into_iter())
        .map(|w| match w {
            Word::Int(n) => n,
            Word::Address(_) => panic!("a function that gives an address"),
        })
        .collect()
}

/// The bits of an integer of the size, in a word.
fn mask(bits: Bits) -> u64 {
    match bits {
        Bits::B64 => u64::MAX,
        _ => (1u64 << bits.count()) - 1,
    }
}

/// The number the bits stand for as a signed integer of the size.
fn signed(n: u64, bits: Bits) -> i128 {
    let up = 64 - bits.count();
    (((n << up) as i64) >> up) as i128
}

/// How many bits an integer type has.
fn bits_of(ty: Type) -> Bits {
    match ty {
        Type::Int(bits) => bits,
        Type::Address => panic!("an address where an integer is needed"),
    }
}

impl Machine<'_> {
    fn call(&mut self, function: usize, args: Vec<Word>) -> Vec<Word> {
        let program = self.program;
        let f = &program.functions[function];
        let mut values: Vec<Option<Word>> = vec![None; f.types.len()];
        let mut block = 0;
        let mut args = args;
        loop {
            let b = &f.blocks[block];
            for (param, word) in b.params.iter().zip(args) {
                values[param.0] = Some(word);
            }
            for instruction in &b.instructions {
                let get = |v: &Value| values[v.0].expect("a value that is defined");
                let given = match &instruction.op {
                    Op::Constant(Constant::Int(n)) => {
                        let bits = bits_of(f.types[instruction.results[0].0]);
                        vec![Word::Int(n & mask(bits))]
                    }
                    Op::Binary { op, a, b } => {
                        let bits = bits_of(f.types[instruction.results[0].0]);
                        let (Word::Int(x), Word::Int(y)) = (get(a), get(b)) else {
                            panic!("arithmetic on what is not an integer")
                        };
                        let size = bits.count();
                        let n = match op {
                            Binary::Add => x.wrapping_add(y),
                            Binary::Sub => x.wrapping_sub(y),
                            Binary::Mul => x.wrapping_mul(y),
                            Binary::MulHighU => ((x as u128 * y as u128) >> size) as u64,
                            Binary::MulHighS => {
                                ((signed(x, bits) * signed(y, bits)) >> size) as u64
                            }
                            Binary::And => x & y,
                            Binary::Xor => x ^ y,
                        };
                        vec![Word::Int(n & mask(bits))]
                    }
                    Op::Compare { op, a, b } => {
                        let bits = bits_of(f.types[a.0]);
                        let (Word::Int(x), Word::Int(y)) = (get(a), get(b)) else {
                            panic!("a comparison of what is not integers")
                        };
                        let yes = match op {
                            Compare::Equal => x == y,
                            Compare::LessU => x < y,
                            Compare::LessS => signed(x, bits) < signed(y, bits),
                        };
                        vec![Word::Int(yes as u64)]
                    }
                    Op::Widen {
                        signed: read,
                        value,
                    } => {
                        let from = bits_of(f.types[value.0]);
                        let to = bits_of(f.types[instruction.results[0].0]);
                        let Word::Int(n) = get(value) else {
                            panic!("a widening of what is not an integer")
                        };
                        let n = if *read { signed(n, from) as u64 } else { n };
                        vec![Word::Int(n & mask(to))]
                    }
                    Op::Narrow(value) => {
                        let to = bits_of(f.types[instruction.results[0].0]);
                        let Word::Int(n) = get(value) else {
                            panic!("a narrowing of what is not an integer")
                        };
                        vec![Word::Int(n & mask(to))]
                    }
                    Op::Choose {
                        on,
                        then,
                        otherwise,
                    } => {
                        let Word::Int(on) = get(on) else {
                            panic!("a choice on what is not an integer")
                        };
                        vec![if on != 0 { get(then) } else { get(otherwise) }]
                    }
                    Op::Constant(Constant::Null) => vec![Word::Address(0)],
                    Op::Constant(Constant::Data(t)) => vec![Word::Address(self.starts[*t])],
                    Op::Call { function, args } => {
                        let args = args.iter().map(get).collect();
                        self.call(*function, args)
                    }
                    Op::System {
                        call: System::Write(_),
                        args,
                    } => {
                        let (Word::Address(at), Word::Int(count)) = (get(&args[0]), get(&args[1]))
                        else {
                            panic!("a write that is not of an address and a count")
                        };
                        if count > 0 {
                            let from = at - BASE;
                            self.stdout
                                .extend_from_slice(&self.memory[from..from + count as usize]);
                        }
                        Vec::new()
                    }
                };
                for (result, word) in instruction.results.iter().zip(given) {
                    values[result.0] = Some(word);
                }
            }
            let jump = |j: &Jump| -> (usize, Vec<Word>) {
                let args = j.args.iter().map(|v| values[v.0].unwrap()).collect();
                (j.block, args)
            };
            match &b.end {
                End::Return(returned) => {
                    return returned.iter().map(|v| values[v.0].unwrap()).collect();
                }
                End::Jump(j) => (block, args) = jump(j),
                End::Switch { on, jumps } => {
                    let Some(Word::Int(n)) = values[on.0] else {
                        panic!("a choice on what is not an integer")
                    };
                    (block, args) = jump(&jumps[n as usize]);
                }
            }
        }
    }
}
