//! Properties of the functions of `Bits`, and of `narrow` and its relatives,
//! that hold for every integer type and every pair of them, and not for the
//! values a generator draws: what each costs in the scalar form, and when
//! each compiles. What they give is in the generator of `numbers`.

use std::collections::BTreeMap;

use smith::scalar::{self, Op, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value, with_header};

pub const ONCE: &[Test] = &[
    (
        "a_function_of_bits_is_one_instruction_and_no_call",
        a_function_of_bits_is_one_instruction_and_no_call,
    ),
    (
        "narrow_is_one_instruction_or_none_and_no_call",
        narrow_is_one_instruction_or_none_and_no_call,
    ),
    (
        "a_shift_compiles_exactly_when_its_amount_is_proven_below_the_bits_of_the_type",
        a_shift_compiles_exactly_when_its_amount_is_proven_below_the_bits_of_the_type,
    ),
    (
        "narrow_compiles_exactly_when_the_value_is_proven_to_fit",
        narrow_compiles_exactly_when_the_value_is_proven_to_fit,
    ),
    (
        "what_arith_knows_of_a_value_it_knows_of_its_narrow_and_of_no_other_narrowing",
        what_arith_knows_of_a_value_it_knows_of_its_narrow_and_of_no_other_narrowing,
    ),
    (
        "narrow_to_a_type_that_holds_every_value_is_an_error_that_says_to_convert",
        narrow_to_a_type_that_holds_every_value_is_an_error_that_says_to_convert,
    ),
    (
        "arith_knows_the_range_of_a_bit_operation_and_no_more_than_is_true",
        arith_knows_the_range_of_a_bit_operation_and_no_more_than_is_true,
    ),
    (
        "the_bound_of_bit_and_is_not_one_too_small_and_not_one_too_large",
        the_bound_of_bit_and_is_not_one_too_small_and_not_one_too_large,
    ),
    (
        "the_bound_of_bit_or_and_bit_xor_is_not_one_too_small_and_not_one_too_large",
        the_bound_of_bit_or_and_bit_xor_is_not_one_too_small_and_not_one_too_large,
    ),
    (
        "the_bound_of_shift_right_is_not_one_too_small_and_not_one_too_large",
        the_bound_of_shift_right_is_not_one_too_small_and_not_one_too_large,
    ),
    (
        "the_bound_of_shift_left_is_not_one_too_small_and_not_one_too_large",
        the_bound_of_shift_left_is_not_one_too_small_and_not_one_too_large,
    ),
    (
        "arith_knows_a_least_of_a_shift_right_of_an_unsigned_number_and_no_more_than_is_true",
        arith_knows_a_least_of_a_shift_right_of_an_unsigned_number_and_no_more_than_is_true,
    ),
    (
        "the_least_of_shift_right_is_not_one_too_large_and_not_one_too_small",
        the_least_of_shift_right_is_not_one_too_large_and_not_one_too_small,
    ),
    (
        "a_shift_by_a_number_compiles_exactly_when_it_is_below_the_bits_of_the_type",
        a_shift_by_a_number_compiles_exactly_when_it_is_below_the_bits_of_the_type,
    ),
    (
        "a_mask_of_a_number_keeps_a_sum_with_it_inside_the_type",
        a_mask_of_a_number_keeps_a_sum_with_it_inside_the_type,
    ),
];

/// How many instructions of each kind the program holds once the functions
/// it calls are inlined, with what they do, and how many calls are left.
fn kinds(source: &str) -> BTreeMap<String, usize> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let mut counts = BTreeMap::new();
    for i in (scalar::inline(&program).functions.iter())
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
    {
        let kind = match &i.op {
            Op::Binary { op, .. } => format!("{op:?}"),
            Op::Compare { op, .. } => format!("{op:?}"),
            Op::Widen { .. } => "Widen".to_string(),
            Op::Narrow(_) => "Narrow".to_string(),
            Op::Constant(_) => "Constant".to_string(),
            Op::Choose { .. } => "Choose".to_string(),
            Op::Call { .. } => "Call".to_string(),
            _ => "other".to_string(),
        };
        *counts.entry(kind).or_insert(0) += 1;
    }
    counts
}

