//! The arm64 instructions the arm64 targets share, with labels resolved
//! once the code and its data are laid out, and how a program in the scalar
//! form becomes them. It reads the scalar form and nothing else.

use super::alloc::{self, Home, Registers};
use super::frame::{self, call_area, round_up, size};
use super::moves::{self, Move, Step};
use crate::scalar::{
    Binary, Compare, Constant, End, Function, Jump, Op, Program, Stream, System as Call, Value,
};

pub const X0: u32 = 0;
pub const X1: u32 = 1;
pub const X2: u32 = 2;
pub const X3: u32 = 3;
pub const X4: u32 = 4;
pub const X5: u32 = 5;
/// Holds the number of a Linux system call.
pub const X8: u32 = 8;
/// Kept across calls, so they hold the place and the length still to write.
pub const X19: u32 = 19;
pub const X20: u32 = 20;
pub const X21: u32 = 21;
/// Kept for the chip, as x16 and x17 are: the register an instruction
/// reaches a place in the frame too far from the stack pointer through.
const X15: u32 = 15;
/// Kept for the chip: the two registers an instruction works in, and where a
/// value that breaks a circle of copies waits.
const X16: u32 = 16;
const X17: u32 = 17;

/// The registers that hold values, in the order the allocator numbers them:
/// x0 to x14, which a call may change, and x19 to x28, which it does not, on
/// every system. The registers that are left are kept for the chip: x15 to
/// x17, x18, which a system may own, x29, x30, and the stack pointer.
const HOMES: [u32; 25] = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
];

const REGISTERS: Registers = Registers {
    changed: 15,
    preserved: 10,
};
/// The link register, which a call puts its return address in.
const X30: u32 = 30;
/// The stack pointer, where an instruction takes it in place of a register.
const SP: u32 = 31;
pub const LE: u32 = 0xd;
pub const EQ: u32 = 0x0;
pub const NE: u32 = 0x1;
pub const LT: u32 = 0xb;
/// Unsigned higher or same.
pub const HS: u32 = 0x2;
/// Signed greater than or equal.
pub const GE: u32 = 0xa;
/// Unsigned lower or same.
pub const LS: u32 = 0x9;

/// A place in the code that refers to something placed later.
enum Fixup {
    /// `b`, with a 26-bit word offset.
    Branch { at: usize, label: usize },
    /// `b.cond` or `cbz`, with a 19-bit word offset.
    Conditional { at: usize, label: usize },
    /// `adrp` then `add`, which form the address of a label.
    Address { at: usize, label: usize },
    /// `adrp` then `ldr`, which load a slot of the table of addresses,
    /// such as `__got`.
    Slot { at: usize, slot: usize },
}

/// Machine code with labels, each resolved once the code and its data are
/// laid out.
#[derive(Default)]
pub struct Asm {
    code: Vec<u8>,
    labels: Vec<Option<usize>>,
    fixups: Vec<Fixup>,
}

