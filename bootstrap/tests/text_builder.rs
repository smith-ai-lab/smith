//! `Text` of `std.text` is built with `text`, `append`, and `append_ascii`,
//! and becomes a `str` with `finish`.
//!
//! The generator keeps the text it builds as a `String`, so it shares nothing
//! with the compiler.

use smith::scalar;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_text_built_from_texts_and_ascii_bytes_finishes_as_their_bytes_in_order",
        a_text_built_from_texts_and_ascii_bytes_finishes_as_their_bytes_in_order,
    ),
    (
        "an_empty_text_asks_for_no_memory",
        an_empty_text_asks_for_no_memory,
    ),
    (
        "a_byte_that_may_be_128_or_more_is_refused_by_append_ascii",
        a_byte_that_may_be_128_or_more_is_refused_by_append_ascii,
    ),
    (
        "no_module_but_std_text_builds_or_reads_the_field_of_a_text",
        no_module_but_std_text_builds_or_reads_the_field_of_a_text,
    ),
    (
        "building_a_text_needs_alloc",
        building_a_text_needs_alloc,
    ),
];

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// One step of building: a text, or a byte below 128.
#[derive(Debug)]
enum Step {
    Text(String),
    Ascii(u8),
}

fn steps(g: &mut Gen) -> Vec<Step> {
    (0..g.below(12))
        .map(|_| match g.one_in(2) {
            true => Step::Ascii(g.below(128) as u8),
            false => Step::Text(
                (0..g.below(6))
                    .map(|_| *g.pick(&['a', 'Z', ' ', 'é', '語', '😀']))
                    .collect(),
            ),
        })
        .collect()
}

/// For every order of texts and bytes below 128, the text finished is the
/// bytes of each in turn, whatever room the text had.
fn a_text_built_from_texts_and_ascii_bytes_finishes_as_their_bytes_in_order() {
    generate::check(21, 40, steps, |steps| {
        let mut chain = String::from("text()");
        let mut model = Vec::new();
        for step in steps {
            match step {
                Step::Text(s) => {
                    chain += &format!(" |> append(\"{s}\")");
                    model.extend(s.bytes());
                }
                Step::Ascii(b) => {
                    chain += &format!(" |> append_ascii({b})");
                    model.push(*b);
                }
            }
        }
        let source = format!(
            "use std.console (console, print)\nuse std.text (append, append_ascii, finish, text)\n\nfn made() -> str ! alloc {{\n  {chain} |> finish()\n}}\n\nfn main() ! console, alloc {{\n  s = made()\n  loop i in 0..s.size {{ print(\"{{s[i]}}\") }}\n  print(\"{{s.size}}\")\n}}\n"
        );
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
        let lines: String = model
            .iter()
            .map(|b| format!("{b}\n"))
            .chain([format!("{}\n", model.len())])
            .collect();
        assert_eq!(scalar::run(&program).stdout, lines.into_bytes(), "for {source}");
    });
}

/// `text()` holds no memory, so a program that makes one and reads its size
/// asks for none.
fn an_empty_text_asks_for_no_memory() {
    let source = "use std.console (console, print)\nuse std.text (finish, text)\n\nfn main() ! console, alloc {\n  s = finish(text())\n  if s.size == 0 { print(\"empty\") }\n}\n";
    let program = lower(source.as_bytes()).unwrap();
    let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
    assert_eq!(outcome.stdout, b"empty\n");
    assert_eq!(taken.iter().filter(|n| **n != 0).count(), 0, "{taken:?}");
}

fn a_byte_that_may_be_128_or_more_is_refused_by_append_ascii() {
    let source = "use std.text (Text, append_ascii, finish, text)\n\nfn wrong(b: Byte) -> Text ! alloc { append_ascii(text(), b) }\n\nfn main() ! alloc { s = finish(wrong(65)) }\n";
    assert!(!refused(source).is_empty(), "{source}");
    let fine = "use std.text (Text, append_ascii, finish, text)\n\nfn right(b: 0..128) -> Text ! alloc { append_ascii(text(), b) }\n\nfn main() ! alloc { s = finish(right(65)) }\n";
    assert_eq!(refused(fine), Vec::<String>::new(), "{fine}");
}

fn no_module_but_std_text_builds_or_reads_the_field_of_a_text() {
    let built = "use std.text (Text)\n\nfn forged() -> Text { Text(bytes: []) }\n\nfn main() { forged() }\n";
    let read = "use std.text (Text, text)\n\nfn peek(t: Text) -> Nat { t.bytes.size }\n\nfn main() { peek(text()) }\n";
    for source in [built, read] {
        let errors = refused(source);
        assert!(
            errors.iter().any(|e| e.contains("bytes")),
            "{errors:?} does not name the field for {source}"
        );
    }
}

fn building_a_text_needs_alloc() {
    let source = "use std.text (append, text)\n\nfn grown() { text() |> append(\"a\") }\n\nfn main() { grown() }\n";
    let errors = refused(source);
    assert!(errors.iter().any(|e| e.contains("`alloc`")), "{errors:?}");
}
