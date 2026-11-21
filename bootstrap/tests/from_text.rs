//! `FromText` is given to every whole number of any range, to `bool`, and to
//! `str`, and `from_text` reads one value of the type from a text: a whole
//! number is its decimal digits, with a `-` where the range has numbers below
//! 0, and nothing else, and is in the range or is not read. A type of a
//! program's own has `FromText` when it writes it. The expectation of each
//! program is what the rule says of each text.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_whole_number_is_read_from_its_digits_exactly_when_it_is_in_its_range",
        a_whole_number_is_read_from_its_digits_exactly_when_it_is_in_its_range,
    ),
    (
        "the_sizes_of_whole_numbers_are_read_to_their_ends_and_refused_one_past",
        the_sizes_of_whole_numbers_are_read_to_their_ends_and_refused_one_past,
    ),
    (
        "a_bool_and_a_text_are_read_as_they_are_written",
        a_bool_and_a_text_are_read_as_they_are_written,
    ),
    (
        "a_type_with_an_impl_of_its_own_is_read_by_it",
        a_type_with_an_impl_of_its_own_is_read_by_it,
    ),
    (
        "a_type_with_no_reading_from_a_text_is_refused_where_it_is_read",
        a_type_with_no_reading_from_a_text_is_refused_where_it_is_read,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_reads_a_text_as_the_evaluator_does",
    every_executable_reads_a_text_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

fn printed(source: &str) -> String {
    String::from_utf8(outcome(source).stdout).expect("text")
}

/// A range, from `least` up to but not including `end`.
#[derive(Clone, Debug)]
struct Range {
    least: i64,
    end: i64,
}

fn range(g: &mut Gen) -> Range {
    let width = [3, 200, 2_000, 70_000, 5_000_000_000i64][g.below(5)];
    let least = match g.below(3) {
        0 => 0,
        1 => 1,
        _ => -(g.below(100) as i64) - 1,
    };
    Range {
        least,
        end: least + width,
    }
}

const USES: &str = "use std.console (console, print)\nuse std.deserialize (Problem, ReadError)\nuse std.fromtext (ReadText, from_text)\n\n";

/// What the rule says of a text read into the range: the number, or the kind
/// of the problem, which is `range` for digits that are out of it.
fn expected(r: &Range, text: &str) -> String {
    let (negative, digits) = match text.strip_prefix('-') {
        Some(rest) => (true, rest),
        None => (false, text),
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return "shape".to_string();
    }
    // A `-` is written only where the range has numbers below 0.
    if negative && r.least >= 0 {
        return "range".to_string();
    }
    match digits.parse::<i128>() {
        Ok(n) => {
            let n = if negative { -n } else { n };
            if i128::from(r.least) <= n && n < i128::from(r.end) {
                n.to_string()
            } else {
                "range".to_string()
            }
        }
        Err(_) => "range".to_string(),
    }
}

fn texts(r: &Range) -> Vec<String> {
    let mut out: Vec<String> = [r.least - 1, r.least, r.end - 1, r.end]
        .iter()
        .map(|n| n.to_string())
        .collect();
    for t in ["", "-", "+1", " 1", "1 ", "1x", "x", "007", "-0", "99999999999999999999999"] {
        out.push(t.to_string());
    }
    out
}

/// Each text is read into the range, and is the number, or `range`, or
/// `shape`, as the rule says.
fn a_whole_number_is_read_from_its_digits_exactly_when_it_is_in_its_range() {
    generate::check(8101, 6, range, |r| {
        let mut source = format!(
            "{USES}fn show(text: str) -> str ! alloc {{\n  \
             got: Result[{}..{}, ReadError] = from_text(\"n\", text)\n  match got {{\n    \
             .ok(v) {{ \"{{v}}\" }}\n    .err(e) {{\n      match e.problem {{\n        \
             .range(t) {{ \"range\" }}\n        .shape(p) {{ \"shape\" }}\n        \
             .malformed {{ \"other\" }}\n        .missing(n) {{ \"other\" }}\n        \
             .case(n) {{ \"other\" }}\n        .unknown(n) {{ \"other\" }}\n        \
             .twice(n) {{ \"other\" }}\n      }}\n    }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n",
            r.least, r.end
        );
        let mut said = String::new();
        for t in texts(r) {
            source += &format!("  print(show(\"{t}\"))\n");
            said += &format!("{}\n", expected(r, &t));
        }
        source += "}\n";
        assert_eq!(printed(&source), said, "for\n{source}");
    });
}

/// The numbers at the ends of each size, and one past each.
fn the_sizes_of_whole_numbers_are_read_to_their_ends_and_refused_one_past() {
    let sizes: &[(&str, &str, &str, &str, &str)] = &[
        ("u8", "0", "255", "-1", "256"),
        ("i8", "-128", "127", "-129", "128"),
        ("u16", "0", "65535", "-1", "65536"),
        ("i16", "-32768", "32767", "-32769", "32768"),
        ("u32", "0", "4294967295", "-1", "4294967296"),
        ("i32", "-2147483648", "2147483647", "-2147483649", "2147483648"),
        ("u64", "0", "18446744073709551615", "-1", "18446744073709551616"),
        ("Int", "-9223372036854775808", "9223372036854775807", "-9223372036854775809", "9223372036854775808"),
        ("Nat", "0", "18446744073709551615", "-1", "18446744073709551616"),
    ];
    for (ty, least, greatest, below, above) in sizes {
        let source = format!(
            "use std.bits (i8, i16, i32, u8, u16, u32, u64)\n{USES}fn show(text: str) -> str ! alloc {{\n  \
             got: Result[{ty}, ReadError] = from_text(\"n\", text)\n  match got {{\n    \
             .ok(v) {{ \"{{v}}\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
             fn main() ! console, alloc {{\n  print(show(\"{least}\"))\n  print(show(\"{greatest}\"))\n  \
             print(show(\"{below}\"))\n  print(show(\"{above}\"))\n}}\n"
        );
        let said = format!("{least}\n{greatest}\nerr\nerr\n");
        assert_eq!(printed(&source), said, "for {ty}\n{source}");
    }
}

fn a_bool_and_a_text_are_read_as_they_are_written() {
    let source = format!(
        "{USES}fn truth(text: str) -> str ! alloc {{\n  got: Result[bool, ReadError] = from_text(\"flag\", text)\n  \
         match got {{\n    .ok(v) {{ \"{{v}}\" }}\n    .err(e) {{ \"{{e.path}} err\" }}\n  }}\n}}\n\n\
         fn words(text: str) -> str ! alloc {{\n  got: Result[str, ReadError] = from_text(\"w\", text)\n  \
         match got {{\n    .ok(v) {{ \"<{{v}}>\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
         fn main() ! console, alloc {{\n  print(truth(\"true\"))\n  print(truth(\"false\"))\n  \
         print(truth(\"True\"))\n  print(truth(\"\"))\n  print(truth(\"truee\"))\n  \
         print(words(\"\"))\n  print(words(\"a b\"))\n}}\n"
    );
    assert_eq!(
        printed(&source),
        "true\nfalse\nflag err\nflag err\nflag err\n<>\n<a b>\n"
    );
}

/// A type that writes `impl FromText` is read by it, and `from_text` names
/// the parameter in the error it gives.
fn a_type_with_an_impl_of_its_own_is_read_by_it() {
    let source = format!(
        "{}enum Level {{\n  low\n  high\n}}\n\n\
         impl FromText[Level] {{\n  fn parse(text: str) -> Option[Level] {{\n    if text == \"low\" {{ Option.some(Level.low) }} \
         else if text == \"high\" {{ Option.some(Level.high) }} else {{ Option.none }}\n  }}\n  \
         fn expects() -> str {{ \"low or high\" }}\n}}\n\n\
         fn show(text: str) -> str ! alloc {{\n  got: Result[Level, ReadError] = from_text(\"level\", text)\n  \
         match got {{\n    .ok(v) {{ match v {{\n      .low {{ \"low\" }}\n      .high {{ \"high\" }}\n    }} }}\n    \
         .err(e) {{ match e.problem {{\n      .shape((wanted, found)) {{ \"{{e.path}}: wanted {{wanted}}\" }}\n      \
         .range(t) {{ \"range\" }}\n      .malformed {{ \"other\" }}\n      .missing(n) {{ \"other\" }}\n      \
         .case(n) {{ \"other\" }}\n      .unknown(n) {{ \"other\" }}\n      .twice(n) {{ \"other\" }}\n    }} }}\n  }}\n}}\n\n\
         fn main() ! console, alloc {{\n  print(show(\"low\"))\n  print(show(\"high\"))\n  print(show(\"mid\"))\n}}\n",
        USES.replace("(ReadText, from_text)", "(FromText, ReadText, from_text)")
    );
    assert_eq!(printed(&source), "low\nhigh\nlevel: wanted low or high\n");
}

/// A struct has no reading from a text, so reading one is refused, naming it.
fn a_type_with_no_reading_from_a_text_is_refused_where_it_is_read() {
    let source = format!(
        "{USES}struct Note {{\n  title: str\n}}\n\nfn main() ! console, alloc {{\n  \
         got: Result[Note, ReadError] = from_text(\"n\", \"x\")\n  print(\"done\")\n}}\n"
    );
    let Err(found) = check(source.as_bytes()) else {
        panic!("accepted");
    };
    assert!(
        found.iter().any(|e| e.message.contains("`Note`")),
        "{found:?}"
    );
}

fn every_executable_reads_a_text_as_the_evaluator_does() {
    let source = format!(
        "use std.bits (i8, u64)\n{USES}fn show(text: str) -> str ! alloc {{\n  \
         got: Result[-5..200, ReadError] = from_text(\"n\", text)\n  match got {{\n    \
         .ok(v) {{ \"{{v}}\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
         fn wide(text: str) -> str ! alloc {{\n  got: Result[u64, ReadError] = from_text(\"n\", text)\n  \
         match got {{\n    .ok(v) {{ \"{{v}}\" }}\n    .err(e) {{ \"err\" }}\n  }}\n}}\n\n\
         fn main() ! console, alloc {{\n  print(show(\"-5\"))\n  print(show(\"-6\"))\n  print(show(\"199\"))\n  \
         print(show(\"200\"))\n  print(show(\"x\"))\n  print(wide(\"18446744073709551615\"))\n  \
         print(wide(\"18446744073709551616\"))\n}}\n"
    );
    let said = outcome(&source);
    assert_eq!(
        String::from_utf8(said.stdout.clone()).unwrap(),
        "-5\nerr\n199\nerr\nerr\n18446744073709551615\nerr\n"
    );
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
