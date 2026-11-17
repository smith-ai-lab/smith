//! Properties of a character written as a whole number, and of `match` on
//! whole numbers and on text by literals.
//!
//! Every generator keeps a model of what it writes, in plain Rust, and the
//! test compares what the compiler's program prints with it, and with the
//! same choice written as `if` and `==`.

use smith::scalar::{self, End};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};

pub const ONCE: &[Test] = &[
    (
        "a_match_on_a_character_names_a_digit_an_open_and_the_rest",
        a_match_on_a_character_names_a_digit_an_open_and_the_rest,
    ),
    (
        "a_character_literal_is_its_code_in_every_type_it_fits",
        a_character_literal_is_its_code_in_every_type_it_fits,
    ),
    (
        "the_escapes_of_a_character_are_39_92_10_and_9",
        the_escapes_of_a_character_are_39_92_10_and_9,
    ),
    (
        "a_character_outside_ascii_is_refused_as_a_u8_and_is_its_code_point_as_a_wider_type",
        a_character_outside_ascii_is_refused_as_a_u8_and_is_its_code_point_as_a_wider_type,
    ),
    (
        "a_match_on_a_whole_number_takes_the_block_of_the_literal_that_equals_it",
        a_match_on_a_whole_number_takes_the_block_of_the_literal_that_equals_it,
    ),
    (
        "a_match_on_text_takes_the_block_of_the_literal_that_equals_it",
        a_match_on_text_takes_the_block_of_the_literal_that_equals_it,
    ),
    (
        "a_literal_in_a_case_and_in_a_tuple_is_matched_by_the_first_pattern_that_matches",
        a_literal_in_a_case_and_in_a_tuple_is_matched_by_the_first_pattern_that_matches,
    ),
    (
        "a_match_on_whole_numbers_or_text_must_end_with_a_bare_name_and_repeat_nothing",
        a_match_on_whole_numbers_or_text_must_end_with_a_bare_name_and_repeat_nothing,
    ),
    (
        "a_match_on_whole_numbers_is_one_switch_however_many_literals_it_has",
        a_match_on_whole_numbers_is_one_switch_however_many_literals_it_has,
    ),
];

/// What the program prints, one line to a `print`.
fn prints(source: &str) -> Vec<String> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let out = scalar::run(&program);
    assert_eq!(out.status, 0, "for {source}");
    String::from_utf8(out.stdout)
        .expect("text")
        .lines()
        .map(str::to_string)
        .collect()
}

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