/// What `with` holds that `without` does not, and the reverse, as the
/// difference in the number of each kind of instruction.
fn difference(without: &str, with: &str) -> BTreeMap<String, i64> {
    let (before, after) = (kinds(without), kinds(with));
    let mut out = BTreeMap::new();
    for kind in before.keys().chain(after.keys()) {
        let n = *after.get(kind).unwrap_or(&0) as i64 - *before.get(kind).unwrap_or(&0) as i64;
        if n != 0 {
            out.insert(kind.clone(), n);
        }
    }
    out
}

fn expected(kinds: &[(&str, i64)]) -> BTreeMap<String, i64> {
    kinds.iter().map(|(k, n)| (k.to_string(), *n)).collect()
}

/// A program that calls `f` with its arguments, where `f` has the
/// parameters and body written.
fn with(params: &str, gives: &str, requires: &str, body: &str, args: &str) -> String {
    with_header(format!(
        "fn f({params}) -> {gives}\n{requires}{{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n"
    ))
}

/// For every integer type, each function of `Bits` adds to a function that
/// gives its first parameter what the operation is, and nothing else, so it
/// is no call: `bit_not` is the bits changed by an exclusive or with every
/// bit set.
fn a_function_of_bits_is_one_instruction_and_no_call() {
    for ty in INTS {
        let t = ty.name;
        let same = format!("x: {t}, y: {t}");
        let both = |body: &str| with(&same, t, "", body, "2, 1");
        let base = both("x");
        for (function, kind) in [("bit_and", "And"), ("bit_or", "Or"), ("bit_xor", "Xor")] {
            assert_eq!(
                difference(&base, &both(&format!("{function}(x, y)"))),
                expected(&[(kind, 1)]),
                "{function} on {t}"
            );
        }
        assert_eq!(
            difference(&base, &both("bit_not(x)")),
            expected(&[("Xor", 1), ("Constant", 1)]),
            "bit_not on {t}"
        );
        let shifted = format!("x: {t}, by: Byte");
        let requires = format!("  requires by < {}\n", ty.bits);
        let shift = |body: &str| with(&shifted, t, &requires, body, "2, 1");
        let base = shift("x");
        let right = if ty.signed {
            "ShiftRightS"
        } else {
            "ShiftRightU"
        };
        for (function, kind) in [("shift_left", "ShiftLeft"), ("shift_right", right)] {
            assert_eq!(
                difference(&base, &shift(&format!("{function}(x, by)"))),
                expected(&[(kind, 1)]),
                "{function} on {t}"
            );
        }
    }
}

/// The conditions that say a value of `from` fits `to`, one for each side of
/// the range of `to` that `from` goes beyond.
fn bounds(from: Int, to: Int) -> Vec<String> {
    let mut out = Vec::new();
    if from.min() < to.min() {
        out.push(format!("x >= {}", to.min()));
    }
    if from.max() > to.max() {
        out.push(format!("x <= {}", to.max()));
    }
    out
}

/// The pairs of integer types where the second does not hold every value of
/// the first.
fn narrowings() -> Vec<(Int, Int)> {
    let mut out = Vec::new();
    for from in INTS {
        for to in INTS {
            if !to.holds_all(*from) {
                out.push((*from, *to));
            }
        }
    }
    out
}

/// For every pair of types that `narrow` goes between, it adds to a function
/// that gives a number of the type it goes to one `narrow` or `widen`, or
/// none when both have the same size, and `wrapping_narrow` does the same,
/// which neither is a call.
fn narrow_is_one_instruction_or_none_and_no_call() {
    for (from, to) in narrowings() {
        let requires = format!("  requires {}\n", bounds(from, to).join(" and "));
        let params = format!("x: {}", from.name);
        let base = with(&params, to.name, &requires, "0", "1");
        let kind = match to.bits.cmp(&from.bits) {
            std::cmp::Ordering::Less => vec![("Narrow", 1), ("Constant", -1)],
            std::cmp::Ordering::Greater => vec![("Widen", 1), ("Constant", -1)],
            std::cmp::Ordering::Equal => vec![("Constant", -1)],
        };
        for function in ["narrow", "wrapping_narrow"] {
            let body = format!("{function}(x)");
            let program = with(&params, to.name, &requires, &body, "1");
            assert_eq!(
                difference(&base, &program),
                expected(&kind),
                "{function} from {} to {}",
                from.name,
                to.name
            );
        }
    }
}

