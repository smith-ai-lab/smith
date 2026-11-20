//! A file that uses what the compiler does not build yet gets every other
//! error it has in the same run, each once and at its place, and no error
//! that only follows from what is not built. Each property takes the places
//! from the text the generator wrote.

use std::collections::{BTreeMap, BTreeSet};

use smith::diag::Pos;
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::{self, Expected, pos_after};
use crate::{choices, enums, lambdas, numbers, reprs, text, traits, tries, tuples};

pub const ONCE: &[Test] = &[
    (
        "a_use_of_what_is_not_built_in_a_file_with_a_placed_mistake_gives_both_errors",
        a_use_of_what_is_not_built_in_a_file_with_a_placed_mistake_gives_both_errors,
    ),
    (
        "what_is_not_built_and_is_used_gives_its_error_and_the_mistake_of_another_function_gives_its",
        what_is_not_built_and_is_used_gives_its_error_and_the_mistake_of_another_function_gives_its,
    ),
    (
        "a_value_that_is_not_built_is_said_nothing_of_where_it_is_used",
        a_value_that_is_not_built_is_said_nothing_of_where_it_is_used,
    ),
];

/// What a file may use that is not built, written with a `@` before each
/// place its error must be at, and `{i}` for a number that makes the names
/// of its functions its own.
struct Unbuilt {
    declares: &'static str,
    /// What `main` runs, or nothing when what it declares cannot be run.
    call: &'static str,
    /// Whether the parser reports all of it, so that it is reported even where
    /// nothing runs it.
    parsed: bool,
}

const UNBUILT: &[Unbuilt] = &[
    Unbuilt {
        declares: "fn u{i}() -> Int {\n  x = @1.5\n  wrapping_add(x, 1)\n}\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}() -> Int { @1e5 }\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}() -> Int {\n  x = @1.5\n  y = @2.5\n  wrapping_add(x, y)\n}\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}(a: @[3]Int) -> Int { 1 }\n",
        call: "u{i}([1, 2, 3])",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}() -> @[3]Int { [1, 2, 3] }\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}[n: @Nat](a: @[n]Int) -> Int { 1 }\n",
        call: "u{i}([1, 2])",
        parsed: false,
    },
    Unbuilt {
        declares: "fn u{i}(n: Int) -> Int {\n  var y = n\n  y @+= 1\n  wrapping_add(y, 1)\n}\n",
        call: "u{i}(1)",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}() -> Int { (1)@(2) }\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "struct S{i} {\n  f: fn(Int) -> Int @! console\n}\n",
        call: "",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}(a: @(Int)) -> Int { 1 }\n",
        call: "u{i}(1)",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}(n: Int) -> Int {\n  if n == 0 { 0 } else { wrapping_add(@u{i}(wrapping_sub(n, 1)), 1) }\n}\n",
        call: "u{i}(1)",
        parsed: false,
    },
    Unbuilt {
        declares: "fn u{i}(n: Int) -> Int { v{i}(n) }\nfn v{i}(n: Int) -> Int { @u{i}(n) }\n",
        call: "u{i}(1)",
        parsed: false,
    },
    Unbuilt {
        declares: "fn u{i}() -> Int ! @fs { 1 }\n",
        call: "u{i}()",
        parsed: false,
    },
    // In a block inside a function, and in two blocks of one statement.
    Unbuilt {
        declares: "fn u{i}(c: bool) -> Int {\n  if c {\n    @1.5\n  } else {\n    @1.5\n  }\n}\n",
        call: "u{i}(true)",
        parsed: true,
    },
    Unbuilt {
        declares: "fn u{i}(c: bool) -> Int {\n  x = if c {\n    @2.5\n  } else {\n    3\n  }\n  wrapping_add(x, 1)\n}\n",
        call: "u{i}(true)",
        parsed: true,
    },
    // A name a statement that is not built would bind is still bound.
    Unbuilt {
        declares: "fn u{i}() -> Int {\n  a = 1\n  z = a\n  b = @1.5\n  (c, d) = @1.5\n  wrapping_add(z, wrapping_add(b, wrapping_add(c, d)))\n}\n",
        call: "u{i}()",
        parsed: true,
    },
    Unbuilt {
        declares: "struct P{i} { x: Int = @1.5 }\nfn u{i}() -> Int { P{i}().x }\n",
        call: "u{i}()",
        parsed: true,
    },
];

