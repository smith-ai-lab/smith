//! x86-64 machine code, shared by every target that runs on x86-64, and
//! how a program in the scalar form becomes it. It reads the scalar form
//! and nothing else.

use crate::scalar::{
    Binary, Bits, Compare, Constant, End, Function, Jump, Op, Program, Stream, System as Call,
    Type, Value,
};

/// A register, by its number in the instruction encoding.
#[derive(Clone, Copy)]
pub struct Reg(u8);

pub const RAX: Reg = Reg(0);
pub const RCX: Reg = Reg(1);
pub const RDX: Reg = Reg(2);
pub const RBX: Reg = Reg(3);
pub const RSI: Reg = Reg(6);
pub const RDI: Reg = Reg(7);
pub const R8: Reg = Reg(8);
pub const R9: Reg = Reg(9);
pub const R10: Reg = Reg(10);
pub const R12: Reg = Reg(12);

/// What a 32-bit offset in the code refers to.
enum Ref {
    /// A label in the code or in the data after it.
    Label(usize),
    /// A pointer slot outside the code, such as one the loader fills.
    Slot(usize),
}

/// Where a read or a write of memory is.
#[derive(Clone, Copy)]
enum Place {
    /// `[rsp + disp]`.
    Frame(u32),
    /// `[rax]`.
    AtRax,
}

/// Machine code with labels, each resolved once the code and its data are
/// laid out.
#[derive(Default)]
pub struct Asm {
    code: Vec<u8>,
    labels: Vec<Option<usize>>,
    /// Places holding a 32-bit offset, from the end of those four bytes, to
    /// what they refer to.
    fixups: Vec<(usize, Ref)>,
}

