//! The text of a float, written once in the scalar form so that every target
//! gives the same text: the shortest digits that read back as exactly the
//! float, by Ryu (Adams, 2018), laid out as
//! [the page on floats](../../../knowledge/language/repr.md) says.
//!
//! Ryu works from the exact interval of numbers that round to the float, and
//! needs only products of 64 bits by 128, which `MulHighU` gives, and two
//! tables of the powers of 5. The tables are program data, which the caller
//! passes. One algorithm serves `f32` and `f64`: the interval of an `f32` is
//! worked out from its own mantissa and exponent, and its digits are exactly
//! the shortest, as the 125 bits of the tables hold both.

use super::runtime::{ADDRESS, BYTE, LENGTH, TEXT, byte, int};
use super::{Binary, Bits, Builder, Compare, Function, Type, Value};

/// The number of entries of the table of the inverses of the powers of 5, and
/// of the powers of 5.
const INVERSES: usize = 342;
const POWERS: usize = 326;
/// The bits that each entry of a table holds.
const BITS: usize = 125;

/// The two tables, as the bytes of the entries, each of 16 bytes: the low word
/// and then the high word. The first is `ceil(2^(k) / 5^i)`, with `k` the
/// length of `5^i` in bits less 1, and 125 more, and the second is `5^i`
/// shifted to 125 bits, as Ryu has them.
pub fn tables() -> (Vec<u8>, Vec<u8>) {
    let mut inverses = Vec::with_capacity(INVERSES * 16);
    let mut powers = Vec::with_capacity(POWERS * 16);
    let mut pow = Big::one();
    for i in 0..INVERSES.max(POWERS) {
        let length = pow.bits();
        if i < INVERSES {
            // `2^j / 5^i + 1`, with `j = length - 1 + 125`, which is the least
            // number above the quotient.
            let j = length - 1 + BITS;
            let mut quotient = Big::one().shifted(j);
            for _ in 0..i {
                quotient = quotient.divided(5);
            }
            quotient = quotient.plus(1);
            inverses.extend_from_slice(&quotient.words());
        }
        if i < POWERS {
            let shifted = if length >= BITS {
                pow.shifted_right(length - BITS)
            } else {
                pow.shifted(BITS - length)
            };
            powers.extend_from_slice(&shifted.words());
        }
        pow = pow.times(5);
    }
    (inverses, powers)
}

/// A whole number of any size, in 32-bit limbs, least first.
#[derive(Clone)]
struct Big(Vec<u32>);

impl Big {
    fn one() -> Big {
        Big(vec![1])
    }

    fn trimmed(mut self) -> Big {
        while self.0.len() > 1 && self.0.last() == Some(&0) {
            self.0.pop();
        }
        self
    }

    fn bits(&self) -> usize {
        let top = self.0.len() - 1;
        top * 32 + (32 - self.0[top].leading_zeros() as usize)
    }

    fn times(&self, m: u32) -> Big {
        let mut out = Vec::with_capacity(self.0.len() + 1);
        let mut carry = 0u64;
        for &limb in &self.0 {
            let x = u64::from(limb) * u64::from(m) + carry;
            out.push(x as u32);
            carry = x >> 32;
        }
        out.push(carry as u32);
        Big(out).trimmed()
    }

    fn plus(&self, n: u32) -> Big {
        let mut out = self.0.clone();
        let mut carry = u64::from(n);
        for limb in &mut out {
            let x = u64::from(*limb) + carry;
            *limb = x as u32;
            carry = x >> 32;
        }
        if carry != 0 {
            out.push(carry as u32);
        }
        Big(out)
    }

    /// The quotient of the division by `d`, which is the floor.
    fn divided(&self, d: u32) -> Big {
        let mut out = vec![0u32; self.0.len()];
        let mut rest = 0u64;
        for i in (0..self.0.len()).rev() {
            let x = (rest << 32) | u64::from(self.0[i]);
            out[i] = (x / u64::from(d)) as u32;
            rest = x % u64::from(d);
        }
        Big(out).trimmed()
    }

