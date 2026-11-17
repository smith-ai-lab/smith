//! x86-64 machine code, shared by every target that runs on x86-64, and
//! how a program in the scalar form becomes it. It reads the scalar form
//! and nothing else.

use super::alloc::{self, Home, Registers};
use super::frame::{self, call_area, round_up, size};
use super::moves::{self, Move, Step};
use crate::scalar::{
    Binary, Compare, Constant, End, Function, Op, Program, Stream, System as Call, Value,
};

/// A register, by its number in the instruction encoding.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Reg(u8);

pub const RAX: Reg = Reg(0);
pub const RCX: Reg = Reg(1);
pub const RDX: Reg = Reg(2);
pub const RBX: Reg = Reg(3);
pub const RBP: Reg = Reg(5);
pub const RSI: Reg = Reg(6);
pub const RDI: Reg = Reg(7);
pub const R8: Reg = Reg(8);
pub const R9: Reg = Reg(9);
pub const R10: Reg = Reg(10);
pub const R11: Reg = Reg(11);
pub const R12: Reg = Reg(12);
pub const R13: Reg = Reg(13);
pub const R14: Reg = Reg(14);
pub const R15: Reg = Reg(15);

/// The registers that hold values, in the order the allocator numbers them.
/// A call may change the first five, and leaves the other six as they were,
/// on every system. Every register that is left is kept for the chip: rax,
/// rcx, and rdx, which the instructions below work in, as multiplying and
/// comparing need them, r11, which holds the value that breaks a circle of
/// copies, and rsp.
const HOMES: [Reg; 11] = [RDI, RSI, R8, R9, R10, RBX, RBP, R12, R13, R14, R15];

/// A call passes its arguments in the first five, `rdi`, `rsi`, `r8`, `r9`,
/// and `r10`, and the function gives its results in the same five, each in
/// the order it has. What does not fit goes in the caller's frame.
const REGISTERS: Registers = Registers {
    changed: 5,
    preserved: 6,
    passed: 5,
};

/// What a 32-bit offset in the code refers to.
enum Ref {
    /// A label in the code or in the data after it.
    Label(usize),
    /// A pointer slot outside the code, such as one the loader fills.
    Slot(usize),
}

/// Where a read or a write of memory is.
#[derive(Clone, Copy)]
pub enum Place {
    /// `[rsp + disp]`.
    Frame(u32),
    /// `[reg]`.
    At(Reg),
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

    /// `test r32, r32`.
    pub fn test32(&mut self, r: Reg) {
        if r.0 >= 8 {
            self.bytes(&[0x45]);
        }
        self.bytes(&[0x85, Self::modrm(r, r)]);
    }

    /// `mov r32, r32`, which clears the upper half.
    fn mov32_rr(&mut self, rd: Reg, rs: Reg) {
        if rd.0 >= 8 || rs.0 >= 8 {
            self.bytes(&[0x40 | (rs.0 >> 3) << 2 | rd.0 >> 3]);
        }
        self.bytes(&[0x89, Self::modrm(rs, rd)]);
    }

    /// `movzx r32, r8`, the low byte of `rs`.
    fn movzx8(&mut self, rd: Reg, rs: Reg) {
        if rs.0 >= 4 || rd.0 >= 8 {
            self.bytes(&[0x40 | (rd.0 >> 3) << 2 | rs.0 >> 3]);
        }
        self.bytes(&[0x0f, 0xb6, Self::modrm(rd, rs)]);
    }

    /// `movzx r32, r16`, the low 16 bits of `rs`.
    fn movzx16(&mut self, rd: Reg, rs: Reg) {
        if rd.0 >= 8 || rs.0 >= 8 {
            self.bytes(&[0x40 | (rd.0 >> 3) << 2 | rs.0 >> 3]);
        }
        self.bytes(&[0x0f, 0xb7, Self::modrm(rd, rs)]);
    }

