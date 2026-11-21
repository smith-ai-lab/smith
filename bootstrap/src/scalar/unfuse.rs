//! A fused product given as a call, for a target that has no instruction that
//! works one out.
//!
//! `a * b + c` is worked out exactly and rounded once, as
//! [the rules floats keep](../../../knowledge/language/float-rules.md) say, and
//! a target whose processor has no instruction for it, as WebAssembly has
//! none, must give the same bits as one that has. Each fused instruction is
//! taken apart into a call of a function of the scalar form that does it
//! with whole numbers: the product is 106 bits, the sum is 128 bits and a bit
//! that says whether anything was left below them, and the result is rounded
//! to the nearest, ties to even, with the gradual underflow and the overflow
//! to infinity that IEEE 754 gives.

use super::{
    Binary, Bits, Builder, Compare, Constant, FloatBinary, FloatBits, Function, Fused, Instruction,
    Op, Program, Type, Value,
};

const ADDRESS: Type = Type::Address;
const LENGTH: Type = Type::Int(Bits::B64);

/// The program with each fused instruction a call, or the program itself
/// when it has none. A function for each size that is fused is added at the
/// end, and one place of data that those functions use as a scratch
/// word.
pub fn unfuse(program: &Program) -> Option<Program> {
    let sizes = |size: FloatBits| {
        (program.functions.iter()).any(|f| {
            f.blocks.iter().any(|b| {
                b.instructions.iter().any(|i| {
                    matches!(&i.op, Op::FloatFused { a, .. }
                        if f.types[a.0] == Type::Float(size))
                })
            })
        })
    };
    let (single, double) = (sizes(FloatBits::F32), sizes(FloatBits::F64));
    if !single && !double {
        return None;
    }
    let mut out = program.clone();
    let scratch = out.data.len();
    out.data.push(vec![0; 8]);
    let mut at = [None, None];
    for (k, (used, bits)) in [(single, FloatBits::F32), (double, FloatBits::F64)]
        .into_iter()
        .enumerate()
    {
        if used {
            at[k] = Some(out.functions.len());
            out.functions.push(fused_function(bits));
        }
    }
    let originals = program.functions.len();
    for f in &mut out.functions[..originals] {
        let mut types = std::mem::take(&mut f.types);
        for block in &mut f.blocks {
            let mut instructions = Vec::with_capacity(block.instructions.len());
            for instruction in std::mem::take(&mut block.instructions) {
                let Op::FloatFused { op, a, b, c } = instruction.op else {
                    instructions.push(instruction);
                    continue;
                };
                let ty = types[a.0];
                let function = at[usize::from(ty == Type::Float(FloatBits::F64))]
                    .expect("a function for the size");
                let fresh = |ty: Type, types: &mut Vec<Type>| {
                    types.push(ty);
                    Value(types.len() - 1)
                };
                let place = instruction.at;
                let address = fresh(ADDRESS, &mut types);
                instructions.push(Instruction {
                    results: vec![address],
                    op: Op::Constant(Constant::Data(scratch)),
                    at: place,
                });
                // `a * b - c` is `a * b + (-c)`, and `c - a * b` is
                // `(-a) * b + c`, as the same exact number.
                let (a, c) = match op {
                    Fused::MulAdd => (a, c),
                    Fused::MulSub => {
                        let negated = fresh(ty, &mut types);
                        instructions.push(Instruction {
                            results: vec![negated],
                            op: Op::FloatNegate(c),
                            at: place,
                        });
                        (a, negated)
                    }
                    Fused::NegMulAdd => {
                        let negated = fresh(ty, &mut types);
                        instructions.push(Instruction {
                            results: vec![negated],
                            op: Op::FloatNegate(a),
                            at: place,
                        });
                        (negated, c)
                    }
                };
                instructions.push(Instruction {
                    results: instruction.results,
                    op: Op::Call {
                        function,
                        args: vec![a, b, c, address],
                    },
                    at: place,
                });
            }
            block.instructions = instructions;
        }
        f.types = types;
    }
    Some(out)
}

// What the function is made of.

fn c(f: &mut Builder, n: u64) -> Value {
    f.int(Bits::B64, n)
}

fn add(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Add, a, b)
}

fn sub(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Sub, a, b)
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

fn shl(f: &mut Builder, a: Value, n: Value) -> Value {
    f.binary(Binary::ShiftLeft, a, n)
}

fn shr(f: &mut Builder, a: Value, n: Value) -> Value {
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

fn flag_not(f: &mut Builder, x: Value) -> Value {
    let one = f.int(Bits::B8, 1);
    f.binary(Binary::Xor, x, one)
}

fn flag_and(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::And, a, b)
}