fn compiles(source: &str) -> Result<(), String> {
    check(source.as_bytes())
        .map(|_| ())
        .map_err(|e| format!("{e:?}"))
}

/// A shift of a number of any type compiles when the amount is proven below
/// the bits of the type, and does not when it may be that many or more.
fn a_shift_compiles_exactly_when_its_amount_is_proven_below_the_bits_of_the_type() {
    for ty in INTS {
        for function in ["shift_left", "shift_right"] {
            for limit in [ty.bits - 1, ty.bits, ty.bits + 1] {
                let source = with_header(format!(
                    "fn f(x: {0}, by: Byte) -> {0}\n  requires by <= {limit}\n{{\n  {function}(x, by)\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 0)}}\")\n}}\n",
                    ty.name
                ));
                let fits = limit < ty.bits;
                // A `Byte` is below its own bits only when it is.
                assert_eq!(
                    compiles(&source).is_ok(),
                    fits,
                    "{function} of {} by at most {limit}",
                    ty.name
                );
            }
        }
    }
}

/// `narrow` compiles when the value is in the range of the type it goes to,
/// by a `requires`, and does not when either side of the range is made one
/// wider, and the relatives compile either way.
fn narrow_compiles_exactly_when_the_value_is_proven_to_fit() {
    for (from, to) in narrowings() {
        let make = |function: &str, requires: &str| {
            let requires = if requires.is_empty() {
                String::new()
            } else {
                format!("  requires {requires}\n")
            };
            with(
                &format!("x: {}", from.name),
                to.name,
                &requires,
                &format!("{function}(x)"),
                "1",
            )
        };
        let exact = bounds(from, to);
        assert!(
            compiles(&make("narrow", &exact.join(" and "))).is_ok(),
            "narrow from {} to {} in its range",
            from.name,
            to.name
        );
        // Each side that the range of `from` goes beyond may not be wider.
        for side in 0..exact.len() {
            let widened: Vec<String> = exact
                .iter()
                .enumerate()
                .map(|(i, c)| {
                    if i != side {
                        return c.clone();
                    }
                    if c.starts_with("x >=") {
                        format!("x >= {}", to.min() - 1)
                    } else {
                        format!("x <= {}", to.max() + 1)
                    }
                })
                .collect();
            let unproven = compiles(&make("narrow", &widened.join(" and ")));
            assert!(
                unproven.is_err(),
                "narrow from {} to {} with `{}`",
                from.name,
                to.name,
                widened.join(" and ")
            );
        }
        for function in ["wrapping_narrow", "saturating_narrow"] {
            assert!(
                compiles(&make(function, "")).is_ok(),
                "{function} from {} to {}",
                from.name,
                to.name
            );
        }
    }
}

/// `narrow` and each relative between two types where the second holds every
/// value of the first is an error that says to use `convert`, except from a
/// type to itself, which no type converts to.
fn narrow_to_a_type_that_holds_every_value_is_an_error_that_says_to_convert() {
    for from in INTS {
        for to in INTS.iter().filter(|t| t.holds_all(*from)) {
            for function in ["narrow", "wrapping_narrow", "saturating_narrow"] {
                let source = with(
                    &format!("x: {}", from.name),
                    to.name,
                    "",
                    &format!("{function}(x)"),
                    "1",
                );
                let errors = check(source.as_bytes()).expect_err("an error");
                assert_eq!(
                    errors.len(),
                    1,
                    "{function} from {} to {}",
                    from.name,
                    to.name
                );
                assert_eq!(
                    errors[0]
                        .fix
                        .as_deref()
                        .is_some_and(|f| f.contains("convert")),
                    from != to,
                    "{function} from {} to {}",
                    from.name,
                    to.name
                );
            }
        }
    }
}

