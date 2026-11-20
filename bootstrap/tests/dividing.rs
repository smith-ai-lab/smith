//! `/` and `%` of whole numbers, which round toward zero: what they give for
//! every type and the values a generator draws, when they compile, and what
//! each costs in the scalar form. The instructions are in `division`.

use std::collections::BTreeMap;

use smith::lower;
use smith::scalar::{self, Op};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value, with_header};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "the_quotient_and_the_rest_of_whole_numbers_are_what_division_rounded_toward_zero_gives",
        the_quotient_and_the_rest_of_whole_numbers_are_what_division_rounded_toward_zero_gives,
    ),
    (
        "a_division_compiles_exactly_when_the_divisor_is_proven_not_to_be_zero",
        a_division_compiles_exactly_when_the_divisor_is_proven_not_to_be_zero,
    ),
    (
        "a_division_of_the_least_number_by_minus_one_compiles_only_where_the_result_fits",
        a_division_of_the_least_number_by_minus_one_compiles_only_where_the_result_fits,
    ),
    (
        "a_quotient_and_what_is_left_over_are_one_instruction_and_no_call",
        a_quotient_and_what_is_left_over_are_one_instruction_and_no_call,
    ),
    (
        "an_unsigned_number_divided_by_a_power_of_two_is_a_shift_and_what_is_left_over_is_a_mask",
        an_unsigned_number_divided_by_a_power_of_two_is_a_shift_and_what_is_left_over_is_a_mask,
    ),
    (
        "a_divisor_that_is_zero_or_may_be_is_the_one_thing_said_of_its_division",
        a_divisor_that_is_zero_or_may_be_is_the_one_thing_said_of_its_division,
    ),
    (
        "a_quotient_of_a_signed_number_by_a_positive_one_fits_the_type_of_both",
        a_quotient_of_a_signed_number_by_a_positive_one_fits_the_type_of_both,
    ),
    (
        "division_and_what_is_left_over_bind_as_multiplication_does",
        division_and_what_is_left_over_bind_as_multiplication_does,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_quotient_and_what_is_left_over_gives_what_the_evaluator_gives",
    every_executable_of_a_quotient_and_what_is_left_over_gives_what_the_evaluator_gives,
)];

/// A function of two numbers of a type that prints what `/` and `%` give,
/// where the second is not zero, and a `main` that calls it with each pair.
fn program(ty: Int, pairs: &[(i128, i128)]) -> String {
    // The least number of a size divided by -1 is asked of the processor, so a
    // number of 64 bits that is signed is not given a divisor of -1.
    let requires = if ty.signed && ty.bits == 64 {
        "b > 0 or b < -1"
    } else {
        "b != 0"
    };
    let mut text = format!(
        "use std.console (console, print)\n\nfn show(a: {t}, b: {t}) ! console, alloc\n  requires {requires}\n{{\n  print(\"{{a / b}} {{a % b}}\")\n}}\n\nfn main() ! console, alloc {{\n",
        t = ty.name
    );
    for (a, b) in pairs {
        text.push_str(&format!("  show({a}, {b})\n"));
    }
    text.push_str("}\n");
    with_header(text)
}

/// Pairs the type is given, with a divisor the program may take.
fn pairs(g: &mut Gen, ty: Int) -> Vec<(i128, i128)> {
    let mut out = Vec::new();
    for _ in 0..g.below(6) + 1 {
        let (a, b) = (value(g, ty), value(g, ty));
        let forbidden = b == 0 || (ty.signed && ty.bits == 64 && b == -1);
        if !forbidden {
            out.push((a, b));
        }
    }
    out
}

/// What each pair prints: the quotient and what is left, each rounded toward
/// zero, which are what 128 bits of arithmetic gives.
fn expected(pairs: &[(i128, i128)]) -> String {
    pairs
        .iter()
        .map(|(a, b)| {
            assert_eq!((a / b) * b + a % b, *a);
            format!("{} {}\n", a / b, a % b)
        })
        .collect()
}

fn the_quotient_and_the_rest_of_whole_numbers_are_what_division_rounded_toward_zero_gives() {
    generate::check(
        5101,
        40,
        |g| {
            let ty = *g.pick(INTS);
            (ty, pairs(g, ty))
        },
        |(ty, pairs)| {
            let source = program(*ty, pairs);
            let lowered =
                lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
            assert_eq!(
                String::from_utf8(scalar::run(&lowered).stdout).unwrap(),
                expected(pairs),
                "for\n{source}"
            );
        },
    );
}

fn every_executable_of_a_quotient_and_what_is_left_over_gives_what_the_evaluator_gives() {
    generate::check(
        5102,
        12,
        |g| {
            let ty = *g.pick(INTS);
            (ty, pairs(g, ty))
        },
        |(ty, pairs)| {
            let source = program(*ty, pairs);
            let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
            let said = scalar::run(&scalar::passes(&lowered));
            for target in runnable() {
                runs_as(source.as_bytes(), target, &said);
            }
        },
    );
}

