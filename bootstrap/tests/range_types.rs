//! A range written `a..b` as a type, with numbers for its bounds, as
//! `knowledge/language/number-types.md` says.

use smith::{check, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;
use smith::lower;

pub const ONCE: &[Test] = &[
    (
        "a_range_holds_exactly_the_numbers_from_its_first_bound_up_to_its_second",
        a_range_holds_exactly_the_numbers_from_its_first_bound_up_to_its_second,
    ),
    (
        "a_range_is_held_by_a_parameter_a_result_a_field_a_list_and_an_option",
        a_range_is_held_by_a_parameter_a_result_a_field_a_list_and_an_option,
    ),
    (
        "a_range_is_compared_and_shown_whatever_its_bounds",
        a_range_is_compared_and_shown_whatever_its_bounds,
    ),
    (
        "a_range_written_two_ways_is_one_type",
        a_range_written_two_ways_is_one_type,
    ),
    (
        "a_range_with_no_number_is_a_type_and_a_range_that_ends_before_it_begins_is_refused",
        a_range_with_no_number_is_a_type_and_a_range_that_ends_before_it_begins_is_refused,
    ),
    (
        "a_range_of_more_than_64_bits_is_refused",
        a_range_of_more_than_64_bits_is_refused,
    ),
    (
        "arith_proves_a_sum_of_a_range_exactly_when_it_fits_the_range",
        arith_proves_a_sum_of_a_range_exactly_when_it_fits_the_range,
    ),
];

fn stdout(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    let out = scalar::run(&program);
    assert_eq!(out.status, 0, "for\n{source}");
    String::from_utf8(out.stdout).unwrap()
}

fn compiles(source: &str) -> bool {
    check(source.as_bytes()).is_ok()
}

/// A range of at most 64 bits: its least and its greatest number. The ends
/// of the sizes are drawn as often as another, since a range is kept at the
/// size that holds it.
fn range(g: &mut Gen) -> (i128, i128) {
    const EDGES: [i128; 9] = [
        -9223372036854775808,
        -32769,
        -129,
        -1,
        0,
        255,
        65536,
        4294967295,
        18446744073709551615,
    ];
    let low = match g.below(3) {
        0 => *g.pick(&EDGES),
        1 => g.below(1000) as i128 - 500,
        _ => (g.below(1 << 20) as i128) << g.below(40),
    };
    let width = match g.below(3) {
        0 => 1,
        1 => g.below(300) as i128 + 1,
        _ => (g.below(1 << 20) as i128 + 1) << g.below(40),
    };
    let high = (low + width - 1).min(18446744073709551615);
    (low.min(high), high)
}

/// Where a number is in a range: its ends, its middle, and the numbers next
/// to the ends, which are not in it.
fn around(low: i128, high: i128) -> Vec<i128> {
    vec![low - 1, low, low + (high - low) / 2, high, high + 1]
}

/// `x: a..b = n` compiles exactly when `n` is from `a` to `b - 1`, and gives
/// `n` back.
fn a_range_holds_exactly_the_numbers_from_its_first_bound_up_to_its_second() {
    generate::check(4001, 30, range, |(low, high)| {
        for n in around(*low, *high) {
            let source = with_header(format!(
                "fn main() ! console, alloc {{\n  x: {low}..{} = {n}\n  print(\"{{x}}\")\n}}\n",
                high + 1
            ));
            let fits = *low <= n && n <= *high;
            assert_eq!(compiles(&source), fits, "for\n{source}");
            if fits {
                assert_eq!(stdout(&source), format!("{n}\n"), "for\n{source}");
            }
        }
    });
}

/// A range is the type of a parameter and a result, a field, an item of a
/// list, and what an `Option` holds, and gives back the number it was given.
fn a_range_is_held_by_a_parameter_a_result_a_field_a_list_and_an_option() {
    generate::check(4002, 30, |g: &mut Gen| (range(g), g.below(5)), |((low, high), shape)| {
        let r = format!("{low}..{}", high + 1);
        let n = high;
        let (items, held, shown) = match shape {
            0 => (format!("fn pass(n: {r}) -> {r} {{ n }}\n"), format!("m: {r} = {n}"), "pass(m)"),
            1 => (format!("struct Box {{\n  n: {r}\n}}\n"), format!("b = Box(n: {n})"), "b.n"),
            2 => (String::new(), format!("xs: List[{r}] = [{n}, {low}]"), "xs[0]"),
            3 => (
                format!("fn found(o: Option[{r}]) -> {r} {{\n  match o {{\n    .some(n) {{ n }}\n    .none {{ {low} }}\n  }}\n}}\n"),
                format!("o: Option[{r}] = .some({n})"),
                "found(o)",
            ),
            _ => (String::new(), format!("t: ({r}, Byte) = ({n}, 1)\n  (a, b) = t"), "a"),
        };
        let source = with_header(format!(
            "{items}\nfn main() ! console, alloc {{\n  {held}\n  print(\"{{{shown}}}\")\n}}\n"
        ));
        assert_eq!(stdout(&source), format!("{n}\n"), "for\n{source}");
    });
}

/// `==`, `!=`, `<`, `<=`, `>`, `>=` and the text of a number work for a range
/// as for the size it is kept at, and say what the numbers say.
fn a_range_is_compared_and_shown_whatever_its_bounds() {
    generate::check(4003, 30, range, |(low, high)| {
        let r = format!("{low}..{}", high + 1);
        let (a, b) = (*low, *high);
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  x: {r} = {a}\n  y: {r} = {b}\n  print(\"{{x}} {{y}} {{x == y}} {{x != y}} {{x < y}} {{x <= y}} {{x > y}} {{x >= y}}\")\n}}\n"
        ));
        let want = format!("{a} {b} {} {} {} {} {} {}\n", a == b, a != b, a < b, a <= b, a > b, a >= b);
        assert_eq!(stdout(&source), want, "for\n{source}");
    });
}