fn flag_or(f: &mut Builder, a: Value, b: Value) -> Value {
    f.binary(Binary::Or, a, b)
}

/// A flag as 0 or 1 of 64 bits.
fn number(f: &mut Builder, flag: Value) -> Value {
    f.widen(false, flag, Bits::B64)
}

/// Whether a number is not 0.
fn nonzero(f: &mut Builder, x: Value) -> Value {
    let zero = c(f, 0);
    let is_zero = eq(f, x, zero);
    flag_not(f, is_zero)
}

/// A number of 128 bits: the high word and the low.
#[derive(Clone, Copy)]
struct Wide {
    high: Value,
    low: Value,
}

/// The number of leading zero bits of a number of 64 bits.
fn clz64(f: &mut Builder, x: Value) -> Value {
    let mut y = x;
    let mut n = c(f, 0);
    for step in [32u64, 16, 8, 4, 2, 1] {
        let up = c(f, 64 - step);
        let top = shr(f, y, up);
        let zero = c(f, 0);
        let empty = eq(f, top, zero);
        let by = c(f, step);
        let moved = shl(f, y, by);
        y = f.choose(empty, moved, y);
        let more = add(f, n, by);
        n = f.choose(empty, more, n);
    }
    let zero = c(f, 0);
    let is_zero = eq(f, x, zero);
    let one = number(f, is_zero);
    add(f, n, one)
}

fn clz128(f: &mut Builder, x: Wide) -> Value {
    let zero = c(f, 0);
    let high_zero = eq(f, x.high, zero);
    let from_high = clz64(f, x.high);
    let from_low = clz64(f, x.low);
    let sixty_four = c(f, 64);
    let past = add(f, sixty_four, from_low);
    f.choose(high_zero, past, from_high)
}

/// `x` moved up by `n`, which is below 128.
fn shl128(f: &mut Builder, x: Wide, n: Value) -> Wide {
    let zero = c(f, 0);
    let sixty_four = c(f, 64);
    let wide = below(f, n, sixty_four);
    let wide = flag_not(f, wide);
    let k = {
        let mask = c(f, 63);
        and(f, n, mask)
    };
    let back = sub(f, sixty_four, k);
    let carried = shr(f, x.low, back);
    let k_zero = eq(f, k, zero);
    let carried = f.choose(k_zero, zero, carried);
    let moved_high = shl(f, x.high, k);
    let small_high = or(f, moved_high, carried);
    let small_low = shl(f, x.low, k);
    let big_high = shl(f, x.low, k);
    let high = f.choose(wide, big_high, small_high);
    let low = f.choose(wide, zero, small_low);
    Wide { high, low }
}

/// `x` moved down by `n`, which may be any number, and whether any bit that
/// was moved out was 1.
fn shr128(f: &mut Builder, x: Wide, n: Value) -> (Wide, Value) {
    let zero = c(f, 0);
    let sixty_four = c(f, 64);
    let hundred_twenty_eight = c(f, 128);
    let small = below(f, n, sixty_four);
    let all = below(f, n, hundred_twenty_eight);
    let all = flag_not(f, all);
    let k = {
        let mask = c(f, 63);
        and(f, n, mask)
    };
    let k_zero = eq(f, k, zero);
    let back = sub(f, sixty_four, k);
    // Below 64: both words move, and what leaves the low word is lost.
    let into_low = shl(f, x.high, back);
    let into_low = f.choose(k_zero, zero, into_low);
    let small_low = {
        let moved = shr(f, x.low, k);
        or(f, moved, into_low)
    };
    let small_high = shr(f, x.high, k);
    let lost_low = shl(f, x.low, back);
    let lost_low = f.choose(k_zero, zero, lost_low);
    let small_lost = nonzero(f, lost_low);
    // From 64 to 127: the high word moves into the low, by `k`.
    let big_low = shr(f, x.high, k);
    let lost_high = shl(f, x.high, back);
    let lost_high = f.choose(k_zero, zero, lost_high);
    let lost_high_bits = nonzero(f, lost_high);
    let lost_low_word = nonzero(f, x.low);
    let big_lost = flag_or(f, lost_high_bits, lost_low_word);
    // 128 and more: nothing is left, and a bit was lost unless there was none.
    let any = {
        let a = or(f, x.high, x.low);
        nonzero(f, a)
    };
    let low = f.choose(small, small_low, big_low);
    let high = f.choose(small, small_high, zero);
    let lost = f.choose(small, small_lost, big_lost);
    let low = f.choose(all, zero, low);
    let high = f.choose(all, zero, high);
    let lost = f.choose(all, any, lost);
    (Wide { high, low }, lost)
}

