//! `std.http1` is held to the text of RFC 9112, which is in
//! `reference/rfc/`: every sentence of sections 2 to 7, 9.3, and 11 that holds MUST,
//! MUST NOT, or SHALL is in a line of the ledger, `http1.ledger`, with the test
//! that holds it or the reason it binds nothing the module does; the helper that
//! reads the sentences and the ledger is tried on a ledger that has lost a line
//! and on one that names a test that is not there; and the bytes that the module
//! takes in a name, a value, a method, and a reason are the bytes the text's
//! grammar allows.

use smith::{lower, scalar};

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[
    (
        "every_must_of_rfc_9112_sections_2_to_7_9_3_and_11_is_in_the_ledger_of_http1",
        every_must_of_rfc_9112_sections_2_to_7_9_3_and_11_is_in_the_ledger_of_http1,
    ),
    (
        "a_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused",
        a_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused,
    ),
    (
        "the_bytes_http1_takes_in_a_name_a_value_a_method_and_a_reason_are_those_of_the_grammar",
        the_bytes_http1_takes_in_a_name_a_value_a_method_and_a_reason_are_those_of_the_grammar,
    ),
];

const SECTIONS: &[&str] = &["2", "3", "4", "5", "6", "7", "9.3", "11"];
const LEDGER: &str = include_str!("http1.ledger");

/// The names of every test that there is.
fn test_names() -> Vec<&'static str> {
    crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect()
}

fn every_must_of_rfc_9112_sections_2_to_7_9_3_and_11_is_in_the_ledger_of_http1() {
    let rfc = standard::read("rfc9112.txt");
    let tests = test_names();
    if let Err(errors) = standard::check_ledger(&rfc, SECTIONS, LEDGER, &tests) {
        panic!("{}", errors.join("\n"));
    }
}

fn a_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused() {
    let rfc = standard::read("rfc9112.txt");
    let tests = test_names();
    let sentences = standard::sentences(&rfc, SECTIONS);
    assert!(sentences.len() > 40, "the sections hold few sentences: {}", sentences.len());
    let lines: Vec<&str> = LEDGER.lines().filter(|l| !l.starts_with('#')).collect();
    // The ledger with each line away in turn: the sentence of that line is the one said to be in none.
    for lost in 0..lines.len() {
        let ledger: String = lines
            .iter()
            .enumerate()
            .filter(|(i, _)| *i != lost)
            .map(|(_, l)| format!("{l}\n"))
            .collect();
        let errors = standard::check_ledger(&rfc, SECTIONS, &ledger, &tests).unwrap_err();
        assert_eq!(errors.len(), 1, "{errors:?}");
        assert!(errors[0].starts_with("in no line of the ledger"), "{errors:?}");
    }
    let wrong = LEDGER.replacen("| test a_", "| test not_a_", 1);
    let errors = standard::check_ledger(&rfc, SECTIONS, &wrong, &tests).unwrap_err();
    assert!(errors.iter().any(|e| e.contains("does not exist")), "{errors:?}");
    let nameless = LEDGER.to_string() + "\n2.2 | No such sentence at all | reason none\n";
    let errors = standard::check_ledger(&rfc, SECTIONS, &nameless, &tests).unwrap_err();
    assert!(errors.iter().any(|e| e.contains("names no sentence")), "{errors:?}");
}

