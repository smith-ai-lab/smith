//! Vectors on x86-64, built with SSE2, which every x86-64 chip has: nothing
//! here needs SSSE3, SSE4, or AVX. A vector is held in a register of floats,
//! `xmm0` to `xmm12`, or in the frame, where it takes 16 bytes. It is read and
//! written with `movups`, which asks for no alignment, so a place in the frame
//! or in the program's memory is at any address, and no instruction here reads
//! a vector from memory but that one: every other instruction reads registers.
//!
//! `xmm13`, `xmm14`, and `xmm15` are kept for the instructions that work in
//! more than one step. A value that is in the frame is read into one of them
//! first, and a result that is worked out in them is put in its home last, so
//! that no register that holds a value is changed before every value is read.
//! The instructions that SSE2 lacks are made of those it has:
//!
//! - a product of lanes of 16 bits is `pmullw`, of 32 bits two `pmuludq`s whose
//!   halves are put together, of 8 bits the product of the even bytes and of the
//!   odd bytes by `pmullw`, and of 64 bits is made lane by lane in the registers
//!   of integers, which have `imul`;
//! - a comparison of lanes of 64 bits is made of those of 32, the low halves as
//!   unsigned numbers and the high halves as signed, and a comparison of numbers
//!   read as unsigned flips the top bit of both first, where no instruction does;
//! - a lane is written by `pinsrw`, 16 bits at a time, and read by `pextrw`,
//!   `movd`, or `movq`, after a shuffle.

use super::{Asm, Loc, Place, RAX, RCX, Reg, Rm, System, Writer, XMM13, XMM14, XMM15};
use crate::scalar::{Binary, FloatBinary, FloatBits, FloatCompare, Lane, Relation, Type, Value};

/// What every lane of a vector of the lane holds, as the bits of the vector, when
/// each lane holds the bits `n`.
fn spread(lane: Lane, n: u64) -> u128 {
    (0..lane.count()).fold(0u128, |v, at| v | (u128::from(n) << (at * lane.bits())))
}

impl Asm {
    /// An instruction of SSE2 on two registers of floats, with the prefix `66` for
    /// the instructions of integers and of doubles and none for those of floats:
    /// `rd = rd op rs`.
    pub fn vector_rr(&mut self, prefix: u8, opcode: u8, rd: Reg, rs: Reg) {
        self.sse(None, prefix, false, opcode, rd, Rm::Reg(rs));
    }

    /// An instruction of SSE2 as `vector_rr`, with one byte after it.
    fn vector_rr_imm(&mut self, prefix: u8, opcode: u8, rd: Reg, rs: Reg, imm: u8) {
        self.sse(None, prefix, false, opcode, rd, Rm::Reg(rs));
        self.bytes_after(imm);
    }

    /// Adds a byte to the instruction written last, which is its immediate.
    fn bytes_after(&mut self, imm: u8) {
        self.code.push(imm);
    }

    /// `pshufd rd, rs, imm`: the dwords of `rs`, each placed as two bits of `imm`
    /// say, from the lowest.
    pub fn pshufd(&mut self, rd: Reg, rs: Reg, imm: u8) {
        self.vector_rr_imm(0x66, 0x70, rd, rs, imm);
    }

    /// `pshuflw rd, rs, imm`: as `pshufd` for the four low words.
    fn pshuflw(&mut self, rd: Reg, rs: Reg, imm: u8) {
        self.vector_rr_imm(0xf2, 0x70, rd, rs, imm);
    }

    /// A shift of every lane of `r` by `count` bits: `group` is 2 for a shift right
    /// that fills with zeros, 4 for one that fills with the sign, and 6 for a shift
    /// left, and `size` is 2, 4, or 8 for words, dwords, and qwords.
    fn shift_lanes(&mut self, group: u8, size: u32, r: Reg, count: u8) {
        let opcode = match size {
            2 => 0x71,
            4 => 0x72,
            _ => 0x73,
        };
        self.sse(None, 0x66, false, opcode, Reg(group), Rm::Reg(r));
        self.bytes_after(count);
    }