    /// `movsx r64, r8`, `r16`, or `r32`: the low `width` bytes of `rs`,
    /// extended as a signed number.
    fn movsx(&mut self, rd: Reg, rs: Reg, width: u32) {
        self.bytes(&[Self::rex_w(rd, rs)]);
        match width {
            1 => self.bytes(&[0x0f, 0xbe]),
            2 => self.bytes(&[0x0f, 0xbf]),
            4 => self.bytes(&[0x63]),
            _ => unreachable!("an integer of {width} bytes"),
        }
        self.bytes(&[Self::modrm(rd, rs)]);
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
        let reg = (reg.0 & 7) << 3;
        match place {
            Place::Frame(disp) if disp < 0x80 => self.bytes(&[0x44 | reg, 0x24, disp as u8]),
            Place::Frame(disp) => {
                self.bytes(&[0x84 | reg, 0x24]);
                self.bytes(&disp.to_le_bytes());
            }
            // rsp and r12 take a SIB byte, and rbp and r13 a displacement.
            Place::At(base) => match base.0 & 7 {
                4 => self.bytes(&[reg | 4, 0x24]),
                5 => self.bytes(&[0x40 | reg | 5, 0]),
                n => self.bytes(&[reg | n]),
            },
        }
    }

    /// The register the place is based on, as a number, for the REX prefix.
    fn base(place: Place) -> u8 {
        match place {
            Place::Frame(_) => 0,
            Place::At(base) => base.0,
        }
    }

    /// Reads an integer of `width` bytes into the whole of `rd`, extended to
    /// 64 bits as its sign says when `signed` and with zeros when not.
    pub fn load(&mut self, rd: Reg, width: u32, signed: bool, from: Place) {
        let rex =
            (u8::from(width == 8 || signed) << 3) | ((rd.0 >> 3) << 2) | (Self::base(from) >> 3);
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

    /// Writes the low `width` bytes of `rs`.
    pub fn store(&mut self, to: Place, rs: Reg, width: u32) {
        if width == 2 {
            self.bytes(&[0x66]);
        }
        let rex = (u8::from(width == 8) << 3) | ((rs.0 >> 3) << 2) | (Self::base(to) >> 3);
        // The low byte of rsi, rdi, rbp, and rsp is reached with a prefix.
        if rex != 0 || (width == 1 && rs.0 >= 4) {
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
    /// refuses it. It may change rax, rcx, rdx, and the registers a call may
    /// change, and leaves the others as they were, which it may do by
    /// holding them in the bytes at the bottom of the frame.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u32);
    /// Asks the system for memory for the number of bytes in rsi, which is
    /// not 0, leaves its address in rax, and jumps to `fail` if the system
    /// refuses. It may change what `write` may.
    fn alloc(asm: &mut Asm, fail: usize);
}

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack that keeps the stack on 16
/// bytes at each call. From the bottom, it holds the bytes the system may
/// use, the places of the calls it makes, the values that have no register,
/// and the registers it must put back. A function finds its parameters in
/// its caller's frame, and puts what it gives back just after them.
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

/// Where a value is held, in the code.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Loc {
    Register(Reg),
    /// `[rsp + disp]`.
    Frame(u32),
}

/// Where the values of a call go, in order: in the registers that carry them
/// as many as there are, and then in the places in the frame.
fn passing(frame: impl Iterator<Item = u32>) -> Vec<Loc> {
    let mut places: Vec<Loc> = HOMES[..REGISTERS.passed]
        .iter()
        .map(|r| Loc::Register(*r))
        .collect();
    places.extend(frame.map(Loc::Frame));
    places
}

/// A function being written, and where each value is.
struct Writer<'a, S> {
    program: &'a Program,
    f: &'a Function,
    texts: &'a [usize],
    functions: &'a [usize],
    fail: usize,
    /// How far below its caller's frame the function's frame reaches, with
    /// the return address.
    frame: u32,
    /// Where each value is.
    at: Vec<Loc>,
    /// Where each parameter of the function arrives, and where each result
    /// is left.
    arrive: Vec<Loc>,
    leave: Vec<Loc>,
    /// The registers a call does not change that the function uses, and
    /// where it keeps what they held.
    saved: Vec<(Reg, u32)>,
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
    let allocation = alloc::allocate(f, REGISTERS);
    let below = S::RESERVED + frame::outgoing(program, f, REGISTERS.passed);
    let saved_from = round_up(below + allocation.frame, 8);
    // The call pushed the return address, so the frame takes 8 bytes more
    // than a multiple of 16.
    let total = round_up(saved_from + 8 * allocation.saved.len() as u32, 16) + 8;
    let area = call_area(f, REGISTERS.passed);
    let in_caller = |places: &[u32]| -> Vec<Loc> {
        passing(places.iter().map(|o| total + 8 + S::RESERVED + o))
    };
    let arrive = in_caller(&area.params);
    let leave = in_caller(&area.results);
    let at = (allocation.homes.iter())
        .map(|home| match home {
            Home::Register(r) => Loc::Register(HOMES[*r]),
            Home::Frame(place) => Loc::Frame(below + place),
        })
        .collect();
    let saved = (allocation.saved.iter().enumerate())
        .map(|(i, r)| (HOMES[*r], saved_from + 8 * i as u32))
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
        arrive,
        leave,
        saved,
        labels,
        system: std::marker::PhantomData,
    };
    asm.reserve_stack(total);
    for (r, at) in &w.saved {
        asm.store(Place::Frame(*at), *r, 8);
    }
    // The function's own parameters are in registers, and in the caller's
    // frame when there are more than registers.
    let moves: Vec<Move<Loc>> = (f.blocks[0].params.iter().zip(&w.arrive))
        .map(|(p, from)| Move {
            to: w.at[p.0],
            from: *from,
            width: w.width(*p),
        })
        .collect();
    w.run(asm, &moves);
    for (b, block) in f.blocks.iter().enumerate() {
        asm.bind(w.labels[b]);
        for instruction in &block.instructions {
            w.instruction(asm, &instruction.results, &instruction.op);
        }
        w.end(asm, b);
    }
}

