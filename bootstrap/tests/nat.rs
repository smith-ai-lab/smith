//! `Nat`, the type of a count, which is `u64` written another way: it is
//! written for a parameter, a result, a field, and inside another type, and it
//! is the type of the size of a list and of a text.

use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, with_header};

pub const ONCE: &[Test] = &[
    (
        "a_literal_is_a_nat_where_one_is_needed_and_is_refused_when_it_does_not_fit",
        a_literal_is_a_nat_where_one_is_needed_and_is_refused_when_it_does_not_fit,
    ),
    (
        "the_size_of_a_list_and_of_a_text_is_a_nat_and_no_other_type",
        the_size_of_a_list_and_of_a_text_is_a_nat_and_no_other_type,
    ),
    (
        "a_loop_over_the_size_gives_a_nat_that_indexes_and_no_other_type_indexes",
        a_loop_over_the_size_gives_a_nat_that_indexes_and_no_other_type_indexes,
    ),
    (
        "an_operator_between_two_types_compiles_exactly_when_they_are_one_type_for_nat_and_every_whole_number_type",
        an_operator_between_two_types_compiles_exactly_when_they_are_one_type_for_nat_and_every_whole_number_type,
    ),
    (
        "nat_and_u64_are_one_type",
        nat_and_u64_are_one_type,
    ),
    (
        "a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function",
        a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function,
    ),
    (
        "arith_proves_a_sum_and_a_difference_of_a_nat_from_what_is_known_of_it",
        arith_proves_a_sum_and_a_difference_of_a_nat_from_what_is_known_of_it,
    ),
];

/// A whole number type as the tests read it: its name and the values it holds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Whole {
    name: &'static str,
    min: i128,
    max: i128,
}

/// Every whole number type, `Nat` being `u64`.
fn whole() -> Vec<Whole> {
    INTS.iter()
        .map(|t| Whole {
            name: t.name,
            min: t.min(),
            max: t.max(),
        })
        .collect()
}

fn stdout(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    let out = scalar::run(&program);
    assert_eq!(out.status, 0, "for\n{source}");
    String::from_utf8(out.stdout).unwrap()
}

fn compiles(source: &str) -> bool {
    check(source.as_bytes()).is_ok()
}

/// How many mistakes the program has, with a mistake that is reported twice
/// counted once.
fn mistakes(source: &str) -> usize {
    let mut errors = check(source.as_bytes()).err().unwrap_or_default();
    errors.dedup_by(|a, b| a.pos == b.pos && a.message == b.message);
    errors.len()
}

/// A value of `Nat`, the least and the greatest as often as another.
fn count(g: &mut Gen) -> u64 {
    match g.below(4) {
        0 => 0,
        1 => u64::MAX,
        2 => g.below(100) as u64,
        _ => (g.below(1 << 30) as u64) << g.below(34),
    }
}

/// A literal is a `Nat` where one is needed, as it is any whole number type,
/// and one the type does not hold is refused where it is written.
fn a_literal_is_a_nat_where_one_is_needed_and_is_refused_when_it_does_not_fit() {
    generate::check(3630, 40, count, |n| {
        let source = with_header(format!(
            "fn same(n: Nat) -> Nat {{ n }}\n\nfn main() ! console, alloc {{\n  a: Nat = {n}\n  print(\"{{a}} {{same({n})}}\")\n}}\n"
        ));
        assert_eq!(stdout(&source), format!("{n} {n}\n"), "for\n{source}");
    });
    for (written, fits) in [("0", true), ("18446744073709551615", true), ("-1", false), ("18446744073709551616", false)] {
        let source = with_header(format!("fn main() ! console, alloc {{\n  a: Nat = {written}\n  print(\"{{a}}\")\n}}\n"));
        assert_eq!(compiles(&source), fits, "for\n{source}");
    }
}

