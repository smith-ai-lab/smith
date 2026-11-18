//! `std.ascii` and `std.fmt`, which are Smith source in `std/`. Each expectation
//! is written here from what the pages say or what Rust's formatting gives, so
//! it shares nothing with the library.

use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_four_kinds_of_ascii_are_what_a_table_of_every_byte_says",
        the_four_kinds_of_ascii_are_what_a_table_of_every_byte_says,
    ),
    (
        "hex_and_binary_give_what_rust_formats_padded_and_never_cut",
        hex_and_binary_give_what_rust_formats_padded_and_never_cut,
    ),
    (
        "the_examples_of_the_pages_on_ascii_and_fmt_hold",
        the_examples_of_the_pages_on_ascii_and_fmt_hold,
    ),
];

/// What the program prints, one line to each of `lines`.
fn prints(source: &str, lines: Vec<String>) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(
        scalar::run(&program),
        Outcome {
            stdout: stdout.into_bytes(),
            status: 0
        },
        "for {source}"
    );
}

/// The four kinds of a byte, from a table written out by byte value.
fn kinds(b: u8) -> [bool; 4] {
    let digit = (48..=57).contains(&b);
    let hex = digit || (97..=102).contains(&b) || (65..=70).contains(&b);
    let letter = (97..=122).contains(&b) || (65..=90).contains(&b);
    let space = [32, 9, 10, 13].contains(&b);
    [digit, hex, letter, space]
}

fn the_four_kinds_of_ascii_are_what_a_table_of_every_byte_says() {
    let source = "use std.ascii (is_digit, is_hex_digit, is_letter, is_space)\n\nfn main() ! console, alloc {\n  loop i in 0..256 {\n    b: u8 = wrapping_narrow(i)\n    print(\"{is_digit(b)} {is_hex_digit(b)} {is_letter(b)} {is_space(b)}\")\n  }\n}\n";
    let lines = (0..=255u8)
        .map(|b| kinds(b).map(|k| k.to_string()).join(" "))
        .collect();
    prints(source, lines);
}

/// A call of `hex` or `binary`: the number, the width, and the case.
#[derive(Debug)]
struct Call {
    n: u64,
    width: u8,
    upper: bool,
}

/// Numbers at the edges of every digit count, and numbers of any size.
fn call(g: &mut Gen) -> Call {
    let n = match g.below(4) {
        0 => *g.pick(&[0, 1, 9, 10, 15, 16, 255, 256, u64::MAX, u64::MAX - 1, 1 << 63]),
        1 => (1u64 << g.below(64)).wrapping_add(g.below(3) as u64).wrapping_sub(1),
        _ => (0..4).fold(0u64, |n, _| (n << 16) | g.below(1 << 16) as u64),
    };
    let width = *g.pick(&[0, 1, 2, 4, 8, 16, 17, 20, 63, 64, 65, 255]);
    Call { n, width, upper: g.one_in(2) }
}

fn hex_and_binary_give_what_rust_formats_padded_and_never_cut() {
    generate::check(311, 20, |g| (0..12).map(|_| call(g)).collect::<Vec<_>>(), |calls| {
        let mut source = String::from("use std.fmt (binary, hex)\n\nfn main() ! console, alloc {\n");
        let mut lines = Vec::new();
        for Call { n, width, upper } in calls {
            let w = *width as usize;
            source.push_str(&format!("  print(hex({n}, width: {width}, upper: {upper}))\n"));
            source.push_str(&format!("  print(binary({n}, width: {width}))\n"));
            lines.push(if *upper { format!("{n:0w$X}") } else { format!("{n:0w$x}") });
            lines.push(format!("{n:0w$b}"));
        }
        source.push_str("}\n");
        prints(&source, lines);
    });
}

fn the_examples_of_the_pages_on_ascii_and_fmt_hold() {
    let source = "use std.ascii (is_digit)\nuse std.fmt (binary, hex)\n\nfn digits(s: str) -> Nat {\n  var n: Nat = 0\n  loop i in 0..s.size {\n    if is_digit(s[i]) { n = saturating_add(n, 1) }\n  }\n  n\n}\n\nfn byte(b: u8) -> str ! alloc { hex(convert(b), width: 2) }\n\nfn main() ! console, alloc {\n  print(\"{digits(\"a1b22\")}\")\n  print(\"U+{hex(233, width: 4)}\")\n  print(byte(10))\n  print(hex(255))\n  print(hex(255, upper: false))\n  print(hex(255, width: 1))\n  print(hex(0))\n  print(hex(0, width: 4))\n  print(binary(5))\n  print(binary(5, width: 8))\n  print(binary(5, width: 2))\n  print(binary(0))\n  print(hex(18446744073709551615, width: 20))\n  print(binary(18446744073709551615))\n}\n";
    let lines = [
        "3",
        "U+00E9",
        "0A",
        "FF",
        "ff",
        "FF",
        "0",
        "0000",
        "101",
        "00000101",
        "101",
        "0",
        "0000FFFFFFFFFFFFFFFF",
        &"1".repeat(64),
    ];
    prints(source, lines.map(String::from).to_vec());
}
