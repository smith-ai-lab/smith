//! The lexer written in Smith, in `compiler/lexer.smith`, gives the tokens and
//! the errors that the Rust lexer gives, each with its line and column. Both
//! are printed in one plain form, which `support/lexer_form.rs` says, and
//! compared. Many texts go in one program, as compiling a program for each
//! would spend the limit of a test.

use smith::target::Target;

use crate::Test;
use crate::choices;
use crate::enums;
use crate::generate::{self, Gen};
use crate::lambdas;
use crate::lexer_form;
use crate::numbers;
use crate::programs;
use crate::reprs;
use crate::targets;
use crate::text;
use crate::traits;
use crate::tries;
use crate::tuples;

pub const MACHINE: &[Test] = &[
    (
        "the_lexer_written_in_smith_lexes_every_generated_program_as_the_rust_lexer_does",
        the_lexer_written_in_smith_lexes_every_generated_program_as_the_rust_lexer_does,
    ),
    (
        "the_lexer_written_in_smith_lexes_every_generated_program_with_a_lexical_mistake_as_the_rust_lexer_does",
        the_lexer_written_in_smith_lexes_every_generated_program_with_a_lexical_mistake_as_the_rust_lexer_does,
    ),
    (
        "the_lexer_written_in_smith_lexes_every_generated_program_with_pipes_as_the_rust_lexer_does",
        the_lexer_written_in_smith_lexes_every_generated_program_with_pipes_as_the_rust_lexer_does,
    ),
    (
        "the_lexer_written_in_smith_lexes_every_case_file_as_the_rust_lexer_does",
        the_lexer_written_in_smith_lexes_every_case_file_as_the_rust_lexer_does,
    ),
    (
        "strings_nested_four_and_five_deep_are_lexed_differently_until_recursion_is_built",
        strings_nested_four_and_five_deep_are_lexed_differently_until_recursion_is_built,
    ),
];

/// How many programs of each generator a test lexes, and from how many of
/// them it takes the shortest. The lexer written in Smith copies a list at
/// every push, so what it spends grows with the square of the length of a
/// text, and a slow kind of machine must run the program well within the time
/// a program is given.
const PER_GENERATOR: usize = 2;
const FROM: u64 = 24;

/// How many texts with a mistake each program makes.
const MISTAKES: u64 = 4;

/// The shortest programs of each generator that builds one, as their text.
fn generated_programs() -> Vec<Vec<u8>> {
    let generators: [fn(&mut Gen) -> Vec<u8>; 10] = [
        |g| programs::program(g).source,
        |g| enums::program(g).source.into_bytes(),
        |g| traits::program(g).source.into_bytes(),
        |g| tuples::program(g).source.into_bytes(),
        |g| tries::program(g).source.into_bytes(),
        |g| lambdas::program(g).source.into_bytes(),
        |g| reprs::program(g).source.into_bytes(),
        |g| choices::program(g).source.into_bytes(),
        |g| numbers::program(g).source.into_bytes(),
        |g| text::program(g).source.into_bytes(),
    ];
    let mut texts = Vec::new();
    for make in generators {
        let mut made: Vec<Vec<u8>> = (0..FROM).map(|case| generate::sample(31, case, make)).collect();
        made.sort_by_key(|text| text.len());
        texts.extend(made.into_iter().take(PER_GENERATOR));
    }
    texts
}

