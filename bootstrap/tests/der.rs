//! `std.der` reads the tagged values of DER as pieces of the bytes it is given,
//! as `knowledge/library/der.md` says.
//!
//! The tests hold a model of DER, in `support/der_model.rs`, which shares nothing
//! with the library. A program is built from the bytes of each case, prints what
//! the library gives for each, and the test compares that with what the model
//! says.

use smith::{lower, scalar};

use crate::Test;
use crate::der_model::{Node, bytes, length, leaf, literal, model_line, model_read, node, run, tlv, values_program};
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_value_of_every_kind_is_read_with_its_parts_and_the_bytes_after_it",
        a_value_of_every_kind_is_read_with_its_parts_and_the_bytes_after_it,
    ),
    (
        "bytes_of_any_content_give_a_value_or_an_error_as_the_model_does",
        bytes_of_any_content_give_a_value_or_an_error_as_the_model_does,
    ),
    (
        "every_way_that_der_is_not_written_right_is_an_error_in_a_value_that_is_right_otherwise",
        every_way_that_der_is_not_written_right_is_an_error_in_a_value_that_is_right_otherwise,
    ),
    (
        "reading_the_parts_of_a_value_asks_for_memory_that_does_not_depend_on_their_bytes",
        reading_the_parts_of_a_value_asks_for_memory_that_does_not_depend_on_their_bytes,
    ),
];

// The tests of values.

fn a_value_of_every_kind_is_read_with_its_parts_and_the_bytes_after_it() {
    generate::check(
        1,
        6,
        |g| {
            // Several values, each followed by bytes that the value does not use.
            (0..8)
                .map(|_| {
                    let mut b = node(g, 2).encode();
                    b.extend(bytes(g, 6));
                    b
                })
                .collect::<Vec<_>>()
        },
        |inputs| {
            let lines = run(&values_program(inputs));
            let model: Vec<String> = inputs.iter().map(|b| model_line(b)).collect();
            assert_eq!(lines, model);
            assert!(model.iter().all(|l| l.starts_with("ok")), "{model:?}");
        },
    );
}

fn bytes_of_any_content_give_a_value_or_an_error_as_the_model_does() {
    generate::check(
        2,
        6,
        |g| {
            // Random bytes mostly begin with a tag, a length, and what they
            // measure, so that the model has some that it reads.
            (0..16)
                .map(|_| match g.below(3) {
                    0 => bytes(g, 12),
                    1 => {
                        let mut b = vec![*g.pick(&[0x30, 0x31, 0x02, 0x01, 0x04, 0xa0, 0x05, 0x06])];
                        b.extend(bytes(g, 12));
                        b
                    }
                    _ => {
                        let mut b = node(g, 2).encode();
                        let at = g.below(b.len());
                        b[at] = g.below(256) as u8;
                        b
                    }
                })
                .collect::<Vec<_>>()
        },
        |inputs| {
            let lines = run(&values_program(inputs));
            let model: Vec<String> = inputs.iter().map(|b| model_line(b)).collect();
            assert_eq!(lines, model);
        },
    );
}

/// What is put into the encoding of one part of a sequence so that DER does
/// not write it right.
fn fault(g: &mut Gen, part: &Node, kind: usize) -> Vec<u8> {
    let b = part.encode();
    let id = b[0];
    let contents = b[model_read(&b).unwrap().start..].to_vec();
    match (kind, part) {
        // A length with no end.
        (0, _) => {
            let mut out = vec![id, 0x80];
            out.extend(&contents);
            out.extend([0, 0]);
            out
        }
        // A length that could be shorter: in the long form where the short
        // one holds it, and with a byte of zero at the front.
        (1, _) if contents.len() < 128 => {
            let mut out = vec![id, 0x81, contents.len() as u8];
            out.extend(&contents);
            out
        }
        (2, _) => {
            let mut out = vec![id];
            if contents.len() >= 128 {
                let minimal = length(contents.len());
                out.extend([minimal[0] + 1, 0]);
                out.extend(&minimal[1..]);
            } else {
                out.extend([0x82, 0, contents.len() as u8]);
            }
            out.extend(&contents);
            out
        }
        // A length that is more than there are bytes.
        (3, _) => {
            let mut out = vec![id, 0x7f];
            out.extend(contents.iter().take(10));
            out
        }
        // An integer with a byte at the front it does not need, and a
        // boolean that is neither 0 nor 255, and a null with a byte in it.
        (4, Node::Leaf(2, c)) => {
            let mut padded = vec![if c[0] < 128 { 0 } else { 255 }];
            padded.extend(c);
            tlv(2, &padded)
        }
        (4, Node::Leaf(1, _)) => tlv(1, &[g.pick(&[1u8, 2, 7, 127, 128, 254]).to_owned()]),
        (4, Node::Leaf(5, _)) => tlv(5, &[0]),
        // A tag of more than one byte.
        _ => {
            let mut out = vec![id | 31, contents.len().min(127) as u8];
            out.extend(contents.iter().take(127));
            out
        }
    }
}

