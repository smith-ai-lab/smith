//! Runs a program in the scalar form, in-process. What it gives is what the
//! program does.

use super::{Constant, End, Jump, Op, Program, System, Value};
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
                    Op::Constant(Constant::Int(n)) => vec![Word::Int(*n)],
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
