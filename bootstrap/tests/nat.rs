//! `Nat`, the type of a count: a type of its own that a program writes for a
//! parameter, a result, a field, and inside another type, which is the type
//! of the size of a list and of a text, and which meets each whole number type
//! as `knowledge/language/number-types.md` says.

use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::INTS;

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
        "convert_between_nat_and_every_whole_number_type_compiles_exactly_when_the_second_holds_every_value_of_the_first",
        convert_between_nat_and_every_whole_number_type_compiles_exactly_when_the_second_holds_every_value_of_the_first,
    ),
    (
        "a_nat_converted_to_a_u64_and_back_is_the_number_it_was",
        a_nat_converted_to_a_u64_and_back_is_the_number_it_was,
    ),
    (
        "a_nat_has_no_wrapping_function_and_no_bit_operation_and_a_u64_has_both",
        a_nat_has_no_wrapping_function_and_no_bit_operation_and_a_u64_has_both,
    ),
    (
        "a_nat_has_the_saturating_and_the_checked_functions_and_gives_what_a_u64_gives",
        a_nat_has_the_saturating_and_the_checked_functions_and_gives_what_a_u64_gives,
    ),
    (
        "a_nat_and_a_u64_use_one_copy_of_a_saturating_function",
        a_nat_and_a_u64_use_one_copy_of_a_saturating_function,
    ),
    (
        "a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function",
        a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function,
    ),
    (
        "a_nat_shares_the_functions_of_a_u64_that_is_beside_it",
        a_nat_shares_the_functions_of_a_u64_that_is_beside_it,
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

impl Whole {
    fn holds_all(self, other: Whole) -> bool {
        self.min <= other.min && other.max <= self.max
    }
}

const NAT: Whole = Whole {
    name: "Nat",
    min: 0,
    max: u64::MAX as i128,
};

/// `Nat` and every other whole number type.
fn whole() -> Vec<Whole> {
    let mut all: Vec<Whole> = INTS
        .iter()
        .map(|t| Whole {
            name: t.name,
            min: t.min(),
            max: t.max(),
        })
        .collect();
    all.push(NAT);
    all
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
        let source = format!(
            "fn same(n: Nat) -> Nat {{ n }}\n\nfn main() ! console, alloc {{\n  a: Nat = {n}\n  print(\"{{a}} {{same({n})}}\")\n}}\n"
        );
        assert_eq!(stdout(&source), format!("{n} {n}\n"), "for\n{source}");
    });
    for (written, fits) in [("0", true), ("18446744073709551615", true), ("-1", false), ("18446744073709551616", false)] {
        let source = format!("fn main() ! console, alloc {{\n  a: Nat = {written}\n  print(\"{{a}}\")\n}}\n");
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
            (format!("xs: List[u8] = [{items}]"), "xs.size"),
            (format!("xs: str = \"{text}\""), "xs.size"),
        ] {
            for ty in whole() {
                let source = format!(
                    "fn main() ! console, alloc {{\n  {given}\n  k: {} = {what}\n  print(\"{{k}}\")\n}}\n",
                    ty.name
                );
                if ty == NAT {
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
        let source = format!(
            "fn main() ! console, alloc {{\n  xs: List[u8] = [{}]\n  loop i in 0..xs.size {{\n    k: Nat = i\n    print(\"{{xs[k]}}\")\n  }}\n}}\n",
            items.join(", ")
        );
        assert_eq!(stdout(&source), items.iter().map(|i| format!("{i}\n")).collect::<String>());
        for ty in whole() {
            let source = format!(
                "fn main() ! console, alloc {{\n  xs: List[u8] = [1, 2, 3]\n  i: {} = 1\n  print(\"{{xs[i]}}\")\n}}\n",
                ty.name
            );
            assert_eq!(compiles(&source), ty == NAT, "for\n{source}");
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
                format!(
                    "fn f(x: {}, y: {}) -> {}\n  requires 0 <= x and x < 9 and 0 <= y and y < 9\n{{\n  x {op} y\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 1)}}\")\n}}\n",
                    a.name, b.name, a.name
                )
            } else {
                let op = COMPARE[(i + j) % COMPARE.len()];
                format!(
                    "fn f(x: {}, y: {}) -> bool {{ x {op} y }}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 2)}}\")\n}}\n",
                    a.name, b.name
                )
            };
            assert_eq!(compiles(&source), a == b && proven, "for\n{source}");
            if a != b && proven {
                assert_eq!(mistakes(&source), 1, "for\n{source}");
            }
        }
    }
}

