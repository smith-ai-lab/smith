//! The status a program ends with, which what `main` returns decides
//! through `Exit`.
//!
//! What a program should do is worked out in Rust from what the generator
//! built it with, so it shares nothing with the compiler.

use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::arguments::output_of;
use crate::cli::{folder, smith, stderr};
use crate::generate::{self, Gen};
use crate::targets::{build, fingerprinting, record, runnable};

pub const ONCE: &[Test] = &[
    (
        "the_three_programs_of_the_status_page_do_what_it_says",
        the_three_programs_of_the_status_page_do_what_it_says,
    ),
    (
        "a_main_that_returns_a_result_of_nothing_and_text_ends_with_what_the_result_says",
        a_main_that_returns_a_result_of_nothing_and_text_ends_with_what_the_result_says,
    ),
    (
        "a_program_ends_with_every_status_its_own_exit_gives",
        a_program_ends_with_every_status_its_own_exit_gives,
    ),
    (
        "a_main_that_returns_a_type_with_no_exit_is_refused_and_the_error_names_the_type_and_the_trait",
        a_main_that_returns_a_type_with_no_exit_is_refused_and_the_error_names_the_type_and_the_trait,
    ),
    (
        "try_works_in_a_main_that_returns_a_result",
        try_works_in_a_main_that_returns_a_result,
    ),
    (
        "a_main_that_returns_nothing_is_not_wrapped",
        a_main_that_returns_nothing_is_not_wrapped,
    ),
    (
        "a_main_that_takes_the_words_may_return_a_status_too",
        a_main_that_takes_the_words_may_return_a_status_too,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_ends_with_the_status_the_evaluator_says",
        every_executable_ends_with_the_status_the_evaluator_says,
    ),
    (
        "every_executable_ends_with_every_edge_of_the_statuses",
        every_executable_ends_with_every_edge_of_the_statuses,
    ),
    (
        "run_ends_with_the_status_of_the_program",
        run_ends_with_the_status_of_the_program,
    ),
];

/// The first program of the page.
const FAILS: &str = "use std.console (console, print)

