//! A `str` is made from bytes by `utf8`, which takes a `Slice[u8]`, reads the
//! bytes once, and copies nothing. The text is the bytes of the slice, so it
//! must not change when other lists reuse memory afterwards.
//!
//! Every generator builds the bytes it means and keeps them, so the model is
//! plain Rust: `std::str::from_utf8` says what is valid and where the first
//! byte that is not valid is.

use smith::scalar::{self, Op, handler};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "utf8_of_a_frozen_list_is_what_the_standard_says_of_its_bytes",
        utf8_of_a_frozen_list_is_what_the_standard_says_of_its_bytes,
    ),
    (
        "every_byte_of_a_text_made_by_utf8_is_read_after_other_lists_reuse_memory",
        every_byte_of_a_text_made_by_utf8_is_read_after_other_lists_reuse_memory,
    ),
    (
        "a_slice_that_utf8_refuses_is_still_read_by_the_caller",
        a_slice_that_utf8_refuses_is_still_read_by_the_caller,
    ),
    (
        "utf8_of_a_frozen_list_asks_for_the_memory_of_the_list_and_copies_no_byte",
        utf8_of_a_frozen_list_asks_for_the_memory_of_the_list_and_copies_no_byte,
    ),
    (
        "utf8_of_a_list_is_one_error_and_a_str_is_not_built_by_hand",
        utf8_of_a_list_is_one_error_and_a_str_is_not_built_by_hand,
    ),
    (
        "a_piece_of_the_bytes_of_a_text_is_a_text_exactly_when_both_ends_fall_between_characters",
        a_piece_of_the_bytes_of_a_text_is_a_text_exactly_when_both_ends_fall_between_characters,
    ),
    (
        "what_is_known_of_the_size_of_a_text_is_known_of_the_size_of_its_bytes_and_the_other_way",
        what_is_known_of_the_size_of_a_text_is_known_of_the_size_of_its_bytes_and_the_other_way,
    ),
    (
        "the_bytes_of_a_text_and_a_piece_of_them_as_text_ask_for_no_memory",
        the_bytes_of_a_text_and_a_piece_of_them_as_text_ask_for_no_memory,
    ),
    (
        "the_bytes_of_a_text_are_not_assigned_to",
        the_bytes_of_a_text_are_not_assigned_to,
    ),
];

const CHARACTERS: &[char] = &['a', 'Z', ' ', 'é', 'ж', '€', '中', '\u{1F600}'];

/// Bytes of generated characters, with one defect that the standard forbids
/// put in half of the draws.
fn bytes(g: &mut Gen) -> Vec<u8> {
    let mut bytes = Vec::new();
    for _ in 0..g.below(8) {
        let mut buffer = [0; 4];
        bytes.extend_from_slice(g.pick(CHARACTERS).encode_utf8(&mut buffer).as_bytes());
    }
    if g.one_in(2) {
        let defect: &[&[u8]] = &[&[0xff], &[0x80], &[0xc0, 0x80], &[0xed, 0xa0, 0x80], &[0xe2, 0x82]];
        let at = g.below(bytes.len() + 1);
        let defect = *g.pick(defect);
        bytes.splice(at..at, defect.iter().copied());
    }
    bytes
}

fn written(bytes: &[u8]) -> String {
    let items: Vec<String> = bytes.iter().map(u8::to_string).collect();
    format!("[{}]", items.join(", "))
}

fn prints(source: &str, lines: Vec<String>) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(
        scalar::run(&program),
        scalar::Outcome {
            stdout: stdout.into_bytes(),
            status: 0
        },
        "for {source}"
    );
}

fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// What the program of the tests prints for bytes: `ok` and every byte, or
/// `err` and the place.
fn expected(bytes: &[u8]) -> Vec<String> {
    match std::str::from_utf8(bytes) {
        Ok(_) => {
            let mut lines = vec![format!("ok {}", bytes.len())];
            lines.extend(bytes.iter().map(u8::to_string));
            lines
        }
        Err(e) => vec![format!("err {}", e.valid_up_to())],
    }
}