    /// `pextrw rd, rs, lane`: the word of `rs` at the place, with zeros above.
    fn pextrw(&mut self, rd: Reg, rs: Reg, lane: u8) {
        self.sse(Some(rd), 0x66, false, 0xc5, rd, Rm::Reg(rs));
        self.bytes_after(lane);
    }

    /// `pinsrw rd, rs, lane`: the word at the place is the low word of `rs`.
    fn pinsrw(&mut self, rd: Reg, rs: Reg, lane: u8) {
        self.sse(None, 0x66, false, 0xc4, rd, Rm::Reg(rs));
        self.bytes_after(lane);
    }

    /// `movups rd, [place]`.
    pub fn vector_load(&mut self, rd: Reg, from: Place) {
        self.sse(None, 0, false, 0x10, rd, Rm::Place(from));
    }

    /// `movups [place], rs`.
    pub fn vector_store(&mut self, to: Place, rs: Reg) {
        self.sse(None, 0, false, 0x11, rs, Rm::Place(to));
    }

    /// `pmovmskb`, `movmskps`, or `movmskpd rd, rs`: the top bit of each byte,
    /// dword, or qword of `rs`, in the low bits of `rd`, as `opcode` and `prefix`
    /// say.
    fn move_mask(&mut self, prefix: u8, opcode: u8, rd: Reg, rs: Reg) {
        self.sse(Some(rd), prefix, false, opcode, rd, Rm::Reg(rs));
    }

    /// `bsf rd, rs`: the place of the lowest bit that is 1, of 32 bits.
    fn bit_scan(&mut self, rd: Reg, rs: Reg) {
        self.writing(Some(rd), |a| {
            let rex = ((rd.0 >> 3) << 2) | (rs.0 >> 3);
            if rex != 0 {
                a.bytes(&[0x40 | rex]);
            }
            a.bytes(&[0x0f, 0xbc, Self::modrm(rd, rs)]);
        });
    }

    /// `neg r32` then `sbb r32, r32`: `r` is all ones when it was not 0, and 0
    /// when it was.
    fn all_ones_if_not_zero(&mut self, r: Reg) {
        self.writing(Some(r), |a| {
            if r.0 >= 8 {
                a.bytes(&[0x41]);
            }
            a.bytes(&[0xf7, Self::modrm(Reg(3), r)]);
            let rex = ((r.0 >> 3) << 2) | (r.0 >> 3);
            if rex != 0 {
                a.bytes(&[0x40 | rex]);
            }
            a.bytes(&[0x19, Self::modrm(r, r)]);
        });
    }

    /// Makes the vector whose bits are `bits` in `r`, working in `rax` and in
    /// `other`, which is another register of floats.
    fn vector_constant(&mut self, r: Reg, other: Reg, bits: u128) {
        let (low, high) = (bits as u64, (bits >> 64) as u64);
        if bits == 0 {
            self.float_zero(r);
        } else if bits == u128::MAX {
            // `pcmpeqd r, r`.
            self.vector_rr(0x66, 0x76, r, r);
        } else {
            self.mov_imm(RAX, low);
            self.float_from_bits(r, 8, RAX);
            if high == low {
                self.pshufd(r, r, 0x44);
            } else if high != 0 {
                self.mov_imm(RAX, high);
                self.float_from_bits(other, 8, RAX);
                // `punpcklqdq r, other`.
                self.vector_rr(0x66, 0x6c, r, other);
            }
        }
    }
}