impl Asm {
    pub fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    pub fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
    }

    pub fn word(&mut self, word: u32) {
        self.code.extend_from_slice(&word.to_le_bytes());
    }

    pub fn branch(&mut self, label: usize) {
        self.fixups.push(Fixup::Branch {
            at: self.code.len(),
            label,
        });
        self.word(0x1400_0000);
    }

    pub fn cond(&mut self, cond: u32, label: usize) {
        self.fixups.push(Fixup::Conditional {
            at: self.code.len(),
            label,
        });
        self.word(0x5400_0000 | cond);
    }

    pub fn cbz(&mut self, reg: u32, label: usize) {
        self.fixups.push(Fixup::Conditional {
            at: self.code.len(),
            label,
        });
        self.word(0xb400_0000 | reg);
    }

    /// `cbz wt, label`, which looks at the low 32 bits only.
    pub fn cbz_w(&mut self, reg: u32, label: usize) {
        self.fixups.push(Fixup::Conditional {
            at: self.code.len(),
            label,
        });
        self.word(0x3400_0000 | reg);
    }

    /// An instruction on three registers, `xd`, `xn`, and `xm`, from the
    /// bits that choose it.
    pub fn three(&mut self, bits: u32, rd: u32, rn: u32, rm: u32) {
        self.word(bits | rm << 16 | rn << 5 | rd);
    }

    /// `cset xd, cond`, which sets `xd` to 1 if `cond` holds and to 0 if
    /// not. `inverse` is the condition that holds when `cond` does not.
    pub fn cset(&mut self, rd: u32, inverse: u32) {
        self.word(0x9a9f_07e0 | inverse << 12 | rd);
    }

    /// `cmp xn, xm`.
    pub fn cmp(&mut self, rn: u32, rm: u32) {
        self.word(0xeb00_001f | rm << 16 | rn << 5);
    }

    /// `csel xd, xn, xm, cond`, which takes `xn` if `cond` holds and `xm`
    /// if not.
    pub fn csel(&mut self, rd: u32, rn: u32, rm: u32, cond: u32) {
        self.word(0x9a80_0000 | rm << 16 | cond << 12 | rn << 5 | rd);
    }

    /// `sub sp, sp, #bytes`, which makes room on the stack. `bytes` keeps
    /// the stack on 16 bytes.
    pub fn reserve_stack(&mut self, bytes: u32) {
        assert!(bytes.is_multiple_of(16));
        self.add_immediate(0xd100_0000, SP, SP, bytes);
    }

    /// `add sp, sp, #bytes`, which gives back room on the stack.
    pub fn release_stack(&mut self, bytes: u32) {
        assert!(bytes.is_multiple_of(16));
        self.add_immediate(0x9100_0000, SP, SP, bytes);
    }

    /// An `add` or a `sub` of an immediate below 2^24, as at most two
    /// instructions: one for the upper 12 bits, one for the lower.
    fn add_immediate(&mut self, op: u32, rd: u32, rn: u32, value: u32) {
        assert!(value < 1 << 24);
        let (high, low) = (value >> 12, value & 0xfff);
        let mut from = rn;
        if high != 0 {
            self.word(op | 1 << 22 | high << 10 | from << 5 | rd);
            from = rd;
        }
        if low != 0 || from == rn && rd != rn {
            self.word(op | low << 10 | from << 5 | rd);
        }
    }

    /// `bl label`.
    pub fn call_label(&mut self, label: usize) {
        self.fixups.push(Fixup::Branch {
            at: self.code.len(),
            label,
        });
        self.word(0x9400_0000);
    }

    /// `ret`.
    pub fn ret(&mut self) {
        self.word(0xd65f_03c0);
    }

    /// `add xd, sp, #offset`, the address of a place on the stack.
    pub fn stack_address(&mut self, rd: u32, offset: u32) {
        assert!(offset < 1 << 12);
        self.word(0x9100_0000 | offset << 10 | SP << 5 | rd);
    }

    /// `mov xd, xn`.
    pub fn mov(&mut self, rd: u32, rn: u32) {
        self.word(0xaa00_03e0 | rn << 16 | rd);
    }

    /// `movz wd, #value`.
    pub fn mov_w(&mut self, rd: u32, value: u16) {
        self.word(0x5280_0000 | (value as u32) << 5 | rd);
    }

    /// `movz` then a `movk` for each further nonzero half-word.
    pub fn mov64(&mut self, rd: u32, value: u64) {
        self.word(0xd280_0000 | ((value & 0xffff) as u32) << 5 | rd);
        for hw in 1..4u32 {
            let part = ((value >> (16 * hw)) & 0xffff) as u32;
            if part != 0 {
                self.word(0xf280_0000 | hw << 21 | part << 5 | rd);
            }
        }
    }

    /// The address of a label into `rd`.
    pub fn address(&mut self, rd: u32, label: usize) {
        self.fixups.push(Fixup::Address {
            at: self.code.len(),
            label,
        });
        self.word(0x9000_0000 | rd);
        self.word(0x9100_0000 | rd << 5 | rd);
    }

    /// `add xd, xn, xm`.
    pub fn add(&mut self, rd: u32, rn: u32, rm: u32) {
        self.word(0x8b00_0000 | rm << 16 | rn << 5 | rd);
    }

    /// `svc #0`, which makes a system call.
    pub fn svc(&mut self) {
        self.word(0xd400_0001);
    }

    /// Calls the address in a slot of the table of addresses, through x16.
    pub fn call(&mut self, slot: usize) {
        const X16: u32 = 16;
        self.fixups.push(Fixup::Slot {
            at: self.code.len(),
            slot,
        });
        self.word(0x9000_0000 | X16);
        self.word(0xf940_0000 | X16 << 5 | X16);
        self.word(0xd63f_0000 | X16 << 5); // blr x16
    }

    fn patch(&mut self, at: usize, bits: u32) {
        let word = u32::from_le_bytes(self.code[at..at + 4].try_into().unwrap()) | bits;
        self.code[at..at + 4].copy_from_slice(&word.to_le_bytes());
    }

    /// The size of the code so far.
    pub fn len(&self) -> usize {
        self.code.len()
    }

    /// The code at `code_address` with each piece of data placed after it
    /// under its label, and the table of addresses at `slots_address`.
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
        let fixups = std::mem::take(&mut self.fixups);
        for fixup in fixups {
            match fixup {
                Fixup::Branch { at, label } => {
                    let words = self.offset(at, label) / 4;
                    self.patch(at, words as u32 & 0x3ff_ffff);
                }
                Fixup::Conditional { at, label } => {
                    let words = self.offset(at, label) / 4;
                    self.patch(at, (words as u32 & 0x7_ffff) << 5);
                }
                Fixup::Address { at, label } => {
                    let target = code_address + self.place(label) as u64;
                    self.page_and_low(at, code_address + at as u64, target, 0);
                }
                Fixup::Slot { at, slot } => {
                    let target = slots_address + 8 * slot as u64;
                    self.page_and_low(at, code_address + at as u64, target, 3);
                }
            }
        }
        self.code
    }

    fn place(&self, label: usize) -> usize {
        self.labels[label].expect("every label is bound")
    }

    fn offset(&self, at: usize, label: usize) -> i64 {
        self.place(label) as i64 - at as i64
    }

    /// Fills an `adrp` at `at` and the instruction after it, whose 12-bit
    /// offset is scaled down by `scale` bits, so that they reach `target`.
    fn page_and_low(&mut self, at: usize, pc: u64, target: u64, scale: u32) {
        let pages = ((target & !0xfff) as i64 - (pc & !0xfff) as i64) >> 12;
        let pages = pages as u32;
        self.patch(at, (pages & 0x3) << 29 | ((pages >> 2) & 0x7_ffff) << 5);
        let low = (target & 0xfff) as u32 >> scale;
        self.patch(at + 4, low << 10);
    }
}

