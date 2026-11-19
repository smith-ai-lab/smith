//! WebAssembly: a module of 32-bit WebAssembly, written by the compiler, and
//! the file of JavaScript that loads it.
//!
//! A value is a local. A function that has more than one block is a loop
//! that reads the number of the block to run in a local and goes to its code
//! through one `br_table`, so a jump is that number and a `br`, and no
//! block's place in the function decides what the form means. An integer of
//! 8, 16, or 32 bits is an `i32` with zeros above its size, one of 64 bits is
//! an `i64`, and an address is an `i32`, held in memory as the 8 bytes the
//! form says, so a list is laid out as it is for every target.

use super::wasm_code::*;
use super::wasm_module::{Body, Export, Global, Module, Signature};
use crate::scalar::{
    Binary, Bits, Builder, Compare, Constant, End, Function, Instruction, Jump, Op, Program,
    Stream, System, Type, Value,
};

/// Where the first of the program's texts is. Nothing is below it, so no text
/// is at the address that holds nothing.
const DATA_START: u32 = 16;

/// The global that holds where the memory that is free begins, and the one that
/// holds the status the program ended with, which is below 0 until it has.
const HEAP: u32 = 0;
const STATUS: u32 = 1;

/// How many bytes a page of the memory has.
const PAGE: i64 = 1 << 16;

/// What the module asks of its host.
struct Imports {
    /// Whether the program writes to a stream: the operation `print` of the
    /// effect `console`.
    print: bool,
}

/// What a module of the program asks of its host, by the effect and the
/// operation, in the order of the imports.
pub fn imports(program: &Program) -> Vec<(&'static str, &'static str)> {
    let mut found = Vec::new();
    if imports_of(program).print {
        found.push(("console", "print"));
    }
    found
}

fn imports_of(program: &Program) -> Imports {
    let instructions = program.functions.iter().flat_map(|f| &f.blocks);
    let print = instructions.flat_map(|b| &b.instructions).any(|i| {
        matches!(
            i.op,
            Op::System {
                call: System::Write(_),
                ..
            }
        )
    });
    Imports { print }
}

/// Whether an instruction needs the high half of a product of 64 bits.
fn needs_high(f: &Function, i: &Instruction) -> bool {
    match &i.op {
        Op::Binary {
            op: Binary::MulHighU | Binary::MulHighS,
            ..
        } => f.types[i.results[0].0] == Type::Int(Bits::B64),
        Op::Overflows {
            op: Binary::Mul, a, ..
        } => f.types[a.0] == Type::Int(Bits::B64),
        _ => false,
    }
}

/// The module for the program.
pub fn emit(program: &Program) -> Vec<u8> {
    let imports = imports_of(program);
    let mut module = Module::default();
    let mut print = None;
    if imports.print {
        // The stream, as the number a file has for it, and the bytes.
        print = Some(module.import(
            "console",
            "print",
            Signature {
                params: vec![Kind::I32; 3],
                results: vec![],
            },
        ));
    }
    let image = image(program);

    let needs_high = (program.functions.iter()).any(|f| {
        f.blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .any(|i| needs_high(f, i))
    });
    let mut functions: Vec<&Function> = program.functions.iter().collect();
    let helpers = if needs_high {
        let first = program.functions.len();
        vec![high_unsigned(), high_signed(first)]
    } else {
        Vec::new()
    };
    functions.extend(&helpers);

    let first = u32::from(imports.print);
    let ends = first + functions.len() as u32;
    let context = Context {
        starts: &image.starts,
        print,
        high_unsigned: first + program.functions.len() as u32,
        high_signed: first + program.functions.len() as u32 + 1,
        allocate: ends,
        exit: ends + 1,
        first,
    };
    for f in &functions {
        let (signature, body) = lower(f, &context);
        module.function(signature, body);
    }
    module.function(
        Signature {
            params: vec![Kind::I64],
            results: vec![Kind::I32],
        },
        allocate(),
    );
    module.function(
        Signature {
            params: vec![Kind::I32],
            results: vec![],
        },
        exit(),
    );
    let start = module.function(
        Signature {
            params: vec![],
            results: vec![],
        },
        start(program, &image, first + program.main as u32),
    );
    let status = module.function(
        Signature {
            params: vec![],
            results: vec![Kind::I32],
        },
        status(),
    );
    module.pages = (image.heap as usize).div_ceil(PAGE as usize).max(1) as u32;
    module.globals = vec![
        Global {
            initial: image.heap as i32,
        },
        Global { initial: -1 },
    ];
    module.export("memory", Export::Memory);
    module.export("main", Export::Function(start));
    module.export("status", Export::Function(status));
    if !image.bytes.is_empty() {
        module.data.push((DATA_START, image.bytes));
    }
    module.finish()
}

