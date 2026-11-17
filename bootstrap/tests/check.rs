//! The `smith check` command: it reports what `smith build` reports, and
//! warnings besides, and writes no file.
//!
//! The warnings are checked on programs from a generator of its own, which
//! builds programs with no warning and can place one definition that
//! `main` does not reach, or one binding that is never read.

use std::fmt;
use std::path::Path;

use smith::diag::{self, Error, Pos};
use smith::{check, compile, lower};

use crate::Test;
use crate::cli::{cases, folder, smith, stderr, timing_lines};
use crate::generate::{self, Gen};
use crate::programs::{PIECES, pos_after};
use crate::props::{self, Bytes};
use crate::targets::any;

pub const ONCE: &[Test] = &[
    (
        "check_of_a_program_with_nothing_to_report_prints_nothing",
        check_of_a_program_with_nothing_to_report_prints_nothing,
    ),
    (
        "check_reports_what_build_reports_for_the_cases_and_writes_no_file",
        check_reports_what_build_reports_for_the_cases_and_writes_no_file,
    ),
    (
        "check_reports_the_errors_build_reports_for_every_input",
        check_reports_the_errors_build_reports_for_every_input,
    ),
    (
        "check_warns_of_dead_code_and_an_unread_binding_that_build_allows",
        check_warns_of_dead_code_and_an_unread_binding_that_build_allows,
    ),
    (
        "check_run_and_build_refuse_the_error_in_a_function_main_does_not_reach",
        check_run_and_build_refuse_the_error_in_a_function_main_does_not_reach,
    ),
    (
        "without_a_file_check_checks_main_smith",
        without_a_file_check_checks_main_smith,
    ),
    (
        "check_timings_list_the_stages_before_emit",
        check_timings_list_the_stages_before_emit,
    ),
    (
        "every_placed_warning_is_reported_by_check_alone_at_its_place",
        every_placed_warning_is_reported_by_check_alone_at_its_place,
    ),
];

const HELLO: &str = "fn main() ! console { print(\"hello world\") }\n";

/// The program of the issue that asked for warnings: a function nothing
/// calls, and a binding nothing reads.
const TWO_WARNINGS: &str = "fn unused() -> str { \"never called\" }