/// Where a read or a write of memory is.
#[derive(Clone, Copy)]
pub enum Place {
    /// `[sp + offset]`.
    Frame(u32),
    /// `[xn]`.
    At(u32),
}

impl Asm {
    /// The register that holds the address of a place, and the number of
    /// `width` bytes from it that the instruction takes. A place in the frame
    /// too far for the instruction is reached through x17.
    fn reach(&mut self, place: Place, width: u32) -> (u32, u32) {
        match place {
            Place::At(r) => (r, 0),
            Place::Frame(offset) => {
                assert!(offset.is_multiple_of(width));
                if offset / width < 1 << 12 {
                    (SP, offset / width)
                } else {
                    self.add_immediate(0x9100_0000, X15, SP, offset);
                    (X15, 0)
                }
            }
        }
    }

    /// Reads an integer of `width` bytes into the whole of `rt`, extended to
    /// 64 bits as its sign says when `signed` and with zeros when not.
    pub fn load(&mut self, rt: u32, width: u32, signed: bool, from: Place) {
        let (base, at) = self.reach(from, width);
        let op = match (width, signed) {
            (1, false) => 0x3940_0000,
            (2, false) => 0x7940_0000,
            (4, false) => 0xb940_0000,
            (8, _) => 0xf940_0000,
            (1, true) => 0x3980_0000,
            (2, true) => 0x7980_0000,
            (4, true) => 0xb980_0000,
            _ => unreachable!("an integer of {width} bytes"),
        };
        self.word(op | at << 10 | base << 5 | rt);
    }

    /// Writes the low `width` bytes of `rt`.
    pub fn store(&mut self, to: Place, rt: u32, width: u32) {
        let (base, at) = self.reach(to, width);
        let op = match width {
            1 => 0x3900_0000,
            2 => 0x7900_0000,
            4 => 0xb900_0000,
            8 => 0xf900_0000,
            _ => unreachable!("an integer of {width} bytes"),
        };
        self.word(op | at << 10 | base << 5 | rt);
    }

    /// `cmp xn, #value`, for a value below 2^12.
    fn cmp_imm(&mut self, rn: u32, value: u32) {
        assert!(value < 1 << 12);
        self.word(0xf100_001f | value << 10 | rn << 5);
    }

    /// `sxtb`, `sxth`, or `sxtw`: the low `width` bytes of `xn`, extended
    /// as a signed number, into `xd`.
    fn sign_extend(&mut self, rd: u32, rn: u32, width: u32) {
        let op = match width {
            1 => 0x9340_1c00,
            2 => 0x9340_3c00,
            4 => 0x9340_7c00,
            _ => unreachable!("an integer of {width} bytes"),
        };
        self.word(op | rn << 5 | rd);
    }