/// `a + b`, and the carry out of the 128 bits.
fn add128(f: &mut Builder, a: Wide, b: Wide) -> (Wide, Value) {
    let low = add(f, a.low, b.low);
    let carry_low = below(f, low, a.low);
    let carry_low = number(f, carry_low);
    let high = add(f, a.high, b.high);
    let carry_a = below(f, high, a.high);
    let high2 = add(f, high, carry_low);
    let carry_b = below(f, high2, high);
    let carry = flag_or(f, carry_a, carry_b);
    (Wide { high: high2, low }, carry)
}

/// `a - b`, where `a` is not below `b`.
fn sub128(f: &mut Builder, a: Wide, b: Wide) -> Wide {
    let low = sub(f, a.low, b.low);
    let borrow = below(f, a.low, b.low);
    let borrow = number(f, borrow);
    let high = sub(f, a.high, b.high);
    let high = sub(f, high, borrow);
    Wide { high, low }
}

/// Whether `a` is below `b`.
fn below128(f: &mut Builder, a: Wide, b: Wide) -> Value {
    let high_below = below(f, a.high, b.high);
    let high_same = eq(f, a.high, b.high);
    let low_below = below(f, a.low, b.low);
    let same_and = flag_and(f, high_same, low_below);
    flag_or(f, high_below, same_and)
}