impl Asm {
    pub fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    pub fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
    }

    /// How many bytes of code there are so far.
    pub fn len(&self) -> usize {
        self.code.len()
    }

    fn bytes(&mut self, bytes: &[u8]) {
        self.code.extend_from_slice(bytes);
    }

    fn offset_to(&mut self, to: Ref) {
        self.fixups.push((self.code.len(), to));
        self.bytes(&[0; 4]);
    }

    /// The REX prefix for a 64-bit operation on `reg` and `rm`.
    fn rex_w(reg: Reg, rm: Reg) -> u8 {
        0x48 | (reg.0 >> 3) << 2 | rm.0 >> 3
    }

    /// The ModRM byte for two registers.
    fn modrm(reg: Reg, rm: Reg) -> u8 {
        0xc0 | (reg.0 & 7) << 3 | (rm.0 & 7)
    }

    /// `mov r32, imm32`, which clears the upper half.
    pub fn mov32(&mut self, rd: Reg, value: u32) {
        if rd.0 >= 8 {
            self.bytes(&[0x41]);
        }
        self.bytes(&[0xb8 | (rd.0 & 7)]);
        self.bytes(&value.to_le_bytes());
    }

    /// `mov r64, imm64`.
    pub fn mov64(&mut self, rd: Reg, value: u64) {
        self.bytes(&[Self::rex_w(RAX, rd), 0xb8 | (rd.0 & 7)]);
        self.bytes(&value.to_le_bytes());
    }

    /// `mov rd, rs`.
    pub fn mov(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x89, Self::modrm(rs, rd)]);
    }

    /// `test r64, r64`.
    pub fn test(&mut self, r: Reg) {
        self.bytes(&[Self::rex_w(r, r), 0x85, Self::modrm(r, r)]);
    }

    /// `test r32, r32`, for a register below r8.
    pub fn test32(&mut self, r: Reg) {
        assert!(r.0 < 8);
        self.bytes(&[0x85, Self::modrm(r, r)]);
    }

    /// `cmp ra, rb`, which compares `ra - rb`.
    pub fn cmp(&mut self, ra: Reg, rb: Reg) {
        self.bytes(&[Self::rex_w(rb, ra), 0x39, Self::modrm(rb, ra)]);
    }

    /// `cmova rd, rs`: copies `rs` when the last comparison found its first
    /// operand above its second, as unsigned numbers.
    pub fn cmova(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rd, rs), 0x0f, 0x47, Self::modrm(rd, rs)]);
    }

    /// `cmovne rd, rs`: copies `rs` when the last test found a value that is
    /// not 0.
    fn cmovne(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rd, rs), 0x0f, 0x45, Self::modrm(rd, rs)]);
    }

    /// The ModRM, SIB, and 8-bit displacement of `[rsp + disp]`, with `reg`
    /// in the ModRM reg field.
    fn stack_slot(&mut self, reg: u8, disp: i8) {
        self.bytes(&[0x44 | (reg & 7) << 3, 0x24, disp as u8]);
    }

    /// `lea rd, [rsp + disp]`.
    pub fn stack_address(&mut self, rd: Reg, disp: i8) {
        self.bytes(&[Self::rex_w(rd, RAX), 0x8d]);
        self.stack_slot(rd.0, disp);
    }

    /// `mov qword [rsp + disp], imm32`, sign-extended.
    pub fn store_stack_imm32(&mut self, disp: i8, value: u32) {
        self.bytes(&[0x48, 0xc7]);
        self.stack_slot(0, disp);
        self.bytes(&value.to_le_bytes());
    }

    /// `mov r32, dword [rsp + disp]`, which clears the upper half, for a
    /// register below r8.
    pub fn load32_stack(&mut self, rd: Reg, disp: i8) {
        assert!(rd.0 < 8);
        self.bytes(&[0x8b]);
        self.stack_slot(rd.0, disp);
    }

    /// `add rd, rs`.
    pub fn add(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x01, Self::modrm(rs, rd)]);
    }

    /// `sub rd, rs`.
    pub fn sub(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x29, Self::modrm(rs, rd)]);
    }

    /// `cmp r64, imm8`, sign-extended.
    pub fn cmp_imm8(&mut self, r: Reg, value: i8) {
        self.bytes(&[
            Self::rex_w(RAX, r),
            0x83,
            Self::modrm(Reg(7), r),
            value as u8,
        ]);
    }

    /// `cmp r64, imm32`, sign-extended.
    fn cmp_imm32(&mut self, r: Reg, value: u32) {
        self.bytes(&[Self::rex_w(RAX, r), 0x81, Self::modrm(Reg(7), r)]);
        self.bytes(&value.to_le_bytes());
    }

    /// `cmp dword [r], imm8`, sign-extended, for a register below r8 other
    /// than rsp and rbp.
    pub fn cmp_load32_imm8(&mut self, r: Reg, value: i8) {
        assert!(r.0 < 8 && r.0 != 4 && r.0 != 5);
        self.bytes(&[0x83, 7 << 3 | r.0, value as u8]);
    }

    /// The ModRM, and what follows it, of a place, with `reg` in the ModRM
    /// reg field.
    fn place(&mut self, reg: Reg, place: Place) {
        match place {
            Place::Frame(disp) => {
                self.bytes(&[0x84 | (reg.0 & 7) << 3, 0x24]);
                self.bytes(&disp.to_le_bytes());
            }
            Place::AtRax => self.bytes(&[(reg.0 & 7) << 3]),
        }
    }

    /// Reads an integer of `width` bytes into the whole of `rd`, extended to
    /// 64 bits as its sign says when `signed` and with zeros when not.
    fn load(&mut self, rd: Reg, width: u32, signed: bool, from: Place) {
        let rex = (u8::from(width == 8 || signed) << 3) | ((rd.0 >> 3) << 2);
        if rex != 0 {
            self.bytes(&[0x40 | rex]);
        }
        match (width, signed) {
            (1, false) => self.bytes(&[0x0f, 0xb6]),
            (2, false) => self.bytes(&[0x0f, 0xb7]),
            (1, true) => self.bytes(&[0x0f, 0xbe]),
            (2, true) => self.bytes(&[0x0f, 0xbf]),
            (4, true) => self.bytes(&[0x63]),
            (4, false) | (8, _) => self.bytes(&[0x8b]),
            _ => unreachable!("an integer of {width} bytes"),
        }
        self.place(rd, from);
    }

    /// Writes the low `width` bytes of `rs`. A single byte comes from rax,
    /// rcx, rdx, or rbx.
    fn store(&mut self, to: Place, rs: Reg, width: u32) {
        assert!(width != 1 || rs.0 < 4);
        if width == 2 {
            self.bytes(&[0x66]);
        }
        let rex = (u8::from(width == 8) << 3) | ((rs.0 >> 3) << 2);
        if rex != 0 {
            self.bytes(&[0x40 | rex]);
        }
        self.bytes(&[if width == 1 { 0x88 } else { 0x89 }]);
        self.place(rs, to);
    }

    /// `sub rsp, imm32`.
    pub fn reserve_stack(&mut self, bytes: u32) {
        self.bytes(&[0x48, 0x81, 0xec]);
        self.bytes(&bytes.to_le_bytes());
    }

    /// `add rsp, imm32`.
    pub fn release_stack(&mut self, bytes: u32) {
        self.bytes(&[0x48, 0x81, 0xc4]);
        self.bytes(&bytes.to_le_bytes());
    }

    /// `and rsp, -16`, which puts the stack on 16 bytes.
    pub fn align_stack(&mut self) {
        self.bytes(&[0x48, 0x83, 0xe4, 0xf0]);
    }

    /// `call label`.
    fn call(&mut self, label: usize) {
        self.bytes(&[0xe8]);
        self.offset_to(Ref::Label(label));
    }

    /// `ret`.
    fn ret(&mut self) {
        self.bytes(&[0xc3]);
    }

    /// `lea rd, [rip + label]`.
    fn address(&mut self, rd: Reg, label: usize) {
        self.bytes(&[Self::rex_w(rd, RAX), 0x8d, (rd.0 & 7) << 3 | 5]);
        self.offset_to(Ref::Label(label));
    }

    /// `jmp label`.
    pub fn jmp(&mut self, label: usize) {
        self.bytes(&[0xe9]);
        self.offset_to(Ref::Label(label));
    }

    /// `jcc label`, for a condition code such as `Z`.
    pub fn jcc(&mut self, cond: u8, label: usize) {
        self.bytes(&[0x0f, 0x80 | cond]);
        self.offset_to(Ref::Label(label));
    }

    /// `call [rip + slot]`, which calls through a pointer outside the code.
    pub fn call_slot(&mut self, slot: usize) {
        self.bytes(&[0xff, 0x15]);
        self.offset_to(Ref::Slot(slot));
    }

    /// `syscall`.
    pub fn syscall(&mut self) {
        self.bytes(&[0x0f, 0x05]);
    }

    /// `and rd, rs`.
    fn and(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x21, Self::modrm(rs, rd)]);
    }

    /// `xor rd, rs`.
    fn xor(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x31, Self::modrm(rs, rd)]);
    }

    /// `imul rd, rs`, which keeps the low 64 bits.
    fn imul(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rd, rs), 0x0f, 0xaf, Self::modrm(rd, rs)]);
    }

    /// An instruction of the group that multiplies rdx:rax, by the number of
    /// the operation in the ModRM reg field.
    fn group(&mut self, op: u8, r: Reg) {
        self.bytes(&[Self::rex_w(RAX, r), 0xf7, Self::modrm(Reg(op), r)]);
    }

    /// `shr r, imm8`.
    fn shr_imm(&mut self, r: Reg, count: u8) {
        self.bytes(&[Self::rex_w(RAX, r), 0xc1, Self::modrm(Reg(5), r), count]);
    }

    /// `sar r, imm8`.
    fn sar_imm(&mut self, r: Reg, count: u8) {
        self.bytes(&[Self::rex_w(RAX, r), 0xc1, Self::modrm(Reg(7), r), count]);
    }

    /// `setcc al` then `movzx eax, al`: rax is 1 when the condition holds
    /// and 0 when not.
    fn truth(&mut self, cond: u8) {
        self.bytes(&[0x0f, 0x90 | cond, 0xc0, 0x0f, 0xb6, 0xc0]);
    }

    /// The code with each piece of data placed after it under its label.
    /// The code is at `code_address`, and the 8-byte slot `i` is at
    /// `slots_address + 8 * i`.
    pub fn finish(
        mut self,
        data: &[(usize, &[u8])],
        code_address: u64,
        slots_address: u64,
    ) -> Vec<u8> {
        for (label, bytes) in data {
            self.bind(*label);
            self.code.extend_from_slice(bytes);
        }
        for (at, to) in &self.fixups {
            let target = match to {
                Ref::Label(label) => self.labels[*label].expect("every label is bound") as i64,
                Ref::Slot(slot) => (slots_address + 8 * *slot as u64) as i64 - code_address as i64,
            };
            let offset = i32::try_from(target - (*at as i64 + 4)).expect("in reach");
            self.code[*at..*at + 4].copy_from_slice(&offset.to_le_bytes());
        }
        self.code
    }
}