/// What `main` prints for `body` of a function `f` of `params`, or the
/// messages of the errors that refuse it.
fn built(params: &str, requires: &str, body: &str, args: &str) -> Result<String, String> {
    let gives = if params.contains("Nat") { "Nat" } else { "Int" };
    let source = with_header(format!(
        "use std.console (console, print)\n\nfn f({params}) -> {gives}\n{requires}{{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n"
    ));
    match lower(source.as_bytes()) {
        Ok(program) => Ok(String::from_utf8(scalar::run(&program).stdout).unwrap()),
        Err(errors) => Err(errors
            .iter()
            .map(|e| e.message.clone())
            .collect::<Vec<_>>()
            .join("\n")),
    }
}

fn a_division_compiles_exactly_when_the_divisor_is_proven_not_to_be_zero() {
    generate::check(
        5103,
        40,
        |g| {
            let low = g.below(9) as i128 - 4;
            let high = low + g.below(5) as i128;
            (low, high, g.one_in(2))
        },
        |(low, high, remainder)| {
            let op = if *remainder { "%" } else { "/" };
            let (checked, wrapping) = if *remainder {
                ("checked_rem", "wrapping_rem")
            } else {
                ("checked_div", "wrapping_div")
            };
            let params = format!("total: i32, n: {low}..{}", high + 1);
            let excludes_zero = *low > 0 || *high < 0;
            let args = format!("7, {}", if excludes_zero { *low } else { 1 });
            let outcome = built(&params, "", &format!("total {op} n"), &args);
            if excludes_zero {
                assert!(outcome.is_ok(), "{low}..{high}: {outcome:?}");
            } else {
                // Only the divisor is refused, and the error names the goal
                // and the functions that do not need it.
                let error = outcome.expect_err("a divisor that may be zero");
                assert!(error.contains("cannot prove that `n != 0`"), "{error}");
            }
            let _ = (checked, wrapping);
        },
    );
    // A check first is a proof, as it is for any range.
    assert_eq!(
        built("total: Nat, n: 0..9", "", "if n > 0 { total / n } else { 0 }", "7, 2"),
        Ok("3\n".to_string())
    );
    assert_eq!(
        built("total: Nat, n: Nat", "  requires n != 0\n", "total % n", "7, 4"),
        Ok("3\n".to_string())
    );
    let refused = built("total: Nat, n: Nat", "", "total / n", "7, 2").unwrap_err();
    assert!(refused.contains("cannot prove that `n != 0`"), "{refused}");
}

fn a_division_of_the_least_number_by_minus_one_compiles_only_where_the_result_fits() {
    // A number of 64 bits whose least value is divided by -1 gives one that
    // no number of 64 bits holds.
    let refused = built("a: Int, b: Int", "  requires b != 0\n", "a / b", "7, 2").unwrap_err();
    assert!(refused.contains("a / b"), "{refused}");
    assert!(
        built("a: Int, b: Int", "  requires b > 0 or b < -1\n", "a / b", "-7, 2")
            .is_ok_and(|said| said == "-3\n")
    );
    // A smaller size has a wider one that holds it.
    assert_eq!(
        built("a: i8, b: i8", "  requires b != 0\n", "a / b", "-128, -1"),
        Ok("128\n".to_string())
    );
    let refused = built("a: Int, b: Int", "  requires b != 0\n", "a % b", "7, 2").unwrap_err();
    assert!(refused.contains("a % b"), "{refused}");
}

fn count(source: &str) -> BTreeMap<String, usize> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let mut counts = BTreeMap::new();
    for i in (scalar::inline(program).functions.iter())
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
    {
        let kind = match &i.op {
            Op::Binary { op, .. } => format!("{op:?}"),
            Op::Call { .. } => "Call".to_string(),
            _ => continue,
        };
        *counts.entry(kind).or_insert(0) += 1;
    }
    counts
}

/// What `with` holds of each kind of instruction that `without` does not, and
/// the reverse, as the difference in their number.
fn difference(without: &str, with: &str) -> BTreeMap<String, i64> {
    let (before, after) = (count(without), count(with));
    let mut out = BTreeMap::new();
    for kind in before.keys().chain(after.keys()) {
        let n = *after.get(kind).unwrap_or(&0) as i64 - *before.get(kind).unwrap_or(&0) as i64;
        if n != 0 {
            out.insert(kind.clone(), n);
        }
    }
    out
}

/// A program that calls `f` of `params` with the body, which gives `t`.
fn calling(params: &str, t: &str, body: &str, args: &str) -> String {
    with_header(format!(
        "use std.console (console, print)\n\nfn f({params}) -> {t} {{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n"
    ))
}

