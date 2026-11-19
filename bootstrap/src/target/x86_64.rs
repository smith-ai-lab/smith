//! x86-64 machine code, shared by every target that runs on x86-64, and
//! how a program in the scalar form becomes it. It reads the scalar form
//! and nothing else.

use super::alloc::{self, Home, Kinds, Registers};
use super::frame::{self, PAGE, Passed, call_area, round_up, size};
use super::indexed::Folded;
use super::moves::{self, Move, Step};
use crate::scalar::{
    At as Written, Binary, Compare, Constant, End, FloatBinary, FloatCompare, Function, Fused, Op,
    Program, Stream, System as Call, Type, Value,
};

mod level;
mod vector;

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

/// The registers that hold floats and vectors: `xmm0` to `xmm12`, in the order
/// the allocator numbers them, after those of `HOMES`. A call may change every
/// one, on every system, as a call into the system of one of them may. `xmm13`,
/// `xmm14`, and `xmm15` are kept for the chip: `xmm15` is where a result is
/// worked out when its home is the frame or one of its operands, `xmm14` holds
/// a value that is read from the frame to be compared or converted, and
/// `xmm13` is the third that the instructions of vectors work in.
const FLOATS: [Reg; 13] = [
    Reg(0),
    Reg(1),
    Reg(2),
    Reg(3),
    Reg(4),
    Reg(5),
    Reg(6),
    Reg(7),
    Reg(8),
    Reg(9),
    Reg(10),
    Reg(11),
    Reg(12),
];
const XMM13: Reg = Reg(13);
const XMM14: Reg = Reg(14);
const XMM15: Reg = Reg(15);

/// A call passes its first eight floats, and the function gives its first eight
/// results that are floats, in `xmm0` to `xmm7`, in the order each has among
/// the floats. What does not fit goes in the caller's frame.
const FLOAT_REGISTERS: Registers = Registers {
    changed: 13,
    preserved: 0,
    passed: 8,
};

