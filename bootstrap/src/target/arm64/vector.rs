//! Vectors on arm64, built with NEON, which every arm64 chip has. A vector is
//! held in a register of floats, `v0` to `v28`, or in the frame, where it takes
//! 16 bytes, and is read and written as a `q` register. `v29`, `v30`, and `v31`
//! are kept for the instructions that work in more than one step: a value that
//! is in the frame is read into one of them first, and a result that is worked
//! out in them is put in its home last.
//!
//! A call keeps only the low 64 bits of `v8` to `v15`, so a vector is never in
//! one of them across a call, which the allocator sees to. NEON has no product of
//! lanes of 64 bits, which is made lane by lane in the registers of integers, and
//! no instruction that says where the first lane of a mask that is set is, which
//! is made from `shrn`, `rbit`, and `clz`.

use super::{Asm, Loc, NE, Place, SP, System, V29, V30, V31, Writer, X15, X16, X17};
use crate::scalar::{
    Binary, FloatBinary, FloatBits, FloatCompare, Fused, Lane, Relation, Type, Value,
};

/// The `size` field of an instruction on lanes of the lane's size.
fn size_of(lane: Lane) -> u32 {
    match lane.bits() {
        8 => 0,
        16 => 1,
        32 => 2,
        _ => 3,
    }
}

/// The `imm5` field of an instruction that names the lane at the place: one bit
/// that says the size of a lane, and the place above it.
fn lane_field(lane: Lane, at: u32) -> u32 {
    let log = (lane.bits() / 8).trailing_zeros();
    (at << (log + 1)) | (1 << log)
}

impl Asm {
    /// An instruction of three registers of vectors, from the bits that choose
    /// it: `vd = vn op vm`.
    fn vector_three(&mut self, bits: u32, rd: u32, rn: u32, rm: u32) {
        self.word(bits | rm << 16 | rn << 5 | rd);
    }

    /// An instruction of two registers of vectors, from the bits that choose it.
    fn vector_two(&mut self, bits: u32, rd: u32, rn: u32) {
        self.word(bits | rn << 5 | rd);
    }

    /// `mov vd.16b, vn.16b`: copies a vector, whole.
    pub fn vector_mov(&mut self, rd: u32, rn: u32) {
        self.vector_three(0x4ea0_1c00, rd, rn, rn);
    }

    /// The register that holds the address of a place, and the number of 16
    /// bytes from it that the instruction takes. A place in the frame that is not a
    /// multiple of 16, or is too far for the instruction, is reached through x15.
    fn reach_vector(&mut self, place: Place) -> (u32, u32) {
        match place {
            Place::At(r) => (r, 0),
            Place::Indexed { .. } => unreachable!("a vector is not written at an index"),
            Place::Frame(offset) => {
                if offset.is_multiple_of(16) && offset / 16 < 1 << 12 {
                    (SP, offset / 16)
                } else {
                    self.add_immediate(0x9100_0000, X15, SP, offset);
                    (X15, 0)
                }
            }
        }
    }

    /// `ldr qt, [place]`.
    pub fn vector_load(&mut self, rt: u32, from: Place) {
        let (base, at) = self.reach_vector(from);
        self.word(0x3dc0_0000 | at << 10 | base << 5 | rt);
    }

    /// `str qt, [place]`.
    pub fn vector_store(&mut self, to: Place, rt: u32) {
        let (base, at) = self.reach_vector(to);
        self.word(0x3d80_0000 | at << 10 | base << 5 | rt);
    }

    /// `movi vd.2d, #0` or `movi vd.2d, #0xffffffffffffffff`.
    fn vector_fill(&mut self, rd: u32, ones: bool) {
        self.word(if ones { 0x6f07_e7e0 } else { 0x6f00_e400 } | rd);
    }

    /// `dup vd.T, wn` or `xn`: every lane is the low bits of the register.
    fn vector_dup(&mut self, lane: Lane, rd: u32, rn: u32) {
        self.word(0x4e00_0c00 | lane_field(lane, 0) << 16 | rn << 5 | rd);
    }

    /// `dup vd.T, vn.T[0]`: every lane is the first lane of the vector.
    fn vector_dup_lane(&mut self, lane: Lane, rd: u32, rn: u32) {
        self.word(0x4e00_0400 | lane_field(lane, 0) << 16 | rn << 5 | rd);
    }