/// What is in memory when the program starts.
struct Image {
    /// Where each of the program's texts is.
    starts: Vec<u32>,
    bytes: Vec<u8>,
    /// Where the words the program was started with are laid out, as Linux
    /// lays them out, when `main` takes them: the program's name and no more.
    arguments: Option<u32>,
    /// Where the memory the program asks for begins.
    heap: u32,
}

fn image(program: &Program) -> Image {
    let mut bytes = Vec::new();
    let mut starts = Vec::new();
    for text in &program.data {
        starts.push(DATA_START + bytes.len() as u32);
        bytes.extend_from_slice(text);
    }
    let mut arguments = None;
    if program.takes_arguments() {
        bytes.resize(bytes.len().next_multiple_of(8), 0);
        let name = DATA_START + bytes.len() as u32;
        bytes.extend_from_slice(b"program\0");
        arguments = Some(DATA_START + bytes.len() as u32);
        for word in [u64::from(name), 0] {
            bytes.extend_from_slice(&word.to_le_bytes());
        }
    }
    let heap = (DATA_START + bytes.len() as u32).next_multiple_of(16);
    Image {
        starts,
        bytes,
        arguments,
        heap,
    }
}

struct Context<'a> {
    starts: &'a [u32],
    print: Option<u32>,
    high_unsigned: u32,
    high_signed: u32,
    allocate: u32,
    exit: u32,
    /// The number of the first function the program defines.
    first: u32,
}

fn kind(ty: Type) -> Kind {
    match ty {
        Type::Int(Bits::B64) => Kind::I64,
        _ => Kind::I32,
    }
}

/// The high half of a product of two unsigned numbers of 64 bits, from the
/// halves of each: `u1 * v1` and the carries of the three products that
/// are in the middle.
fn high_unsigned() -> Function {
    let mut f = Builder::new();
    let [a, b] = f.params(&[Type::Int(Bits::B64); 2])[..] else {
        unreachable!()
    };
    let low = f.int(Bits::B64, 0xFFFF_FFFF);
    let half = f.int(Bits::B64, 32);
    let u0 = f.binary(Binary::And, a, low);
    let u1 = f.binary(Binary::ShiftRightU, a, half);
    let v0 = f.binary(Binary::And, b, low);
    let v1 = f.binary(Binary::ShiftRightU, b, half);
    let w0 = f.binary(Binary::Mul, u0, v0);
    let carry = f.binary(Binary::ShiftRightU, w0, half);
    let u1v0 = f.binary(Binary::Mul, u1, v0);
    let t = f.binary(Binary::Add, u1v0, carry);
    let w1 = f.binary(Binary::And, t, low);
    let w2 = f.binary(Binary::ShiftRightU, t, half);
    let u0v1 = f.binary(Binary::Mul, u0, v1);
    let w1 = f.binary(Binary::Add, u0v1, w1);
    let carry = f.binary(Binary::ShiftRightU, w1, half);
    let u1v1 = f.binary(Binary::Mul, u1, v1);
    let sum = f.binary(Binary::Add, u1v1, w2);
    let high = f.binary(Binary::Add, sum, carry);
    f.ret(vec![high]);
    f.finish(&[Type::Int(Bits::B64)])
}

/// The high half of a product of two signed numbers of 64 bits: that of the
/// unsigned numbers less each number that is negative's other.
fn high_signed(unsigned: usize) -> Function {
    let mut f = Builder::new();
    let [a, b] = f.params(&[Type::Int(Bits::B64); 2])[..] else {
        unreachable!()
    };
    let high = f.call(unsigned, vec![a, b], &[Type::Int(Bits::B64)])[0];
    let zero = f.int(Bits::B64, 0);
    let a_below = f.compare(Compare::LessS, a, zero);
    let b_below = f.compare(Compare::LessS, b, zero);
    let from_a = f.choose(a_below, b, zero);
    let from_b = f.choose(b_below, a, zero);
    let high = f.binary(Binary::Sub, high, from_a);
    let high = f.binary(Binary::Sub, high, from_b);
    f.ret(vec![high]);
    f.finish(&[Type::Int(Bits::B64)])
}