impl<S: System> Writer<'_, S> {
    fn width(&self, v: Value) -> u32 {
        size(self.f.types[v.0])
    }

    /// Copies `width` bytes from one place to another, through rax when both
    /// are in the frame.
    fn copy(&self, asm: &mut Asm, to: Loc, from: Loc, width: u32) {
        match (to, from) {
            (Loc::Register(to), Loc::Register(from)) => {
                if to != from {
                    asm.mov(to, from);
                }
            }
            (Loc::Register(to), Loc::Frame(from)) => {
                asm.load(to, width, false, Place::Frame(from));
            }
            (Loc::Frame(to), Loc::Register(from)) => {
                asm.store(Place::Frame(to), from, width);
            }
            (Loc::Frame(to), Loc::Frame(from)) => {
                asm.load(RAX, width, false, Place::Frame(from));
                asm.store(Place::Frame(to), RAX, width);
            }
        }
    }

    /// Does the moves as if all were copied at once. A place that another
    /// move is still to read is held in r11 first, when they go in a circle.
    fn run(&self, asm: &mut Asm, moves: &[Move<Loc>]) {
        for step in moves::sequence(moves) {
            match step {
                Step::Copy { to, from, width } => self.copy(asm, to, from, width),
                Step::Save { from, width } => self.copy(asm, Loc::Register(R11), from, width),
                Step::Restore { to, width } => self.copy(asm, to, Loc::Register(R11), width),
            }
        }
    }

    /// A register that holds the value, with zeros above it: its own, or rax,
    /// rcx, or rdx, the one `tmp` says, with the value read into it.
    fn read(&self, asm: &mut Asm, tmp: Reg, v: Value) -> Reg {
        match self.at[v.0] {
            Loc::Register(r) => r,
            Loc::Frame(at) => {
                asm.load(tmp, self.width(v), false, Place::Frame(at));
                tmp
            }
        }
    }

    /// Reads the value into the register, with zeros above it.
    fn read_into(&self, asm: &mut Asm, rd: Reg, v: Value) {
        self.copy(asm, Loc::Register(rd), self.at[v.0], self.width(v));
    }

    /// Reads the value into the register, extended as a signed number.
    fn read_signed_into(&self, asm: &mut Asm, rd: Reg, v: Value) {
        let width = self.width(v);
        match self.at[v.0] {
            Loc::Register(r) if width == 8 => {
                if r != rd {
                    asm.mov(rd, r);
                }
            }
            Loc::Register(r) => asm.movsx(rd, r, width),
            Loc::Frame(at) => asm.load(rd, width, true, Place::Frame(at)),
        }
    }

    /// A register that holds the value extended as a signed number: its own
    /// when it takes 64 bits, and otherwise `tmp`.
    fn read_signed(&self, asm: &mut Asm, tmp: Reg, v: Value) -> Reg {
        match self.at[v.0] {
            Loc::Register(r) if self.width(v) == 8 => r,
            _ => {
                self.read_signed_into(asm, tmp, v);
                tmp
            }
        }
    }

    /// The register to work out a result in: its own, unless that is one of
    /// the registers `avoid` that the work still reads, and then rax.
    fn target(&self, v: Value, avoid: &[Reg]) -> Reg {
        match self.at[v.0] {
            Loc::Register(r) if !avoid.contains(&r) => r,
            _ => RAX,
        }
    }

    /// Puts the result, which is in the low bytes of `r`, in its home. The
    /// bits above it are zeros when `extended`, and anything when not.
    fn finish(&self, asm: &mut Asm, v: Value, r: Reg, extended: bool) {
        let width = self.width(v);
        match self.at[v.0] {
            Loc::Frame(at) => asm.store(Place::Frame(at), r, width),
            Loc::Register(h) if extended || width == 8 => {
                if h != r {
                    asm.mov(h, r);
                }
            }
            Loc::Register(h) => match width {
                4 => asm.mov32_rr(h, r),
                2 => asm.movzx16(h, r),
                _ => asm.movzx8(h, r),
            },
        }
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], op: &Op) {
        match op {
            Op::Constant(c) => {
                let result = results[0];
                let t = self.target(result, &[]);
                match c {
                    Constant::Int(n) => {
                        let n = match self.width(result) {
                            8 => *n,
                            w => n & ((1 << (8 * w)) - 1),
                        };
                        match u32::try_from(n) {
                            Ok(n) => asm.mov32(t, n),
                            Err(_) => asm.mov64(t, n),
                        }
                    }
                    Constant::Null => asm.mov32(t, 0),
                    Constant::Data(d) => asm.address(t, self.texts[*d]),
                }
                self.finish(asm, result, t, true);
            }
            Op::Binary { op, a, b } => self.binary(asm, results[0], *op, *a, *b),
            Op::Compare { op, a, b } => {
                let (ra, rb) = if matches!(op, Compare::LessS) {
                    (
                        self.read_signed(asm, RAX, *a),
                        self.read_signed(asm, RCX, *b),
                    )
                } else {
                    (self.read(asm, RAX, *a), self.read(asm, RCX, *b))
                };
                asm.cmp(ra, rb);
                asm.truth(match op {
                    Compare::Equal => Z,
                    Compare::LessU => B,
                    Compare::LessS => L,
                });
                self.finish(asm, results[0], RAX, true);
            }
            Op::Widen { signed, value } => {
                let t = self.target(results[0], &[]);
                if *signed {
                    self.read_signed_into(asm, t, *value);
                } else {
                    self.read_into(asm, t, *value);
                }
                self.finish(asm, results[0], t, !*signed);
            }
            Op::Narrow(value) => {
                let t = self.target(results[0], &[]);
                self.read_into(asm, t, *value);
                self.finish(asm, results[0], t, false);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } => {
                let on = self.read(asm, RDX, *on);
                let then = self.read(asm, RCX, *then);
                let t = self.target(results[0], &[on, then]);
                asm.test32(on);
                self.read_into(asm, t, *otherwise);
                asm.cmovne(t, then);
                self.finish(asm, results[0], t, true);
            }
            Op::Load { ty, address } => {
                let address = self.read(asm, RAX, *address);
                let t = self.target(results[0], &[]);
                asm.load(t, size(*ty), false, Place::At(address));
                self.finish(asm, results[0], t, true);
            }
            Op::Store { ty, address, value } => {
                let address = self.read(asm, RAX, *address);
                let value = self.read(asm, RCX, *value);
                asm.store(Place::At(address), value, size(*ty));
            }
            Op::Call { function, args } => {
                let area = call_area(&self.program.functions[*function], REGISTERS.passed);
                let at_bottom = |places: &[u32]| passing(places.iter().map(|o| S::RESERVED + o));
                let params = at_bottom(&area.params);
                let moves: Vec<Move<Loc>> = (args.iter().zip(&params))
                    .map(|(arg, to)| Move {
                        to: *to,
                        from: self.at[arg.0],
                        width: self.width(*arg),
                    })
                    .collect();
                self.run(asm, &moves);
                asm.call(self.functions[*function]);
                let moves: Vec<Move<Loc>> = (results.iter().zip(at_bottom(&area.results)))
                    .map(|(result, from)| Move {
                        to: self.at[result.0],
                        from,
                        width: self.width(*result),
                    })
                    .collect();
                self.run(asm, &moves);
            }
            Op::System {
                call: Call::Write(stream),
                args,
            } => {
                let text = [S::TEXT.0, S::TEXT.1];
                let moves: Vec<Move<Loc>> = (args.iter().zip(text))
                    .map(|(arg, to)| Move {
                        to: Loc::Register(to),
                        from: self.at[arg.0],
                        width: self.width(*arg),
                    })
                    .collect();
                self.run(asm, &moves);
                S::write(asm, *stream, self.fail);
            }
            Op::System {
                call: Call::Allocate,
                args,
            } => {
                self.read_into(asm, RSI, args[0]);
                S::alloc(asm, self.fail);
                self.finish(asm, results[0], RAX, true);
            }
        }
    }

    fn binary(&self, asm: &mut Asm, result: Value, op: Binary, a: Value, b: Value) {
        let bits = self.width(result) * 8;
        let wide = bits == 64;
        match op {
            Binary::MulHighU | Binary::MulHighS if wide => {
                self.read_into(asm, RAX, a);
                let b = self.read(asm, RCX, b);
                asm.group(if matches!(op, Binary::MulHighU) { 4 } else { 5 }, b);
                self.finish(asm, result, RDX, true);
            }
            // The product of two integers of fewer than 64 bits fits in
            // rax, and its high half is what is left after the shift.
            Binary::MulHighU => {
                self.read_into(asm, RAX, a);
                let b = self.read(asm, RCX, b);
                asm.imul(RAX, b);
                asm.shr_imm(RAX, bits as u8);
                self.finish(asm, result, RAX, false);
            }
            Binary::MulHighS => {
                let a = self.read_signed(asm, RAX, a);
                let b = self.read_signed(asm, RCX, b);
                asm.mov(RDX, a);
                asm.imul(RDX, b);
                asm.sar_imm(RDX, bits as u8);
                self.finish(asm, result, RDX, false);
            }
            _ => {
                let b = self.read(asm, RCX, b);
                let t = self.target(result, &[b]);
                self.read_into(asm, t, a);
                match op {
                    Binary::Add => asm.add(t, b),
                    Binary::Sub => asm.sub(t, b),
                    Binary::Mul => asm.imul(t, b),
                    Binary::And => asm.and(t, b),
                    Binary::Xor => asm.xor(t, b),
                    Binary::MulHighU | Binary::MulHighS => unreachable!(),
                }
                self.finish(asm, result, t, matches!(op, Binary::And | Binary::Xor));
            }
        }
    }

    /// Makes the jump: gives the block its parameters as if all were copied
    /// at once, and goes there, unless it is the next block.
    fn jump(&self, asm: &mut Asm, jump: &crate::scalar::Jump, next: usize) {
        let target = &self.f.blocks[jump.block];
        let moves: Vec<Move<Loc>> = (jump.args.iter().zip(&target.params))
            .map(|(from, to)| Move {
                to: self.at[to.0],
                from: self.at[from.0],
                width: self.width(*to),
            })
            .collect();
        self.run(asm, &moves);
        if jump.block != next {
            asm.jmp(self.labels[jump.block]);
        }
    }

    fn end(&self, asm: &mut Asm, block: usize) {
        let next = block + 1;
        match &self.f.blocks[block].end {
            End::Jump(jump) => self.jump(asm, jump, next),
            End::Switch { on, jumps } => {
                // Each jump but the last is chosen by comparing the number
                // with its place, and the last is made when none was.
                let on = self.read(asm, RAX, *on);
                for (i, jump) in jumps.iter().enumerate() {
                    let skip = asm.label();
                    if i + 1 < jumps.len() {
                        match i8::try_from(i) {
                            Ok(i) => asm.cmp_imm8(on, i),
                            Err(_) => asm.cmp_imm32(on, i as u32),
                        }
                        asm.jcc(NE, skip);
                    }
                    // Only the last may fall through to the next block, as
                    // the others are followed by the comparison for the next.
                    let falls = if i + 1 == jumps.len() {
                        next
                    } else {
                        usize::MAX
                    };
                    self.jump(asm, jump, falls);
                    asm.bind(skip);
                }
            }
            End::Return(values) => {
                let moves: Vec<Move<Loc>> = (values.iter().zip(&self.leave))
                    .map(|(value, to)| Move {
                        to: *to,
                        from: self.at[value.0],
                        width: self.width(*value),
                    })
                    .collect();
                self.run(asm, &moves);
                for (r, at) in &self.saved {
                    asm.load(*r, 8, false, Place::Frame(*at));
                }
                asm.release_stack(self.frame);
                asm.ret();
            }
        }
    }
}