    /// `umov wd, vn.T[at]`, or `xd` for lanes of 64 bits: the lane, with zeros
    /// above.
    fn vector_umov(&mut self, lane: Lane, rd: u32, rn: u32, at: u32) {
        let q = if lane.bits() == 64 { 0x4000_0000 } else { 0 };
        self.word(0x0e00_3c00 | q | lane_field(lane, at) << 16 | rn << 5 | rd);
    }

    /// `ins vd.T[at], wn` or `xn`: the lane is the low bits of the register.
    fn vector_ins(&mut self, lane: Lane, rd: u32, at: u32, rn: u32) {
        self.word(0x4e00_1c00 | lane_field(lane, at) << 16 | rn << 5 | rd);
    }

    /// `ins vd.T[at], vn.T[0]`: the lane is the first lane of `vn`.
    fn vector_ins_lane(&mut self, lane: Lane, rd: u32, at: u32, rn: u32) {
        self.word(0x6e00_0400 | lane_field(lane, at) << 16 | rn << 5 | rd);
    }

    /// `dup sd, vn.s[at]` or `dup dd, vn.d[at]`: the lane as a float.
    fn vector_lane_as_float(&mut self, lane: Lane, rd: u32, rn: u32, at: u32) {
        self.word(0x5e00_0400 | lane_field(lane, at) << 16 | rn << 5 | rd);
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

    /// Puts the vector the value is in the register, from its register or from
    /// the frame.
    fn vector_into(&self, asm: &mut Asm, to: u32, v: Value) {
        match self.at[v.0] {
            Loc::Float(r) => {
                if r != to {
                    asm.vector_mov(to, r);
                }
            }
            Loc::Frame(at) => asm.vector_load(to, Place::Frame(at)),
            _ => unreachable!("a vector is in a register of floats or in the frame"),
        }
    }

    /// A register that holds the vector: its own, or `tmp`, with the vector read
    /// into it.
    fn vector_read(&self, asm: &mut Asm, tmp: u32, v: Value) -> u32 {
        match self.at[v.0] {
            Loc::Float(r) => r,
            _ => {
                self.vector_into(asm, tmp, v);
                tmp
            }
        }
    }

    /// The register to work a result out in: its own, or v30.
    fn vector_target(&self, v: Value) -> u32 {
        match self.at[v.0] {
            Loc::Float(r) => r,
            _ => V30,
        }
    }

    /// Puts the vector, which is in `r`, in the home of the value.
    fn vector_finish(&self, asm: &mut Asm, v: Value, r: u32) {
        match self.at[v.0] {
            Loc::Float(h) => {
                if h != r {
                    asm.vector_mov(h, r);
                }
            }
            Loc::Frame(at) => asm.vector_store(Place::Frame(at), r),
            _ => unreachable!("a vector is in a register of floats or in the frame"),
        }
    }

    /// Writes the vector to the place.
    pub(super) fn vector_write(&self, asm: &mut Asm, to: Place, value: Value) {
        let r = self.vector_read(asm, V30, value);
        asm.vector_store(to, r);
    }

    /// `ldr q` of the vector at the address, into the home of the result.
    pub(super) fn vector_from_memory(&self, asm: &mut Asm, result: Value, address: u32) {
        let t = self.vector_target(result);
        asm.vector_load(t, Place::At(address));
        self.vector_finish(asm, result, t);
    }

    /// Copies a vector from the frame to the frame, through x16, 8 bytes at a
    /// time.
    pub(super) fn copy_vector_frame(&self, asm: &mut Asm, to: u32, from: u32) {
        for half in [0, 8] {
            asm.load(X16, 8, false, Place::Frame(from + half));
            asm.store(Place::Frame(to + half), X16, 8);
        }
    }

    /// A vector constant.
    pub(super) fn vector_constant(&self, asm: &mut Asm, result: Value, bits: u128) {
        let t = self.vector_target(result);
        let (low, high) = (bits as u64, (bits >> 64) as u64);
        if bits == 0 || bits == u128::MAX {
            asm.vector_fill(t, bits != 0);
        } else {
            asm.mov_imm(X16, low);
            if high == low {
                asm.vector_dup(Lane::Int(crate::scalar::Bits::B64), t, X16);
            } else {
                // `fmov dd, xn` zeros the rest, then the high half is written.
                asm.float_from_bits(t, 8, X16);
                if high != 0 {
                    asm.mov_imm(X16, high);
                    asm.vector_ins(Lane::Int(crate::scalar::Bits::B64), t, 1, X16);
                }
            }
        }
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
        let size = size_of(lane) << 22;
        let bits = match op {
            Binary::Add => 0x4e20_8400 | size,
            Binary::Sub => 0x6e20_8400 | size,
            Binary::Mul if lane.bits() == 64 => {
                return self.vector_multiply_lanes(asm, result, a, b);
            }
            Binary::Mul => 0x4e20_9c00 | size,
            Binary::And => 0x4e20_1c00,
            Binary::Or => 0x4ea0_1c00,
            Binary::Xor => 0x6e20_1c00,
            _ => unreachable!("the form has no {op:?} of lanes"),
        };
        let (ra, rb) = (self.vector_read(asm, V30, a), self.vector_read(asm, V31, b));
        let t = self.vector_target(result);
        asm.vector_three(bits, t, ra, rb);
        self.vector_finish(asm, result, t);
    }

    /// The product of lanes of 64 bits, which NEON has no instruction for: each
    /// lane is multiplied in the registers of integers.
    fn vector_multiply_lanes(&self, asm: &mut Asm, result: Value, a: Value, b: Value) {
        let wide = Lane::Int(crate::scalar::Bits::B64);
        for at in 0..2 {
            self.lane_into(asm, X15, a, at);
            self.lane_into(asm, X16, b, at);
            // `mul x15, x15, x16`.
            asm.three(0x9b00_7c00, X15, X15, X16);
            if at == 0 {
                asm.float_from_bits(V29, 8, X15);
            } else {
                asm.vector_ins(wide, V29, 1, X15);
            }
        }
        self.vector_finish(asm, result, V29);
    }

    /// Reads the lane at the place of a vector of lanes of 64 bits into the
    /// register of integers, from the frame as it is.
    fn lane_into(&self, asm: &mut Asm, rd: u32, v: Value, at: u32) {
        match self.at[v.0] {
            Loc::Float(r) => asm.vector_umov(Lane::Int(crate::scalar::Bits::B64), rd, r, at),
            Loc::Frame(place) => asm.load(rd, 8, false, Place::Frame(place + 8 * at)),
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
        let double = u32::from(lane.bits() == 64) << 22;
        let bits = match op {
            FloatBinary::Add => 0x4e20_d400,
            FloatBinary::Sub => 0x4ea0_d400,
            FloatBinary::Mul => 0x6e20_dc00,
            FloatBinary::Div => unreachable!("the form has no division of lanes"),
        } | double;
        let (ra, rb) = (self.vector_read(asm, V30, a), self.vector_read(asm, V31, b));
        let t = self.vector_target(result);
        asm.vector_three(bits, t, ra, rb);
        self.vector_finish(asm, result, t);
    }

    /// `a * b + c`, or the form of `op` of them, of floats, in each lane, rounded
    /// once. `fmla` and `fmls` add to or take from the register they give the
    /// result in, so it holds `c` first, or `-c` for `a * b - c`, which `fneg`
    /// makes exactly. The register is not one that `a` or `b` is read from.
    pub(super) fn vector_float_fused(
        &self,
        asm: &mut Asm,
        result: Value,
        op: Fused,
        a: Value,
        b: Value,
        c: Value,
    ) {
        let lane = self.lane_of(result);
        let double = u32::from(lane.bits() == 64) << 22;
        let (ra, rb) = (self.vector_read(asm, V31, a), self.vector_read(asm, V29, b));
        let t = match self.at[result.0] {
            Loc::Float(r) if r != ra && r != rb => r,
            _ => V30,
        };
        self.vector_into(asm, t, c);
        match op {
            Fused::MulAdd => asm.vector_three(0x4e20_cc00 | double, t, ra, rb),
            Fused::NegMulAdd => asm.vector_three(0x4ea0_cc00 | double, t, ra, rb),
            Fused::MulSub => {
                asm.vector_two(0x6ea0_f800 | double, t, t);
                asm.vector_three(0x4e20_cc00 | double, t, ra, rb);
            }
        }
        self.vector_finish(asm, result, t);
    }

    /// The mask of a comparison of two vectors of integers: lanes that are all
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
        let size = size_of(lane) << 22;
        // `x > y` and `x >= y` have an instruction each, as signed and as unsigned
        // numbers, and `==`, which `!=` is the opposite of. Below is above with
        // the operands the other way round.
        let (bits, x, y, invert) = match (op, signed) {
            (Relation::Equal, _) => (0x6e20_8c00, a, b, false),
            (Relation::NotEqual, _) => (0x6e20_8c00, a, b, true),
            (Relation::Greater, true) => (0x4e20_3400, a, b, false),
            (Relation::Greater, false) => (0x6e20_3400, a, b, false),
            (Relation::GreaterEqual, true) => (0x4e20_3c00, a, b, false),
            (Relation::GreaterEqual, false) => (0x6e20_3c00, a, b, false),
            (Relation::Less, true) => (0x4e20_3400, b, a, false),
            (Relation::Less, false) => (0x6e20_3400, b, a, false),
            (Relation::LessEqual, true) => (0x4e20_3c00, b, a, false),
            (Relation::LessEqual, false) => (0x6e20_3c00, b, a, false),
        };
        let (rx, ry) = (self.vector_read(asm, V30, x), self.vector_read(asm, V31, y));
        let t = self.vector_target(result);
        asm.vector_three(bits | size, t, rx, ry);
        if invert {
            // `mvn vd.16b, vn.16b`.
            asm.vector_two(0x6e20_5800, t, t);
        }
        self.vector_finish(asm, result, t);
    }

    /// The mask of a comparison of two vectors of floats: lanes that are all
    /// ones where it holds, and none of them when either is not a number but for
    /// `!=`.
    pub(super) fn vector_float_compare(
        &self,
        asm: &mut Asm,
        result: Value,
        op: FloatCompare,
        a: Value,
        b: Value,
    ) {
        let lane = self.lane_of(a);
        let double = u32::from(lane.bits() == 64) << 22;
        // `fcmeq`, `fcmge`, and `fcmgt` are false for a number that is not one,
        // and the others are made of them: `!=` is not `==`, which is true for it,
        // and below is above with the operands the other way round.
        let (bits, x, y, invert) = match op {
            FloatCompare::Equal => (0x4e20_e400, a, b, false),
            FloatCompare::NotEqual => (0x4e20_e400, a, b, true),
            FloatCompare::Greater => (0x6ea0_e400, a, b, false),
            FloatCompare::GreaterEqual => (0x6e20_e400, a, b, false),
            FloatCompare::Less => (0x6ea0_e400, b, a, false),
            FloatCompare::LessEqual => (0x6e20_e400, b, a, false),
        };
        let (rx, ry) = (self.vector_read(asm, V30, x), self.vector_read(asm, V31, y));
        let t = self.vector_target(result);
        asm.vector_three(bits | double, t, rx, ry);
        if invert {
            asm.vector_two(0x6e20_5800, t, t);
        }
        self.vector_finish(asm, result, t);
    }

    /// Each bit from `then` where the mask has a 1 and from `otherwise` where it
    /// has a 0, which is `bsl` on a copy of the mask, made in v29.
    fn vector_select(&self, asm: &mut Asm, result: Value, then: Value, otherwise: Value) {
        let (rt, ro) = (
            self.vector_read(asm, V30, then),
            self.vector_read(asm, V31, otherwise),
        );
        // `bsl vd.16b, vn.16b, vm.16b`.
        asm.vector_three(0x6e60_1c00, V29, rt, ro);
        self.vector_finish(asm, result, V29);
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
        self.vector_into(asm, V29, mask);
        self.vector_select(asm, result, then, otherwise);
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
        let on = self.read(asm, X16, on);
        asm.cmp_imm(on, 0);
        // `csetm x17, ne`: all ones when it is not 0, and 0 when it is.
        asm.word(0xda9f_03e0 | (NE ^ 1) << 12 | X17);
        asm.vector_dup(Lane::Int(crate::scalar::Bits::B64), V29, X17);
        self.vector_select(asm, result, then, otherwise);
    }

    /// A vector of one integer or float.
    pub(super) fn vector_splat(&self, asm: &mut Asm, result: Value, value: Value) {
        let lane = self.lane_of(result);
        let t = self.vector_target(result);
        match lane {
            Lane::Float(_) => {
                let r = self.float_read(asm, V31, value);
                asm.vector_dup_lane(lane, t, r);
            }
            Lane::Int(_) => {
                let r = self.read(asm, X16, value);
                asm.vector_dup(lane, t, r);
            }
        }
        self.vector_finish(asm, result, t);
    }

    /// The lane at the place, as a number or a float.
    pub(super) fn vector_extract(&self, asm: &mut Asm, result: Value, vector: Value, at: u8) {
        let lane = self.lane_of(vector);
        let v = self.vector_read(asm, V30, vector);
        match lane {
            Lane::Float(_) => {
                let t = self.float_target(result);
                asm.vector_lane_as_float(lane, t, v, u32::from(at));
                self.float_finish(asm, result, t);
            }
            Lane::Int(_) => {
                let t = self.target(result);
                asm.vector_umov(lane, t, v, u32::from(at));
                self.finish(asm, result, t, true);
            }
        }
    }

    /// The vector with the lane at the place written.
    pub(super) fn vector_insert(
        &self,
        asm: &mut Asm,
        result: Value,
        vector: Value,
        at: u8,
        value: Value,
    ) {
        let lane = self.lane_of(vector);
        let at = u32::from(at);
        // The register the vector is worked out in is not the one the value is in.
        let own = self.vector_target(result);
        let t = match (lane, self.at[value.0]) {
            (Lane::Float(_), Loc::Float(r)) if r == own => V30,
            _ => own,
        };
        match lane {
            Lane::Float(_) => {
                let r = self.float_read(asm, V31, value);
                self.vector_into(asm, t, vector);
                asm.vector_ins_lane(lane, t, at, r);
            }
            Lane::Int(_) => {
                let r = self.read(asm, X16, value);
                self.vector_into(asm, t, vector);
                asm.vector_ins(lane, t, at, r);
            }
        }
        self.vector_finish(asm, result, t);
    }

    /// The sum of the lanes. Integers wrap, and floats are added in pairs of lanes
    /// that lie next to each other, and then those sums.
    pub(super) fn vector_sum(&self, asm: &mut Asm, result: Value, vector: Value) {
        let lane = self.lane_of(vector);
        let v = self.vector_read(asm, V30, vector);
        match lane {
            Lane::Float(FloatBits::F32) => {
                // `faddp v31.4s, v.4s, v.4s` gives `l0 + l1` and `l2 + l3` twice,
                // and `faddp s, v31.2s` adds them.
                asm.vector_three(0x6e20_d400, V31, v, v);
                let t = self.float_target(result);
                asm.vector_two(0x7e30_d800, t, V31);
                self.float_finish(asm, result, t);
            }
            Lane::Float(FloatBits::F64) => {
                let t = self.float_target(result);
                asm.vector_two(0x7e70_d800, t, v);
                self.float_finish(asm, result, t);
            }
            Lane::Int(bits) if bits.count() == 64 => {
                // `addp d31, v.2d`.
                asm.vector_two(0x5ef1_b800, V31, v);
                let t = self.target(result);
                asm.bits_from_float(t, 8, V31);
                self.finish(asm, result, t, true);
            }
            Lane::Int(bits) => {
                // `addv`, across the lanes.
                asm.vector_two(0x4e31_b800 | size_of(lane) << 22, V31, v);
                let t = self.target(result);
                match bits.count() {
                    32 => asm.bits_from_float(t, 4, V31),
                    _ => asm.vector_umov(lane, t, V31, 0),
                }
                self.finish(asm, result, t, true);
            }
        }
    }

    /// The place of the first lane of the mask whose top bit is 1, and the number
    /// of lanes when none has.
    pub(super) fn vector_first_set(&self, asm: &mut Asm, result: Value, mask: Value) {
        let lane = self.lane_of(mask);
        let m = self.vector_read(asm, V30, mask);
        // The lanes whose top bit is 1 become all ones, and are narrowed to 64
        // bits, each lane to as many bits as 64 shared among the lanes.
        asm.vector_two(0x4e20_a800 | size_of(lane) << 22, V31, m);
        let (narrow, shift) = match lane.bits() {
            8 | 16 => (
                0x0f0c_8400 | V31 << 5 | V31,
                2 + u32::from(lane.bits() == 16),
            ),
            32 => (0x0f10_8400 | V31 << 5 | V31, 4),
            _ => (0x0f20_8400 | V31 << 5 | V31, 5),
        };
        asm.word(narrow);
        asm.bits_from_float(X16, 8, V31);
        // `rbit x16, x16` then `clz x16, x16`: the place of the lowest bit, or 64.
        asm.word(0xdac0_0000 | X16 << 5 | X16);
        asm.word(0xdac0_1000 | X16 << 5 | X16);
        asm.shift_right(X16, shift);
        self.finish(asm, result, X16, true);
    }
}
