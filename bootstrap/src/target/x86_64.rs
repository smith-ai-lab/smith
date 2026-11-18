//! x86-64 machine code, shared by every target that runs on x86-64, and
//! how a program in the scalar form becomes it. It reads the scalar form
//! and nothing else.

use super::alloc::{self, Home, Registers};
use super::frame::{self, PAGE, call_area, round_up, size};
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
    /// How far a label is from the start of a table, which is another label.
    From { label: usize, table: usize },
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
    /// The tables, each a label and the labels whose distances from it are its
    /// entries, which are placed after the code, with the data.
    tables: Vec<(usize, Vec<usize>)>,
}

impl Asm {
    pub fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    pub fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
    }

    /// How many bytes of code there are so far, and of the tables that are
    /// placed after it.
    pub fn len(&self) -> usize {
        self.code.len()
            + self
                .tables
                .iter()
                .map(|(_, labels)| 4 * labels.len())
                .sum::<usize>()
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

    /// Puts the number in the register, whole, in the shortest way.
    pub fn mov_imm(&mut self, rd: Reg, value: u64) {
        match u32::try_from(value) {
            Ok(n) => self.mov32(rd, n),
            Err(_) => self.mov64(rd, value),
        }
    }

    /// Writes the low `width` bytes of the number to the place, in one
    /// instruction, which holds it: as the 32 bits it takes when it is 4
    /// bytes, or 8 and the number is the same when taken as 32 bits and
    /// extended. Says whether it could.
    pub fn store_imm(&mut self, to: Place, value: u64, width: u32) -> bool {
        let rex = Self::base(to) >> 3;
        match width {
            1 => {
                if rex != 0 {
                    self.bytes(&[0x40 | rex]);
                }
                self.bytes(&[0xc6]);
                self.place(Reg(0), to);
                self.bytes(&[value as u8]);
            }
            2 => {
                self.bytes(&[0x66]);
                if rex != 0 {
                    self.bytes(&[0x40 | rex]);
                }
                self.bytes(&[0xc7]);
                self.place(Reg(0), to);
                self.bytes(&(value as u16).to_le_bytes());
            }
            4 => {
                if rex != 0 {
                    self.bytes(&[0x40 | rex]);
                }
                self.bytes(&[0xc7]);
                self.place(Reg(0), to);
                self.bytes(&(value as u32).to_le_bytes());
            }
            _ => {
                let Ok(n) = i32::try_from(value as i64) else {
                    return false;
                };
                self.bytes(&[0x48 | rex, 0xc7]);
                self.place(Reg(0), to);
                self.bytes(&n.to_le_bytes());
            }
        }
        true
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

    /// `op r64, imm`, sign-extended, for `add` as 0, `or` as 1, `and` as 4,
    /// `sub` as 5, `xor` as 6, and `cmp` as 7.
    fn alu_imm(&mut self, op: u8, r: Reg, value: i32) {
        match i8::try_from(value) {
            Ok(small) => self.bytes(&[
                Self::rex_w(RAX, r),
                0x83,
                Self::modrm(Reg(op), r),
                small as u8,
            ]),
            Err(_) => {
                self.bytes(&[Self::rex_w(RAX, r), 0x81, Self::modrm(Reg(op), r)]);
                self.bytes(&value.to_le_bytes());
            }
        }
    }

    /// A 64-bit instruction with one operand in memory and a register in the
    /// ModRM reg field: `cmp [place], r` is 0x39, `cmp r, [place]` 0x3b, and
    /// `imul r, [place], imm32` 0x69 with an immediate after it.
    fn with_memory(&mut self, opcode: u8, reg: Reg, place: Place) {
        self.bytes(&[0x48 | (reg.0 >> 3) << 2 | Self::base(place) >> 3, opcode]);
        self.place(reg, place);
    }

    /// `cmp qword [place], imm`, sign-extended.
    fn cmp_memory_imm(&mut self, place: Place, value: i32) {
        match i8::try_from(value) {
            Ok(small) => {
                self.with_memory(0x83, Reg(7), place);
                self.bytes(&[small as u8]);
            }
            Err(_) => {
                self.with_memory(0x81, Reg(7), place);
                self.bytes(&value.to_le_bytes());
            }
        }
    }

    /// `imul rd, qword [place], imm32`.
    fn imul_memory_imm(&mut self, rd: Reg, place: Place, value: i32) {
        self.with_memory(0x69, rd, place);
        self.bytes(&value.to_le_bytes());
    }

    /// `imul rd, rs, imm32`.
    fn imul_imm(&mut self, rd: Reg, rs: Reg, value: i32) {
        self.bytes(&[Self::rex_w(rd, rs), 0x69, Self::modrm(rd, rs)]);
        self.bytes(&value.to_le_bytes());
    }

    /// `lea rd, [ra + rb]`.
    fn lea_sum(&mut self, rd: Reg, ra: Reg, rb: Reg) {
        // rsp cannot be an index, and rbp and r13 as a base need a
        // displacement, so the sum is taken the other way round or with a
        // displacement of 0.
        let (base, index) = if rb.0 == 4 { (rb, ra) } else { (ra, rb) };
        let rex = 0x48 | (rd.0 >> 3) << 2 | (index.0 >> 3) << 1 | base.0 >> 3;
        if base.0 & 7 == 5 {
            self.bytes(&[rex, 0x8d, 0x44 | (rd.0 & 7) << 3, (index.0 & 7) << 3 | 5, 0]);
        } else {
            self.bytes(&[
                rex,
                0x8d,
                0x04 | (rd.0 & 7) << 3,
                (index.0 & 7) << 3 | (base.0 & 7),
            ]);
        }
    }

    /// `lea rd, [rs + disp32]`.
    fn lea_displaced(&mut self, rd: Reg, rs: Reg, disp: i32) {
        self.bytes(&[
            Self::rex_w(rd, rs),
            0x8d,
            0x80 | (rd.0 & 7) << 3 | (rs.0 & 7),
        ]);
        if rs.0 & 7 == 4 {
            self.bytes(&[0x24]);
        }
        self.bytes(&disp.to_le_bytes());
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

    /// Makes room on the stack for a frame of more than a page, as a system
    /// that gives a program its stack one page at a time needs: it touches
    /// each page below the stack pointer in turn, from the nearest, so that
    /// none is passed over, then moves the stack pointer and touches the
    /// lowest page.
    pub fn reserve_stack_by_page(&mut self, bytes: u32) {
        for page in 1..=bytes / PAGE {
            // `test dword [rsp - page * PAGE], eax`, which changes only flags.
            self.bytes(&[0x85, 0x84, 0x24]);
            self.bytes(&(page * PAGE).wrapping_neg().to_le_bytes());
        }
        self.reserve_stack(bytes);
        // `test dword [rsp], eax`.
        self.bytes(&[0x85, 0x04, 0x24]);
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

    /// Goes to the label that entry `index` of the table at `table` says, as
    /// `table_entries` writes it, which is 4 bytes for each: `rcx` and `rdx`
    /// are changed.
    pub fn jump_through(&mut self, index: Reg, table: usize) {
        self.address(RCX, table);
        // movsxd rdx, [rcx + 4 * index]
        self.bytes(&[
            0x48 | (index.0 >> 3) << 1,
            0x63,
            0x14,
            0x81 | (index.0 & 7) << 3,
        ]);
        // add rdx, rcx
        self.bytes(&[0x48, 0x01, 0xca]);
        // jmp rdx
        self.bytes(&[0xff, 0xe2]);
    }

    /// The table at `table`, which is placed after the code: for each label,
    /// how far it is from the start of the table.
    pub fn table_entries(&mut self, table: usize, labels: Vec<usize>) {
        self.tables.push((table, labels));
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

    /// `or rd, rs`.
    fn or(&mut self, rd: Reg, rs: Reg) {
        self.bytes(&[Self::rex_w(rs, rd), 0x09, Self::modrm(rs, rd)]);
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

    /// An instruction that shifts `r` by `cl`, by the number of the
    /// operation in the ModRM reg field: 4 for `shl`, 5 for `shr`, and 7
    /// for `sar`. The count is taken modulo 64.
    fn shift_cl(&mut self, op: u8, r: Reg) {
        self.bytes(&[Self::rex_w(RAX, r), 0xd3, Self::modrm(Reg(op), r)]);
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
        for (table, labels) in std::mem::take(&mut self.tables) {
            self.bind(table);
            for label in labels {
                self.fixups
                    .push((self.code.len(), Ref::From { label, table }));
                self.bytes(&[0; 4]);
            }
        }
        for (at, to) in &self.fixups {
            let place = |label: &usize| self.labels[*label].expect("every label is bound") as i64;
            let offset = match to {
                Ref::Label(label) => place(label) - (*at as i64 + 4),
                Ref::Slot(slot) => {
                    (slots_address + 8 * *slot as u64) as i64
                        - code_address as i64
                        - (*at as i64 + 4)
                }
                Ref::From { label, table } => place(label) - place(table),
            };
            let offset = i32::try_from(offset).expect("in reach");
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
    /// Whether the system gives a program its stack one page at a time, so
    /// that a frame of more than `PAGE` bytes is reserved a page at a time.
    const STACK_BY_PAGE: bool = false;
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
    /// Ends the program with the status in rdi.
    fn end(asm: &mut Asm);
    /// Ends the program with the status.
    fn exit(asm: &mut Asm, status: u32) {
        asm.mov32(RDI, status);
        Self::end(asm);
    }
    /// Asks the system for memory for the number of bytes in rsi, which is
    /// not 0, leaves its address in rax, and jumps to `fail` if the system
    /// refuses. It may change what `write` may.
    fn alloc(asm: &mut Asm, fail: usize);
    /// Gives `main` what the system gives a program that takes its words, in
    /// rdi, rsi, and r8: a number, the address of an array of addresses, and
    /// the address of a command line, as `scalar::ARGUMENTS` says. It runs
    /// first, before the stack is touched, so a system that gives the words
    /// on the stack reads them here, and may change only those registers.
    fn arguments_on_entry(asm: &mut Asm);
    /// Does what `arguments_on_entry` does for a system that gives the words
    /// by a call. The stack is ready for a call, and rdi, rsi, and r8 are as
    /// `arguments_on_entry` left them.
    fn arguments_by_call(asm: &mut Asm);
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
    let takes = program.takes_arguments();
    if takes {
        S::arguments_on_entry(&mut asm);
    }
    asm.align_stack();
    asm.reserve_stack(S::RESERVED);
    if takes {
        S::arguments_by_call(&mut asm);
    }
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
    /// Which blocks end with a choice between two jumps on a comparison that
    /// is the last instruction of the block and that nothing else reads. That
    /// comparison is not made into a value: the branch tests it.
    branches_on_compare: Vec<bool>,
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
    // The call pushed the return address, so the frame takes 8 bytes more
    // than a multiple of 16.
    let total = round_up(saved_from + 8 * allocation.saved.len() as u32, 16) + 8;
    let area = call_area(f, REGISTERS.passed);
    let in_caller = |places: &[u32]| -> Vec<Loc> {
        passing(places.iter().map(|o| total + 8 + S::RESERVED + o))
    };
    let arrive = in_caller(&area.params);
    let leave = in_caller(&area.results);
    let at = (allocation.homes.iter().enumerate())
        .map(|(v, home)| match home {
            Home::Register(r) => Loc::Register(HOMES[*r]),
            Home::Frame(place) => Loc::Frame(below + place),
            Home::Held => Loc::Imm(constants[v].expect("a value held is a constant")),
        })
        .collect();
    let saved = (allocation.saved.iter().enumerate())
        .map(|(i, r)| (HOMES[*r], saved_from + 8 * i as u32))
        .collect();
    let labels = f.blocks.iter().map(|_| asm.label()).collect();
    let branches_on_compare = super::branch::on_compare(f);
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
        branches_on_compare,
        constants,
        system: std::marker::PhantomData,
    };
    if S::STACK_BY_PAGE && total > PAGE {
        asm.reserve_stack_by_page(total);
    } else {
        asm.reserve_stack(total);
    }
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
}

/// The number an instruction holds in itself in place of its second operand,
/// when it is a constant that fits 32 bits as the instruction reads it:
/// sign-extended to 64 bits it is the number the operand stands for.
fn immediate(f: &Function, constants: &[Option<u64>], op: &Op) -> Option<i32> {
    let (a, b, signed) = match op {
        Op::Binary {
            op: Binary::Add | Binary::Sub | Binary::And | Binary::Or | Binary::Xor | Binary::Mul,
            a,
            b,
        } => (*a, *b, false),
        Op::Compare { op, a, b } => (*a, *b, matches!(op, Compare::LessS)),
        _ => return None,
    };
    let n = constants[b.0]?;
    let bytes = size(f.types[a.0]);
    if bytes == 8 {
        i32::try_from(n as i64).ok()
    } else if signed {
        let shift = 64 - 8 * bytes;
        i32::try_from(((n << shift) as i64) >> shift).ok()
    } else {
        i32::try_from(n).ok()
    }
}

/// Which values that a constant gives need no register or place in the frame,
/// as each instruction that reads one holds it: as the second operand of an
/// instruction that can, as the value stored or chosen, or as what a jump,
/// a call, or a return gives, which is moved there with the number. A value
/// that anything else reads, such as an address or the place a choice is
/// made on, has a home as any other.
fn held(f: &Function, constants: &[Option<u64>]) -> Vec<bool> {
    let mut needs_a_home = vec![false; f.types.len()];
    for block in &f.blocks {
        for i in &block.instructions {
            let embedded = immediate(f, constants, &i.op).is_some();
            for (k, v) in i.op.operands().into_iter().enumerate() {
                needs_a_home[v.0] |= !match &i.op {
                    Op::Binary { .. } | Op::Compare { .. } => embedded && k == 1,
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
    /// The number the instruction holds in itself, if it does.
    fn immediate(&self, op: &Op) -> Option<i32> {
        immediate(self.f, &self.constants, op)
    }

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
            (Loc::Register(to), Loc::Imm(n)) => asm.mov_imm(to, n),
            (Loc::Frame(to), Loc::Imm(n)) => self.store_imm(asm, Place::Frame(to), n, width, RAX),
            (Loc::Imm(_), _) => unreachable!("a number is not a place to copy to"),
        }
    }

    /// Writes the number, `width` bytes of it, to the place, in the one
    /// instruction when it holds the number, and through `tmp` when not, which
    /// is not a register the place is based on.
    fn store_imm(&self, asm: &mut Asm, to: Place, n: u64, width: u32, tmp: Reg) {
        if !asm.store_imm(to, n, width) {
            asm.mov_imm(tmp, n);
            asm.store(to, tmp, width);
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
            Loc::Imm(n) => {
                asm.mov_imm(tmp, n);
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
            Loc::Imm(n) => {
                let shift = 64 - 8 * width;
                asm.mov_imm(rd, (((n << shift) as i64) >> shift) as u64);
            }
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
            Loc::Imm(_) => unreachable!("an instruction gives no result that is a number held"),
        }
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], operation: &Op) {
        match operation {
            Op::Constant(_) if matches!(self.at[results[0].0], Loc::Imm(_)) => {}
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
            Op::Binary { op, a, b } => {
                let immediate = self.immediate(operation);
                self.binary(asm, results[0], *op, *a, *b, immediate)
            }
            Op::Compare { op, a, b } => {
                let cond = self.compare(asm, op, *a, *b);
                asm.truth(cond);
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
                match self.at[value.0] {
                    Loc::Imm(n) => self.store_imm(asm, Place::At(address), n, size(*ty), RCX),
                    _ => {
                        let value = self.read(asm, RCX, *value);
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
                call: Call::Exit,
                args,
            } => {
                self.read_into(asm, RDI, args[0]);
                S::end(asm);
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

    fn binary(
        &self,
        asm: &mut Asm,
        result: Value,
        op: Binary,
        a: Value,
        b: Value,
        immediate: Option<i32>,
    ) {
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
            // The count is in cl, and the value, extended to 64 bits as the
            // shift reads it, is in rax.
            Binary::ShiftLeft | Binary::ShiftRightU | Binary::ShiftRightS => {
                self.read_into(asm, RCX, b);
                let (n, extended) = match op {
                    Binary::ShiftLeft => (4, false),
                    Binary::ShiftRightU => (5, true),
                    _ => (7, false),
                };
                if matches!(op, Binary::ShiftRightS) {
                    self.read_signed_into(asm, RAX, a);
                } else {
                    self.read_into(asm, RAX, a);
                }
                asm.shift_cl(n, RAX);
                self.finish(asm, result, RAX, extended);
            }
            Binary::Mul if immediate.is_some() => {
                let t = self.target(result, &[]);
                match self.at[a.0] {
                    Loc::Frame(at) if wide => {
                        asm.imul_memory_imm(t, Place::Frame(at), immediate.unwrap());
                    }
                    _ => {
                        let ra = self.read(asm, RAX, a);
                        asm.imul_imm(t, ra, immediate.unwrap());
                    }
                }
                self.finish(asm, result, t, false);
            }
            Binary::Add | Binary::Sub if immediate.is_some() && wide => {
                let ra = self.read(asm, RAX, a);
                let t = self.target(result, &[]);
                let n = immediate.unwrap();
                match (op, n.checked_neg()) {
                    (Binary::Add, _) if t != ra => asm.lea_displaced(t, ra, n),
                    (Binary::Sub, Some(n)) if t != ra => asm.lea_displaced(t, ra, n),
                    _ => {
                        if t != ra {
                            asm.mov(t, ra);
                        }
                        asm.alu_imm(if matches!(op, Binary::Add) { 0 } else { 5 }, t, n);
                    }
                }
                self.finish(asm, result, t, true);
            }
            _ if immediate.is_some() => {
                let t = self.target(result, &[]);
                self.read_into(asm, t, a);
                let ext = match op {
                    Binary::Add => 0,
                    Binary::Or => 1,
                    Binary::And => 4,
                    Binary::Sub => 5,
                    Binary::Xor => 6,
                    _ => unreachable!(),
                };
                asm.alu_imm(ext, t, immediate.unwrap());
                self.finish(
                    asm,
                    result,
                    t,
                    matches!(op, Binary::And | Binary::Xor | Binary::Or),
                );
            }
            Binary::Add if wide => {
                let ra = self.read(asm, RAX, a);
                let rb = self.read(asm, RCX, b);
                let t = self.target(result, &[]);
                if t == ra {
                    asm.add(t, rb);
                } else if t == rb {
                    asm.add(t, ra);
                } else {
                    asm.lea_sum(t, ra, rb);
                }
                self.finish(asm, result, t, true);
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
                    Binary::Or => asm.or(t, b),
                    Binary::MulHighU
                    | Binary::MulHighS
                    | Binary::ShiftLeft
                    | Binary::ShiftRightU
                    | Binary::ShiftRightS => unreachable!(),
                }
                self.finish(
                    asm,
                    result,
                    t,
                    matches!(op, Binary::And | Binary::Xor | Binary::Or),
                );
            }
        }
    }

    /// Compares the two values, and gives the condition code under which the
    /// first is below, or the same as, the second, as the comparison says.
    fn compare(&self, asm: &mut Asm, op: &Compare, a: Value, b: Value) -> u8 {
        let signed = matches!(op, Compare::LessS);
        let cond = match op {
            Compare::Equal => Z,
            Compare::LessU => B,
            Compare::LessS => L,
        };
        let held = self.immediate(&Op::Compare { op: *op, a, b });
        // An integer of 8 bytes in the frame is compared where it is.
        let in_frame = |v: Value| match self.at[v.0] {
            Loc::Frame(at) if self.width(v) == 8 => Some(Place::Frame(at)),
            _ => None,
        };
        match (in_frame(a), held, in_frame(b)) {
            (Some(at), Some(n), _) => asm.cmp_memory_imm(at, n),
            (Some(at), None, _) if matches!(self.at[b.0], Loc::Register(_)) => {
                let Loc::Register(rb) = self.at[b.0] else {
                    unreachable!()
                };
                asm.with_memory(0x39, rb, at);
            }
            (None, None, Some(at)) if matches!(self.at[a.0], Loc::Register(_)) => {
                let Loc::Register(ra) = self.at[a.0] else {
                    unreachable!()
                };
                asm.with_memory(0x3b, ra, at);
            }
            _ => {
                let ra = if signed {
                    self.read_signed(asm, RAX, a)
                } else {
                    self.read(asm, RAX, a)
                };
                match held {
                    Some(n) => asm.alu_imm(7, ra, n),
                    None => {
                        let rb = if signed {
                            self.read_signed(asm, RCX, b)
                        } else {
                            self.read(asm, RCX, b)
                        };
                        asm.cmp(ra, rb);
                    }
                }
            }
        }
        cond
    }

    /// The moves that give the block a jump goes to its parameters.
    fn jump_moves(&self, jump: &crate::scalar::Jump) -> Vec<Move<Loc>> {
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
    fn jump(&self, asm: &mut Asm, jump: &crate::scalar::Jump, next: usize) {
        self.run(asm, &self.jump_moves(jump));
        if jump.block != next {
            asm.jmp(self.labels[jump.block]);
        }
    }

    /// Makes the first jump, or the second when the flags say the condition
    /// holds. A jump that gives its block nothing to move is the branch
    /// itself, so the code is one branch and the code of the other jump.
    fn branch(&self, asm: &mut Asm, cond: u8, jumps: &[crate::scalar::Jump], next: usize) {
        let [no, yes] = jumps else { unreachable!() };
        let (moves_no, moves_yes) = (self.jump_moves(no), self.jump_moves(yes));
        let free = |m: &[Move<Loc>]| moves::sequence(m).is_empty();
        let (free_no, free_yes) = (free(&moves_no), free(&moves_yes));
        if free_no && (!free_yes || yes.block == next) {
            asm.jcc(cond ^ 1, self.labels[no.block]);
            self.jump(asm, yes, next);
        } else if free_yes {
            asm.jcc(cond, self.labels[yes.block]);
            self.jump(asm, no, next);
        } else {
            let skip = asm.label();
            asm.jcc(cond, skip);
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
                let on = self.read(asm, RAX, *on);
                asm.cmp_imm8(on, 0);
                self.branch(asm, NE, jumps, next);
            }
            End::Switch { on, jumps } if jumps.len() >= super::branch::TABLE => {
                // The number is the place of an entry of a table, which
                // says where the code of the jump is: the block, when the
                // jump moves nothing, and the moves then a jump to it when
                // it does, which follows. The table is placed after the code.
                let on = self.read(asm, RAX, *on);
                let table = asm.label();
                asm.jump_through(on, table);
                let moves_nothing =
                    |jump: &crate::scalar::Jump| moves::sequence(&self.jump_moves(jump)).is_empty();
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