/// The condition codes `jcc` takes.
pub const Z: u8 = 0x4;
pub const NE: u8 = 0x5;
pub const LE: u8 = 0xe;
pub const L: u8 = 0xc;
pub const S: u8 = 0x8;
/// Below, as unsigned numbers.
const B: u8 = 0x2;

/// What a system gives the code of an x86-64 target: how it writes, how it
/// ends the program, and how it asks for memory. These are the three calls
/// into the system the scalar form has, and the only code the targets
/// differ in.
pub trait System {
    /// How many bytes at the bottom of every frame a call to the system may
    /// use. A multiple of 16.
    const RESERVED: u32;
    /// The registers that hold the address and the number of bytes to write.
    const TEXT: (Reg, Reg);
    /// Writes every byte in `TEXT`, and jumps to `fail` if the system
    /// refuses it. It may change any register but rsp.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u32);
    /// Asks the system for memory for the number of bytes in rsi, which is
    /// not 0, leaves its address in rax, and jumps to `fail` if the system
    /// refuses. It may change any register but rsp.
    fn alloc(asm: &mut Asm, fail: usize);
}

/// How many bytes a value of the type takes, and so where it is a multiple
/// of.
fn size(ty: Type) -> u32 {
    match ty {
        Type::Int(Bits::B8) => 1,
        Type::Int(Bits::B16) => 2,
        Type::Int(Bits::B32) => 4,
        Type::Int(Bits::B64) | Type::Address => 8,
    }
}

