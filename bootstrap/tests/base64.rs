//! `std.base64` writes bytes as text in two alphabets and reads them back.
//!
//! The generator builds the bytes, keeps them, and writes the text they must
//! become with a model of the alphabet written here, so it shares nothing with
//! the library. A text is faulted in the ways the page lists, and the test
//! says what the reader must say of it from the fault the generator made.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_examples_of_the_page_and_the_vectors_of_rfc_4648_give_what_they_say",
        the_examples_of_the_page_and_the_vectors_of_rfc_4648_give_what_they_say,
    ),
    (
        "bytes_are_written_as_the_model_writes_them_and_read_back_in_both_forms",
        bytes_are_written_as_the_model_writes_them_and_read_back_in_both_forms,
    ),
    (
        "a_text_is_read_only_when_the_bytes_it_gives_are_written_as_that_text",
        a_text_is_read_only_when_the_bytes_it_gives_are_written_as_that_text,
    ),
    (
        "a_text_with_one_of_the_faults_of_the_page_put_in_is_never_read",
        a_text_with_one_of_the_faults_of_the_page_put_in_is_never_read,
    ),
];

const STANDARD: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const URL: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

fn alphabet(url: bool) -> &'static [u8; 64] {
    if url { URL } else { STANDARD }
}

/// The text of `bytes`, written from the alphabet three bytes at a time.
fn model_encode(bytes: &[u8], url: bool) -> String {
    let table = alphabet(url);
    let mut text = String::new();
    for group in bytes.chunks(3) {
        let n = group.iter().fold(0u32, |n, b| n << 8 | *b as u32) << (8 * (3 - group.len()));
        for k in 0..group.len() + 1 {
            text.push(table[(n >> (18 - 6 * k) & 63) as usize] as char);
        }
        if !url {
            text.extend(std::iter::repeat_n('=', 3 - group.len()));
        }
    }
    text
}

/// What a reader must give: the one list of bytes that is written as `text`,
/// if there is one. Any padding is taken off, the characters are read, and
/// what they give is written again and has to be `text`.
fn model_decode(text: &str, url: bool) -> Option<Vec<u8>> {
    let data = text.trim_end_matches('=').as_bytes();
    let mut bits = 0u32;
    let mut held = 0;
    let mut bytes = Vec::new();
    for c in data {
        let v = alphabet(url).iter().position(|a| a == c)? as u32;
        bits = bits << 6 | v;
        held += 6;
        if held >= 8 {
            held -= 8;
            bytes.push((bits >> held) as u8);
            bits &= (1 << held) - 1;
        }
    }
    (model_encode(&bytes, url) == text).then_some(bytes)
}

/// The source of a program that runs each call in `calls` and prints what it
/// gives, as `show` says.
fn program(calls: &[String]) -> String {
    let mut body = String::new();
    for call in calls {
        body += &format!("  {call}\n");
    }
    let used = |name: &str| calls.iter().any(|c| c.starts_with(name));
    let mut imports = vec![];
    let mut helpers = String::new();
    if used("enc(") {
        imports.push("encode");
        helpers += "fn enc(b: take List[Byte]) ! console, alloc {\n  print(encode(freeze(b)))\n}\n\n";
    }
    if used("enc_url(") {
        imports.push("encode_url");
        helpers += "fn enc_url(b: take List[Byte]) ! console, alloc {\n  print(encode_url(freeze(b)))\n}\n\n";
    }
    if used("dec(") {
        imports.push("decode");
        helpers += "fn dec(s: str) ! console, alloc {\n  show(decode(s))\n}\n\n";
    }
    if used("dec_url(") {
        imports.push("decode_url");
        helpers += "fn dec_url(s: str) ! console, alloc {\n  show(decode_url(s))\n}\n\n";
    }
    if used("dec") {
        helpers = format!(
            "fn show(o: Option[List[Byte]]) ! console, alloc {{\n  match o {{\n    .none {{ print(\"none\") }}\n    .some(l) {{\n      print(\"some {{l.size}}\")\n      loop v in l {{ print(\"{{v}}\") }}\n    }}\n  }}\n}}\n\n{helpers}"
        );
    }
    imports.sort();
    format!(
        "use std.base64 ({})\nuse std.console (console, print)\nuse std.list (freeze)\n\n{helpers}fn main() ! console, alloc {{\n{body}}}\n",
        imports.join(", ")
    )
}

