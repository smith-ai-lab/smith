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
        "a_range_is_taken_where_a_range_that_holds_every_number_of_it_is_needed",
        a_range_is_taken_where_a_range_that_holds_every_number_of_it_is_needed,
    ),
    (
        "a_value_enters_a_narrower_range_exactly_when_a_condition_or_a_requires_shows_it_fits",
        a_value_enters_a_narrower_range_exactly_when_a_condition_or_a_requires_shows_it_fits,
    ),
    (
        "any_two_ranges_are_compared_as_the_numbers_they_hold_are",
        any_two_ranges_are_compared_as_the_numbers_they_hold_are,
    ),
    (
        "a_parameter_of_a_range_to_a_name_is_below_it_at_every_call_and_in_the_body",
        a_parameter_of_a_range_to_a_name_is_below_it_at_every_call_and_in_the_body,
    ),
    (
        "a_range_from_a_name_is_at_least_it_at_every_call",
        a_range_from_a_name_is_at_least_it_at_every_call,
    ),
    (
        "a_bound_that_is_not_a_parameter_or_is_written_where_names_are_not_built_is_refused",
        a_bound_that_is_not_a_parameter_or_is_written_where_names_are_not_built_is_refused,
    ),
    (
        "the_blocks_of_an_if_and_a_match_give_the_smallest_range_that_holds_them",
        the_blocks_of_an_if_and_a_match_give_the_smallest_range_that_holds_them,
    ),
    (
        "a_value_is_not_taken_where_a_range_that_does_not_hold_every_number_of_its_range_is_needed",
        a_value_is_not_taken_where_a_range_that_does_not_hold_every_number_of_its_range_is_needed,
    ),
    (
        "arith_proves_a_sum_of_a_range_exactly_when_it_fits_the_range",
        arith_proves_a_sum_of_a_range_exactly_when_it_fits_the_range,
    ),
    (
        "a_sum_a_difference_and_a_product_give_the_exact_range_and_no_narrower_one",
        a_sum_a_difference_and_a_product_give_the_exact_range_and_no_narrower_one,
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
    // `0..n` that is 2^64 fills the size of 64 bits, where the exact range of a sum
    // is not one a type holds, so the sum is proven as an operation and the fix
    // names the functions that wrap, saturate, and check. Of any other range the sum
    // is the exact range, which enters the result with a proof, and the fix says to
    // check the value first.
    const SIZES: [i128; 4] = [1 << 8, 1 << 16, 1 << 32, 1 << 64];
    let case = |n: i128| {
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
        let fixes: Vec<String> = errors.iter().filter_map(|e| e.fix.clone()).collect();
        assert!(!fixes.is_empty(), "{errors:?} for\n{unproven}");
        let names = n == 1 << 64;
        assert!(fixes.iter().all(|f| f.contains("wrapping_add") == names), "{fixes:?} for\n{unproven}");
    };
    // Both kinds are tried on every run: a range that fills a size and one that does
    // not, whatever the seed draws.
    for n in SIZES.into_iter().chain([2, 3, 255, 257, 65537]) {
        case(n);
    }
    generate::check(4005, 20, |g: &mut Gen| g.below(1000) as i128 + 2, |n| case(*n));
    let sized = with_header(
        "fn next(i: Byte) -> Byte { i + 1 }\n\nfn main() ! console, alloc {\n  print(\"{next(0)}\")\n}\n".to_string(),
    );
    let errors = check(sized.as_bytes()).expect_err("a sum that may not fit");
    let fixes: Vec<String> = errors.iter().filter_map(|e| e.fix.clone()).collect();
    assert!(fixes.iter().all(|f| !f.contains("wrapping_add")), "{errors:?}");
}

/// A range inside another: the other's bounds, a number of the first, and the
/// first's bounds.
fn nested(g: &mut Gen) -> ((i128, i128), (i128, i128), i128) {
    let (low, high) = range(g);
    let inner_low = low + g.below((high - low).min(1000) as usize + 1) as i128;
    let inner_high = inner_low + g.below((high - inner_low).min(1000) as usize + 1) as i128;
    let n = inner_low + g.below((inner_high - inner_low).min(1000) as usize + 1) as i128;
    ((low, high), (inner_low, inner_high), n)
}