/// For every pair of types that `narrow` goes between, a fact about a value
/// that fits is known of what `narrow` gives, so a function that needs it
/// of the result is called, and one that needs a fact that is false of the
/// value is refused. `wrapping_narrow` and `saturating_narrow` give a number
/// that `arith` knows nothing of when the value is not proven to fit, so
/// each is refused whatever is known of the value.
fn what_arith_knows_of_a_value_it_knows_of_its_narrow_and_of_no_other_narrowing() {
    for (from, to) in narrowings() {
        let k = to.max().min(from.max()) / 2;
        let mut known = bounds(from, to);
        known.push(format!("x <= {k}"));
        let program = |function: &str, needed: i128, requires: &str| {
            with_header(format!(
                "fn g(y: {to_t}) -> {to_t}\n  requires y <= {needed}\n{{\n  y\n}}\n\nfn f(x: {from_t}) -> {to_t}\n{requires}{{\n  y: {to_t} = {function}(x)\n  g(y)\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1)}}\")\n}}\n",
                to_t = to.name,
                from_t = from.name,
            ))
        };
        let requires = format!("  requires {}\n", known.join(" and "));
        let name = format!("{} to {}", from.name, to.name);
        assert!(
            compiles(&program("narrow", k, &requires)).is_ok(),
            "narrow from {name}"
        );
        assert!(
            compiles(&program("narrow", k - 1, &requires)).is_err(),
            "narrow from {name} when the fact is false of the value"
        );
        // The same fact is known of the value, and nothing of its narrowing.
        for function in ["wrapping_narrow", "saturating_narrow"] {
            assert!(
                compiles(&program(function, k, &requires)).is_err(),
                "{function} from {name}"
            );
        }
    }
}

/// The bit operations `arith` knows the range of.
#[derive(Clone, Copy, Debug)]
enum Bit {
    And,
    Or,
    Xor,
    Left,
    Right,
}

/// A bit operation on a number bounded above, and a bound that a function
/// that takes what the operation gives needs of it.
#[derive(Clone, Debug)]
struct Range {
    ty: Int,
    op: Bit,
    /// The number is at least this and at most `most`.
    least: i128,
    most: i128,
    /// What the operation is given with the number: a bound of a second
    /// number, written as a literal or as a name with that bound, and for a
    /// shift the amount.
    other: i128,
    literal: bool,
    /// What the function that takes the result needs of it: at most this.
    needs: i128,
}

/// The value with the bits that do not fit the type left off.
fn wrap(ty: Int, n: i128) -> i128 {
    (n - ty.min()).rem_euclid(1i128 << ty.bits) + ty.min()
}

/// What the operation gives, worked out on 128 bits.
fn gives(r: &Range, a: i128, b: i128) -> i128 {
    match r.op {
        Bit::And => a & b,
        Bit::Or => a | b,
        Bit::Xor => a ^ b,
        Bit::Left => wrap(r.ty, a << b),
        Bit::Right => a >> b,
    }
}

/// The least power of two above `n`, less one: every bit below the highest
/// bit of `n` set.
fn all_ones_to(n: i128) -> i128 {
    let mut ones = 0;
    while ones < n {
        ones = ones * 2 + 1;
    }
    ones
}

/// The bound the range of the result is known to have, or none where `arith`
/// is not to know a bound: of a signed number, other than a shift left that
/// fits, which is the number times a power of two.
fn known(r: &Range) -> Option<i128> {
    match r.op {
        _ if r.ty.signed && !matches!(r.op, Bit::Left) => None,
        Bit::And => Some(r.most.min(r.other)),
        Bit::Or | Bit::Xor => Some(all_ones_to(r.most.max(r.other))),
        Bit::Right => Some(r.most >> r.other),
        Bit::Left => {
            let (low, high) = (r.least << r.other, r.most << r.other);
            Some(if low >= r.ty.min() && high <= r.ty.max() {
                high
            } else {
                r.ty.max()
            })
        }
    }
}

fn program(r: &Range) -> String {
    let t = r.ty.name;
    let function = match r.op {
        Bit::And => "bit_and",
        Bit::Or => "bit_or",
        Bit::Xor => "bit_xor",
        Bit::Left => "shift_left",
        Bit::Right => "shift_right",
    };
    let shift = matches!(r.op, Bit::Left | Bit::Right);
    let (params, requires, second) = if shift || r.literal {
        (
            format!("x: {t}"),
            format!("x >= {} and x <= {}", r.least, r.most),
            r.other.to_string(),
        )
    } else {
        (
            format!("x: {t}, z: {t}"),
            format!("x >= {0} and x <= {1} and z >= {0} and z <= {2}", r.least, r.most, r.other),
            "z".to_string(),
        )
    };
    with_header(format!(
        "fn g(y: {t}) -> {t}\n  requires y <= {needs}\n{{\n  y\n}}\n\nfn f({params}) -> {t}\n  requires {requires}\n{{\n  g({function}(x, {second}))\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n",
        needs = r.needs,
        args = if params.contains("z") {
            format!("{}, {}", r.least, r.least)
        } else {
            r.least.to_string()
        },
    ))
}