    fn shifted(&self, by: usize) -> Big {
        let (words, bits) = (by / 32, by % 32);
        let mut out = vec![0u32; words];
        let mut carry = 0u32;
        for &limb in &self.0 {
            out.push(if bits == 0 {
                limb
            } else {
                (limb << bits) | carry
            });
            carry = if bits == 0 { 0 } else { limb >> (32 - bits) };
        }
        out.push(carry);
        Big(out).trimmed()
    }

    fn shifted_right(&self, by: usize) -> Big {
        let (words, bits) = (by / 32, by % 32);
        let mut out = Vec::new();
        for i in words..self.0.len() {
            let low = self.0[i] >> bits;
            let high = if bits == 0 {
                0
            } else {
                self.0.get(i + 1).copied().unwrap_or(0) << (32 - bits)
            };
            out.push(low | high);
        }
        if out.is_empty() {
            out.push(0);
        }
        Big(out).trimmed()
    }

    /// The low 128 bits, as 16 bytes, least first.
    fn words(&self) -> [u8; 16] {
        let mut out = [0u8; 16];
        for (i, limb) in self.0.iter().take(4).enumerate() {
            out[i * 4..i * 4 + 4].copy_from_slice(&limb.to_le_bytes());
        }
        out
    }
}

// What a function of Ryu is made of.

fn c(f: &mut Builder, n: u64) -> Value {
    int(f, n)
}

fn add(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Add, a, b)
}

fn sub(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Sub, a, b)
}

fn mul(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Mul, a, b)
}

fn and(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::And, a, b)
}

fn or(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Or, a, b)
}

fn xor(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Xor, a, b)
}

fn shl(f: &mut Builder, a: Value, n: u64) -> Value {
    let n = c(f, n);
    f.binary(Binary::ShiftLeft, a, n)
}

fn shr(f: &mut Builder, a: Value, n: u64) -> Value {
    let n = c(f, n);
    f.binary(Binary::ShiftRightU, a, n)
}

fn eq(f: &mut Builder, a: Value, b: Value) -> Value {
    f.compare(Compare::Equal, a, b)
}

fn below(f: &mut Builder, a: Value, b: Value) -> Value {
    f.compare(Compare::LessU, a, b)
}

fn below_s(f: &mut Builder, a: Value, b: Value) -> Value {
    f.compare(Compare::LessS, a, b)
}

/// Whether `a` is not 1, for a flag.
fn not(f: &mut Builder, flag: Value) -> Value {
    let one = byte(f, 1);
    xor(f, flag, one)
}

/// A flag as a number of 64 bits, 0 or 1.
fn number(f: &mut Builder, flag: Value) -> Value {
    f.widen(false, flag, Bits::B64)
}

fn div10(f: &mut Builder, a: Value) -> Value {
    let ten = c(f, 10);
    f.binary(Binary::DivU, a, ten)
}

fn rem(f: &mut Builder, a: Value, by: u64) -> Value {
    let by = c(f, by);
    f.binary(Binary::RemU, a, by)
}

/// A loop that runs `body` on the values it carries while `cond` holds of
/// them, and gives the values after the last round.
fn looping(
    f: &mut Builder,
    start: Vec<Value>,
    cond: impl FnOnce(&mut Builder, &[Value]) -> Value,
    body: impl FnOnce(&mut Builder, &[Value]) -> Vec<Value>,
) -> Vec<Value> {
    let types: Vec<Type> = start.iter().map(|v| f.type_of(*v)).collect();
    let (head, params) = f.block(&types);
    let (inside, _) = f.block(&[]);
    let (done, results) = f.block(&types);
    f.jump(head, start);
    f.at(head);
    let holds = cond(f, &params);
    f.switch(holds, vec![(done, params.clone()), (inside, vec![])]);
    f.at(inside);
    let next = body(f, &params);
    f.jump(head, next);
    f.at(done);
    results
}