/// `convert` between two whole number types compiles exactly when they
/// differ and the second holds every value of the first, which for `Nat` is
/// every unsigned type below it and `u64`, and from `Nat` is `u64` only.
fn convert_between_nat_and_every_whole_number_type_compiles_exactly_when_the_second_holds_every_value_of_the_first()
 {
    let all = whole();
    for from in &all {
        for to in &all {
            if *from != NAT && *to != NAT {
                continue;
            }
            let source = format!(
                "fn f(x: {}) -> {} {{ convert(x) }}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1)}}\")\n}}\n",
                from.name, to.name
            );
            let wanted = from != to && to.holds_all(*from);
            assert_eq!(compiles(&source), wanted, "for\n{source}");
            if !wanted {
                assert_eq!(mistakes(&source), 1, "for\n{source}");
            }
        }
    }
}

/// A `Nat` taken to a `u64` and back, and a `u64` taken to a `Nat` and back,
/// are the number they were, for the least and the greatest value and others.
fn a_nat_converted_to_a_u64_and_back_is_the_number_it_was() {
    generate::check(3633, 30, count, |n| {
        let source = format!(
            "fn main() ! console, alloc {{\n  a: Nat = {n}\n  b: u64 = convert(a)\n  c: Nat = convert(b)\n  d: u64 = {n}\n  e: Nat = convert(d)\n  f: u64 = convert(e)\n  print(\"{{b}} {{c}} {{e}} {{f}}\")\n}}\n"
        );
        assert_eq!(stdout(&source), format!("{n} {n} {n} {n}\n"), "for\n{source}");
    });
}

/// The wrapping functions and the functions of `Bits` are for the types that
/// are a word of the machine, and a `Nat` is not one.
fn a_nat_has_no_wrapping_function_and_no_bit_operation_and_a_u64_has_both() {
    const FUNCTIONS: [(&str, &str, &str); 9] = [
        ("wrapping_add", "std.int", "x, y"),
        ("wrapping_sub", "std.int", "x, y"),
        ("wrapping_mul", "std.int", "x, y"),
        ("bit_and", "std.bits", "x, y"),
        ("bit_or", "std.bits", "x, y"),
        ("bit_xor", "std.bits", "x, y"),
        ("bit_not", "std.bits", "x"),
        ("shift_left", "std.bits", "x, 1"),
        ("shift_right", "std.bits", "x, 1"),
    ];
    for (name, module, args) in FUNCTIONS {
        for ty in ["u64", "Nat"] {
            let source = format!(
                "use {module} ({name})\n\nfn main() ! console, alloc {{\n  x: {ty} = 6\n  y: {ty} = 3\n  print(\"{{{name}({args})}}\")\n}}\n"
            );
            assert_eq!(compiles(&source), ty == "u64", "for\n{source}");
            if ty == "Nat" {
                assert_eq!(mistakes(&source), 1, "for\n{source}");
            }
        }
    }
}

/// The saturating and the checked functions of a `Nat` give what they give on
/// a `u64` of the same values, for values drawn with the edges, and the
/// greatest `Nat` plus one stops at the greatest `Nat` and is `.none` when
/// checked.
fn a_nat_has_the_saturating_and_the_checked_functions_and_gives_what_a_u64_gives() {
    const FUNCTIONS: [&str; 6] = [
        "saturating_add",
        "saturating_sub",
        "saturating_mul",
        "checked_add",
        "checked_sub",
        "checked_mul",
    ];
    generate::check(3637, 12, |g: &mut Gen| (count(g), count(g)), |(a, b)| {
        let program = |ty: &str| {
            let mut source = format!("fn main() ! console, alloc {{\n  a: {ty} = {a}\n  b: {ty} = {b}\n");
            for f in FUNCTIONS {
                source += &format!("  print(\"{{{f}(a, b)}}\")\n");
            }
            source + "}\n"
        };
        assert_eq!(stdout(&program("Nat")), stdout(&program("u64")), "for\n{}", program("Nat"));
    });
    let source = "fn next_line(line: Nat) -> Nat { saturating_add(line, 1) }\n\nfn main() ! console, alloc {\n  top: Nat = 18446744073709551615\n  print(\"{next_line(top)} {next_line(4)}\")\n  c: Option[Nat] = checked_add(top, 1)\n  print(\"{c == .none}\")\n}\n";
    assert_eq!(stdout(source), "18446744073709551615 5\ntrue\n", "for\n{source}");
}

/// A program that uses a saturating function on a `Nat` and on a `u64` holds
/// the functions it holds when it uses it on two `u64`.
fn a_nat_and_a_u64_use_one_copy_of_a_saturating_function() {
    generate::check(3638, 6, |g: &mut Gen| g.below(6), |which| {
        let f = ["saturating_add", "saturating_sub", "saturating_mul", "checked_add", "checked_sub", "checked_mul"][*which];
        let source = |a: &str| {
            format!(
                "fn main() ! console, alloc {{\n  xs: List[u8] = [1, 2]\n  a: {a} = 7\n  b: u64 = 3\n  print(\"{{{f}(a, a)}} {{{f}(b, b)}}\")\n}}\n"
            )
        };
        let nat = source("Nat").replace("a: Nat = 7", "a: Nat = xs.size");
        let functions = |source: &str| {
            lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}")).functions.len()
        };
        assert_eq!(functions(&nat), functions(&source("u64")), "for\n{nat}");
    });
}