/// The values of a bound that can show a fact false: both ends, the ones
/// next to them, and the powers of two and the ones below them.
fn edges(ty: Int, least: i128, most: i128) -> Vec<i128> {
    let mut out = vec![0, 1];
    for d in 0..8 {
        out.push(least + d);
        out.push(most - d);
    }
    for bit in 0..ty.bits.min(63) {
        out.push((1i128 << bit) - 1);
        out.push(1i128 << bit);
    }
    out.retain(|n| (least..=most).contains(n));
    out
}

fn range(g: &mut Gen) -> Range {
    let ty = *g.pick(INTS);
    let op = *g.pick(&[Bit::And, Bit::Or, Bit::Xor, Bit::Left, Bit::Right]);
    let shift = matches!(op, Bit::Left | Bit::Right);
    let small = g.below(40) as i128;
    let (a, b) = if g.one_in(2) {
        (value(g, ty), value(g, ty))
    } else {
        (small, (small ^ g.below(40) as i128).min(ty.max()))
    };
    let (least, most) = if ty.signed && matches!(op, Bit::Left) {
        (a.min(b), a.max(b))
    } else if ty.signed {
        (ty.min(), a.max(b))
    } else {
        (0, a)
    };
    let any = g.below(ty.bits as usize) as i128;
    let other = if shift {
        *g.pick(&[0, 1, i128::from(ty.bits) - 1, any])
    } else if g.one_in(2) {
        value(g, ty).max(0)
    } else {
        b.clamp(0, ty.max())
    };
    let mut r = Range {
        ty,
        op,
        least,
        most,
        other,
        literal: g.one_in(2),
        needs: 0,
    };
    // A bound near what is known, near what is true, or any.
    let near = known(&r).unwrap_or(ty.max());
    let result = gives(&r, *g.pick(&edges(ty, least, most)), other);
    let any = value(g, ty);
    let needs = *g.pick(&[near, near - 1, near + 1, result, result - 1, ty.max(), any]);
    r.needs = needs.clamp(ty.min(), ty.max());
    r
}

/// For every integer type, each of `bit_and`, `bit_or`, `bit_xor`,
/// `shift_left` and `shift_right` of a number, whose bounds are values the
/// generator draws, gives a number that `arith` knows a bound of, and no
/// bound is claimed that some values make false: when a function that needs
/// the result to be at most a bound is called with it, it compiles only when
/// every value in the bounds gives a result that is, as 128-bit arithmetic
/// says. It compiles exactly when the bound is what is known of an unsigned
/// number, which is the smaller bound for `bit_and`, below the next power of
/// two above the larger for `bit_or` and `bit_xor`, the bound divided by two
/// to the amount for `shift_right`, and the bound times two to the amount for
/// `shift_left` where that fits the type.
fn arith_knows_the_range_of_a_bit_operation_and_no_more_than_is_true() {
    generate::check(241, 100, range, holds);
}

/// What must be true when the function that needs the result to be at most
/// `r.needs` is called with the operation: it compiles only when every
/// number in the bounds, those at each end and each power of two among them,
/// gives a result that is, and exactly when `r.needs` is the bound that is
/// known.
fn holds(r: &Range) {
    let compiled = check(program(r).as_bytes()).is_ok();
    if compiled {
        let shift = matches!(r.op, Bit::Left | Bit::Right);
        let b_least = if r.literal || shift { r.other } else { 0 };
        let (xs, zs) = (
            edges(r.ty, r.least, r.most),
            edges(r.ty, b_least, r.other),
        );
        for a in &xs {
            let others = if shift || r.literal {
                vec![r.other]
            } else {
                zs.clone()
            };
            for b in others {
                let result = gives(r, *a, b);
                assert!(
                    result <= r.needs,
                    "accepted, though {a} and {b} give {result}, above {}",
                    r.needs
                );
            }
        }
    }
    if let Some(bound) = known(r) {
        assert_eq!(
            compiled,
            r.needs >= bound,
            "{r:?} is known to be at most {bound}, and {} is needed",
            r.needs
        );
    }
}