/// `m * mul >> j`, where `mul` is the 128 bits `hi:lo` and `j` is between 65
/// and 127, as Ryu's `mulShift64` has it.
fn mul_shift(f: &mut Builder, m: Value, (lo, hi): (Value, Value), j: Value) -> Value {
    let first = f.binary(Binary::MulHighU, m, lo);
    let second_low = mul(f, m, hi);
    let second_high = f.binary(Binary::MulHighU, m, hi);
    let sum = add(f, first, second_low);
    let carried = below(f, sum, second_low);
    let carry = number(f, carried);
    let high = add(f, second_high, carry);
    let s64 = c(f, 64);
    let s = sub(f, j, s64);
    let low_part = f.binary(Binary::ShiftRightU, sum, s);
    let back = sub(f, s64, s);
    let high_part = f.binary(Binary::ShiftLeft, high, back);
    or(f, low_part, high_part)
}

/// Whether `value`, which is not 0, is a multiple of `5^p`.
fn multiple_of_five(f: &mut Builder, value: Value, p: Value) -> Value {
    let zero = c(f, 0);
    let found = looping(
        f,
        vec![value, zero],
        |f, v| {
            let r = rem(f, v[0], 5);
            let z = c(f, 0);
            let divisible = eq(f, r, z);
            let nonzero = eq(f, v[0], z);
            let nonzero = not(f, nonzero);
            and(f, divisible, nonzero)
        },
        |f, v| {
            let five = c(f, 5);
            let rest = f.binary(Binary::DivU, v[0], five);
            let one = c(f, 1);
            let count = add(f, v[1], one);
            vec![rest, count]
        },
    );
    let short = below(f, found[1], p);
    not(f, short)
}

