//! The arm64 instructions the arm64 targets share, with labels resolved
//! once the code and its data are laid out, and how a program in the scalar
//! form becomes them. It reads the scalar form and nothing else.

use super::alloc::{self, Home, Registers};
use super::frame::{self, PAGE, call_area, round_up, size};
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

/// A call passes its arguments in all fifteen of the first, `x0` to `x14`,
/// and the function gives its results in the same fifteen, each in the order
/// it has. What does not fit goes in the caller's frame.
const REGISTERS: Registers = Registers {
    changed: 15,
    preserved: 10,
    passed: 15,
};
/// The link register, which a call puts its return address in.
const X30: u32 = 30;
/// The stack pointer, where an instruction takes it in place of a register.
const SP: u32 = 31;
/// The register number that reads as 0 and, as what a store writes, is 0.
const XZR: u32 = 31;
pub const LE: u32 = 0xd;
pub const EQ: u32 = 0x0;
pub const NE: u32 = 0x1;
pub const LT: u32 = 0xb;
/// Unsigned lower.
pub const LO: u32 = 0x3;
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
    /// A 32-bit signed number: how far the label is from the start of a
    /// table, which is another label.
    From {
        at: usize,
        label: usize,
        table: usize,
    },
}

/// Machine code with labels, each resolved once the code and its data are
/// laid out.
#[derive(Default)]
pub struct Asm {
    code: Vec<u8>,
    labels: Vec<Option<usize>>,
    fixups: Vec<Fixup>,
    /// The tables, each a label and the labels whose distances from it are
    /// its entries, which are placed after the code, with the data.
    tables: Vec<(usize, Vec<usize>)>,
}