const KINDS: Kinds = Kinds {
    ints: REGISTERS,
    floats: FLOAT_REGISTERS,
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

/// The operand of an instruction of floats that is read: a register, or a
/// place in memory.
#[derive(Clone, Copy)]
pub enum Rm {
    Reg(Reg),
    Place(Place),
}

/// Where a read or a write of memory is.
#[derive(Clone, Copy)]
pub enum Place {
    /// `[rsp + disp]`.
    Frame(u32),
    /// `[reg]`.
    At(Reg),
    /// `[base + index * scale]`, with a scale of 1, 2, 4, or 8.
    Indexed { base: Reg, index: Reg, scale: u8 },
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
    /// For each of rax, rcx, and rdx, which the Writer works in, how many
    /// instructions that may have changed it were written. A value that is
    /// held in one of them is still there while its number is the same.
    versions: [u32; 3],
    /// Where in the code the code of an instruction with a new place begins, with
    /// that place, for the map of an executable.
    places: Vec<(usize, Written)>,
}

impl Asm {
    /// Notes that the code written from here on is for an instruction that is
    /// written at `at`, when that is not the place of the code before it.
    pub fn note_place(&mut self, at: Written) {
        if self.places.last().is_none_or(|(_, last)| *last != at) {
            self.places.push((self.code.len(), at));
        }
    }

    /// How many instructions that may have changed the register were
    /// written, for rax, rcx, and rdx, and 0 for any other.
    pub fn version(&self, r: Reg) -> u32 {
        self.versions.get(r.0 as usize).copied().unwrap_or(0)
    }

    /// Writes an instruction that changes the register `rd` and no other,
    /// or none when it is `None`. Every instruction that is written otherwise
    /// is taken to have changed rax, rcx, and rdx.
    fn writing(&mut self, rd: Option<Reg>, emit: impl FnOnce(&mut Self)) {
        let kept = self.versions;
        emit(self);
        self.versions = kept;
        if let Some(r) = rd.filter(|r| r.0 < 3) {
            self.versions[r.0 as usize] += 1;
        }
    }

    pub fn label(&mut self) -> usize {
        self.labels.push(None);
        self.labels.len() - 1
    }

    /// Where the label is bound, in the code.
    pub fn label_offset(&self, label: usize) -> usize {
        self.labels[label].expect("a label that is bound")
    }

    /// How many bytes of code there are so far, not counting the tables placed
    /// after it.
    pub fn code_len(&self) -> usize {
        self.code.len()
    }

    pub fn bind(&mut self, label: usize) {
        self.labels[label] = Some(self.code.len());
        // Another path may come here.
        self.versions = self.versions.map(|v| v + 1);
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
        self.versions = self.versions.map(|v| v + 1);
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
        self.writing(Some(rd), |a| {
            if rd.0 >= 8 {
                a.bytes(&[0x41]);
            }
            a.bytes(&[0xb8 | (rd.0 & 7)]);
            a.bytes(&value.to_le_bytes());
        });
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
        let kept = self.versions;
        let done = self.store_imm_bytes(to, value, width);
        self.versions = kept;
        done
    }

    fn store_imm_bytes(&mut self, to: Place, value: u64, width: u32) -> bool {
        let rex = Self::rex_xb(to);
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
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(RAX, rd), 0xb8 | (rd.0 & 7)]);
            a.bytes(&value.to_le_bytes());
        });
    }

    /// `mov rd, rs`.
    pub fn mov(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x89, Self::modrm(rs, rd)]);
        });
    }

    /// `test r64, r64`.
    pub fn test(&mut self, r: Reg) {
        self.writing(None, |a| {
            a.bytes(&[Self::rex_w(r, r), 0x85, Self::modrm(r, r)]);
        });
    }

    /// `test r32, r32`.
    pub fn test32(&mut self, r: Reg) {
        self.writing(None, |a| {
            if r.0 >= 8 {
                a.bytes(&[0x45]);
            }
            a.bytes(&[0x85, Self::modrm(r, r)]);
        });
    }

    /// `mov r32, r32`, which clears the upper half.
    fn mov32_rr(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            if rd.0 >= 8 || rs.0 >= 8 {
                a.bytes(&[0x40 | (rs.0 >> 3) << 2 | rd.0 >> 3]);
            }
            a.bytes(&[0x89, Self::modrm(rs, rd)]);
        });
    }

    /// `movzx r32, r8`, the low byte of `rs`.
    fn movzx8(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            if rs.0 >= 4 || rd.0 >= 8 {
                a.bytes(&[0x40 | (rd.0 >> 3) << 2 | rs.0 >> 3]);
            }
            a.bytes(&[0x0f, 0xb6, Self::modrm(rd, rs)]);
        });
    }

    /// `movzx r32, r16`, the low 16 bits of `rs`.
    fn movzx16(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            if rd.0 >= 8 || rs.0 >= 8 {
                a.bytes(&[0x40 | (rd.0 >> 3) << 2 | rs.0 >> 3]);
            }
            a.bytes(&[0x0f, 0xb7, Self::modrm(rd, rs)]);
        });
    }

    /// `movsx r64, r8`, `r16`, or `r32`: the low `width` bytes of `rs`,
    /// extended as a signed number.
    fn movsx(&mut self, rd: Reg, rs: Reg, width: u32) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, rs)]);
            match width {
                1 => a.bytes(&[0x0f, 0xbe]),
                2 => a.bytes(&[0x0f, 0xbf]),
                4 => a.bytes(&[0x63]),
                _ => unreachable!("an integer of {width} bytes"),
            }
            a.bytes(&[Self::modrm(rd, rs)]);
        });
    }

    /// `cmp ra, rb`, which compares `ra - rb`.
    pub fn cmp(&mut self, ra: Reg, rb: Reg) {
        self.writing(None, |a| {
            a.bytes(&[Self::rex_w(rb, ra), 0x39, Self::modrm(rb, ra)]);
        });
    }

    /// `cmova rd, rs`: copies `rs` when the last comparison found its first
    /// operand above its second, as unsigned numbers.
    pub fn cmova(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, rs), 0x0f, 0x47, Self::modrm(rd, rs)]);
        });
    }

    /// `cmovne rd, rs`: copies `rs` when the last test found a value that is
    /// not 0.
    fn cmov(&mut self, cond: u8, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, rs), 0x0f, 0x40 | cond, Self::modrm(rd, rs)]);
        });
    }

    /// The ModRM, SIB, and 8-bit displacement of `[rsp + disp]`, with `reg`
    /// in the ModRM reg field.
    fn stack_slot(&mut self, reg: u8, disp: i8) {
        self.bytes(&[0x44 | (reg & 7) << 3, 0x24, disp as u8]);
    }

    /// `lea rd, [rsp + disp]`.
    pub fn stack_address(&mut self, rd: Reg, disp: i8) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, RAX), 0x8d]);
            a.stack_slot(rd.0, disp);
        });
    }

    /// `mov qword [rsp + disp], imm32`, sign-extended.
    pub fn store_stack_imm32(&mut self, disp: i8, value: u32) {
        self.writing(None, |a| {
            a.bytes(&[0x48, 0xc7]);
            a.stack_slot(0, disp);
            a.bytes(&value.to_le_bytes());
        });
    }

    /// `mov r32, dword [rsp + disp]`, which clears the upper half, for a
    /// register below r8.
    pub fn load32_stack(&mut self, rd: Reg, disp: i8) {
        self.writing(Some(rd), |a| {
            assert!(rd.0 < 8);
            a.bytes(&[0x8b]);
            a.stack_slot(rd.0, disp);
        });
    }

    /// `add rd, rs`.
    pub fn add(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x01, Self::modrm(rs, rd)]);
        });
    }

    /// `sub rd, rs`.
    pub fn sub(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x29, Self::modrm(rs, rd)]);
        });
    }

    /// `op r64, imm`, sign-extended, for `add` as 0, `or` as 1, `and` as 4,
    /// `sub` as 5, `xor` as 6, and `cmp` as 7.
    fn alu_imm(&mut self, op: u8, r: Reg, value: i32) {
        self.writing(
            if op == 7 { None } else { Some(r) },
            |a| match i8::try_from(value) {
                Ok(small) => a.bytes(&[
                    Self::rex_w(RAX, r),
                    0x83,
                    Self::modrm(Reg(op), r),
                    small as u8,
                ]),
                Err(_) => {
                    a.bytes(&[Self::rex_w(RAX, r), 0x81, Self::modrm(Reg(op), r)]);
                    a.bytes(&value.to_le_bytes());
                }
            },
        );
    }

    /// A 64-bit instruction with one operand in memory and a register in the
    /// ModRM reg field: `cmp [place], r` is 0x39, `cmp r, [place]` 0x3b, and
    /// `imul r, [place], imm32` 0x69 with an immediate after it.
    fn with_memory(&mut self, opcode: u8, reg: Reg, place: Place) {
        self.writing(if opcode == 0x69 { Some(reg) } else { None }, |a| {
            a.bytes(&[0x48 | (reg.0 >> 3) << 2 | Self::rex_xb(place), opcode]);
            a.place(reg, place);
        });
    }

    /// `cmp qword [place], imm`, sign-extended.
    fn cmp_memory_imm(&mut self, place: Place, value: i32) {
        self.writing(None, |a| match i8::try_from(value) {
            Ok(small) => {
                a.with_memory(0x83, Reg(7), place);
                a.bytes(&[small as u8]);
            }
            Err(_) => {
                a.with_memory(0x81, Reg(7), place);
                a.bytes(&value.to_le_bytes());
            }
        });
    }

    /// `imul rd, qword [place], imm32`.
    fn imul_memory_imm(&mut self, rd: Reg, place: Place, value: i32) {
        self.writing(Some(rd), |a| {
            a.with_memory(0x69, rd, place);
            a.bytes(&value.to_le_bytes());
        });
    }

    /// `imul rd, rs, imm32`.
    fn imul_imm(&mut self, rd: Reg, rs: Reg, value: i32) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, rs), 0x69, Self::modrm(rd, rs)]);
            a.bytes(&value.to_le_bytes());
        });
    }

    /// `lea rd, [ra + rb]`.
    fn lea_sum(&mut self, rd: Reg, ra: Reg, rb: Reg) {
        self.writing(Some(rd), |a| {
            // rsp cannot be an index, and rbp and r13 as a base need a
            // displacement, so the sum is taken the other way round or with a
            // displacement of 0.
            let (base, index) = if rb.0 == 4 { (rb, ra) } else { (ra, rb) };
            let rex = 0x48 | (rd.0 >> 3) << 2 | (index.0 >> 3) << 1 | base.0 >> 3;
            if base.0 & 7 == 5 {
                a.bytes(&[rex, 0x8d, 0x44 | (rd.0 & 7) << 3, (index.0 & 7) << 3 | 5, 0]);
            } else {
                a.bytes(&[
                    rex,
                    0x8d,
                    0x04 | (rd.0 & 7) << 3,
                    (index.0 & 7) << 3 | (base.0 & 7),
                ]);
            }
        });
    }

    /// `lea rd, [rs + disp32]`.
    fn lea_displaced(&mut self, rd: Reg, rs: Reg, disp: i32) {
        self.writing(Some(rd), |a| {
            a.bytes(&[
                Self::rex_w(rd, rs),
                0x8d,
                0x80 | (rd.0 & 7) << 3 | (rs.0 & 7),
            ]);
            if rs.0 & 7 == 4 {
                a.bytes(&[0x24]);
            }
            a.bytes(&disp.to_le_bytes());
        });
    }

    /// `cmp r64, imm8`, sign-extended.
    pub fn cmp_imm8(&mut self, r: Reg, value: i8) {
        self.writing(None, |a| {
            a.bytes(&[
                Self::rex_w(RAX, r),
                0x83,
                Self::modrm(Reg(7), r),
                value as u8,
            ]);
        });
    }

    /// `cmp r64, imm32`, sign-extended.
    fn cmp_imm32(&mut self, r: Reg, value: u32) {
        self.writing(None, |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0x81, Self::modrm(Reg(7), r)]);
            a.bytes(&value.to_le_bytes());
        });
    }

    /// `cmp dword [r], imm8`, sign-extended, for a register below r8 other
    /// than rsp and rbp.
    pub fn cmp_load32_imm8(&mut self, r: Reg, value: i8) {
        self.writing(None, |a| {
            assert!(r.0 < 8 && r.0 != 4 && r.0 != 5);
            a.bytes(&[0x83, 7 << 3 | r.0, value as u8]);
        });
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
            // rbp and r13 as a base take a displacement of 0.
            Place::Indexed { base, index, scale } => {
                let sib = (scale.trailing_zeros() as u8) << 6 | (index.0 & 7) << 3 | (base.0 & 7);
                match base.0 & 7 {
                    5 => self.bytes(&[0x44 | reg, sib, 0]),
                    _ => self.bytes(&[0x04 | reg, sib]),
                }
            }
        }
    }

    /// The bits of the REX prefix that say the place's base register, and its
    /// index register, are one of the last eight: `B` is 1, `X` is 2.
    fn rex_xb(place: Place) -> u8 {
        match place {
            Place::Frame(_) => 0,
            Place::At(base) => base.0 >> 3,
            Place::Indexed { base, index, .. } => (base.0 >> 3) | ((index.0 >> 3) << 1),
        }
    }

    /// Reads an integer of `width` bytes into the whole of `rd`, extended to
    /// 64 bits as its sign says when `signed` and with zeros when not.
    pub fn load(&mut self, rd: Reg, width: u32, signed: bool, from: Place) {
        self.writing(Some(rd), |a| {
            let rex =
                (u8::from(width == 8 || signed) << 3) | ((rd.0 >> 3) << 2) | (Self::rex_xb(from));
            if rex != 0 {
                a.bytes(&[0x40 | rex]);
            }
            match (width, signed) {
                (1, false) => a.bytes(&[0x0f, 0xb6]),
                (2, false) => a.bytes(&[0x0f, 0xb7]),
                (1, true) => a.bytes(&[0x0f, 0xbe]),
                (2, true) => a.bytes(&[0x0f, 0xbf]),
                (4, true) => a.bytes(&[0x63]),
                (4, false) | (8, _) => a.bytes(&[0x8b]),
                _ => unreachable!("an integer of {width} bytes"),
            }
            a.place(rd, from);
        });
    }

    /// Writes the low `width` bytes of `rs`.
    pub fn store(&mut self, to: Place, rs: Reg, width: u32) {
        self.writing(None, |a| {
            if width == 2 {
                a.bytes(&[0x66]);
            }
            let rex = (u8::from(width == 8) << 3) | ((rs.0 >> 3) << 2) | (Self::rex_xb(to));
            // The low byte of rsi, rdi, rbp, and rsp is reached with a prefix.
            if rex != 0 || (width == 1 && rs.0 >= 4) {
                a.bytes(&[0x40 | rex]);
            }
            a.bytes(&[if width == 1 { 0x88 } else { 0x89 }]);
            a.place(rs, to);
        });
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
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, RAX), 0x8d, (rd.0 & 7) << 3 | 5]);
            a.offset_to(Ref::Label(label));
        });
    }

    /// `jmp label`.
    pub fn jmp(&mut self, label: usize) {
        self.writing(None, |a| {
            a.bytes(&[0xe9]);
            a.offset_to(Ref::Label(label));
        });
    }

    /// `jcc label`, for a condition code such as `Z`.
    pub fn jcc(&mut self, cond: u8, label: usize) {
        self.writing(None, |a| {
            a.bytes(&[0x0f, 0x80 | cond]);
            a.offset_to(Ref::Label(label));
        });
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
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x21, Self::modrm(rs, rd)]);
        });
    }

    /// `or rd, rs`.
    fn or(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x09, Self::modrm(rs, rd)]);
        });
    }

    /// `xor rd, rs`.
    fn xor(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rs, rd), 0x31, Self::modrm(rs, rd)]);
        });
    }

    /// `imul rd, rs`, which keeps the low 64 bits.
    fn imul(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            a.bytes(&[Self::rex_w(rd, rs), 0x0f, 0xaf, Self::modrm(rd, rs)]);
        });
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
        self.writing(Some(r), |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0xd3, Self::modrm(Reg(op), r)]);
        });
    }

    /// `shr r, imm8`.
    fn shr_imm(&mut self, r: Reg, count: u8) {
        self.writing(Some(r), |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0xc1, Self::modrm(Reg(5), r), count]);
        });
    }

    /// `shl r, imm8`.
    fn shl_imm(&mut self, r: Reg, count: u8) {
        self.writing(Some(r), |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0xc1, Self::modrm(Reg(4), r), count]);
        });
    }

    /// `sar r, imm8`.
    fn sar_imm(&mut self, r: Reg, count: u8) {
        self.writing(Some(r), |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0xc1, Self::modrm(Reg(7), r), count]);
        });
    }

    /// An SSE instruction: the prefix that says the size of the float, the REX
    /// prefix when it is needed, `0f`, the opcode, and the operand, with the
    /// register `reg` in the ModRM reg field. `writes` is the register of
    /// integers it changes, if it changes one.
    fn sse(&mut self, writes: Option<Reg>, prefix: u8, w: bool, opcode: u8, reg: Reg, rm: Rm) {
        self.writing(writes, |a| {
            if prefix != 0 {
                a.bytes(&[prefix]);
            }
            let xb = match rm {
                Rm::Reg(r) => r.0 >> 3,
                Rm::Place(place) => Self::rex_xb(place),
            };
            let rex = (u8::from(w) << 3) | ((reg.0 >> 3) << 2) | xb;
            if rex != 0 {
                a.bytes(&[0x40 | rex]);
            }
            a.bytes(&[0x0f, opcode]);
            match rm {
                Rm::Reg(r) => a.bytes(&[Self::modrm(reg, r)]),
                Rm::Place(place) => a.place(reg, place),
            }
        });
    }

    /// A fused multiply and add of FMA, in the form where the register that
    /// holds `c` is also the result: `rd = ra * rb + rd`, or the form of `op` of
    /// it, on 128 bits or on one float. `w` says doubles, `scalar` one float. It
    /// is a VEX instruction of three bytes before its opcode: the extensions of
    /// the registers, the map of opcodes `0f38`, `w` and the second source, 128
    /// bits, and the prefix `66`.
    pub fn fused(&mut self, op: Fused, w: bool, scalar: bool, rd: Reg, ra: Reg, rb: Reg) {
        // The opcode of the form with the 231 order of operands: of packed
        // floats, and of one float one more.
        let opcode = match op {
            Fused::MulAdd => 0xb8,
            Fused::MulSub => 0xba,
            Fused::NegMulAdd => 0xbc,
        } + u8::from(scalar);
        self.writing(None, |a| {
            a.bytes(&[
                0xc4,
                0x40 | (!(rd.0 >> 3) & 1) << 7 | (!(rb.0 >> 3) & 1) << 5 | 0x02,
                u8::from(w) << 7 | (!ra.0 & 0xf) << 3 | 0x01,
                opcode,
                Self::modrm(rd, rb),
            ]);
        });
    }

    /// The prefix of the instruction of scalar floats of `width` bytes: `f3` for
    /// 4 and `f2` for 8.
    fn scalar(width: u32) -> u8 {
        match width {
            4 => 0xf3,
            8 => 0xf2,
            // `movups`, which moves a vector whole, has no prefix.
            _ => 0,
        }
    }

    /// `movaps rd, rs`: copies a float, whole.
    pub fn movaps(&mut self, rd: Reg, rs: Reg) {
        self.sse(None, 0, false, 0x28, rd, Rm::Reg(rs));
    }

    /// `movss` or `movsd rd, [place]`.
    pub fn float_load(&mut self, rd: Reg, width: u32, from: Place) {
        self.sse(None, Self::scalar(width), false, 0x10, rd, Rm::Place(from));
    }

    /// `movss` or `movsd [place], rs`.
    pub fn float_store(&mut self, to: Place, rs: Reg, width: u32) {
        self.sse(None, Self::scalar(width), false, 0x11, rs, Rm::Place(to));
    }

    /// `addss`, `subss`, `mulss`, or `divss` of `width` 4, and the same of
    /// doubles for 8: `rd = rd op rm`. `opcode` is 0x58 for the sum, 0x5c for
    /// the difference, 0x59 for the product, and 0x5e for the quotient.
    pub fn float_arithmetic(&mut self, opcode: u8, width: u32, rd: Reg, rm: Rm) {
        self.sse(None, Self::scalar(width), false, opcode, rd, rm);
    }

    /// `ucomiss` or `ucomisd ra, rm`: sets the zero, parity, and carry flags to
    /// say whether `ra` is above, below, or the same as `rm`, or one is not a
    /// number, when all three are set.
    pub fn float_compare(&mut self, width: u32, ra: Reg, rm: Rm) {
        let prefix = if width == 4 { 0 } else { 0x66 };
        self.sse(None, prefix, false, 0x2e, ra, rm);
    }

    /// `cvtsi2ss` or `cvtsi2sd rd, rs`: the float nearest the signed integer of
    /// 64 bits.
    pub fn float_from_int(&mut self, width: u32, rd: Reg, rs: Reg) {
        self.sse(None, Self::scalar(width), true, 0x2a, rd, Rm::Reg(rs));
    }

    /// `cvttss2si` or `cvttsd2si rd, rm`: the signed integer of 64 bits that the
    /// float is cut to, or the least integer when it does not fit.
    pub fn int_from_float(&mut self, width: u32, rd: Reg, rm: Rm) {
        self.sse(Some(rd), Self::scalar(width), true, 0x2c, rd, rm);
    }

    /// `cvtss2sd rd, rm` for a `from` of 4 bytes and `cvtsd2ss` for 8.
    pub fn float_resize(&mut self, from: u32, rd: Reg, rm: Rm) {
        self.sse(None, Self::scalar(from), false, 0x5a, rd, rm);
    }

    /// `xorps r, r`: zero.
    pub fn float_zero(&mut self, r: Reg) {
        self.sse(None, 0, false, 0x57, r, Rm::Reg(r));
    }

    /// `movd` or `movq xmm, r`: the bits of the register, `width` bytes of them,
    /// with zeros above.
    pub fn float_from_bits(&mut self, rd: Reg, width: u32, rs: Reg) {
        self.sse(None, 0x66, width == 8, 0x6e, rd, Rm::Reg(rs));
    }

    /// `movd` or `movq r, xmm`: the bits of the float, with zeros above.
    pub fn bits_from_float(&mut self, rd: Reg, width: u32, rs: Reg) {
        self.sse(Some(rd), 0x66, width == 8, 0x7e, rs, Rm::Reg(rd));
    }

    /// `btc r, bit`: flips the bit, of a register of `width` bytes.
    pub fn flip_bit(&mut self, r: Reg, width: u32, bit: u8) {
        self.writing(Some(r), |a| {
            let rex = (u8::from(width == 8) << 3) | (r.0 >> 3);
            if rex != 0 {
                a.bytes(&[0x40 | rex]);
            }
            a.bytes(&[0x0f, 0xba, Self::modrm(Reg(7), r), bit]);
        });
    }

    /// `setcc r8`, for `al` or `cl`.
    fn set(&mut self, cond: u8, r: Reg) {
        self.writing(Some(r), |a| {
            a.bytes(&[0x0f, 0x90 | cond, Self::modrm(RAX, r)]);
        });
    }

    /// `and al, cl` when `and` and `or al, cl` when not.
    fn combine_flags(&mut self, and: bool) {
        self.writing(Some(RAX), |a| {
            a.bytes(&[if and { 0x20 } else { 0x08 }, Self::modrm(RCX, RAX)]);
        });
    }

    /// `movzx eax, al`.
    fn extend_flag(&mut self) {
        self.writing(Some(RAX), |a| {
            a.bytes(&[0x0f, 0xb6, 0xc0]);
        });
    }

    /// `not r`.
    fn not(&mut self, r: Reg) {
        self.writing(Some(r), |a| {
            a.bytes(&[Self::rex_w(RAX, r), 0xf7, Self::modrm(Reg(2), r)]);
        });
    }

    /// `setcc al` then `movzx eax, al`: rax is 1 when the condition holds
    /// and 0 when not.
    fn truth(&mut self, cond: u8) {
        self.writing(Some(RAX), |a| {
            a.bytes(&[0x0f, 0x90 | cond, 0xc0, 0x0f, 0xb6, 0xc0]);
        });
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
/// The last operation overflowed, as signed numbers.
const O: u8 = 0x0;
const NO: u8 = 0x1;
/// Above, and not below or the same, as unsigned numbers or as floats.
const A: u8 = 0x7;
/// Not below, which is above or the same, for unsigned numbers and floats.
const AE: u8 = 0x3;
const BE: u8 = 0x6;
const P: u8 = 0xa;
const NP: u8 = 0xb;
const NS: u8 = 0x9;
const G: u8 = 0xf;

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
pub fn code<S: System>(program: &Program) -> super::Laid<'_, Asm> {
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
    let lines = level::check::<S>(&mut asm, fail);
    if takes {
        S::arguments_by_call(&mut asm);
    }
    asm.call(functions[program.main]);
    S::exit(&mut asm, 0);
    asm.bind(fail);
    S::exit(&mut asm, 1);
    let mut frames = Vec::new();
    for (f, &label) in program.functions.iter().zip(&functions) {
        asm.bind(label);
        frames.push(function::<S>(
            &mut asm, program, f, &texts, &functions, fail,
        ));
    }
    let offsets: Vec<usize> = functions.iter().map(|&l| asm.label_offset(l)).collect();
    let pieces: Vec<Written> = (program.functions.iter())
        .map(|f| Written::of(&f.name))
        .collect();
    let parts = super::parts(&offsets, &frames, asm.code_len(), &asm.places, &pieces);
    let data = texts
        .into_iter()
        .zip(program.data.iter().map(Vec::as_slice))
        .chain(lines)
        .collect();
    (asm, data, parts)
}