fn a_quotient_and_what_is_left_over_are_one_instruction_and_no_call() {
    for ty in INTS {
        let t = ty.name;
        let (div, rem) = if ty.signed {
            ("DivS", "RemS")
        } else {
            ("DivU", "RemU")
        };
        // A divisor that is a range above two is no power of two the program
        // names, and no processor is asked for the least number by -1.
        let params = format!("x: {t}, y: 3..5");
        let base = calling(&params, t, "x", "7, 4");
        for (op, kind) in [("/", div), ("%", rem)] {
            let with = calling(&params, t, &format!("x {op} y"), "7, 4");
            let found = difference(&base, &with);
            // A quotient of a number of a size kept at its own range is the
            // instruction, and a number that is not is taken to its size and
            // taken back: no call, and the instruction once.
            assert_eq!(found.get(kind), Some(&1), "{op} on {t}: {found:?}");
            assert_eq!(found.get("Call"), None, "{op} on {t}: {found:?}");
        }
    }
}

fn an_unsigned_number_divided_by_a_power_of_two_is_a_shift_and_what_is_left_over_is_a_mask() {
    for ty in INTS.iter().filter(|t| !t.signed) {
        let t = ty.name;
        let base = calling(&format!("x: {t}"), t, "x", "7");
        for shift in [1u32, 3, ty.bits - 1] {
            let power = 1u128 << shift;
            let with = |op: &str| calling(&format!("x: {t}"), t, &format!("x {op} {power}"), "7");
            let quotient = difference(&base, &with("/"));
            assert_eq!(
                quotient.get("ShiftRightU"),
                Some(&1),
                "{t} / {power}: {quotient:?}"
            );
            assert_eq!(quotient.get("DivU"), None, "{t} / {power}: {quotient:?}");
            let rest = difference(&base, &with("%"));
            assert_eq!(rest.get("And"), Some(&1), "{t} % {power}: {rest:?}");
            assert_eq!(rest.get("RemU"), None, "{t} % {power}: {rest:?}");
        }
    }
}

fn division_and_what_is_left_over_bind_as_multiplication_does() {
    let main = |expression: &str| {
        with_header(format!(
            "use std.console (console, print)\n\nfn main() ! console, alloc {{\n  print(\"{{{expression}}}\")\n}}\n"
        ))
    };
    // They group to the left, as `*` does, and bind tighter than `+` and `-`.
    for (expression, value) in [
        ("2 + 7 / 2 * 3 % 4", 3),
        ("100 / 10 / 5", 2),
        ("20 - 7 % 4 * 2", 14),
        ("7 * 2 / 4", 3),
        ("-7 / 2", -3),
        ("-7 % 2", -1),
        ("7 / -2", -3),
        ("7 % -2", 1),
    ] {
        let program = lower(main(expression).as_bytes())
            .unwrap_or_else(|e| panic!("{e:?} for {expression}"));
        assert_eq!(
            String::from_utf8(scalar::run(&program).stdout).unwrap(),
            format!("{value}\n"),
            "{expression}"
        );
    }
}

/// What the checker says of a file of one function.
fn checked(function: &str) -> Result<(), String> {
    let source = with_header(format!(
        "use std.bits (i8)\nuse std.console (console, print)\n\n{function}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 2)}}\")\n}}\n"
    ));
    lower(source.as_bytes()).map(|_| ()).map_err(|e| format!("{e:?}"))
}

/// A divisor of the literal zero is said to divide by zero and given no fix
/// that checks `0 != 0`, and a divisor that may be zero is refused once, for
/// that, and not again for the quotient not fitting.
fn a_divisor_that_is_zero_or_may_be_is_the_one_thing_said_of_its_division() {
    for op in ["/", "%"] {
        let error = checked(&format!("fn f(a: Int, n: Int) -> Int {{ a {op} 0 }}")).unwrap_err();
        assert!(error.contains("divides by zero"), "{error}");
        assert!(!error.contains("0 != 0"), "{error}");
        assert!(error.contains("fix: None"), "{error}");
        let error = checked(&format!("fn f(a: Int, n: Int) -> Int {{ a {op} n }}")).unwrap_err();
        assert_eq!(error.matches("message:").count(), 1, "{error}");
        assert!(error.contains("`n != 0`"), "{error}");
        let error = checked(&format!("fn f(a: i8, n: i8) -> i8 {{ a {op} n }}")).unwrap_err();
        assert_eq!(error.matches("message:").count(), 1, "{error}");
    }
}

/// The least number of a signed type divided by -1 is the only quotient that
/// leaves the type, so a divisor that is above zero, or is not -1, is enough.
fn a_quotient_of_a_signed_number_by_a_positive_one_fits_the_type_of_both() {
    assert_eq!(checked("fn f(a: i8, b: i8) -> i8 requires b > 0 { a / b }"), Ok(()));
    assert_eq!(
        checked("fn f(a: i8, b: i8) -> i8 { if b > 0 { a / b } else { 0 } }"),
        Ok(())
    );
    assert_eq!(checked("fn f(a: i8, b: i8) -> i8 requires b > 0 { a % b }"), Ok(()));
    let error = checked("fn f(a: i8, b: i8) -> i8 requires b != 0 { a / b }").unwrap_err();
    assert!(error.contains("a / b <= 127"), "{error}");
}