/// The lines a program prints.
fn printed(source: &str) -> Vec<String> {
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&lowered);
    assert_eq!(outcome.status, 0, "for {source}");
    String::from_utf8(outcome.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

fn list(bytes: &[u8]) -> String {
    let items: Vec<String> = bytes.iter().map(u8::to_string).collect();
    format!("[{}]", items.join(", "))
}

/// What `show` prints of a reading.
fn shown(read: Option<Vec<u8>>) -> Vec<String> {
    match read {
        None => vec!["none".to_string()],
        Some(bytes) => std::iter::once(format!("some {}", bytes.len()))
            .chain(bytes.iter().map(u8::to_string))
            .collect(),
    }
}

fn literal(text: &str) -> String {
    format!("\"{text}\"")
}

fn bytes_of(g: &mut Gen, most: usize) -> Vec<u8> {
    (0..g.below(most + 1)).map(|_| g.below(256) as u8).collect()
}

fn the_examples_of_the_page_and_the_vectors_of_rfc_4648_give_what_they_say() {
    let source = "use std.base64 (decode, decode_url, encode, encode_url)\nuse std.console (console, print)\n\nfn show(o: Option[List[Byte]]) ! console, alloc {\n  match o {\n    .none { print(\"none\") }\n    .some(l) {\n      print(\"some {l.size}\")\n      loop v in l { print(\"{v}\") }\n    }\n  }\n}\n\nfn main() ! console, alloc {\n  print(encode(\"hi?\".bytes))\n  print(encode_url(\"hi?\".bytes))\n  print(encode(\"hi\".bytes))\n  print(encode_url(\"hi\".bytes))\n  show(decode(\"aGk/\"))\n}\n";
    let mut expected: Vec<String> = ["aGk/", "aGk_", "aGk=", "aGk"].map(String::from).to_vec();
    expected.extend(shown(Some(b"hi?".to_vec())));
    assert_eq!(printed(source), expected);

    // RFC 4648 section 10.
    let vectors = [
        ("", ""),
        ("f", "Zg=="),
        ("fo", "Zm8="),
        ("foo", "Zm9v"),
        ("foob", "Zm9vYg=="),
        ("fooba", "Zm9vYmE="),
        ("foobar", "Zm9vYmFy"),
    ];
    let mut calls = Vec::new();
    let mut expected = Vec::new();
    for (plain, written) in vectors {
        calls.push(format!("print(encode({}.bytes))", literal(plain)));
        expected.push(written.to_string());
        calls.push(format!("show(decode({}))", literal(written)));
        expected.extend(shown(Some(plain.as_bytes().to_vec())));
    }
    let body: String = calls.iter().map(|c| format!("  {c}\n")).collect();
    let source = format!("use std.base64 (decode, encode)\nuse std.console (console, print)\n\nfn show(o: Option[List[Byte]]) ! console, alloc {{\n  match o {{\n    .none {{ print(\"none\") }}\n    .some(l) {{\n      print(\"some {{l.size}}\")\n      loop v in l {{ print(\"{{v}}\") }}\n    }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n{body}}}\n");
    assert_eq!(printed(&source), expected);
}

fn bytes_are_written_as_the_model_writes_them_and_read_back_in_both_forms() {
    generate::check(
        5781,
        3,
        |g| (0..16).map(|_| bytes_of(g, 24)).collect::<Vec<_>>(),
        |batch| {
            let mut calls = Vec::new();
            let mut expected = Vec::new();
            for bytes in batch {
                calls.push(format!("enc({})", list(bytes)));
                expected.push(model_encode(bytes, false));
                calls.push(format!("enc_url({})", list(bytes)));
                expected.push(model_encode(bytes, true));
                for url in [false, true] {
                    let text = model_encode(bytes, url);
                    calls.push(format!("{}({})", if url { "dec_url" } else { "dec" }, literal(&text)));
                    expected.extend(shown(Some(bytes.clone())));
                }
            }
            assert_eq!(printed(&program(&calls)), expected);
        },
    );
}

/// A text of the characters of both alphabets, padding, and a few that are in
/// neither, or an encoding of bytes with a character of that kind put in.
fn any_text(g: &mut Gen) -> String {
    const ODD: &[char] = &['=', '-', '_', '+', '/', ' ', '*', 'é', 'A', 'Q', 'g', 'w'];
    let url = g.one_in(2);
    if g.one_in(3) {
        return (0..g.below(9)).map(|_| *g.pick(ODD)).collect();
    }
    let mut text: Vec<char> = model_encode(&bytes_of(g, 7), url).chars().collect();
    for _ in 0..g.below(3) {
        let at = g.below(text.len() + 1);
        match g.below(3) {
            0 => text.insert(at, *g.pick(ODD)),
            1 if at < text.len() => text[at] = *g.pick(ODD),
            _ if at < text.len() => {
                text.remove(at);
            }
            _ => {}
        }
    }
    text.into_iter().collect()
}

fn a_text_is_read_only_when_the_bytes_it_gives_are_written_as_that_text() {
    generate::check(
        5782,
        3,
        |g| (0..40).map(|_| any_text(g)).collect::<Vec<_>>(),
        |batch| {
            let mut calls = Vec::new();
            let mut expected = Vec::new();
            for text in batch {
                for url in [false, true] {
                    calls.push(format!("{}({})", if url { "dec_url" } else { "dec" }, literal(text)));
                    expected.extend(shown(model_decode(text, url)));
                }
            }
            let lines = printed(&program(&calls));
            assert_eq!(lines, expected);
            // The batch is not made only of texts that are refused.
            assert!(expected.iter().any(|l| l.starts_with("some")) || batch.len() < 8, "{batch:?}");
        },
    );
}

/// A fault the page lists, put into the text of bytes by the generator.
fn faulted(g: &mut Gen) -> (String, bool) {
    let url = g.one_in(2);
    let bytes = bytes_of(g, 8);
    let mut text: Vec<char> = model_encode(&bytes, url).chars().collect();
    let alphabet: Vec<char> = alphabet(url).iter().map(|b| *b as char).collect();
    let foreign: Vec<char> = if url {
        vec![' ', '*', 'é', '+', '/']
    } else {
        vec![' ', '*', 'é', '-', '_']
    };
    match g.below(4) {
        // A character outside the alphabet in place of one.
        0 => {
            let bytes = if bytes.is_empty() { vec![1] } else { bytes };
            text = model_encode(&bytes, url).chars().collect();
            let at = g.below(text.len());
            text[at] = *g.pick(&foreign);
        }
        // A size that no encoding has.
        1 => {
            let bytes: Vec<u8> = bytes.iter().copied().take(bytes.len() / 3 * 3).collect();
            text = model_encode(&bytes, url).chars().collect();
            if !url && !text.is_empty() && g.one_in(2) {
                text.pop();
            } else {
                text.push(*g.pick(&alphabet));
            }
        }
        // Padding where it cannot be: a pad in the standard form anywhere but
        // the last two places of a quartet that ends the text, and any pad in
        // the form for an address.
        2 => {
            if url {
                let at = g.below(text.len() + 1);
                text.insert(at, '=');
            } else {
                let bytes = if bytes.is_empty() { vec![1] } else { bytes };
                text = model_encode(&bytes, url).chars().collect();
                // The third place of the last quartet is left out: a pad there
                // can be the end of an encoding of fewer bytes.
                let last = text.len().div_ceil(4);
                let places: Vec<usize> = (0..text.len())
                    .filter(|i| i % 4 != 3 && text[*i] != '=' && (i % 4 < 2 || i / 4 + 1 < last))
                    .collect();
                let at = *g.pick(&places);
                text[at] = '=';
            }
        }
        // Bits left over that are not zero.
        _ => {
            let mut bytes = bytes;
            if bytes.len().is_multiple_of(3) {
                bytes.push(g.below(256) as u8);
            }
            text = model_encode(&bytes, url).chars().collect();
            let last = text.iter().rposition(|c| *c != '=').unwrap();
            let value = alphabet.iter().position(|c| *c == text[last]).unwrap();
            text[last] = alphabet[value | 1];
        }
    }
    (text.into_iter().collect(), url)
}

fn a_text_with_one_of_the_faults_of_the_page_put_in_is_never_read() {
    generate::check(
        5783,
        3,
        |g| (0..40).map(|_| faulted(g)).collect::<Vec<_>>(),
        |batch| {
            let calls: Vec<String> = batch
                .iter()
                .map(|(text, url)| format!("{}({})", if *url { "dec_url" } else { "dec" }, literal(text)))
                .collect();
            let lines = printed(&program(&calls));
            assert_eq!(lines, vec!["none"; batch.len()], "{batch:?}");
        },
    );
}
