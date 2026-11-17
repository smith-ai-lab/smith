//! Runs a program in the scalar form, in-process. What it gives is what the
//! program does.

use super::{Binary, Bits, Compare, Constant, End, Jump, Op, Program, System, Type, Value};

/// What a program did: what it wrote to standard output, and the status it
/// ended with.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub stdout: Vec<u8>,
    pub status: i32,
}

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
    /// How many bytes the system will still give.
    left: usize,
}

/// The system refused to give memory, so the program ends.
struct Refused;

/// Runs the program, with standard output going to memory, where no write
/// is refused, and no request for memory is either.
pub fn run(program: &Program) -> Outcome {
    run_with_limit(program, usize::MAX)
}

/// Runs the program as `run` does, except that the system gives no more
/// than `limit` bytes in all, and a request for more ends the program with
/// status 1.
pub fn run_with_limit(program: &Program, limit: usize) -> Outcome {
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
        left: limit,
    };
    let status = match machine.call(program.main, Vec::new()) {
        Ok(_) => 0,
        Err(Refused) => 1,
    };
    Outcome {
        stdout: machine.stdout,
        status,
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
        left: usize::MAX,
    };
    let words = args.iter().map(|n| Word::Int(*n)).collect();
    (machine
        .call(function, words)
        .ok()
        .expect("memory")
        .into_iter())
    .map(|w| match w {
        Word::Int(n) => n,
        Word::Address(_) => panic!("a function that gives an address"),
    })
    .collect()
}

/// The bits of an integer of the size, in a word.
pub(super) fn mask(bits: Bits) -> u64 {
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

/// What `op` gives for two integers of the size, as bits of that size. The
/// evaluator and the pass that folds constants both work it out here.
pub(super) fn binary(op: Binary, bits: Bits, x: u64, y: u64) -> u64 {
    let size = bits.count();
    let n = match op {
        Binary::Add => x.wrapping_add(y),
        Binary::Sub => x.wrapping_sub(y),
        Binary::Mul => x.wrapping_mul(y),
        Binary::MulHighU => ((x as u128 * y as u128) >> size) as u64,
        Binary::MulHighS => ((signed(x, bits) * signed(y, bits)) >> size) as u64,
        Binary::And => x & y,
        Binary::Xor => x ^ y,
    };
    n & mask(bits)
}

/// 1 if the two integers of the size are as `op` asks, and 0 if not.
pub(super) fn compare(op: Compare, bits: Bits, x: u64, y: u64) -> u64 {
    (match op {
        Compare::Equal => x == y,
        Compare::LessU => x < y,
        Compare::LessS => signed(x, bits) < signed(y, bits),
    }) as u64
}

/// The integer of `from` bits widened to `to` bits, read as signed or not.
pub(super) fn widen(read_signed: bool, from: Bits, to: Bits, n: u64) -> u64 {
    let n = if read_signed {
        signed(n, from) as u64
    } else {
        n
    };
    n & mask(to)
}

/// The low bits of the integer, as many as `to` has.
pub(super) fn narrow(to: Bits, n: u64) -> u64 {
    n & mask(to)
}

/// How many bits an integer type has.
pub(super) fn bits_of(ty: Type) -> Bits {
    match ty {
        Type::Int(bits) => bits,
        Type::Address => panic!("an address where an integer is needed"),
    }
}

impl Machine<'_> {
    fn call(&mut self, function: usize, args: Vec<Word>) -> Result<Vec<Word>, Refused> {
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
                    Op::Binary { a, b, .. }
                        if f.types[instruction.results[0].0] == Type::Address =>
                    {
                        let (Word::Address(x), Word::Int(y)) = (get(a), get(b)) else {
                            panic!("a move of what is not an address by a number")
                        };
                        vec![Word::Address(x.wrapping_add(y as usize))]
                    }
                    Op::Binary { op, a, b } => {
                        let bits = bits_of(f.types[instruction.results[0].0]);
                        let (Word::Int(x), Word::Int(y)) = (get(a), get(b)) else {
                            panic!("arithmetic on what is not an integer")
                        };
                        vec![Word::Int(binary(*op, bits, x, y))]
                    }
                    Op::Compare { op, a, b } => {
                        let bits = bits_of(f.types[a.0]);
                        let (Word::Int(x), Word::Int(y)) = (get(a), get(b)) else {
                            panic!("a comparison of what is not integers")
                        };
                        vec![Word::Int(compare(*op, bits, x, y))]
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
                        vec![Word::Int(widen(*read, from, to, n))]
                    }
                    Op::Narrow(value) => {
                        let to = bits_of(f.types[instruction.results[0].0]);
                        let Word::Int(n) = get(value) else {
                            panic!("a narrowing of what is not an integer")
                        };
                        vec![Word::Int(narrow(to, n))]
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
                        self.call(*function, args)?
                    }
                    Op::Load { ty, address } => {
                        let Word::Address(at) = get(address) else {
                            panic!("a read through what is not an address")
                        };
                        let size = bits_of(*ty).count() as usize / 8;
                        let from = at - BASE;
                        let mut n = 0u64;
                        for (k, byte) in self.memory[from..from + size].iter().enumerate() {
                            n |= (*byte as u64) << (8 * k);
                        }
                        vec![Word::Int(n)]
                    }
                    Op::Store { ty, address, value } => {
                        let (Word::Address(at), Word::Int(n)) = (get(address), get(value)) else {
                            panic!("a write of what is not a number through an address")
                        };
                        let size = bits_of(*ty).count() as usize / 8;
                        let from = at - BASE;
                        for k in 0..size {
                            self.memory[from + k] = (n >> (8 * k)) as u8;
                        }
                        Vec::new()
                    }
                    Op::System {
                        call: System::Allocate,
                        args,
                    } => {
                        let Word::Int(count) = get(&args[0]) else {
                            panic!("a request for memory that is not a number")
                        };
                        if count > self.left as u64 {
                            return Err(Refused);
                        }
                        self.left -= count as usize;
                        let at = BASE + self.memory.len();
                        self.memory.resize(self.memory.len() + count as usize, 0);
                        vec![Word::Address(at)]
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
                    return Ok(returned.iter().map(|v| values[v.0].unwrap()).collect());
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
