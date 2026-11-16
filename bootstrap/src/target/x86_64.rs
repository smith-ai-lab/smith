//! x86-64 machine code, shared by every target that runs on x86-64, and
//! how a program's functions become it.

use crate::ir::{Function, Program, Step, Stream, Value};

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
    pub fn cmp_imm32(&mut self, r: Reg, value: u32) {
        self.bytes(&[Self::rex_w(RAX, r), 0x81, Self::modrm(Reg(7), r)]);
        self.bytes(&value.to_le_bytes());
    }

    /// `cmp dword [r], imm8`, sign-extended, for a register below r8 other
    /// than rsp and rbp.
    pub fn cmp_load32_imm8(&mut self, r: Reg, value: i8) {
        assert!(r.0 < 8 && r.0 != 4 && r.0 != 5);
        self.bytes(&[0x83, 7 << 3 | r.0, value as u8]);
    }

    /// The ModRM, SIB, and 32-bit displacement of `[rsp + disp]`, with
    /// `reg` in the ModRM reg field.
    fn far_stack_slot(&mut self, reg: u8, disp: u32) {
        self.bytes(&[0x84 | (reg & 7) << 3, 0x24]);
        self.bytes(&disp.to_le_bytes());
    }

    /// `mov rd, qword [rsp + disp]`.
    pub fn load_stack(&mut self, rd: Reg, disp: u32) {
        self.bytes(&[Self::rex_w(rd, RAX), 0x8b]);
        self.far_stack_slot(rd.0, disp);
    }

    /// `mov qword [rsp + disp], rs`.
    pub fn store_stack(&mut self, disp: u32, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, RAX), 0x89]);
        self.far_stack_slot(rs.0, disp);
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
    pub fn call(&mut self, label: usize) {
        self.bytes(&[0xe8]);
        self.offset_to(Ref::Label(label));
    }

    /// `ret`.
    pub fn ret(&mut self) {
        self.bytes(&[0xc3]);
    }

    /// `lea rd, [rip + label]`.
    pub fn address(&mut self, rd: Reg, label: usize) {
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

    /// `add r64, imm8`, sign-extended.
    pub fn add_imm8(&mut self, r: Reg, value: i8) {
        self.bytes(&[
            Self::rex_w(RAX, r),
            0x83,
            Self::modrm(Reg(0), r),
            value as u8,
        ]);
    }

    /// `movzx rd32, byte [base + index]`, which clears the rest of `rd`.
    /// `base` is not rbp or r13, and `index` is not rsp.
    pub fn load_byte(&mut self, rd: Reg, base: Reg, index: Reg) {
        assert!(base.0 & 7 != 5 && index.0 != 4);
        let rex = (rd.0 >> 3) << 2 | (index.0 >> 3) << 1 | base.0 >> 3;
        if rex != 0 {
            self.bytes(&[0x40 | rex]);
        }
        self.bytes(&[
            0x0f,
            0xb6,
            0x04 | (rd.0 & 7) << 3,
            (index.0 & 7) << 3 | (base.0 & 7),
        ]);
    }

    /// `mov byte [r], imm8`, for a register below r8 other than rsp and
    /// rbp.
    pub fn store_byte_imm(&mut self, r: Reg, value: u8) {
        assert!(r.0 < 8 && r.0 != 4 && r.0 != 5);
        self.bytes(&[0xc6, r.0, value]);
    }

    /// `mov byte [to], from8`: the low byte of `from`, which is one of rax,
    /// rcx, rdx, and rbx, to a register below r8 other than rsp and rbp.
    pub fn store_byte(&mut self, to: Reg, from: Reg) {
        assert!(from.0 < 4 && to.0 < 8 && to.0 != 4 && to.0 != 5);
        self.bytes(&[0x88, from.0 << 3 | to.0]);
    }

    /// `rep movsb`, which copies rcx bytes from rsi to rdi, and leaves rsi
    /// and rdi just after them.
    pub fn copy_bytes(&mut self) {
        self.bytes(&[0xf3, 0xa4]);
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

/// What a system gives the code of an x86-64 target: how it writes and how
/// it ends the program.
pub trait System {
    /// How many bytes at the bottom of every frame a call to the system may
    /// use. A multiple of 16.
    const RESERVED: u32;
    /// The registers that hold the address and the length of the text to
    /// write.
    const TEXT: (Reg, Reg);
    /// Writes every byte of the text in `TEXT`, and jumps to `fail` if the
    /// system refuses it. It may change any register but rsp.
    fn write(asm: &mut Asm, stream: Stream, fail: usize);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u32);
    /// Asks the system for memory for the number of bytes in rsi, which is
    /// not 0, leaves its address in rax, and jumps to `fail` if the system
    /// refuses. It may change any register but rsp.
    fn alloc(asm: &mut Asm, fail: usize);
}

/// The bytes a piece of text takes in a frame: its address, then its
/// length.
const SLOT: u32 = 16;

/// The machine code of the program, and the data it reads, each piece
/// under its label.
///
/// Every function has a frame on the stack that keeps the stack on 16
/// bytes at each call. From the bottom, it holds the bytes the system may
/// use, the arguments of the calls it makes and the texts those calls give
/// back, and the texts it holds. A function finds its parameters among the
/// arguments in its caller's frame, and puts the texts it gives back just
/// after them.
pub fn code<S: System>(program: &Program) -> (Asm, Vec<(usize, &[u8])>) {
    let mut asm = Asm::default();
    let texts: Vec<usize> = program.texts.iter().map(|_| asm.label()).collect();
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
    // The call pushed the return address, so the frame takes 8 bytes more
    // than a multiple of 16.
    let frame = S::RESERVED + SLOT * (outgoing + held) + 8;
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
    at.steps(asm, &f.steps);
    // The values it gives back go after its parameters in its caller's
    // frame, so writing one never overwrites a parameter still to be read.
    for (i, value) in f.results.iter().enumerate() {
        at.load(asm, *value, (RAX, RDX));
        let to = frame + 8 + S::RESERVED + SLOT * (f.params + i) as u32;
        asm.store_stack(to, RAX);
        asm.store_stack(to + 8, RDX);
    }
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
            self.frame + 8 + S::RESERVED + SLOT * local as u32
        } else {
            S::RESERVED + SLOT * (self.outgoing + (local - self.f.params) as u32)
        }
    }

    /// Loads a value into two registers: a text's address and length, or a
    /// case's place and 0.
    fn load(&self, asm: &mut Asm, value: Value, (address, length): (Reg, Reg)) {
        match value {
            Value::Text(t) => {
                asm.address(address, self.texts[t]);
                let len = self.program.texts[t].len() as u64;
                match u32::try_from(len) {
                    Ok(len) => asm.mov32(length, len),
                    Err(_) => asm.mov64(length, len),
                }
            }
            Value::Case(c) => {
                asm.mov32(address, c as u32);
                asm.mov32(length, 0);
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
                    self.load(asm, *value, S::TEXT);
                    S::write(asm, *stream, self.fail);
                }
                Step::Call {
                    function,
                    args,
                    results,
                } => {
                    for (i, arg) in args.iter().enumerate() {
                        self.load(asm, *arg, (RAX, RDX));
                        let at = S::RESERVED + SLOT * i as u32;
                        asm.store_stack(at, RAX);
                        asm.store_stack(at + 8, RDX);
                    }
                    asm.call(self.functions[*function]);
                    for (i, r) in results.iter().enumerate() {
                        let at = S::RESERVED + SLOT * (args.len() + i) as u32;
                        asm.load_stack(RAX, at);
                        asm.load_stack(RDX, at + 8);
                        asm.store_stack(self.slot(*r), RAX);
                        asm.store_stack(self.slot(*r) + 8, RDX);
                    }
                }
                Step::Set { local, value } => {
                    self.load(asm, *value, (RAX, RDX));
                    asm.store_stack(self.slot(*local), RAX);
                    asm.store_stack(self.slot(*local) + 8, RDX);
                }
                Step::Join { local, parts } => {
                    // The length, in rsi, is the sum of the parts'. Memory
                    // is asked for only when it is not 0, and then each part
                    // is copied after the one before it.
                    let to = self.slot(*local);
                    let end = asm.label();
                    asm.mov32(RSI, 0);
                    for part in parts {
                        self.load(asm, *part, (RAX, RDX));
                        asm.add(RSI, RDX);
                    }
                    asm.store_stack(to, RSI);
                    asm.store_stack(to + 8, RSI);
                    asm.test(RSI);
                    asm.jcc(Z, end);
                    S::alloc(asm, self.fail);
                    asm.store_stack(to, RAX);
                    asm.mov(RDI, RAX);
                    for part in parts {
                        self.load(asm, *part, (RSI, RCX));
                        asm.copy_bytes();
                    }
                    asm.bind(end);
                }
                Step::Escape { local, value } => self.escape(asm, *local, *value),
                Step::Equal { local, a, b } => self.equal(asm, *local, *a, *b),
                Step::Match { value, arms } => {
                    // Each arm but the last is chosen by comparing the case
                    // with its place, and the last runs when none was.
                    self.load(asm, *value, (RAX, RDX));
                    let end = asm.label();
                    for (c, arm) in arms.iter().enumerate() {
                        let next = asm.label();
                        if c + 1 < arms.len() {
                            asm.cmp_imm32(RAX, c as u32);
                            asm.jcc(NE, next);
                        }
                        self.steps(asm, arm);
                        asm.jmp(end);
                        asm.bind(next);
                    }
                    asm.bind(end);
                }
            }
        }
    }

    /// Puts in the local case 1 if the texts have the same bytes, and case 0
    /// if not. The case is in rsi, the texts are at r8 and r10, their
    /// lengths are in r9 and rdx, and rcx counts the bytes compared.
    fn equal(&self, asm: &mut Asm, local: usize, a: Value, b: Value) {
        let to = self.slot(local);
        self.load(asm, a, (R8, R9));
        self.load(asm, b, (R10, RDX));
        let (compare, same, done) = (asm.label(), asm.label(), asm.label());
        asm.mov32(RSI, 0);
        asm.cmp(R9, RDX);
        asm.jcc(NE, done);
        asm.mov32(RCX, 0);
        asm.bind(compare);
        asm.cmp(RCX, R9);
        asm.jcc(Z, same);
        asm.load_byte(RAX, R8, RCX);
        asm.load_byte(RDI, R10, RCX);
        asm.cmp(RAX, RDI);
        asm.jcc(NE, done);
        asm.add_imm8(RCX, 1);
        asm.jmp(compare);
        asm.bind(same);
        asm.mov32(RSI, 1);
        asm.bind(done);
        asm.store_stack(to, RSI);
        asm.mov32(RAX, 0);
        asm.store_stack(to + 8, RAX);
    }

    /// Writes the text as a string literal into the local. The first pass
    /// counts the bytes that take a backslash, in rsi, which starts at the
    /// length and the two quotes, and the second writes each byte after
    /// the one before it, from rdi. The text is at r8, its length is in r9,
    /// and rcx counts the bytes read.
    fn escape(&self, asm: &mut Asm, local: usize, value: Value) {
        const ESCAPED: &[u8] = b"\"\\{}";
        let to = self.slot(local);
        self.load(asm, value, (R8, R9));
        asm.mov(RSI, R9);
        asm.add_imm8(RSI, 2);
        asm.mov32(RCX, 0);
        let (count, counted, more, next) = (asm.label(), asm.label(), asm.label(), asm.label());
        asm.bind(count);
        asm.cmp(RCX, R9);
        asm.jcc(Z, counted);
        asm.load_byte(RAX, R8, RCX);
        for &b in ESCAPED.iter().chain(b"\n\t") {
            asm.cmp_imm8(RAX, b as i8);
            asm.jcc(Z, more);
        }
        asm.jmp(next);
        asm.bind(more);
        asm.add_imm8(RSI, 1);
        asm.bind(next);
        asm.add_imm8(RCX, 1);
        asm.jmp(count);
        asm.bind(counted);
        asm.store_stack(to + 8, RSI);
        S::alloc(asm, self.fail);
        asm.store_stack(to, RAX);
        asm.mov(RDI, RAX);
        self.load(asm, value, (R8, R9));
        asm.store_byte_imm(RDI, b'"');
        asm.add_imm8(RDI, 1);
        asm.mov32(RCX, 0);
        let (copy, copied, backslash, plain) = (asm.label(), asm.label(), asm.label(), asm.label());
        asm.bind(copy);
        asm.cmp(RCX, R9);
        asm.jcc(Z, copied);
        asm.load_byte(RAX, R8, RCX);
        asm.mov(RDX, RAX);
        for (b, written) in [(b'\n', b'n'), (b'\t', b't')] {
            let after = asm.label();
            asm.cmp_imm8(RAX, b as i8);
            asm.jcc(NE, after);
            asm.mov32(RDX, written as u32);
            asm.jmp(backslash);
            asm.bind(after);
        }
        for &b in ESCAPED {
            asm.cmp_imm8(RAX, b as i8);
            asm.jcc(Z, backslash);
        }
        asm.jmp(plain);
        asm.bind(backslash);
        asm.store_byte_imm(RDI, b'\\');
        asm.add_imm8(RDI, 1);
        asm.bind(plain);
        asm.store_byte(RDI, RDX);
        asm.add_imm8(RDI, 1);
        asm.add_imm8(RCX, 1);
        asm.jmp(copy);
        asm.bind(copied);
        asm.store_byte_imm(RDI, b'"');
    }
}
