//! The arm64 instructions the arm64 targets share, with labels resolved
//! once the code and its data are laid out, and how a program in the scalar
//! form becomes them. It reads the scalar form and nothing else.

use super::frame::{self, call_area, round_up, size};
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
/// Free for any use between two instructions.
const X17: u32 = 17;
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
                    self.add_immediate(0x9100_0000, X17, SP, offset);
                    (X17, 0)
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
    /// Writes every byte of the text whose address is in x19 and whose
    /// length is in x20, and branches to `fail` if the system refuses it.
    /// It may change any register but sp, x29, and x18.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u16);
    /// Asks the system for memory for the number of bytes in x1, which is
    /// not 0, leaves its address in x0, and branches to `fail` if the system
    /// refuses. It may change any register but sp, x29, and x18.
    fn alloc(asm: &mut Asm, fail: usize);
}

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack, a multiple of 16 bytes. From
/// the bottom, it holds the bytes the system may use, the places of the
/// calls it makes, its values, and the address it returns to. A function
/// finds its parameters in its caller's frame, and puts what it gives back
/// just after them.
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

/// A function being written, and where its frame keeps each value.
struct Writer<'a, S> {
    program: &'a Program,
    f: &'a Function,
    texts: &'a [usize],
    functions: &'a [usize],
    fail: usize,
    /// How far below its caller's frame the function's frame reaches.
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
    let held = frame::frame(program, f);
    let below = S::RESERVED + held.outgoing;
    // The address the function returns to is kept above its values.
    let total = round_up(below + held.held, 16) + 16;
    let (offsets, _) = call_area(f);
    let passed: Vec<u32> = offsets.iter().map(|o| total + S::RESERVED + o).collect();
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
    asm.store(Place::Frame(total - 16), X30, 8);
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
    fn get(&self, asm: &mut Asm, r: u32, v: Value) {
        asm.load(r, self.width(v), false, self.place(v));
    }

    /// Reads the value into the register, extended as a signed number.
    fn get_signed(&self, asm: &mut Asm, r: u32, v: Value) {
        asm.load(r, self.width(v), true, self.place(v));
    }

    /// Writes the low bytes of the register to the value, as many as its
    /// type takes.
    fn put(&self, asm: &mut Asm, v: Value, r: u32) {
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
                        asm.mov64(X0, n);
                    }
                    Constant::Null => asm.mov_w(X0, 0),
                    Constant::Data(t) => asm.address(X0, self.texts[*t]),
                }
                self.put(asm, result, X0);
            }
            Op::Binary { op, a, b } => self.binary(asm, results[0], *op, *a, *b),
            Op::Compare { op, a, b } => {
                if matches!(op, Compare::LessS) {
                    self.get_signed(asm, X0, *a);
                    self.get_signed(asm, X1, *b);
                } else {
                    self.get(asm, X0, *a);
                    self.get(asm, X1, *b);
                }
                asm.cmp(X0, X1);
                asm.cset(
                    X0,
                    match op {
                        Compare::Equal => NE,
                        Compare::LessU => HS,
                        Compare::LessS => GE,
                    },
                );
                self.put(asm, results[0], X0);
            }
            Op::Widen { signed, value } => {
                if *signed {
                    self.get_signed(asm, X0, *value);
                } else {
                    self.get(asm, X0, *value);
                }
                self.put(asm, results[0], X0);
            }
            Op::Narrow(value) => {
                self.get(asm, X0, *value);
                self.put(asm, results[0], X0);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } => {
                self.get(asm, X0, *otherwise);
                self.get(asm, X1, *then);
                self.get(asm, X2, *on);
                asm.cmp_imm(X2, 0);
                asm.csel(X0, X1, X0, NE);
                self.put(asm, results[0], X0);
            }
            Op::Load { ty, address } => {
                self.get(asm, X0, *address);
                asm.load(X1, size(*ty), false, Place::At(X0));
                self.put(asm, results[0], X1);
            }
            Op::Store { ty, address, value } => {
                self.get(asm, X0, *address);
                self.get(asm, X1, *value);
                asm.store(Place::At(X0), X1, size(*ty));
            }
            Op::Call { function, args } => {
                let callee = &self.program.functions[*function];
                let (offsets, _) = call_area(callee);
                for (arg, offset) in args.iter().zip(&offsets) {
                    self.get(asm, X0, *arg);
                    asm.store(Place::Frame(S::RESERVED + offset), X0, self.width(*arg));
                }
                asm.call_label(self.functions[*function]);
                for (result, offset) in results.iter().zip(&offsets[args.len()..]) {
                    let from = Place::Frame(S::RESERVED + offset);
                    asm.load(X0, self.width(*result), false, from);
                    self.put(asm, *result, X0);
                }
            }
            Op::System {
                call: Call::Write(stream),
                args,
            } => {
                self.get(asm, X19, args[0]);
                self.get(asm, X20, args[1]);
                S::write(asm, *stream, self.fail);
            }
            Op::System {
                call: Call::Allocate,
                args,
            } => {
                self.get(asm, X1, args[0]);
                S::alloc(asm, self.fail);
                self.put(asm, results[0], X0);
            }
        }
    }

    fn binary(&self, asm: &mut Asm, result: Value, op: Binary, a: Value, b: Value) {
        let bits = self.width(result) * 8;
        let wide = bits == 64;
        match op {
            Binary::MulHighS => {
                self.get_signed(asm, X0, a);
                self.get_signed(asm, X1, b);
            }
            _ => {
                self.get(asm, X0, a);
                self.get(asm, X1, b);
            }
        }
        match op {
            Binary::Add => asm.add(X0, X0, X1),
            Binary::Sub => asm.three(0xcb00_0000, X0, X0, X1),
            Binary::Mul => asm.three(0x9b00_7c00, X0, X0, X1),
            Binary::And => asm.three(0x8a00_0000, X0, X0, X1),
            Binary::Xor => asm.three(0xca00_0000, X0, X0, X1),
            Binary::MulHighU if wide => asm.three(0x9bc0_7c00, X0, X0, X1),
            Binary::MulHighS if wide => asm.three(0x9b40_7c00, X0, X0, X1),
            // The product of two integers of fewer than 64 bits fits in
            // x0, and its high half is what is left after the shift.
            Binary::MulHighU => {
                asm.three(0x9b00_7c00, X0, X0, X1);
                asm.shift_right(X0, bits);
            }
            Binary::MulHighS => {
                asm.three(0x9b00_7c00, X0, X0, X1);
                asm.shift_right_signed(X0, bits);
            }
        }
        self.put(asm, result, X0);
    }

    /// Where the values of a jump go, as if all were copied at once: a value
    /// that another is to be copied from is saved, in x2, before it is
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
                        asm.load(X0, width, false, Place::Frame(from));
                        asm.store(Place::Frame(to), X0, width);
                    } else {
                        asm.store(Place::Frame(to), X2, width);
                    }
                }
                // Every place is still to be read, so they go round in a
                // circle. Saving one breaks it.
                None => {
                    let (place, _, width) = moves[0];
                    asm.load(X2, width, false, Place::Frame(place));
                    for m in &mut moves {
                        if m.1 == From::Frame(place) {
                            m.1 = From::Saved;
                        }
                    }
                }
            }
        }
        asm.branch(self.labels[jump.block]);
    }

    fn end(&self, asm: &mut Asm, end: &End) {
        match end {
            End::Jump(jump) => self.jump(asm, jump),
            End::Switch { on, jumps } => {
                // Each jump but the last is chosen by comparing the number
                // with its place, and the last is made when none was.
                self.get(asm, X0, *on);
                for (i, jump) in jumps.iter().enumerate() {
                    let next = asm.label();
                    if i + 1 < jumps.len() {
                        asm.cmp_imm(X0, i as u32);
                        asm.cond(NE, next);
                    }
                    self.jump(asm, jump);
                    asm.bind(next);
                }
            }
            End::Return(values) => {
                let results = &self.passed[self.passed.len() - values.len()..];
                for (value, to) in values.iter().zip(results) {
                    self.get(asm, X0, *value);
                    asm.store(Place::Frame(*to), X0, self.width(*value));
                }
                asm.load(X30, 8, false, Place::Frame(self.frame - 16));
                asm.release_stack(self.frame);
                asm.ret();
            }
        }
    }
}