/// A `Nat` is a field of a struct, an item of a tuple and of a list, inside an
/// `Option`, and the parameter and the result of a function, and each gives
/// back the number it was given.
fn a_nat_is_held_by_a_field_a_tuple_a_list_an_option_and_a_function() {
    generate::check(3634, 30, |g: &mut Gen| (g.below(5), count(g)), |(shape, n)| {
        let (items, held, shown) = match shape {
            0 => ("struct Box {\n  n: Nat\n}\n", format!("b = Box(n: {n})"), "b.n"),
            1 => ("", format!("t: (Nat, u8) = ({n}, 1)\n  (a, b) = t"), "a"),
            2 => ("", format!("xs: List[Nat] = [{n}, 2]"), "xs[0]"),
            3 => (
                "fn found(o: Option[Nat]) -> Nat {\n  match o {\n    .some(n) { n }\n    .none { 0 }\n  }\n}\n",
                format!("o: Option[Nat] = .some({n})"),
                "found(o)",
            ),
            _ => ("fn pass(n: Nat) -> Nat { n }\n", format!("m: Nat = {n}"), "pass(m)"),
        };
        let source = format!("{items}\nfn main() ! console, alloc {{\n  {held}\n  print(\"{{{shown}}}\")\n}}\n");
        assert_eq!(stdout(&source), format!("{n}\n"), "for\n{source}");
    });
}

/// `arith` is asked of a `Nat` what it is asked of any whole number: `n + 1`
/// compiles when `n` is known to be below the greatest value, and not
/// otherwise, and `n - k` when `k` is known to be at most `n`.
fn arith_proves_a_sum_and_a_difference_of_a_nat_from_what_is_known_of_it() {
    generate::check(3635, 8, |g: &mut Gen| (g.below(1000), count(g)), |(bound, n)| {
        let bound = bound + 1;
        let next = format!(
            "fn next(n: Nat) -> Nat requires n < {bound} {{ n + 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{next({})}}\")\n}}\n",
            bound - 1
        );
        assert_eq!(stdout(&next), format!("{bound}\n"), "for\n{next}");
        let unproven = "fn next(n: Nat) -> Nat { n + 1 }\n\nfn main() ! console, alloc {\n  print(\"{next(1)}\")\n}\n";
        let errors = check(unproven.as_bytes()).expect_err("a sum that may not fit");
        assert!(errors.iter().all(|e| e.message.contains("n + 1")), "{errors:?}");
        let top = format!(
            "fn next(n: Nat) -> Nat requires n < 18446744073709551615 {{ n + 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{next({})}}\")\n}}\n",
            n.min(&18446744073709551614)
        );
        assert!(compiles(&top), "for\n{top}");
        let drop = "fn drop(n: Nat, k: Nat) -> Nat {\n  if k <= n { n - k } else { 0 }\n}\n\nfn main() ! console, alloc {\n  print(\"{drop(5, 2)} {drop(2, 5)}\")\n}\n";
        assert_eq!(stdout(drop), "3 0\n");
        let refused = "fn drop(n: Nat, k: Nat) -> Nat { n - k }\n\nfn main() ! console, alloc {\n  print(\"{drop(5, 2)}\")\n}\n";
        assert!(!compiles(refused));
        let sizes = "fn main() ! console, alloc {\n  xs: List[u8] = [1, 2]\n  var last: Nat = 0\n  loop i in 0..xs.size { last = i + 1 }\n  print(\"{last}\")\n}\n";
        assert_eq!(stdout(sizes), "2\n");
    });
}

/// A `Nat` costs what a `u64` costs: where a program holds a count that it
/// held in a `u64` before, and a `u64` beside it, the program has the
/// functions it had, since the scalar form has one type of 64 bits and a
/// function that works on it is held once.
fn a_nat_shares_the_functions_of_a_u64_that_is_beside_it() {
    generate::check(3636, 12, |g: &mut Gen| (g.below(3), g.below(5) + 1, g.below(5)), |(uses, n, m)| {
        let used = [
            "print(\"{a} {b}\")",
            "print(\"{a == a} {b == b}\")",
            "print(\"{a < a} {b < b} {a >= a}\")",
        ][*uses];
        let source = |a: &str| {
            let items = vec!["1"; *n].join(", ");
            format!(
                "fn main() ! console, alloc {{\n  xs: List[u8] = [{items}]\n  a: {a} = {n}\n  b: u64 = {m}\n  {used}\n}}\n"
            )
        };
        let nat = source("Nat").replace(&format!("a: Nat = {n}"), "a: Nat = xs.size");
        let functions = |source: &str| {
            lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}")).functions.len()
        };
        assert_eq!(functions(&nat), functions(&source("u64")), "for\n{nat}");
    });
}