fn a_match_on_a_character_names_a_digit_an_open_and_the_rest() {
    let source = "fn kind(b: u8) -> str {
  match b {
    '(' { \"open\" }
    ')' { \"close\" }
    other {
      if other >= '0' and other <= '9' { \"digit\" } else { \"other\" }
    }
  }
}

fn main() ! console {
  print(kind('7'))
  print(kind('('))
  print(kind(' '))
}
";
    assert_eq!(prints(source), ["digit", "open", "other"]);
}

/// How a character is written, and its code, for every ASCII character.
fn ascii(g: &mut Gen) -> (String, i128) {
    let code = 32 + g.below(95) as u8;
    let written = match code {
        b'\'' => "\\'".to_string(),
        b'\\' => "\\\\".to_string(),
        c => (c as char).to_string(),
    };
    (format!("'{written}'"), code as i128)
}

fn a_character_literal_is_its_code_in_every_type_it_fits() {
    generate::check(
        11,
        150,
        |g| (ascii(g), *g.pick(INTS)),
        |((written, code), ty): &((String, i128), Int)| {
            let source = format!(
                "fn main() ! console, alloc {{\n  x: {} = {written}\n  print(\"{{x}}\")\n}}\n",
                ty.name
            );
            assert_eq!(prints(&source), [code.to_string()], "for {source}");
        },
    );
}

fn the_escapes_of_a_character_are_39_92_10_and_9() {
    for (written, code) in [("'\\''", 39), ("'\\\\'", 92), ("'\\n'", 10), ("'\\t'", 9)] {
        for ty in INTS {
            let source = format!(
                "fn main() ! console, alloc {{\n  x: {} = {written}\n  print(\"{{x}}\")\n}}\n",
                ty.name
            );
            assert_eq!(prints(&source), [code.to_string()], "for {source}");
        }
    }
}

fn a_character_outside_ascii_is_refused_as_a_u8_and_is_its_code_point_as_a_wider_type() {
    generate::check(
        12,
        60,
        |g| {
            let c = *g.pick(&['é', 'ÿ', 'ß', '€', '世', '😀', '\u{80}']);
            (c, *g.pick(INTS))
        },
        |(c, ty): &(char, Int)| {
            let source = format!(
                "fn main() ! console, alloc {{\n  x: {} = '{c}'\n  print(\"{{x}}\")\n}}\n",
                ty.name
            );
            let code = *c as i128;
            if ty.bits > 8 && code <= ty.max() {
                assert_eq!(prints(&source), [code.to_string()], "for {source}");
            } else {
                assert!(!refused(&source).is_empty(), "for {source}");
            }
        },
    );
}

/// How a whole number pattern is written: in decimal, in hexadecimal when it
/// is not negative, or as a character when it is a printable ASCII one.
fn spelling(g: &mut Gen, n: i128) -> String {
    let character = (32..127).contains(&n) && n != 39 && n != 92;
    match g.below(3) {
        1 if n >= 0 => format!("{n:#x}"),
        2 if character => format!("'{}'", n as u8 as char),
        _ => n.to_string(),
    }
}

/// A `match` on a whole number: the type, the literals with what each
/// gives, in order, what the bare name at the end gives, and the values to
/// match.
#[derive(Debug)]
struct Numbers {
    ty: Int,
    arms: Vec<(i128, i64)>,
    rest: i64,
    /// Where the bare name stands, among the arms, if it is not last.
    subjects: Vec<i128>,
    spelled: Vec<String>,
}

fn numbers(g: &mut Gen) -> Numbers {
    let ty = *g.pick(INTS);
    let mut arms: Vec<(i128, i64)> = Vec::new();
    for _ in 0..1 + g.below(6) {
        let n = if g.one_in(3) {
            32 + g.below(95) as i128
        } else {
            value(g, ty)
        };
        let n = match n {
            39 | 92 => 40,
            n => n,
        };
        if !arms.iter().any(|(m, _)| *m == n) {
            arms.push((n, g.below(100) as i64));
        }
    }
    let spelled = arms.iter().map(|(n, _)| spelling(g, *n)).collect();
    let mut subjects: Vec<i128> = arms.iter().map(|(n, _)| *n).collect();
    for _ in 0..3 {
        subjects.push(value(g, ty));
    }
    Numbers {
        ty,
        arms,
        rest: 100 + g.below(100) as i64,
        subjects,
        spelled,
    }
}

fn a_match_on_a_whole_number_takes_the_block_of_the_literal_that_equals_it() {
    generate::check(13, 120, numbers, |n: &Numbers| {
        let t = n.ty.name;
        let mut by_match = format!("fn f(b: {t}) -> i64 {{\n  match b {{\n");
        let mut by_if = format!("fn f(b: {t}) -> i64 {{\n  ");
        for ((arm, given), spelled) in n.arms.iter().zip(&n.spelled) {
            by_match.push_str(&format!("    {spelled} {{ {given} }}\n"));
            by_if.push_str(&format!("if b == {arm} {{ {given} }} else "));
        }
        by_match.push_str(&format!("    other {{ {} }}\n  }}\n}}\n", n.rest));
        by_if.push_str(&format!("{{ {} }}\n}}\n", n.rest));
        let mut main = String::from("\nfn main() ! console, alloc {\n");
        for (i, s) in n.subjects.iter().enumerate() {
            main.push_str(&format!("  x{i}: {t} = {s}\n  print(\"{{f(x{i})}}\")\n"));
        }
        main.push_str("}\n");
        let expected: Vec<String> = (n.subjects.iter())
            .map(|s| {
                let at = n.arms.iter().find(|(m, _)| m == s);
                at.map_or(n.rest, |(_, given)| *given).to_string()
            })
            .collect();
        assert_eq!(prints(&format!("{by_match}{main}")), expected, "for {n:?}");
        assert_eq!(prints(&format!("{by_if}{main}")), expected, "for {n:?}");
    });
}

/// Texts that are prefixes of one another and empty among them, which a
/// comparison that stops early would mix up.
const WORDS: &[&str] = &["", "a", "ab", "abc", "b", "ba", "é", "a b", "aé"];

fn a_match_on_text_takes_the_block_of_the_literal_that_equals_it() {
    generate::check(
        14,
        120,
        |g| {
            let mut arms: Vec<(&str, i64)> = Vec::new();
            for _ in 0..1 + g.below(5) {
                let w = *g.pick(WORDS);
                if !arms.iter().any(|(m, _)| *m == w) {
                    arms.push((w, g.below(100) as i64));
                }
            }
            (arms, WORDS.to_vec())
        },
        |(arms, subjects): &(Vec<(&str, i64)>, Vec<&str>)| {
            let mut source = String::from("fn f(s: str) -> i64 {\n  match s {\n");
            for (w, given) in arms {
                source.push_str(&format!("    \"{w}\" {{ {given} }}\n"));
            }
            source.push_str("    other { 100 }\n  }\n}\n\nfn main() ! console, alloc {\n");
            for s in subjects {
                source.push_str(&format!("  print(\"{{f(\"{s}\")}}\")\n"));
            }
            source.push_str("}\n");
            let expected: Vec<String> = (subjects.iter())
                .map(|s| {
                    let at = arms.iter().find(|(m, _)| m == s);
                    at.map_or(100, |(_, given)| *given).to_string()
                })
                .collect();
            assert_eq!(prints(&source), expected, "for {source}");
        },
    );
}

/// The values a pattern is tried on: the ones the literals name, and one
/// that none of them does.
fn domain() -> std::ops::Range<u8> {
    0..5
}

/// A pattern for a value of a type `u8`, `Option[u8]`, or a pair: a literal
/// or a bare name.
#[derive(Clone, Debug)]
enum Part {
    Lit(u8),
    Name,
}

impl Part {
    fn written(&self, name: &str) -> String {
        match self {
            Part::Lit(n) => n.to_string(),
            Part::Name => name.to_string(),
        }
    }

    fn matches(&self, n: u8) -> bool {
        match self {
            Part::Lit(m) => *m == n,
            Part::Name => true,
        }
    }
}

fn part(g: &mut Gen) -> Part {
    if g.one_in(3) {
        Part::Name
    } else {
        Part::Lit(g.below(4) as u8)
    }
}

/// The rows of a `match` on a pair, and the cases of one on an `Option`.
type Patterns = (Vec<(Part, Part, i64)>, Vec<(Part, i64)>);

fn a_literal_in_a_case_and_in_a_tuple_is_matched_by_the_first_pattern_that_matches() {
    generate::check(
        15,
        120,
        |g| {
            // A pattern is kept only when some value reaches it, and the last
            // pattern is written only when some value is left for it.
            let mut rows: Vec<(Part, Part, i64)> = Vec::new();
            for i in 0..g.below(6) {
                let row = (part(g), part(g), i as i64);
                let reached = domain()
                    .flat_map(|a| domain().map(move |b| (a, b)))
                    .any(|(a, b)| {
                        row.0.matches(a)
                            && row.1.matches(b)
                            && !rows.iter().any(|r| r.0.matches(a) && r.1.matches(b))
                    });
                if reached {
                    rows.push(row);
                }
            }
            let mut cases: Vec<(Part, i64)> = Vec::new();
            for i in 0..g.below(5) {
                let case = (part(g), i as i64);
                if domain().any(|n| case.0.matches(n) && !cases.iter().any(|c| c.0.matches(n))) {
                    cases.push(case);
                }
            }
            (rows, cases)
        },
        |(rows, cases): &Patterns| {
            let mut source = String::from("fn pair(a: u8, b: u8) -> i64 {\n  match (a, b) {\n");
            for (x, y, given) in rows {
                source.push_str(&format!(
                    "    ({}, {}) {{ {given} }}\n",
                    x.written("p"),
                    y.written("q")
                ));
            }
            let left = domain()
                .flat_map(|a| domain().map(move |b| (a, b)))
                .any(|(a, b)| !rows.iter().any(|r| r.0.matches(a) && r.1.matches(b)));
            if left {
                source.push_str("    (p, q) { 100 }\n");
            }
            source.push_str("  }\n}\n\n");
            source.push_str("fn one(found: Option[u8]) -> i64 {\n  match found {\n");
            for (x, given) in cases {
                source.push_str(&format!("    .some({}) {{ {given} }}\n", x.written("n")));
            }
            if domain().any(|n| !cases.iter().any(|c| c.0.matches(n))) {
                source.push_str("    .some(n) { 100 }\n");
            }
            source.push_str("    .none { 200 }\n  }\n}\n\n");
            source.push_str("fn main() ! console, alloc {\n");
            let mut expected = Vec::new();
            for a in 0..4u8 {
                for b in 0..4u8 {
                    source.push_str(&format!("  print(\"{{pair({a}, {b})}}\")\n"));
                    let at = rows.iter().find(|(x, y, _)| x.matches(a) && y.matches(b));
                    expected.push(at.map_or(100, |r| r.2).to_string());
                }
            }
            for n in 0..4u8 {
                source.push_str(&format!("  print(\"{{one(.some({n}))}}\")\n"));
                let at = cases.iter().find(|(x, _)| x.matches(n));
                expected.push(at.map_or(100, |r| r.1).to_string());
            }
            source.push_str("  print(\"{one(.none)}\")\n}\n");
            expected.push("200".to_string());
            assert_eq!(prints(&source), expected, "for {source}");
        },
    );
}

fn a_match_on_whole_numbers_or_text_must_end_with_a_bare_name_and_repeat_nothing() {
    for (ty, one, two) in [("u8", "1", "2"), ("str", "\"a\"", "\"b\"")] {
        let wrap = |arms: &str| {
            format!(
                "fn f(b: {ty}) -> i64 {{\n  match b {{\n{arms}  }}\n}}\n\n\
                 fn main() ! console, alloc {{\n  print(\"{{f({one})}}\")\n}}\n"
            )
        };
        let open = wrap(&format!("    {one} {{ 1 }}\n    {two} {{ 2 }}\n"));
        let errors = refused(&open);
        assert!(
            errors.iter().any(|e| e.contains("rest")),
            "{errors:?} for {open}"
        );
        let twice = wrap(&format!(
            "    {one} {{ 1 }}\n    {one} {{ 2 }}\n    x {{ 3 }}\n"
        ));
        let errors = refused(&twice);
        assert!(
            errors.iter().any(|e| e.contains("never reached")),
            "{errors:?} for {twice}"
        );
        let covered = wrap(&format!("    x {{ 3 }}\n    {two} {{ 2 }}\n"));
        let errors = refused(&covered);
        assert!(
            errors.iter().any(|e| e.contains("never reached")),
            "{errors:?} for {covered}"
        );
        let fine = wrap(&format!("    {one} {{ 1 }}\n    x {{ 3 }}\n"));
        assert_eq!(refused(&fine), Vec::<String>::new(), "for {fine}");
    }
}

fn a_match_on_whole_numbers_is_one_switch_however_many_literals_it_has() {
    for count in [1, 2, 7, 40] {
        let mut source = String::from("fn f(b: u8) -> i64 {\n  match b {\n");
        for i in 0..count {
            source.push_str(&format!("    {} {{ {i} }}\n", i * 3));
        }
        source.push_str("    other { 1000 }\n  }\n}\n\nfn main() ! console {\n");
        source.push_str("  x: u8 = 3\n  if f(x) == 1 { print(\"yes\") }\n}\n");
        let program = lower(source.as_bytes()).expect("compiles");
        let switches: usize = (program.functions.iter().enumerate())
            .filter(|(at, _)| *at != program.main)
            .flat_map(|(_, f)| &f.blocks)
            .filter(|b| matches!(b.end, End::Switch { .. }))
            .count();
        assert_eq!(switches, 1, "for {source}");
    }
}