/// Where a value is held, in the code.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Loc {
    Register(Reg),
    /// A register that holds a float, by its number among the 16.
    Xmm(Reg),
    /// `[rsp + disp]`.
    Frame(u32),
    /// A number that a constant gives, which is in no place: each
    /// instruction that reads it holds it.
    Imm(u64),
}

/// Where the values of a call go, in order, of the types: in the registers that
/// carry them, the k-th of its kind for the k-th value of the kind, and in the
/// places in the frame, which start at `base`.
fn passing(places: &[Passed], types: &[Type], base: u32) -> Vec<Loc> {
    (places.iter().zip(types))
        .map(|(place, ty)| match (place, ty) {
            (Passed::Register(k), Type::Float(_) | Type::Vector(_)) => Loc::Xmm(FLOATS[*k]),
            (Passed::Register(k), _) => Loc::Register(HOMES[*k]),
            (Passed::Frame(at), _) => Loc::Frame(base + at),
        })
        .collect()
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
    /// Which instructions are folded into the store that reads them.
    folded: Vec<Vec<Folded>>,
    /// Which blocks end with a choice between two jumps on a comparison that
    /// is the last instruction of the block and that nothing else reads. That
    /// comparison is not made into a value: the branch tests it.
    branches_on_compare: Vec<bool>,
    before_choice: Vec<Vec<bool>>,
    /// The number each value that a constant integer gives is.
    constants: Vec<Option<u64>>,
    /// What rax, rcx, and rdx hold of the frame: the place, the bytes of it,
    /// and `Asm::version` of the register when it was put there. A register
    /// is still holding it while the version is the same.
    scratch: std::cell::RefCell<[Option<(u32, u32, u32)>; 3]>,
    system: std::marker::PhantomData<S>,
}