fn round_up(n: u32, to: u32) -> u32 {
    n.div_ceil(to) * to
}

/// Where values of the types go one after another, each at a multiple of its
/// size, and where they end.
fn lay_out(types: impl Iterator<Item = Type>) -> (Vec<u32>, u32) {
    let mut end = 0;
    let places = types
        .map(|ty| {
            let at = round_up(end, size(ty));
            end = at + size(ty);
            at
        })
        .collect();
    (places, end)
}

/// The places of what a call to the function passes: its parameters, and
/// after them what it gives back, so that writing a result never overwrites
/// a parameter still to be read. It is in the caller's frame, just above the
/// bytes the system may use.
fn call_area(f: &Function) -> (Vec<u32>, u32) {
    let params = f.blocks[0].params.iter().map(|v| f.types[v.0]);
    lay_out(params.chain(f.results.iter().copied()))
}

/// What a function's frame holds, apart from the bytes the system may use
/// and what a caller passes it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Frame {
    /// The bytes for the calls the function makes: the most any one takes,
    /// as `call_area` says.
    pub outgoing: u32,
    /// The bytes its values take: each value but its parameters, in the
    /// order of their numbers, at the next multiple of its size.
    pub held: u32,
    /// Where each value is in those bytes, or `None` for a parameter, which
    /// is passed.
    pub places: Vec<Option<u32>>,
}

/// The frame of each function of the program.
pub fn frames(program: &Program) -> Vec<Frame> {
    program
        .functions
        .iter()
        .map(|f| frame(program, f))
        .collect()
}