fn every_way_that_der_is_not_written_right_is_an_error_in_a_value_that_is_right_otherwise() {
    generate::check(
        3,
        4,
        |g| {
            // Each fault in turn, six of them, in three cases each: the sixth
            // is the tag of more than one byte, and the fifth is for an
            // integer, a boolean, or a null, with an integer of each sign.
            (0..18)
                .map(|i| {
                    let kind = i % 6;
                    let n = 1 + g.below(3);
                    let mut parts: Vec<Node> = (0..n).map(|_| leaf(g)).collect();
                    let at = g.below(parts.len());
                    if kind == 2 && i / 6 == 1 {
                        parts[at] = Node::Leaf(4, vec![9; 128 + g.below(100)]);
                    }
                    if kind == 4 {
                        parts[at] = match i / 6 {
                            0 => Node::Leaf(2, vec![g.below(128) as u8, g.below(256) as u8]),
                            1 => Node::Leaf(2, vec![128 + g.below(128) as u8, g.below(256) as u8]),
                            _ => if g.one_in(2) { Node::Leaf(1, vec![0]) } else { Node::Leaf(5, vec![]) },
                        };
                        if let Node::Leaf(2, c) = &mut parts[at] {
                            // One that DER writes right, as the others are.
                            while c.len() > 1 && ((c[0] == 0 && c[1] < 128) || (c[0] == 255 && c[1] >= 128)) {
                                c.remove(0);
                            }
                        }
                    }
                    let whole = Node::Parts(0x30, parts.clone()).encode();
                    let bad = fault(g, &parts[at], kind);
                    let mut c: Vec<u8> = Vec::new();
                    for (i, p) in parts.iter().enumerate() {
                        c.extend(if i == at { bad.clone() } else { p.encode() });
                    }
                    (whole, tlv(0x30, &c))
                })
                .collect::<Vec<_>>()
        },
        |pairs| {
            let right: Vec<Vec<u8>> = pairs.iter().map(|p| p.0.clone()).collect();
            let wrong: Vec<Vec<u8>> = pairs.iter().map(|p| p.1.clone()).collect();
            for line in run(&values_program(&right)) {
                assert!(line.starts_with("ok") && !line.contains("kids-err"), "{line}");
            }
            for (line, b) in run(&values_program(&wrong)).iter().zip(&wrong) {
                assert!(line == "err" || line.contains("kids-err"), "{line} for {b:?}");
            }
        },
    );
}

fn reading_the_parts_of_a_value_asks_for_memory_that_does_not_depend_on_their_bytes() {
    // A sequence of three byte strings, once with the bytes of each one long
    // and once with none: the parts are pieces of the bytes given, so what the
    // reading asks for is the list of the parts and nothing for their bytes.
    let sequence = |len: usize| Node::Parts(0x30, (0..3).map(|i| Node::Leaf(4, vec![i; len])).collect()).encode();
    let requests = |input: Vec<u8>| {
        let source = format!(
            "use std.console (console, print)\nuse std.der (children, value)\n\nfn input() -> Slice[Byte] {{ {} }}\n\nfn main() ! console, alloc {{\n  match value(input()) {{\n    .ok((v, rest)) {{\n      match children(v) {{\n        .ok(cs) {{\n          if cs.size > 0 {{ print(\"{{cs.size}} {{cs[0].bytes.size}}\") }}\n        }}\n        .err(e) {{ print(e) }}\n      }}\n    }}\n    .err(e) {{ print(e) }}\n  }}\n}}\n",
            literal(&input)
        );
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
        (String::from_utf8(outcome.stdout).unwrap(), requests)
    };
    let ((short_said, short), (long_said, long)) = (requests(sequence(0)), requests(sequence(3000)));
    assert_eq!(short_said, "3 0\n");
    assert_eq!(long_said, "3 3000\n");
    assert!(!short.is_empty());
    assert_eq!(short, long, "the memory asked for depends on how long the bytes are");
}