/// Ends the program with a failing status, as a request the host refuses does.
fn fail(code: &mut Code) {
    code.i32_const(1);
    code.global_set(STATUS);
    code.op(UNREACHABLE);
}

/// The function that gives the address of as many bytes as a 64-bit number
/// says, taken from the memory after the last that was given, which it grows
/// by the pages the request needs. A request that cannot be met ends the
/// program with status 1.
fn allocate() -> Body {
    let mut code = Code::default();
    // A request of 2^32 bytes or more cannot be met.
    code.local_get(0);
    code.i64_const(32);
    code.op(I64_SHR_U);
    code.op(I64_EQZ);
    code.op(I32_EQZ);
    code.open(IF);
    fail(&mut code);
    code.op(END);
    // Where the request would end, and where the memory does.
    code.global_get(HEAP);
    code.op(I64_EXTEND_I32_U);
    code.local_get(0);
    code.op(I64_ADD);
    code.local_set(1);
    code.memory_size();
    code.op(I64_EXTEND_I32_U);
    code.i64_const(16);
    code.op(I64_SHL);
    code.local_set(2);
    code.local_get(1);
    code.local_get(2);
    code.op(I64_GT_U);
    code.open(IF);
    // Past the greatest address, no page can be had.
    code.local_get(1);
    code.i64_const(0xFFFF_FFFF);
    code.op(I64_GT_U);
    code.open(IF);
    fail(&mut code);
    code.op(END);
    // The pages that hold the bytes that are missing.
    code.local_get(1);
    code.local_get(2);
    code.op(I64_SUB);
    code.i64_const(PAGE - 1);
    code.op(I64_ADD);
    code.i64_const(16);
    code.op(I64_SHR_U);
    code.op(I32_WRAP_I64);
    code.memory_grow();
    code.i32_const(-1);
    code.op(I32_EQ);
    code.open(IF);
    fail(&mut code);
    code.op(END);
    code.op(END);
    code.global_get(HEAP);
    code.local_get(1);
    code.op(I32_WRAP_I64);
    code.global_set(HEAP);
    code.op(END);
    Body {
        locals: vec![(2, Kind::I64)],
        code: code.bytes,
    }
}

/// The function that ends the program with the status, a byte: it keeps the
/// status and traps, and the host reads the status back.
fn exit() -> Body {
    let mut code = Code::default();
    code.local_get(0);
    code.i32_const(255);
    code.op(I32_AND);
    code.global_set(STATUS);
    code.op(UNREACHABLE);
    code.op(END);
    Body {
        locals: vec![],
        code: code.bytes,
    }
}

/// The function the host calls: it runs `main`, which takes the words the
/// program was started with when it asks, and there are none but the
/// program's name.
fn start(program: &Program, image: &Image, main: u32) -> Body {
    let mut code = Code::default();
    if program.takes_arguments() {
        code.i64_const(1);
        code.i32_const(image.arguments.unwrap_or(0) as i32);
        code.i32_const(0);
    }
    code.call(main);
    code.op(END);
    Body {
        locals: vec![],
        code: code.bytes,
    }
}

/// The function that gives the status the program ended with, or -1.
fn status() -> Body {
    let mut code = Code::default();
    code.global_get(STATUS);
    code.op(END);
    Body {
        locals: vec![],
        code: code.bytes,
    }
}

/// The signature and the body of a function of the form.
fn lower(f: &Function, context: &Context) -> (Signature, Body) {
    let params = &f.blocks[0].params;
    let mut local = vec![u32::MAX; f.types.len()];
    for (place, p) in params.iter().enumerate() {
        local[p.0] = place as u32;
    }
    // The place that holds the block to run comes first of the locals of 32
    // bits, which come before those of 64.
    let mut next = params.len() as u32;
    let pc = next;
    let mut locals = Vec::new();
    for here in [Kind::I32, Kind::I64] {
        let before = next;
        if here == Kind::I32 && f.blocks.len() > 1 {
            next += 1;
        }
        for (v, ty) in f.types.iter().enumerate() {
            if local[v] == u32::MAX && kind(*ty) == here {
                local[v] = next;
                next += 1;
            }
        }
        if next > before {
            locals.push((next - before, here));
        }
    }
    let mut lowering = Lowering {
        f,
        context,
        code: Code::default(),
        local,
        pc,
        block: 0,
    };
    lowering.function();
    let signature = Signature {
        params: params.iter().map(|p| kind(f.types[p.0])).collect(),
        results: f.results.iter().map(|t| kind(*t)).collect(),
    };
    (
        signature,
        Body {
            locals,
            code: lowering.code.bytes,
        },
    )
}