fn frame(program: &Program, f: &Function) -> Frame {
    let passed = &f.blocks[0].params;
    let held: Vec<usize> = (0..f.types.len())
        .filter(|v| !passed.contains(&Value(*v)))
        .collect();
    let (at, end) = lay_out(held.iter().map(|v| f.types[*v]));
    let mut places = vec![None; f.types.len()];
    for (v, at) in held.into_iter().zip(at) {
        places[v] = Some(at);
    }
    let mut outgoing = 0;
    for instruction in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = &instruction.op {
            outgoing = outgoing.max(call_area(&program.functions[*function]).1);
        }
    }
    Frame {
        outgoing: round_up(outgoing, 8),
        held: end,
        places,
    }
}

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack that keeps the stack on 16
/// bytes at each call. From the bottom, it holds the bytes the system may
/// use, the places of the calls it makes, and its values. A function finds
/// its parameters in its caller's frame, and puts what it gives back just
/// after them.
pub fn code<S: System>(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
    let mut asm = Asm::default();
    let texts: Vec<usize> = program.data.iter().map(|_| asm.label()).collect();
    let functions: Vec<usize> = program.functions.iter().map(|_| asm.label()).collect();
    let fail = asm.label();
    asm.align_stack();
    asm.reserve_stack(S::RESERVED);
    asm.call(functions[program.main]);
    S::exit(&mut asm, 0);
    asm.bind(fail);
    S::exit(&mut asm, 1);
    for (f, &label) in program.functions.iter().zip(&functions) {
        asm.bind(label);
        function::<S>(&mut asm, program, f, &texts, &functions, fail);
    }
    let data = texts
        .into_iter()
        .zip(program.data.iter().map(Vec::as_slice))
        .collect();
    (asm, data)
}

/// A function being written, and where its frame keeps each value.
struct Writer<'a, S> {
    program: &'a Program,
    f: &'a Function,
    texts: &'a [usize],
    functions: &'a [usize],
    fail: usize,
    /// How far below its caller's frame the function's frame reaches, with
    /// the return address.
    frame: u32,
    /// Where each value is, from the bottom of the frame.
    at: Vec<u32>,
    /// Where each parameter and result of the function is, in the call
    /// area, from the bottom of the frame.
    passed: Vec<u32>,
    labels: Vec<usize>,
    system: std::marker::PhantomData<S>,
}

fn function<S: System>(
    asm: &mut Asm,
    program: &Program,
    f: &Function,
    texts: &[usize],
    functions: &[usize],
    fail: usize,
) {
    let held = frame(program, f);
    let below = S::RESERVED + held.outgoing;
    // The call pushed the return address, so the frame takes 8 bytes more
    // than a multiple of 16.
    let total = round_up(below + held.held, 16) + 8;
    let (offsets, _) = call_area(f);
    let passed: Vec<u32> = offsets
        .iter()
        .map(|o| total + 8 + S::RESERVED + o)
        .collect();
    let at = (held.places.iter().enumerate())
        .map(|(v, place)| match place {
            Some(place) => below + place,
            None => {
                let i = f.blocks[0].params.iter().position(|p| p.0 == v);
                passed[i.expect("a parameter")]
            }
        })
        .collect();
    let labels = f.blocks.iter().map(|_| asm.label()).collect();
    let w = Writer::<S> {
        program,
        f,
        texts,
        functions,
        fail,
        frame: total,
        at,
        passed,
        labels,
        system: std::marker::PhantomData,
    };
    asm.reserve_stack(total);
    for (b, block) in f.blocks.iter().enumerate() {
        asm.bind(w.labels[b]);
        for instruction in &block.instructions {
            w.instruction(asm, &instruction.results, &instruction.op);
        }
        w.end(asm, &block.end);
    }
}