/// The names of the library a file imports, by module.
type Uses = BTreeMap<String, BTreeSet<String>>;

/// The `use` lines of `uses`, sorted as the formatter sorts them, and the
/// blank line after them, or nothing when it holds no name.
fn header(uses: &Uses) -> String {
    let mut out = String::new();
    for (module, names) in uses {
        let names: Vec<&str> = names.iter().map(String::as_str).collect();
        out.push_str(&format!("use {module} ({})\n", names.join(", ")));
    }
    if !out.is_empty() {
        out.push('\n');
    }
    out
}

/// The `use` lines a program begins with, and the length of the text they
/// and the blank lines after them take.
fn leading_uses(program: &[u8]) -> (Uses, usize) {
    let text = String::from_utf8_lossy(program);
    let mut uses = Uses::new();
    let mut end = 0;
    for line in text.split_inclusive('\n') {
        if let Some(rest) = line.strip_prefix("use ") {
            let (module, names) = rest.split_once(" (").expect("a use line");
            let names = names.trim_end().trim_end_matches(')');
            let at = uses.entry(module.to_string()).or_default();
            at.extend(names.split(", ").map(str::to_string));
        } else if !line.trim().is_empty() {
            break;
        }
        end += line.len();
    }
    (uses, end)
}

/// Some of what is not built, written as functions that follow `before`,
/// where each error of it is, and the statements of a `main` that runs it.
/// Only what the parser reports in full is used when nothing runs it.
fn written(g: &mut Gen, before: &str, runs: bool) -> (String, Vec<Pos>, String, Uses) {
    let mut text = String::new();
    let mut uses = Uses::new();
    let mut places = Vec::new();
    let mut calls = String::new();
    for i in 0..1 + g.below(4) {
        let one = loop {
            let one = g.pick(UNBUILT);
            if runs || one.parsed {
                break one;
            }
        };
        let declares = one.declares.replace("{i}", &i.to_string());
        for (module, name) in [
            ("std.console", "console"),
            ("std.int", "wrapping_add"),
            ("std.int", "wrapping_sub"),
        ] {
            if declares.contains(name) {
                uses.entry(module.to_string())
                    .or_default()
                    .insert(name.to_string());
            }
        }
        let mut rest = declares.as_str();
        while let Some(at) = rest.find('@') {
            text.push_str(&rest[..at]);
            places.push(pos_after(&format!("{before}{text}")));
            rest = &rest[at + 1..];
        }
        text.push_str(rest);
        if runs && !one.call.is_empty() {
            let call = one.call.replace("{i}", &i.to_string());
            calls.push_str(&format!("  r{i} = {call}\n  print(\"{{r{i}}}\")\n"));
        }
    }
    (text, places, calls, uses)
}