/// `xs.size` and `s.size` have the type `Nat`: they are bound to a `Nat`, and
/// bound to any other whole number type they are refused with one error.
fn the_size_of_a_list_and_of_a_text_is_a_nat_and_no_other_type() {
    generate::check(3631, 6, |g: &mut Gen| g.below(6), |n| {
        let items = vec!["7"; *n].join(", ");
        let text = "a".repeat(*n);
        for (given, what) in [
            (format!("xs: List[Byte] = [{items}]"), "xs.size"),
            (format!("xs: str = \"{text}\""), "xs.size"),
        ] {
            for ty in whole() {
                let source = with_header(format!(
                    "fn main() ! console, alloc {{\n  {given}\n  k: {} = {what}\n  print(\"{{k}}\")\n}}\n",
                    ty.name
                ));
                if ty.name == "u64" {
                    assert_eq!(stdout(&source), format!("{n}\n"), "for\n{source}");
                } else {
                    assert_eq!(mistakes(&source), 1, "for\n{source}");
                }
            }
        }
    });
}

/// A `loop i in 0..xs.size` gives `i` the type `Nat`, and `xs[i]` compiles
/// there. An index of another type is refused, and one of type `Nat` that
/// `arith` shows below the size compiles.
fn a_loop_over_the_size_gives_a_nat_that_indexes_and_no_other_type_indexes() {
    generate::check(3632, 6, |g: &mut Gen| g.below(6), |n| {
        let items: Vec<String> = (0..*n).map(|i| i.to_string()).collect();
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  xs: List[Byte] = [{}]\n  loop i in 0..xs.size {{\n    k: Nat = i\n    print(\"{{xs[k]}}\")\n  }}\n}}\n",
            items.join(", ")
        ));
        assert_eq!(stdout(&source), items.iter().map(|i| format!("{i}\n")).collect::<String>());
        for ty in whole() {
            let source = with_header(format!(
                "fn main() ! console, alloc {{\n  xs: List[Byte] = [1, 2, 3]\n  i: {} = 1\n  print(\"{{xs[i]}}\")\n}}\n",
                ty.name
            ));
            assert_eq!(compiles(&source), ty.name == "u64", "for\n{source}");
        }
    });
}

/// The operators and comparisons take two sides of one type, for `Nat` as for
/// every other type, so a `Nat` and a `u64` meeting in one are refused, and
/// so is each other pair.
fn an_operator_between_two_types_compiles_exactly_when_they_are_one_type_for_nat_and_every_whole_number_type()
{
    const COMPARE: [&str; 6] = ["==", "!=", "<", "<=", ">", ">="];
    let all = whole();
    for (i, a) in all.iter().enumerate() {
        for (j, b) in all.iter().enumerate() {
            let mut proven = true;
            let source = if (i + j) % 2 == 0 {
                // A difference is not proven to fit from the ranges of its sides,
                // and a product of two variables is not one `arith` reasons about.
                let op = ["+", "-", "*"][(i / 2 + j) % 3];
                proven = op == "+";
                with_header(format!(
                    "fn f(x: {}, y: {}) -> {}\n  requires 0 <= x and x < 9 and 0 <= y and y < 9\n{{\n  x {op} y\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 1)}}\")\n}}\n",
                    a.name, b.name, a.name
                ))
            } else {
                let op = COMPARE[(i + j) % COMPARE.len()];
                with_header(format!(
                    "fn f(x: {}, y: {}) -> bool {{ x {op} y }}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 2)}}\")\n}}\n",
                    a.name, b.name
                ))
            };
            assert_eq!(compiles(&source), a == b && proven, "for\n{source}");
            if a != b && proven {
                assert_eq!(mistakes(&source), 1, "for\n{source}");
            }
        }
    }
}

/// A `Nat` is a field of a struct, an item of a tuple and of a list, inside an
/// `Option`, and the parameter and the result of a function, and each gives
/// back the number it was given.
fn a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function() {
    generate::check(3634, 30, |g: &mut Gen| (g.below(5), count(g)), |(shape, n)| {
        let (items, held, shown) = match shape {
            0 => ("struct Box {\n  n: Nat\n}\n", format!("b = Box(n: {n})"), "b.n"),
            1 => ("", format!("t: (Nat, Byte) = ({n}, 1)\n  (a, b) = t"), "a"),
            2 => ("", format!("xs: List[Nat] = [{n}, 2]"), "xs[0]"),
            3 => (
                "fn found(o: Option[Nat]) -> Nat {\n  match o {\n    .some(n) { n }\n    .none { 0 }\n  }\n}\n",
                format!("o: Option[Nat] = .some({n})"),
                "found(o)",
            ),
            _ => ("fn pass(n: Nat) -> Nat { n }\n", format!("m: Nat = {n}"), "pass(m)"),
        };
        let source = with_header(format!(
            "{items}\nfn main() ! console, alloc {{\n  {held}\n  print(\"{{{shown}}}\")\n}}\n"
        ));
        assert_eq!(stdout(&source), format!("{n}\n"), "for\n{source}");
    });
}

/// `arith` is asked of a `Nat` what it is asked of any whole number: `n + 1`
/// compiles when `n` is known to be below the greatest value, and not
/// otherwise, and `n - k` when `k` is known to be at most `n`.
fn arith_proves_a_sum_and_a_difference_of_a_nat_from_what_is_known_of_it() {
    generate::check(3635, 8, |g: &mut Gen| (g.below(1000), count(g)), |(bound, n)| {
        let bound = bound + 1;
        let next = with_header(format!(
            "fn next(n: Nat) -> Nat requires n < {bound} {{ n + 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{next({})}}\")\n}}\n",
            bound - 1
        ));
        assert_eq!(stdout(&next), format!("{bound}\n"), "for\n{next}");
        let unproven = "use std.console (console, print)\n\nfn next(n: Nat) -> Nat { n + 1 }\n\nfn main() ! console, alloc {\n  print(\"{next(1)}\")\n}\n";
        let errors = check(unproven.as_bytes()).expect_err("a sum that may not fit");
        assert!(errors.iter().all(|e| e.message.contains("n + 1")), "{errors:?}");
        let top = with_header(format!(
            "fn next(n: Nat) -> Nat requires n < 18446744073709551615 {{ n + 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{next({})}}\")\n}}\n",
            n.min(&18446744073709551614)
        ));
        assert!(compiles(&top), "for\n{top}");
        let drop = "use std.console (console, print)\n\nfn drop(n: Nat, k: Nat) -> Nat {\n  if k <= n { n - k } else { 0 }\n}\n\nfn main() ! console, alloc {\n  print(\"{drop(5, 2)} {drop(2, 5)}\")\n}\n";
        assert_eq!(stdout(drop), "3 0\n");
        let refused = "use std.console (console, print)\n\nfn drop(n: Nat, k: Nat) -> Nat { n - k }\n\nfn main() ! console, alloc {\n  print(\"{drop(5, 2)}\")\n}\n";
        assert!(!compiles(refused));
        let sizes = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  xs: List[Byte] = [1, 2]\n  var last: Nat = 0\n  loop i in 0..xs.size { last = i + 1 }\n  print(\"{last}\")\n}\n";
        assert_eq!(stdout(sizes), "2\n");
    });
}


/// `Nat` and `u64` are one type with two names: a value of one is the other
/// with nothing written, in a binding, an argument, a result, an operator,
/// and inside another type, and the functions of `u64` serve a `Nat`.
fn nat_and_u64_are_one_type() {
    generate::check(3639, 20, count, |n| {
        let source = with_header(format!(
            "use std.bits (u64)\nuse std.int (wrapping_add)\n\nfn count(n: Nat) -> u64 {{ n }}\n\nfn back(n: u64) -> Nat {{ n }}\n\nfn main() ! console, alloc {{\n  a: Nat = {n}\n  b: u64 = a\n  l: List[Nat] = [a]\n  m: List[u64] = l\n  print(\"{{count(a)}} {{back(b)}} {{a == b}} {{wrapping_add(a, b)}} {{m[0]}}\")\n}}\n"
        ));
        let twice = (*n as u128 * 2) as u64;
        assert_eq!(stdout(&source), format!("{n} {n} true {twice} {n}\n"), "for\n{source}");
    });
}