/// A value of a range is taken, with nothing written, where a range that
/// holds every number of it is needed: bound, passed, given back, put in a
/// field, a variable, a list, and given by a block, as the number it is.
fn a_range_is_taken_where_a_range_that_holds_every_number_of_it_is_needed() {
    generate::check(4006, 30, nested, |((low, high), (ilow, ihigh), n)| {
        let wide = format!("{low}..{}", high + 1);
        let narrow = format!("{ilow}..{}", ihigh + 1);
        let source = with_header(format!(
            "struct Held {{\n  n: {wide}\n}}\n\nfn pass(x: {wide}) -> {wide} {{ x }}\n\nfn lift(x: {narrow}) -> {wide} {{ x }}\n\nfn pick(flag: bool, x: {narrow}) -> {wide} {{\n  if flag {{ x }} else {{ {ilow} }}\n}}\n\nfn which(o: Option[{narrow}]) -> {wide} {{\n  match o {{\n    .some(v) {{ v }}\n    .none {{ {ilow} }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n  a: {narrow} = {n}\n  b: {wide} = a\n  var c: {wide} = {low}\n  c = a\n  h = Held(n: a)\n  xs: List[{wide}] = [a, {low}]\n  print(\"{{b}} {{pass(a)}} {{lift(a)}} {{c}} {{h.n}} {{xs[0]}} {{pick(true, a)}} {{which(.some(a))}}\")\n}}\n"
        ));
        assert_eq!(stdout(&source), format!("{}\n", vec![n.to_string(); 8].join(" ")), "for\n{source}");
    });
}

/// A value of a range is not taken where the range needed does not hold every
/// number of it, and nothing says that the value is in it.
fn a_value_is_not_taken_where_a_range_that_does_not_hold_every_number_of_its_range_is_needed() {
    generate::check(4007, 30, |g: &mut Gen| (range(g), range(g)), |((a, b), (c, d))| {
        if *c <= *a && *b <= *d {
            return;
        }
        let source = with_header(format!(
            "fn same(x: {a}..{}) -> {c}..{} {{ x }}\n\nfn main() ! console, alloc {{\n  print(\"{{same({a})}}\")\n}}\n",
            b + 1,
            d + 1
        ));
        assert!(!compiles(&source), "for\n{source}");
    });
}

/// The blocks of an `if` or a `match` that give ranges, where no type is
/// needed, give the smallest range that holds them, whichever block is first:
/// it is the type of the name they are bound to, so it is taken where exactly
/// that range is needed, and the number is the one the block gave.
fn the_blocks_of_an_if_and_a_match_give_the_smallest_range_that_holds_them() {
    generate::check(4008, 30, |g: &mut Gen| (range(g), range(g), g.below(2)), |((a, b), (c, d), which)| {
        let (low, high) = (*a.min(c), *b.max(d));
        let hull_fits = if low < 0 { high <= i64::MAX as i128 } else { high <= u64::MAX as i128 };
        let want = [a, c][*which];
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  s: Byte = {which}\n  x: {a}..{} = {a}\n  y: {c}..{} = {c}\n  r = if s == 0 {{ x }} else {{ y }}\n  m = match s {{\n    0 {{ y }}\n    _ {{ x }}\n  }}\n  h: {low}..{} = r\n  k: {low}..{} = m\n  print(\"{{h}} {{k}}\")\n}}\n",
            b + 1,
            d + 1,
            high + 1,
            high + 1
        ));
        if !hull_fits {
            assert!(!compiles(&source), "for\n{source}");
            return;
        }
        let other = [c, a][*which];
        assert_eq!(stdout(&source), format!("{want} {other}\n"), "for\n{source}");
    });
}

/// A value of a range enters a narrower range, as a result, a binding, an
/// argument, and a block, only where the facts about it show it fits: a
/// `requires` or the condition of an `if` does, and nothing does not, and
/// the error says what could not be proven.
fn a_value_enters_a_narrower_range_exactly_when_a_condition_or_a_requires_shows_it_fits() {
    generate::check(4009, 30, nested, |((low, high), (ilow, ihigh), n)| {
        let wide = format!("{low}..{}", high + 1);
        let narrow = format!("{ilow}..{}", ihigh + 1);
        let strictly = ilow > low || ihigh < high;
        let lead = format!(
            "fn takes(v: {narrow}) -> {narrow} {{ v }}\n\nfn result(x: {wide}) -> {narrow}"
        );
        let facts = format!("requires x >= {ilow} and x <= {ihigh}");
        let tail = format!(
            "fn checked(x: {wide}) -> {narrow} {{\n  if x >= {ilow} and x <= {ihigh} {{ x }} else {{ {ilow} }}\n}}\n\nfn bound(x: {wide}) -> {narrow} {{\n  var y: {narrow} = {ilow}\n  if x >= {ilow} and x <= {ihigh} {{ y = x }}\n  y\n}}\n\nfn passed(x: {wide}) -> {narrow}\n  {facts}\n{{\n  takes(x)\n}}\n\nfn main() ! console, alloc {{\n  a: {wide} = {n}\n  print(\"{{result(a)}} {{checked(a)}} {{bound(a)}} {{passed(a)}}\")\n}}\n"
        );
        let proven = with_header(format!("{lead}\n  {facts}\n{{ x }}\n\n{tail}"));
        assert_eq!(stdout(&proven), format!("{}\n", vec![n.to_string(); 4].join(" ")), "for\n{proven}");
        let unproven = with_header(format!("{lead} {{ x }}\n\n{tail}"));
        if strictly {
            let errors = check(unproven.as_bytes()).expect_err("a value that may not fit");
            assert!(
                errors.iter().any(|e| e.message.contains("cannot prove") && e.message.contains(&narrow_name(*ilow, *ihigh))),
                "{errors:?} for\n{unproven}"
            );
        } else {
            assert!(compiles(&unproven), "for\n{unproven}");
        }
    });
}

