//! Properties of the functions of `Bits`, and of `narrow` and its relatives,
//! that hold for every integer type and every pair of them, and not for the
//! values a generator draws: what each costs in the scalar form, and when
//! each compiles. What they give is in the generator of `numbers`.

use std::collections::BTreeMap;

use smith::scalar::{self, Op};
use smith::{check, lower};

use crate::Test;
use crate::numbers::{INTS, Int};

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
        "narrow_to_a_type_that_holds_every_value_is_an_error_that_says_to_convert",
        narrow_to_a_type_that_holds_every_value_is_an_error_that_says_to_convert,
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
    format!(
        "fn f({params}) -> {gives}\n{requires}{{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n"
    )
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
        let shifted = format!("x: {t}, by: u8");
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
                let source = format!(
                    "fn f(x: {0}, by: u8) -> {0}\n  requires by <= {limit}\n{{\n  {function}(x, by)\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 0)}}\")\n}}\n",
                    ty.name
                );
                let fits = limit < ty.bits;
                // A `u8` is below its own bits only when it is.
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