/// What a mistake of the lexer's kind is placed as: an unclosed string, a bad
/// escape, a bad number, a carriage return, a byte that is not UTF-8.
fn place_a_mistake(g: &mut Gen, mut text: Vec<u8>) -> Vec<u8> {
    let kind = g.below(5);
    let starts: Vec<usize> = std::iter::once(0)
        .chain(
            text.iter()
                .enumerate()
                .filter(|(_, b)| **b == b'\n')
                .map(|(i, _)| i + 1),
        )
        .collect();
    let line = *g.pick(&starts);
    let anywhere = g.below(text.len() + 1);
    let (at, put): (usize, Vec<u8>) = match kind {
        0 => (line, b"\"never closed\n".to_vec()),
        1 => (line, b"\"a \\q escape\"\n".to_vec()),
        2 => (line, format!("{}\n", g.pick(&["0x", "0b2", "1__0", "1_", "0xg", "1e+"])).into_bytes()),
        3 => (anywhere, b"\r".to_vec()),
        _ => {
            // A byte that starts a character and ends too soon, a surrogate, an
            // overlong form, and a value past the last character.
            let bad: &[&[u8]] = &[
                &[0xFF],
                &[0xC3],
                &[0xE2, 0x82],
                &[0xED, 0xA0, 0x80],
                &[0xE0, 0x80, 0x80],
                &[0xC0, 0x80],
                &[0xF4, 0x90, 0x80, 0x80],
            ];
            (anywhere, g.pick(bad).to_vec())
        }
    };
    text.splice(at..at, put);
    text
}

/// How many texts with pipes each program makes.
const PIPED: u64 = 2;

/// Where `|>` is placed in a text, at the start of a line or in the middle of
/// one, with the line breaks, empty lines, comments and indentation that may
/// come before it, and what looks like it and is not it: `|` and `>` apart,
/// beside other punctuation, and with no `>` at all.
fn place_pipes(g: &mut Gen, mut text: Vec<u8>) -> Vec<u8> {
    const PIECES: &[&str] = &[
        "|> f()\n",
        "  |> f()\n",
        "\n|> f()\n",
        "// a comment\n|> f()\n",
        "\n\n  // a comment\n\n    |> f()\n",
        "\t|> f() |> g(_)\n",
        "x |> f()\n",
        "x|>f()\n",
        "x | > f()\n",
        "| >\n",
        "|\n",
        ">|\n",
        "||>\n",
        "|>>\n",
        "|>=\n",
        "|>|>\n",
        "|>",
    ];
    for _ in 0..1 + g.below(3) {
        let starts: Vec<usize> = std::iter::once(0)
            .chain(text.iter().enumerate().filter(|(_, b)| **b == b'\n').map(|(i, _)| i + 1))
            .collect();
        let at = if g.below(2) == 0 { *g.pick(&starts) } else { g.below(text.len() + 1) };
        let piece = g.pick(PIECES).as_bytes().to_vec();
        text.splice(at..at, piece);
    }
    text
}

/// Builds the program that lexes `texts` for each target this machine runs, and
/// checks that it prints what the Rust lexer says. For a fingerprint, notes
/// what would be run.
fn lexes_as_the_rust_lexer_does(texts: &[Vec<u8>]) {
    let texts: Vec<&[u8]> = texts.iter().map(|t| t.as_slice()).collect();
    let source = lexer_form::program(&texts);
    let expected = lexer_form::expected(&texts);
    for target in targets::runnable() {
        let bytes = targets::build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        if targets::fingerprinting() {
            targets::record(target, &[&bytes, expected.as_bytes()]);
            continue;
        }
        let said = targets::run(&bytes);
        assert_eq!(said.status, 0, "{}", target.name());
        let said = String::from_utf8(said.stdout).unwrap();
        if said != expected {
            panic!("{}", disagreement(&texts, &said, &expected, target));
        }
    }
}

/// Which text the two lexers read differently, and where.
fn disagreement(texts: &[&[u8]], said: &str, expected: &str, target: Target) -> String {
    let each = |all: &'_ str| -> Vec<Vec<String>> {
        let mut each: Vec<Vec<String>> = Vec::new();
        for line in all.lines() {
            if line == "input" {
                each.push(Vec::new());
            } else {
                each.last_mut().unwrap().push(line.to_string());
            }
        }
        each
    };
    let (said, expected) = (each(said), each(expected));
    for ((text, said), expected) in texts.iter().zip(&said).zip(&expected) {
        if said != expected {
            let at = said.iter().zip(expected).position(|(a, b)| a != b);
            let at = at.unwrap_or(said.len().min(expected.len()));
            return format!(
                "on {:?}, for {}\nthe lexer written in Smith says {:?}\nthe Rust lexer says {:?}",
                String::from_utf8_lossy(text),
                target.name(),
                said.get(at),
                expected.get(at)
            );
        }
    }
    format!(
        "the lexer written in Smith lexed {} texts and the Rust lexer {}",
        said.len(),
        expected.len()
    )
}