struct Lowering<'a> {
    f: &'a Function,
    context: &'a Context<'a>,
    code: Code,
    /// The local of each value.
    local: Vec<u32>,
    /// The local that holds the number of the block to run.
    pc: u32,
    /// The block being written.
    block: usize,
}

impl Lowering<'_> {
    fn function(&mut self) {
        let n = self.f.blocks.len();
        self.code.open(LOOP);
        for _ in 0..n {
            self.code.open(BLOCK);
        }
        if n > 1 {
            self.code.local_get(self.pc);
            self.code.br_table(n as u32);
        }
        for block in 0..n {
            self.code.op(END);
            self.block = block;
            self.write_block();
        }
        self.code.op(END);
        self.code.op(UNREACHABLE);
        self.code.op(END);
    }

    fn write_block(&mut self) {
        let f = self.f;
        let block = &f.blocks[self.block];
        for instruction in &block.instructions {
            self.instruction(instruction);
        }
        match &block.end {
            End::Return(values) => {
                for v in values {
                    self.get(*v);
                }
                self.code.op(RETURN);
            }
            End::Jump(jump) => self.jump(jump, 0),
            End::Switch { on, jumps } => self.switch(*on, jumps),
        }
    }

    fn get(&mut self, v: Value) {
        self.code.local_get(self.local[v.0]);
    }

    fn set(&mut self, v: Value) {
        self.code.local_set(self.local[v.0]);
    }

    fn kind(&self, v: Value) -> Kind {
        kind(self.f.types[v.0])
    }

    fn bits(&self, v: Value) -> Bits {
        match self.f.types[v.0] {
            Type::Int(bits) => bits,
            Type::Address => panic!("an address where an integer is needed"),
        }
    }

    /// Gives a number of 64 bits, or an address, a place in 32.
    fn get_wrapped(&mut self, v: Value) {
        self.get(v);
        if self.kind(v) == Kind::I64 {
            self.code.op(I32_WRAP_I64);
        }
    }

    /// Makes the jump: the values it gives are put in the parameters of its
    /// block at once, and the loop of the function goes on from there. `open` is
    /// how many blocks and choices are open here besides those that make the
    /// function's own.
    fn jump(&mut self, jump: &Jump, open: u32) {
        let f = self.f;
        for a in &jump.args {
            self.get(*a);
        }
        for p in f.blocks[jump.block].params.iter().rev() {
            self.set(*p);
        }
        let n = f.blocks.len();
        if n > 1 {
            self.code.i32_const(jump.block as i32);
            self.code.local_set(self.pc);
        }
        self.code.br((n - 1 - self.block) as u32 + open);
    }

    fn switch(&mut self, on: Value, jumps: &[Jump]) {
        let m = jumps.len() as u32;
        match jumps {
            [only] => self.jump(only, 0),
            [zero, one] => {
                self.get_wrapped(on);
                self.code.open(IF);
                self.jump(one, 1);
                self.code.op(ELSE);
                self.jump(zero, 1);
                self.code.op(END);
            }
            _ => {
                for _ in 0..m {
                    self.code.open(BLOCK);
                }
                self.get_wrapped(on);
                self.code.br_table(m);
                for (place, jump) in jumps.iter().enumerate() {
                    self.code.op(END);
                    self.jump(jump, m - 1 - place as u32);
                }
            }
        }
    }

    /// Writes the integer of `bits` bits that is on the stack with the bits
    /// above its size cleared, which only a size below 32 bits has.
    fn clear_above(&mut self, bits: Bits) {
        match bits {
            Bits::B8 => {
                self.code.i32_const(0xFF);
                self.code.op(I32_AND);
            }
            Bits::B16 => {
                self.code.i32_const(0xFFFF);
                self.code.op(I32_AND);
            }
            Bits::B32 | Bits::B64 => {}
        }
    }

    /// Makes the integer of `bits` bits that is on the stack, in 32, the
    /// number it stands for as a signed number.
    fn sign_extend(&mut self, bits: Bits) {
        match bits {
            Bits::B8 => self.code.op(I32_EXTEND8_S),
            Bits::B16 => self.code.op(I32_EXTEND16_S),
            Bits::B32 | Bits::B64 => {}
        }
    }

    /// Puts a value of up to 32 bits on the stack as a number of 64, read as a
    /// signed or an unsigned number.
    fn get_extended(&mut self, v: Value, signed: bool) {
        let bits = self.bits(v);
        self.get(v);
        if signed {
            self.sign_extend(bits);
            self.code.op(I64_EXTEND_I32_S);
        } else {
            self.code.op(I64_EXTEND_I32_U);
        }
    }

    /// The number of bits a shift moves by, in 32: the second value, modulo 64.
    fn shift_count(&mut self, b: Value) {
        self.get_wrapped(b);
        self.code.i32_const(63);
        self.code.op(I32_AND);
    }

    fn instruction(&mut self, instruction: &Instruction) {
        let f = self.f;
        let results = &instruction.results;
        match &instruction.op {
            Op::Constant(constant) => {
                let ty = f.types[results[0].0];
                match (constant, ty) {
                    (Constant::Int(n), Type::Int(Bits::B64)) => self.code.i64_const(*n as i64),
                    (Constant::Int(n), Type::Int(bits)) => {
                        let mask = if bits == Bits::B32 {
                            u64::from(u32::MAX)
                        } else {
                            (1u64 << bits.count()) - 1
                        };
                        self.code.i32_const((*n & mask) as u32 as i32);
                    }
                    (Constant::Null, _) | (Constant::Int(_), Type::Address) => {
                        self.code.i32_const(0)
                    }
                    (Constant::Data(text), _) => {
                        self.code.i32_const(self.context.starts[*text] as i32)
                    }
                }
                self.set(results[0]);
            }
            Op::Binary { op, a, b } => {
                match f.types[results[0].0] {
                    Type::Address => {
                        self.get(*a);
                        self.get_wrapped(*b);
                        self.code.op(I32_ADD);
                    }
                    Type::Int(bits) => self.binary(*op, bits, *a, *b),
                }
                self.set(results[0]);
            }
            Op::Compare { op, a, b } => {
                self.compare(*op, *a, *b);
                self.set(results[0]);
            }
            Op::Overflows { op, signed, a, b } => {
                let bits = self.bits(*a);
                self.binary(*op, bits, *a, *b);
                self.set(results[0]);
                self.overflowed(*op, *signed, bits, *a, *b, results[0]);
                self.set(results[1]);
            }
            Op::Widen { signed, value } => {
                let from = self.bits(*value);
                let to = self.bits(results[0]);
                self.get(*value);
                if to == Bits::B64 {
                    if *signed {
                        self.sign_extend(from);
                        self.code.op(I64_EXTEND_I32_S);
                    } else {
                        self.code.op(I64_EXTEND_I32_U);
                    }
                } else if *signed {
                    self.sign_extend(from);
                    self.clear_above(to);
                }
                self.set(results[0]);
            }
            Op::Narrow(value) => {
                let to = self.bits(results[0]);
                self.get_wrapped(*value);
                self.clear_above(to);
                self.set(results[0]);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } => {
                self.get(*then);
                self.get(*otherwise);
                self.get(*on);
                self.code.op(SELECT);
                self.set(results[0]);
            }
            Op::Load { ty, address } => {
                self.get(*address);
                match ty {
                    Type::Int(Bits::B8) => self.code.memory(I32_LOAD8_U),
                    Type::Int(Bits::B16) => self.code.memory(I32_LOAD16_U),
                    Type::Int(Bits::B32) => self.code.memory(I32_LOAD),
                    Type::Int(Bits::B64) => self.code.memory(I64_LOAD),
                    Type::Address => {
                        self.code.memory(I64_LOAD);
                        self.code.op(I32_WRAP_I64);
                    }
                }
                self.set(results[0]);
            }
            Op::Store { ty, address, value } => {
                self.get(*address);
                self.get(*value);
                match ty {
                    Type::Int(Bits::B8) => self.code.memory(I32_STORE8),
                    Type::Int(Bits::B16) => self.code.memory(I32_STORE16),
                    Type::Int(Bits::B32) => self.code.memory(I32_STORE),
                    Type::Int(Bits::B64) => self.code.memory(I64_STORE),
                    Type::Address => {
                        self.code.op(I64_EXTEND_I32_U);
                        self.code.memory(I64_STORE);
                    }
                }
            }
            Op::Call { function, args } => {
                for a in args {
                    self.get(*a);
                }
                self.code.call(self.context.first + *function as u32);
                for r in results.iter().rev() {
                    self.set(*r);
                }
            }
            Op::System { call, args } => self.system(*call, args, results),
        }
    }

    fn system(&mut self, call: System, args: &[Value], results: &[Value]) {
        match call {
            System::Write(stream) => {
                let print = self.context.print.expect("a write is an import");
                self.code.i32_const(match stream {
                    Stream::Stdout => 1,
                    Stream::Stderr => 2,
                });
                self.get(args[0]);
                self.get_wrapped(args[1]);
                self.code.call(print);
            }
            System::Allocate => {
                self.get(args[0]);
                self.code.call(self.context.allocate);
                self.set(results[0]);
            }
            System::Exit => {
                self.get_wrapped(args[0]);
                self.code.call(self.context.exit);
            }
        }
    }

    fn compare(&mut self, op: Compare, a: Value, b: Value) {
        let bits = self.bits(a);
        let wide = bits == Bits::B64;
        let signed = op == Compare::LessS;
        self.get(a);
        if !wide && signed {
            self.sign_extend(bits);
        }
        self.get(b);
        if !wide && signed {
            self.sign_extend(bits);
        }
        self.code.op(match (op, wide) {
            (Compare::Equal, false) => I32_EQ,
            (Compare::Equal, true) => I64_EQ,
            (Compare::LessU, false) => I32_LT_U,
            (Compare::LessU, true) => I64_LT_U,
            (Compare::LessS, false) => I32_LT_S,
            (Compare::LessS, true) => I64_LT_S,
        });
    }

    /// Puts what `op` gives of the two integers of `bits` bits on the stack,
    /// with the bits that do not fit left off.
    fn binary(&mut self, op: Binary, bits: Bits, a: Value, b: Value) {
        if bits == Bits::B64 {
            return self.binary_64(op, a, b);
        }
        let size = bits.count() as i32;
        match op {
            Binary::Add | Binary::Sub | Binary::Mul => {
                self.get(a);
                self.get(b);
                self.code.op(match op {
                    Binary::Add => I32_ADD,
                    Binary::Sub => I32_SUB,
                    _ => I32_MUL,
                });
                self.clear_above(bits);
            }
            Binary::And | Binary::Or | Binary::Xor => {
                self.get(a);
                self.get(b);
                self.code.op(match op {
                    Binary::And => I32_AND,
                    Binary::Or => I32_OR,
                    _ => I32_XOR,
                });
            }
            Binary::MulHighU if bits == Bits::B32 => {
                self.get_extended(a, false);
                self.get_extended(b, false);
                self.code.op(I64_MUL);
                self.code.i64_const(32);
                self.code.op(I64_SHR_U);
                self.code.op(I32_WRAP_I64);
            }
            Binary::MulHighU => {
                self.get(a);
                self.get(b);
                self.code.op(I32_MUL);
                self.code.i32_const(size);
                self.code.op(I32_SHR_U);
            }
            Binary::MulHighS if bits == Bits::B32 => {
                self.get_extended(a, true);
                self.get_extended(b, true);
                self.code.op(I64_MUL);
                self.code.i64_const(32);
                self.code.op(I64_SHR_S);
                self.code.op(I32_WRAP_I64);
            }
            Binary::MulHighS => {
                self.get(a);
                self.sign_extend(bits);
                self.get(b);
                self.sign_extend(bits);
                self.code.op(I32_MUL);
                self.code.i32_const(size);
                self.code.op(I32_SHR_S);
                self.clear_above(bits);
            }
            // A shift by as many bits as the integer has, or more, leaves
            // none of them, and the engine takes the count modulo 32.
            Binary::ShiftLeft => {
                self.get(a);
                self.shift_count(b);
                self.code.op(I32_SHL);
                self.clear_above(bits);
                self.code.i32_const(0);
                self.shift_count(b);
                self.code.i32_const(size);
                self.code.op(I32_LT_U);
                self.code.op(SELECT);
            }
            Binary::ShiftRightU => {
                self.get(a);
                self.shift_count(b);
                self.code.op(I32_SHR_U);
                self.code.i32_const(0);
                self.shift_count(b);
                self.code.i32_const(32);
                self.code.op(I32_LT_U);
                self.code.op(SELECT);
            }
            Binary::ShiftRightS => {
                self.get(a);
                self.sign_extend(bits);
                // A count of 31 or more gives the sign alone, as 31 does.
                self.shift_count(b);
                self.code.i32_const(31);
                self.shift_count(b);
                self.code.i32_const(31);
                self.code.op(I32_LT_U);
                self.code.op(SELECT);
                self.code.op(I32_SHR_S);
                self.clear_above(bits);
            }
        }
    }

    fn binary_64(&mut self, op: Binary, a: Value, b: Value) {
        match op {
            Binary::MulHighU | Binary::MulHighS => {
                self.get(a);
                self.get(b);
                self.code.call(match op {
                    Binary::MulHighU => self.context.high_unsigned,
                    _ => self.context.high_signed,
                });
            }
            Binary::ShiftLeft | Binary::ShiftRightU | Binary::ShiftRightS => {
                self.get(a);
                self.get(b);
                if self.kind(b) == Kind::I32 {
                    self.code.op(I64_EXTEND_I32_U);
                }
                // The engine takes the count modulo 64.
                self.code.op(match op {
                    Binary::ShiftLeft => I64_SHL,
                    Binary::ShiftRightU => I64_SHR_U,
                    _ => I64_SHR_S,
                });
            }
            _ => {
                self.get(a);
                self.get(b);
                self.code.op(match op {
                    Binary::Add => I64_ADD,
                    Binary::Sub => I64_SUB,
                    Binary::Mul => I64_MUL,
                    Binary::And => I64_AND,
                    Binary::Or => I64_OR,
                    _ => I64_XOR,
                });
            }
        }
    }

    /// Puts on the stack whether the number `result`, which `op` gave of `a`
    /// and `b` as `bits`-bit numbers read as signed or as unsigned, is not what
    /// `op` gives of them.
    fn overflowed(
        &mut self,
        op: Binary,
        signed: bool,
        bits: Bits,
        a: Value,
        b: Value,
        result: Value,
    ) {
        if bits == Bits::B64 {
            return self.overflowed_64(op, signed, a, b, result);
        }
        if op == Binary::Sub && !signed {
            self.get(a);
            self.get(b);
            self.code.op(I32_LT_U);
            return;
        }
        // Worked out in 64 bits, which holds every number of these sizes, a
        // product too.
        self.get_extended(a, signed);
        self.get_extended(b, signed);
        self.code.op(match op {
            Binary::Add => I64_ADD,
            Binary::Sub => I64_SUB,
            _ => I64_MUL,
        });
        if signed {
            self.get_extended(result, true);
            self.code.op(I64_NE);
        } else {
            self.code.i64_const(i64::from(bits.count()));
            self.code.op(I64_SHR_U);
            self.code.op(I64_EQZ);
            self.code.op(I32_EQZ);
        }
    }

    fn overflowed_64(&mut self, op: Binary, signed: bool, a: Value, b: Value, result: Value) {
        match (op, signed) {
            (Binary::Add, false) => {
                self.get(result);
                self.get(a);
                self.code.op(I64_LT_U);
            }
            (Binary::Sub, false) => {
                self.get(a);
                self.get(b);
                self.code.op(I64_LT_U);
            }
            // A sum overflows when its sign is not that of either operand, which
            // have the same, and a difference when its sign is not that of the
            // first, and the operands have different signs.
            (Binary::Add, true) => {
                self.get(a);
                self.get(result);
                self.code.op(I64_XOR);
                self.get(b);
                self.get(result);
                self.code.op(I64_XOR);
                self.code.op(I64_AND);
                self.code.i64_const(0);
                self.code.op(I64_LT_S);
            }
            (Binary::Sub, true) => {
                self.get(a);
                self.get(b);
                self.code.op(I64_XOR);
                self.get(a);
                self.get(result);
                self.code.op(I64_XOR);
                self.code.op(I64_AND);
                self.code.i64_const(0);
                self.code.op(I64_LT_S);
            }
            (_, false) => {
                self.get(a);
                self.get(b);
                self.code.call(self.context.high_unsigned);
                self.code.i64_const(0);
                self.code.op(I64_NE);
            }
            (_, true) => {
                self.get(a);
                self.get(b);
                self.code.call(self.context.high_signed);
                self.get(result);
                self.code.i64_const(63);
                self.code.op(I64_SHR_S);
                self.code.op(I64_NE);
            }
        }
    }
}