impl<S: System> Writer<'_, S> {
    fn width(&self, v: Value) -> u32 {
        size(self.f.types[v.0])
    }

    fn place(&self, v: Value) -> Place {
        Place::Frame(self.at[v.0])
    }

    /// Reads the value into the register, with zeros above it.
    fn get(&self, asm: &mut Asm, r: Reg, v: Value) {
        asm.load(r, self.width(v), false, self.place(v));
    }

    /// Reads the value into the register, extended as a signed number.
    fn get_signed(&self, asm: &mut Asm, r: Reg, v: Value) {
        asm.load(r, self.width(v), true, self.place(v));
    }

    /// Writes the low bytes of the register to the value, as many as its
    /// type takes.
    fn put(&self, asm: &mut Asm, v: Value, r: Reg) {
        asm.store(self.place(v), r, self.width(v));
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], op: &Op) {
        match op {
            Op::Constant(c) => {
                let result = results[0];
                match c {
                    Constant::Int(n) => {
                        let n = match self.width(result) {
                            8 => *n,
                            w => n & ((1 << (8 * w)) - 1),
                        };
                        match u32::try_from(n) {
                            Ok(n) => asm.mov32(RAX, n),
                            Err(_) => asm.mov64(RAX, n),
                        }
                    }
                    Constant::Null => asm.mov32(RAX, 0),
                    Constant::Data(t) => asm.address(RAX, self.texts[*t]),
                }
                self.put(asm, result, RAX);
            }
            Op::Binary { op, a, b } => self.binary(asm, results[0], *op, *a, *b),
            Op::Compare { op, a, b } => {
                if matches!(op, Compare::LessS) {
                    self.get_signed(asm, RAX, *a);
                    self.get_signed(asm, RCX, *b);
                } else {
                    self.get(asm, RAX, *a);
                    self.get(asm, RCX, *b);
                }
                asm.cmp(RAX, RCX);
                asm.truth(match op {
                    Compare::Equal => Z,
                    Compare::LessU => B,
                    Compare::LessS => L,
                });
                self.put(asm, results[0], RAX);
            }
            Op::Widen { signed, value } => {
                if *signed {
                    self.get_signed(asm, RAX, *value);
                } else {
                    self.get(asm, RAX, *value);
                }
                self.put(asm, results[0], RAX);
            }
            Op::Narrow(value) => {
                self.get(asm, RAX, *value);
                self.put(asm, results[0], RAX);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } => {
                self.get(asm, RAX, *otherwise);
                self.get(asm, RCX, *then);
                self.get(asm, RDX, *on);
                asm.test32(RDX);
                asm.cmovne(RAX, RCX);
                self.put(asm, results[0], RAX);
            }
            Op::Load { ty, address } => {
                self.get(asm, RAX, *address);
                asm.load(RCX, size(*ty), false, Place::AtRax);
                self.put(asm, results[0], RCX);
            }
            Op::Store { ty, address, value } => {
                self.get(asm, RAX, *address);
                self.get(asm, RCX, *value);
                asm.store(Place::AtRax, RCX, size(*ty));
            }
            Op::Call { function, args } => {
                let callee = &self.program.functions[*function];
                let (offsets, _) = call_area(callee);
                for (arg, offset) in args.iter().zip(&offsets) {
                    self.get(asm, RAX, *arg);
                    asm.store(Place::Frame(S::RESERVED + offset), RAX, self.width(*arg));
                }
                asm.call(self.functions[*function]);
                for (result, offset) in results.iter().zip(&offsets[args.len()..]) {
                    asm.load(
                        RAX,
                        self.width(*result),
                        false,
                        Place::Frame(S::RESERVED + offset),
                    );
                    self.put(asm, *result, RAX);
                }
            }
            Op::System {
                call: Call::Write(stream),
                args,
            } => {
                self.get(asm, S::TEXT.0, args[0]);
                self.get(asm, S::TEXT.1, args[1]);
                S::write(asm, *stream, self.fail);
            }
            Op::System {
                call: Call::Allocate,
                args,
            } => {
                self.get(asm, RSI, args[0]);
                S::alloc(asm, self.fail);
                self.put(asm, results[0], RAX);
            }
        }
    }

    fn binary(&self, asm: &mut Asm, result: Value, op: Binary, a: Value, b: Value) {
        let bits = self.width(result) * 8;
        let wide = bits == 64;
        match op {
            Binary::MulHighS => {
                self.get_signed(asm, RAX, a);
                self.get_signed(asm, RCX, b);
            }
            _ => {
                self.get(asm, RAX, a);
                self.get(asm, RCX, b);
            }
        }
        match op {
            Binary::Add => asm.add(RAX, RCX),
            Binary::Sub => asm.sub(RAX, RCX),
            Binary::Mul => asm.imul(RAX, RCX),
            Binary::And => asm.and(RAX, RCX),
            Binary::Xor => asm.xor(RAX, RCX),
            // The product of two integers of fewer than 64 bits fits in
            // rax, and its high half is what is left after the shift.
            Binary::MulHighU if wide => {
                asm.group(4, RCX);
                asm.mov(RAX, RDX);
            }
            Binary::MulHighS if wide => {
                asm.group(5, RCX);
                asm.mov(RAX, RDX);
            }
            Binary::MulHighU => {
                asm.imul(RAX, RCX);
                asm.shr_imm(RAX, bits as u8);
            }
            Binary::MulHighS => {
                asm.imul(RAX, RCX);
                asm.sar_imm(RAX, bits as u8);
            }
        }
        self.put(asm, result, RAX);
    }

    /// Where the values of a jump go, as if all were copied at once: a value
    /// that another is to be copied from is saved, in rdx, before it is
    /// overwritten.
    fn jump(&self, asm: &mut Asm, jump: &Jump) {
        /// Where what a move writes comes from.
        #[derive(Clone, Copy, PartialEq)]
        enum From {
            Frame(u32),
            Saved,
        }
        let target = &self.f.blocks[jump.block];
        let mut moves: Vec<(u32, From, u32)> = (jump.args.iter().zip(&target.params))
            .map(|(from, to)| (self.at[to.0], From::Frame(self.at[from.0]), self.width(*to)))
            .filter(|(to, from, _)| From::Frame(*to) != *from)
            .collect();
        while !moves.is_empty() {
            let free = (0..moves.len()).find(|&i| {
                let to = From::Frame(moves[i].0);
                moves.iter().all(|m| m.1 != to)
            });
            match free {
                Some(i) => {
                    let (to, from, width) = moves.remove(i);
                    if let From::Frame(from) = from {
                        asm.load(RAX, width, false, Place::Frame(from));
                        asm.store(Place::Frame(to), RAX, width);
                    } else {
                        asm.store(Place::Frame(to), RDX, width);
                    }
                }
                // Every place is still to be read, so they go round in a
                // circle. Saving one breaks it.
                None => {
                    let (place, _, width) = moves[0];
                    asm.load(RDX, width, false, Place::Frame(place));
                    for m in &mut moves {
                        if m.1 == From::Frame(place) {
                            m.1 = From::Saved;
                        }
                    }
                }
            }
        }
        asm.jmp(self.labels[jump.block]);
    }

    fn end(&self, asm: &mut Asm, end: &End) {
        match end {
            End::Jump(jump) => {
                self.jump(asm, jump);
            }
            End::Switch { on, jumps } => {
                // Each jump but the last is chosen by comparing the number
                // with its place, and the last is made when none was.
                self.get(asm, RAX, *on);
                for (i, jump) in jumps.iter().enumerate() {
                    let next = asm.label();
                    if i + 1 < jumps.len() {
                        asm.cmp_imm32(RAX, i as u32);
                        asm.jcc(NE, next);
                    }
                    self.jump(asm, jump);
                    asm.bind(next);
                }
            }
            End::Return(values) => {
                let results = &self.passed[self.passed.len() - values.len()..];
                for (value, to) in values.iter().zip(results) {
                    self.get(asm, RAX, *value);
                    asm.store(Place::Frame(*to), RAX, self.width(*value));
                }
                asm.release_stack(self.frame);
                asm.ret();
            }
        }
    }
}