fn main() -> Result[(), str] ! console, alloc {
  print(\"started\")
  .err(\"could not finish\")
}
";

/// The program of the page that has an `Exit` of its own.
const OUTCOME: &str = "use std.console (console)

enum Outcome {
  done
  usage
}

impl Exit[Outcome] {
  fn exit(value: Outcome) -> Byte ! console, alloc {
    match value {
      .done { 0 }
      .usage { 2 }
    }
  }
}

fn main() -> Outcome ! console, alloc { .usage }
";

fn program(source: &str) -> scalar::Program {
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::check(&lowered), Ok(()));
    scalar::passes(&lowered)
}

/// What the evaluator says a program does: what it prints, its status, and
/// what it writes to standard error.
fn said(source: &str) -> (Outcome, Vec<u8>) {
    scalar::run_with_words_and_errors(&program(source), &[])
}

fn the_three_programs_of_the_status_page_do_what_it_says() {
    assert_eq!(
        said(FAILS),
        (
            Outcome {
                stdout: b"started\n".to_vec(),
                status: 1
            },
            b"could not finish\n".to_vec()
        )
    );
    assert_eq!(
        said(OUTCOME),
        (
            Outcome {
                stdout: Vec::new(),
                status: 2
            },
            Vec::new()
        )
    );
    assert_eq!(
        said("use std.console (console, print)\n\nfn main() ! console { print(\"hi\") }\n"),
        (
            Outcome {
                stdout: b"hi\n".to_vec(),
                status: 0
            },
            Vec::new()
        )
    );
}

/// What a program that gives `.ok(())` or `.err(text)` is, as a case of the
/// generator builds it: the text, if it fails.
fn failure(g: &mut Gen) -> Option<String> {
    if g.one_in(3) {
        return None;
    }
    let length = g.below(12);
    let letters: Vec<char> = (0x20u8..0x7f)
        .map(char::from)
        .filter(|c| !matches!(c, '"' | '\\' | '{' | '}'))
        .collect();
    Some((0..length).map(|_| *g.pick(&letters)).collect())
}

fn returns_result(failure: &Option<String>) -> String {
    let given = match failure {
        None => ".ok(())".to_string(),
        Some(text) => format!(".err(\"{text}\")"),
    };
    format!(
        "use std.console (console)\n\nfn main() -> Result[(), str] ! console, alloc {{\n  {given}\n}}\n"
    )
}

/// What a `main` that returns `.ok(())` or `.err(text)` does: nothing is
/// printed, and a failure's text is written to standard error with a line
/// break, and gives status 1.
fn result_of(failure: &Option<String>) -> (Outcome, Vec<u8>) {
    match failure {
        None => (
            Outcome {
                stdout: Vec::new(),
                status: 0,
            },
            Vec::new(),
        ),
        Some(text) => (
            Outcome {
                stdout: Vec::new(),
                status: 1,
            },
            format!("{text}\n").into_bytes(),
        ),
    }
}

fn a_main_that_returns_a_result_of_nothing_and_text_ends_with_what_the_result_says() {
    generate::check(1, 60, failure, |failure| {
        assert_eq!(said(&returns_result(failure)), result_of(failure));
    });
}

/// A program whose own `Exit` gives the status.
fn gives_status(status: u8) -> String {
    format!(
        "use std.console (console)

struct Code {{
  number: Byte
}}

impl Exit[Code] {{
  fn exit(value: Code) -> Byte ! console, alloc {{ value.number }}
}}

fn main() -> Code ! console, alloc {{ Code(number: {status}) }}
"
    )
}

fn a_program_ends_with_every_status_its_own_exit_gives() {
    // Every status is a case, with no program written twice: the source
    // changes by the one number, so the program is lowered once for the
    // lengths of the numbers and then for each.
    for status in 0..=255u8 {
        assert_eq!(
            said(&gives_status(status)).0.status,
            i32::from(status),
            "status {status}"
        );
    }
}

/// The errors a source gives, one to a line, as the command shows them.
fn errors_of(source: &str) -> Vec<String> {
    match lower(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.iter().map(|e| e.to_string()).collect(),
    }
}

fn a_main_that_returns_a_type_with_no_exit_is_refused_and_the_error_names_the_type_and_the_trait() {
    let types = "use std.bits (i32)
use std.console (console)

struct Plain {
  n: i32
}

enum Mood {
  calm
  upset
}

";
    for (ty, value) in [
        ("i32", "3"),
        ("Plain", "Plain(n: 1)"),
        ("Mood", ".calm"),
        ("Option[()]", ".none"),
        ("Result[Plain, str]", ".ok(Plain(n: 1))"),
    ] {
        let source = format!("{types}fn main() -> {ty} ! console, alloc {{ {value} }}\n");
        let errors = errors_of(&source);
        assert!(!errors.is_empty(), "{source}");
        let all = errors.join("\n");
        let name = ty.split(['[', ',']).next().unwrap();
        assert!(all.contains("Exit"), "{source}: {all}");
        assert!(all.contains(name), "{source}: {all}");
    }
}

fn try_works_in_a_main_that_returns_a_result() {
    let source = "use std.bits (i32)
use std.console (console, print)

fn half(x: i32) -> Result[i32, str] {
  if x > 0 { .ok(x) } else { .err(\"not above zero\") }
}

fn main() -> Result[(), str] ! console, alloc {
  a = try half(4)
  print(\"{a}\")
  b = try half(0)
  print(\"{b}\")
  .ok(())
}
";
    assert_eq!(
        said(source),
        (
            Outcome {
                stdout: b"4\n".to_vec(),
                status: 1
            },
            b"not above zero\n".to_vec()
        )
    );
}

fn a_main_that_returns_nothing_is_not_wrapped() {
    // The program starts at `main` itself, which takes nothing and has one
    // call to the system for what it prints, and none for ending.
    let source = "use std.console (console, print)\n\nfn main() ! console { print(\"hi\") }\n";
    let program = program(source);
    assert_eq!(program.functions.len(), 1);
    assert_eq!(program.main, 0);
}

fn a_main_that_takes_the_words_may_return_a_status_too() {
    let source = "use std.console (console)

fn main(args: List[str]) -> Result[(), str] ! console, alloc {
  if args.size > 1 { .err(\"too many\") } else { .ok(()) }
}
";
    let program = program(source);
    let run = |words: &[&str]| {
        let words: Vec<Vec<u8>> = words.iter().map(|w| w.as_bytes().to_vec()).collect();
        scalar::run_with_words_and_errors(&program, &words)
    };
    assert_eq!(run(&["a"]).0.status, 0);
    assert_eq!(
        run(&["a", "b"]),
        (
            Outcome {
                stdout: Vec::new(),
                status: 1
            },
            b"too many\n".to_vec(),
        )
    );
}

/// What an executable does that is not what the evaluator says, said so the
/// failure shows the target.
fn executables_end_as(source: &str, expected: &(Outcome, Vec<u8>)) {
    for target in runnable() {
        let bytes = build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        if fingerprinting() {
            record(target, &[&bytes, &expected.0.stdout, &expected.1]);
            continue;
        }
        let output = output_of(&bytes, &[]);
        let shown = format!("{}: {output:?}", target.name());
        assert_eq!(output.status.code(), Some(expected.0.status), "{shown}");
        assert_eq!(output.stdout, expected.0.stdout, "{shown}");
        assert_eq!(output.stderr, expected.1, "{shown}");
    }
}

fn every_executable_ends_with_the_status_the_evaluator_says() {
    for source in [FAILS, OUTCOME] {
        executables_end_as(source, &said(source));
    }
    generate::check(2, 12, failure, |failure| {
        let source = returns_result(failure);
        let expected = said(&source);
        assert_eq!(expected, result_of(failure));
        executables_end_as(&source, &expected);
    });
}

fn every_executable_ends_with_every_edge_of_the_statuses() {
    // The statuses at the edges of the bits a status is made of, and
    // around the sign bit, which a wrong extension would change.
    for status in [0, 1, 2, 63, 64, 126, 127, 128, 129, 200, 254, 255] {
        let source = gives_status(status);
        executables_end_as(&source, &said(&source));
    }
}

fn run_ends_with_the_status_of_the_program() {
    if fingerprinting() {
        return crate::targets::record_for_every_target(FAILS.as_bytes());
    }
    if !crate::targets::runnable()
        .contains(&smith::target::Target::host().unwrap_or_else(crate::targets::any))
    {
        return;
    }
    let dir = folder("run_status");
    std::fs::write(dir.join("fails.smith"), FAILS).unwrap();
    std::fs::write(dir.join("usage.smith"), OUTCOME).unwrap();
    let ran = smith(&dir, &["run", "fails.smith"]);
    assert_eq!(ran.status.code(), Some(1), "{}", stderr(&ran));
    assert_eq!(ran.stdout, b"started\n");
    assert!(
        stderr(&ran).ends_with("could not finish\n"),
        "{}",
        stderr(&ran)
    );
    let ran = smith(&dir, &["run", "usage.smith"]);
    assert_eq!(ran.status.code(), Some(2), "{}", stderr(&ran));
}