/// The function that works out `a * b + c` of floats of one size, rounded
/// once. It takes the three floats and the address of 8 bytes it may use,
/// and gives the float.
fn fused_function(size: FloatBits) -> Function {
    let double = size == FloatBits::F64;
    let (mantissa_bits, exponent_bits, bias) = if double {
        (52u64, 11u64, 1023i64)
    } else {
        (23, 8, 127)
    };
    let float = Type::Float(size);
    let mut f = Builder::new();
    let p = f.params(&[float, float, float, ADDRESS]);
    let (fa, fb, fc, scratch) = (p[0], p[1], p[2], p[3]);
    // The bits of a float, through the memory that is given.
    let bits_of = |f: &mut Builder, x: Value| -> Value {
        f.store(scratch, x);
        if double {
            f.load(LENGTH, scratch)
        } else {
            let narrow = f.load(Type::Int(Bits::B32), scratch);
            f.widen(false, narrow, Bits::B64)
        }
    };
    let (ba, bb, bc) = (
        bits_of(&mut f, fa),
        bits_of(&mut f, fb),
        bits_of(&mut f, fc),
    );
    let zero = c(&mut f, 0);
    let one = c(&mut f, 1);
    let mantissa_mask = c(&mut f, (1 << mantissa_bits) - 1);
    let exponent_mask = c(&mut f, (1 << exponent_bits) - 1);
    let mut parts = Vec::new();
    for bits in [ba, bb, bc] {
        let m = and(&mut f, bits, mantissa_mask);
        let by = c(&mut f, mantissa_bits);
        let moved = shr(&mut f, bits, by);
        let e = and(&mut f, moved, exponent_mask);
        let by = c(&mut f, mantissa_bits + exponent_bits);
        let moved = shr(&mut f, bits, by);
        let s = and(&mut f, moved, one);
        parts.push((s, e, m));
    }
    // Whether any is not a number or infinite, or is 0, which the operations
    // of floats give exactly.
    let mut special = f.int(Bits::B8, 0);
    let mut zeros = Vec::new();
    for &(_, e, m) in &parts {
        let top = eq(&mut f, e, exponent_mask);
        let e_zero = eq(&mut f, e, zero);
        let m_zero = eq(&mut f, m, zero);
        let is_zero = flag_and(&mut f, e_zero, m_zero);
        special = flag_or(&mut f, special, top);
        special = flag_or(&mut f, special, is_zero);
        zeros.push(is_zero);
    }
    let (general, general_params) = f.block(&[]);
    let _ = general_params;
    let (simple, _) = f.block(&[]);
    f.switch(special, vec![(general, vec![]), (simple, vec![])]);
    // One of the three is not a finite number other than 0. The result of the
    // operations is the right one, but for `c` being 0, where the sum of two
    // zeros of two signs would take the sign of the product away.
    f.at(simple);
    {
        let product = f.float_binary(FloatBinary::Mul, fa, fb);
        let sum = f.float_binary(FloatBinary::Add, product, fc);
        let (a_zero, b_zero, c_zero) = (zeros[0], zeros[1], zeros[2]);
        let either = flag_or(&mut f, a_zero, b_zero);
        let finite_ab = {
            let a_top = eq(&mut f, parts[0].1, exponent_mask);
            let b_top = eq(&mut f, parts[1].1, exponent_mask);
            let any = flag_or(&mut f, a_top, b_top);
            flag_not(&mut f, any)
        };
        let c_finite = {
            let top = eq(&mut f, parts[2].1, exponent_mask);
            flag_not(&mut f, top)
        };
        // `c` is 0 and the product is a finite number other than 0.
        let no_zero_factor = flag_not(&mut f, either);
        let only_c = flag_and(&mut f, c_zero, no_zero_factor);
        let only_c = flag_and(&mut f, only_c, finite_ab);
        let only_c = flag_and(&mut f, only_c, c_finite);
        let chosen = f.choose(only_c, product, sum);
        // A sum with a `c` that is not a number or is infinite, of a product
        // that is a finite number, is `c`, though the product may be too great
        // for a float of this size.
        let c_top = flag_not(&mut f, c_finite);
        let c_alone = flag_and(&mut f, c_top, finite_ab);
        let result = f.choose(c_alone, fc, chosen);
        f.ret(vec![result]);
    }
    f.at(general);
    let result_bits = {
        let (sa, ea, ma) = parts[0];
        let (sb, eb, mb) = parts[1];
        let (sc, ec, mc) = parts[2];
        let hidden = c(&mut f, 1 << mantissa_bits);
        let bias_c = c(&mut f, (bias + mantissa_bits as i64) as u64);
        let smallest = c(&mut f, (1 - bias - mantissa_bits as i64) as u64);
        // Each is `m * 2^e`: a number with the 1 that is not written where it
        // is not subnormal.
        let mut unpacked = Vec::new();
        for (e, m) in [(ea, ma), (eb, mb), (ec, mc)] {
            let sub_normal = eq(&mut f, e, zero);
            let with = or(&mut f, m, hidden);
            let m = f.choose(sub_normal, m, with);
            let shifted = sub(&mut f, e, bias_c);
            let e = f.choose(sub_normal, smallest, shifted);
            unpacked.push((m, e));
        }
        let ((ma, ea), (mb, eb), (mc, ec)) = (unpacked[0], unpacked[1], unpacked[2]);
        // The product, of up to 106 bits, put with its first 1 at the top.
        let product = Wide {
            high: f.binary(Binary::MulHighU, ma, mb),
            low: f.binary(Binary::Mul, ma, mb),
        };
        let product_sign = xor(&mut f, sa, sb);
        let product_exponent = add(&mut f, ea, eb);
        let lead = clz128(&mut f, product);
        let product = shl128(&mut f, product, lead);
        // The exponent of the first bit: the number is that many and the
        // bits below, so the first of 128 is at 127 above the exponent of the
        // lowest.
        let top = c(&mut f, 127);
        let place = sub(&mut f, top, lead);
        let product_at = add(&mut f, product_exponent, place);
        // The addend likewise.
        let addend = Wide {
            high: zero,
            low: mc,
        };
        let lead_c = clz128(&mut f, addend);
        let addend = shl128(&mut f, addend, lead_c);
        let place_c = sub(&mut f, top, lead_c);
        let addend_at = add(&mut f, ec, place_c);
        // The greater of the two in magnitude, then the other moved down.
        let at_greater = below_s(&mut f, addend_at, product_at);
        let at_same = eq(&mut f, addend_at, product_at);
        let at_least = {
            let less = below128(&mut f, product, addend);
            flag_not(&mut f, less)
        };
        let product_is_greater = {
            let tie = flag_and(&mut f, at_same, at_least);
            flag_or(&mut f, at_greater, tie)
        };
        let pick = |f: &mut Builder, on: Value, a: Value, b: Value| f.choose(on, a, b);
        let big = Wide {
            high: pick(&mut f, product_is_greater, product.high, addend.high),
            low: pick(&mut f, product_is_greater, product.low, addend.low),
        };
        let little = Wide {
            high: pick(&mut f, product_is_greater, addend.high, product.high),
            low: pick(&mut f, product_is_greater, addend.low, product.low),
        };
        let big_at = pick(&mut f, product_is_greater, product_at, addend_at);
        let little_at = pick(&mut f, product_is_greater, addend_at, product_at);
        let big_sign = pick(&mut f, product_is_greater, product_sign, sc);
        let little_sign = pick(&mut f, product_is_greater, sc, product_sign);
        let gap = sub(&mut f, big_at, little_at);
        let (little, lost) = shr128(&mut f, little, gap);
        let same_sign = eq(&mut f, big_sign, little_sign);
        // The same sign adds, the other takes away, and a bit lost below the
        // lower number is a little more taken away.
        let (sum, carry) = add128(&mut f, big, little);
        let lost_number = number(&mut f, lost);
        let lost_wide = Wide {
            high: zero,
            low: lost_number,
        };
        let taken = sub128(&mut f, big, little);
        let taken = {
            // `big - little - 1` where something was lost, as it is the same
            // number as `big - (little + a fraction)` with the fraction held
            // in the lost bit.
            sub128(&mut f, taken, lost_wide)
        };
        let carried = flag_and(&mut f, same_sign, carry);
        // A carry out: the sum moves down by 1, the bit lost goes with it, and
        // the exponent grows.
        let one_n = c(&mut f, 1);
        let (moved, moved_lost) = shr128(&mut f, sum, one_n);
        let top_bit = c(&mut f, 1 << 63);
        let moved = Wide {
            high: or(&mut f, moved.high, top_bit),
            low: moved.low,
        };
        let sum = Wide {
            high: pick(&mut f, carried, moved.high, sum.high),
            low: pick(&mut f, carried, moved.low, sum.low),
        };
        let lost = {
            let from_move = flag_and(&mut f, carried, moved_lost);
            flag_or(&mut f, lost, from_move)
        };
        let at = {
            let grown = add(&mut f, big_at, one_n);
            pick(&mut f, carried, grown, big_at)
        };
        let result = Wide {
            high: pick(&mut f, same_sign, sum.high, taken.high),
            low: pick(&mut f, same_sign, sum.low, taken.low),
        };
        let at = pick(&mut f, same_sign, at, big_at);
        // Moved up until the first bit is at the top.
        let lead_r = clz128(&mut f, result);
        let result = shl128(&mut f, result, lead_r);
        let at = sub(&mut f, at, lead_r);
        let any_result = {
            let w = or(&mut f, result.high, result.low);
            nonzero(&mut f, w)
        };
        // Rounded to the precision, which is less where the number is below
        // the least of the normal ones.
        let least = c(&mut f, (1 - bias) as u64);
        let under = below_s(&mut f, at, least);
        let extra = sub(&mut f, least, at);
        let extra = f.choose(under, extra, zero);
        let keep_bits = c(&mut f, mantissa_bits + 1);
        let hundred_twenty_eight = c(&mut f, 128);
        let drop = {
            let base = sub(&mut f, hundred_twenty_eight, keep_bits);
            add(&mut f, base, extra)
        };
        // The part kept, a half, and what is below the half.
        let (kept, _) = shr128(&mut f, result, drop);
        let drop_less = sub(&mut f, drop, one_n);
        // The half is the lowest bit of what is kept with one less moved, and
        // what is below it is what that moved out, and the bit lost before.
        let (kept_less, dropped_to_half) = shr128(&mut f, result, drop_less);
        let half = and(&mut f, kept_less.low, one_n);
        let half_flag = nonzero(&mut f, half);
        let below_half = flag_or(&mut f, dropped_to_half, lost);
        let odd = and(&mut f, kept.low, one_n);
        let odd = nonzero(&mut f, odd);
        let beyond = flag_or(&mut f, below_half, odd);
        let up = flag_and(&mut f, half_flag, beyond);
        let up = number(&mut f, up);
        let kept_number = add(&mut f, kept.low, up);
        // The bits: the exponent put before the mantissa, so that a mantissa
        // that carries into it is the next exponent.
        let biased = {
            let b = c(&mut f, bias as u64);
            add(&mut f, at, b)
        };
        let field = {
            let before = sub(&mut f, biased, one_n);
            let by = c(&mut f, mantissa_bits);
            let moved = shl(&mut f, before, by);
            f.choose(under, zero, moved)
        };
        let bits = add(&mut f, field, kept_number);
        let infinity = {
            let by = c(&mut f, mantissa_bits);
            shl(&mut f, exponent_mask, by)
        };
        let over = {
            let below_inf = below(&mut f, bits, infinity);
            flag_not(&mut f, below_inf)
        };
        let bits = f.choose(over, infinity, bits);
        // Nothing left is the zero that is not negative.
        let bits = f.choose(any_result, bits, zero);
        // No number left is a zero that is not negative.
        let sign = f.choose(any_result, big_sign, zero);
        let by = c(&mut f, mantissa_bits + exponent_bits);
        let signed = shl(&mut f, sign, by);
        or(&mut f, bits, signed)
    };
    // The float of the bits.
    if double {
        f.store(scratch, result_bits);
    } else {
        let narrow = f.narrow(result_bits, Bits::B32);
        f.store(scratch, narrow);
    }
    let result = f.load(float, scratch);
    f.ret(vec![result]);
    f.finish(&[float])
}