impl<S: System> Writer<'_, S> {
    /// The lane of the vector the value is.
    fn lane_of(&self, v: Value) -> Lane {
        match self.f.types[v.0] {
            Type::Vector(lane) => lane,
            other => unreachable!("{other:?} is not a vector"),
        }
    }

    /// Writes the vector in the register to the frame, and forgets what rax, rcx,
    /// and rdx held of the place.
    pub(super) fn store_vector_frame(&self, asm: &mut Asm, at: u32, r: Reg) {
        asm.vector_store(Place::Frame(at), r);
        for held in self.scratch.borrow_mut().iter_mut() {
            if held.is_some_and(|(place, _, _)| place == at || place == at + 8) {
                *held = None;
            }
        }
    }

    /// Copies a vector from the frame to the frame, through rax, 8 bytes at a
    /// time.
    pub(super) fn copy_vector_frame(&self, asm: &mut Asm, to: u32, from: u32) {
        for half in [0, 8] {
            self.load_frame(asm, RAX, 8, from + half);
            self.store_frame(asm, to + half, RAX, 8, true);
        }
    }

    /// Puts the vector the value is in the register, from its register or from
    /// the frame.
    fn vector_into(&self, asm: &mut Asm, to: Reg, v: Value) {
        match self.at[v.0] {
            Loc::Xmm(r) => {
                if r != to {
                    asm.movaps(to, r);
                }
            }
            Loc::Frame(at) => asm.vector_load(to, Place::Frame(at)),
            _ => unreachable!("a vector is in a register of floats or in the frame"),
        }
    }

    /// A register that holds the vector: its own, or `tmp`, with the vector read
    /// into it.
    fn vector_read(&self, asm: &mut Asm, tmp: Reg, v: Value) -> Reg {
        match self.at[v.0] {
            Loc::Xmm(r) => r,
            _ => {
                self.vector_into(asm, tmp, v);
                tmp
            }
        }
    }

    /// Puts the vector, which is in `r`, in the home of the value.
    fn vector_finish(&self, asm: &mut Asm, v: Value, r: Reg) {
        match self.at[v.0] {
            Loc::Xmm(h) => {
                if h != r {
                    asm.movaps(h, r);
                }
            }
            Loc::Frame(at) => self.store_vector_frame(asm, at, r),
            _ => unreachable!("a vector is in a register of floats or in the frame"),
        }
    }

    /// The register to work a result in: its own, unless that is `avoid`, and then
    /// xmm15.
    fn vector_target(&self, v: Value, avoid: Reg) -> Reg {
        match self.at[v.0] {
            Loc::Xmm(r) if r != avoid => r,
            _ => XMM15,
        }
    }

    /// Writes the vector to the place.
    pub(super) fn vector_write(&self, asm: &mut Asm, to: Place, value: Value) {
        let r = self.vector_read(asm, XMM15, value);
        asm.vector_store(to, r);
    }

    /// `movups` of the vector at the address, into the home of the result.
    pub(super) fn vector_load(&self, asm: &mut Asm, result: Value, address: Reg) {
        let t = self.vector_target(result, XMM15);
        asm.vector_load(t, Place::At(address));
        self.vector_finish(asm, result, t);
    }

    /// A vector constant.
    pub(super) fn vector_constant(&self, asm: &mut Asm, result: Value, bits: u128) {
        let t = self.vector_target(result, XMM15);
        let other = if t == XMM15 { XMM14 } else { XMM15 };
        asm.vector_constant(t, other, bits);
        self.vector_finish(asm, result, t);
    }

    /// `a op b` of two instructions that read two vectors and give one, worked out
    /// in the register of the result when it can be, which `emit` is given, with
    /// the register that holds the second. The operation is `a op b` with the
    /// result in the first register. An operation that gives the same for either
    /// order of its operands is `commutative`.
    fn vector_two(
        &self,
        asm: &mut Asm,
        result: Value,
        a: Value,
        b: Value,
        commutative: bool,
        emit: impl FnOnce(&mut Asm, Reg, Reg),
    ) {
        let (mut first, mut second) = (a, b);
        let mut rb = self.vector_read(asm, XMM13, second);
        let mut t = self.vector_target(result, rb);
        if t == XMM15 && commutative && matches!(self.at[result.0], Loc::Xmm(h) if h == rb) {
            // The result is where the second is: the operation is the same the
            // other way round.
            std::mem::swap(&mut first, &mut second);
            rb = self.vector_read(asm, XMM13, second);
            t = self.vector_target(result, rb);
        }
        self.vector_into(asm, t, first);
        emit(asm, t, rb);
        self.vector_finish(asm, result, t);
    }

    /// The lane-wise sum, difference, product, or bit operation of two vectors of
    /// integers.
    pub(super) fn vector_binary(
        &self,
        asm: &mut Asm,
        result: Value,
        op: Binary,
        a: Value,
        b: Value,
    ) {
        let lane = self.lane_of(result);
        let bits = lane.bits();
        let (commutative, opcode) = match (op, bits) {
            (Binary::Add, 8) => (true, 0xfc),
            (Binary::Add, 16) => (true, 0xfd),
            (Binary::Add, 32) => (true, 0xfe),
            (Binary::Add, _) => (true, 0xd4),
            (Binary::Sub, 8) => (false, 0xf8),
            (Binary::Sub, 16) => (false, 0xf9),
            (Binary::Sub, 32) => (false, 0xfa),
            (Binary::Sub, _) => (false, 0xfb),
            (Binary::And, _) => (true, 0xdb),
            (Binary::Or, _) => (true, 0xeb),
            (Binary::Xor, _) => (true, 0xef),
            (Binary::Mul, 16) => (true, 0xd5),
            (Binary::Mul, _) => return self.vector_multiply(asm, result, lane, a, b),
            _ => unreachable!("the form has no {op:?} of lanes"),
        };
        self.vector_two(asm, result, a, b, commutative, |asm, t, rb| {
            asm.vector_rr(0x66, opcode, t, rb);
        });
    }

    /// The product of lanes of 8, 32, and 64 bits, which SSE2 has no instruction
    /// for. The two vectors are copied into xmm13 and xmm14, which the steps
    /// change, and the result is worked out in xmm13.
    fn vector_multiply(&self, asm: &mut Asm, result: Value, lane: Lane, a: Value, b: Value) {
        match lane.bits() {
            8 => {
                // The even bytes of each word multiply by `pmullw` as they are, and
                // the low byte of each word is theirs. The odd bytes are moved to
                // the low byte first and back after.
                self.vector_into(asm, XMM13, a);
                self.vector_into(asm, XMM14, b);
                asm.movaps(XMM15, XMM13);
                asm.shift_lanes(2, 2, XMM15, 8);
                asm.vector_rr(0x66, 0xd5, XMM13, XMM14);
                asm.shift_lanes(2, 2, XMM14, 8);
                asm.vector_rr(0x66, 0xd5, XMM15, XMM14);
                asm.shift_lanes(6, 2, XMM15, 8);
                // The low byte of each word, of the even products.
                asm.vector_rr(0x66, 0x76, XMM14, XMM14);
                asm.shift_lanes(2, 2, XMM14, 8);
                asm.vector_rr(0x66, 0xdb, XMM13, XMM14);
                asm.vector_rr(0x66, 0xeb, XMM13, XMM15);
            }
            32 => {
                self.vector_into(asm, XMM13, a);
                self.vector_into(asm, XMM14, b);
                // The odd lanes, each in the low dword of a qword.
                asm.pshufd(XMM15, XMM13, 0xf5);
                // `pmuludq`: the even lanes' products, as 64 bits.
                asm.vector_rr(0x66, 0xf4, XMM13, XMM14);
                asm.pshufd(XMM14, XMM14, 0xf5);
                asm.vector_rr(0x66, 0xf4, XMM15, XMM14);
                // The low halves of the products, in order.
                asm.pshufd(XMM13, XMM13, 0xe8);
                asm.pshufd(XMM15, XMM15, 0xe8);
                // `punpckldq`.
                asm.vector_rr(0x66, 0x62, XMM13, XMM15);
            }
            _ => {
                // The two lanes, one at a time, in the registers of integers.
                self.lane_product(asm, a, b, 0, XMM13);
                self.lane_product(asm, a, b, 1, XMM14);
                // `punpcklqdq`: the first product is the low qword, the second
                // the high.
                asm.vector_rr(0x66, 0x6c, XMM13, XMM14);
            }
        }
        self.vector_finish(asm, result, XMM13);
    }

    /// The product of the lane at the place of two vectors of lanes of 64 bits, in
    /// the low qword of `into`.
    fn lane_product(&self, asm: &mut Asm, a: Value, b: Value, lane: u8, into: Reg) {
        self.lane_into(asm, RAX, a, lane);
        self.lane_into(asm, RCX, b, lane);
        asm.imul(RAX, RCX);
        asm.float_from_bits(into, 8, RAX);
    }

    /// Reads the lane at the place of a vector of lanes of 64 bits into the
    /// register of integers, from the frame as it is, and from a register of
    /// floats through xmm15.
    fn lane_into(&self, asm: &mut Asm, rd: Reg, v: Value, lane: u8) {
        match self.at[v.0] {
            Loc::Frame(at) => self.load_frame(asm, rd, 8, at + 8 * u32::from(lane)),
            Loc::Xmm(r) if lane == 0 => asm.bits_from_float(rd, 8, r),
            Loc::Xmm(r) => {
                asm.pshufd(XMM15, r, 0xee);
                asm.bits_from_float(rd, 8, XMM15);
            }
            _ => unreachable!("a vector is in a register of floats or in the frame"),
        }
    }

    /// `a op b` of floats, in each lane.
    pub(super) fn vector_float_binary(
        &self,
        asm: &mut Asm,
        result: Value,
        op: FloatBinary,
        a: Value,
        b: Value,
    ) {
        let lane = self.lane_of(result);
        let (commutative, opcode) = match op {
            FloatBinary::Add => (true, 0x58),
            FloatBinary::Sub => (false, 0x5c),
            FloatBinary::Mul => (true, 0x59),
            FloatBinary::Div => unreachable!("the form has no division of lanes"),
        };
        let prefix = if lane.bits() == 64 { 0x66 } else { 0 };
        self.vector_two(asm, result, a, b, commutative, |asm, t, rb| {
            asm.vector_rr(prefix, opcode, t, rb);
        });
    }

    /// The mask of a comparison of two vectors of integers, as lanes that are all
    /// ones where it holds.
    pub(super) fn vector_compare(
        &self,
        asm: &mut Asm,
        result: Value,
        op: Relation,
        signed: bool,
        a: Value,
        b: Value,
    ) {
        let lane = self.lane_of(result);
        // Each comparison is `x > y` or `x == y`, possibly turned over.
        let (x, y, flipped, equal) = match op {
            Relation::Equal => (a, b, false, true),
            Relation::NotEqual => (a, b, true, true),
            Relation::Greater => (a, b, false, false),
            Relation::LessEqual => (a, b, true, false),
            Relation::Less => (b, a, false, false),
            Relation::GreaterEqual => (b, a, true, false),
        };
        if equal {
            self.lanes_equal(asm, lane, x, y);
        } else {
            self.lanes_greater(asm, lane, signed, x, y);
        }
        if flipped {
            // `pcmpeqd xmm15, xmm15` is all ones.
            asm.vector_rr(0x66, 0x76, XMM15, XMM15);
            asm.vector_rr(0x66, 0xef, XMM13, XMM15);
        }
        self.vector_finish(asm, result, XMM13);
    }

    /// Makes xmm13 the mask of `x == y`.
    fn lanes_equal(&self, asm: &mut Asm, lane: Lane, x: Value, y: Value) {
        self.vector_into(asm, XMM13, x);
        let ry = self.vector_read(asm, XMM14, y);
        match lane.bits() {
            8 => asm.vector_rr(0x66, 0x74, XMM13, ry),
            16 => asm.vector_rr(0x66, 0x75, XMM13, ry),
            32 => asm.vector_rr(0x66, 0x76, XMM13, ry),
            _ => {
                // The dwords that are equal, and then both dwords of a qword.
                asm.vector_rr(0x66, 0x76, XMM13, ry);
                asm.pshufd(XMM14, XMM13, 0xb1);
                asm.vector_rr(0x66, 0xdb, XMM13, XMM14);
            }
        }
    }

    /// Makes xmm13 the mask of `x > y`, read as signed or as unsigned numbers.
    fn lanes_greater(&self, asm: &mut Asm, lane: Lane, signed: bool, x: Value, y: Value) {
        let bits = lane.bits();
        if bits < 64 && signed {
            self.vector_into(asm, XMM13, x);
            let ry = self.vector_read(asm, XMM14, y);
            let opcode = match bits {
                8 => 0x64,
                16 => 0x65,
                _ => 0x66,
            };
            asm.vector_rr(0x66, opcode, XMM13, ry);
            return;
        }
        // A number read as unsigned compares as one that has its top bit flipped
        // and is read as signed. For lanes of 64 bits the high dwords compare as
        // numbers that way, and the low dwords, as unsigned numbers, by flipping
        // their top bit too.
        let flip = if bits < 64 {
            spread(lane, 1 << (bits - 1))
        } else if signed {
            spread(Lane::Int(crate::scalar::Bits::B64), 1 << 31)
        } else {
            spread(Lane::Int(crate::scalar::Bits::B64), (1 << 63) | (1 << 31))
        };
        self.vector_into(asm, XMM13, x);
        self.vector_into(asm, XMM14, y);
        asm.vector_constant(XMM15, XMM15, flip);
        asm.vector_rr(0x66, 0xef, XMM13, XMM15);
        asm.vector_rr(0x66, 0xef, XMM14, XMM15);
        if bits < 64 {
            let opcode = match bits {
                8 => 0x64,
                16 => 0x65,
                _ => 0x66,
            };
            asm.vector_rr(0x66, opcode, XMM13, XMM14);
            return;
        }
        // xmm15: the dwords that are equal. xmm13: the dwords that are greater,
        // the low as unsigned and the high as signed or as unsigned numbers.
        asm.movaps(XMM15, XMM13);
        asm.vector_rr(0x66, 0x76, XMM15, XMM14);
        asm.vector_rr(0x66, 0x66, XMM13, XMM14);
        // The low dword's answer, in both dwords of the qword.
        asm.pshufd(XMM14, XMM13, 0xa0);
        // The high dwords are greater, or are equal and the low are greater.
        asm.vector_rr(0x66, 0xdb, XMM15, XMM14);
        asm.vector_rr(0x66, 0xeb, XMM15, XMM13);
        asm.pshufd(XMM13, XMM15, 0xf5);
    }

    /// The mask of a comparison of two vectors of floats, as lanes that are all
    /// ones where it holds.
    pub(super) fn vector_float_compare(
        &self,
        asm: &mut Asm,
        result: Value,
        op: FloatCompare,
        a: Value,
        b: Value,
    ) {
        let lane = self.lane_of(a);
        // `cmpps` and `cmppd` with the predicate: 0 is equal, 1 is below, 2 is
        // below or equal, and 4 is not equal, which is true for a number that is
        // not one, and false for the others. A comparison the other way round is
        // the one of the swapped operands.
        let (predicate, x, y) = match op {
            FloatCompare::Equal => (0, a, b),
            FloatCompare::NotEqual => (4, a, b),
            FloatCompare::Less => (1, a, b),
            FloatCompare::LessEqual => (2, a, b),
            FloatCompare::Greater => (1, b, a),
            FloatCompare::GreaterEqual => (2, b, a),
        };
        let prefix = if lane.bits() == 64 { 0x66 } else { 0 };
        self.vector_into(asm, XMM13, x);
        let ry = self.vector_read(asm, XMM14, y);
        asm.vector_rr_imm(prefix, 0xc2, XMM13, ry, predicate);
        self.vector_finish(asm, result, XMM13);
    }

    /// Each bit from `then` where the mask has a 1 and from `otherwise` where it
    /// has a 0: `otherwise ^ ((then ^ otherwise) & mask)`, with the mask in the
    /// register.
    fn vector_select(
        &self,
        asm: &mut Asm,
        result: Value,
        mask: Reg,
        then: Value,
        otherwise: Value,
    ) {
        self.vector_into(asm, XMM13, then);
        self.vector_into(asm, XMM14, otherwise);
        asm.vector_rr(0x66, 0xef, XMM13, XMM14);
        asm.vector_rr(0x66, 0xdb, XMM13, mask);
        asm.vector_rr(0x66, 0xef, XMM13, XMM14);
        self.vector_finish(asm, result, XMM13);
    }

    /// A choice of whole vectors by a mask.
    pub(super) fn vector_choose(
        &self,
        asm: &mut Asm,
        result: Value,
        mask: Value,
        then: Value,
        otherwise: Value,
    ) {
        let mask = self.vector_read(asm, XMM15, mask);
        self.vector_select(asm, result, mask, then, otherwise);
    }

    /// A choice of whole vectors by a number that is not 0.
    pub(super) fn vector_choose_by_number(
        &self,
        asm: &mut Asm,
        result: Value,
        on: Value,
        then: Value,
        otherwise: Value,
    ) {
        self.read_into(asm, RCX, on);
        asm.all_ones_if_not_zero(RCX);
        asm.float_from_bits(XMM15, 4, RCX);
        asm.pshufd(XMM15, XMM15, 0);
        self.vector_select(asm, result, XMM15, then, otherwise);
    }

    /// A vector of one integer or float.
    pub(super) fn vector_splat(&self, asm: &mut Asm, result: Value, value: Value) {
        let lane = self.lane_of(result);
        let t = self.vector_target(result, XMM15);
        match lane {
            Lane::Float(bits) => {
                let r = self.float_read(asm, XMM14, value);
                asm.pshufd(t, r, if bits == FloatBits::F32 { 0 } else { 0x44 });
            }
            Lane::Int(_) => {
                let bits = lane.bits();
                let r = self.read(asm, RAX, value);
                asm.float_from_bits(t, if bits == 64 { 8 } else { 4 }, r);
                match bits {
                    8 => {
                        // `punpcklbw`.
                        asm.vector_rr(0x66, 0x60, t, t);
                        asm.pshuflw(t, t, 0);
                        asm.pshufd(t, t, 0);
                    }
                    16 => {
                        asm.pshuflw(t, t, 0);
                        asm.pshufd(t, t, 0);
                    }
                    32 => asm.pshufd(t, t, 0),
                    _ => asm.pshufd(t, t, 0x44),
                }
            }
        }
        self.vector_finish(asm, result, t);
    }

    /// The lane at the place, as a number or a float.
    pub(super) fn vector_extract(&self, asm: &mut Asm, result: Value, vector: Value, at: u8) {
        let lane = self.lane_of(vector);
        let v = self.vector_read(asm, XMM14, vector);
        let bits = lane.bits();
        // The dword or qword that the lane is, moved to the bottom.
        let low = |asm: &mut Asm, shuffle: u8| -> Reg {
            if shuffle == 0 {
                v
            } else {
                asm.pshufd(XMM15, v, shuffle);
                XMM15
            }
        };
        match lane {
            Lane::Float(_) => {
                let t = match (bits, at) {
                    (_, 0) => v,
                    (32, k) => low(asm, 0x55 * k),
                    _ => low(asm, 0xee),
                };
                self.float_finish(asm, result, t);
            }
            Lane::Int(_) => {
                match bits {
                    8 => {
                        asm.pextrw(RAX, v, at / 2);
                        if at % 2 == 1 {
                            asm.shr_imm(RAX, 8);
                        } else {
                            asm.movzx8(RAX, RAX);
                        }
                    }
                    16 => asm.pextrw(RAX, v, at),
                    32 => {
                        let r = low(asm, 0x55 * at);
                        asm.bits_from_float(RAX, 4, r);
                    }
                    _ => {
                        let r = low(asm, if at == 0 { 0 } else { 0xee });
                        asm.bits_from_float(RAX, 8, r);
                    }
                }
                self.finish(asm, result, RAX, true);
            }
        }
    }

    /// The vector with the lane at the place written, 16 bits at a time.
    pub(super) fn vector_insert(
        &self,
        asm: &mut Asm,
        result: Value,
        vector: Value,
        at: u8,
        value: Value,
    ) {
        let lane = self.lane_of(vector);
        let bits = lane.bits();
        // The lane's bits are in rcx, with zeros above.
        match lane {
            Lane::Float(_) => self.float_bits_into(asm, RCX, value),
            Lane::Int(_) => self.read_into(asm, RCX, value),
        }
        let t = self.vector_target(result, XMM15);
        self.vector_into(asm, t, vector);
        let at = u32::from(at);
        match bits {
            8 => {
                asm.pextrw(RAX, t, (at / 2) as u8);
                if at % 2 == 1 {
                    asm.alu_imm(4, RAX, 0xff);
                    asm.shl_imm(RCX, 8);
                } else {
                    asm.alu_imm(4, RAX, 0xff00);
                }
                asm.or(RAX, RCX);
                asm.pinsrw(t, RAX, (at / 2) as u8);
            }
            _ => {
                let words = bits / 16;
                for k in 0..words {
                    asm.pinsrw(t, RCX, (at * words + k) as u8);
                    if k + 1 < words {
                        asm.shr_imm(RCX, 16);
                    }
                }
            }
        }
        self.vector_finish(asm, result, t);
    }

    /// The sum of the lanes. Integers wrap, and floats are added in pairs of lanes
    /// that lie next to each other, and then those sums.
    pub(super) fn vector_sum(&self, asm: &mut Asm, result: Value, vector: Value) {
        let lane = self.lane_of(vector);
        let v = self.vector_read(asm, XMM14, vector);
        // Every step but the first is made on xmm13, which is a copy.
        let add = |asm: &mut Asm, opcode: u8, shuffle: u8| {
            asm.pshufd(XMM15, XMM13, shuffle);
            asm.vector_rr(0x66, opcode, XMM13, XMM15);
        };
        match lane {
            Lane::Float(FloatBits::F32) => {
                // `(l0 + l1) + (l2 + l3)`: the neighbours' sums are in lanes 0 and 2.
                asm.pshufd(XMM15, v, 0xb1);
                asm.movaps(XMM13, v);
                asm.vector_rr(0, 0x58, XMM13, XMM15);
                asm.pshufd(XMM15, XMM13, 0xee);
                asm.float_arithmetic(0x58, 4, XMM13, Rm::Reg(XMM15));
                self.float_finish(asm, result, XMM13);
            }
            Lane::Float(FloatBits::F64) => {
                asm.pshufd(XMM15, v, 0xee);
                asm.movaps(XMM13, v);
                asm.float_arithmetic(0x58, 8, XMM13, Rm::Reg(XMM15));
                self.float_finish(asm, result, XMM13);
            }
            Lane::Int(bits) => {
                match bits.count() {
                    8 => {
                        // The sum of the bytes of each half, as an unsigned number
                        // of 16 bits, and the two added.
                        asm.float_zero(XMM15);
                        asm.movaps(XMM13, v);
                        asm.vector_rr(0x66, 0xf6, XMM13, XMM15);
                        add(asm, 0xd4, 0xee);
                    }
                    16 => {
                        asm.movaps(XMM13, v);
                        add(asm, 0xfd, 0xee);
                        add(asm, 0xfd, 0x55);
                        asm.movaps(XMM15, XMM13);
                        asm.shift_lanes(2, 4, XMM15, 16);
                        asm.vector_rr(0x66, 0xfd, XMM13, XMM15);
                    }
                    32 => {
                        asm.movaps(XMM13, v);
                        add(asm, 0xfe, 0xee);
                        add(asm, 0xfe, 0x55);
                    }
                    _ => {
                        asm.movaps(XMM13, v);
                        add(asm, 0xd4, 0xee);
                    }
                }
                let width = if bits.count() == 64 { 8 } else { 4 };
                asm.bits_from_float(RAX, width, XMM13);
                match bits.count() {
                    8 => asm.movzx8(RAX, RAX),
                    16 => asm.movzx16(RAX, RAX),
                    _ => {}
                }
                self.finish(asm, result, RAX, true);
            }
        }
    }

    /// The place of the first lane of the mask whose top bit is 1, and the number
    /// of lanes when none has.
    pub(super) fn vector_first_set(&self, asm: &mut Asm, result: Value, mask: Value) {
        let lane = self.lane_of(mask);
        let m = self.vector_read(asm, XMM14, mask);
        // A bit that is 1 for each lane whose top bit is, and one more that is
        // always 1, where the lanes end, for `bsf` to find when none is.
        let ends = match lane.bits() {
            8 => {
                asm.move_mask(0x66, 0xd7, RAX, m);
                16
            }
            16 => {
                // The words as bytes that keep their sign, twice.
                asm.movaps(XMM15, m);
                asm.vector_rr(0x66, 0x63, XMM15, XMM15);
                asm.move_mask(0x66, 0xd7, RAX, XMM15);
                8
            }
            32 => {
                asm.move_mask(0, 0x50, RAX, m);
                4
            }
            _ => {
                asm.move_mask(0x66, 0x50, RAX, m);
                2
            }
        };
        if ends == 8 {
            // The words as bytes are in both halves of the register.
            asm.alu_imm(4, RAX, 0xff);
        }
        asm.alu_imm(1, RAX, 1 << ends);
        asm.bit_scan(RAX, RAX);
        self.finish(asm, result, RAX, true);
    }
}