/// The function that gives the shortest digits of a float and the power of 10
/// they are to be multiplied by. It takes the bits of a float that is
/// finite and not 0, as 64 bits, and the two tables, and gives the digits as
/// a number and the exponent as a signed one.
pub(super) fn shortest(double: bool) -> Function {
    let (mantissa_bits, exponent_bits, bias) = if double { (52, 11, 1023) } else { (23, 8, 127) };
    let mut f = Builder::new();
    let p = f.params(&[LENGTH, ADDRESS, ADDRESS]);
    let (bits, inverse_table, power_table) = (p[0], p[1], p[2]);
    let mantissa_mask = c(&mut f, (1 << mantissa_bits) - 1);
    let ieee_m = and(&mut f, bits, mantissa_mask);
    let shifted = shr(&mut f, bits, mantissa_bits);
    let exponent_mask = c(&mut f, (1 << exponent_bits) - 1);
    let ieee_e = and(&mut f, shifted, exponent_mask);
    let zero = c(&mut f, 0);
    let is_subnormal = eq(&mut f, ieee_e, zero);
    let offset = c(&mut f, bias + mantissa_bits + 2);
    let normal_e2 = sub(&mut f, ieee_e, offset);
    let subnormal_e2 = c(
        &mut f,
        (1i64 - bias as i64 - mantissa_bits as i64 - 2) as u64,
    );
    let e2 = f.choose(is_subnormal, subnormal_e2, normal_e2);
    let hidden = c(&mut f, 1 << mantissa_bits);
    let with_hidden = or(&mut f, ieee_m, hidden);
    let m2 = f.choose(is_subnormal, ieee_m, with_hidden);
    let low_bit = rem(&mut f, m2, 2);
    let accept = eq(&mut f, low_bit, zero);
    let mv = shl(&mut f, m2, 2);
    let two = c(&mut f, 2);
    let small_exponent = below(&mut f, ieee_e, two);
    let has_mantissa = eq(&mut f, ieee_m, zero);
    let has_mantissa = not(&mut f, has_mantissa);
    let shift_flag = or(&mut f, has_mantissa, small_exponent);
    let mm_shift = number(&mut f, shift_flag);
    // The values the two branches give: the digits of the float, of the
    // number above it and of the number below it, as far as they are
    // known, the exponent, and whether the digits are zeros past where they
    // are exact.
    let (join, joined) = f.block(&[LENGTH, LENGTH, LENGTH, LENGTH, BYTE, BYTE]);
    let (positive, _) = f.block(&[]);
    let (negative, _) = f.block(&[]);
    let non_negative = below_s(&mut f, e2, zero);
    f.switch(non_negative, vec![(positive, vec![]), (negative, vec![])]);
    // The exponent is 0 or more.
    f.at(positive);
    {
        let t = {
            let m = c(&mut f, 78913);
            let x = mul(&mut f, e2, m);
            shr(&mut f, x, 18)
        };
        let three = c(&mut f, 3);
        let above_three = below(&mut f, three, e2);
        let above_three = number(&mut f, above_three);
        let q = sub(&mut f, t, above_three);
        let bits_of_power = {
            let m = c(&mut f, 1217359);
            let x = mul(&mut f, q, m);
            shr(&mut f, x, 19)
        };
        let k = c(&mut f, BITS as u64);
        let k = add(&mut f, k, bits_of_power);
        let i = add(&mut f, q, k);
        let i = sub(&mut f, i, e2);
        let sixteen = c(&mut f, 16);
        let at = mul(&mut f, q, sixteen);
        let address = add(&mut f, inverse_table, at);
        let lo = f.load(Type::Int(Bits::B64), address);
        let eight = c(&mut f, 8);
        let high_address = add(&mut f, address, eight);
        let hi = f.load(Type::Int(Bits::B64), high_address);
        let vr = mul_shift(&mut f, mv, (lo, hi), i);
        let two = c(&mut f, 2);
        let up = add(&mut f, mv, two);
        let mut vp = mul_shift(&mut f, up, (lo, hi), i);
        let one = c(&mut f, 1);
        let down = sub(&mut f, mv, one);
        let down = sub(&mut f, down, mm_shift);
        let vm = mul_shift(&mut f, down, (lo, hi), i);
        // For a quotient of 21 or less, whether a number is a multiple of
        // `5^q` says whether the digits are zeros past where they are exact.
        let limit = c(&mut f, 22);
        let small = below(&mut f, q, limit);
        let five_rest = rem(&mut f, mv, 5);
        let divisible = eq(&mut f, five_rest, zero);
        let lower = sub(&mut f, mv, one);
        let lower = sub(&mut f, lower, mm_shift);
        let upper = add(&mut f, mv, two);
        let other = f.choose(accept, lower, upper);
        let tested = f.choose(divisible, mv, other);
        let multiple = multiple_of_five(&mut f, tested, q);
        let not_divisible = not(&mut f, divisible);
        let not_accept = not(&mut f, accept);
        let a = and(&mut f, small, divisible);
        let vr_zeros = and(&mut f, a, multiple);
        let a = and(&mut f, small, not_divisible);
        let a2 = and(&mut f, a, accept);
        let vm_zeros = and(&mut f, a2, multiple);
        let a3 = and(&mut f, a, not_accept);
        let step = and(&mut f, a3, multiple);
        let step = number(&mut f, step);
        vp = sub(&mut f, vp, step);
        f.jump(join, vec![vr, vp, vm, q, vr_zeros, vm_zeros]);
    }
    // The exponent is below 0.
    f.at(negative);
    {
        let ne2 = sub(&mut f, zero, e2);
        let t = {
            let m = c(&mut f, 732923);
            let x = mul(&mut f, ne2, m);
            shr(&mut f, x, 20)
        };
        let one = c(&mut f, 1);
        let above_one = below(&mut f, one, ne2);
        let above_one = number(&mut f, above_one);
        let q = sub(&mut f, t, above_one);
        let e10 = add(&mut f, q, e2);
        let i = sub(&mut f, ne2, q);
        let bits_of_power = {
            let m = c(&mut f, 1217359);
            let x = mul(&mut f, i, m);
            shr(&mut f, x, 19)
        };
        // `j = q - (pow5bits(i) - 125)`.
        let base = c(&mut f, BITS as u64 - 1);
        let j = add(&mut f, q, base);
        let j = sub(&mut f, j, bits_of_power);
        let sixteen = c(&mut f, 16);
        let at = mul(&mut f, i, sixteen);
        let address = add(&mut f, power_table, at);
        let lo = f.load(Type::Int(Bits::B64), address);
        let eight = c(&mut f, 8);
        let high_address = add(&mut f, address, eight);
        let hi = f.load(Type::Int(Bits::B64), high_address);
        let vr = mul_shift(&mut f, mv, (lo, hi), j);
        let two = c(&mut f, 2);
        let up = add(&mut f, mv, two);
        let mut vp = mul_shift(&mut f, up, (lo, hi), j);
        let down = sub(&mut f, mv, one);
        let down = sub(&mut f, down, mm_shift);
        let vm = mul_shift(&mut f, down, (lo, hi), j);
        // A quotient of 1 or less is exact, and a larger one is exact when the
        // low bits of `mv` are zeros.
        let two = c(&mut f, 2);
        let tiny = below(&mut f, q, two);
        let limit = c(&mut f, 63);
        let small = below(&mut f, q, limit);
        let one_less = sub(&mut f, q, one);
        let ones = {
            let bit = f.binary(Binary::ShiftLeft, one, one_less);
            sub(&mut f, bit, one)
        };
        let low_bits = and(&mut f, mv, ones);
        let low_zero = eq(&mut f, low_bits, zero);
        let wide = and(&mut f, small, low_zero);
        let vr_zeros = or(&mut f, tiny, wide);
        let shifted_only = eq(&mut f, mm_shift, one);
        let not_accept = not(&mut f, accept);
        let a = and(&mut f, tiny, accept);
        let vm_zeros = and(&mut f, a, shifted_only);
        let a = and(&mut f, tiny, not_accept);
        let step = number(&mut f, a);
        vp = sub(&mut f, vp, step);
        f.jump(join, vec![vr, vp, vm, e10, vr_zeros, vm_zeros]);
    }
    f.at(join);
    // Digits are taken off the three while the number above and the number
    // below still differ in them.
    let (vr, vp, vm, e10, vr_zeros, vm_zeros) = (
        joined[0], joined[1], joined[2], joined[3], joined[4], joined[5],
    );
    let first = looping(
        &mut f,
        vec![vr, vp, vm, zero, zero, vm_zeros, vr_zeros],
        |f, v| {
            let above = div10(f, v[1]);
            let below_ = div10(f, v[2]);
            below(f, below_, above)
        },
        |f, v| {
            let r = rem(f, v[2], 10);
            let z = c(f, 0);
            let vm_ends = eq(f, r, z);
            let vm_zeros = and(f, v[5], vm_ends);
            let last_zero = eq(f, v[4], z);
            let vr_zeros = and(f, v[6], last_zero);
            let last = rem(f, v[0], 10);
            let (vr, vp, vm) = (div10(f, v[0]), div10(f, v[1]), div10(f, v[2]));
            let one = c(f, 1);
            let removed = add(f, v[3], one);
            vec![vr, vp, vm, removed, last, vm_zeros, vr_zeros]
        },
    );
    let second = looping(
        &mut f,
        first,
        |f, v| {
            let r = rem(f, v[2], 10);
            let z = c(f, 0);
            let ends = eq(f, r, z);
            and(f, ends, v[5])
        },
        |f, v| {
            let z = c(f, 0);
            let last_zero = eq(f, v[4], z);
            let vr_zeros = and(f, v[6], last_zero);
            let last = rem(f, v[0], 10);
            let (vr, vp, vm) = (div10(f, v[0]), div10(f, v[1]), div10(f, v[2]));
            let one = c(f, 1);
            let removed = add(f, v[3], one);
            vec![vr, vp, vm, removed, last, v[5], vr_zeros]
        },
    );
    let (vr, vm, removed, last, vm_zeros, vr_zeros) = (
        second[0], second[2], second[3], second[4], second[5], second[6],
    );
    // A tie at an exact 5 goes to the even digit.
    let five = c(&mut f, 5);
    let is_five = eq(&mut f, last, five);
    let even = rem(&mut f, vr, 2);
    let is_even = eq(&mut f, even, zero);
    let tie = and(&mut f, vr_zeros, is_five);
    let tie = and(&mut f, tie, is_even);
    let four = c(&mut f, 4);
    let last = f.choose(tie, four, last);
    let same = eq(&mut f, vr, vm);
    let not_accept = not(&mut f, accept);
    let not_zeros = not(&mut f, vm_zeros);
    let open = or(&mut f, not_accept, not_zeros);
    let down = and(&mut f, same, open);
    let up = below(&mut f, four, last);
    let round = or(&mut f, down, up);
    let round = number(&mut f, round);
    let output = add(&mut f, vr, round);
    let exponent = add(&mut f, e10, removed);
    f.ret(vec![output, exponent]);
    f.finish(&[LENGTH, LENGTH])
}