/// How the error spells a range: by the name of the size it fills, if any.
fn narrow_name(low: i128, high: i128) -> String {
    for (name, l, h) in [
        ("u8", 0, 255),
        ("u16", 0, 65535),
        ("u32", 0, 4294967295),
        ("u64", 0, 18446744073709551615),
        ("i8", -128, 127),
        ("i16", -32768, 32767),
        ("i32", -2147483648, 2147483647),
        ("i64", -9223372036854775808, 9223372036854775807),
    ] {
        if (low, high) == (l, h) {
            return name.to_string();
        }
    }
    format!("{low}..{}", high + 1)
}

/// `==`, `!=`, `<`, `<=`, `>`, and `>=` between two different ranges say what
/// the numbers say, and are refused only where no type holds both ranges.
fn any_two_ranges_are_compared_as_the_numbers_they_hold_are() {
    generate::check(4010, 40, |g: &mut Gen| (range(g), range(g), g.below(2), g.below(2)), |((a, b), (c, d), x, y)| {
        let (low, high) = (*a.min(c), *b.max(d));
        let held = low >= i64::MIN as i128
            && high <= if low < 0 { i64::MAX as i128 } else { u64::MAX as i128 };
        let (x, y) = ([*a, *b][*x], [*c, *d][*y]);
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  p: {a}..{} = {x}\n  q: {c}..{} = {y}\n  print(\"{{p == q}} {{p != q}} {{p < q}} {{p <= q}} {{p > q}} {{p >= q}}\")\n}}\n",
            b + 1,
            d + 1
        ));
        if !held {
            assert!(!compiles(&source), "for\n{source}");
            return;
        }
        let want = format!("{} {} {} {} {} {}\n", x == y, x != y, x < y, x <= y, x > y, x >= y);
        assert_eq!(stdout(&source), want, "for\n{source}");
    });
}

/// A parameter written `0..n`, for a parameter `n`, is a number below it: the
/// body knows that, and a call compiles exactly when its argument is shown
/// to be below the argument for `n`. Where `n` is 0 no argument is.
fn a_parameter_of_a_range_to_a_name_is_below_it_at_every_call_and_in_the_body() {
    generate::check(4011, 30, |g: &mut Gen| (g.below(40) as u64, g.below(40) as u64), |(n, i)| {
        let source = |args: &str| {
            with_header(format!(
                "fn pick(n: Nat, i: 0..n) -> Nat {{ i }}\n\nfn inner(n: Nat, i: 0..n) -> Nat {{ i }}\n\nfn outer(n: Nat, i: 0..n) -> Nat {{ inner(n, i) }}\n\nfn main() ! console, alloc {{\n  print(\"{{pick({args})}} {{outer({args})}}\")\n}}\n"
            ))
        };
        let called = source(&format!("{n}, {i}"));
        if i < n {
            assert_eq!(stdout(&called), format!("{i} {i}\n"), "for\n{called}");
        } else {
            let errors = check(called.as_bytes()).expect_err("an argument that is not below");
            assert!(errors.iter().any(|e| e.message.contains("cannot prove")), "{errors:?} for\n{called}");
        }
        // A function that is not given a fact about `i` cannot give it on.
        let loose = with_header(
            "fn inner(n: Nat, i: 0..n) -> Nat { i }\n\nfn outer(n: Nat, i: Nat) -> Nat { inner(n, i) }\n\nfn main() ! console, alloc {\n  print(\"{outer(5, 2)}\")\n}\n"
                .to_string(),
        );
        assert!(!compiles(&loose), "for\n{loose}");
    });
}

