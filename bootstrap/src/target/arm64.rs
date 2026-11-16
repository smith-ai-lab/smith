//! The arm64 instructions the arm64 targets share, with labels resolved
//! once the code and its data are laid out, and how a program's functions
//! become them.

use crate::ir::{Function, Program, Step, Stream, Value};

pub const X0: u32 = 0;
pub const X1: u32 = 1;
pub const X2: u32 = 2;
pub const X3: u32 = 3;
pub const X4: u32 = 4;
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

    /// The base register and the offset from it of the 8 bytes at
    /// `sp + offset`, which `ldr` and `str` reach.
    fn stack_place(&mut self, offset: u32) -> (u32, u32) {
        assert!(offset.is_multiple_of(8));
        if offset / 8 < 1 << 12 {
            (SP, offset)
        } else {
            self.add_immediate(0x9100_0000, X17, SP, offset);
            (X17, 0)
        }
    }

    /// `ldr xt, [sp, #offset]`.
    pub fn load_stack(&mut self, rt: u32, offset: u32) {
        let (base, offset) = self.stack_place(offset);
        self.word(0xf940_0000 | (offset / 8) << 10 | base << 5 | rt);
    }

    /// `str xt, [sp, #offset]`.
    pub fn store_stack(&mut self, offset: u32, rt: u32) {
        let (base, offset) = self.stack_place(offset);
        self.word(0xf900_0000 | (offset / 8) << 10 | base << 5 | rt);
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

    /// `ldr wt, [sp, #offset]`, which loads 32 bits from the stack.
    pub fn load32_stack(&mut self, rt: u32, offset: u32) {
        assert!(offset.is_multiple_of(4) && offset / 4 < 1 << 12);
        self.word(0xb940_0000 | (offset / 4) << 10 | SP << 5 | rt);
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

/// What a system gives the code of an arm64 target: how it writes and how
/// it ends the program.
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
}

/// The bytes a piece of text takes in a frame: its address, then its
/// length.
const SLOT: u32 = 16;

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack, a multiple of 16 bytes. From
/// the bottom, it holds the bytes the system may use, the arguments of the
/// calls it makes and the texts those calls give back, the texts it holds,
/// and the address it returns to. A function finds its parameters among
/// the arguments in its caller's frame, and puts the texts it gives back
/// just after them.
pub fn code<S: System>(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
    let mut asm = Asm::default();
    let texts: Vec<usize> = program.texts.iter().map(|_| asm.label()).collect();
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
        .zip(program.texts.iter().map(Vec::as_slice))
        .collect();
    (asm, data)
}

fn function<S: System>(
    asm: &mut Asm,
    program: &Program,
    f: &Function,
    texts: &[usize],
    functions: &[usize],
    fail: usize,
) {
    let outgoing = f.outgoing() as u32;
    let held = (f.locals - f.params) as u32;
    let frame = S::RESERVED + SLOT * (outgoing + held) + 16;
    let at = Frame::<S> {
        program,
        f,
        texts,
        functions,
        fail,
        frame,
        outgoing,
        system: std::marker::PhantomData,
    };
    asm.reserve_stack(frame);
    asm.store_stack(frame - 16, X30);
    at.steps(asm, &f.steps);
    // The values it gives back go after its parameters in its caller's
    // frame, so writing one never overwrites a parameter still to be read.
    for (i, value) in f.results.iter().enumerate() {
        at.load(asm, *value, X0, X1);
        let to = frame + S::RESERVED + SLOT * (f.params + i) as u32;
        asm.store_stack(to, X0);
        asm.store_stack(to + 8, X1);
    }
    asm.load_stack(X30, frame - 16);
    asm.release_stack(frame);
    asm.ret();
}

/// A function being written, and where its frame keeps each value.
struct Frame<'a, S> {
    program: &'a Program,
    f: &'a Function,
    texts: &'a [usize],
    functions: &'a [usize],
    fail: usize,
    frame: u32,
    outgoing: u32,
    system: std::marker::PhantomData<S>,
}

impl<S: System> Frame<'_, S> {
    /// Where a local is, from the bottom of the frame.
    fn slot(&self, local: usize) -> u32 {
        if local < self.f.params {
            self.frame + S::RESERVED + SLOT * local as u32
        } else {
            S::RESERVED + SLOT * (self.outgoing + (local - self.f.params) as u32)
        }
    }

    /// Loads a value into two registers: a text's address and length, or a
    /// case's place and 0.
    fn load(&self, asm: &mut Asm, value: Value, address: u32, length: u32) {
        match value {
            Value::Text(t) => {
                asm.address(address, self.texts[t]);
                asm.mov64(length, self.program.texts[t].len() as u64);
            }
            Value::Case(c) => {
                asm.mov64(address, c as u64);
                asm.mov64(length, 0);
            }
            Value::Local(l) => {
                asm.load_stack(address, self.slot(l));
                asm.load_stack(length, self.slot(l) + 8);
            }
        }
    }

    fn steps(&self, asm: &mut Asm, steps: &[Step]) {
        for step in steps {
            match step {
                Step::Write { stream, value } => {
                    self.load(asm, *value, X19, X20);
                    S::write(asm, *stream, self.fail);
                }
                Step::Call {
                    function,
                    args,
                    results,
                } => {
                    for (i, arg) in args.iter().enumerate() {
                        self.load(asm, *arg, X0, X1);
                        let at = S::RESERVED + SLOT * i as u32;
                        asm.store_stack(at, X0);
                        asm.store_stack(at + 8, X1);
                    }
                    asm.call_label(self.functions[*function]);
                    for (i, r) in results.iter().enumerate() {
                        let at = S::RESERVED + SLOT * (args.len() + i) as u32;
                        asm.load_stack(X0, at);
                        asm.load_stack(X1, at + 8);
                        asm.store_stack(self.slot(*r), X0);
                        asm.store_stack(self.slot(*r) + 8, X1);
                    }
                }
                Step::Set { local, value } => {
                    self.load(asm, *value, X0, X1);
                    asm.store_stack(self.slot(*local), X0);
                    asm.store_stack(self.slot(*local) + 8, X1);
                }
                Step::Match { value, arms } => {
                    // Each arm but the last is chosen by comparing the case
                    // with its place, and the last runs when none was.
                    self.load(asm, *value, X0, X1);
                    let end = asm.label();
                    for (c, arm) in arms.iter().enumerate() {
                        let next = asm.label();
                        if c + 1 < arms.len() {
                            asm.mov64(X17, c as u64);
                            asm.cmp(X0, X17);
                            asm.cond(NE, next);
                        }
                        self.steps(asm, arm);
                        asm.branch(end);
                        asm.bind(next);
                    }
                    asm.bind(end);
                }
            }
        }
    }
}