/// The ranges that fill a size are the ones the names `Byte`, `Nat`, `Int`,
/// and the names of sizes stand for.
fn a_range_written_two_ways_is_one_type() {
    for (written, name) in [
        ("0..256", "Byte"),
        ("0..256", "u8"),
        ("0..18446744073709551616", "Nat"),
        ("0..18446744073709551616", "u64"),
        ("-9223372036854775808..9223372036854775808", "Int"),
        ("-9223372036854775808..9223372036854775808", "i64"),
        ("-128..128", "i8"),
        ("-32768..32768", "i16"),
        ("0..65536", "u16"),
        ("0..4294967296", "u32"),
        ("-2147483648..2147483648", "i32"),
    ] {
        let source = with_header(format!(
            "use std.bits ({name})\n\nfn same(n: {name}) -> {written} {{ n }}\n\nfn main() ! console, alloc {{\n  a: {written} = 7\n  b: {name} = same(a)\n  print(\"{{b}}\")\n}}\n"
        ));
        let source = if matches!(name, "Byte" | "Nat" | "Int") {
            source.replace(&format!("use std.bits ({name})\n"), "")
        } else {
            source
        };
        assert_eq!(stdout(&source), "7\n", "for\n{source}");
    }
}

/// `5..5` is a type that no value is given to, so a function that takes one
/// is accepted and no call of it is. A range that ends before it begins is
/// refused.
fn a_range_with_no_number_is_a_type_and_a_range_that_ends_before_it_begins_is_refused() {
    generate::check(4004, 20, range, |(low, _)| {
        let empty = with_header(format!(
            "fn never(x: {low}..{low}) -> Nat {{ 0 }}\n\nfn main() ! console, alloc {{\n  print(\"1\")\n}}\n"
        ));
        assert!(compiles(&empty), "for\n{empty}");
        let called = with_header(format!(
            "fn never(x: {low}..{low}) -> Nat {{ 0 }}\n\nfn main() ! console, alloc {{\n  print(\"{{never({low})}}\")\n}}\n"
        ));
        assert!(!compiles(&called), "for\n{called}");
        let backwards = with_header(format!(
            "fn never(x: {low}..{}) -> Nat {{ 0 }}\n\nfn main() ! console, alloc {{\n  print(\"1\")\n}}\n",
            low - 1
        ));
        assert!(!compiles(&backwards), "for\n{backwards}");
    });
}

/// A range that holds a number needing more than 64 bits is no type.
fn a_range_of_more_than_64_bits_is_refused() {
    for r in [
        "0..18446744073709551617",
        "-9223372036854775809..1",
        "0..36893488147419103232",
        "-18446744073709551616..0",
    ] {
        let source = with_header(format!(
            "fn big(x: {r}) -> Nat {{ 0 }}\n\nfn main() ! console, alloc {{\n  print(\"{{big(1)}}\")\n}}\n"
        ));
        let errors = check(source.as_bytes()).expect_err("a range of more than 64 bits");
        assert!(errors.iter().any(|e| e.message.contains("64 bits")), "{errors:?} for\n{source}");
    }
}

/// `i + 1` of a range `0..n` compiles into the same range exactly when `i` is
/// known to be below the greatest number, as for the sizes.
fn arith_proves_a_sum_of_a_range_exactly_when_it_fits_the_range() {
    generate::check(4005, 20, |g: &mut Gen| g.below(1000) as i128 + 2, |n| {
        let proven = with_header(format!(
            "fn next(i: 0..{n}) -> 0..{n}\n  requires i < {}\n{{\n  i + 1\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{next({})}}\")\n}}\n",
            n - 1,
            n - 2
        ));
        assert_eq!(stdout(&proven), format!("{}\n", n - 1), "for\n{proven}");
        let unproven = with_header(format!(
            "fn next(i: 0..{n}) -> 0..{n} {{ i + 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{next(0)}}\")\n}}\n"
        ));
        let errors = check(unproven.as_bytes()).expect_err("a sum that may not fit");
        assert!(errors.iter().all(|e| e.message.contains("i + 1")), "{errors:?}");
    });
}