    /// `uxtb`, `uxth`, or `mov wd, wn`: the low `width` bytes of `xn`, with
    /// zeros above, into `xd`.
    fn zero_extend(&mut self, rd: u32, rn: u32, width: u32) {
        match width {
            1 => self.word(0x5300_1c00 | rn << 5 | rd),
            2 => self.word(0x5300_3c00 | rn << 5 | rd),
            4 => self.word(0x2a00_03e0 | rn << 16 | rd),
            _ => unreachable!("an integer of {width} bytes"),
        }
    }

    /// `lsr xd, xd, #count`.
    fn shift_right(&mut self, rd: u32, count: u32) {
        self.word(0xd340_fc00 | count << 16 | rd << 5 | rd);
    }

    /// `asr xd, xd, #count`.
    fn shift_right_signed(&mut self, rd: u32, count: u32) {
        self.word(0x9340_fc00 | count << 16 | rd << 5 | rd);
    }
}

/// What a system gives the code of an arm64 target: how it writes, how it
/// ends the program, and how it asks for memory. These are the three calls
/// into the system the scalar form has, and the only code the targets
/// differ in.
pub trait System {
    /// How many bytes at the bottom of every frame a call to the system may
    /// use. A multiple of 16.
    const RESERVED: u32;
    /// Writes every byte of the text whose address is in x1 and whose length
    /// is in x2, and branches to `fail` if the system refuses it. It may
    /// change x0 to x17, and leaves x19 to x28 as they were, which it may do
    /// by holding them in the bytes at the bottom of the frame.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u16);
    /// Asks the system for memory for the number of bytes in x1, which is
    /// not 0, leaves its address in x0, and branches to `fail` if the system
    /// refuses. It may change what `write` may.
    fn alloc(asm: &mut Asm, fail: usize);
}

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack, a multiple of 16 bytes. From
/// the bottom, it holds the bytes the system may use, the places of the
/// calls it makes, the values that have no register, the registers it must
/// put back, and the address it returns to. A function finds its parameters
/// in its caller's frame, and puts what it gives back just after them.
pub fn code<S: System>(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
    let mut asm = Asm::default();
    let texts: Vec<usize> = program.data.iter().map(|_| asm.label()).collect();
    let functions: Vec<usize> = program.functions.iter().map(|_| asm.label()).collect();
    let fail = asm.label();
    if S::RESERVED > 0 {
        asm.reserve_stack(S::RESERVED);
    }
    asm.call_label(functions[program.main]);
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
    Register(u32),
    /// `[sp + offset]`.
    Frame(u32),
}