/// The bounds at which each fact is tight, for every type: a bound that is
/// all ones, one that is not, the greatest and the least of the type, and the
/// one that makes a shift left just fit and just not.
fn tight() -> Vec<Range> {
    let mut out = Vec::new();
    let mut add = |ty: Int, op: Bit, least: i128, most: i128, other: i128, literal: bool| {
        if (ty.min()..=ty.max()).contains(&least)
            && (ty.min()..=ty.max()).contains(&most)
            && (0..=ty.max()).contains(&other)
        {
            out.push(Range {
                ty,
                op,
                least,
                most,
                other,
                literal,
                needs: 0,
            });
        }
    };
    for ty in INTS {
        let (m, n) = (ty.max(), i128::from(ty.bits));
        if !ty.signed {
            for (a, b, literal) in [(m, 1, true), (5, 12, false), (m, m >> 1, false), (6, 3, true)] {
                add(*ty, Bit::And, 0, a, b, literal);
            }
            for op in [Bit::Or, Bit::Xor] {
                for (a, b, literal) in [
                    (5, 2, false),
                    (7, 7, false),
                    (7, 0, true),
                    (8, 0, false),
                    (4, 1, true),
                    (m >> 1, 1, true),
                    (m, 0, true),
                ] {
                    add(*ty, op, 0, a, b, literal);
                }
            }
            for k in [0, 1, n - 1] {
                for a in [m, 5, 1 << (n - 1), m - 1] {
                    add(*ty, Bit::Right, 0, a, k, true);
                }
            }
        }
        for k in [0, 1, n - 1] {
            // Both ends of what fits, and one past each.
            let (low, high) = (ty.min() >> k, m >> k);
            for (least, most) in [(low, high), (low, 0), (0, high), (0, high + 1), (low - 1, 0)] {
                add(*ty, Bit::Left, least, most, k, true);
            }
        }
    }
    out
}

/// A function that needs the result to be at most the bound that is known of
/// each of the cases of `tight` for the operations, compiles, and one that
/// needs one less does not, so the bound is neither one too small, which a
/// number in the bounds makes false, nor one too large.
fn bounds_are_tight(ops: &[&str]) {
    for tight in tight() {
        if !ops.contains(&format!("{:?}", tight.op).as_str()) {
            continue;
        }
        let Some(bound) = known(&tight) else { continue };
        for needs in [bound, bound - 1] {
            if needs >= tight.ty.min() {
                holds(&Range {
                    needs,
                    ..tight.clone()
                });
            }
        }
    }
}

fn the_bound_of_bit_and_is_not_one_too_small_and_not_one_too_large() {
    bounds_are_tight(&["And"]);
}

fn the_bound_of_bit_or_and_bit_xor_is_not_one_too_small_and_not_one_too_large() {
    bounds_are_tight(&["Or", "Xor"]);
}

fn the_bound_of_shift_right_is_not_one_too_small_and_not_one_too_large() {
    bounds_are_tight(&["Right"]);
}

fn the_bound_of_shift_left_is_not_one_too_small_and_not_one_too_large() {
    bounds_are_tight(&["Left"]);
}

/// A shift of any type by a number compiles exactly when the number is below
/// the bits of the type, whether the shift is left or right, and the amount
/// of the last place, of 0, and of 1 are all inside it. A shift by the bits
/// of the type or more is refused when the program is built, since nothing
/// tests it when the program runs.
fn a_shift_by_a_number_compiles_exactly_when_it_is_below_the_bits_of_the_type() {
    for ty in INTS {
        for function in ["shift_left", "shift_right"] {
            for by in [0, 1, ty.bits - 1, ty.bits, ty.bits + 1, 255] {
                let source = with_header(format!(
                    "fn f(x: {0}) -> {0} {{ {function}(x, {by}) }}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1)}}\")\n}}\n",
                    ty.name
                ));
                let result = check(source.as_bytes());
                assert_eq!(
                    result.is_ok(),
                    by < ty.bits,
                    "{function} of a {} by {by}",
                    ty.name
                );
                if let Err(errors) = result {
                    assert!(
                        errors.iter().any(|e| e.message.contains(&format!("{by} <"))),
                        "{errors:?} does not state `{by} < {}`",
                        ty.bits
                    );
                }
            }
        }
    }
}