fn function<S: System>(
    asm: &mut Asm,
    program: &Program,
    f: &Function,
    texts: &[usize],
    functions: &[usize],
    fail: usize,
) -> u32 {
    let f = &crate::scalar::rematerialized(f);
    let constants = super::branch::constants(f);
    let held = held(f, &constants);
    let allocation = alloc::allocate_kinds(f, KINDS, &held);
    let passed = [REGISTERS.passed, FLOAT_REGISTERS.passed];
    let below = S::RESERVED + frame::outgoing(program, f, passed);
    let saved_from = round_up(below + allocation.frame, 8);
    // The call pushed the return address, so the frame takes 8 bytes more
    // than a multiple of 16.
    let total = round_up(saved_from + 8 * allocation.saved.len() as u32, 16) + 8;
    let area = call_area(f, passed);
    let in_caller = |places: &[Passed], types: &[Type]| -> Vec<Loc> {
        passing(places, types, total + 8 + S::RESERVED)
    };
    let params: Vec<Type> = f.blocks[0].params.iter().map(|v| f.types[v.0]).collect();
    let arrive = in_caller(&area.params, &params);
    let leave = in_caller(&area.results, &f.results);
    let at = (allocation.homes.iter().enumerate())
        .map(|(v, home)| match home {
            Home::Register(r) if *r < HOMES.len() => Loc::Register(HOMES[*r]),
            Home::Register(r) => Loc::Xmm(FLOATS[*r - HOMES.len()]),
            Home::Frame(place) => Loc::Frame(below + place),
            Home::Held => Loc::Imm(constants[v].expect("a value held is a constant")),
        })
        .collect();
    let saved = (allocation.saved.iter().enumerate())
        .map(|(i, r)| (HOMES[*r], saved_from + 8 * i as u32))
        .collect();
    let labels = f.blocks.iter().map(|_| asm.label()).collect();
    let branches_on_compare = super::branch::on_compare(f);
    let before_choice = super::branch::before_choice(f);
    let folded = super::indexed::stores(f, &constants, &held, |_, _| true);
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
        folded,
        branches_on_compare,
        before_choice,
        constants,
        scratch: std::cell::RefCell::new([None; 3]),
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
        for (i, instruction) in block.instructions[..made].iter().enumerate() {
            match w.folded[b][i] {
                Folded::Skipped => continue,
                Folded::Store { base, index, scale } => {
                    w.indexed_store(asm, instruction, base, index, scale);
                    continue;
                }
                Folded::No => {}
            }
            if w.before_choice[b][i] {
                continue;
            }
            asm.note_place(instruction.at);
            match i.checked_sub(1).filter(|j| w.before_choice[b][*j]) {
                Some(j) => w.choice(asm, &block.instructions[j], instruction),
                None => w.instruction(asm, &instruction.results, &instruction.op),
            }
        }
        w.end(asm, b);
    }
    total
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
        // The count of a shift is a number of bits below 64.
        Op::Binary {
            op: Binary::ShiftLeft | Binary::ShiftRightU | Binary::ShiftRightS,
            b,
            ..
        } => return constants[b.0].filter(|n| *n < 64).map(|n| n as i32),
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
            (Loc::Register(to), Loc::Frame(from)) => self.load_frame(asm, to, width, from),
            (Loc::Frame(to), Loc::Register(from)) => self.store_frame(asm, to, from, width, true),
            (Loc::Frame(to), Loc::Frame(from)) if width == 16 => {
                self.copy_vector_frame(asm, to, from)
            }
            (Loc::Frame(to), Loc::Frame(from)) => {
                self.load_frame(asm, RAX, width, from);
                self.store_frame(asm, to, RAX, width, true);
            }
            (Loc::Register(to), Loc::Imm(n)) => asm.mov_imm(to, n),
            (Loc::Frame(to), Loc::Imm(n)) => self.store_imm(asm, Place::Frame(to), n, width, RAX),
            (Loc::Imm(_), _) => unreachable!("a number is not a place to copy to"),
            (Loc::Xmm(to), Loc::Xmm(from)) => {
                if to != from {
                    asm.movaps(to, from);
                }
            }
            (Loc::Xmm(to), Loc::Frame(from)) => asm.float_load(to, width, Place::Frame(from)),
            (Loc::Frame(to), Loc::Xmm(from)) if width == 16 => {
                self.store_vector_frame(asm, to, from)
            }
            (Loc::Frame(to), Loc::Xmm(from)) => self.store_float_frame(asm, to, from, width),
            // Through r11, which holds what breaks a circle of copies. The
            // circle may hold floats of both sizes, so the register is copied
            // whole.
            (Loc::Register(_), Loc::Xmm(_)) | (Loc::Xmm(_), Loc::Register(_)) if width == 16 => {
                unreachable!("a vector is never in a register of integers")
            }
            (Loc::Register(to), Loc::Xmm(from)) => asm.bits_from_float(to, 8, from),
            (Loc::Xmm(to), Loc::Register(from)) => asm.float_from_bits(to, 8, from),
            (Loc::Xmm(_), Loc::Imm(_)) => unreachable!("a float is not a number held"),
        }
    }

    /// Reads `width` bytes of the frame into the register, with zeros above,
    /// unless the register is rax, rcx, or rdx and still holds them.
    fn load_frame(&self, asm: &mut Asm, rd: Reg, width: u32, at: u32) {
        let i = usize::from(rd.0);
        if i < 3 && self.scratch.borrow()[i] == Some((at, width, asm.version(rd))) {
            return;
        }
        asm.load(rd, width, false, Place::Frame(at));
        if i < 3 {
            self.scratch.borrow_mut()[i] = Some((at, width, asm.version(rd)));
        }
    }

    /// Writes the low `width` bytes of the register to the frame. The
    /// register holds them with zeros above when `extended`, and then it, if
    /// it is rax, rcx, or rdx, holds what is in the place.
    fn store_frame(&self, asm: &mut Asm, at: u32, r: Reg, width: u32, extended: bool) {
        asm.store(Place::Frame(at), r, width);
        let mut scratch = self.scratch.borrow_mut();
        for held in scratch.iter_mut() {
            if held.is_some_and(|(place, _, _)| place == at) {
                *held = None;
            }
        }
        let i = usize::from(r.0);
        if i < 3 && (extended || width == 8) {
            scratch[i] = Some((at, width, asm.version(r)));
        }
    }

    /// Writes the number, `width` bytes of it, to the place, in the one
    /// instruction when it holds the number, and through `tmp` when not, which
    /// is not a register the place is based on.
    fn store_imm(&self, asm: &mut Asm, to: Place, n: u64, width: u32, tmp: Reg) {
        if let Place::Frame(at) = to {
            for held in self.scratch.borrow_mut().iter_mut() {
                if held.is_some_and(|(place, _, _)| place == at) {
                    *held = None;
                }
            }
        }
        if !asm.store_imm(to, n, width) {
            asm.mov_imm(tmp, n);
            asm.store(to, tmp, width);
        }
    }

    /// Does the moves as if all were copied at once. A place that another
    /// move is still to read is held in r11 first, when they go in a circle,
    /// and a vector in xmm15, which no copy changes.
    fn run(&self, asm: &mut Asm, moves: &[Move<Loc>]) {
        let aside = |width: u32| {
            if width == 16 {
                Loc::Xmm(XMM15)
            } else {
                Loc::Register(R11)
            }
        };
        for step in moves::sequence(moves) {
            match step {
                Step::Copy { to, from, width } => self.copy(asm, to, from, width),
                Step::Save { from, width } => self.copy(asm, aside(width), from, width),
                Step::Restore { to, width } => self.copy(asm, to, aside(width), width),
            }
        }
    }

    /// A register that holds the value, with zeros above it: its own, or rax,
    /// rcx, or rdx, the one `tmp` says, with the value read into it.
    fn read(&self, asm: &mut Asm, tmp: Reg, v: Value) -> Reg {
        match self.at[v.0] {
            Loc::Register(r) => r,
            Loc::Frame(at) => {
                self.load_frame(asm, tmp, self.width(v), at);
                tmp
            }
            Loc::Imm(n) => {
                asm.mov_imm(tmp, n);
                tmp
            }
            Loc::Xmm(_) => unreachable!("a float is not read as an integer"),
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
            Loc::Xmm(_) => unreachable!("a float is not read as an integer"),
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
            Loc::Frame(at) => self.store_frame(asm, at, r, width, extended),
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
            Loc::Xmm(_) => unreachable!("an integer is not given in a register of floats"),
        }
    }

    /// Writes the float of `width` bytes to the frame, and forgets what rax,
    /// rcx, and rdx held of the place.
    fn store_float_frame(&self, asm: &mut Asm, at: u32, r: Reg, width: u32) {
        asm.float_store(Place::Frame(at), r, width);
        for held in self.scratch.borrow_mut().iter_mut() {
            if held.is_some_and(|(place, _, _)| place == at) {
                *held = None;
            }
        }
    }

    /// The operand an instruction of floats reads for the value: its register,
    /// or its place in the frame.
    fn float_operand(&self, v: Value) -> Rm {
        match self.at[v.0] {
            Loc::Xmm(r) => Rm::Reg(r),
            Loc::Frame(at) => Rm::Place(Place::Frame(at)),
            _ => unreachable!("a float is in a register of floats or in the frame"),
        }
    }

    /// A register of floats that holds the value: its own, or `tmp`, with the
    /// value read into it.
    fn float_read(&self, asm: &mut Asm, tmp: Reg, v: Value) -> Reg {
        match self.float_operand(v) {
            Rm::Reg(r) => r,
            Rm::Place(place) => {
                asm.float_load(tmp, self.width(v), place);
                tmp
            }
        }
    }

    /// The register of floats to work a result out in: its own, unless that is
    /// one of those `avoid`, and then xmm15.
    fn float_target(&self, v: Value, avoid: &[Reg]) -> Reg {
        match self.at[v.0] {
            Loc::Xmm(r) if !avoid.contains(&r) => r,
            _ => XMM15,
        }
    }

    /// Puts the float, which is in `r`, in the home of the value.
    fn float_finish(&self, asm: &mut Asm, v: Value, r: Reg) {
        match self.at[v.0] {
            Loc::Xmm(h) => {
                if h != r {
                    asm.movaps(h, r);
                }
            }
            Loc::Frame(at) => self.store_float_frame(asm, at, r, self.width(v)),
            _ => unreachable!("a float is in a register of floats or in the frame"),
        }
    }

    /// Reads the bits of the float into `rd`, with zeros above them.
    fn float_bits_into(&self, asm: &mut Asm, rd: Reg, v: Value) {
        match self.at[v.0] {
            Loc::Xmm(r) => asm.bits_from_float(rd, self.width(v), r),
            Loc::Frame(at) => self.load_frame(asm, rd, self.width(v), at),
            _ => unreachable!("a float is in a register of floats or in the frame"),
        }
    }

    /// Puts the bits of a float, which are in `r` with zeros above, in the home
    /// of the value.
    fn float_finish_bits(&self, asm: &mut Asm, v: Value, r: Reg) {
        match self.at[v.0] {
            Loc::Xmm(h) => asm.float_from_bits(h, self.width(v), r),
            Loc::Frame(at) => self.store_frame(asm, at, r, self.width(v), true),
            _ => unreachable!("a float is in a register of floats or in the frame"),
        }
    }

    /// Writes the float to the place, through rcx when it is in the frame.
    fn float_store(&self, asm: &mut Asm, to: Place, value: Value) {
        let width = self.width(value);
        match self.at[value.0] {
            Loc::Xmm(r) => asm.float_store(to, r, width),
            Loc::Frame(at) => {
                self.load_frame(asm, RCX, width, at);
                asm.store(to, RCX, width);
            }
            _ => unreachable!("a float is in a register of floats or in the frame"),
        }
    }

    /// `a op b` of floats.
    fn float_binary(&self, asm: &mut Asm, result: Value, op: FloatBinary, a: Value, b: Value) {
        let width = self.width(result);
        let opcode = match op {
            FloatBinary::Add => 0x58,
            FloatBinary::Sub => 0x5c,
            FloatBinary::Mul => 0x59,
            FloatBinary::Div => 0x5e,
        };
        // A sum and a product give the same bits whichever way round, so the
        // one that is in the register of the result is the first.
        let (a, b) = match (op, self.at[a.0], self.at[b.0], self.at[result.0]) {
            (FloatBinary::Add | FloatBinary::Mul, from, Loc::Xmm(x), Loc::Xmm(h))
                if x == h && from != Loc::Xmm(h) =>
            {
                (b, a)
            }
            _ => (a, b),
        };
        let second = self.float_operand(b);
        let avoid = match second {
            Rm::Reg(r) if self.at[a.0] != Loc::Xmm(r) => vec![r],
            _ => Vec::new(),
        };
        let t = self.float_target(result, &avoid);
        match self.float_operand(a) {
            Rm::Reg(r) => {
                if r != t {
                    asm.movaps(t, r);
                }
            }
            Rm::Place(place) => asm.float_load(t, width, place),
        }
        asm.float_arithmetic(opcode, width, t, second);
        self.float_finish(asm, result, t);
    }

    /// `a * b + c`, or the form of `op` of them, of floats, in one instruction
    /// that rounds once. The result is worked out in the register that holds
    /// `c` is copied to, which is neither of the registers `a` and `b` are read
    /// from.
    fn float_fused(&self, asm: &mut Asm, result: Value, op: Fused, a: Value, b: Value, c: Value) {
        let width = self.width(result);
        let ra = self.float_read(asm, XMM13, a);
        let rb = self.float_read(asm, XMM14, b);
        let t = self.float_target(result, &[ra, rb]);
        match self.float_operand(c) {
            Rm::Reg(r) => {
                if r != t {
                    asm.movaps(t, r);
                }
            }
            Rm::Place(place) => asm.float_load(t, width, place),
        }
        asm.fused(op, width == 8, true, t, ra, rb);
        self.float_finish(asm, result, t);
    }

    /// The comparison of two floats, as a value: 1 when it holds, and 0 when
    /// not, which is 0 for every comparison but `!=` when a float is not a
    /// number.
    fn float_comparison(&self, asm: &mut Asm, result: Value, op: FloatCompare, a: Value, b: Value) {
        let width = self.width(a);
        // The flags say whether the first is above the second, and ignore a
        // pair that holds a number that is not one, so that `<` and `<=` read
        // the pair the other way round.
        let (first, second) = match op {
            FloatCompare::Less | FloatCompare::LessEqual => (b, a),
            _ => (a, b),
        };
        let first = self.float_read(asm, XMM14, first);
        asm.float_compare(width, first, self.float_operand(second));
        match op {
            FloatCompare::Equal => {
                asm.set(Z, RAX);
                asm.set(NP, RCX);
                asm.combine_flags(true);
            }
            FloatCompare::NotEqual => {
                asm.set(NE, RAX);
                asm.set(P, RCX);
                asm.combine_flags(false);
            }
            FloatCompare::Less | FloatCompare::Greater => asm.set(A, RAX),
            FloatCompare::LessEqual | FloatCompare::GreaterEqual => asm.set(AE, RAX),
        }
        asm.extend_flag();
        self.finish(asm, result, RAX, true);
    }

    /// The float with its sign bit flipped.
    fn float_negate(&self, asm: &mut Asm, result: Value, value: Value) {
        let width = self.width(result);
        self.float_bits_into(asm, RAX, value);
        asm.flip_bit(RAX, width, (8 * width - 1) as u8);
        self.float_finish_bits(asm, result, RAX);
    }

    /// The float nearest the integer, which is read as signed or as unsigned.
    fn float_from_int(&self, asm: &mut Asm, result: Value, signed: bool, value: Value) {
        let width = self.width(result);
        let t = self.float_target(result, &[]);
        if signed {
            self.read_signed_into(asm, RAX, value);
        } else {
            self.read_into(asm, RAX, value);
        }
        if signed || self.width(value) < 8 {
            asm.float_from_int(width, t, RAX);
        } else {
            // An integer of 64 bits with its top bit set does not fit what
            // the instruction reads. It is halved, with the bit that was lost
            // kept in the lowest, so that the one rounding is the right one,
            // and the float doubled.
            let (big, done) = (asm.label(), asm.label());
            asm.test(RAX);
            asm.jcc(S, big);
            asm.float_from_int(width, t, RAX);
            asm.jmp(done);
            asm.bind(big);
            asm.mov(RCX, RAX);
            asm.shr_imm(RCX, 1);
            asm.alu_imm(4, RAX, 1);
            asm.or(RCX, RAX);
            asm.float_from_int(width, t, RCX);
            asm.float_arithmetic(0x58, width, t, Rm::Reg(t));
            asm.bind(done);
        }
        self.float_finish(asm, result, t);
    }

    /// The integer the float is cut to, which saturates, and is 0 for a float
    /// that is not a number. Worked out in 64 bits from the float as a double,
    /// and then held to the size of the result.
    fn int_from_float(&self, asm: &mut Asm, result: Value, signed: bool, value: Value) {
        let to = 8 * self.width(result);
        // The float, as a double, in xmm15, which the work may change.
        match self.float_operand(value) {
            Rm::Reg(r) if self.width(value) == 8 => asm.movaps(XMM15, r),
            operand if self.width(value) == 8 => asm.float_load(
                XMM15,
                8,
                match operand {
                    Rm::Place(place) => place,
                    Rm::Reg(_) => unreachable!(),
                },
            ),
            operand => asm.float_resize(4, XMM15, operand),
        }
        let done = asm.label();
        let not_a_number = asm.label();
        if signed {
            asm.int_from_float(8, RAX, Rm::Reg(XMM15));
            // The least integer is what the instruction gives for a float that
            // does not fit and for one that is not a number, and also for the
            // float that is that integer: only the least integer fails to
            // overflow when 1 is taken from it.
            asm.alu_imm(7, RAX, 1);
            asm.jcc(NO, done);
            asm.float_zero(XMM14);
            asm.float_compare(8, XMM15, Rm::Reg(XMM14));
            asm.jcc(P, not_a_number);
            // Not above 0: it is the least integer, or below it.
            asm.jcc(BE, done);
            asm.not(RAX);
            asm.jmp(done);
            asm.bind(not_a_number);
            asm.xor(RAX, RAX);
            asm.bind(done);
            if to < 64 {
                let greatest = (1i64 << (to - 1)) - 1;
                let least = -greatest - 1;
                asm.mov_imm(RCX, greatest as u64);
                asm.cmp(RAX, RCX);
                asm.cmov(G, RAX, RCX);
                asm.mov_imm(RCX, least as u64);
                asm.cmp(RAX, RCX);
                asm.cmov(L, RAX, RCX);
            }
        } else {
            let (big, saturates) = (asm.label(), asm.label());
            // 2^63, which the instruction does not reach.
            asm.mov_imm(RAX, 0x43e0_0000_0000_0000);
            asm.float_from_bits(XMM14, 8, RAX);
            asm.float_compare(8, XMM15, Rm::Reg(XMM14));
            asm.jcc(P, not_a_number);
            asm.jcc(AE, big);
            asm.int_from_float(8, RAX, Rm::Reg(XMM15));
            asm.test(RAX);
            asm.jcc(NS, done);
            asm.xor(RAX, RAX);
            asm.jmp(done);
            asm.bind(big);
            asm.float_arithmetic(0x5c, 8, XMM15, Rm::Reg(XMM14));
            asm.int_from_float(8, RAX, Rm::Reg(XMM15));
            asm.alu_imm(7, RAX, 1);
            asm.jcc(O, saturates);
            asm.flip_bit(RAX, 8, 63);
            asm.jmp(done);
            asm.bind(saturates);
            asm.mov_imm(RAX, u64::MAX);
            asm.jmp(done);
            asm.bind(not_a_number);
            asm.xor(RAX, RAX);
            asm.bind(done);
            if to < 64 {
                asm.mov_imm(RCX, (1u64 << to) - 1);
                asm.cmp(RAX, RCX);
                asm.cmova(RAX, RCX);
            }
        }
        self.finish(asm, result, RAX, to == 64);
    }

    /// Makes the flags say what the comparison, or the operation that tells
    /// whether its answer fitted, found, and gives the condition under which
    /// the value it makes is 1.
    fn flags(&self, asm: &mut Asm, i: &crate::scalar::Instruction) -> u8 {
        match &i.op {
            Op::Compare { op, a, b } => self.compare(asm, op, *a, *b),
            Op::Overflows { op, signed, a, b } => {
                self.overflow(asm, i.results[0], *op, *signed, *a, *b)
            }
            _ => unreachable!("only a comparison and an overflow make a flag"),
        }
    }

    /// A choice on a comparison, or on whether an answer fitted, that nothing
    /// else reads: compares, and takes `then` on the condition the flags say.
    /// Moving a value does not change what the flags found.
    fn choice(
        &self,
        asm: &mut Asm,
        compare: &crate::scalar::Instruction,
        choose: &crate::scalar::Instruction,
    ) {
        let Op::Choose {
            then, otherwise, ..
        } = &choose.op
        else {
            unreachable!()
        };
        let cond = self.flags(asm, compare);
        if let (Loc::Imm(n), Loc::Register(other)) = (self.at[then.0], self.at[otherwise.0])
            && self.at[choose.results[0].0] != Loc::Register(other)
        {
            // The number goes to the result, and the other value is taken
            // when the condition does not hold.
            let t = self.target(choose.results[0], &[other]);
            asm.mov_imm(t, n);
            asm.cmov(cond ^ 1, t, other);
            return self.finish(asm, choose.results[0], t, true);
        }
        let then = self.read(asm, RCX, *then);
        let t = self.target(choose.results[0], &[then]);
        self.read_into(asm, t, *otherwise);
        asm.cmov(cond, t, then);
        self.finish(asm, choose.results[0], t, true);
    }

    /// A store whose address is a base and an index times a scale, as one
    /// instruction. The two instructions that made the address are not made.
    fn indexed_store(
        &self,
        asm: &mut Asm,
        store: &crate::scalar::Instruction,
        base: Value,
        index: Value,
        scale: u8,
    ) {
        let Op::Store { ty, value, .. } = &store.op else {
            unreachable!("a folded instruction is a store");
        };
        let base = self.read(asm, RAX, base);
        let index = self.read(asm, RDX, index);
        let place = Place::Indexed { base, index, scale };
        if matches!(ty, Type::Float(_)) {
            return self.float_store(asm, place, *value);
        }
        if matches!(ty, Type::Vector(_)) {
            return self.vector_write(asm, place, *value);
        }
        match self.at[value.0] {
            Loc::Imm(n) => self.store_imm(asm, place, n, size(*ty), RCX),
            _ => {
                let value = self.read(asm, RCX, *value);
                asm.store(place, value, size(*ty));
            }
        }
    }

    fn instruction(&self, asm: &mut Asm, results: &[Value], operation: &Op) {
        match operation {
            Op::FloatBinary { op, a, b } => self.float_binary(asm, results[0], *op, *a, *b),
            Op::FloatFused { op, a, b, c } => self.float_fused(asm, results[0], *op, *a, *b, *c),
            Op::VectorFloatFused { op, a, b, c } => {
                self.vector_float_fused(asm, results[0], *op, *a, *b, *c)
            }
            Op::FloatNegate(value) => self.float_negate(asm, results[0], *value),
            Op::FloatCompare { op, a, b } => self.float_comparison(asm, results[0], *op, *a, *b),
            Op::FloatFromInt { signed, value } => {
                self.float_from_int(asm, results[0], *signed, *value)
            }
            Op::IntFromFloat { signed, value } => {
                self.int_from_float(asm, results[0], *signed, *value)
            }
            Op::FloatResize(value) => {
                let t = self.float_target(results[0], &[]);
                asm.float_resize(self.width(*value), t, self.float_operand(*value));
                self.float_finish(asm, results[0], t);
            }
            Op::Constant(Constant::Float(n)) => {
                let result = results[0];
                let width = self.width(result);
                let n = if width == 8 { *n } else { n & 0xffff_ffff };
                match self.at[result.0] {
                    Loc::Xmm(h) if n == 0 => asm.float_zero(h),
                    Loc::Xmm(h) => {
                        asm.mov_imm(RAX, n);
                        asm.float_from_bits(h, width, RAX);
                    }
                    Loc::Frame(at) => self.store_imm(asm, Place::Frame(at), n, width, RAX),
                    _ => unreachable!("a float is in a register of floats or in the frame"),
                }
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } if matches!(self.f.types[results[0].0], Type::Float(_)) => {
                // The bits of both are chosen between in registers of integers.
                let on = self.read(asm, RDX, *on);
                self.float_bits_into(asm, RAX, *then);
                self.float_bits_into(asm, RCX, *otherwise);
                asm.test32(on);
                asm.cmov(Z, RAX, RCX);
                self.float_finish_bits(asm, results[0], RAX);
            }
            Op::Load {
                ty: Type::Float(_),
                address,
            } => {
                let address = self.read(asm, RAX, *address);
                let t = self.float_target(results[0], &[]);
                asm.float_load(t, self.width(results[0]), Place::At(address));
                self.float_finish(asm, results[0], t);
            }
            Op::Store {
                ty: Type::Float(_),
                address,
                value,
            } => {
                let address = self.read(asm, RAX, *address);
                self.float_store(asm, Place::At(address), *value);
            }
            Op::Load {
                ty: Type::Vector(_),
                address,
            } => {
                let address = self.read(asm, RAX, *address);
                self.vector_load(asm, results[0], address);
            }
            Op::Store {
                ty: Type::Vector(_),
                address,
                value,
            } => {
                let address = self.read(asm, RAX, *address);
                self.vector_write(asm, Place::At(address), *value);
            }
            Op::Choose {
                on,
                then,
                otherwise,
            } if matches!(self.f.types[results[0].0], Type::Vector(_)) => {
                self.vector_choose_by_number(asm, results[0], *on, *then, *otherwise)
            }
            Op::Constant(Constant::Vector(..)) => {
                let Op::Constant(c) = operation else {
                    unreachable!()
                };
                self.vector_constant(asm, results[0], c.vector_bits().expect("a vector"));
            }
            Op::VectorBinary { op, a, b } => self.vector_binary(asm, results[0], *op, *a, *b),
            Op::VectorFloatBinary { op, a, b } => {
                self.vector_float_binary(asm, results[0], *op, *a, *b)
            }
            Op::VectorCompare { op, signed, a, b } => {
                self.vector_compare(asm, results[0], *op, *signed, *a, *b)
            }
            Op::VectorFloatCompare { op, a, b } => {
                self.vector_float_compare(asm, results[0], *op, *a, *b)
            }
            Op::VectorChoose {
                mask,
                then,
                otherwise,
            } => self.vector_choose(asm, results[0], *mask, *then, *otherwise),
            Op::VectorSplat(value) => self.vector_splat(asm, results[0], *value),
            Op::VectorExtract { vector, lane } => {
                self.vector_extract(asm, results[0], *vector, *lane)
            }
            Op::VectorInsert {
                vector,
                lane,
                value,
            } => self.vector_insert(asm, results[0], *vector, *lane, *value),
            Op::VectorSum(vector) => self.vector_sum(asm, results[0], *vector),
            Op::VectorFirstSet(mask) => self.vector_first_set(asm, results[0], *mask),
            Op::Constant(_) if matches!(self.at[results[0].0], Loc::Imm(_)) => {}
            Op::Constant(c) => {
                let result = results[0];
                let t = self.target(result, &[]);
                match c {
                    Constant::Float(_) => unreachable!("x86-64 does not build floats yet"),
                    Constant::Vector(..) => unreachable!("a vector constant is made as one"),
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
            Op::Overflows { op, signed, a, b } => {
                let cond = self.overflow(asm, results[0], *op, *signed, *a, *b);
                asm.truth(cond);
                self.finish(asm, results[1], RAX, true);
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
                asm.cmov(NE, t, then);
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
                let callee = &self.program.functions[*function];
                let passed = [REGISTERS.passed, FLOAT_REGISTERS.passed];
                let area = call_area(callee, passed);
                let at_bottom =
                    |places: &[Passed], types: &[Type]| passing(places, types, S::RESERVED);
                let types: Vec<Type> = (callee.blocks[0].params.iter())
                    .map(|v| callee.types[v.0])
                    .collect();
                let params = at_bottom(&area.params, &types);
                let moves: Vec<Move<Loc>> = (args.iter().zip(&params))
                    .map(|(arg, to)| Move {
                        to: *to,
                        from: self.at[arg.0],
                        width: self.width(*arg),
                    })
                    .collect();
                self.run(asm, &moves);
                asm.call(self.functions[*function]);
                let moves: Vec<Move<Loc>> = (results
                    .iter()
                    .zip(at_bottom(&area.results, &callee.results)))
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

    /// Works out `op` of `a` and `b`, read as signed or as unsigned numbers,
    /// puts the result where `result` lives, and leaves the flags to say
    /// whether it did not fit: the condition that gives is the one returned.
    /// A number of 64 bits is worked out by the operation whose flags say it.
    /// A narrower number is widened, worked out in 64 bits, where it cannot
    /// overflow, and looked at: for unsigned numbers, whether any bit is left
    /// above the size, and for signed numbers, whether the answer is what its
    /// low bytes, extended, are.
    fn overflow(
        &self,
        asm: &mut Asm,
        result: Value,
        op: Binary,
        signed: bool,
        a: Value,
        b: Value,
    ) -> u8 {
        let width = self.width(result);
        if width == 8 {
            if op == Binary::Mul && !signed {
                self.read_into(asm, RAX, a);
                let b = self.read(asm, RCX, b);
                asm.group(4, b);
                self.finish(asm, result, RAX, true);
                return B;
            }
            // An operation that gives the same either way round takes the
            // register the result is in as its first value.
            let swap = op != Binary::Sub
                && matches!(self.at[b.0], Loc::Register(_))
                && self.at[b.0] == self.at[result.0];
            let (a, b) = if swap { (b, a) } else { (a, b) };
            let b = self.read(asm, RCX, b);
            let t = self.target(result, &[b]);
            self.read_into(asm, t, a);
            match op {
                Binary::Add => asm.add(t, b),
                Binary::Sub => asm.sub(t, b),
                _ => asm.imul(t, b),
            }
            self.finish(asm, result, t, true);
            return if signed || op == Binary::Mul { O } else { B };
        }
        if signed {
            self.read_signed_into(asm, RAX, a);
            self.read_signed_into(asm, RCX, b);
        } else {
            self.read_into(asm, RAX, a);
            self.read_into(asm, RCX, b);
        }
        match op {
            Binary::Add => asm.add(RAX, RCX),
            Binary::Sub => asm.sub(RAX, RCX),
            _ => asm.imul(RAX, RCX),
        }
        if signed {
            asm.movsx(RDX, RAX, width);
            asm.cmp(RDX, RAX);
        } else {
            asm.mov(RDX, RAX);
            asm.shr_imm(RDX, (width * 8) as u8);
        }
        self.finish(asm, result, RAX, false);
        NE
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
            // A count that is a number written out is held by the shift.
            Binary::ShiftLeft | Binary::ShiftRightU | Binary::ShiftRightS
                if immediate.is_some() =>
            {
                let count = immediate.unwrap() as u8;
                let t = self.target(result, &[]);
                if matches!(op, Binary::ShiftRightS) {
                    self.read_signed_into(asm, t, a);
                } else {
                    self.read_into(asm, t, a);
                }
                match op {
                    Binary::ShiftLeft => asm.shl_imm(t, count),
                    Binary::ShiftRightU => asm.shr_imm(t, count),
                    _ => asm.sar_imm(t, count),
                }
                self.finish(asm, result, t, matches!(op, Binary::ShiftRightU));
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
                // An operation that gives the same either way round takes
                // the register the result is in as its first value, so that
                // the result needs no move.
                let swap = !matches!(op, Binary::Sub)
                    && matches!(self.at[b.0], Loc::Register(_))
                    && self.at[b.0] == self.at[result.0];
                let (a, b) = if swap { (b, a) } else { (a, b) };
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
                debug_assert_eq!(compare.results.last(), Some(on));
                let cond = self.flags(asm, compare);
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