fn main() ! console {
  word = \"never read\"
  print(\"hello\")
}
";

/// The names of the files in a folder, in order.
fn files_in(dir: &Path) -> Vec<String> {
    let mut names: Vec<String> = std::fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
        .collect();
    names.sort();
    names
}

fn check_of_a_program_with_nothing_to_report_prints_nothing() {
    let dir = folder("clean");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let checked = smith(&dir, &["check", "hello.smith"]);
    assert_eq!(checked.status.code(), Some(0), "{}", stderr(&checked));
    assert!(checked.stdout.is_empty());
    assert!(checked.stderr.is_empty(), "{}", stderr(&checked));
    assert_eq!(files_in(&dir), ["hello.smith"]);
}

fn check_reports_what_build_reports_for_the_cases_and_writes_no_file() {
    for source in cases("err") {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let dir = folder(name);
        std::fs::copy(&source, dir.join(&file)).unwrap();
        let checked = smith(&dir, &["check", &file]);
        assert!(!checked.status.success(), "{file}");
        let expected = std::fs::read_to_string(source.with_extension("err")).unwrap();
        assert_eq!(stderr(&checked), expected, "{file}");
        assert!(checked.stdout.is_empty(), "{file}");
        assert_eq!(files_in(&dir), std::slice::from_ref(&file), "{file}");
    }
}

fn check_reports_the_errors_build_reports_for_every_input() {
    generate::check(41, 483, props::bytes, |Bytes(source)| {
        match (lower(source), check(source)) {
            (Err(built), Err(checked)) => assert_eq!(checked, built),
            (Ok(_), Ok(_)) => {}
            (built, checked) => panic!("build gives {built:?}, and check gives {checked:?}"),
        }
    });
}

fn check_warns_of_dead_code_and_an_unread_binding_that_build_allows() {
    let dir = folder("warned");
    std::fs::write(dir.join("two.smith"), TWO_WARNINGS).unwrap();
    let checked = smith(&dir, &["check", "two.smith"]);
    assert!(!checked.status.success());
    let warnings = check(TWO_WARNINGS.as_bytes()).expect("no error");
    assert_eq!(warnings.len(), 2, "{warnings:?}");
    let places: Vec<Pos> = warnings.iter().map(|w| w.pos).collect();
    let at = |name: &str| pos_after(&TWO_WARNINGS[..TWO_WARNINGS.find(name).unwrap()]);
    assert_eq!(places, [at("unused"), at("word")]);
    assert!(warnings[0].message.contains("dead code"), "{}", warnings[0]);
    assert!(warnings[1].message.contains("`word`"), "{}", warnings[1]);
    assert!(warnings.iter().all(|w| w.fix.is_some()), "{warnings:?}");
    // A warning is printed in the one form an error is.
    let printed = diag::render("two.smith", TWO_WARNINGS.as_bytes(), &warnings);
    assert_eq!(stderr(&checked), printed);
    assert_eq!(printed.matches("\n  fix: ").count(), 2, "{printed}");
    assert_eq!(files_in(&dir), ["two.smith"]);

    let built = smith(&dir, &["build", "--target", any().name(), "two.smith"]);
    assert!(built.status.success(), "{}", stderr(&built));
    assert!(built.stderr.is_empty(), "{}", stderr(&built));
}

/// An error in a function that `main` does not reach, as it is placed.
const UNREACHED_ERROR: &str = "fn dead(x: u8) -> u8 { x ";

fn check_run_and_build_refuse_the_error_in_a_function_main_does_not_reach() {
    let source = format!("{UNREACHED_ERROR}+ 1 }}\n\nfn main() ! console {{ print(\"a\") }}\n");
    let dir = folder("dead");
    std::fs::write(dir.join("dead.smith"), &source).unwrap();
    let target = any().name();
    let commands: [&[&str]; 3] = [
        &["check", "dead.smith"],
        &["run", "--target", target, "dead.smith"],
        &["build", "--target", target, "dead.smith"],
    ];
    let place = pos_after(UNREACHED_ERROR);
    for args in commands {
        let output = smith(&dir, args);
        assert!(!output.status.success(), "{args:?}");
        assert!(output.stdout.is_empty(), "{args:?}");
        let shown = stderr(&output);
        assert!(
            shown.starts_with(&format!("dead.smith:{}:{}: ", place.line, place.col)),
            "{shown}"
        );
        assert!(shown.contains("may not fit"), "{shown}");
        assert_eq!(files_in(&dir), ["dead.smith"], "{args:?}");
    }
}

fn without_a_file_check_checks_main_smith() {
    let dir = folder("project");
    let missing = smith(&dir, &["check"]);
    assert!(!missing.status.success());
    assert!(
        stderr(&missing).contains("main.smith"),
        "{}",
        stderr(&missing)
    );

    std::fs::write(dir.join("main.smith"), TWO_WARNINGS).unwrap();
    let named = smith(&dir, &["check", "main.smith"]);
    let unnamed = smith(&dir, &["check"]);
    assert!(!unnamed.status.success());
    assert_eq!(unnamed.status.code(), named.status.code());
    assert_eq!(stderr(&unnamed), stderr(&named));
    assert!(stderr(&unnamed).starts_with("main.smith:"));

    std::fs::write(dir.join("main.smith"), HELLO).unwrap();
    let clean = smith(&dir, &["check"]);
    assert_eq!(clean.status.code(), Some(0), "{}", stderr(&clean));
    assert!(clean.stderr.is_empty());
    assert_eq!(files_in(&dir), ["main.smith"]);
}

fn check_timings_list_the_stages_before_emit() {
    let mut sources: Vec<(String, String, String)> = vec![
        ("hello.smith".into(), HELLO.into(), String::new()),
        ("two.smith".into(), TWO_WARNINGS.into(), {
            let warnings = check(TWO_WARNINGS.as_bytes()).unwrap();
            diag::render("two.smith", TWO_WARNINGS.as_bytes(), &warnings)
        }),
    ];
    for source in cases("err") {
        let file = source.file_name().unwrap().to_string_lossy().into_owned();
        let text = std::fs::read_to_string(&source).unwrap();
        let errors = std::fs::read_to_string(source.with_extension("err")).unwrap();
        sources.push((file, text, errors));
    }
    for (file, text, reported) in sources {
        let dir = folder("timed");
        std::fs::write(dir.join(&file), &text).unwrap();
        let checked = smith(&dir, &["check", "--timings", &file]);
        let shown = stderr(&checked);
        assert_eq!(checked.status.success(), reported.is_empty(), "{file}");
        assert!(shown.starts_with(&reported), "{file}: {shown}");
        let names: Vec<String> = timing_lines(&shown[reported.len()..])
            .into_iter()
            .map(|(name, _)| name)
            .collect();
        let (last, ran) = names.split_last().unwrap();
        assert_eq!(last, "total", "{file}");
        let stages = ["lex", "parse", "check"];
        assert!(
            !ran.is_empty() && ran == &stages[..ran.len()],
            "{file}: {names:?}"
        );
        if reported.is_empty() || text == TWO_WARNINGS {
            assert_eq!(ran, stages, "{file}");
        }
        assert_eq!(files_in(&dir), std::slice::from_ref(&file), "{file}");
    }
}

/// A generated program, and the warnings `smith check` must report about
/// it: the place of each, in order, and what its message must mention.
struct Linted {
    source: String,
    warnings: Vec<(Pos, &'static str)>,
}

impl fmt::Debug for Linted {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// What the generator places in a program that has no warning.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Place {
    Nothing,
    DeadFunction,
    DeadStruct,
    DeadTrait,
    /// A trait and an implementation of it, each of which is a warning.
    DeadImplementation,
    UnreadBinding,
}

const PLACES: &[Place] = &[
    Place::Nothing,
    Place::DeadFunction,
    Place::DeadStruct,
    Place::DeadTrait,
    Place::DeadImplementation,
    Place::UnreadBinding,
];

/// A definition, and the places in it of what a warning must be at, as
/// byte offsets with what the message mentions.
struct Item {
    text: String,
    marks: Vec<(usize, &'static str)>,
}

impl Item {
    fn plain(text: String) -> Item {
        Item {
            text,
            marks: Vec::new(),
        }
    }
}

/// A string literal of pieces that stand for valid text.
fn literal(g: &mut Gen) -> String {
    let pieces: String = (0..g.below(4)).map(|_| g.pick(PIECES).0).collect();
    format!("\"{pieces}\"")
}

/// A function's definition: its first line, its statements, and `}`. The
/// statement at `unread`, if any, is a binding nothing reads.
fn function(head: String, stmts: &[String], unread: Option<usize>) -> Item {
    let mut text = format!("{head} {{\n");
    let mut marks = Vec::new();
    for (i, stmt) in stmts.iter().enumerate() {
        text.push_str("  ");
        if unread == Some(i) {
            marks.push((text.len(), "`spare`"));
        }
        text.push_str(stmt);
        text.push('\n');
    }
    text.push('}');
    Item { text, marks }
}

/// A program with no warning: a few structs, now and then a trait with an
/// implementation, and a few functions, each of which `main` uses, with
/// bindings that are each read and parameters that may not be. Then the
/// one thing the generator places.
fn linted(g: &mut Gen) -> Linted {
    let place = *g.pick(PLACES);
    let structs = g.below(3);
    let with_trait = structs > 0 && g.one_in(2);
    let functions = g.below(4);
    let mut items = Vec::new();
    let mut main = Vec::new();
    for s in 0..structs {
        items.push(Item::plain(format!("struct S{s} {{\n  a: str\n}}")));
        main.push(format!("v{s} = S{s}(a: {})", literal(g)));
        main.push(if with_trait && s == 0 {
            "print(name_of(v0))".to_string()
        } else {
            format!("print(v{s}.a)")
        });
    }
    if with_trait {
        items.push(Item::plain(
            "trait Named[T] {\n  fn name_of(x: T) -> str\n}".to_string(),
        ));
        items.push(Item::plain(
            "impl Named[S0] {\n  fn name_of(x: S0) -> str { x.a }\n}".to_string(),
        ));
    }
    // The function the unread binding goes in, where `functions` is main.
    let unread_in = g.below(functions + 1);
    for f in 0..functions {
        let params: Vec<String> = (1..=g.below(3)).map(|p| format!("p{p}")).collect();
        let written: Vec<String> = params.iter().map(|p| format!("{p}: str")).collect();
        let head = format!("fn f{f}({}) -> str", written.join(", "));
        // Each binding is read by the next, and the last by the result.
        let mut stmts = Vec::new();
        let mut last = None;
        for b in 1..=g.below(3) {
            let value = match &last {
                Some(name) => name,
                None if !params.is_empty() && g.one_in(2) => g.pick(&params),
                None => &literal(g),
            };
            stmts.push(format!("b{b} = {value}"));
            last = Some(format!("b{b}"));
        }
        let unread = (place == Place::UnreadBinding && unread_in == f).then(|| {
            let at = g.below(stmts.len() + 1);
            stmts.insert(at, format!("spare = {}", literal(g)));
            at
        });
        stmts.push(match last {
            Some(name) => name,
            None if !params.is_empty() && g.one_in(2) => g.pick(&params).clone(),
            None => literal(g),
        });
        items.push(function(head, &stmts, unread));
        let args: Vec<String> = params.iter().map(|_| literal(g)).collect();
        main.push(format!("print(f{f}({}))", args.join(", ")));
    }
    if g.one_in(2) {
        main.push(format!("m = {}", literal(g)));
        main.push("print(m)".to_string());
    }
    if main.is_empty() {
        main.push(format!("print({})", literal(g)));
    }
    let unread = (place == Place::UnreadBinding && unread_in == functions).then(|| {
        let at = g.below(main.len() + 1);
        main.insert(at, format!("spare = {}", literal(g)));
        at
    });
    items.push(function("fn main() ! console".to_string(), &main, unread));
    let dead = match place {
        Place::Nothing | Place::UnreadBinding => None,
        Place::DeadFunction => Some(Item {
            text: g
                .pick(&[
                    "fn spare() -> str { \"a\" }",
                    "fn spare(p1: str) ! console { print(p1) }",
                ])
                .to_string(),
            marks: vec![(3, "dead code")],
        }),
        Place::DeadStruct => Some(Item {
            text: "struct Spare {\n  a: str\n}".to_string(),
            marks: vec![(7, "dead code")],
        }),
        Place::DeadTrait => Some(Item {
            text: "trait Spare[T] {\n  fn spare_of(x: T) -> str\n}".to_string(),
            marks: vec![(6, "dead code")],
        }),
        Place::DeadImplementation => {
            let text = "trait Spare[T] {\n  fn spare_of(x: T) -> str\n}\n\n\
                        impl Spare[str] {\n  fn spare_of(x: str) -> str { x }\n}";
            let implementation = text.find("impl").unwrap() + "impl ".len();
            Some(Item {
                text: text.to_string(),
                marks: vec![(6, "dead code"), (implementation, "dead code")],
            })
        }
    };
    if let Some(dead) = dead {
        let at = g.below(items.len() + 1);
        items.insert(at, dead);
    }
    // The definitions in any order, as a name can be used before it is
    // defined.
    let mut order = Vec::new();
    while !items.is_empty() {
        order.push(items.remove(g.below(items.len())));
    }
    let mut source = String::new();
    let mut warnings = Vec::new();
    for item in order {
        if !source.is_empty() {
            source.push_str("\n\n");
        }
        for (offset, mentions) in &item.marks {
            let before = format!("{source}{}", &item.text[..*offset]);
            warnings.push((pos_after(&before), *mentions));
        }
        source.push_str(&item.text);
    }
    source.push('\n');
    warnings.sort_by_key(|(pos, _)| *pos);
    Linted { source, warnings }
}

fn every_placed_warning_is_reported_by_check_alone_at_its_place() {
    generate::check(42, 60, linted, |linted| {
        let source = linted.source.as_bytes();
        let found: Vec<Error> = check(source).unwrap_or_else(|e| panic!("{e:?}"));
        let places: Vec<Pos> = found.iter().map(|w| w.pos).collect();
        let expected: Vec<Pos> = linted.warnings.iter().map(|(pos, _)| *pos).collect();
        assert_eq!(places, expected, "{found:?}");
        for (warning, (_, mentions)) in found.iter().zip(&linted.warnings) {
            assert!(warning.message.contains(mentions), "{warning}");
            assert!(warning.fix.is_some(), "{warning}");
        }
        compile(source, any()).unwrap_or_else(|e| panic!("{e:?}"));
    });
}