/// Where the code, the labels, the places to fill, and the tables are, so
/// that what is written after can be taken back.
type Mark = (usize, usize, usize, usize);

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

    /// Whether a conditional branch written now reaches the label, when the
    /// label is already placed, which a conditional branch back to a place far
    /// behind it, such as the code that ends the program on a failure, may
    /// not. It is a branch of 19 bits of words, 1 MB either way.
    fn near(&self, label: usize) -> bool {
        self.labels[label].is_none_or(|place| {
            (-(1 << 18)..1 << 18).contains(&((place as i64 - self.code.len() as i64) / 4))
        })
    }

    /// A conditional branch that is `word` with the label's offset in it, over
    /// an unconditional branch to the label when the label is out of reach,
    /// in which case `inverse` is the word that branches when `word` does not.
    fn conditional(&mut self, word: u32, inverse: u32, label: usize) {
        if self.near(label) {
            self.fixups.push(Fixup::Conditional {
                at: self.code.len(),
                label,
            });
            self.word(word);
        } else {
            let skip = self.label();
            self.fixups.push(Fixup::Conditional {
                at: self.code.len(),
                label: skip,
            });
            self.word(inverse);
            self.branch(label);
            self.bind(skip);
        }
    }

    pub fn cond(&mut self, cond: u32, label: usize) {
        self.conditional(0x5400_0000 | cond, 0x5400_0000 | (cond ^ 1), label);
    }

    pub fn cbz(&mut self, reg: u32, label: usize) {
        self.conditional(0xb400_0000 | reg, 0xb500_0000 | reg, label);
    }

    /// `cbz wt, label`, which looks at the low 32 bits only.
    pub fn cbz_w(&mut self, reg: u32, label: usize) {
        self.conditional(0x3400_0000 | reg, 0x3500_0000 | reg, label);
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

    /// Makes room on the stack for a frame of more than a page, as a system
    /// that gives a program its stack one page at a time needs: it touches
    /// each page below the stack pointer in turn, from the nearest, through
    /// x16, so that none is passed over, then moves the stack pointer and
    /// touches the lowest page.
    pub fn reserve_stack_by_page(&mut self, bytes: u32) {
        // add x16, sp, #0
        self.word(0x9100_03e0 | X16);
        for _ in 0..bytes / PAGE {
            // sub x16, x16, #1, lsl #12; ldr wzr, [x16]
            self.word(0xd140_0400 | X16 << 5 | X16);
            self.word(0xb940_001f | X16 << 5);
        }
        self.reserve_stack(bytes);
        // ldr wzr, [sp]
        self.word(0xb940_001f | SP << 5);
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

    /// Puts the number in the register, whole, in the fewest instructions.
    pub fn mov_imm(&mut self, rd: u32, value: u64) {
        match u16::try_from(value) {
            Ok(n) => self.mov_w(rd, n),
            Err(_) => self.mov64(rd, value),
        }
    }

    /// `add xd, xn, #value`, or `sub` when `sub`, for a value below 2^12.
    pub fn add_or_sub_imm(&mut self, sub: bool, rd: u32, rn: u32, value: u32) {
        assert!(value < 1 << 12);
        self.word(if sub { 0xd100_0000 } else { 0x9100_0000 } | value << 10 | rn << 5 | rd);
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

    /// Goes to the label that entry `index` of the table at `table` says,
    /// as `table_entries` places it, which is 4 bytes for each: x15 and x17
    /// are changed.
    pub fn jump_through(&mut self, index: u32, table: usize) {
        self.address(X17, table);
        // ldrsw x15, [x17, index, lsl #2]
        self.word(0xb8a0_7800 | index << 16 | X17 << 5 | X15);
        self.add(X17, X17, X15);
        // br x17
        self.word(0xd61f_0000 | X17 << 5);
    }

    /// The table at `table`, which is placed after the code: for each label,
    /// how far it is from the start of the table.
    pub fn table_entries(&mut self, table: usize, labels: Vec<usize>) {
        self.tables.push((table, labels));
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

    /// The size of the code so far, and of the tables that are placed after
    /// it.
    pub fn len(&self) -> usize {
        self.code.len()
            + self
                .tables
                .iter()
                .map(|(_, labels)| 4 * labels.len())
                .sum::<usize>()
    }

    /// Where the code, the labels, the places to fill, and the tables are
    /// now.
    fn mark(&self) -> Mark {
        (
            self.code.len(),
            self.labels.len(),
            self.fixups.len(),
            self.tables.len(),
        )
    }

    /// Takes back what was written after `mark`.
    fn reset(&mut self, mark: Mark) {
        self.code.truncate(mark.0);
        self.labels.truncate(mark.1);
        self.fixups.truncate(mark.2);
        self.tables.truncate(mark.3);
    }

    /// Whether every conditional branch written after `mark` reaches the
    /// label it goes to, which every label of the function is bound for.
    fn reaches(&self, mark: Mark) -> bool {
        self.fixups[mark.2..].iter().all(|f| match f {
            Fixup::Conditional { at, label } => {
                (-(1 << 18)..1 << 18).contains(&(self.offset(*at, *label) / 4))
            }
            _ => true,
        })
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
        for (table, labels) in std::mem::take(&mut self.tables) {
            self.bind(table);
            for label in labels {
                let at = self.code.len();
                self.fixups.push(Fixup::From { at, label, table });
                self.word(0);
            }
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
                    assert!(
                        (-(1 << 18)..1 << 18).contains(&words),
                        "a conditional branch that reaches 1 MB or more"
                    );
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
                Fixup::From { at, label, table } => {
                    let distance = self.place(label) as i64 - self.place(table) as i64;
                    let distance = i32::try_from(distance).expect("in reach");
                    self.code[at..at + 4].copy_from_slice(&distance.to_le_bytes());
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
    /// Whether the system gives a program its stack one page at a time, so
    /// that a frame of more than `PAGE` bytes is reserved a page at a time.
    const STACK_BY_PAGE: bool = false;
    /// How many bytes at the bottom of every frame a call to the system may
    /// use. A multiple of 16.
    const RESERVED: u32;
    /// Writes every byte of the text whose address is in x1 and whose length
    /// is in x2, and branches to `fail` if the system refuses it. It may
    /// change x0 to x17, and leaves x19 to x28 as they were, which it may do
    /// by holding them in the bytes at the bottom of the frame.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status in x0.
    fn end(asm: &mut Asm);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u16) {
        asm.mov_w(X0, status);
        Self::end(asm);
    }
    /// Asks the system for memory for the number of bytes in x1, which is
    /// not 0, leaves its address in x0, and branches to `fail` if the system
    /// refuses. It may change what `write` may.
    fn alloc(asm: &mut Asm, fail: usize);
    /// Gives `main` what the system gives a program that takes its words, in
    /// x0, x1, and x2: a number, the address of an array of addresses, and
    /// the address of a command line, as `scalar::ARGUMENTS` says. It runs
    /// first, before the stack is touched, so a system that gives the words
    /// on the stack reads them here, and may change only those registers.
    fn arguments_on_entry(asm: &mut Asm);
    /// Does what `arguments_on_entry` does for a system that gives the words
    /// by a call. The stack is ready for a call, and x0, x1, and x2 are as
    /// `arguments_on_entry` left them.
    fn arguments_by_call(asm: &mut Asm);
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
    let takes = program.takes_arguments();
    if takes {
        S::arguments_on_entry(&mut asm);
    }
    if S::RESERVED > 0 {
        asm.reserve_stack(S::RESERVED);
    }
    if takes {
        S::arguments_by_call(&mut asm);
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
    /// A number that a constant gives, which is in no place: each
    /// instruction that reads it holds it.
    Imm(u64),
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
    /// How far below its caller's frame the function's frame reaches.
    frame: u32,
    /// Where each value is.
    at: Vec<Loc>,
    /// Where each parameter of the function arrives, and where each result
    /// is left.
    arrive: Vec<Loc>,
    leave: Vec<Loc>,
    /// The registers a call does not change that the function uses, and
    /// where it keeps what they held.
    saved: Vec<(u32, u32)>,
    labels: Vec<usize>,
    /// Which blocks end with a choice between two jumps on a comparison that
    /// is the last instruction of the block and that nothing else reads. That
    /// comparison is not made into a value: the branch tests it.
    branches_on_compare: Vec<bool>,
    /// Whether a conditional branch goes over an unconditional one to reach
    /// a block, as a function of more than 1 MB needs, in place of going
    /// straight to it.
    far: bool,
    /// The number each value that a constant integer gives is.
    constants: Vec<Option<u64>>,
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
    let f = &crate::scalar::rematerialized(f);
    let constants = super::branch::constants(f);
    let held = held(f, &constants);
    let allocation = alloc::allocate(f, REGISTERS, &held);
    let below = S::RESERVED + frame::outgoing(program, f, REGISTERS.passed);
    let saved_from = round_up(below + allocation.frame, 8);
    // The address the function returns to is kept above its values.
    let total = round_up(saved_from + 8 * allocation.saved.len() as u32, 16) + 16;
    let area = call_area(f, REGISTERS.passed);
    let in_caller =
        |places: &[u32]| -> Vec<Loc> { passing(places.iter().map(|o| total + S::RESERVED + o)) };
    let arrive = in_caller(&area.params);
    let leave = in_caller(&area.results);
    let at: Vec<Loc> = (allocation.homes.iter().enumerate())
        .map(|(v, home)| match home {
            Home::Register(r) => Loc::Register(HOMES[*r]),
            Home::Frame(place) => Loc::Frame(below + place),
            Home::Held => Loc::Imm(constants[v].expect("a value held is a constant")),
        })
        .collect();
    let saved: Vec<(u32, u32)> = (allocation.saved.iter().enumerate())
        .map(|(i, r)| (HOMES[*r], saved_from + 8 * i as u32))
        .collect();
    let branches_on_compare = super::branch::on_compare(f);
    // A choice branches straight to the block it goes to when that block is
    // in reach of a conditional branch, which is 1 MB, and otherwise the
    // function is written again with the branch over an unconditional one.
    let mark = asm.mark();
    for far in [false, true] {
        let labels = f.blocks.iter().map(|_| asm.label()).collect();
        let w = Writer::<S> {
            program,
            f,
            texts,
            functions,
            fail,
            frame: total,
            at: at.clone(),
            arrive: arrive.clone(),
            leave: leave.clone(),
            saved: saved.clone(),
            labels,
            branches_on_compare: branches_on_compare.clone(),
            far,
            constants: constants.clone(),
            system: std::marker::PhantomData,
        };
        if S::STACK_BY_PAGE && total > PAGE {
            asm.reserve_stack_by_page(total);
        } else {
            asm.reserve_stack(total);
        }
        asm.store(Place::Frame(total - 16), X30, 8);
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
            let made = block.instructions.len() - usize::from(w.branches_on_compare[b]);
            for instruction in &block.instructions[..made] {
                w.instruction(asm, &instruction.results, &instruction.op);
            }
            w.end(asm, b);
        }
        if far || asm.reaches(mark) {
            break;
        }
        asm.reset(mark);
    }
}

/// The number an `add` or a `sub` holds in itself in place of its second
/// operand, when it is a constant below 4096.
fn immediate(constants: &[Option<u64>], op: &Op) -> Option<u32> {
    match op {
        Op::Binary {
            op: Binary::Add | Binary::Sub,
            b,
            ..
        } => constants[b.0]
            .and_then(|n| u32::try_from(n).ok())
            .filter(|n| *n < 1 << 12),
        _ => None,
    }
}

/// Which values that a constant gives need no register or place in the frame,
/// as each instruction that reads one holds it: as the second operand of an
/// `add` or a `sub`, as the value stored or chosen, or as what a jump, a
/// call, or a return gives, which is moved there with the number. A value
/// that anything else reads has a home as any other.
fn held(f: &Function, constants: &[Option<u64>]) -> Vec<bool> {
    let mut needs_a_home = vec![false; f.types.len()];
    for block in &f.blocks {
        for i in &block.instructions {
            let embedded = immediate(constants, &i.op).is_some();
            for (k, v) in i.op.operands().into_iter().enumerate() {
                needs_a_home[v.0] |= !match &i.op {
                    Op::Binary { .. } => embedded && k == 1,
                    Op::Store { .. } => k == 1,
                    Op::Choose { .. } => k > 0,
                    Op::Call { .. } | Op::System { .. } => true,
                    _ => false,
                };
            }
        }
        if let End::Switch { on, .. } = &block.end {
            needs_a_home[on.0] = true;
        }
    }
    (0..needs_a_home.len())
        .map(|v| constants[v].is_some() && !needs_a_home[v])
        .collect()
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
            (Loc::Register(to), Loc::Imm(n)) => asm.mov_imm(to, n),
            (Loc::Frame(to), Loc::Imm(n)) => self.store_imm(asm, Place::Frame(to), n, width, X16),
            (Loc::Imm(_), _) => unreachable!("a number is not a place to copy to"),
        }
    }

    /// Writes the number, `width` bytes of it, to the place: the zero register
    /// holds 0, and any other number is made in `tmp` first, which is not a
    /// register the place is based on.
    fn store_imm(&self, asm: &mut Asm, to: Place, n: u64, width: u32, tmp: u32) {
        if n == 0 {
            asm.store(to, XZR, width);
        } else {
            asm.mov_imm(tmp, n);
            asm.store(to, tmp, width);
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
            Loc::Imm(n) => {
                asm.mov_imm(tmp, n);
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
            Loc::Imm(n) => {
                let shift = 64 - 8 * width;
                asm.mov_imm(rd, (((n << shift) as i64) >> shift) as u64);
            }
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
            Loc::Frame(_) | Loc::Imm(_) => X16,
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
            Loc::Imm(_) => unreachable!("an instruction gives no result that is a number held"),
        }
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], op: &Op) {
        match op {
            Op::Constant(_) if matches!(self.at[results[0].0], Loc::Imm(_)) => {}
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
                let cond = self.compare(asm, op, *a, *b);
                let t = self.target(results[0]);
                asm.cset(t, cond ^ 1);
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
                match self.at[value.0] {
                    Loc::Imm(n) => self.store_imm(asm, Place::At(address), n, size(*ty), X17),
                    _ => {
                        let value = self.read(asm, X17, *value);
                        asm.store(Place::At(address), value, size(*ty));
                    }
                }
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
                asm.call_label(self.functions[*function]);
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
                call: Call::Exit,
                args,
            } => {
                self.read_into(asm, X0, args[0]);
                S::end(asm);
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
        if let Some(n) = immediate(&self.constants, &Op::Binary { op, a, b }) {
            let a = self.read(asm, X16, a);
            let t = self.target(result);
            asm.add_or_sub_imm(matches!(op, Binary::Sub), t, a, n);
            self.finish(asm, result, t, false);
            return;
        }
        let bits = self.width(result) * 8;
        let wide = bits == 64;
        let signed = matches!(op, Binary::MulHighS) && !wide;
        // A shift right that keeps the sign reads the value as signed, and
        // the count as it is.
        let a = if signed || matches!(op, Binary::ShiftRightS) {
            self.read_signed(asm, X16, a)
        } else {
            self.read(asm, X16, a)
        };
        let b = if signed {
            self.read_signed(asm, X17, b)
        } else {
            self.read(asm, X17, b)
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
            Binary::Or => {
                asm.three(0xaa00_0000, t, a, b);
                extended = true;
            }
            // `lslv`, `lsrv`, and `asrv`, which take the count modulo 64.
            Binary::ShiftLeft => asm.three(0x9ac0_2000, t, a, b),
            Binary::ShiftRightU => {
                asm.three(0x9ac0_2400, t, a, b);
                extended = true;
            }
            Binary::ShiftRightS => asm.three(0x9ac0_2800, t, a, b),
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

    /// Compares the two values, and gives the condition code that holds when
    /// the first is below, or the same as, the second, as the comparison says.
    fn compare(&self, asm: &mut Asm, op: &Compare, a: Value, b: Value) -> u32 {
        let (ra, rb) = if matches!(op, Compare::LessS) {
            (self.read_signed(asm, X16, a), self.read_signed(asm, X17, b))
        } else {
            (self.read(asm, X16, a), self.read(asm, X17, b))
        };
        asm.cmp(ra, rb);
        match op {
            Compare::Equal => EQ,
            Compare::LessU => LO,
            Compare::LessS => LT,
        }
    }

    /// The moves that give the block a jump goes to its parameters.
    fn jump_moves(&self, jump: &Jump) -> Vec<Move<Loc>> {
        let target = &self.f.blocks[jump.block];
        (jump.args.iter().zip(&target.params))
            .map(|(from, to)| Move {
                to: self.at[to.0],
                from: self.at[from.0],
                width: self.width(*to),
            })
            .collect()
    }

    /// Makes the jump: gives the block its parameters as if all were copied
    /// at once, and goes there, unless it is the next block.
    fn jump(&self, asm: &mut Asm, jump: &Jump, next: usize) {
        self.run(asm, &self.jump_moves(jump));
        if jump.block != next {
            asm.branch(self.labels[jump.block]);
        }
    }

    /// Goes to the label when the condition holds, over an unconditional
    /// branch when the label may be out of reach.
    fn go_if(&self, asm: &mut Asm, cond: u32, label: usize) {
        if self.far {
            let skip = asm.label();
            asm.cond(cond ^ 1, skip);
            asm.branch(label);
            asm.bind(skip);
        } else {
            asm.cond(cond, label);
        }
    }

    /// Makes the first jump, or the second when the flags say the condition
    /// holds. A jump that gives its block nothing to move is the branch
    /// itself, so the code is one branch and the code of the other jump.
    fn branch(&self, asm: &mut Asm, cond: u32, jumps: &[Jump], next: usize) {
        let [no, yes] = jumps else { unreachable!() };
        let (moves_no, moves_yes) = (self.jump_moves(no), self.jump_moves(yes));
        let free = |m: &[Move<Loc>]| moves::sequence(m).is_empty();
        let (free_no, free_yes) = (free(&moves_no), free(&moves_yes));
        if free_no && (!free_yes || yes.block == next) {
            self.go_if(asm, cond ^ 1, self.labels[no.block]);
            self.jump(asm, yes, next);
        } else if free_yes {
            self.go_if(asm, cond, self.labels[yes.block]);
            self.jump(asm, no, next);
        } else {
            let skip = asm.label();
            asm.cond(cond, skip);
            self.jump(asm, no, usize::MAX);
            asm.bind(skip);
            self.jump(asm, yes, next);
        }
    }

    fn end(&self, asm: &mut Asm, block: usize) {
        let next = block + 1;
        match &self.f.blocks[block].end {
            End::Jump(jump) => self.jump(asm, jump, next),
            End::Switch { on, jumps } if self.branches_on_compare[block] => {
                let compare = self.f.blocks[block].instructions.last().unwrap();
                let Op::Compare { op, a, b } = &compare.op else {
                    unreachable!()
                };
                debug_assert_eq!(compare.results, [*on]);
                let cond = self.compare(asm, op, *a, *b);
                self.branch(asm, cond, jumps, next);
            }
            End::Switch { on, jumps } if jumps.len() == 2 => {
                let on = self.read(asm, X16, *on);
                asm.cmp_imm(on, 0);
                self.branch(asm, NE, jumps, next);
            }
            End::Switch { on, jumps } if jumps.len() >= super::branch::TABLE => {
                // The number is the place of an entry of a table, which
                // says where the code of the jump is: the block, when the
                // jump moves nothing, and the moves then a jump to it when
                // it does, which follows. The table is placed after the code.
                let on = self.read(asm, X16, *on);
                let table = asm.label();
                asm.jump_through(on, table);
                let moves_nothing =
                    |jump: &Jump| moves::sequence(&self.jump_moves(jump)).is_empty();
                let entries: Vec<usize> = (jumps.iter())
                    .map(|jump| {
                        if moves_nothing(jump) {
                            self.labels[jump.block]
                        } else {
                            asm.label()
                        }
                    })
                    .collect();
                asm.table_entries(table, entries.clone());
                let last = jumps.iter().rposition(|j| !moves_nothing(j));
                for (i, jump) in jumps.iter().enumerate() {
                    if !moves_nothing(jump) {
                        asm.bind(entries[i]);
                        let falls = if Some(i) == last { next } else { usize::MAX };
                        self.jump(asm, jump, falls);
                    }
                }
            }
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
                asm.load(X30, 8, false, Place::Frame(self.frame - 16));
                asm.release_stack(self.frame);
                asm.ret();
            }
        }
    }
}