/// What a head with the byte `b` put in it, by `at`, reads as: `complete` or
/// `refused`.
fn kinds(heads: &[Vec<u8>], request: bool) -> Vec<String> {
    let show = if request { "request_head" } else { "response_head" };
    let calls: String = heads
        .iter()
        .map(|b| {
            let items: Vec<String> = b.iter().map(u8::to_string).collect();
            format!("  show([{}])\n", items.join(", "))
        })
        .collect();
    let source = format!(
        "use std.console (console, print)\nuse std.http1 ({show})\nuse std.list (freeze)\n\nfn show(b: take List[Byte]) ! console, alloc {{\n  match {show}(freeze(b)) {{\n    .complete((h, used)) {{ print(\"complete\") }}\n    .incomplete {{ print(\"incomplete\") }}\n    .refused(why) {{ print(\"refused\") }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n{calls}}}\n"
    );
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&program);
    String::from_utf8(outcome.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

fn the_bytes_http1_takes_in_a_name_a_value_a_method_and_a_reason_are_those_of_the_grammar() {
    // The rules are in the text of the two standards, and a rule that one of
    // them names is in the other.
    let text = format!("{}\n{}", standard::read("rfc9112.txt"), standard::read("rfc9110.txt"));
    let token = standard::abnf_bytes(&text, "tchar");
    let value = standard::abnf_bytes(&text, "field-vchar");
    let reason = standard::abnf_bytes(&text, "reason-phrase");
    assert_eq!(token.iter().filter(|b| **b).count(), 77, "the characters of a token");
    // The line breaks make lines, and a colon ends a name, so none is tried.
    let tried: Vec<u8> = (0..=255u8).filter(|b| ![10, 13, 58].contains(b)).collect();
    let with = |before: &[u8], b: u8, after: &[u8]| {
        let mut bytes = before.to_vec();
        bytes.push(b);
        bytes.extend(after);
        bytes
    };
    // A byte that is no whole character is no text, and a value is text, so
    // a byte from 128 on, which is a character of the grammar, is read as the
    // refusal of its place, as the page says.
    let text_of = |b: u8, set: &[bool; 256], blanks: bool| {
        let allowed = set[b as usize] || (blanks && (b == b' ' || b == b'\t'));
        if allowed && b < 128 { "complete" } else { "refused" }
    };
    let name = tried.iter().map(|b| with(b"GET / HTTP/1.1\r\nhost: h\r\nx", *b, b"y: v\r\n\r\n")).collect::<Vec<_>>();
    let got = kinds(&name, true);
    let want: Vec<&str> = tried.iter().map(|b| text_of(*b, &token, false)).collect();
    assert_eq!(got, want, "the bytes of the name of a header, which is a token");
    let method = tried.iter().map(|b| with(b"G", *b, b"T / HTTP/1.1\r\nhost: h\r\n\r\n")).collect::<Vec<_>>();
    let got = kinds(&method, true);
    assert_eq!(got, want, "the bytes of a method, which is a token");
    let values = tried.iter().map(|b| with(b"GET / HTTP/1.1\r\nhost: h\r\nx: a", *b, b"c\r\n\r\n")).collect::<Vec<_>>();
    let got = kinds(&values, true);
    let want: Vec<&str> = tried.iter().map(|b| text_of(*b, &value, true)).collect();
    assert_eq!(got, want, "the bytes of the value of a header");
    let reasons = tried.iter().map(|b| with(b"HTTP/1.1 200 a", *b, b"c\r\n\r\n")).collect::<Vec<_>>();
    let got = kinds(&reasons, false);
    let want: Vec<&str> = tried.iter().map(|b| text_of(*b, &reason, true)).collect();
    assert_eq!(got, want, "the bytes of a reason");
    // A digit of a status code (RFC 5234's `DIGIT`), and a digit of a chunk
    // size (`HEXDIG`), which the text of RFC 9112 uses without defining.
    let digits = tried.iter().map(|b| with(b"HTTP/1.1 2", *b, b"0 x\r\n\r\n")).collect::<Vec<_>>();
    let want: Vec<&str> = tried
        .iter()
        .map(|b| if standard::in_core(standard::DIGIT, *b) { "complete" } else { "refused" })
        .collect();
    assert_eq!(kinds(&digits, false), want, "the digits of a status code");
    let sizes = tried.iter().map(|b| with(b"", *b, b"\r\n")).collect::<Vec<_>>();
    let got = chunk_kinds(&sizes);
    // A size is read, and the bytes it asks for are not there: incomplete.
    let want: Vec<&str> = tried
        .iter()
        .map(|b| if standard::in_core(standard::HEXDIG, *b) { "incomplete" } else { "refused" })
        .collect();
    assert_eq!(got, want, "the digits of the size of a chunk");
}

/// What `chunk` says of each of the bytes.
fn chunk_kinds(chunks: &[Vec<u8>]) -> Vec<String> {
    let calls: String = chunks
        .iter()
        .map(|b| {
            let items: Vec<String> = b.iter().map(u8::to_string).collect();
            format!("  show([{}])\n", items.join(", "))
        })
        .collect();
    let source = format!(
        "use std.console (console, print)\nuse std.http1 (chunk)\nuse std.list (freeze)\n\nfn show(b: take List[Byte]) ! console, alloc {{\n  match chunk(freeze(b)) {{\n    .complete((h, used)) {{ print(\"complete\") }}\n    .incomplete {{ print(\"incomplete\") }}\n    .refused(why) {{ print(\"refused\") }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n{calls}}}\n"
    );
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    String::from_utf8(scalar::run(&program).stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}