/// A program with a mistake placed in it, from one of the generators.
fn with_mistake(g: &mut Gen) -> (Vec<u8>, Expected) {
    let (source, mistake) = match g.below(10) {
        0 => {
            let c = programs::program_with_mistake(g);
            (c.source, c.mistake)
        }
        1 => {
            let c = traits::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        2 => {
            let c = enums::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        3 => {
            let c = tuples::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        4 => {
            let c = lambdas::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        5 => {
            let c = tries::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        6 => {
            let c = text::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        7 => {
            let c = choices::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        8 => {
            let c = numbers::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
        _ => {
            let c = reprs::program_with_mistake(g);
            (c.source.into_bytes(), c.mistake)
        }
    };
    (source, mistake.expect("a placed mistake"))
}

/// Checks that the file gives an error at each of `places` that says a thing
/// is not supported yet, and one at `mistake` that mentions what it must, and
/// no other, each once and in the order of their places.
fn assert_errors(source: &[u8], mistake: &Expected, places: &[Pos]) {
    let found = lower(source).expect_err("errors");
    let mut wanted = places.to_vec();
    wanted.push(mistake.at);
    wanted.sort();
    let got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
    assert_eq!(got, wanted, "{found:?}");
    for e in &found {
        if e.pos == mistake.at {
            assert!(e.message.contains(&mistake.mentions), "{e}");
        } else {
            assert!(e.message.contains("not supported yet"), "{e}");
        }
    }
}

/// A program with a placed mistake and, before it, functions that use what
/// is not built, which nothing runs.
fn program_with_unbuilt(g: &mut Gen) -> (Vec<u8>, Expected, Vec<Pos>) {
    let (program, mut mistake) = with_mistake(g);
    let (mut source, mut places, _, needed) = written(g, "", false);
    // A mistake in the text or the tokens hides every error of the later
    // stages, so the file is the program as it is, and the functions are not
    // written into it, whose `use` lines the mistake may be in.
    if smith::count_tokens(&program).is_err() {
        return (program, mistake, Vec::new());
    }
    source.push('\n');
    // The functions follow the `use` lines of the program, with the names
    // they use added.
    let (mut uses, end) = leading_uses(&program);
    for (module, names) in needed {
        uses.entry(module).or_default().extend(names);
    }
    let head = header(&uses);
    let moved = head.matches('\n').count() + source.matches('\n').count()
        - program[..end].iter().filter(|b| **b == b'\n').count();
    for place in &mut places {
        place.line += head.matches('\n').count();
    }
    // An error about the whole file, that there is no `main`, stays at its start.
    if mistake.mentions != "`main`" {
        mistake.at.line += moved;
    }
    let mut out = head.into_bytes();
    out.extend_from_slice(source.as_bytes());
    out.extend_from_slice(&program[end..]);
    (out, mistake, places)
}

fn a_use_of_what_is_not_built_in_a_file_with_a_placed_mistake_gives_both_errors() {
    generate::check(2430, 45, program_with_unbuilt, |(source, mistake, places)| {
        assert_errors(source, mistake, places);
    });
}

/// A file with a function that holds a mistake, and a `main` that runs it
/// and the functions that use what is not built, and uses what they give.
fn program_that_runs_unbuilt(g: &mut Gen) -> (Vec<u8>, Expected, Vec<Pos>) {
    let (head, mistake, mentions) = if g.one_in(2) {
        ("fn bad(a: Int) -> Int {\n  a ", "+ \"b\"", "whole numbers")
    } else {
        ("fn bad(a: Int) -> str {\n  ", "a", "text")
    };
    let mut source = head.to_string();
    let at = pos_after(&source);
    source.push_str(mistake);
    source.push_str("\n}\n\n");
    let (more, mut places, calls, mut uses) = written(g, &source, true);
    source.push_str(&more);
    source.push_str("\nfn main() ! console, alloc {\n  a = bad(1)\n");
    source.push_str(&calls);
    source.push_str("  print(\"{a}\")\n}\n");
    uses.entry("std.console".to_string())
        .or_default()
        .extend(["console".to_string(), "print".to_string()]);
    let head = header(&uses);
    // What is written is placed from the start of the text, and the lines of
    // the `use` lines move it all down.
    let at = Pos {
        line: at.line + head.matches('\n').count(),
        col: at.col,
    };
    for place in &mut places {
        place.line += head.matches('\n').count();
    }
    let source = format!("{head}{source}");
    let mistake = Expected {
        at,
        mentions: mentions.to_string(),
        fix: None,
    };
    (source.into_bytes(), mistake, places)
}

fn what_is_not_built_and_is_used_gives_its_error_and_the_mistake_of_another_function_gives_its() {
    generate::check(2431, 55, program_that_runs_unbuilt, |(source, mistake, places)| {
        assert_errors(source, mistake, places);
    });
}

/// A value that is not built is the value of a binding that is read, of a
/// field, of a condition, and of an argument, and nothing is said of any.
fn a_value_that_is_not_built_is_said_nothing_of_where_it_is_used() {
    let before = "use std.console (console, print)\nuse std.int (wrapping_add)\n\nfn f() -> Int ! console, alloc {\n  x = ";
    let source = format!(
        "{before}1.5\n  y = x\n  a = if y == 2 {{ 1 }} else {{ 2 }}\n  b = wrapping_add(y, a)\n  print(\"{{y}}{{b}}\")\n  b\n}}\n\n\
         fn main() ! console, alloc {{\n  print(\"{{f()}}\")\n}}\n"
    );
    let found = lower(source.as_bytes()).expect_err("an error");
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos, pos_after(before));
    assert!(found[0].message.contains("a float is not supported yet"));
}
