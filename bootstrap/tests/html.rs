//! `std.html` writes a page, and every text in it is written so that it is
//! text and nothing else.
//!
//! The generator builds the texts and the reference that says what each must
//! become is written here, so the tests share nothing with the library.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is",
        the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is,
    ),
    (
        "a_text_is_escaped_as_the_reference_escapes_it_part_one",
        a_text_is_escaped_as_the_reference_escapes_it_part_one,
    ),
    (
        "a_text_is_escaped_as_the_reference_escapes_it_part_two",
        a_text_is_escaped_as_the_reference_escapes_it_part_two,
    ),
    (
        "an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity",
        an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity,
    ),
];

/// What the page says a text becomes: `<`, `>`, `&`, `"`, and `'` are
/// written as the entities HTML writes them as, and every other character as
/// it is.
fn reference_escape(text: &str) -> String {
    let mut written = String::new();
    for c in text.chars() {
        match c {
            '<' => written += "&lt;",
            '>' => written += "&gt;",
            '&' => written += "&amp;",
            '"' => written += "&quot;",
            '\'' => written += "&#39;",
            c => written.push(c),
        }
    }
    written
}

/// The text a text of the reference becomes when each entity is read as the
/// character it stands for, or none when a `&` begins no entity.
fn reference_unescape(written: &str) -> Option<String> {
    let mut text = String::new();
    let mut rest = written;
    while let Some(c) = rest.chars().next() {
        if c == '&' {
            let (entity, stands_for) = [
                ("&lt;", '<'),
                ("&gt;", '>'),
                ("&amp;", '&'),
                ("&quot;", '"'),
                ("&#39;", '\''),
            ]
            .into_iter()
            .find(|(entity, _)| rest.starts_with(entity))?;
            text.push(stands_for);
            rest = &rest[entity.len()..];
        } else {
            text.push(c);
            rest = &rest[c.len_utf8()..];
        }
    }
    Some(text)
}

/// The source of a Smith text that is `text`.
fn literal(text: &str) -> String {
    let mut written = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => written += "\\\"",
            '\\' => written += "\\\\",
            '{' => written += "\\{",
            '}' => written += "\\}",
            c => written.push(c),
        }
    }
    written + "\""
}

/// The lines a program that prints `escape` of each text prints.
fn escaped(texts: &[String]) -> Vec<String> {
    let mut body = String::new();
    for text in texts {
        body += &format!("  print(escape({}))\n", literal(text));
    }
    let source = format!(
        "use std.console (console, print)\nuse std.html (escape)\n\nfn main() ! console, alloc {{\n{body}}}\n"
    );
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&lowered);
    assert_eq!(outcome.status, 0, "for {source}");
    let printed = String::from_utf8(outcome.stdout).unwrap();
    let lines: Vec<String> = printed.lines().map(str::to_string).collect();
    assert_eq!(lines.len(), texts.len(), "for {source}");
    lines
}

/// A text of the five bytes that are escaped, of pieces that read as an
/// entity already, of characters of one to four bytes, and of plain ones.
fn any_text(g: &mut Gen) -> String {
    const PIECES: &[&str] = &[
        "<", ">", "&", "\"", "'", "&lt;", "&amp;", "&#39;", "a", "z", " ", "é", "日", "😀", "0",
        "/", "=",
    ];
    (0..g.below(12)).map(|_| *g.pick(PIECES)).collect()
}

fn the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is() {
    let texts: Vec<String> = ["<", ">", "&", "\"", "'", "", "plain", "a<b>c", "é<日>😀"]
        .map(String::from)
        .to_vec();
    assert_eq!(
        escaped(&texts),
        ["&lt;", "&gt;", "&amp;", "&quot;", "&#39;", "", "plain", "a&lt;b&gt;c", "é&lt;日&gt;😀"]
    );
}

fn a_text_is_escaped_as_the_reference_escapes_it(part: u64) {
    generate::check_part(
        8301,
        40,
        part,
        2,
        |g| (0..10).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            let expected: Vec<String> = texts.iter().map(|t| reference_escape(t)).collect();
            assert_eq!(escaped(texts), expected);
        },
    );
}

fn a_text_is_escaped_as_the_reference_escapes_it_part_one() {
    a_text_is_escaped_as_the_reference_escapes_it(0);
}

fn a_text_is_escaped_as_the_reference_escapes_it_part_two() {
    a_text_is_escaped_as_the_reference_escapes_it(1);
}

fn an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity() {
    generate::check(
        8302,
        8,
        |g| (0..10).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            for (text, written) in texts.iter().zip(escaped(texts)) {
                assert!(
                    !written.contains(['<', '>', '"', '\'']),
                    "{written:?} holds a byte of markup"
                );
                // Every `&` begins an entity, and reading them back gives the text.
                assert_eq!(reference_unescape(&written).as_deref(), Some(text.as_str()));
            }
        },
    );
}