fn utf8_of_a_frozen_list_is_what_the_standard_says_of_its_bytes() {
    generate::check(401, 100, bytes, |bytes| {
        let source = format!(
            "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {{\n  b: List[Byte] = {}\n  match utf8(freeze(b)) {{\n    .ok(t) {{\n      print(\"ok {{t.size}}\")\n      loop i in 0..t.size {{ print(\"{{t[i]}}\") }}\n    }}\n    .err(e) {{ print(\"err {{e.at}}\") }}\n  }}\n}}\n",
            written(bytes)
        );
        prints(&source, expected(bytes));
    });
}

/// A text is made, then lists that grow and write in memory are built, and
/// then every byte of the text is read: a text whose bytes changed is wrong.
fn every_byte_of_a_text_made_by_utf8_is_read_after_other_lists_reuse_memory() {
    generate::check(402, 60, bytes, |bytes| {
        let source = format!(
            "use std.bits (u64)\nuse std.console (console, print)\nuse std.list (push)\nuse std.utf8 (utf8)\n\nfn fill(n: u64, v: Byte) -> List[Byte] ! alloc {{\n  var xs: List[Byte] = []\n  loop i in 0..n {{ xs = push(xs, v) }}\n  xs\n}}\n\nfn main() ! console, alloc {{\n  b: List[Byte] = {}\n  match utf8(freeze(b)) {{\n    .ok(t) {{\n      a = fill(40, 255)\n      c = fill(300, 254)\n      d = fill(9, 253)\n      print(\"ok {{t.size}} {{a.size}} {{c.size}} {{d.size}}\")\n      loop i in 0..t.size {{ print(\"{{t[i]}}\") }}\n    }}\n    .err(e) {{\n      a = fill(40, 255)\n      print(\"err {{e.at}} {{a.size}}\")\n    }}\n  }}\n}}\n",
            written(bytes)
        );
        let lines = match std::str::from_utf8(bytes) {
            Ok(_) => {
                let mut lines = vec![format!("ok {} 40 300 9", bytes.len())];
                lines.extend(bytes.iter().map(u8::to_string));
                lines
            }
            Err(e) => vec![format!("err {} 40", e.valid_up_to())],
        };
        prints(&source, lines);
    });
}

fn a_slice_that_utf8_refuses_is_still_read_by_the_caller() {
    let source = "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {\n  s: Slice[Byte] = [255, 104]\n  match utf8(s) {\n    .ok(t) { print(\"ok\") }\n    .err(e) { print(\"err {e.at}\") }\n  }\n  print(\"{s.size} {s[0]} {s[1]}\")\n}\n";
    prints(source, ["err 0", "2 255 104"].map(String::from).to_vec());
}