/// The function of the issue: a number masked to its low five bits and
/// added to 200 fits a `Byte`, and is 231 for 255, and a mask that keeps one
/// bit more does not.
fn a_mask_of_a_number_keeps_a_sum_with_it_inside_the_type() {
    let source = |mask: u32| {
        with_header(format!(
            "fn f(x: Byte) -> Byte {{ bit_and(x, {mask}) + 200 }}\n\nfn main() ! console, alloc {{\n  print(\"{{f(255)}}\")\n}}\n"
        ))
    };
    let program = lower(source(31).as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(
        scalar::run(&program),
        Outcome {
            stdout: b"231\n".to_vec(),
            status: 0
        }
    );
    assert!(check(source(63).as_bytes()).is_err());
}

#[derive(Debug)]
struct Least {
    ty: Int,
    least: i128,
    most: i128,
    by: i128,
    needs: i128,
}

fn least(g: &mut Gen) -> Least {
    let unsigned: Vec<Int> = INTS.iter().copied().filter(|t| !t.signed).collect();
    let ty = *g.pick(&unsigned);
    let (a, b) = (value(g, ty), value(g, ty));
    let any = g.below(ty.bits as usize) as i128;
    let by = *g.pick(&[0, 1, i128::from(ty.bits) - 1, any]);
    let (least, most) = (a.min(b), a.max(b));
    let known = least >> by;
    let other = value(g, ty);
    let needs = *g.pick(&[known, known + 1, known - 1, 0, other, ty.max()]);
    Least {
        ty,
        least,
        most,
        by,
        needs: needs.clamp(ty.min(), ty.max()),
    }
}

/// For every unsigned type, a function that needs the result of
/// `shift_right(x, k)` to be at least a bound compiles exactly when the bound
/// is the least of `x` divided by two to the `k`, rounded down, and then
/// every value in the bounds gives a result that is, as 128-bit arithmetic
/// says.
fn arith_knows_a_least_of_a_shift_right_of_an_unsigned_number_and_no_more_than_is_true() {
    generate::check(242, 60, least, holds_least);
}

/// The least of `shift_right` is neither one too large, which a number in the
/// bounds makes false, nor one too small, which makes a function that needs
/// the true least refused: where it is tight, a function that needs it
/// compiles and one that needs one more does not.
fn the_least_of_shift_right_is_not_one_too_large_and_not_one_too_small() {
    for ty in INTS.iter().filter(|t| !t.signed) {
        let (m, n) = (ty.max(), i128::from(ty.bits));
        for by in [0, 1, n - 1] {
            for (low, most) in [(m, m), (m - 1, m), (5, m), (1 << (n - 1), m), (0, m), (3, 9)] {
                let known = low >> by;
                for needs in [known, known + 1] {
                    if needs <= m {
                        holds_least(&Least {
                            ty: *ty,
                            least: low,
                            most,
                            by,
                            needs,
                        });
                    }
                }
            }
        }
    }
}

fn holds_least(l: &Least) {
    let t = l.ty.name;
    let source = with_header(format!(
        "fn g(y: {t}) -> {t}\n  requires y >= {needs}\n{{\n  y\n}}\n\nfn f(x: {t}) -> {t}\n  requires x >= {least} and x <= {most}\n{{\n  g(shift_right(x, {by}))\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({least})}}\")\n}}\n",
        needs = l.needs,
        least = l.least,
        most = l.most,
        by = l.by,
    ));
    let compiled = check(source.as_bytes()).is_ok();
    assert_eq!(compiled, l.needs <= l.least >> l.by, "{source}");
    if compiled {
        for x in edges(l.ty, l.least, l.most) {
            assert!(l.needs <= x >> l.by, "accepted, though {x} gives less");
        }
    }
}