/// A parameter written `m..256`, for a parameter `m`, is at least it.
fn a_range_from_a_name_is_at_least_it_at_every_call() {
    generate::check(4012, 30, |g: &mut Gen| (g.below(256) as u64, g.below(256) as u64), |(m, i)| {
        let source = with_header(format!(
            "fn from(m: Byte, i: m..256) -> Byte {{ i }}\n\nfn main() ! console, alloc {{\n  print(\"{{from({m}, {i})}}\")\n}}\n"
        ));
        if m <= i {
            assert_eq!(stdout(&source), format!("{i}\n"), "for\n{source}");
        } else {
            assert!(!compiles(&source), "for\n{source}");
        }
    });
}

/// A bound that is a name is a parameter of the function, other than the
/// parameter itself, and a whole number. Names are not built in a result, a
/// binding, a field, or inside another type.
fn a_bound_that_is_not_a_parameter_or_is_written_where_names_are_not_built_is_refused() {
    for (bad, call, mentions) in [
        ("fn f(n: Nat, i: 0..m) -> Nat { i }", "f(1, 0)", "`m`"),
        ("fn f(i: 0..i) -> Nat { 0 }", "f(0)", "`i`"),
        ("fn f(n: str, i: 0..n) -> Nat { 0 }", "f(\"a\", 0)", "whole numbers"),
        ("fn f(n: Nat) -> 0..n { 0 }", "f(1)", "not supported"),
        ("fn f(n: Nat) -> Nat {\n  x: 0..n = 0\n  0\n}", "f(1)", "not supported"),
        ("fn f(n: Nat, xs: List[0..n]) -> Nat { 0 }", "f(1, [0])", "not supported"),
        ("struct S {\n  a: 0..n\n}\n\nfn f(s: S) -> Nat { 0 }", "f(S(a: 0))", "not supported"),
    ] {
        let source = with_header(format!(
            "{bad}\n\nfn main() ! console, alloc {{\n  print(\"{{{call}}}\")\n}}\n"
        ));
        let errors = check(source.as_bytes()).expect_err("a bound that is not built");
        assert!(errors.iter().any(|e| e.message.contains(mentions)), "{errors:?} for\n{source}");
    }
}

/// A range of two to a few hundred numbers, near 0 or far from it, whose
/// least number and greatest are given.
fn small_range(g: &mut Gen) -> (i128, i128) {
    let low = match g.below(3) {
        0 => g.below(40) as i128 - 20,
        1 => g.below(2000) as i128 - 1000,
        _ => (g.below(1 << 16) as i128) << g.below(12),
    };
    (low, low + g.below(300) as i128)
}

/// `a + b`, `a - b`, and `a * b` of two numbers of any two ranges are the
/// range from the least to the greatest of the results at the ends of the
/// ranges, which a result that states it holds with nothing proven, and no
/// range that excludes either end holds.
fn a_sum_a_difference_and_a_product_give_the_exact_range_and_no_narrower_one() {
    generate::check(
        4006,
        60,
        |g: &mut Gen| (small_range(g), small_range(g), g.below(3)),
        |&((a_low, a_high), (b_low, b_high), which)| {
            let (op, f): (&str, fn(i128, i128) -> i128) = [
                ("+", (|x, y| x + y) as fn(i128, i128) -> i128),
                ("-", |x, y| x - y),
                ("*", |x, y| x * y),
            ][which];
            let corners = [
                f(a_low, b_low),
                f(a_low, b_high),
                f(a_high, b_low),
                f(a_high, b_high),
            ];
            let (low, high) = (
                *corners.iter().min().unwrap(),
                *corners.iter().max().unwrap(),
            );
            let program = |result_low: i128, result_high: i128| {
                with_header(format!(
                    "fn f(a: {a_low}..{}, b: {b_low}..{}) -> {result_low}..{} {{ a {op} b }}\n\nfn main() ! console, alloc {{\n  print(\"{{f({a_low}, {b_low})}} {{f({a_low}, {b_high})}} {{f({a_high}, {b_low})}} {{f({a_high}, {b_high})}}\")\n}}\n",
                    a_high + 1,
                    b_high + 1,
                    result_high + 1
                ))
            };
            let exact = program(low, high);
            let expected = format!(
                "{} {} {} {}\n",
                corners[0], corners[1], corners[2], corners[3]
            );
            assert_eq!(stdout(&exact), expected, "for\n{exact}");
            // A range wider than the exact one takes it with nothing written too.
            let wider = program(low - 3, high + 3);
            assert_eq!(stdout(&wider), expected, "for\n{wider}");
            // One that leaves out the least or the greatest number does not.
            for narrower in [program(low + 1, high), program(low, high - 1)] {
                if low < high {
                    assert!(!compiles(&narrower), "accepted\n{narrower}");
                }
            }
        },
    );
}