/// The function that gives the text of a float. It takes the bits of the float
/// as 64 bits, which for an `f32` are the 32 of it, the two tables, and the
/// place of `shortest`, and gives the text.
///
/// A float that is not a number is `nan`, an infinity is `inf` or `-inf`, a
/// zero is `0.0` or `-0.0`. Any other float is its shortest digits: with a
/// point and every digit between `10^-5` and `10^16`, always with a digit on
/// each side of the point, and otherwise as `d.ddde-x`, which has no point
/// when there is one digit. So every text but the first two is a float that
/// is written out, and reads back as exactly the float.
pub(super) fn float_text(double: bool, shortest: usize) -> Function {
    let (mantissa_bits, exponent_bits) = if double { (52u64, 11u64) } else { (23, 8) };
    let mut f = Builder::new();
    let p = f.params(&[LENGTH, ADDRESS, ADDRESS]);
    let (bits, inverse_table, power_table) = (p[0], p[1], p[2]);
    let zero = c(&mut f, 0);
    let one = c(&mut f, 1);
    let sign = shr(&mut f, bits, mantissa_bits + exponent_bits);
    let mantissa_mask = c(&mut f, (1 << mantissa_bits) - 1);
    let ieee_m = and(&mut f, bits, mantissa_mask);
    let shifted = shr(&mut f, bits, mantissa_bits);
    let exponent_mask = c(&mut f, (1 << exponent_bits) - 1);
    let ieee_e = and(&mut f, shifted, exponent_mask);
    let is_special = eq(&mut f, ieee_e, exponent_mask);
    let mantissa_zero = eq(&mut f, ieee_m, zero);
    let exponent_zero = eq(&mut f, ieee_e, zero);
    let is_zero = and(&mut f, exponent_zero, mantissa_zero);
    let (special, _) = f.block(&[]);
    let (number_block, _) = f.block(&[]);
    let (not_special, _) = f.block(&[]);
    f.switch(is_special, vec![(not_special, vec![]), (special, vec![])]);
    // A float that is not a number or is an infinity.
    f.at(special);
    {
        let (nan, _) = f.block(&[]);
        let (inf, _) = f.block(&[]);
        f.switch(mantissa_zero, vec![(nan, vec![]), (inf, vec![])]);
        f.at(nan);
        let text = write_text(&mut f, b"nan", None);
        f.ret(text);
        f.at(inf);
        let text = write_text(&mut f, b"inf", Some(sign));
        f.ret(text);
    }
    f.at(not_special);
    {
        let (zero_block, _) = f.block(&[]);
        f.switch(is_zero, vec![(number_block, vec![]), (zero_block, vec![])]);
        f.at(zero_block);
        let text = write_text(&mut f, b"0.0", Some(sign));
        f.ret(text);
    }
    f.at(number_block);
    let result = f.call(
        shortest,
        vec![bits, inverse_table, power_table],
        &[LENGTH, LENGTH],
    );
    let (output, exp) = (result[0], result[1]);
    // The digits, and how many there are.
    let counted = looping(
        &mut f,
        vec![output, zero],
        |f, v| {
            let z = c(f, 0);
            // At least one digit, whatever the number.
            let done = eq(f, v[0], z);
            let first = eq(f, v[1], z);
            let stop = not(f, first);
            let stop = and(f, stop, done);
            not(f, stop)
        },
        |f, v| {
            let rest = div10(f, v[0]);
            let one = c(f, 1);
            let count = add(f, v[1], one);
            vec![rest, count]
        },
    );
    let n = counted[1];
    let tmp = f.allocate(n);
    // They are written from the last.
    let written = looping(
        &mut f,
        vec![output, n],
        |f, v| {
            let z = c(f, 0);
            let done = eq(f, v[1], z);
            not(f, done)
        },
        |f, v| {
            let one = c(f, 1);
            let at = sub(f, v[1], one);
            let digit = rem(f, v[0], 10);
            let low = f.narrow(digit, Bits::B8);
            let zero_char = byte(f, b'0');
            let character = add(f, low, zero_char);
            let place = add(f, tmp, at);
            f.store(place, character);
            let rest = div10(f, v[0]);
            vec![rest, at]
        },
    );
    let _ = written;
    // The exponent of the first digit.
    let x = add(&mut f, exp, n);
    let x = sub(&mut f, x, one);
    let minus_six = c(&mut f, (-6i64) as u64);
    let seventeen = c(&mut f, 17);
    let above = below_s(&mut f, minus_six, x);
    let under = below_s(&mut f, x, seventeen);
    let positional = and(&mut f, above, under);
    let negative_x = below_s(&mut f, x, zero);
    let n_above_one = below(&mut f, one, n);
    let neg_number = number(&mut f, negative_x);
    // The two lengths of a text with a point and the length of one without.
    let int_length = add(&mut f, x, one);
    let wide = below(&mut f, int_length, n);
    let with_point = add(&mut f, n, one);
    let without = {
        let two = c(&mut f, 2);
        add(&mut f, int_length, two)
    };
    let big_len = f.choose(wide, with_point, without);
    let small_len = {
        let a = add(&mut f, n, one);
        sub(&mut f, a, x)
    };
    let positional_len = f.choose(negative_x, small_len, big_len);
    // Without a point: `d.ddd` then `e`, the sign of the exponent, and its
    // digits.
    let magnitude = {
        let negated = sub(&mut f, zero, x);
        f.choose(negative_x, negated, x)
    };
    let ten = c(&mut f, 10);
    let hundred = c(&mut f, 100);
    let two_digits = below(&mut f, magnitude, ten);
    let two_digits = not(&mut f, two_digits);
    let three_digits = below(&mut f, magnitude, hundred);
    let three_digits = not(&mut f, three_digits);
    let two_digits_n = number(&mut f, two_digits);
    let three_digits_n = number(&mut f, three_digits);
    let digits_len = add(&mut f, one, two_digits_n);
    let digits_len = add(&mut f, digits_len, three_digits_n);
    let point_len = number(&mut f, n_above_one);
    let scientific_len = {
        let a = add(&mut f, n, point_len);
        let a = add(&mut f, a, one);
        let a = add(&mut f, a, neg_number);
        add(&mut f, a, digits_len)
    };
    let body_len = f.choose(positional, positional_len, scientific_len);
    let sign_number = and(&mut f, sign, one);
    let total = add(&mut f, body_len, sign_number);
    let start = f.allocate(total);
    // The character at each place, from the first.
    let e_head = {
        let a = add(&mut f, n, one);
        f.choose(n_above_one, a, one)
    };
    let int_zeros = {
        // The zeros after the point of a number below 1.
        let negated = sub(&mut f, zero, x);
        sub(&mut f, negated, one)
    };
    looping(
        &mut f,
        vec![zero],
        |f, v| below(f, v[0], total),
        |f, v| {
            let i = v[0];
            let is_sign = below(f, i, sign_number);
            let q = sub(f, i, sign_number);
            let digit = |f: &mut Builder, index: Value| {
                let valid = below(f, index, n);
                let safe = f.choose(valid, index, zero);
                let place = add(f, tmp, safe);
                f.load(BYTE, place)
            };
            let dot = byte(f, b'.');
            let nought = byte(f, b'0');
            // A number of 1 or more written with a point.
            let in_int = below(f, q, int_length);
            let q_digit = digit(f, q);
            let q_has = below(f, q, n);
            let inside = f.choose(q_has, q_digit, nought);
            let at_point = eq(f, q, int_length);
            let prior = sub(f, q, one);
            let after_digit = digit(f, prior);
            let after = f.choose(wide, after_digit, nought);
            let past = f.choose(at_point, dot, after);
            let big = f.choose(in_int, inside, past);
            // A number below 1.
            let at_zero = eq(f, q, zero);
            let at_one = eq(f, q, one);
            let two = c(f, 2);
            let zeros_end = add(f, int_zeros, two);
            let in_zeros = below(f, q, zeros_end);
            let fraction_index = sub(f, q, zeros_end);
            let fraction = digit(f, fraction_index);
            let inner = f.choose(in_zeros, nought, fraction);
            let inner = f.choose(at_one, dot, inner);
            let small = f.choose(at_zero, nought, inner);
            let plain = f.choose(negative_x, small, big);
            // A number with an exponent.
            let first = digit(f, zero);
            let before = sub(f, q, one);
            let mid = digit(f, before);
            let at_dot = and(f, n_above_one, at_one);
            let in_digits = below(f, q, e_head);
            let at_e = eq(f, q, e_head);
            let e_char = byte(f, b'e');
            let past_e = sub(f, q, e_head);
            let past_e = sub(f, past_e, one);
            let exponent_place = sub(f, past_e, neg_number);
            let minus = byte(f, b'-');
            let at_first = eq(f, past_e, zero);
            let at_minus = and(f, negative_x, at_first);
            // The digit of the magnitude at a place, of `digits_len` of them.
            let from_end = {
                let last = sub(f, digits_len, one);
                sub(f, last, exponent_place)
            };
            let one_place = eq(f, from_end, zero);
            let ten_place = eq(f, from_end, one);
            let power = {
                let inner = f.choose(ten_place, ten, hundred);
                f.choose(one_place, one, inner)
            };
            let shifted_value = f.binary(Binary::DivU, magnitude, power);
            let low_digit = rem(f, shifted_value, 10);
            let low_digit = f.narrow(low_digit, Bits::B8);
            let character = add(f, low_digit, nought);
            let exponent_char = f.choose(at_minus, minus, character);
            let after_e = f.choose(at_e, e_char, exponent_char);
            let heads = f.choose(in_digits, mid, after_e);
            let heads = f.choose(at_dot, dot, heads);
            let scientific = f.choose(at_zero, first, heads);
            let body = f.choose(positional, plain, scientific);
            let character = f.choose(is_sign, minus, body);
            let place = add(f, start, i);
            f.store(place, character);
            let next = add(f, i, one);
            vec![next]
        },
    );
    f.ret(vec![start, total]);
    f.finish(&TEXT)
}

/// Writes `text` into new memory, after a `-` when `sign` is given and its
/// value is not 0, and gives the text.
fn write_text(f: &mut Builder, text: &[u8], sign: Option<Value>) -> Vec<Value> {
    let zero = c(f, 0);
    let one = c(f, 1);
    let minus_length = match sign {
        Some(s) => and(f, s, one),
        None => zero,
    };
    let length = c(f, text.len() as u64);
    let total = add(f, length, minus_length);
    let start = f.allocate(total);
    if sign.is_some() {
        let (put, _) = f.block(&[]);
        let (go_on, _) = f.block(&[]);
        let flag = f.narrow(minus_length, Bits::B8);
        f.switch(flag, vec![(go_on, vec![]), (put, vec![])]);
        f.at(put);
        let minus = byte(f, b'-');
        f.store(start, minus);
        f.jump(go_on, vec![]);
        f.at(go_on);
    }
    for (k, b) in text.iter().enumerate() {
        let k = c(f, k as u64);
        let offset = add(f, minus_length, k);
        let place = add(f, start, offset);
        let value = byte(f, *b);
        f.store(place, value);
    }
    vec![start, total]
}