/// A function being written, and where each value is.
struct Writer<'a, S> {
    program: &'a Program,
    f: &'a Function,
    texts: &'a [usize],
    functions: &'a [usize],
    fail: usize,
    /// How far below its caller's frame the function's frame reaches.
    frame: u32,
    /// Where each value is.
    at: Vec<Loc>,
    /// Where each parameter and result of the function is, in the call
    /// area, from the bottom of the frame.
    passed: Vec<u32>,
    /// The registers a call does not change that the function uses, and
    /// where it keeps what they held.
    saved: Vec<(u32, u32)>,
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
    let below = S::RESERVED + frame::outgoing(program, f);
    let saved_from = round_up(below + allocation.frame, 8);
    // The address the function returns to is kept above its values.
    let total = round_up(saved_from + 8 * allocation.saved.len() as u32, 16) + 16;
    let (offsets, _) = call_area(f);
    let passed: Vec<u32> = offsets.iter().map(|o| total + S::RESERVED + o).collect();
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
        passed,
        saved,
        labels,
        system: std::marker::PhantomData,
    };
    asm.reserve_stack(total);
    asm.store(Place::Frame(total - 16), X30, 8);
    for (r, at) in &w.saved {
        asm.store(Place::Frame(*at), *r, 8);
    }
    // The function's own parameters are in the caller's frame.
    for (p, from) in f.blocks[0].params.iter().zip(&w.passed) {
        w.copy(asm, w.at[p.0], Loc::Frame(*from), w.width(*p));
    }
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

    /// Copies `width` bytes from one place to another, through x16 when both
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
                asm.load(X16, width, false, Place::Frame(from));
                asm.store(Place::Frame(to), X16, width);
            }
        }
    }

    /// Does the moves as if all were copied at once. A place that another
    /// move is still to read is held in x17 first, when they go in a circle.
    fn run(&self, asm: &mut Asm, moves: &[Move<Loc>]) {
        for step in moves::sequence(moves) {
            match step {
                Step::Copy { to, from, width } => self.copy(asm, to, from, width),
                Step::Save { from, width } => self.copy(asm, Loc::Register(X17), from, width),
                Step::Restore { to, width } => self.copy(asm, to, Loc::Register(X17), width),
            }
        }
    }

    /// A register that holds the value, with zeros above it: its own, or the
    /// one `tmp` says, with the value read into it.
    fn read(&self, asm: &mut Asm, tmp: u32, v: Value) -> u32 {
        match self.at[v.0] {
            Loc::Register(r) => r,
            Loc::Frame(at) => {
                asm.load(tmp, self.width(v), false, Place::Frame(at));
                tmp
            }
        }
    }

    /// Reads the value into the register, with zeros above it.
    fn read_into(&self, asm: &mut Asm, rd: u32, v: Value) {
        self.copy(asm, Loc::Register(rd), self.at[v.0], self.width(v));
    }

    /// Reads the value into the register, extended as a signed number.
    fn read_signed_into(&self, asm: &mut Asm, rd: u32, v: Value) {
        let width = self.width(v);
        match self.at[v.0] {
            Loc::Register(r) if width == 8 => {
                if r != rd {
                    asm.mov(rd, r);
                }
            }
            Loc::Register(r) => asm.sign_extend(rd, r, width),
            Loc::Frame(at) => asm.load(rd, width, true, Place::Frame(at)),
        }
    }

    /// A register that holds the value extended as a signed number: its own
    /// when it takes 64 bits, and otherwise `tmp`.
    fn read_signed(&self, asm: &mut Asm, tmp: u32, v: Value) -> u32 {
        match self.at[v.0] {
            Loc::Register(r) if self.width(v) == 8 => r,
            _ => {
                self.read_signed_into(asm, tmp, v);
                tmp
            }
        }
    }

    /// The register to work out a result in: its own, or x16.
    fn target(&self, v: Value) -> u32 {
        match self.at[v.0] {
            Loc::Register(r) => r,
            Loc::Frame(_) => X16,
        }
    }

    /// Puts the result, which is in the low bytes of `r`, in its home. The
    /// bits above it are zeros when `extended`, and anything when not.
    fn finish(&self, asm: &mut Asm, v: Value, r: u32, extended: bool) {
        let width = self.width(v);
        match self.at[v.0] {
            Loc::Frame(at) => asm.store(Place::Frame(at), r, width),
            Loc::Register(h) if extended || width == 8 => {
                if h != r {
                    asm.mov(h, r);
                }
            }
            Loc::Register(h) => asm.zero_extend(h, r, width),
        }
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], op: &Op) {
        match op {
            Op::Constant(c) => {
                let result = results[0];
                let t = self.target(result);
                match c {
                    Constant::Int(n) => {
                        let n = match self.width(result) {
                            8 => *n,
                            w => n & ((1 << (8 * w)) - 1),
                        };
                        asm.mov64(t, n);
                    }
                    Constant::Null => asm.mov_w(t, 0),
                    Constant::Data(d) => asm.address(t, self.texts[*d]),
                }
                self.finish(asm, result, t, true);
            }
            Op::Binary { op, a, b } => self.binary(asm, results[0], *op, *a, *b),
            Op::Compare { op, a, b } => {
                let (ra, rb) = if matches!(op, Compare::LessS) {
                    (
                        self.read_signed(asm, X16, *a),
                        self.read_signed(asm, X17, *b),
                    )
                } else {
                    (self.read(asm, X16, *a), self.read(asm, X17, *b))
                };
                asm.cmp(ra, rb);
                let t = self.target(results[0]);
                asm.cset(
                    t,
                    match op {
                        Compare::Equal => NE,
                        Compare::LessU => HS,
                        Compare::LessS => GE,
                    },
                );
                self.finish(asm, results[0], t, true);
            }
            Op::Widen { signed, value } => {
                let t = self.target(results[0]);
                if *signed {
                    self.read_signed_into(asm, t, *value);
                } else {
                    self.read_into(asm, t, *value);
                }
                self.finish(asm, results[0], t, !*signed);
            }
            Op::Narrow(value) => {
                let t = self.target(results[0]);
                self.read_into(asm, t, *value);
                self.finish(asm, results[0], t, false);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } => {
                let on = self.read(asm, X16, *on);
                asm.cmp_imm(on, 0);
                let then = self.read(asm, X16, *then);
                let otherwise = self.read(asm, X17, *otherwise);
                let t = self.target(results[0]);
                asm.csel(t, then, otherwise, NE);
                self.finish(asm, results[0], t, true);
            }
            Op::Load { ty, address } => {
                let address = self.read(asm, X16, *address);
                let t = self.target(results[0]);
                asm.load(t, size(*ty), false, Place::At(address));
                self.finish(asm, results[0], t, true);
            }
            Op::Store { ty, address, value } => {
                let address = self.read(asm, X16, *address);
                let value = self.read(asm, X17, *value);
                asm.store(Place::At(address), value, size(*ty));
            }
            Op::Call { function, args } => {
                let callee = &self.program.functions[*function];
                let (offsets, _) = call_area(callee);
                for (arg, offset) in args.iter().zip(&offsets) {
                    let to = Loc::Frame(S::RESERVED + offset);
                    self.copy(asm, to, self.at[arg.0], self.width(*arg));
                }
                asm.call_label(self.functions[*function]);
                for (result, offset) in results.iter().zip(&offsets[args.len()..]) {
                    let from = Loc::Frame(S::RESERVED + offset);
                    self.copy(asm, self.at[result.0], from, self.width(*result));
                }
            }
            Op::System {
                call: Call::Write(stream),
                args,
            } => {
                let moves: Vec<Move<Loc>> = (args.iter().zip([X1, X2]))
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
                self.read_into(asm, X1, args[0]);
                S::alloc(asm, self.fail);
                self.finish(asm, results[0], X0, true);
            }
        }
    }

    fn binary(&self, asm: &mut Asm, result: Value, op: Binary, a: Value, b: Value) {
        let bits = self.width(result) * 8;
        let wide = bits == 64;
        let signed = matches!(op, Binary::MulHighS) && !wide;
        let (a, b) = if signed {
            (self.read_signed(asm, X16, a), self.read_signed(asm, X17, b))
        } else {
            (self.read(asm, X16, a), self.read(asm, X17, b))
        };
        let t = self.target(result);
        let mut extended = false;
        match op {
            Binary::Add => asm.add(t, a, b),
            Binary::Sub => asm.three(0xcb00_0000, t, a, b),
            Binary::Mul => asm.three(0x9b00_7c00, t, a, b),
            Binary::And => {
                asm.three(0x8a00_0000, t, a, b);
                extended = true;
            }
            Binary::Xor => {
                asm.three(0xca00_0000, t, a, b);
                extended = true;
            }
            Binary::MulHighU if wide => {
                asm.three(0x9bc0_7c00, t, a, b);
                extended = true;
            }
            Binary::MulHighS if wide => asm.three(0x9b40_7c00, t, a, b),
            // The product of two integers of fewer than 64 bits fits in
            // 64, and its high half is what is left after the shift.
            Binary::MulHighU => {
                asm.three(0x9b00_7c00, t, a, b);
                asm.shift_right(t, bits);
                extended = true;
            }
            Binary::MulHighS => {
                asm.three(0x9b00_7c00, t, a, b);
                asm.shift_right_signed(t, bits);
            }
        }
        self.finish(asm, result, t, extended);
    }

    /// Makes the jump: gives the block its parameters as if all were copied
    /// at once, and goes there, unless it is the next block.
    fn jump(&self, asm: &mut Asm, jump: &Jump, next: usize) {
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
            asm.branch(self.labels[jump.block]);
        }
    }

    fn end(&self, asm: &mut Asm, block: usize) {
        let next = block + 1;
        match &self.f.blocks[block].end {
            End::Jump(jump) => self.jump(asm, jump, next),
            End::Switch { on, jumps } => {
                // Each jump but the last is chosen by comparing the number
                // with its place, and the last is made when none was.
                let on = self.read(asm, X16, *on);
                for (i, jump) in jumps.iter().enumerate() {
                    let skip = asm.label();
                    if i + 1 < jumps.len() {
                        asm.cmp_imm(on, i as u32);
                        asm.cond(NE, skip);
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
                let results = &self.passed[self.passed.len() - values.len()..];
                for (value, to) in values.iter().zip(results) {
                    self.copy(asm, Loc::Frame(*to), self.at[value.0], self.width(*value));
                }
                for (r, at) in &self.saved {
                    asm.load(*r, 8, false, Place::Frame(*at));
                }
                asm.load(X30, 8, false, Place::Frame(self.frame - 16));
                asm.release_stack(self.frame);
                asm.ret();
            }
        }
    }
}