fn utf8_of_a_frozen_list_asks_for_the_memory_of_the_list_and_copies_no_byte() {
    let take = handler::take();
    let allocations = |source: &str| -> usize {
        let program = lower(source.as_bytes()).expect("compiles");
        program
            .functions
            .iter()
            .flat_map(|f| &f.blocks)
            .flat_map(|b| &b.instructions)
            .filter(|i| {
                matches!(&i.op, Op::Call { function, .. } if program.functions[*function] == take)
            })
            .count()
    };
    let without = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  b: List[Byte] = [104, 105]\n  print(\"ok\")\n}\n";
    let with = "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {\n  b: List[Byte] = [104, 105]\n  match utf8(freeze(b)) {\n    .ok(t) { print(\"ok\") }\n    .err(e) { print(\"err\") }\n  }\n}\n";
    assert_eq!(allocations(with), allocations(without));
}

fn utf8_of_a_list_is_one_error_and_a_str_is_not_built_by_hand() {
    let list = "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {\n  b: List[Byte] = [104]\n  match utf8(b) {\n    .ok(t) { print(\"ok\") }\n    .err(e) { print(\"err\") }\n  }\n}\n";
    assert_eq!(refused(list).len(), 1, "{:?}", refused(list));
    let by_hand = "use std.console (console)\n\nfn main() ! console {\n  s = str(bytes: [104])\n}\n";
    assert!(!refused(by_hand).is_empty());
}

/// A text of generated characters and two places in it, `a <= z`.
fn text_and_places(g: &mut Gen) -> (Vec<u8>, usize, usize) {
    let mut bytes = Vec::new();
    for _ in 0..g.below(6) {
        let mut buffer = [0; 4];
        bytes.extend_from_slice(g.pick(CHARACTERS).encode_utf8(&mut buffer).as_bytes());
    }
    let a = g.below(bytes.len() + 1);
    let z = a + g.below(bytes.len() - a + 1);
    (bytes, a, z)
}

fn a_piece_of_the_bytes_of_a_text_is_a_text_exactly_when_both_ends_fall_between_characters() {
    generate::check(403, 60, text_and_places, |(bytes, a, z)| {
        let source = format!(
            "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {{\n  match utf8(freeze({})) {{\n    .ok(t) {{\n      a: Nat = {a}\n      z: Nat = {z}\n      b = t.bytes\n      if a <= z and z <= t.size {{\n        match utf8(b[a..z]) {{\n          .ok(u) {{\n            print(\"ok {{u.size}}\")\n            loop i in 0..u.size {{ print(\"{{u[i]}}\") }}\n          }}\n          .err(e) {{ print(\"err\") }}\n        }}\n      }}\n    }}\n    .err(e) {{ print(\"bad\") }}\n  }}\n}}\n",
            written(bytes)
        );
        let lines = if std::str::from_utf8(&bytes[*a..*z]).is_ok() {
            let mut lines = vec![format!("ok {}", z - a)];
            lines.extend(bytes[*a..*z].iter().map(u8::to_string));
            lines
        } else {
            vec!["err".to_string()]
        };
        prints(&source, lines);
    });
}

fn the_bytes_of_a_text_and_a_piece_of_them_as_text_ask_for_no_memory() {
    let take = handler::take();
    let allocations = |source: &str| -> usize {
        let program = lower(source.as_bytes()).expect("compiles");
        program
            .functions
            .iter()
            .flat_map(|f| &f.blocks)
            .flat_map(|b| &b.instructions)
            .filter(|i| {
                matches!(&i.op, Op::Call { function, .. } if program.functions[*function] == take)
            })
            .count()
    };
    let without = "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {\n  match utf8(freeze([104, 105])) {\n    .ok(t) { print(\"ok\") }\n    .err(e) { print(\"err\") }\n  }\n}\n";
    let with = "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {\n  match utf8(freeze([104, 105])) {\n    .ok(t) {\n      b = t.bytes\n      if 1 <= b.size {\n        match utf8(b[0..1]) {\n          .ok(u) { print(\"ok\") }\n          .err(e) { print(\"err\") }\n        }\n      }\n    }\n    .err(e) { print(\"err\") }\n  }\n}\n";
    assert_eq!(allocations(with), allocations(without));
}

fn the_bytes_of_a_text_are_not_assigned_to() {
    let source = |target: &str| {
        format!(
            "use std.console (console, print)\nuse std.utf8 (utf8)\n\nfn main() ! console, alloc {{\n  match utf8(freeze([104, 105])) {{\n    .ok(t) {{\n      var s = t\n      {target}\n    }}\n    .err(e) {{ print(\"err\") }}\n  }}\n}}\n"
        )
    };
    assert!(!refused(&source("s.bytes = [104]")).is_empty());
    assert!(!refused(&source("s.bytes[0] = 104")).is_empty());
}

fn what_is_known_of_the_size_of_a_text_is_known_of_the_size_of_its_bytes_and_the_other_way() {
    let source = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  s = \"h\u{e9}llo\"\n  if 3 <= s.size {\n    t = s.bytes[0..3]\n    print(\"{t.size}\")\n  }\n  if 2 <= s.bytes.size {\n    print(\"{s[1]}\")\n  }\n}\n";
    prints(source, ["3", "195"].map(String::from).to_vec());
}