fn the_lexer_written_in_smith_lexes_every_generated_program_as_the_rust_lexer_does() {
    lexes_as_the_rust_lexer_does(&generated_programs());
}

fn the_lexer_written_in_smith_lexes_every_generated_program_with_a_lexical_mistake_as_the_rust_lexer_does()
 {
    // Each program, with a mistake of each of several kinds, as a seed picks.
    let texts: Vec<Vec<u8>> = generated_programs()
        .into_iter()
        .enumerate()
        .flat_map(|(n, text)| (0..MISTAKES).map(move |k| (n as u64 * MISTAKES + k, text.clone())))
        .map(|(case, text)| generate::sample(32, case, |g: &mut Gen| place_a_mistake(g, text.clone())))
        .collect();
    lexes_as_the_rust_lexer_does(&texts);
}

fn the_lexer_written_in_smith_lexes_every_generated_program_with_pipes_as_the_rust_lexer_does() {
    let texts: Vec<Vec<u8>> = generated_programs()
        .into_iter()
        .enumerate()
        .flat_map(|(n, text)| (0..PIPED).map(move |k| (n as u64 * PIPED + k, text.clone())))
        .map(|(case, text)| generate::sample(33, case, |g: &mut Gen| place_pipes(g, text.clone())))
        .collect();
    lexes_as_the_rust_lexer_does(&texts);
}

fn the_lexer_written_in_smith_lexes_every_case_file_as_the_rust_lexer_does() {
    let texts: Vec<Vec<u8>> = targets::case_files()
        .iter()
        .map(|path| std::fs::read(path).unwrap())
        .collect();
    lexes_as_the_rust_lexer_does(&texts);
}

/// A string nested this many deep, each inside an expression of the one
/// outside it.
fn nested(depth: usize) -> Vec<u8> {
    let mut text = String::from("x");
    for _ in 0..depth {
        text = format!("\"{{{text}}}\"");
    }
    text.into_bytes()
}

/// The lexer written in Smith reads strings nested three deep, as recursion is
/// not built, and misreads four and five deep, which the Rust lexer reads. The
/// work-round is marked `GAP 2` in `compiler/lexer.smith`. This test expects the
/// difference, and fails when it is gone, so that the work-round is taken out
/// then.
fn strings_nested_four_and_five_deep_are_lexed_differently_until_recursion_is_built() {
    let texts = [nested(3), nested(4), nested(5)];
    let refs: Vec<&[u8]> = texts.iter().map(|t| t.as_slice()).collect();
    let source = lexer_form::program(&refs);
    let expected = lexer_form::expected(&refs);
    for target in targets::runnable() {
        let bytes = targets::build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        if targets::fingerprinting() {
            targets::record(target, &[&bytes, expected.as_bytes()]);
            continue;
        }
        let said = String::from_utf8(targets::run(&bytes).stdout).unwrap();
        let (said, expected) = (lines_of_each(&said), lines_of_each(&expected));
        assert_eq!(said[0], expected[0], "three deep, {}", target.name());
        for deep in [1, 2] {
            assert_ne!(
                said[deep], expected[deep],
                "{} deep is now read as the Rust lexer reads it: take out `GAP 2` of \
                 compiler/lexer.smith, and this test, for {}",
                deep + 3,
                target.name()
            );
        }
    }
}

fn lines_of_each(all: &str) -> Vec<Vec<String>> {
    let mut each: Vec<Vec<String>> = Vec::new();
    for line in all.lines() {
        if line == "input" {
            each.push(Vec::new());
        } else {
            each.last_mut().unwrap().push(line.to_string());
        }
    }
    each
}
