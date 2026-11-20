//! The exact range of a bit function of numbers that are not negative, as
//! `bits.md` says: the examples of the page, and for every pair of small
//! ranges a result type that holds every result and whose ends are reached.

use smith::scalar;
use smith::{check, lower};

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "the_examples_of_bits_md_give_what_the_page_says",
        the_examples_of_bits_md_give_what_the_page_says,
    ),
    (
        "the_range_of_bit_and_is_the_least_to_the_greatest_result",
        the_range_of_bit_and_is_the_least_to_the_greatest_result,
    ),
    (
        "the_range_of_bit_or_is_the_least_to_the_greatest_result",
        the_range_of_bit_or_is_the_least_to_the_greatest_result,
    ),
    (
        "the_range_of_bit_xor_is_the_least_to_the_greatest_result",
        the_range_of_bit_xor_is_the_least_to_the_greatest_result,
    ),
    (
        "the_range_of_shift_left_is_the_least_to_the_greatest_result",
        the_range_of_shift_left_is_the_least_to_the_greatest_result,
    ),
    (
        "the_range_of_shift_right_is_the_least_to_the_greatest_result",
        the_range_of_shift_right_is_the_least_to_the_greatest_result,
    ),
];

fn prints(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// The UTF-8 lines of the page enter a `Byte` with nothing written, and the
/// shifts print what it says.
fn the_examples_of_bits_md_give_what_the_page_says() {
    let utf8 = "use std.bits (bit_and, bit_or, shift_right)\nuse std.console (console, print)\n\n\
        fn low(x: Nat) -> Byte { bit_and(x, 255) }\n\n\
        fn lead(code: 0..2048) -> Byte { bit_or(192, shift_right(code, 6)) }\n\n\
        fn rest(code: 0..2048) -> Byte { bit_or(128, bit_and(code, 63)) }\n\n\
        fn main() ! console, alloc {\n  print(\"{low(1000)} {lead(1000)} {rest(1000)}\")\n}\n";
    assert_eq!(prints(utf8), "232 207 168\n");
    let shifts = "use std.bits (bit_and, i8, shift_left, shift_right, u32)\nuse std.console (console, print)\n\
        use std.int (wrapping_shift_left)\n\n\
        fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }\n\n\
        fn main() ! console, alloc {\n  a: Byte = 0b1100\n  u: Byte = 0b1111_0000\n  s: i8 = -8\n\
        \x20 print(\"{second_byte(0x12345678)}\")\n  print(\"{shift_left(a, 2)} {shift_right(a, 2)}\")\n\
        \x20 print(\"{wrapping_shift_left(u, 1)} {shift_left(u, 1)} {shift_right(u, 1)}\")\n\
        \x20 print(\"{shift_left(s, 1)} {shift_right(s, 1)}\")\n}\n";
    assert_eq!(prints(shifts), "86\n48 3\n224 480 120\n-16 -4\n");
}

/// Every range from `a` to `b` less one, of numbers below `LIMIT`.
const LIMIT: i128 = 6;

fn ranges() -> Vec<(i128, i128)> {
    let mut out = Vec::new();
    for a in 0..LIMIT {
        for b in a + 1..=LIMIT {
            out.push((a, b));
        }
    }
    out
}

fn compiles(source: &str) -> bool {
    check(source.as_bytes()).is_ok()
}

/// A function over the two ranges that gives the range `lo..hi`.
fn program(call: &str, first: (i128, i128), second: (i128, i128), gives: (i128, i128)) -> String {
    format!(
        "use std.bits (bit_and, bit_or, bit_xor, shift_left, shift_right)\n\
         use std.console (console, print)\n\n\
         fn f(a: {}..{}, b: {}..{}) -> {}..{} {{ {call} }}\n\n\
         fn main() ! console, alloc {{\n  print(\"{{f({}, {})}}\")\n}}\n",
        first.0, first.1, second.0, second.1, gives.0, gives.1, first.0, second.0
    )
}

/// The function gives its exact range, and not a range that is one less at
/// either end.
fn exact(function: &str, op: fn(i128, i128) -> i128, second: &dyn Fn(i128) -> Vec<(i128, i128)>) {
    exact_of(ranges(), function, op, second);
}

fn exact_of(
    firsts: Vec<(i128, i128)>,
    function: &str,
    op: fn(i128, i128) -> i128,
    second: &dyn Fn(i128) -> Vec<(i128, i128)>,
) {
    for first in firsts {
        for second in second(first.0) {
            let mut results = Vec::new();
            for a in first.0..first.1 {
                for b in second.0..second.1 {
                    results.push(op(a, b));
                }
            }
            let (lo, hi) = (*results.iter().min().unwrap(), *results.iter().max().unwrap());
            let call = format!("{function}(a, b)");
            let named = format!("{function} of {first:?} and {second:?}");
            assert!(
                compiles(&program(&call, first, second, (lo, hi + 1))),
                "{named} is not {lo} to {hi}"
            );
            if lo < hi {
                assert!(
                    !compiles(&program(&call, first, second, (lo, hi))),
                    "{named} reaches {hi}"
                );
                assert!(
                    !compiles(&program(&call, first, second, (lo + 1, hi + 1))),
                    "{named} reaches {lo}"
                );
            }
        }
    }
}

fn every_second(_: i128) -> Vec<(i128, i128)> {
    ranges().into_iter().filter(|(a, b)| (a + b) % 2 == 0).collect()
}

fn the_range_of_bit_and_is_the_least_to_the_greatest_result() {
    exact("bit_and", |a, b| a & b, &every_second);
}

fn the_range_of_bit_or_is_the_least_to_the_greatest_result() {
    exact("bit_or", |a, b| a | b, &every_second);
}

fn the_range_of_bit_xor_is_the_least_to_the_greatest_result() {
    exact("bit_xor", |a, b| a ^ b, &every_second);
}

/// A shift by an amount from a range below 6 is `a` times two to the amount
/// over every amount in the range.
fn the_range_of_shift_left_is_the_least_to_the_greatest_result() {
    exact("shift_left", |a, b| a << b, &|_| ranges());
}

/// A shift right by an amount from a range below 6 is `a` divided by two to
/// the amount, over every amount in the range.
fn the_range_of_shift_right_is_the_least_to_the_greatest_result() {
    exact("shift_right", |a, b| a >> b, &|_| ranges());
}
